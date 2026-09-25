/******************************************************************************
 *  File:       console_flash_manager.c
 *  Author:     Callum Rafferty
 *  Created:    15-Aug-2026
 *
 *  Description:
 *      Staged console hardware tests for the External Flash and Flash Manager
 *      stack. Commands deliberately use public production APIs so successful
 *      tests exercise QSPI, NAND, External Flash, Flash Manager buffers and the
 *      Flash Manager task rather than test-only storage paths.
 *
 *  Complete hardware test sequence (destructive):
 *
 *      1. `flash status`
 *         Require IDLE and inspect NAND page size, partition lengths/capacity,
 *         bad-block count, ECC state, QSPI busy state and echo-harness state.
 *
 *      2. `flash external_test [seed]`
 *         Exercise QSPI -> NAND -> External Flash directly. One full and one
 *         partial page are programmed and read back in both logical
 *         partitions. This overwrites both partitions.
 *
 *      3. `flash upload_test [instruction_count] [seed]`
 *         Exercise Host Interface -> Flash Manager -> External Flash. The
 *         default deterministic instruction stream crosses the three-page RAM
 *         ring and finishes on a partial page. Instruction N is assigned tick
 *         N and contains one diagnostic operation with twelve opaque bytes.
 *
 *      4. `flash prepare`
 *         Start a result session and preload instructions. Do not continue
 *         until the command reports FLASH_MANAGER_STATE_EXECUTING.
 *
 *      5. `flash execute_echo [100|1000|10000]`
 *         Temporarily route TIM4 to the console's genuine ISR test callback;
 *         100 Hz is the safe first-bring-up default. For every due instruction,
 *         the ISR peeks, reserves result storage, copies the twelve operation
 *         bytes, commits byte-compatible diagnostic metadata, and consumes the
 *         complete instruction. A future instruction is left unconsumed for
 *         the next tick. The command waits while the Flash Manager task
 *         concurrently refills/drains pages, stops TIM4, and restores the
 *         production callback.
 *
 *      6. `flash finalise`
 *         After execute_echo has returned, publish and drain the final partial
 *         result page and wait for RESULTS_READY.
 *
 *      7. `flash results verify`
 *         Exercise Flash Manager -> Host Interface retrieval and compare the
 *         entire packed result stream byte-for-byte with the diagnostic
 *         instruction stream. The command prints length and FNV-1a checksum,
 *         consumes the transfer, and returns Flash Manager to IDLE.
 *
 *      8. Repeat step 3 onward at 1 kHz and 10 kHz, then with larger record
 *         counts. This distinguishes functional correctness from whether NAND
 *         refill/drain throughput can sustain the intended execution rate.
 *
 *  Failure handling and limitations:
 *      - Never call a Flash Manager FromISR API directly from the console task.
 *        execute_echo uses a real priority-5 TIM4 interrupt for this reason.
 *      - TIM4 is shared with the Execution Manager. Do not run the production
 *        scheduler concurrently with either execution diagnostic.
 *      - TODO(Run State Manager): place this harness behind a diagnostic build
 *        flag and require exclusive execution ownership before replacing the
 *        TIM4 callback. The current console-only ownership rule is temporary.
 *      - NOT_BUFFERED means instruction refill missed its deadline; result
 *        reservation failure means result drain did not free capacity in time;
 *        a past timestamp means the execution schedule overran.
 *      - On execute_echo failure, do not use result verification. Inspect
 *        `flash status`, optionally finalise/read partial results, then reset.
 *        Flash Manager intentionally has no in-session fault recovery.
 *      - End-of-instruction-stream stops this diagnostic harness only. It does
 *        not redefine the production rule that test completion is controlled
 *        separately from instruction exhaustion.
 *      - To test empty-result finalisation, run prepare and finalise without
 *        execute_echo, then retrieve with `flash results` without `verify`.
 *      - The echo copy validates storage contracts, not peripheral dispatch,
 *        driver measurements, or worst-case Execution Manager timing.
 *      - No console output occurs in TIM4 or DMA interrupt context.
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include "console_flash_manager.h"

#include "console.h"
#include "external_flash.h"
#include "execution_manager.h"
#include "execution_operation_payloads.h"
#include "exec_analogue_input.h"
#include "exec_analogue_output.h"
#include "exec_can.h"
#include "exec_digital_input.h"
#include "exec_digital_output.h"
#include "exec_pwm_capture.h"
#include "exec_spi.h"
#include "exec_uart.h"
#include "flash_manager.h"
#include "hw_nand.h"
#include "hw_pwm_gen.h"
#include "hw_qspi.h"
#include "hw_timer.h"
#include "run_state_manager.h"
#include "rtos_config.h"
#include "test_configuration.h"
#ifndef TEST_BUILD
#include "stm32f4xx.h"
#endif

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */

#define CONSOLE_FLASH_STATE_TIMEOUT_MS ( 30000U )
#define CONSOLE_FLASH_PROGRESS_TIMEOUT_MS ( 30000U )
#define CONSOLE_FLASH_POLL_PERIOD_MS ( 10U )
#define CONSOLE_FLASH_RESULT_READ_BYTES ( 256U )
#define CONSOLE_FLASH_DIGITAL_INPUT_RESULT_BYTES                                                   \
    ( sizeof( FlashManagerResultHeader_T ) + sizeof( uint32_t ) )
#define CONSOLE_FLASH_ANALOGUE_INPUT_RESULT_BYTES                                                  \
    ( sizeof( FlashManagerResultHeader_T ) + ( 2U * sizeof( uint32_t ) ) )
#define CONSOLE_FLASH_PWM_CAPTURE_RESULT_BYTES                                                     \
    ( sizeof( FlashManagerResultHeader_T ) + ( 2U * sizeof( uint32_t ) ) )

#define CONSOLE_FLASH_TEST_PAYLOAD_BYTES ( 12U )
#define CONSOLE_FLASH_TEST_OPERATION_COUNT ( 1U )
#define CONSOLE_FLASH_DEFAULT_SEED ( 0x31U )
#define CONSOLE_FLASH_THROUGHPUT_DEFAULT_PAGES ( 1000U )
#define CONSOLE_FLASH_DO_TEST_INSTRUCTION_COUNT ( 2U )
#define CONSOLE_FLASH_DO_TEST_OPERATION_BYTES ( 12U )
#define CONSOLE_FLASH_DO_TEST_INSTRUCTION_BYTES ( 20U )
#define CONSOLE_FLASH_DO_TEST_DEFAULT_DELAY_TICKS ( 100U )
#define CONSOLE_FLASH_DO_TEST_DEFAULT_HIGH_TICKS ( 300U )
#define CONSOLE_FLASH_PWM_TEST_OPERATION_BYTES ( 12U )
#define CONSOLE_FLASH_PWM_TEST_INSTRUCTION_BYTES ( 20U )
#define CONSOLE_FLASH_AO_TEST_OPERATION_BYTES                                                      \
    ( EXECUTION_OPERATION_ENCODED_SIZE_BYTES( EXECUTION_ANALOGUE_OUTPUT_FRAME_SIZE_BYTES ) )
#define CONSOLE_FLASH_AO_TEST_INSTRUCTION_BYTES                                                    \
    ( sizeof( ExecutionInstructionHeader_T ) + CONSOLE_FLASH_AO_TEST_OPERATION_BYTES )
#define CONSOLE_FLASH_CAN_TEST_OPERATION_BYTES                                                     \
    ( EXECUTION_OPERATION_ENCODED_SIZE_BYTES( EXECUTION_CAN_PACKET_SIZE_BYTES ) )
#define CONSOLE_FLASH_CAN_TEST_INSTRUCTION_BYTES                                                   \
    ( sizeof( ExecutionInstructionHeader_T ) + CONSOLE_FLASH_CAN_TEST_OPERATION_BYTES )
#define CONSOLE_FLASH_OUTPUT_STRESS_DEFAULT_SAMPLES ( 1000U )
#define CONSOLE_FLASH_OUTPUT_STRESS_DEFAULT_INTERVAL_TICKS ( 1U )
#define CONSOLE_FLASH_OUTPUT_STRESS_DRAIN_TICKS ( 10U )
#define CONSOLE_FLASH_OUTPUT_STRESS_SPI1_BYTES ( 128U )
#define CONSOLE_FLASH_OUTPUT_STRESS_SPI2_BYTES ( 256U )
#define CONSOLE_FLASH_OUTPUT_STRESS_UART_BYTES ( 16U )
#define CONSOLE_FLASH_OUTPUT_STRESS_OPERATION_COUNT ( 7U )
#define CONSOLE_FLASH_OUTPUT_STRESS_PWM_FREQ_HZ ( 1000000U )
#define CONSOLE_FLASH_OUTPUT_STRESS_PWM_DUTY_PERMILLE ( 500U )
#define CONSOLE_FLASH_OUTPUT_STRESS_UART_BAUD ( 2000000U )
#define CONSOLE_FLASH_OUTPUT_STRESS_LOGICAL_DI_MASK                                                \
    ( ( 1UL << EXEC_DIGITAL_INPUT_CHANNEL_COUNT ) - 1UL )
/* Current board clock tree: TIM12 is APB1 x2; TIM8 is APB2 x2. */
#define CONSOLE_FLASH_PWM_LV_TIMER_CLOCK_HZ ( 90000000U )
#define CONSOLE_FLASH_PWM_HV_TIMER_CLOCK_HZ ( 180000000U )

/* Exact TIM4 divisors for the current 90 MHz timer clock. */
#define CONSOLE_FLASH_EXECUTION_100HZ_PSC ( 14U )
#define CONSOLE_FLASH_EXECUTION_100HZ_ARR ( 59999U )
#define CONSOLE_FLASH_EXECUTION_1KHZ_PSC ( 1U )
#define CONSOLE_FLASH_EXECUTION_1KHZ_ARR ( 44999U )
#define CONSOLE_FLASH_EXECUTION_10KHZ_PSC ( 0U )
#define CONSOLE_FLASH_EXECUTION_10KHZ_ARR ( 8999U )
#define CONSOLE_FLASH_EXECUTION_EXPECTED_TIMER_CLOCK_HZ ( 90000000U )
#define CONSOLE_FLASH_EXECUTION_DEFAULT_FREQUENCY_HZ ( 100U )
#define CONSOLE_FLASH_EXECUTION_TIMEOUT_MARGIN_MS ( 5000U )
#define CONSOLE_FLASH_EXECUTION_MINIMUM_TIMEOUT_MS ( 30000U )

#define CONSOLE_FLASH_FNV1A_OFFSET_BASIS ( UINT32_C( 2166136261 ) )
#define CONSOLE_FLASH_FNV1A_PRIME ( UINT32_C( 16777619 ) )

_Static_assert( sizeof( ExecutionInstructionHeader_T ) == sizeof( FlashManagerResultHeader_T ),
                "Echo verification requires identical packed header sizes" );
_Static_assert(
    CONSOLE_FLASH_PWM_TEST_OPERATION_BYTES
        == EXECUTION_OPERATION_ENCODED_SIZE_BYTES( EXECUTION_PWM_UPDATE_PAYLOAD_SIZE_BYTES ),
    "PWM test operation size must follow the canonical encoding" );
_Static_assert( CONSOLE_FLASH_PWM_TEST_INSTRUCTION_BYTES
                    == sizeof( ExecutionInstructionHeader_T )
                           + CONSOLE_FLASH_PWM_TEST_OPERATION_BYTES,
                "PWM test instruction size must follow the canonical encoding" );
_Static_assert( CONSOLE_FLASH_AO_TEST_OPERATION_BYTES == 8U,
                "Analogue-output test operation size must follow the canonical encoding" );
_Static_assert( CONSOLE_FLASH_AO_TEST_INSTRUCTION_BYTES == 16U,
                "Analogue-output test instruction size must follow the canonical encoding" );
_Static_assert( CONSOLE_FLASH_CAN_TEST_OPERATION_BYTES == 16U,
                "CAN test operation size must follow the canonical encoding" );
_Static_assert( CONSOLE_FLASH_CAN_TEST_INSTRUCTION_BYTES == 24U,
                "CAN test instruction size must follow the canonical encoding" );

/**-----------------------------------------------------------------------------
 *  Private Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

/** Console-visible state of the temporary TIM4 execution-API test harness. */
typedef enum
{
    CONSOLE_FLASH_EXECUTION_TEST_NOT_RUN = 0,
    CONSOLE_FLASH_EXECUTION_TEST_RUNNING,
    CONSOLE_FLASH_EXECUTION_TEST_COMPLETE,
    CONSOLE_FLASH_EXECUTION_TEST_FAILED
} ConsoleFlashExecutionTestState_T;

/** First terminal fault observed by the execution-API test harness. */
typedef enum
{
    CONSOLE_FLASH_EXECUTION_FAILURE_NONE = 0,
    CONSOLE_FLASH_EXECUTION_FAILURE_FLASH_MANAGER_STATE,
    CONSOLE_FLASH_EXECUTION_FAILURE_INSTRUCTION_NOT_BUFFERED,
    CONSOLE_FLASH_EXECUTION_FAILURE_INSTRUCTION_CORRUPT,
    CONSOLE_FLASH_EXECUTION_FAILURE_UNEXPECTED_INSTRUCTION,
    CONSOLE_FLASH_EXECUTION_FAILURE_TIMESTAMP_OVERRUN,
    CONSOLE_FLASH_EXECUTION_FAILURE_TICK_OVERFLOW,
    CONSOLE_FLASH_EXECUTION_FAILURE_RESULT_RESERVATION,
    CONSOLE_FLASH_EXECUTION_FAILURE_RESULT_COMMIT,
    CONSOLE_FLASH_EXECUTION_FAILURE_INSTRUCTION_CONSUME,
    CONSOLE_FLASH_EXECUTION_FAILURE_RECORD_COUNT,
    CONSOLE_FLASH_EXECUTION_FAILURE_TIMEOUT
} ConsoleFlashExecutionFailure_T;

/**
 * State shared between the console task and the temporary TIM4 callback.
 *
 * Every field read by both contexts is volatile. The ISR publishes diagnostic
 * fields before writing the terminal state; the console task cannot resume
 * until that ISR has returned.
 */
typedef struct
{
    volatile ConsoleFlashExecutionTestState_T    state;
    volatile ConsoleFlashExecutionFailure_T      failure;
    volatile uint32_t                            frequency_hz;
    volatile uint32_t                            expected_records;
    volatile uint32_t                            current_tick;
    volatile uint32_t                            timer_interrupts;
    volatile uint32_t                            instructions_consumed;
    volatile uint32_t                            future_instruction_deferrals;
    volatile FlashManagerInstructionReadStatus_T last_instruction_status;
    volatile FlashManagerResultCommitStatus_T    last_commit_status;
} ConsoleFlashExecutionTestContext_T;

#ifndef TEST_BUILD
typedef struct
{
    uint64_t total_cycles;
    uint32_t minimum_cycles;
    uint32_t maximum_cycles;
} ConsoleFlashPageTiming_T;
#endif

/**-----------------------------------------------------------------------------
 *  Private (static) Variables
 *------------------------------------------------------------------------------
 */

static uint8_t console_flash_write_buffer[EXTERNAL_FLASH_MAX_PAGE_SIZE_BYTES];
static uint8_t console_flash_read_buffer[EXTERNAL_FLASH_MAX_PAGE_SIZE_BYTES];

static uint32_t console_flash_last_upload_records   = 0U;
static uint32_t console_flash_last_upload_bytes     = 0U;
static uint8_t  console_flash_last_upload_seed      = 0U;
static uint32_t console_flash_run_tick_count        = 0U;
static uint32_t console_flash_stress_sample_count   = 0U;
static uint32_t console_flash_stress_interval_ticks = 0U;

/** Physical GPIOD bit positions for logical DI channels 1 through 10. */
static const uint8_t console_flash_stress_di_pin_positions[EXEC_DIGITAL_INPUT_CHANNEL_COUNT] = {
    8U, 9U, 10U, 11U, 14U, 15U, 0U, 1U, 2U, 3U,
};

typedef struct
{
    bool     valid;
    uint32_t first_tick;
    uint32_t interval_ticks;
    uint32_t repeat_words;
    uint32_t pattern_word;
    uint32_t run_ticks;
} ConsoleFlashDigitalPattern_T;

static ConsoleFlashDigitalPattern_T console_flash_digital_pattern = { 0 };

typedef struct
{
    bool     valid;
    uint32_t output_channel;
    uint32_t expected_millivolts;
    uint32_t update_tick;
    uint32_t run_ticks;
} ConsoleFlashAnalogueLoopback_T;

static ConsoleFlashAnalogueLoopback_T console_flash_analogue_loopback = { 0 };

typedef struct
{
    bool     valid;
    uint32_t output_channel;
    uint32_t frequency_hz;
    uint32_t duty_permille;
    uint32_t update_tick;
    uint32_t run_ticks;
} ConsoleFlashPwmLoopback_T;

static ConsoleFlashPwmLoopback_T console_flash_pwm_loopback = { 0 };

static ConsoleFlashExecutionTestContext_T console_flash_execution_test = {
    .state                        = CONSOLE_FLASH_EXECUTION_TEST_NOT_RUN,
    .failure                      = CONSOLE_FLASH_EXECUTION_FAILURE_NONE,
    .frequency_hz                 = 0U,
    .expected_records             = 0U,
    .current_tick                 = 0U,
    .timer_interrupts             = 0U,
    .instructions_consumed        = 0U,
    .future_instruction_deferrals = 0U,
    .last_instruction_status      = FLASH_MANAGER_INSTRUCTION_END_OF_STREAM,
    .last_commit_status           = FLASH_MANAGER_RESULT_COMMIT_OK,
};

/**-----------------------------------------------------------------------------
 *  Private (static) Function Prototypes
 *------------------------------------------------------------------------------
 */

static void        CONSOLE_Flash_PrintUsage( void );
static const char* CONSOLE_Flash_StateName( FlashManagerState_T state );
static bool        CONSOLE_Flash_ParseU32( const char* text, uint32_t* value );
static bool CONSOLE_Flash_WaitForState( FlashManagerState_T expected_state, uint32_t timeout_ms );
static bool CONSOLE_Flash_WaitForRunState( RunState_T expected_state, uint32_t timeout_ms );
static bool CONSOLE_Flash_HasTimedOut( TickType_t start_tick, uint32_t timeout_ms );
static bool CONSOLE_Flash_RequireIdle( void );
static const char* CONSOLE_Flash_ExecutionTestStateName( ConsoleFlashExecutionTestState_T state );
static const char* CONSOLE_Flash_ExecutionFailureName( ConsoleFlashExecutionFailure_T failure );
static bool     CONSOLE_Flash_GetExecutionTimerSettings( uint32_t frequency_hz, uint32_t* prescaler,
                                                         uint32_t* auto_reload );
static uint32_t CONSOLE_Flash_GetExecutionTimeoutMs( uint32_t record_count, uint32_t frequency_hz );
static void     CONSOLE_Flash_ResetExecutionHarnessState( void );
static void     CONSOLE_Flash_StopExecutionHarness( void );
static void     CONSOLE_Flash_EndExecutionHarnessFromISR( ConsoleFlashExecutionTestState_T state,
                                                          ConsoleFlashExecutionFailure_T   failure );
static void     CONSOLE_Flash_ExecutionEchoFromISR( BaseType_t* higher_priority_task_woken );
static void     CONSOLE_Flash_WriteU16Le( uint8_t* destination, uint16_t value );
static void     CONSOLE_Flash_WriteU32Le( uint8_t* destination, uint32_t value );
static void CONSOLE_Flash_EncodeDigitalOutputInstruction( uint8_t* destination, uint32_t timestamp,
                                                          uint32_t high_bitmask,
                                                          uint32_t low_bitmask );
static void CONSOLE_Flash_EncodePwmInstruction( uint8_t* destination, uint32_t timestamp,
                                                uint8_t                            channel,
                                                const ExecutionPwmUpdatePayload_T* payload );
static void
CONSOLE_Flash_EncodeAnalogueOutputInstruction( uint8_t* destination, uint32_t timestamp,
                                               const AnalogueOutputPreparedFrame_T* frame );
static uint32_t CONSOLE_Flash_EncodeCanInstruction( uint8_t* destination, uint32_t timestamp,
                                                    uint8_t channel, uint16_t id, uint8_t value,
                                                    uint8_t dlc );
static uint32_t CONSOLE_Flash_EncodeUartInstruction( uint8_t* destination, uint32_t timestamp,
                                                     uint8_t channel, uint8_t value,
                                                     uint16_t payload_length_bytes );
static uint32_t CONSOLE_Flash_EncodeSpiInstruction( uint8_t* destination, uint32_t timestamp,
                                                    uint8_t channel, uint8_t value,
                                                    uint32_t packet_length_bytes );
static uint32_t CONSOLE_Flash_EncodeOutputStressInstruction(
    uint8_t* destination, uint32_t timestamp, uint32_t high_bitmask, uint32_t low_bitmask,
    const ExecutionPwmUpdatePayload_T pwm_payloads[EXEC_PWM_GEN_CHANNEL_COUNT] );
static void     CONSOLE_Flash_FillPattern( uint8_t* destination, uint32_t stream_offset,
                                           uint32_t length, uint8_t seed );
static bool     CONSOLE_Flash_VerifyPattern( const uint8_t* data, uint32_t stream_offset,
                                             uint32_t length, uint8_t seed,
                                             uint32_t* first_bad_offset );
static void     CONSOLE_Flash_FillInstructionChunk( uint8_t* destination, uint32_t stream_offset,
                                                    uint32_t length, uint8_t seed );
static uint32_t CONSOLE_Flash_Fnv1aUpdate( uint32_t hash, const uint8_t* data, uint32_t length );
static uint32_t CONSOLE_Flash_StressLogicalPattern( uint32_t sample_index );
static uint32_t CONSOLE_Flash_StressDigitalInputMask( uint32_t logical_pattern );
static void     CONSOLE_Flash_PrintNandPhaseTiming( const char*                  label,
                                                    const HW_NAND_PhaseTiming_T* timing );
#ifndef TEST_BUILD
static void CONSOLE_Flash_RecordPageTiming( ConsoleFlashPageTiming_T* timing,
                                            uint32_t                  elapsed_cycles );
static void CONSOLE_Flash_PrintPageTiming( const char* label, uint32_t operation_count,
                                           uint32_t                        bytes_per_operation,
                                           const ConsoleFlashPageTiming_T* timing );
#endif

static void CONSOLE_Flash_StatusCommand( void );
static void CONSOLE_Flash_ExternalTestCommand( uint16_t argc, char* argv[] );
#ifndef TEST_BUILD
static void CONSOLE_Flash_ThroughputTestCommand( uint16_t argc, char* argv[] );
#endif
static void CONSOLE_Flash_UploadTestCommand( uint16_t argc, char* argv[] );
static void CONSOLE_Flash_UploadDigitalOutputTestCommand( uint16_t argc, char* argv[] );
static void CONSOLE_Flash_UploadDigitalPatternCommand( uint16_t argc, char* argv[] );
static void CONSOLE_Flash_UploadPwmTestCommand( uint16_t argc, char* argv[] );
static void CONSOLE_Flash_UploadAnalogueOutputTestCommand( uint16_t argc, char* argv[] );
static void CONSOLE_Flash_UploadCanTestCommand( uint16_t argc, char* argv[] );
static void CONSOLE_Flash_UploadSpiTestCommand( uint16_t argc, char* argv[] );
static void CONSOLE_Flash_UploadUartTestCommand( uint16_t argc, char* argv[] );
static void CONSOLE_Flash_VerifyUartLoopbackResultsCommand( uint16_t argc, char* argv[] );
static void CONSOLE_Flash_VerifySpiLoopbackResultsCommand( uint16_t argc, char* argv[] );
static void CONSOLE_Flash_VerifyCanLoopbackResultsCommand( uint16_t argc, char* argv[] );
static void CONSOLE_Flash_UploadOutputStressTestCommand( uint16_t argc, char* argv[] );
static void CONSOLE_Flash_PrepareCommand( void );
static void CONSOLE_Flash_ExecuteEchoCommand( uint16_t argc, char* argv[] );
static void CONSOLE_Flash_FinaliseCommand( void );
static void CONSOLE_Flash_ResultsCommand( bool verify_echo_stream );
static void CONSOLE_Flash_VerifyDigitalLoopbackResultsCommand( uint16_t argc, char* argv[] );
static void CONSOLE_Flash_VerifyAnalogueLoopbackResultsCommand( uint16_t argc, char* argv[] );
static void CONSOLE_Flash_VerifyPwmLoopbackResultsCommand( uint16_t argc, char* argv[] );
static void CONSOLE_Flash_VerifyStressResultsCommand( uint16_t argc, char* argv[] );

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

/** Prints the staged destructive-test workflow. */
static void CONSOLE_Flash_PrintUsage( void )
{
    CONSOLE_Printf( "Flash hardware bring-up (destructive):\r\n" );
    CONSOLE_Printf( "  flash status\r\n" );
    CONSOLE_Printf( "  flash external_test [seed]\r\n" );
#ifndef TEST_BUILD
    CONSOLE_Printf( "  flash throughput_test [page_count]\r\n" );
#endif
    CONSOLE_Printf( "  flash upload_test [instruction_count] [seed]\r\n" );
    CONSOLE_Printf( "  flash upload_do_test <channel 1..10> [delay_ticks] [high_ticks]\r\n" );
    CONSOLE_Printf( "  flash upload_do_pattern <channel> <first_tick> <interval_ticks> "
                    "<repeat_words> <hex_word> <run_ticks>\r\n" );
    CONSOLE_Printf( "  flash upload_pwm_test <channel 1..2> <frequency_hz> "
                    "<duty_permille> <update_tick> <run_ticks>\r\n" );
    CONSOLE_Printf( "  flash upload_ao_test <channel 0..5> <voltage> "
                    "<update_tick> <run_ticks>\r\n" );
    CONSOLE_Printf( "  flash upload_can_test <channel 1..2> <id 0..2047> <byte 0..255> "
                    "<dlc 0..8> <first_tick> <run_ticks> [repeat_count interval_ticks]\r\n" );
    CONSOLE_Printf( "  flash upload_spi_test <channel 1..2> <byte> <length> "
                    "<first_tick> <run_ticks> [repeat_count interval_ticks]\r\n" );
    CONSOLE_Printf( "  flash upload_uart_test <channel 1..2> <byte> <length> "
                    "<first_tick> <run_ticks> [repeat_count interval_ticks]\r\n" );
    CONSOLE_Printf( "  flash upload_output_stress [sample_count] [interval_ticks]\r\n" );
    CONSOLE_Printf( "  flash prepare\r\n" );
    CONSOLE_Printf( "  flash execute_echo [100|1000|10000]\r\n" );
    CONSOLE_Printf( "  flash finalise\r\n" );
    CONSOLE_Printf( "  flash results [verify]\r\n" );
    CONSOLE_Printf( "  flash results verify_stress\r\n" );
    CONSOLE_Printf( "  flash results verify_do_di <delay_ticks> <high_ticks>\r\n" );
    CONSOLE_Printf( "  flash results verify_do_pattern\r\n" );
    CONSOLE_Printf( "  flash results verify_ao_ai <input_channel 0..1>\r\n" );
    CONSOLE_Printf( "  flash results verify_pwm_capture <input_channel 1..2>\r\n" );
    CONSOLE_Printf( "  flash results verify_uart_loopback <channel 1..2> <byte> <length>\r\n" );
    CONSOLE_Printf( "  flash results verify_spi_loopback <channel 1..2> <byte> <length>\r\n" );
    CONSOLE_Printf( "  flash results verify_can_loopback <channel 1..2> <id> <byte> <dlc>\r\n" );
    CONSOLE_Printf( "Use 'flash status' after every phase. Reset after FAULT.\r\n" );
}

/** Returns a printable lifecycle state name. */
static const char* CONSOLE_Flash_StateName( FlashManagerState_T state )
{
    switch ( state )
    {
        case FLASH_MANAGER_STATE_UNINITIALISED:
            return "UNINITIALISED";
        case FLASH_MANAGER_STATE_IDLE:
            return "IDLE";
        case FLASH_MANAGER_STATE_PREPARING_INSTRUCTION_UPLOAD:
            return "PREPARING_INSTRUCTION_UPLOAD";
        case FLASH_MANAGER_STATE_INSTRUCTION_UPLOAD:
            return "INSTRUCTION_UPLOAD";
        case FLASH_MANAGER_STATE_FINALISING_INSTRUCTION_UPLOAD:
            return "FINALISING_INSTRUCTION_UPLOAD";
        case FLASH_MANAGER_STATE_PREPARING_EXECUTION:
            return "PREPARING_EXECUTION";
        case FLASH_MANAGER_STATE_EXECUTING:
            return "EXECUTING";
        case FLASH_MANAGER_STATE_FINALISING_RESULTS:
            return "FINALISING_RESULTS";
        case FLASH_MANAGER_STATE_RESULTS_READY:
            return "RESULTS_READY";
        case FLASH_MANAGER_STATE_TRANSFERRING_RESULTS:
            return "TRANSFERRING_RESULTS";
        case FLASH_MANAGER_STATE_ABORTING:
            return "ABORTING";
        case FLASH_MANAGER_STATE_FAULT:
            return "FAULT";
        default:
            return "UNKNOWN";
    }
}

/** Parses one unsigned decimal or 0x-prefixed console argument. */
static bool CONSOLE_Flash_ParseU32( const char* text, uint32_t* value )
{
    if ( ( text == NULL ) || ( value == NULL ) || ( text[0] == '\0' ) || ( text[0] == '-' ) )
    {
        return false;
    }

    char*              end = NULL;
    unsigned long long parsed;

    errno  = 0;
    parsed = strtoull( text, &end, 0 );

    if ( ( errno == ERANGE ) || ( end == text ) || ( end == NULL ) || ( *end != '\0' )
         || ( parsed > UINT32_MAX ) )
    {
        return false;
    }

    *value = ( uint32_t )parsed;
    return true;
}

/** Reports whether a bounded task-context wait has expired. */
static bool CONSOLE_Flash_HasTimedOut( TickType_t start_tick, uint32_t timeout_ms )
{
    TickType_t elapsed_ticks = xTaskGetTickCount() - start_tick;
    return elapsed_ticks >= pdMS_TO_TICKS( timeout_ms );
}

/** Waits for one Flash Manager lifecycle state while allowing its task to run. */
static bool CONSOLE_Flash_WaitForState( FlashManagerState_T expected_state, uint32_t timeout_ms )
{
    TickType_t start_tick = xTaskGetTickCount();

    for ( ;; )
    {
        FlashManagerState_T state = FLASH_MANAGER_STATE_UNINITIALISED;

        if ( !FLASH_MANAGER_GetState( &state ) )
        {
            return false;
        }

        if ( state == expected_state )
        {
            return true;
        }

        if ( state == FLASH_MANAGER_STATE_FAULT )
        {
            CONSOLE_Printf( "Flash Manager entered FAULT while waiting for %s\r\n",
                            CONSOLE_Flash_StateName( expected_state ) );
            return false;
        }

        if ( CONSOLE_Flash_HasTimedOut( start_tick, timeout_ms ) )
        {
            CONSOLE_Printf( "Timeout waiting for %s (current=%s)\r\n",
                            CONSOLE_Flash_StateName( expected_state ),
                            CONSOLE_Flash_StateName( state ) );
            return false;
        }

        vTaskDelay( pdMS_TO_TICKS( CONSOLE_FLASH_POLL_PERIOD_MS ) );
    }
}

/** Waits in console task context for an RSM-owned lifecycle transition. */
static bool CONSOLE_Flash_WaitForRunState( RunState_T expected_state, uint32_t timeout_ms )
{
    const TickType_t started_at = xTaskGetTickCount();

    for ( ;; )
    {
        RunStateManagerStatus_T status = { 0 };
        RUN_STATE_MANAGER_GetStatus( &status );
        if ( status.state == expected_state )
        {
            return true;
        }
        if ( status.state == RUN_STATE_FAULT
             || CONSOLE_Flash_HasTimedOut( started_at, timeout_ms ) )
        {
            return false;
        }
        vTaskDelay( pdMS_TO_TICKS( CONSOLE_FLASH_POLL_PERIOD_MS ) );
    }
}

/** Requires an initialised and idle Flash Manager before destructive direct tests. */
static bool CONSOLE_Flash_RequireIdle( void )
{
    FlashManagerState_T state = FLASH_MANAGER_STATE_UNINITIALISED;

    if ( !FLASH_MANAGER_GetState( &state ) )
    {
        CONSOLE_Printf( "Flash Manager startup initialisation failed.\r\n" );
        return false;
    }

    if ( state != FLASH_MANAGER_STATE_IDLE )
    {
        CONSOLE_Printf( "Flash Manager must be IDLE (current=%s).\r\n",
                        CONSOLE_Flash_StateName( state ) );
        return false;
    }

    return true;
}

/** Returns a printable execution-harness state name. */
static const char* CONSOLE_Flash_ExecutionTestStateName( ConsoleFlashExecutionTestState_T state )
{
    switch ( state )
    {
        case CONSOLE_FLASH_EXECUTION_TEST_NOT_RUN:
            return "NOT_RUN";
        case CONSOLE_FLASH_EXECUTION_TEST_RUNNING:
            return "RUNNING";
        case CONSOLE_FLASH_EXECUTION_TEST_COMPLETE:
            return "COMPLETE";
        case CONSOLE_FLASH_EXECUTION_TEST_FAILED:
            return "FAILED";
        default:
            return "UNKNOWN";
    }
}

/** Returns a concise diagnostic for the first execution-harness failure. */
static const char* CONSOLE_Flash_ExecutionFailureName( ConsoleFlashExecutionFailure_T failure )
{
    switch ( failure )
    {
        case CONSOLE_FLASH_EXECUTION_FAILURE_NONE:
            return "none";
        case CONSOLE_FLASH_EXECUTION_FAILURE_FLASH_MANAGER_STATE:
            return "Flash Manager left EXECUTING";
        case CONSOLE_FLASH_EXECUTION_FAILURE_INSTRUCTION_NOT_BUFFERED:
            return "instruction underrun";
        case CONSOLE_FLASH_EXECUTION_FAILURE_INSTRUCTION_CORRUPT:
            return "corrupt instruction stream";
        case CONSOLE_FLASH_EXECUTION_FAILURE_UNEXPECTED_INSTRUCTION:
            return "unexpected diagnostic instruction";
        case CONSOLE_FLASH_EXECUTION_FAILURE_TIMESTAMP_OVERRUN:
            return "instruction timestamp overrun";
        case CONSOLE_FLASH_EXECUTION_FAILURE_TICK_OVERFLOW:
            return "execution tick overflow";
        case CONSOLE_FLASH_EXECUTION_FAILURE_RESULT_RESERVATION:
            return "result reservation failed";
        case CONSOLE_FLASH_EXECUTION_FAILURE_RESULT_COMMIT:
            return "result commit failed";
        case CONSOLE_FLASH_EXECUTION_FAILURE_INSTRUCTION_CONSUME:
            return "instruction consume failed";
        case CONSOLE_FLASH_EXECUTION_FAILURE_RECORD_COUNT:
            return "instruction count mismatch";
        case CONSOLE_FLASH_EXECUTION_FAILURE_TIMEOUT:
            return "console wait timeout";
        default:
            return "unknown";
    }
}

/** Maps the supported bring-up frequencies to the production TIM4 divisors. */
static bool CONSOLE_Flash_GetExecutionTimerSettings( uint32_t frequency_hz, uint32_t* prescaler,
                                                     uint32_t* auto_reload )
{
    if ( ( prescaler == NULL ) || ( auto_reload == NULL ) )
    {
        return false;
    }

    switch ( frequency_hz )
    {
        case 100U:
            *prescaler   = CONSOLE_FLASH_EXECUTION_100HZ_PSC;
            *auto_reload = CONSOLE_FLASH_EXECUTION_100HZ_ARR;
            return true;
        case 1000U:
            *prescaler   = CONSOLE_FLASH_EXECUTION_1KHZ_PSC;
            *auto_reload = CONSOLE_FLASH_EXECUTION_1KHZ_ARR;
            return true;
        case 10000U:
            *prescaler   = CONSOLE_FLASH_EXECUTION_10KHZ_PSC;
            *auto_reload = CONSOLE_FLASH_EXECUTION_10KHZ_ARR;
            return true;
        default:
            return false;
    }
}

/** Calculates a bounded wait from the generated record schedule. */
static uint32_t CONSOLE_Flash_GetExecutionTimeoutMs( uint32_t record_count, uint32_t frequency_hz )
{
    uint64_t required_ticks = record_count;
    uint64_t expected_duration_ms =
        ( ( required_ticks * 1000U ) + frequency_hz - 1U ) / frequency_hz;
    uint64_t timeout_ms = ( expected_duration_ms * 2U ) + CONSOLE_FLASH_EXECUTION_TIMEOUT_MARGIN_MS;

    if ( timeout_ms < CONSOLE_FLASH_EXECUTION_MINIMUM_TIMEOUT_MS )
    {
        timeout_ms = CONSOLE_FLASH_EXECUTION_MINIMUM_TIMEOUT_MS;
    }

    if ( timeout_ms > UINT32_MAX )
    {
        timeout_ms = UINT32_MAX;
    }

    return ( uint32_t )timeout_ms;
}

/** Clears task-owned diagnostics before a new deterministic upload or run. */
static void CONSOLE_Flash_ResetExecutionHarnessState( void )
{
    console_flash_execution_test.state                 = CONSOLE_FLASH_EXECUTION_TEST_NOT_RUN;
    console_flash_execution_test.failure               = CONSOLE_FLASH_EXECUTION_FAILURE_NONE;
    console_flash_execution_test.frequency_hz          = 0U;
    console_flash_execution_test.expected_records      = 0U;
    console_flash_execution_test.current_tick          = 0U;
    console_flash_execution_test.timer_interrupts      = 0U;
    console_flash_execution_test.instructions_consumed = 0U;
    console_flash_execution_test.future_instruction_deferrals = 0U;
    console_flash_execution_test.last_instruction_status = FLASH_MANAGER_INSTRUCTION_END_OF_STREAM;
    console_flash_execution_test.last_commit_status      = FLASH_MANAGER_RESULT_COMMIT_OK;
}

/** Stops TIM4 and atomically restores its production Execution Manager route. */
static void CONSOLE_Flash_StopExecutionHarness( void )
{
    taskENTER_CRITICAL();
    HW_TIMER_Stop_Timer( EXECUTION_MANAGER_TIMER );
    HW_TIMER_Set_Execution_Callback( NULL );
    taskEXIT_CRITICAL();
}

/** Publishes a terminal harness result and prevents any further test ticks. */
static void CONSOLE_Flash_EndExecutionHarnessFromISR( ConsoleFlashExecutionTestState_T state,
                                                      ConsoleFlashExecutionFailure_T   failure )
{
    HW_TIMER_Stop_Timer( EXECUTION_MANAGER_TIMER );
    console_flash_execution_test.failure = failure;
    console_flash_execution_test.state   = state;
}

/**
 * Exercises the complete execution-facing Flash Manager API from TIM4.
 *
 * This callback deliberately emulates only the storage behaviour required of
 * the Execution Manager storage contract. It does not dispatch a peripheral driver. For
 * each diagnostic instruction due on the current tick it reserves result
 * payload storage, copies the opaque operation bytes into that storage, commits
 * a byte-compatible diagnostic result header, and consumes the instruction.
 * The later Host Interface readback therefore validates both packed streams
 * byte-for-byte without interpreting an operation.
 */
static void CONSOLE_Flash_ExecutionEchoFromISR( BaseType_t* higher_priority_task_woken )
{
    if ( console_flash_execution_test.state != CONSOLE_FLASH_EXECUTION_TEST_RUNNING )
    {
        return;
    }

    console_flash_execution_test.timer_interrupts++;

    for ( ;; )
    {
        const FlashManagerInstructionView_T* instruction = NULL;
        FlashManagerInstructionReadStatus_T  instruction_status =
            FLASH_MANAGER_PeekNextInstructionFromISR( &instruction );

        console_flash_execution_test.last_instruction_status = instruction_status;

        if ( instruction_status == FLASH_MANAGER_INSTRUCTION_END_OF_STREAM )
        {
            if ( console_flash_execution_test.instructions_consumed
                 == console_flash_execution_test.expected_records )
            {
                CONSOLE_Flash_EndExecutionHarnessFromISR( CONSOLE_FLASH_EXECUTION_TEST_COMPLETE,
                                                          CONSOLE_FLASH_EXECUTION_FAILURE_NONE );
            }
            else
            {
                CONSOLE_Flash_EndExecutionHarnessFromISR(
                    CONSOLE_FLASH_EXECUTION_TEST_FAILED,
                    CONSOLE_FLASH_EXECUTION_FAILURE_RECORD_COUNT );
            }
            break;
        }

        if ( instruction_status == FLASH_MANAGER_INSTRUCTION_NOT_BUFFERED )
        {
            CONSOLE_Flash_EndExecutionHarnessFromISR(
                CONSOLE_FLASH_EXECUTION_TEST_FAILED,
                CONSOLE_FLASH_EXECUTION_FAILURE_INSTRUCTION_NOT_BUFFERED );
            break;
        }

        if ( ( instruction_status != FLASH_MANAGER_INSTRUCTION_AVAILABLE )
             || ( instruction == NULL ) )
        {
            CONSOLE_Flash_EndExecutionHarnessFromISR(
                CONSOLE_FLASH_EXECUTION_TEST_FAILED,
                CONSOLE_FLASH_EXECUTION_FAILURE_INSTRUCTION_CORRUPT );
            break;
        }

        uint32_t record_index       = console_flash_execution_test.instructions_consumed;
        uint32_t expected_timestamp = record_index;

        if ( ( instruction->header.timestamp != expected_timestamp )
             || ( instruction->header.operations_length_bytes != CONSOLE_FLASH_TEST_PAYLOAD_BYTES )
             || ( instruction->header.operation_count != CONSOLE_FLASH_TEST_OPERATION_COUNT )
             || ( instruction->header.reserved != 0U ) )
        {
            CONSOLE_Flash_EndExecutionHarnessFromISR(
                CONSOLE_FLASH_EXECUTION_TEST_FAILED,
                CONSOLE_FLASH_EXECUTION_FAILURE_UNEXPECTED_INSTRUCTION );
            break;
        }

        if ( instruction->header.timestamp > console_flash_execution_test.current_tick )
        {
            console_flash_execution_test.future_instruction_deferrals++;

            if ( console_flash_execution_test.current_tick == UINT32_MAX )
            {
                CONSOLE_Flash_EndExecutionHarnessFromISR(
                    CONSOLE_FLASH_EXECUTION_TEST_FAILED,
                    CONSOLE_FLASH_EXECUTION_FAILURE_TICK_OVERFLOW );
            }
            else
            {
                console_flash_execution_test.current_tick++;
            }
            break;
        }

        if ( instruction->header.timestamp < console_flash_execution_test.current_tick )
        {
            CONSOLE_Flash_EndExecutionHarnessFromISR(
                CONSOLE_FLASH_EXECUTION_TEST_FAILED,
                CONSOLE_FLASH_EXECUTION_FAILURE_TIMESTAMP_OVERRUN );
            break;
        }

        FlashManagerResultWriteLease_T lease;
        if ( !FLASH_MANAGER_ReserveResultRecordFromISR( instruction->header.operations_length_bytes,
                                                        &lease ) )
        {
            CONSOLE_Flash_EndExecutionHarnessFromISR(
                CONSOLE_FLASH_EXECUTION_TEST_FAILED,
                CONSOLE_FLASH_EXECUTION_FAILURE_RESULT_RESERVATION );
            break;
        }

        memcpy( lease.payload, instruction->operations,
                instruction->header.operations_length_bytes );

        FlashManagerResultCommitStatus_T commit_status = FLASH_MANAGER_CommitResultRecordFromISR(
            &lease, instruction->header.timestamp, instruction->header.operation_count,
            instruction->header.reserved, instruction->header.operations_length_bytes,
            higher_priority_task_woken );

        console_flash_execution_test.last_commit_status = commit_status;

        if ( commit_status != FLASH_MANAGER_RESULT_COMMIT_OK )
        {
            /* Best effort only: some commit failures may already invalidate the lease. */
            ( void )FLASH_MANAGER_CancelResultRecordFromISR( &lease );
            CONSOLE_Flash_EndExecutionHarnessFromISR(
                CONSOLE_FLASH_EXECUTION_TEST_FAILED,
                CONSOLE_FLASH_EXECUTION_FAILURE_RESULT_COMMIT );
            break;
        }

        if ( !FLASH_MANAGER_ConsumeInstructionFromISR( higher_priority_task_woken ) )
        {
            CONSOLE_Flash_EndExecutionHarnessFromISR(
                CONSOLE_FLASH_EXECUTION_TEST_FAILED,
                CONSOLE_FLASH_EXECUTION_FAILURE_INSTRUCTION_CONSUME );
            break;
        }

        console_flash_execution_test.instructions_consumed++;
    }
}

/** Generates a reproducible byte pattern for direct External Flash verification. */
static void CONSOLE_Flash_FillPattern( uint8_t* destination, uint32_t stream_offset,
                                       uint32_t length, uint8_t seed )
{
    for ( uint32_t index = 0U; index < length; index++ )
    {
        destination[index] = ( uint8_t )( seed + ( uint8_t )( ( stream_offset + index ) * 37U ) );
    }
}

/** Locates the first mismatch in a generated direct-storage test pattern. */
static bool CONSOLE_Flash_VerifyPattern( const uint8_t* data, uint32_t stream_offset,
                                         uint32_t length, uint8_t seed, uint32_t* first_bad_offset )
{
    for ( uint32_t index = 0U; index < length; index++ )
    {
        uint8_t expected = ( uint8_t )( seed + ( uint8_t )( ( stream_offset + index ) * 37U ) );

        if ( data[index] != expected )
        {
            if ( first_bad_offset != NULL )
            {
                *first_bad_offset = stream_offset + index;
            }
            return false;
        }
    }

    return true;
}

/** Generates a slice of the deterministic framing-compatible diagnostic stream. */
static void CONSOLE_Flash_FillInstructionChunk( uint8_t* destination, uint32_t stream_offset,
                                                uint32_t length, uint8_t seed )
{
    const uint32_t header_length_bytes = ( uint32_t )sizeof( ExecutionInstructionHeader_T );
    const uint32_t record_length_bytes = header_length_bytes + CONSOLE_FLASH_TEST_PAYLOAD_BYTES;
    const uint32_t encoded_fields      = ( uint32_t )CONSOLE_FLASH_TEST_PAYLOAD_BYTES
                                    | ( ( uint32_t )CONSOLE_FLASH_TEST_OPERATION_COUNT << 16U );

    for ( uint32_t output_index = 0U; output_index < length; output_index++ )
    {
        uint32_t absolute_offset = stream_offset + output_index;
        uint32_t record_index    = absolute_offset / record_length_bytes;
        uint32_t record_offset   = absolute_offset % record_length_bytes;

        if ( record_offset < sizeof( uint32_t ) )
        {
            destination[output_index] = ( uint8_t )( record_index >> ( record_offset * 8U ) );
        }
        else if ( record_offset < header_length_bytes )
        {
            uint32_t field_byte_offset = record_offset - sizeof( uint32_t );
            destination[output_index] = ( uint8_t )( encoded_fields >> ( field_byte_offset * 8U ) );
        }
        else
        {
            uint32_t payload_offset = record_offset - header_length_bytes;
            destination[output_index] =
                ( uint8_t )( seed + ( uint8_t )( record_index * 13U ) + ( uint8_t )payload_offset );
        }
    }
}

/** Extends an FNV-1a checksum without retaining the complete result stream. */
static uint32_t CONSOLE_Flash_Fnv1aUpdate( uint32_t hash, const uint8_t* data, uint32_t length )
{
    for ( uint32_t index = 0U; index < length; index++ )
    {
        hash ^= data[index];
        hash *= CONSOLE_FLASH_FNV1A_PRIME;
    }

    return hash;
}

/** Returns the channel-distinguishing logical DO pattern for one stress instruction. */
static uint32_t CONSOLE_Flash_StressLogicalPattern( uint32_t sample_index )
{
    return ( sample_index + 1U ) & CONSOLE_FLASH_OUTPUT_STRESS_LOGICAL_DI_MASK;
}

/** Translates logical DI channel bits into the raw GPIOD representation stored in results. */
static uint32_t CONSOLE_Flash_StressDigitalInputMask( uint32_t logical_pattern )
{
    uint32_t physical_mask = 0U;
    for ( uint32_t channel = 0U; channel < EXEC_DIGITAL_INPUT_CHANNEL_COUNT; channel++ )
    {
        if ( ( logical_pattern & ( 1UL << channel ) ) != 0U )
        {
            physical_mask |= 1UL << console_flash_stress_di_pin_positions[channel];
        }
    }
    return physical_mask;
}

static void CONSOLE_Flash_PrintNandPhaseTiming( const char*                  label,
                                                const HW_NAND_PhaseTiming_T* timing )
{
    const uint32_t average_cycles =
        timing->samples == 0U ? 0U : ( uint32_t )( timing->total_cycles / timing->samples );

    CONSOLE_Printf( "NAND phase %s: samples=%lu latest=%lu avg=%lu max=%lu cycles\r\n", label,
                    ( unsigned long )timing->samples, ( unsigned long )timing->latest_cycles,
                    ( unsigned long )average_cycles, ( unsigned long )timing->maximum_cycles );
}

#ifndef TEST_BUILD
static void CONSOLE_Flash_RecordPageTiming( ConsoleFlashPageTiming_T* timing,
                                            uint32_t                  elapsed_cycles )
{
    timing->total_cycles += elapsed_cycles;
    if ( elapsed_cycles < timing->minimum_cycles )
    {
        timing->minimum_cycles = elapsed_cycles;
    }
    if ( elapsed_cycles > timing->maximum_cycles )
    {
        timing->maximum_cycles = elapsed_cycles;
    }
}

static void CONSOLE_Flash_PrintPageTiming( const char* label, uint32_t operation_count,
                                           uint32_t                        bytes_per_operation,
                                           const ConsoleFlashPageTiming_T* timing )
{
    const uint32_t average_cycles =
        operation_count == 0U ? 0U : ( uint32_t )( timing->total_cycles / operation_count );
    const uint64_t total_bytes = ( uint64_t )operation_count * bytes_per_operation;
    const uint32_t bytes_per_second =
        timing->total_cycles == 0U
            ? 0U
            : ( uint32_t )( ( total_bytes * SystemCoreClock ) / timing->total_cycles );

    CONSOLE_Printf( "%s: operations=%lu avg=%lu min=%lu max=%lu cycles, "
                    "%lu.%03lu MB/s\r\n",
                    label, ( unsigned long )operation_count, ( unsigned long )average_cycles,
                    ( unsigned long )timing->minimum_cycles,
                    ( unsigned long )timing->maximum_cycles,
                    ( unsigned long )( bytes_per_second / 1000000U ),
                    ( unsigned long )( ( bytes_per_second % 1000000U ) / 1000U ) );
}
#endif

/** Prints Flash Manager lifecycle and External Flash/NAND diagnostic state. */
static void CONSOLE_Flash_StatusCommand( void )
{
    FlashManagerState_T state = FLASH_MANAGER_STATE_UNINITIALISED;
    if ( FLASH_MANAGER_GetState( &state ) )
    {
        CONSOLE_Printf( "Flash Manager: %s\r\n", CONSOLE_Flash_StateName( state ) );
    }
    else
    {
        CONSOLE_Printf( "Flash Manager: not initialised\r\n" );
    }

    ExternalFlashInfo_T   info        = { 0 };
    ExternalFlashStatus_T info_status = EXTERNAL_FLASH_GetInfo( &info );
    if ( info_status == EXTERNAL_FLASH_STATUS_OK )
    {
        CONSOLE_Printf( "Page size: %lu, bad blocks: %lu\r\n",
                        ( unsigned long )info.page_size_bytes,
                        ( unsigned long )info.bad_block_count );
        CONSOLE_Printf( "Instruction: %lu / %lu bytes\r\n",
                        ( unsigned long )info.instruction_length_bytes,
                        ( unsigned long )info.instruction_capacity_bytes );
        CONSOLE_Printf( "Results: %lu / %lu bytes\r\n", ( unsigned long )info.result_length_bytes,
                        ( unsigned long )info.result_capacity_bytes );
    }
    else
    {
        CONSOLE_Printf( "External Flash: unavailable (status=%d)\r\n", ( int )info_status );
    }

    HW_NAND_EccStatus_T ecc_status  = HW_NAND_ECC_STATUS_UNKNOWN;
    HW_NAND_Status_T    nand_status = HW_NAND_GetLastEccStatus( &ecc_status );
    CONSOLE_Printf( "NAND ECC: status=%d value=%d, QSPI busy=%u\r\n", ( int )nand_status,
                    ( int )ecc_status, HW_QSPI_IsBusy() ? 1U : 0U );

    HW_NAND_PerformanceDiagnostics_T nand_diagnostics = { 0 };
    if ( HW_NAND_GetPerformanceDiagnostics( &nand_diagnostics ) )
    {
        CONSOLE_Flash_PrintNandPhaseTiming( "read-array", &nand_diagnostics.read_array_to_cache );
        CONSOLE_Flash_PrintNandPhaseTiming( "read-DMA", &nand_diagnostics.read_cache_dma );
        CONSOLE_Flash_PrintNandPhaseTiming( "program-load-DMA",
                                            &nand_diagnostics.program_load_dma );
        CONSOLE_Flash_PrintNandPhaseTiming( "program-execute", &nand_diagnostics.program_execute );
    }

    FlashManagerExecutionDiagnostics_T diagnostics = { 0 };
    if ( FLASH_MANAGER_GetExecutionDiagnostics( &diagnostics ) )
    {
        const uint32_t average_drain_cycles =
            diagnostics.result_pages_drained == 0U
                ? 0U
                : ( uint32_t )( diagnostics.result_page_drain_total_cycles
                                / diagnostics.result_pages_drained );
        const uint32_t average_refill_cycles =
            diagnostics.instruction_pages_refilled == 0U
                ? 0U
                : ( uint32_t )( diagnostics.instruction_page_refill_total_cycles
                                / diagnostics.instruction_pages_refilled );
        CONSOLE_Printf( "Execution drain: pages=%lu latest=%lu avg=%lu max=%lu cycles, "
                        "pending=%lu peak=%lu bytes\r\n",
                        ( unsigned long )diagnostics.result_pages_drained,
                        ( unsigned long )diagnostics.result_page_drain_latest_cycles,
                        ( unsigned long )average_drain_cycles,
                        ( unsigned long )diagnostics.result_page_drain_max_cycles,
                        ( unsigned long )diagnostics.current_pending_result_bytes,
                        ( unsigned long )diagnostics.peak_pending_result_bytes );
        CONSOLE_Printf( "Execution buffer: reserve_failures=%lu last_request=%u last_free=%lu, "
                        "commit_failures=%lu last_commit=%d\r\n",
                        ( unsigned long )diagnostics.result_reserve_failures,
                        ( unsigned int )diagnostics.last_failed_reserve_payload_bytes,
                        ( unsigned long )diagnostics.free_bytes_at_last_reserve_failure,
                        ( unsigned long )diagnostics.result_commit_failures,
                        ( int )diagnostics.last_commit_failure );
        CONSOLE_Printf( "Execution refill: pages=%lu latest=%lu avg=%lu max=%lu cycles\r\n",
                        ( unsigned long )diagnostics.instruction_pages_refilled,
                        ( unsigned long )diagnostics.instruction_page_refill_latest_cycles,
                        ( unsigned long )average_refill_cycles,
                        ( unsigned long )diagnostics.instruction_page_refill_max_cycles );
        const uint32_t average_publish_cycles =
            diagnostics.instruction_page_publish_samples == 0U
                ? 0U
                : ( uint32_t )( diagnostics.instruction_page_publish_total_cycles
                                / diagnostics.instruction_page_publish_samples );
        const uint32_t average_service_gap_cycles =
            diagnostics.nand_service_gap_samples == 0U
                ? 0U
                : ( uint32_t )( diagnostics.nand_service_gap_total_cycles
                                / diagnostics.nand_service_gap_samples );
        CONSOLE_Printf( "Instruction publish: samples=%lu latest=%lu avg=%lu max=%lu cycles\r\n",
                        ( unsigned long )diagnostics.instruction_page_publish_samples,
                        ( unsigned long )diagnostics.instruction_page_publish_latest_cycles,
                        ( unsigned long )average_publish_cycles,
                        ( unsigned long )diagnostics.instruction_page_publish_max_cycles );
        CONSOLE_Printf( "NAND service gap: samples=%lu latest=%lu avg=%lu max=%lu cycles\r\n",
                        ( unsigned long )diagnostics.nand_service_gap_samples,
                        ( unsigned long )diagnostics.nand_service_gap_latest_cycles,
                        ( unsigned long )average_service_gap_cycles,
                        ( unsigned long )diagnostics.nand_service_gap_max_cycles );
        CONSOLE_Printf( "Execution arbitration: contentions=%lu\r\n",
                        ( unsigned long )diagnostics.refill_drain_contentions );
    }

    if ( console_flash_last_upload_records != 0U )
    {
        CONSOLE_Printf( "Last upload test: instructions=%lu bytes=%lu seed=0x%02X\r\n",
                        ( unsigned long )console_flash_last_upload_records,
                        ( unsigned long )console_flash_last_upload_bytes,
                        ( unsigned int )console_flash_last_upload_seed );
    }

    CONSOLE_Printf( "Execution echo: %s, failure=%s, frequency=%lu Hz, "
                    "tick=%lu, consumed=%lu/%lu\r\n",
                    CONSOLE_Flash_ExecutionTestStateName( console_flash_execution_test.state ),
                    CONSOLE_Flash_ExecutionFailureName( console_flash_execution_test.failure ),
                    ( unsigned long )console_flash_execution_test.frequency_hz,
                    ( unsigned long )console_flash_execution_test.current_tick,
                    ( unsigned long )console_flash_execution_test.instructions_consumed,
                    ( unsigned long )console_flash_execution_test.expected_records );
}

/** Programs and verifies full and partial pages in both logical partitions. */
static void CONSOLE_Flash_ExternalTestCommand( uint16_t argc, char* argv[] )
{
    console_flash_run_tick_count = 0U;
    if ( !CONSOLE_Flash_RequireIdle() )
    {
        return;
    }

    uint32_t seed_value = CONSOLE_FLASH_DEFAULT_SEED;
    if ( ( argc == 3U ) && !CONSOLE_Flash_ParseU32( argv[2], &seed_value ) )
    {
        CONSOLE_Printf( "Invalid seed. Use decimal or 0x-prefixed byte value.\r\n" );
        return;
    }
    if ( ( argc > 3U ) || ( seed_value > UINT8_MAX ) )
    {
        CONSOLE_Printf( "Usage: flash external_test [seed 0..255]\r\n" );
        return;
    }

    ExternalFlashInfo_T   info   = { 0 };
    ExternalFlashStatus_T status = EXTERNAL_FLASH_GetInfo( &info );
    if ( status != EXTERNAL_FLASH_STATUS_OK )
    {
        CONSOLE_Printf( "External Flash GetInfo failed (status=%d).\r\n", ( int )status );
        return;
    }

    if ( ( info.page_size_bytes == 0U )
         || ( info.page_size_bytes > sizeof( console_flash_write_buffer ) ) )
    {
        CONSOLE_Printf( "Unsupported NAND page size: %lu.\r\n",
                        ( unsigned long )info.page_size_bytes );
        return;
    }

    uint32_t partial_length   = ( info.page_size_bytes > 37U ) ? 37U : 1U;
    uint32_t expected_length  = info.page_size_bytes + partial_length;
    uint8_t  seed             = ( uint8_t )seed_value;
    uint32_t first_bad_offset = 0U;

    CONSOLE_Printf( "External Flash test starting; existing data will be overwritten.\r\n" );

    status = EXTERNAL_FLASH_StartInstructionUpload( expected_length );
    if ( status != EXTERNAL_FLASH_STATUS_OK )
    {
        CONSOLE_Printf( "Instruction upload start failed (status=%d).\r\n", ( int )status );
        return;
    }

    /* Direct partition writes invalidate any earlier deterministic echo image. */
    console_flash_last_upload_records = 0U;
    console_flash_last_upload_bytes   = 0U;
    CONSOLE_Flash_ResetExecutionHarnessState();

    CONSOLE_Flash_FillPattern( console_flash_write_buffer, 0U, info.page_size_bytes, seed );
    status =
        EXTERNAL_FLASH_WriteInstructionPage( console_flash_write_buffer, info.page_size_bytes );
    if ( status != EXTERNAL_FLASH_STATUS_OK )
    {
        CONSOLE_Printf( "Full instruction page write failed (status=%d).\r\n", ( int )status );
        return;
    }

    CONSOLE_Flash_FillPattern( console_flash_write_buffer, info.page_size_bytes, partial_length,
                               seed );
    status = EXTERNAL_FLASH_WriteInstructionPage( console_flash_write_buffer, partial_length );
    if ( status != EXTERNAL_FLASH_STATUS_OK )
    {
        CONSOLE_Printf( "Partial instruction page write failed (status=%d).\r\n", ( int )status );
        return;
    }

    status = EXTERNAL_FLASH_FinishInstructionUpload();
    if ( status != EXTERNAL_FLASH_STATUS_OK )
    {
        CONSOLE_Printf( "Instruction upload finish failed (status=%d).\r\n", ( int )status );
        return;
    }

    status =
        EXTERNAL_FLASH_ReadInstructionPage( 0U, console_flash_read_buffer, info.page_size_bytes );
    if ( ( status != EXTERNAL_FLASH_STATUS_OK )
         || !CONSOLE_Flash_VerifyPattern( console_flash_read_buffer, 0U, info.page_size_bytes, seed,
                                          &first_bad_offset ) )
    {
        CONSOLE_Printf( "Instruction full-page verify failed: status=%d offset=%lu.\r\n",
                        ( int )status, ( unsigned long )first_bad_offset );
        return;
    }

    status = EXTERNAL_FLASH_ReadInstructionPage( info.page_size_bytes, console_flash_read_buffer,
                                                 partial_length );
    if ( ( status != EXTERNAL_FLASH_STATUS_OK )
         || !CONSOLE_Flash_VerifyPattern( console_flash_read_buffer, info.page_size_bytes,
                                          partial_length, seed, &first_bad_offset ) )
    {
        CONSOLE_Printf( "Instruction partial-page verify failed: status=%d offset=%lu.\r\n",
                        ( int )status, ( unsigned long )first_bad_offset );
        return;
    }

    uint8_t result_seed = ( uint8_t )( seed ^ 0xA5U );
    status              = EXTERNAL_FLASH_StartSession( info.page_size_bytes + partial_length );
    if ( status != EXTERNAL_FLASH_STATUS_OK )
    {
        CONSOLE_Printf( "Result session start failed (status=%d).\r\n", ( int )status );
        return;
    }

    CONSOLE_Flash_FillPattern( console_flash_write_buffer, 0U, info.page_size_bytes, result_seed );
    status = EXTERNAL_FLASH_WriteResultPage( console_flash_write_buffer, info.page_size_bytes );
    if ( status != EXTERNAL_FLASH_STATUS_OK )
    {
        CONSOLE_Printf( "Full result page write failed (status=%d).\r\n", ( int )status );
        return;
    }

    CONSOLE_Flash_FillPattern( console_flash_write_buffer, info.page_size_bytes, partial_length,
                               result_seed );
    status = EXTERNAL_FLASH_WriteResultPage( console_flash_write_buffer, partial_length );
    if ( status != EXTERNAL_FLASH_STATUS_OK )
    {
        CONSOLE_Printf( "Partial result page write failed (status=%d).\r\n", ( int )status );
        return;
    }

    status = EXTERNAL_FLASH_ReadResultPage( 0U, console_flash_read_buffer, info.page_size_bytes );
    if ( ( status != EXTERNAL_FLASH_STATUS_OK )
         || !CONSOLE_Flash_VerifyPattern( console_flash_read_buffer, 0U, info.page_size_bytes,
                                          result_seed, &first_bad_offset ) )
    {
        CONSOLE_Printf( "Result full-page verify failed: status=%d offset=%lu.\r\n", ( int )status,
                        ( unsigned long )first_bad_offset );
        return;
    }

    status = EXTERNAL_FLASH_ReadResultPage( info.page_size_bytes, console_flash_read_buffer,
                                            partial_length );
    if ( ( status != EXTERNAL_FLASH_STATUS_OK )
         || !CONSOLE_Flash_VerifyPattern( console_flash_read_buffer, info.page_size_bytes,
                                          partial_length, result_seed, &first_bad_offset ) )
    {
        CONSOLE_Printf( "Result partial-page verify failed: status=%d offset=%lu.\r\n",
                        ( int )status, ( unsigned long )first_bad_offset );
        return;
    }

    status = EXTERNAL_FLASH_GetInfo( &info );
    if ( ( status != EXTERNAL_FLASH_STATUS_OK )
         || ( info.instruction_length_bytes != expected_length )
         || ( info.result_length_bytes != expected_length ) )
    {
        CONSOLE_Printf( "External Flash committed-length verification failed.\r\n" );
        return;
    }

    CONSOLE_Printf( "External Flash test PASS: %lu bytes per partition, seed=0x%02X.\r\n",
                    ( unsigned long )expected_length, ( unsigned int )seed );
}

/** Benchmarks full-page NAND service through the production External Flash APIs. */
#ifndef TEST_BUILD
static void CONSOLE_Flash_ThroughputTestCommand( uint16_t argc, char* argv[] )
{
    if ( !CONSOLE_Flash_RequireIdle() )
    {
        return;
    }

    uint32_t page_count = CONSOLE_FLASH_THROUGHPUT_DEFAULT_PAGES;
    if ( ( argc == 3U ) && !CONSOLE_Flash_ParseU32( argv[2], &page_count ) )
    {
        CONSOLE_Printf( "Invalid page count.\r\n" );
        return;
    }
    if ( ( argc > 3U ) || ( page_count == 0U ) )
    {
        CONSOLE_Printf( "Usage: flash throughput_test [page_count > 0]\r\n" );
        return;
    }

    ExternalFlashInfo_T   info   = { 0 };
    ExternalFlashStatus_T status = EXTERNAL_FLASH_GetInfo( &info );
    if ( status != EXTERNAL_FLASH_STATUS_OK )
    {
        CONSOLE_Printf( "External Flash GetInfo failed (status=%d).\r\n", ( int )status );
        return;
    }

    if ( ( info.page_size_bytes == 0U )
         || ( info.page_size_bytes > sizeof( console_flash_write_buffer ) ) )
    {
        CONSOLE_Printf( "Unsupported NAND page size: %lu.\r\n",
                        ( unsigned long )info.page_size_bytes );
        return;
    }

    const uint32_t maximum_pages = ( info.instruction_capacity_bytes < info.result_capacity_bytes
                                         ? info.instruction_capacity_bytes
                                         : info.result_capacity_bytes )
                                   / info.page_size_bytes;
    if ( page_count > maximum_pages )
    {
        CONSOLE_Printf( "Page count exceeds benchmark capacity (maximum=%lu).\r\n",
                        ( unsigned long )maximum_pages );
        return;
    }

    const uint32_t           total_bytes      = page_count * info.page_size_bytes;
    const uint8_t            instruction_seed = CONSOLE_FLASH_DEFAULT_SEED;
    const uint8_t            result_seed      = ( uint8_t )( instruction_seed ^ 0xA5U );
    uint32_t                 first_bad_offset = 0U;
    ConsoleFlashPageTiming_T program_timing   = {
          .total_cycles = 0U, .minimum_cycles = UINT32_MAX, .maximum_cycles = 0U };
    ConsoleFlashPageTiming_T read_timing = {
        .total_cycles = 0U, .minimum_cycles = UINT32_MAX, .maximum_cycles = 0U };
    ConsoleFlashPageTiming_T alternating_read_timing = {
        .total_cycles = 0U, .minimum_cycles = UINT32_MAX, .maximum_cycles = 0U };
    ConsoleFlashPageTiming_T alternating_program_timing = {
        .total_cycles = 0U, .minimum_cycles = UINT32_MAX, .maximum_cycles = 0U };
    ConsoleFlashPageTiming_T alternating_pair_timing = {
        .total_cycles = 0U, .minimum_cycles = UINT32_MAX, .maximum_cycles = 0U };

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    CONSOLE_Printf( "NAND throughput test starting: pages=%lu page_size=%lu bytes. "
                    "Existing instruction and result data will be overwritten.\r\n",
                    ( unsigned long )page_count, ( unsigned long )info.page_size_bytes );

    status = EXTERNAL_FLASH_StartInstructionUpload( total_bytes );
    if ( status != EXTERNAL_FLASH_STATUS_OK )
    {
        CONSOLE_Printf( "Throughput instruction setup failed (status=%d).\r\n", ( int )status );
        return;
    }

    console_flash_last_upload_records = 0U;
    console_flash_last_upload_bytes   = 0U;
    console_flash_run_tick_count      = 0U;
    CONSOLE_Flash_ResetExecutionHarnessState();

    for ( uint32_t page = 0U; page < page_count; page++ )
    {
        const uint32_t offset = page * info.page_size_bytes;
        CONSOLE_Flash_FillPattern( console_flash_write_buffer, offset, info.page_size_bytes,
                                   instruction_seed );
        const uint32_t start_cycles = DWT->CYCCNT;
        status =
            EXTERNAL_FLASH_WriteInstructionPage( console_flash_write_buffer, info.page_size_bytes );
        CONSOLE_Flash_RecordPageTiming( &program_timing, DWT->CYCCNT - start_cycles );
        if ( status != EXTERNAL_FLASH_STATUS_OK )
        {
            CONSOLE_Printf( "Instruction program failed at page=%lu status=%d.\r\n",
                            ( unsigned long )page, ( int )status );
            return;
        }
    }

    status = EXTERNAL_FLASH_FinishInstructionUpload();
    if ( status != EXTERNAL_FLASH_STATUS_OK )
    {
        CONSOLE_Printf( "Instruction upload finish failed (status=%d).\r\n", ( int )status );
        return;
    }

    for ( uint32_t page = 0U; page < page_count; page++ )
    {
        const uint32_t offset       = page * info.page_size_bytes;
        const uint32_t start_cycles = DWT->CYCCNT;
        status = EXTERNAL_FLASH_ReadInstructionPage( offset, console_flash_read_buffer,
                                                     info.page_size_bytes );
        CONSOLE_Flash_RecordPageTiming( &read_timing, DWT->CYCCNT - start_cycles );
        if ( ( status != EXTERNAL_FLASH_STATUS_OK )
             || !CONSOLE_Flash_VerifyPattern( console_flash_read_buffer, offset,
                                              info.page_size_bytes, instruction_seed,
                                              &first_bad_offset ) )
        {
            CONSOLE_Printf( "Instruction read/verify failed at page=%lu status=%d offset=%lu.\r\n",
                            ( unsigned long )page, ( int )status,
                            ( unsigned long )first_bad_offset );
            return;
        }
    }

    status = EXTERNAL_FLASH_StartSession( total_bytes );
    if ( status != EXTERNAL_FLASH_STATUS_OK )
    {
        CONSOLE_Printf( "Throughput result setup failed (status=%d).\r\n", ( int )status );
        return;
    }

    for ( uint32_t page = 0U; page < page_count; page++ )
    {
        const uint32_t offset       = page * info.page_size_bytes;
        uint32_t       start_cycles = DWT->CYCCNT;
        status = EXTERNAL_FLASH_ReadInstructionPage( offset, console_flash_read_buffer,
                                                     info.page_size_bytes );
        const uint32_t read_cycles = DWT->CYCCNT - start_cycles;
        CONSOLE_Flash_RecordPageTiming( &alternating_read_timing, read_cycles );
        if ( ( status != EXTERNAL_FLASH_STATUS_OK )
             || !CONSOLE_Flash_VerifyPattern( console_flash_read_buffer, offset,
                                              info.page_size_bytes, instruction_seed,
                                              &first_bad_offset ) )
        {
            CONSOLE_Printf( "Alternating read failed at page=%lu status=%d offset=%lu.\r\n",
                            ( unsigned long )page, ( int )status,
                            ( unsigned long )first_bad_offset );
            return;
        }

        CONSOLE_Flash_FillPattern( console_flash_write_buffer, offset, info.page_size_bytes,
                                   result_seed );
        start_cycles = DWT->CYCCNT;
        status = EXTERNAL_FLASH_WriteResultPage( console_flash_write_buffer, info.page_size_bytes );
        const uint32_t program_cycles = DWT->CYCCNT - start_cycles;
        CONSOLE_Flash_RecordPageTiming( &alternating_program_timing, program_cycles );
        CONSOLE_Flash_RecordPageTiming( &alternating_pair_timing, read_cycles + program_cycles );
        if ( status != EXTERNAL_FLASH_STATUS_OK )
        {
            CONSOLE_Printf( "Alternating program failed at page=%lu status=%d.\r\n",
                            ( unsigned long )page, ( int )status );
            return;
        }
    }

    for ( uint32_t page = 0U; page < page_count; page++ )
    {
        const uint32_t offset = page * info.page_size_bytes;
        status                = EXTERNAL_FLASH_ReadResultPage( offset, console_flash_read_buffer,
                                                               info.page_size_bytes );
        if ( ( status != EXTERNAL_FLASH_STATUS_OK )
             || !CONSOLE_Flash_VerifyPattern( console_flash_read_buffer, offset,
                                              info.page_size_bytes, result_seed,
                                              &first_bad_offset ) )
        {
            CONSOLE_Printf( "Result verify failed at page=%lu status=%d offset=%lu.\r\n",
                            ( unsigned long )page, ( int )status,
                            ( unsigned long )first_bad_offset );
            return;
        }
    }

    CONSOLE_Flash_PrintPageTiming( "Program only", page_count, info.page_size_bytes,
                                   &program_timing );
    CONSOLE_Flash_PrintPageTiming( "Read only", page_count, info.page_size_bytes, &read_timing );
    CONSOLE_Flash_PrintPageTiming( "Alternating read", page_count, info.page_size_bytes,
                                   &alternating_read_timing );
    CONSOLE_Flash_PrintPageTiming( "Alternating program", page_count, info.page_size_bytes,
                                   &alternating_program_timing );
    CONSOLE_Flash_PrintPageTiming( "Alternating pair", page_count, 2U * info.page_size_bytes,
                                   &alternating_pair_timing );
    CONSOLE_Printf( "NAND throughput test PASS: %lu pages verified in each partition.\r\n",
                    ( unsigned long )page_count );
}
#endif

static void CONSOLE_Flash_WriteU32Le( uint8_t* destination, uint32_t value )
{
    destination[0] = ( uint8_t )value;
    destination[1] = ( uint8_t )( value >> 8U );
    destination[2] = ( uint8_t )( value >> 16U );
    destination[3] = ( uint8_t )( value >> 24U );
}

static void CONSOLE_Flash_WriteU16Le( uint8_t* destination, uint16_t value )
{
    destination[0] = ( uint8_t )value;
    destination[1] = ( uint8_t )( value >> 8U );
}

static void CONSOLE_Flash_EncodeDigitalOutputInstruction( uint8_t* destination, uint32_t timestamp,
                                                          uint32_t high_bitmask,
                                                          uint32_t low_bitmask )
{
    uint32_t instruction_word =
        CONSOLE_FLASH_DO_TEST_OPERATION_BYTES | ( CONSOLE_FLASH_TEST_OPERATION_COUNT << 16U );
    uint32_t operation_word = EXECUTION_OPERATION_OPCODE_DIGITAL_OUTPUT_UPDATE
                              | ( EXECUTION_DIGITAL_OUTPUT_PAYLOAD_SIZE_BYTES << 16U );

    CONSOLE_Flash_WriteU32Le( &destination[0], timestamp );
    CONSOLE_Flash_WriteU32Le( &destination[4], instruction_word );
    CONSOLE_Flash_WriteU32Le( &destination[8], operation_word );
    CONSOLE_Flash_WriteU32Le( &destination[12], high_bitmask );
    CONSOLE_Flash_WriteU32Le( &destination[16], low_bitmask );
}

static void CONSOLE_Flash_EncodePwmInstruction( uint8_t* destination, uint32_t timestamp,
                                                uint8_t                            channel,
                                                const ExecutionPwmUpdatePayload_T* payload )
{
    const uint32_t instruction_word =
        CONSOLE_FLASH_PWM_TEST_OPERATION_BYTES | ( CONSOLE_FLASH_TEST_OPERATION_COUNT << 16U );
    const uint32_t operation_word = EXECUTION_OPERATION_OPCODE_PWM_UPDATE
                                    | ( ( uint32_t )channel << 8U )
                                    | ( EXECUTION_PWM_UPDATE_PAYLOAD_SIZE_BYTES << 16U );

    CONSOLE_Flash_WriteU32Le( &destination[0], timestamp );
    CONSOLE_Flash_WriteU32Le( &destination[4], instruction_word );
    CONSOLE_Flash_WriteU32Le( &destination[8], operation_word );
    CONSOLE_Flash_WriteU16Le( &destination[12], payload->arr );
    CONSOLE_Flash_WriteU16Le( &destination[14], payload->ccr );
    CONSOLE_Flash_WriteU16Le( &destination[16], payload->psc );
    destination[18] = 0U;
    destination[19] = 0U;
}

static void
CONSOLE_Flash_EncodeAnalogueOutputInstruction( uint8_t* destination, uint32_t timestamp,
                                               const AnalogueOutputPreparedFrame_T* frame )
{
    const uint32_t instruction_word =
        CONSOLE_FLASH_AO_TEST_OPERATION_BYTES | ( CONSOLE_FLASH_TEST_OPERATION_COUNT << 16U );
    const uint32_t operation_word = EXECUTION_OPERATION_OPCODE_ANALOGUE_OUTPUT_BATCH
                                    | ( EXECUTION_ANALOGUE_OUTPUT_FRAME_SIZE_BYTES << 16U );

    CONSOLE_Flash_WriteU32Le( &destination[0], timestamp );
    CONSOLE_Flash_WriteU32Le( &destination[4], instruction_word );
    CONSOLE_Flash_WriteU32Le( &destination[8], operation_word );
    ( void )memset( &destination[12], 0,
                    CONSOLE_FLASH_AO_TEST_OPERATION_BYTES - EXECUTION_OPERATION_HEADER_SIZE_BYTES );
    ( void )memcpy( &destination[12], frame->bytes, EXECUTION_ANALOGUE_OUTPUT_FRAME_SIZE_BYTES );
}

static uint32_t CONSOLE_Flash_EncodeCanInstruction( uint8_t* destination, uint32_t timestamp,
                                                    uint8_t channel, uint16_t id, uint8_t value,
                                                    uint8_t dlc )
{
    const uint32_t instruction_word =
        CONSOLE_FLASH_CAN_TEST_OPERATION_BYTES | ( CONSOLE_FLASH_TEST_OPERATION_COUNT << 16U );
    const uint32_t operation_word = EXECUTION_OPERATION_OPCODE_CAN_TRANSMIT
                                    | ( ( uint32_t )channel << 8U )
                                    | ( EXECUTION_CAN_PACKET_SIZE_BYTES << 16U );

    CONSOLE_Flash_WriteU32Le( &destination[0], timestamp );
    CONSOLE_Flash_WriteU32Le( &destination[4], instruction_word );
    CONSOLE_Flash_WriteU32Le( &destination[8], operation_word );
    ( void )memset( &destination[12], 0,
                    CONSOLE_FLASH_CAN_TEST_OPERATION_BYTES
                        - EXECUTION_OPERATION_HEADER_SIZE_BYTES );
    CONSOLE_Flash_WriteU16Le( &destination[12], id );
    destination[14] = dlc;
    ( void )memset( &destination[15], value, dlc );

    return ( uint32_t )sizeof( ExecutionInstructionHeader_T )
           + CONSOLE_FLASH_CAN_TEST_OPERATION_BYTES;
}

static uint32_t CONSOLE_Flash_EncodeUartInstruction( uint8_t* destination, uint32_t timestamp,
                                                     uint8_t channel, uint8_t value,
                                                     uint16_t payload_length_bytes )
{
    const uint32_t operation_bytes = EXECUTION_OPERATION_ENCODED_SIZE_BYTES( payload_length_bytes );
    const uint32_t instruction_word =
        operation_bytes | ( CONSOLE_FLASH_TEST_OPERATION_COUNT << 16U );
    const uint32_t operation_word = EXECUTION_OPERATION_OPCODE_UART_TRANSMIT
                                    | ( ( uint32_t )channel << 8U )
                                    | ( ( uint32_t )payload_length_bytes << 16U );

    CONSOLE_Flash_WriteU32Le( &destination[0], timestamp );
    CONSOLE_Flash_WriteU32Le( &destination[4], instruction_word );
    CONSOLE_Flash_WriteU32Le( &destination[8], operation_word );
    ( void )memset( &destination[12], 0, operation_bytes - EXECUTION_OPERATION_HEADER_SIZE_BYTES );
    ( void )memset( &destination[12], value, payload_length_bytes );

    return ( uint32_t )sizeof( ExecutionInstructionHeader_T ) + operation_bytes;
}

static uint32_t CONSOLE_Flash_EncodeSpiInstruction( uint8_t* destination, uint32_t timestamp,
                                                    uint8_t channel, uint8_t value,
                                                    uint32_t packet_length_bytes )
{
    const uint32_t payload_length_bytes =
        EXECUTION_SPI_DATA_OFFSET_BYTES( 1U ) + packet_length_bytes;
    const uint32_t operation_bytes = EXECUTION_OPERATION_ENCODED_SIZE_BYTES( payload_length_bytes );
    const uint32_t instruction_word =
        operation_bytes | ( CONSOLE_FLASH_TEST_OPERATION_COUNT << 16U );
    const uint32_t operation_word = EXECUTION_OPERATION_OPCODE_SPI_TRANSMIT
                                    | ( ( uint32_t )channel << 8U )
                                    | ( payload_length_bytes << 16U );

    CONSOLE_Flash_WriteU32Le( &destination[0], timestamp );
    CONSOLE_Flash_WriteU32Le( &destination[4], instruction_word );
    CONSOLE_Flash_WriteU32Le( &destination[8], operation_word );
    ( void )memset( &destination[12], 0, operation_bytes - EXECUTION_OPERATION_HEADER_SIZE_BYTES );
    CONSOLE_Flash_WriteU32Le( &destination[12], 1U );
    CONSOLE_Flash_WriteU32Le( &destination[16], packet_length_bytes );
    ( void )memset( &destination[20], value, packet_length_bytes );

    return ( uint32_t )sizeof( ExecutionInstructionHeader_T ) + operation_bytes;
}

/** Builds one instruction containing the approved output paths, excluding CAN. */
static uint32_t CONSOLE_Flash_EncodeOutputStressInstruction(
    uint8_t* destination, uint32_t timestamp, uint32_t high_bitmask, uint32_t low_bitmask,
    const ExecutionPwmUpdatePayload_T pwm_payloads[EXEC_PWM_GEN_CHANNEL_COUNT] )
{
    uint32_t offset = ( uint32_t )sizeof( ExecutionInstructionHeader_T );

    const uint32_t digital_operation_bytes =
        EXECUTION_OPERATION_ENCODED_SIZE_BYTES( EXECUTION_DIGITAL_OUTPUT_PAYLOAD_SIZE_BYTES );
    CONSOLE_Flash_WriteU32Le( &destination[offset],
                              EXECUTION_OPERATION_OPCODE_DIGITAL_OUTPUT_UPDATE
                                  | ( EXECUTION_DIGITAL_OUTPUT_PAYLOAD_SIZE_BYTES << 16U ) );
    CONSOLE_Flash_WriteU32Le( &destination[offset + 4U], high_bitmask );
    CONSOLE_Flash_WriteU32Le( &destination[offset + 8U], low_bitmask );
    offset += digital_operation_bytes;

    for ( uint32_t channel = 0U; channel < EXEC_PWM_GEN_CHANNEL_COUNT; channel++ )
    {
        const uint32_t pwm_operation_bytes =
            EXECUTION_OPERATION_ENCODED_SIZE_BYTES( EXECUTION_PWM_UPDATE_PAYLOAD_SIZE_BYTES );
        CONSOLE_Flash_WriteU32Le( &destination[offset],
                                  EXECUTION_OPERATION_OPCODE_PWM_UPDATE | ( channel << 8U )
                                      | ( EXECUTION_PWM_UPDATE_PAYLOAD_SIZE_BYTES << 16U ) );
        CONSOLE_Flash_WriteU16Le( &destination[offset + 4U], pwm_payloads[channel].arr );
        CONSOLE_Flash_WriteU16Le( &destination[offset + 6U], pwm_payloads[channel].ccr );
        CONSOLE_Flash_WriteU16Le( &destination[offset + 8U], pwm_payloads[channel].psc );
        destination[offset + 10U] = 0U;
        destination[offset + 11U] = 0U;
        offset += pwm_operation_bytes;
    }

    static const uint32_t spi_packet_bytes[EXEC_SPI_CHANNEL_COUNT] = {
        [EXEC_SPI_CHANNEL_1] = CONSOLE_FLASH_OUTPUT_STRESS_SPI1_BYTES,
        [EXEC_SPI_CHANNEL_2] = CONSOLE_FLASH_OUTPUT_STRESS_SPI2_BYTES,
    };
    static const uint8_t spi_patterns[EXEC_SPI_CHANNEL_COUNT] = {
        [EXEC_SPI_CHANNEL_1] = 0x5AU,
        [EXEC_SPI_CHANNEL_2] = 0xA5U,
    };
    for ( uint32_t channel = 0U; channel < EXEC_SPI_CHANNEL_COUNT; channel++ )
    {
        const uint32_t spi_payload_bytes =
            EXECUTION_SPI_DATA_OFFSET_BYTES( 1U ) + spi_packet_bytes[channel];
        const uint32_t spi_operation_bytes =
            EXECUTION_OPERATION_ENCODED_SIZE_BYTES( spi_payload_bytes );
        CONSOLE_Flash_WriteU32Le( &destination[offset], EXECUTION_OPERATION_OPCODE_SPI_TRANSMIT
                                                            | ( channel << 8U )
                                                            | ( spi_payload_bytes << 16U ) );
        CONSOLE_Flash_WriteU32Le( &destination[offset + 4U], 1U );
        CONSOLE_Flash_WriteU32Le( &destination[offset + 8U], spi_packet_bytes[channel] );
        ( void )memset( &destination[offset + 12U], spi_patterns[channel],
                        spi_packet_bytes[channel] );
        offset += spi_operation_bytes;
    }

    for ( uint32_t channel = 0U; channel < EXEC_UART_CHANNEL_COUNT; channel++ )
    {
        const uint32_t uart_operation_bytes =
            EXECUTION_OPERATION_ENCODED_SIZE_BYTES( CONSOLE_FLASH_OUTPUT_STRESS_UART_BYTES );
        CONSOLE_Flash_WriteU32Le( &destination[offset],
                                  EXECUTION_OPERATION_OPCODE_UART_TRANSMIT | ( channel << 8U )
                                      | ( CONSOLE_FLASH_OUTPUT_STRESS_UART_BYTES << 16U ) );
        ( void )memset( &destination[offset + EXECUTION_OPERATION_HEADER_SIZE_BYTES],
                        channel == EXEC_UART_CHANNEL_1 ? 0x55 : 0xAA,
                        CONSOLE_FLASH_OUTPUT_STRESS_UART_BYTES );
        offset += uart_operation_bytes;
    }

    const uint32_t operations_length = offset - ( uint32_t )sizeof( ExecutionInstructionHeader_T );
    CONSOLE_Flash_WriteU32Le( &destination[0], timestamp );
    CONSOLE_Flash_WriteU32Le( &destination[4],
                              operations_length
                                  | ( CONSOLE_FLASH_OUTPUT_STRESS_OPERATION_COUNT << 16U ) );
    return offset;
}

/** Uploads two canonical digital-output instructions through the public upload API. */
static void CONSOLE_Flash_UploadDigitalOutputTestCommand( uint16_t argc, char* argv[] )
{
    uint32_t channel     = 0U;
    uint32_t delay_ticks = CONSOLE_FLASH_DO_TEST_DEFAULT_DELAY_TICKS;
    uint32_t high_ticks  = CONSOLE_FLASH_DO_TEST_DEFAULT_HIGH_TICKS;

    if ( ( argc < 3U ) || ( argc > 5U ) || !CONSOLE_Flash_ParseU32( argv[2], &channel )
         || ( ( argc >= 4U ) && !CONSOLE_Flash_ParseU32( argv[3], &delay_ticks ) )
         || ( ( argc == 5U ) && !CONSOLE_Flash_ParseU32( argv[4], &high_ticks ) )
         || ( channel < 1U ) || ( channel > EXEC_DIGITAL_OUTPUT_CHANNEL_COUNT )
         || ( delay_ticks == 0U ) || ( high_ticks == 0U )
         || ( ( uint64_t )delay_ticks + high_ticks > UINT32_MAX ) )
    {
        CONSOLE_Printf( "Usage: flash upload_do_test <channel 1..10> "
                        "[delay_ticks > 0] [high_ticks > 0]\r\n" );
        return;
    }

    uint32_t low_tick = delay_ticks + high_ticks;

    if ( !CONSOLE_Flash_RequireIdle() )
    {
        return;
    }

    console_flash_run_tick_count = 0U;

    GPIOOutput_T           gpio_output = ( GPIOOutput_T )( DIGITAL_OUTPUT_0 + channel - 1U );
    DigitalOutputPinmask_T pin_mask =
        EXEC_DIGITAL_OUTPUT_Combine_Port_Pin_Masks( &gpio_output, 1U );
    if ( pin_mask == 0U )
    {
        CONSOLE_Printf( "Failed to map digital-output channel %lu to a physical pin.\r\n",
                        ( unsigned long )channel );
        return;
    }

    const uint32_t upload_bytes =
        CONSOLE_FLASH_DO_TEST_INSTRUCTION_COUNT * CONSOLE_FLASH_DO_TEST_INSTRUCTION_BYTES;
    FlashManagerInstructionUploadRequestStatus_T status =
        FLASH_MANAGER_RequestInstructionUploadStart( upload_bytes );
    if ( status != FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_ACCEPTED
         || !CONSOLE_Flash_WaitForState( FLASH_MANAGER_STATE_INSTRUCTION_UPLOAD,
                                         CONSOLE_FLASH_STATE_TIMEOUT_MS ) )
    {
        CONSOLE_Printf( "Digital-output upload start failed (status=%d).\r\n", ( int )status );
        return;
    }

    CONSOLE_Flash_EncodeDigitalOutputInstruction( &console_flash_write_buffer[0], delay_ticks,
                                                  pin_mask, 0U );
    CONSOLE_Flash_EncodeDigitalOutputInstruction(
        &console_flash_write_buffer[CONSOLE_FLASH_DO_TEST_INSTRUCTION_BYTES], low_tick, 0U,
        pin_mask );

    status = FLASH_MANAGER_SubmitInstructionUploadBytes( console_flash_write_buffer, upload_bytes );
    if ( status != FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_ACCEPTED )
    {
        CONSOLE_Printf( "Digital-output instruction submission failed (status=%d).\r\n",
                        ( int )status );
        return;
    }

    TickType_t finish_started_at = xTaskGetTickCount();
    do
    {
        status = FLASH_MANAGER_RequestInstructionUploadFinish();
        if ( status == FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_BUSY )
        {
            if ( CONSOLE_Flash_HasTimedOut( finish_started_at, CONSOLE_FLASH_PROGRESS_TIMEOUT_MS ) )
            {
                CONSOLE_Printf( "Digital-output upload finalisation timed out.\r\n" );
                return;
            }
            vTaskDelay( pdMS_TO_TICKS( CONSOLE_FLASH_POLL_PERIOD_MS ) );
        }
    } while ( status == FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_BUSY );

    if ( status != FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_ACCEPTED
         || !CONSOLE_Flash_WaitForState( FLASH_MANAGER_STATE_IDLE,
                                         CONSOLE_FLASH_STATE_TIMEOUT_MS ) )
    {
        CONSOLE_Printf( "Digital-output upload finalisation failed (status=%d).\r\n",
                        ( int )status );
        return;
    }

    console_flash_last_upload_records = CONSOLE_FLASH_DO_TEST_INSTRUCTION_COUNT;
    console_flash_last_upload_bytes   = upload_bytes;
    console_flash_run_tick_count      = low_tick;
    CONSOLE_Flash_ResetExecutionHarnessState();

    CONSOLE_Printf( "Digital-output upload PASS: channel=%lu mask=0x%08lX high_tick=%lu "
                    "low_tick=%lu run_ticks=%lu.\r\n",
                    ( unsigned long )channel, ( unsigned long )pin_mask,
                    ( unsigned long )delay_ticks, ( unsigned long )low_tick,
                    ( unsigned long )console_flash_run_tick_count );
    CONSOLE_Printf( "Next: configure the RSM, then 'run_state execute %lu 0'.\r\n",
                    ( unsigned long )console_flash_run_tick_count );
}

/** Uploads a repeated 32-bit, least-significant-bit-first digital-output pattern. */
static void CONSOLE_Flash_UploadDigitalPatternCommand( uint16_t argc, char* argv[] )
{
    uint32_t channel        = 0U;
    uint32_t first_tick     = 0U;
    uint32_t interval_ticks = 0U;
    uint32_t repeat_words   = 0U;
    uint32_t pattern_word   = 0U;
    uint32_t run_ticks      = 0U;

    if ( argc != 8U || !CONSOLE_Flash_ParseU32( argv[2], &channel )
         || !CONSOLE_Flash_ParseU32( argv[3], &first_tick )
         || !CONSOLE_Flash_ParseU32( argv[4], &interval_ticks )
         || !CONSOLE_Flash_ParseU32( argv[5], &repeat_words )
         || !CONSOLE_Flash_ParseU32( argv[6], &pattern_word )
         || !CONSOLE_Flash_ParseU32( argv[7], &run_ticks ) || channel < 1U
         || channel > EXEC_DIGITAL_OUTPUT_CHANNEL_COUNT || first_tick == 0U || interval_ticks == 0U
         || repeat_words == 0U || repeat_words > ( UINT32_MAX / 32U ) )
    {
        CONSOLE_Printf( "Usage: flash upload_do_pattern <channel 1..10> <first_tick> "
                        "<interval_ticks> <repeat_words> <hex_word> <run_ticks>\r\n" );
        return;
    }

    const uint32_t pattern_bits = repeat_words * 32U;
    if ( ( pattern_bits > 1U
           && ( pattern_bits - 1U ) > ( ( UINT32_MAX - first_tick ) / interval_ticks ) )
         || first_tick + ( ( pattern_bits - 1U ) * interval_ticks ) > UINT32_MAX - interval_ticks )
    {
        CONSOLE_Printf( "Digital pattern timestamp range overflow.\r\n" );
        return;
    }

    const uint32_t final_low_tick = first_tick + ( pattern_bits * interval_ticks );
    if ( run_ticks <= final_low_tick || pattern_bits == UINT32_MAX
         || pattern_bits + 1U > ( UINT32_MAX / CONSOLE_FLASH_DO_TEST_INSTRUCTION_BYTES ) )
    {
        CONSOLE_Printf( "run_ticks must extend beyond the pattern's final LOW update.\r\n" );
        return;
    }

    if ( !CONSOLE_Flash_RequireIdle() )
    {
        return;
    }

    GPIOOutput_T           gpio_output = ( GPIOOutput_T )( DIGITAL_OUTPUT_0 + channel - 1U );
    DigitalOutputPinmask_T pin_mask =
        EXEC_DIGITAL_OUTPUT_Combine_Port_Pin_Masks( &gpio_output, 1U );
    if ( pin_mask == 0U )
    {
        CONSOLE_Printf( "Failed to map digital-output channel %lu.\r\n", ( unsigned long )channel );
        return;
    }

    const uint32_t instruction_count = pattern_bits + 1U;
    const uint32_t upload_bytes      = instruction_count * CONSOLE_FLASH_DO_TEST_INSTRUCTION_BYTES;
    FlashManagerInstructionUploadRequestStatus_T status =
        FLASH_MANAGER_RequestInstructionUploadStart( upload_bytes );
    if ( status != FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_ACCEPTED
         || !CONSOLE_Flash_WaitForState( FLASH_MANAGER_STATE_INSTRUCTION_UPLOAD,
                                         CONSOLE_FLASH_STATE_TIMEOUT_MS ) )
    {
        CONSOLE_Printf( "Digital pattern upload start failed (status=%d).\r\n", ( int )status );
        return;
    }

    for ( uint32_t bit = 0U; bit < pattern_bits; bit++ )
    {
        const bool high = ( pattern_word & ( UINT32_C( 1 ) << ( bit % 32U ) ) ) != 0U;
        CONSOLE_Flash_EncodeDigitalOutputInstruction( console_flash_write_buffer,
                                                      first_tick + ( bit * interval_ticks ),
                                                      high ? pin_mask : 0U, high ? 0U : pin_mask );

        TickType_t progress_started_at = xTaskGetTickCount();
        do
        {
            status = FLASH_MANAGER_SubmitInstructionUploadBytes(
                console_flash_write_buffer, CONSOLE_FLASH_DO_TEST_INSTRUCTION_BYTES );
            if ( status == FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_BUSY )
            {
                if ( CONSOLE_Flash_HasTimedOut( progress_started_at,
                                                CONSOLE_FLASH_PROGRESS_TIMEOUT_MS ) )
                {
                    CONSOLE_Printf( "Digital pattern upload timed out at bit %lu.\r\n",
                                    ( unsigned long )bit );
                    return;
                }
                vTaskDelay( pdMS_TO_TICKS( CONSOLE_FLASH_POLL_PERIOD_MS ) );
            }
        } while ( status == FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_BUSY );

        if ( status != FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_ACCEPTED )
        {
            CONSOLE_Printf( "Digital pattern upload failed at bit %lu (status=%d).\r\n",
                            ( unsigned long )bit, ( int )status );
            return;
        }
    }

    CONSOLE_Flash_EncodeDigitalOutputInstruction( console_flash_write_buffer, final_low_tick, 0U,
                                                  pin_mask );
    TickType_t final_submit_started_at = xTaskGetTickCount();
    do
    {
        status = FLASH_MANAGER_SubmitInstructionUploadBytes(
            console_flash_write_buffer, CONSOLE_FLASH_DO_TEST_INSTRUCTION_BYTES );
        if ( status == FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_BUSY )
        {
            if ( CONSOLE_Flash_HasTimedOut( final_submit_started_at,
                                            CONSOLE_FLASH_PROGRESS_TIMEOUT_MS ) )
            {
                CONSOLE_Printf( "Digital pattern final LOW submission timed out.\r\n" );
                return;
            }
            vTaskDelay( pdMS_TO_TICKS( CONSOLE_FLASH_POLL_PERIOD_MS ) );
        }
    } while ( status == FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_BUSY );

    if ( status != FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_ACCEPTED )
    {
        CONSOLE_Printf( "Digital pattern final LOW submission failed (status=%d).\r\n",
                        ( int )status );
        return;
    }

    TickType_t finish_started_at = xTaskGetTickCount();
    do
    {
        status = FLASH_MANAGER_RequestInstructionUploadFinish();
        if ( status == FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_BUSY )
        {
            if ( CONSOLE_Flash_HasTimedOut( finish_started_at, CONSOLE_FLASH_PROGRESS_TIMEOUT_MS ) )
            {
                CONSOLE_Printf( "Digital pattern upload finalisation timed out.\r\n" );
                return;
            }
            vTaskDelay( pdMS_TO_TICKS( CONSOLE_FLASH_POLL_PERIOD_MS ) );
        }
    } while ( status == FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_BUSY );

    if ( status != FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_ACCEPTED
         || !CONSOLE_Flash_WaitForState( FLASH_MANAGER_STATE_IDLE,
                                         CONSOLE_FLASH_STATE_TIMEOUT_MS ) )
    {
        CONSOLE_Printf( "Digital pattern upload finalisation failed (status=%d).\r\n",
                        ( int )status );
        return;
    }

    console_flash_digital_pattern = ( ConsoleFlashDigitalPattern_T ){
        .valid          = true,
        .first_tick     = first_tick,
        .interval_ticks = interval_ticks,
        .repeat_words   = repeat_words,
        .pattern_word   = pattern_word,
        .run_ticks      = run_ticks,
    };
    console_flash_run_tick_count = run_ticks;

    CONSOLE_Printf( "Digital pattern upload PASS: channel=%lu bits=%lu first_tick=%lu "
                    "interval=%lu final_low_tick=%lu run_ticks=%lu.\r\n",
                    ( unsigned long )channel, ( unsigned long )pattern_bits,
                    ( unsigned long )first_tick, ( unsigned long )interval_ticks,
                    ( unsigned long )final_low_tick, ( unsigned long )run_ticks );
}

/** Uploads one driver-prepared PWM register update through the public upload API. */
static void CONSOLE_Flash_UploadPwmTestCommand( uint16_t argc, char* argv[] )
{
    uint32_t channel       = 0U;
    uint32_t frequency_hz  = 0U;
    uint32_t duty_permille = 0U;
    uint32_t update_tick   = 0U;
    uint32_t run_ticks     = 0U;

    if ( argc != 7U || !CONSOLE_Flash_ParseU32( argv[2], &channel )
         || !CONSOLE_Flash_ParseU32( argv[3], &frequency_hz )
         || !CONSOLE_Flash_ParseU32( argv[4], &duty_permille )
         || !CONSOLE_Flash_ParseU32( argv[5], &update_tick )
         || !CONSOLE_Flash_ParseU32( argv[6], &run_ticks ) || channel < 1U
         || channel > EXEC_PWM_GEN_CHANNEL_COUNT || duty_permille > 1000U || update_tick == 0U
         || run_ticks < update_tick )
    {
        CONSOLE_Printf(
            "Usage: flash upload_pwm_test <channel 1..2> <frequency_hz> "
            "<duty_permille 0..1000> <update_tick > 0> <run_ticks >= update_tick>\r\n" );
        return;
    }

    if ( !CONSOLE_Flash_RequireIdle() )
    {
        return;
    }

    DutDriverConfiguration_T configuration = { 0 };
    if ( !TEST_CONFIGURATION_GetActive( &configuration )
         || !configuration.pwm_generation_channels[channel - 1U].is_enabled )
    {
        CONSOLE_Printf( "PWM channel %lu is not enabled in the active test configuration.\r\n",
                        ( unsigned long )channel );
        return;
    }

    const uint32_t timer_clock_hz =
        channel == 1U ? CONSOLE_FLASH_PWM_LV_TIMER_CLOCK_HZ : CONSOLE_FLASH_PWM_HV_TIMER_CLOCK_HZ;
    ExecutionPwmUpdatePayload_T payload = { 0 };
    if ( !HW_PWM_GEN_compute_psc( frequency_hz, timer_clock_hz, &payload.psc )
         || !HW_PWM_GEN_compute_arr( frequency_hz, timer_clock_hz, payload.psc, &payload.arr )
         || !HW_PWM_GEN_compute_ccr( ( uint16_t )duty_permille, payload.arr, &payload.ccr ) )
    {
        CONSOLE_Printf( "PWM frequency/duty cannot be represented by channel %lu.\r\n",
                        ( unsigned long )channel );
        return;
    }

    console_flash_run_tick_count = 0U;
    FlashManagerInstructionUploadRequestStatus_T status =
        FLASH_MANAGER_RequestInstructionUploadStart( CONSOLE_FLASH_PWM_TEST_INSTRUCTION_BYTES );
    if ( status != FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_ACCEPTED
         || !CONSOLE_Flash_WaitForState( FLASH_MANAGER_STATE_INSTRUCTION_UPLOAD,
                                         CONSOLE_FLASH_STATE_TIMEOUT_MS ) )
    {
        CONSOLE_Printf( "PWM upload start failed (status=%d).\r\n", ( int )status );
        return;
    }

    CONSOLE_Flash_EncodePwmInstruction( console_flash_write_buffer, update_tick,
                                        ( uint8_t )( channel - 1U ), &payload );
    status = FLASH_MANAGER_SubmitInstructionUploadBytes( console_flash_write_buffer,
                                                         CONSOLE_FLASH_PWM_TEST_INSTRUCTION_BYTES );
    if ( status != FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_ACCEPTED )
    {
        CONSOLE_Printf( "PWM instruction submission failed (status=%d).\r\n", ( int )status );
        return;
    }

    TickType_t finish_started_at = xTaskGetTickCount();
    do
    {
        status = FLASH_MANAGER_RequestInstructionUploadFinish();
        if ( status == FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_BUSY )
        {
            if ( CONSOLE_Flash_HasTimedOut( finish_started_at, CONSOLE_FLASH_PROGRESS_TIMEOUT_MS ) )
            {
                CONSOLE_Printf( "PWM upload finalisation timed out.\r\n" );
                return;
            }
            vTaskDelay( pdMS_TO_TICKS( CONSOLE_FLASH_POLL_PERIOD_MS ) );
        }
    } while ( status == FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_BUSY );

    if ( status != FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_ACCEPTED
         || !CONSOLE_Flash_WaitForState( FLASH_MANAGER_STATE_IDLE,
                                         CONSOLE_FLASH_STATE_TIMEOUT_MS ) )
    {
        CONSOLE_Printf( "PWM upload finalisation failed (status=%d).\r\n", ( int )status );
        return;
    }

    console_flash_last_upload_records = 1U;
    console_flash_last_upload_bytes   = CONSOLE_FLASH_PWM_TEST_INSTRUCTION_BYTES;
    console_flash_run_tick_count      = run_ticks;
    console_flash_pwm_loopback        = ( ConsoleFlashPwmLoopback_T ){
               .valid          = true,
               .output_channel = channel,
               .frequency_hz   = frequency_hz,
               .duty_permille  = duty_permille,
               .update_tick    = update_tick,
               .run_ticks      = run_ticks,
    };
    CONSOLE_Flash_ResetExecutionHarnessState();
    CONSOLE_Printf( "PWM upload PASS: channel=%lu target=%lu Hz duty=%lu/1000 "
                    "arr=%u ccr=%u psc=%u update_tick=%lu run_ticks=%lu.\r\n",
                    ( unsigned long )channel, ( unsigned long )frequency_hz,
                    ( unsigned long )duty_permille, ( unsigned int )payload.arr,
                    ( unsigned int )payload.ccr, ( unsigned int )payload.psc,
                    ( unsigned long )update_tick, ( unsigned long )run_ticks );
    CONSOLE_Printf( "Next: finish configuring the RSM, then "
                    "'run_state execute %lu'.\r\n",
                    ( unsigned long )console_flash_run_tick_count );
}

/** Uploads one driver-prepared analogue-output frame through the production instruction path. */
static void CONSOLE_Flash_UploadAnalogueOutputTestCommand( uint16_t argc, char* argv[] )
{
    uint32_t channel     = 0U;
    uint32_t update_tick = 0U;
    uint32_t run_ticks   = 0U;
    char*    voltage_end = NULL;
    float    voltage     = 0.0F;

    if ( argc != 6U || !CONSOLE_Flash_ParseU32( argv[2], &channel )
         || !CONSOLE_Flash_ParseU32( argv[4], &update_tick )
         || !CONSOLE_Flash_ParseU32( argv[5], &run_ticks ) || update_tick == 0U
         || run_ticks < update_tick )
    {
        CONSOLE_Printf( "Usage: flash upload_ao_test <channel 0..5> <voltage 0..20V> "
                        "<update_tick > 0> <run_ticks >= update_tick>\r\n" );
        return;
    }

    voltage = strtof( argv[3], &voltage_end );
    if ( ( voltage_end == argv[3] ) || ( *voltage_end != '\0' ) )
    {
        CONSOLE_Printf( "Invalid analogue-output voltage.\r\n" );
        return;
    }

    AnalogueOutputPreparedFrame_T frame = { { 0U, 0U, 0U } };
    if ( !EXEC_ANALOGUE_OUTPUT_Prepare_Frame( ( uint8_t )channel, voltage, &frame ) )
    {
        CONSOLE_Printf( "Analogue-output channel or voltage is invalid.\r\n" );
        return;
    }

    if ( !CONSOLE_Flash_RequireIdle() )
    {
        return;
    }

    DutDriverConfiguration_T configuration = { 0 };
    if ( !TEST_CONFIGURATION_GetActive( &configuration )
         || !configuration.analogue_output.is_enabled )
    {
        CONSOLE_Printf( "Analogue output is not enabled in the active test configuration.\r\n" );
        return;
    }

    const uint32_t instruction_bytes = CONSOLE_FLASH_AO_TEST_INSTRUCTION_BYTES;
    console_flash_run_tick_count     = 0U;

    FlashManagerInstructionUploadRequestStatus_T status =
        FLASH_MANAGER_RequestInstructionUploadStart( instruction_bytes );
    if ( status != FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_ACCEPTED
         || !CONSOLE_Flash_WaitForState( FLASH_MANAGER_STATE_INSTRUCTION_UPLOAD,
                                         CONSOLE_FLASH_STATE_TIMEOUT_MS ) )
    {
        CONSOLE_Printf( "Analogue-output upload start failed (status=%d).\r\n", ( int )status );
        return;
    }

    CONSOLE_Flash_EncodeAnalogueOutputInstruction( console_flash_write_buffer, update_tick,
                                                   &frame );
    status =
        FLASH_MANAGER_SubmitInstructionUploadBytes( console_flash_write_buffer, instruction_bytes );
    if ( status != FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_ACCEPTED )
    {
        CONSOLE_Printf( "Analogue-output instruction submission failed (status=%d).\r\n",
                        ( int )status );
        return;
    }

    TickType_t finish_started_at = xTaskGetTickCount();
    do
    {
        status = FLASH_MANAGER_RequestInstructionUploadFinish();
        if ( status == FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_BUSY )
        {
            if ( CONSOLE_Flash_HasTimedOut( finish_started_at, CONSOLE_FLASH_PROGRESS_TIMEOUT_MS ) )
            {
                CONSOLE_Printf( "Analogue-output upload finalisation timed out.\r\n" );
                return;
            }
            vTaskDelay( pdMS_TO_TICKS( CONSOLE_FLASH_POLL_PERIOD_MS ) );
        }
    } while ( status == FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_BUSY );

    if ( status != FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_ACCEPTED
         || !CONSOLE_Flash_WaitForState( FLASH_MANAGER_STATE_IDLE,
                                         CONSOLE_FLASH_STATE_TIMEOUT_MS ) )
    {
        CONSOLE_Printf( "Analogue-output upload finalisation failed (status=%d).\r\n",
                        ( int )status );
        return;
    }

    console_flash_last_upload_records = 1U;
    console_flash_last_upload_bytes   = instruction_bytes;
    console_flash_run_tick_count      = run_ticks;
    CONSOLE_Flash_ResetExecutionHarnessState();
    console_flash_analogue_loopback = ( ConsoleFlashAnalogueLoopback_T ){
        .valid               = true,
        .output_channel      = channel,
        .expected_millivolts = ( uint32_t )( ( voltage * 1000.0F ) + 0.5F ),
        .update_tick         = update_tick,
        .run_ticks           = run_ticks,
    };

    CONSOLE_Printf( "Analogue-output upload PASS: channel=%lu voltage=%s "
                    "update_tick=%lu run_ticks=%lu.\r\n",
                    ( unsigned long )channel, argv[3], ( unsigned long )update_tick,
                    ( unsigned long )run_ticks );
    CONSOLE_Printf( "Next: finish configuring the RSM, then 'run_state execute %lu'.\r\n",
                    ( unsigned long )console_flash_run_tick_count );
}

/** Uploads repeated single-frame CAN transfers through the production instruction path. */
static void CONSOLE_Flash_UploadCanTestCommand( uint16_t argc, char* argv[] )
{
    uint32_t channel        = 0U;
    uint32_t identifier     = 0U;
    uint32_t value          = 0U;
    uint32_t dlc            = 0U;
    uint32_t first_tick     = 0U;
    uint32_t run_ticks      = 0U;
    uint32_t repeat_count   = 1U;
    uint32_t interval_ticks = 0U;

    if ( ( argc != 8U && argc != 10U ) || !CONSOLE_Flash_ParseU32( argv[2], &channel )
         || !CONSOLE_Flash_ParseU32( argv[3], &identifier )
         || !CONSOLE_Flash_ParseU32( argv[4], &value ) || !CONSOLE_Flash_ParseU32( argv[5], &dlc )
         || !CONSOLE_Flash_ParseU32( argv[6], &first_tick )
         || !CONSOLE_Flash_ParseU32( argv[7], &run_ticks )
         || ( argc == 10U
              && ( !CONSOLE_Flash_ParseU32( argv[8], &repeat_count )
                   || !CONSOLE_Flash_ParseU32( argv[9], &interval_ticks ) ) )
         || channel < 1U || channel > EXEC_CAN_CHANNEL_COUNT
         || identifier > EXEC_CAN_STANDARD_ID_MAX || value > UINT8_MAX
         || dlc > EXEC_CAN_MAX_PAYLOAD_SIZE || first_tick == 0U || repeat_count == 0U
         || ( repeat_count > 1U && interval_ticks == 0U ) )
    {
        CONSOLE_Printf( "Usage: flash upload_can_test <channel 1..2> <id 0..2047> "
                        "<byte 0..255> <dlc 0..8> <first_tick > 0> <run_ticks> "
                        "[repeat_count interval_ticks]\r\n" );
        return;
    }

    if ( repeat_count > 1U
         && ( repeat_count - 1U ) > ( ( UINT32_MAX - first_tick ) / interval_ticks ) )
    {
        CONSOLE_Printf( "CAN repeat schedule exceeds the timestamp range.\r\n" );
        return;
    }

    const uint32_t last_tick = first_tick + ( ( repeat_count - 1U ) * interval_ticks );
    if ( run_ticks < last_tick )
    {
        CONSOLE_Printf( "Run ticks must include the final CAN transmit tick (%lu).\r\n",
                        ( unsigned long )last_tick );
        return;
    }

    if ( !CONSOLE_Flash_RequireIdle() )
    {
        return;
    }

    DutDriverConfiguration_T configuration = { 0 };
    if ( !TEST_CONFIGURATION_GetActive( &configuration )
         || !configuration.can_channels[channel - 1U].is_enabled )
    {
        CONSOLE_Printf( "CAN channel %lu is not enabled in the active test configuration.\r\n",
                        ( unsigned long )channel );
        return;
    }

    const uint32_t instruction_bytes = CONSOLE_FLASH_CAN_TEST_INSTRUCTION_BYTES;
    if ( repeat_count > ( UINT32_MAX / instruction_bytes ) )
    {
        CONSOLE_Printf( "CAN repeat stream exceeds the upload length range.\r\n" );
        return;
    }

    const uint32_t upload_bytes  = repeat_count * instruction_bytes;
    console_flash_run_tick_count = 0U;

    FlashManagerInstructionUploadRequestStatus_T status =
        FLASH_MANAGER_RequestInstructionUploadStart( upload_bytes );
    if ( status != FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_ACCEPTED
         || !CONSOLE_Flash_WaitForState( FLASH_MANAGER_STATE_INSTRUCTION_UPLOAD,
                                         CONSOLE_FLASH_STATE_TIMEOUT_MS ) )
    {
        CONSOLE_Printf( "CAN upload start failed (status=%d).\r\n", ( int )status );
        return;
    }

    for ( uint32_t repeat_index = 0U; repeat_index < repeat_count; repeat_index++ )
    {
        const uint32_t transmit_tick = first_tick + ( repeat_index * interval_ticks );
        ( void )CONSOLE_Flash_EncodeCanInstruction(
            console_flash_write_buffer, transmit_tick, ( uint8_t )( channel - 1U ),
            ( uint16_t )identifier, ( uint8_t )value, ( uint8_t )dlc );

        TickType_t progress_started_at = xTaskGetTickCount();
        for ( ;; )
        {
            status = FLASH_MANAGER_SubmitInstructionUploadBytes( console_flash_write_buffer,
                                                                 instruction_bytes );
            if ( status == FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_ACCEPTED )
            {
                break;
            }

            if ( status != FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_BUSY )
            {
                CONSOLE_Printf( "CAN instruction %lu submission failed (status=%d).\r\n",
                                ( unsigned long )( repeat_index + 1U ), ( int )status );
                return;
            }

            if ( CONSOLE_Flash_HasTimedOut( progress_started_at,
                                            CONSOLE_FLASH_PROGRESS_TIMEOUT_MS ) )
            {
                CONSOLE_Printf( "CAN upload timed out at instruction %lu.\r\n",
                                ( unsigned long )( repeat_index + 1U ) );
                return;
            }

            vTaskDelay( pdMS_TO_TICKS( CONSOLE_FLASH_POLL_PERIOD_MS ) );
        }
    }

    TickType_t finish_started_at = xTaskGetTickCount();
    do
    {
        status = FLASH_MANAGER_RequestInstructionUploadFinish();
        if ( status == FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_BUSY )
        {
            if ( CONSOLE_Flash_HasTimedOut( finish_started_at, CONSOLE_FLASH_PROGRESS_TIMEOUT_MS ) )
            {
                CONSOLE_Printf( "CAN upload finalisation timed out.\r\n" );
                return;
            }
            vTaskDelay( pdMS_TO_TICKS( CONSOLE_FLASH_POLL_PERIOD_MS ) );
        }
    } while ( status == FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_BUSY );

    if ( status != FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_ACCEPTED
         || !CONSOLE_Flash_WaitForState( FLASH_MANAGER_STATE_IDLE,
                                         CONSOLE_FLASH_STATE_TIMEOUT_MS ) )
    {
        CONSOLE_Printf( "CAN upload finalisation failed (status=%d).\r\n", ( int )status );
        return;
    }

    console_flash_last_upload_records = repeat_count;
    console_flash_last_upload_bytes   = upload_bytes;
    console_flash_run_tick_count      = run_ticks;
    CONSOLE_Flash_ResetExecutionHarnessState();

    CONSOLE_Printf( "CAN upload PASS: channel=%lu id=0x%03lX byte=0x%02lX dlc=%lu "
                    "first_tick=%lu repeats=%lu interval_ticks=%lu run_ticks=%lu.\r\n",
                    ( unsigned long )channel, ( unsigned long )identifier, ( unsigned long )value,
                    ( unsigned long )dlc, ( unsigned long )first_tick,
                    ( unsigned long )repeat_count, ( unsigned long )interval_ticks,
                    ( unsigned long )run_ticks );
    CONSOLE_Printf( "Next: finish configuring the RSM, then 'run_state execute %lu 0'.\r\n",
                    ( unsigned long )console_flash_run_tick_count );
}

/** Uploads one or more single-packet SPI transfers through the production instruction path. */
static void CONSOLE_Flash_UploadSpiTestCommand( uint16_t argc, char* argv[] )
{
    uint32_t channel        = 0U;
    uint32_t value          = 0U;
    uint32_t length         = 0U;
    uint32_t first_tick     = 0U;
    uint32_t run_ticks      = 0U;
    uint32_t repeat_count   = 1U;
    uint32_t interval_ticks = 0U;

    if ( ( argc != 7U && argc != 9U ) || !CONSOLE_Flash_ParseU32( argv[2], &channel )
         || !CONSOLE_Flash_ParseU32( argv[3], &value )
         || !CONSOLE_Flash_ParseU32( argv[4], &length )
         || !CONSOLE_Flash_ParseU32( argv[5], &first_tick )
         || !CONSOLE_Flash_ParseU32( argv[6], &run_ticks )
         || ( argc == 9U
              && ( !CONSOLE_Flash_ParseU32( argv[7], &repeat_count )
                   || !CONSOLE_Flash_ParseU32( argv[8], &interval_ticks ) ) )
         || channel < 1U || channel > EXEC_SPI_CHANNEL_COUNT || value > UINT8_MAX || length == 0U
         || first_tick == 0U || repeat_count == 0U
         || ( repeat_count > 1U && interval_ticks == 0U ) )
    {
        CONSOLE_Printf( "Usage: flash upload_spi_test <channel 1..2> <byte 0..255> <length> "
                        "<first_tick > 0> <run_ticks> [repeat_count interval_ticks]\r\n" );
        return;
    }

    if ( repeat_count > 1U
         && ( repeat_count - 1U ) > ( ( UINT32_MAX - first_tick ) / interval_ticks ) )
    {
        CONSOLE_Printf( "SPI repeat schedule exceeds the timestamp range.\r\n" );
        return;
    }

    const uint32_t last_tick = first_tick + ( ( repeat_count - 1U ) * interval_ticks );
    if ( run_ticks < last_tick )
    {
        CONSOLE_Printf( "Run ticks must include the final SPI transmit tick (%lu).\r\n",
                        ( unsigned long )last_tick );
        return;
    }

    if ( !CONSOLE_Flash_RequireIdle() )
    {
        return;
    }

    DutDriverConfiguration_T configuration = { 0 };
    if ( !TEST_CONFIGURATION_GetActive( &configuration )
         || !configuration.spi_channels[channel - 1U].is_enabled )
    {
        CONSOLE_Printf( "SPI channel %lu is not enabled in the active test configuration.\r\n",
                        ( unsigned long )channel );
        return;
    }

    if ( configuration.spi_channels[channel - 1U].data_size == EXEC_SPI_SIZE_16_BIT
         && ( length % 2U ) != 0U )
    {
        CONSOLE_Printf( "SPI packet length must be even for a 16-bit channel.\r\n" );
        return;
    }

    const uint64_t payload_length_bytes =
        ( uint64_t )EXECUTION_SPI_DATA_OFFSET_BYTES( 1U ) + length;
    if ( payload_length_bytes > UINT16_MAX )
    {
        CONSOLE_Printf( "SPI payload exceeds the canonical 16-bit length field.\r\n" );
        return;
    }

    const uint32_t operation_bytes =
        EXECUTION_OPERATION_ENCODED_SIZE_BYTES( ( uint32_t )payload_length_bytes );
    const uint32_t instruction_bytes =
        ( uint32_t )sizeof( ExecutionInstructionHeader_T ) + operation_bytes;
    if ( instruction_bytes > sizeof( console_flash_write_buffer ) )
    {
        CONSOLE_Printf( "SPI instruction exceeds the %u-byte console staging buffer.\r\n",
                        ( unsigned int )sizeof( console_flash_write_buffer ) );
        return;
    }

    if ( repeat_count > ( UINT32_MAX / instruction_bytes ) )
    {
        CONSOLE_Printf( "SPI repeat stream exceeds the upload length range.\r\n" );
        return;
    }

    const uint32_t upload_bytes  = repeat_count * instruction_bytes;
    console_flash_run_tick_count = 0U;

    FlashManagerInstructionUploadRequestStatus_T status =
        FLASH_MANAGER_RequestInstructionUploadStart( upload_bytes );
    if ( status != FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_ACCEPTED
         || !CONSOLE_Flash_WaitForState( FLASH_MANAGER_STATE_INSTRUCTION_UPLOAD,
                                         CONSOLE_FLASH_STATE_TIMEOUT_MS ) )
    {
        CONSOLE_Printf( "SPI upload start failed (status=%d).\r\n", ( int )status );
        return;
    }

    for ( uint32_t repeat_index = 0U; repeat_index < repeat_count; repeat_index++ )
    {
        const uint32_t transmit_tick = first_tick + ( repeat_index * interval_ticks );
        ( void )CONSOLE_Flash_EncodeSpiInstruction( console_flash_write_buffer, transmit_tick,
                                                    ( uint8_t )( channel - 1U ), ( uint8_t )value,
                                                    length );

        TickType_t progress_started_at = xTaskGetTickCount();
        for ( ;; )
        {
            status = FLASH_MANAGER_SubmitInstructionUploadBytes( console_flash_write_buffer,
                                                                 instruction_bytes );
            if ( status == FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_ACCEPTED )
            {
                break;
            }

            if ( status != FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_BUSY )
            {
                CONSOLE_Printf( "SPI instruction %lu submission failed (status=%d).\r\n",
                                ( unsigned long )( repeat_index + 1U ), ( int )status );
                return;
            }

            if ( CONSOLE_Flash_HasTimedOut( progress_started_at,
                                            CONSOLE_FLASH_PROGRESS_TIMEOUT_MS ) )
            {
                CONSOLE_Printf( "SPI upload timed out at instruction %lu.\r\n",
                                ( unsigned long )( repeat_index + 1U ) );
                return;
            }

            vTaskDelay( pdMS_TO_TICKS( CONSOLE_FLASH_POLL_PERIOD_MS ) );
        }
    }

    TickType_t finish_started_at = xTaskGetTickCount();
    do
    {
        status = FLASH_MANAGER_RequestInstructionUploadFinish();
        if ( status == FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_BUSY )
        {
            if ( CONSOLE_Flash_HasTimedOut( finish_started_at, CONSOLE_FLASH_PROGRESS_TIMEOUT_MS ) )
            {
                CONSOLE_Printf( "SPI upload finalisation timed out.\r\n" );
                return;
            }
            vTaskDelay( pdMS_TO_TICKS( CONSOLE_FLASH_POLL_PERIOD_MS ) );
        }
    } while ( status == FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_BUSY );

    if ( status != FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_ACCEPTED
         || !CONSOLE_Flash_WaitForState( FLASH_MANAGER_STATE_IDLE,
                                         CONSOLE_FLASH_STATE_TIMEOUT_MS ) )
    {
        CONSOLE_Printf( "SPI upload finalisation failed (status=%d).\r\n", ( int )status );
        return;
    }

    console_flash_last_upload_records = repeat_count;
    console_flash_last_upload_bytes   = upload_bytes;
    console_flash_run_tick_count      = run_ticks;
    CONSOLE_Flash_ResetExecutionHarnessState();

    CONSOLE_Printf( "SPI upload PASS: channel=%lu byte=0x%02lX packet_length=%lu "
                    "first_tick=%lu repeats=%lu interval_ticks=%lu run_ticks=%lu.\r\n",
                    ( unsigned long )channel, ( unsigned long )value, ( unsigned long )length,
                    ( unsigned long )first_tick, ( unsigned long )repeat_count,
                    ( unsigned long )interval_ticks, ( unsigned long )run_ticks );
    CONSOLE_Printf( "Next: finish configuring the RSM, then 'run_state execute %lu'.\r\n",
                    ( unsigned long )console_flash_run_tick_count );
}

/** Uploads one or more variable-length UART bursts through the production instruction path. */
static void CONSOLE_Flash_UploadUartTestCommand( uint16_t argc, char* argv[] )
{
    uint32_t channel        = 0U;
    uint32_t value          = 0U;
    uint32_t length         = 0U;
    uint32_t first_tick     = 0U;
    uint32_t run_ticks      = 0U;
    uint32_t repeat_count   = 1U;
    uint32_t interval_ticks = 0U;

    if ( ( argc != 7U && argc != 9U ) || !CONSOLE_Flash_ParseU32( argv[2], &channel )
         || !CONSOLE_Flash_ParseU32( argv[3], &value )
         || !CONSOLE_Flash_ParseU32( argv[4], &length )
         || !CONSOLE_Flash_ParseU32( argv[5], &first_tick )
         || !CONSOLE_Flash_ParseU32( argv[6], &run_ticks )
         || ( argc == 9U
              && ( !CONSOLE_Flash_ParseU32( argv[7], &repeat_count )
                   || !CONSOLE_Flash_ParseU32( argv[8], &interval_ticks ) ) )
         || channel < 1U || channel > EXEC_UART_CHANNEL_COUNT || value > UINT8_MAX || length == 0U
         || length > UINT16_MAX || first_tick == 0U || repeat_count == 0U
         || ( repeat_count > 1U && interval_ticks == 0U ) )
    {
        CONSOLE_Printf( "Usage: flash upload_uart_test <channel 1..2> <byte 0..255> <length> "
                        "<first_tick > 0> <run_ticks> [repeat_count interval_ticks]\r\n" );
        return;
    }

    if ( repeat_count > 1U
         && ( repeat_count - 1U ) > ( ( UINT32_MAX - first_tick ) / interval_ticks ) )
    {
        CONSOLE_Printf( "UART repeat schedule exceeds the timestamp range.\r\n" );
        return;
    }

    const uint32_t last_tick = first_tick + ( ( repeat_count - 1U ) * interval_ticks );
    if ( run_ticks < last_tick )
    {
        CONSOLE_Printf( "Run ticks must include the final UART transmit tick (%lu).\r\n",
                        ( unsigned long )last_tick );
        return;
    }

    if ( !CONSOLE_Flash_RequireIdle() )
    {
        return;
    }

    const uint32_t operation_bytes = EXECUTION_OPERATION_ENCODED_SIZE_BYTES( length );
    const uint32_t instruction_bytes =
        ( uint32_t )sizeof( ExecutionInstructionHeader_T ) + operation_bytes;
    if ( instruction_bytes > sizeof( console_flash_write_buffer ) )
    {
        CONSOLE_Printf( "UART instruction exceeds the %u-byte console staging buffer.\r\n",
                        ( unsigned int )sizeof( console_flash_write_buffer ) );
        return;
    }

    if ( repeat_count > ( UINT32_MAX / instruction_bytes ) )
    {
        CONSOLE_Printf( "UART repeat stream exceeds the upload length range.\r\n" );
        return;
    }

    const uint32_t upload_bytes = repeat_count * instruction_bytes;

    console_flash_run_tick_count = 0U;

    FlashManagerInstructionUploadRequestStatus_T status =
        FLASH_MANAGER_RequestInstructionUploadStart( upload_bytes );
    if ( status != FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_ACCEPTED
         || !CONSOLE_Flash_WaitForState( FLASH_MANAGER_STATE_INSTRUCTION_UPLOAD,
                                         CONSOLE_FLASH_STATE_TIMEOUT_MS ) )
    {
        CONSOLE_Printf( "UART upload start failed (status=%d).\r\n", ( int )status );
        return;
    }

    for ( uint32_t repeat_index = 0U; repeat_index < repeat_count; repeat_index++ )
    {
        const uint32_t transmit_tick = first_tick + ( repeat_index * interval_ticks );
        ( void )CONSOLE_Flash_EncodeUartInstruction( console_flash_write_buffer, transmit_tick,
                                                     ( uint8_t )( channel - 1U ), ( uint8_t )value,
                                                     ( uint16_t )length );

        TickType_t progress_started_at = xTaskGetTickCount();
        for ( ;; )
        {
            status = FLASH_MANAGER_SubmitInstructionUploadBytes( console_flash_write_buffer,
                                                                 instruction_bytes );
            if ( status == FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_ACCEPTED )
            {
                break;
            }

            if ( status != FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_BUSY )
            {
                CONSOLE_Printf( "UART instruction %lu submission failed (status=%d).\r\n",
                                ( unsigned long )( repeat_index + 1U ), ( int )status );
                return;
            }

            if ( CONSOLE_Flash_HasTimedOut( progress_started_at,
                                            CONSOLE_FLASH_PROGRESS_TIMEOUT_MS ) )
            {
                CONSOLE_Printf( "UART upload timed out at instruction %lu.\r\n",
                                ( unsigned long )( repeat_index + 1U ) );
                return;
            }

            vTaskDelay( pdMS_TO_TICKS( CONSOLE_FLASH_POLL_PERIOD_MS ) );
        }
    }

    TickType_t finish_started_at = xTaskGetTickCount();
    do
    {
        status = FLASH_MANAGER_RequestInstructionUploadFinish();
        if ( status == FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_BUSY )
        {
            if ( CONSOLE_Flash_HasTimedOut( finish_started_at, CONSOLE_FLASH_PROGRESS_TIMEOUT_MS ) )
            {
                CONSOLE_Printf( "UART upload finalisation timed out.\r\n" );
                return;
            }
            vTaskDelay( pdMS_TO_TICKS( CONSOLE_FLASH_POLL_PERIOD_MS ) );
        }
    } while ( status == FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_BUSY );

    if ( status != FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_ACCEPTED
         || !CONSOLE_Flash_WaitForState( FLASH_MANAGER_STATE_IDLE,
                                         CONSOLE_FLASH_STATE_TIMEOUT_MS ) )
    {
        CONSOLE_Printf( "UART upload finalisation failed (status=%d).\r\n", ( int )status );
        return;
    }

    console_flash_last_upload_records = repeat_count;
    console_flash_last_upload_bytes   = upload_bytes;
    console_flash_run_tick_count      = run_ticks;
    CONSOLE_Flash_ResetExecutionHarnessState();

    CONSOLE_Printf( "UART upload PASS: channel=%lu byte=0x%02lX length=%u "
                    "first_tick=%lu repeats=%lu interval_ticks=%lu run_ticks=%lu.\r\n",
                    ( unsigned long )channel, ( unsigned long )value, ( unsigned int )length,
                    ( unsigned long )first_tick, ( unsigned long )repeat_count,
                    ( unsigned long )interval_ticks, ( unsigned long )run_ticks );
    CONSOLE_Printf( "Next: finish configuring the RSM, then run 'run_state execute %lu'.\r\n",
                    ( unsigned long )console_flash_run_tick_count );
}

/** Configures and uploads repeated peak-load output instructions for TIM4 profiling. */
static void CONSOLE_Flash_UploadOutputStressTestCommand( uint16_t argc, char* argv[] )
{
    uint32_t sample_count   = CONSOLE_FLASH_OUTPUT_STRESS_DEFAULT_SAMPLES;
    uint32_t interval_ticks = CONSOLE_FLASH_OUTPUT_STRESS_DEFAULT_INTERVAL_TICKS;

    if ( argc > 4U || ( argc >= 3U && !CONSOLE_Flash_ParseU32( argv[2], &sample_count ) )
         || ( argc == 4U && !CONSOLE_Flash_ParseU32( argv[3], &interval_ticks ) )
         || sample_count == 0U || interval_ticks == 0U
         || ( sample_count - 1U ) > ( ( UINT32_MAX - 1U - CONSOLE_FLASH_OUTPUT_STRESS_DRAIN_TICKS )
                                      / interval_ticks ) )
    {
        CONSOLE_Printf(
            "Usage: flash upload_output_stress [sample_count > 0] [interval_ticks > 0]\r\n" );
        return;
    }

    RunStateManagerStatus_T run_status = { 0 };
    RUN_STATE_MANAGER_GetStatus( &run_status );
    if ( run_status.state != RUN_STATE_TEST_PACKAGE_RECEIVE
         || !CONSOLE_Flash_WaitForState( FLASH_MANAGER_STATE_INSTRUCTION_UPLOAD,
                                         CONSOLE_FLASH_STATE_TIMEOUT_MS ) )
    {
        CONSOLE_Printf( "Output stress setup requires an RSM-owned instruction upload.\r\n" );
        return;
    }

    GPIOOutput_T high_outputs[EXEC_DIGITAL_OUTPUT_CHANNEL_COUNT];
    uint32_t     output_pin_masks[EXEC_DIGITAL_OUTPUT_CHANNEL_COUNT];
    for ( uint32_t channel = 0U; channel < EXEC_DIGITAL_OUTPUT_CHANNEL_COUNT; channel++ )
    {
        high_outputs[channel] = ( GPIOOutput_T )( DIGITAL_OUTPUT_0 + channel );
        output_pin_masks[channel] =
            EXEC_DIGITAL_OUTPUT_Combine_Port_Pin_Masks( &high_outputs[channel], 1U );
        if ( output_pin_masks[channel] == 0U )
        {
            CONSOLE_Printf( "Output stress DO%lu mapping failed.\r\n",
                            ( unsigned long )( channel + 1U ) );
            return;
        }
    }
    const uint32_t all_output_mask = EXEC_DIGITAL_OUTPUT_Combine_Port_Pin_Masks(
        high_outputs, ( uint8_t )EXEC_DIGITAL_OUTPUT_CHANNEL_COUNT );

    ExecutionPwmUpdatePayload_T pwm_payloads[EXEC_PWM_GEN_CHANNEL_COUNT] = { 0 };
    const uint32_t              pwm_clocks[EXEC_PWM_GEN_CHANNEL_COUNT]   = {
        CONSOLE_FLASH_PWM_LV_TIMER_CLOCK_HZ,
        CONSOLE_FLASH_PWM_HV_TIMER_CLOCK_HZ,
    };
    for ( uint32_t channel = 0U; channel < EXEC_PWM_GEN_CHANNEL_COUNT; channel++ )
    {
        if ( !HW_PWM_GEN_compute_psc( CONSOLE_FLASH_OUTPUT_STRESS_PWM_FREQ_HZ, pwm_clocks[channel],
                                      &pwm_payloads[channel].psc )
             || !HW_PWM_GEN_compute_arr( CONSOLE_FLASH_OUTPUT_STRESS_PWM_FREQ_HZ,
                                         pwm_clocks[channel], pwm_payloads[channel].psc,
                                         &pwm_payloads[channel].arr )
             || !HW_PWM_GEN_compute_ccr( CONSOLE_FLASH_OUTPUT_STRESS_PWM_DUTY_PERMILLE,
                                         pwm_payloads[channel].arr, &pwm_payloads[channel].ccr ) )
        {
            CONSOLE_Printf( "Output stress PWM preparation failed.\r\n" );
            return;
        }
    }

    DutDriverConfiguration_T configuration = { 0 };
    for ( uint32_t channel = 0U; channel < EXEC_DIGITAL_OUTPUT_CHANNEL_COUNT; channel++ )
    {
        configuration.digital_outputs.channels[channel] = ( ExecDigitalOutputChannelConfig_T ){
            .is_enabled   = true,
            .mode         = EXEC_DIGITAL_OUTPUT_MODE_3V3,
            .initial_high = false,
        };
    }
    for ( uint32_t channel = 0U; channel < EXEC_DIGITAL_INPUT_CHANNEL_COUNT; channel++ )
    {
        configuration.digital_inputs.channels[channel] = EXEC_DIGITAL_INPUT_MODE_3V3;
    }
    configuration.analogue_output = ( ExecAnalogueOutputConfig_T ){
        .is_enabled        = false,
        .use_external_vref = false,
    };
    configuration.analogue_input = ( ExecAnalogueInputConfig_T ){
        .is_enabled      = true,
        .sample_rate     = EXEC_ANALOGUE_INPUT_SAMPLE_RATE_10K_HZ,
        .ch_0_is_enabled = true,
        .ch_1_is_enabled = true,
    };
    for ( uint32_t channel = 0U; channel < EXEC_PWM_GEN_CHANNEL_COUNT; channel++ )
    {
        configuration.pwm_generation_channels[channel] = ( ExecPwmGenConfig_T ){
            .is_enabled    = true,
            .voltage_level = channel == EXEC_PWM_GEN_CHANNEL_LV ? EXEC_PWM_GEN_VOLTAGE_3V3
                                                                : EXEC_PWM_GEN_VOLTAGE_12V,
            .initial_arr   = pwm_payloads[channel].arr,
            .initial_ccr   = pwm_payloads[channel].ccr,
            .initial_psc   = pwm_payloads[channel].psc,
        };
    }
    configuration.pwm_capture_channels[EXEC_PWM_CAPTURE_CHANNEL_1] = ( ExecPwmCaptureConfig_T ){
        .is_enabled = true,
        .mode       = EXEC_PWM_CAPTURE_LV_3V3,
    };
    configuration.pwm_capture_channels[EXEC_PWM_CAPTURE_CHANNEL_2] = ( ExecPwmCaptureConfig_T ){
        .is_enabled = true,
        .mode       = EXEC_PWM_CAPTURE_HV_12V,
    };
    configuration.spi_channels[EXEC_SPI_CHANNEL_1] = ( ExecSPIConfig_T ){
        .is_enabled = true,
        .spi_mode   = EXEC_SPI_MASTER_MODE,
        .data_size  = EXEC_SPI_SIZE_8_BIT,
        .first_bit  = EXEC_SPI_FIRST_MSB,
        .baud_rate  = EXEC_SPI_BAUD_22M5BIT,
        .cpol       = EXEC_SPI_CPOL_LOW,
        .cpha       = EXEC_SPI_CPHA_1_EDGE,
    };
    configuration.spi_channels[EXEC_SPI_CHANNEL_2] = ( ExecSPIConfig_T ){
        .is_enabled = true,
        .spi_mode   = EXEC_SPI_MASTER_MODE,
        .data_size  = EXEC_SPI_SIZE_8_BIT,
        .first_bit  = EXEC_SPI_FIRST_MSB,
        .baud_rate  = EXEC_SPI_BAUD_45MBIT,
        .cpol       = EXEC_SPI_CPOL_LOW,
        .cpha       = EXEC_SPI_CPHA_1_EDGE,
    };
    for ( uint32_t channel = 0U; channel < EXEC_UART_CHANNEL_COUNT; channel++ )
    {
        configuration.uart_channels[channel] = ( ExecUartConfig_T ){
            .interface_mode = EXEC_UART_MODE_TTL_3V3,
            .baud_rate      = CONSOLE_FLASH_OUTPUT_STRESS_UART_BAUD,
            .word_length    = HW_UART_WORD_LENGTH_8_BITS,
            .stop_bits      = HW_UART_STOP_BITS_1,
            .parity         = HW_UART_PARITY_NONE,
            .rx_enabled     = true,
            .tx_enabled     = true,
            .is_enabled     = true,
        };
    }
    for ( uint32_t channel = 0U; channel < EXEC_CAN_CHANNEL_COUNT; channel++ )
    {
        configuration.can_channels[channel] = ( EXEC_CAN_Config_T ){
            .is_enabled = false,
        };
    }

    if ( !TEST_CONFIGURATION_Commit( &configuration ) )
    {
        CONSOLE_Printf( "Output stress configuration commit failed.\r\n" );
        return;
    }

    const uint32_t instruction_bytes = CONSOLE_Flash_EncodeOutputStressInstruction(
        console_flash_write_buffer, 1U, 0U, all_output_mask, pwm_payloads );
    if ( instruction_bytes > sizeof( console_flash_write_buffer )
         || sample_count > ( UINT32_MAX / instruction_bytes ) )
    {
        CONSOLE_Printf( "Output stress instruction stream exceeds the upload range.\r\n" );
        return;
    }

    const uint32_t                               upload_bytes = sample_count * instruction_bytes;
    FlashManagerInstructionUploadRequestStatus_T status;

    for ( uint32_t sample = 0U; sample < sample_count; sample++ )
    {
        const uint32_t logical_pattern = CONSOLE_Flash_StressLogicalPattern( sample );
        uint32_t       high_bitmask    = 0U;
        for ( uint32_t channel = 0U; channel < EXEC_DIGITAL_OUTPUT_CHANNEL_COUNT; channel++ )
        {
            if ( ( logical_pattern & ( 1UL << channel ) ) != 0U )
            {
                high_bitmask |= output_pin_masks[channel];
            }
        }
        CONSOLE_Flash_WriteU32Le(
            &console_flash_write_buffer[sizeof( ExecutionInstructionHeader_T )
                                        + EXECUTION_OPERATION_HEADER_SIZE_BYTES],
            high_bitmask );
        CONSOLE_Flash_WriteU32Le(
            &console_flash_write_buffer[sizeof( ExecutionInstructionHeader_T )
                                        + EXECUTION_OPERATION_HEADER_SIZE_BYTES
                                        + sizeof( uint32_t )],
            all_output_mask & ~high_bitmask );
        CONSOLE_Flash_WriteU32Le( console_flash_write_buffer, 1U + ( sample * interval_ticks ) );
        TickType_t progress_started_at = xTaskGetTickCount();
        for ( ;; )
        {
            status = FLASH_MANAGER_SubmitInstructionUploadBytes( console_flash_write_buffer,
                                                                 instruction_bytes );
            if ( status == FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_ACCEPTED )
            {
                break;
            }
            if ( status != FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_BUSY
                 || CONSOLE_Flash_HasTimedOut( progress_started_at,
                                               CONSOLE_FLASH_PROGRESS_TIMEOUT_MS ) )
            {
                CONSOLE_Printf( "Output stress upload failed at sample %lu (status=%d).\r\n",
                                ( unsigned long )( sample + 1U ), ( int )status );
                return;
            }
            vTaskDelay( pdMS_TO_TICKS( CONSOLE_FLASH_POLL_PERIOD_MS ) );
        }
    }

    TickType_t finish_started_at = xTaskGetTickCount();
    do
    {
        status = FLASH_MANAGER_RequestInstructionUploadFinish();
        if ( status == FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_BUSY )
        {
            if ( CONSOLE_Flash_HasTimedOut( finish_started_at, CONSOLE_FLASH_PROGRESS_TIMEOUT_MS ) )
            {
                CONSOLE_Printf( "Output stress upload finalisation timed out.\r\n" );
                return;
            }
            vTaskDelay( pdMS_TO_TICKS( CONSOLE_FLASH_POLL_PERIOD_MS ) );
        }
    } while ( status == FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_BUSY );

    if ( status != FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_ACCEPTED
         || !CONSOLE_Flash_WaitForState( FLASH_MANAGER_STATE_IDLE,
                                         CONSOLE_FLASH_STATE_TIMEOUT_MS ) )
    {
        CONSOLE_Printf( "Output stress upload finalisation failed (status=%d).\r\n",
                        ( int )status );
        return;
    }

    console_flash_last_upload_records   = sample_count;
    console_flash_last_upload_bytes     = upload_bytes;
    console_flash_stress_sample_count   = sample_count;
    console_flash_stress_interval_ticks = interval_ticks;
    console_flash_run_tick_count =
        1U + ( ( sample_count - 1U ) * interval_ticks ) + CONSOLE_FLASH_OUTPUT_STRESS_DRAIN_TICKS;
    CONSOLE_Flash_ResetExecutionHarnessState();
    EXECUTION_MANAGER_RequestOperationTiming();

    CONSOLE_Printf( "Stress test upload PASS: samples=%lu interval=%lu ticks instruction=%lu bytes "
                    "run_ticks=%lu.\r\n",
                    ( unsigned long )sample_count, ( unsigned long )interval_ticks,
                    ( unsigned long )instruction_bytes,
                    ( unsigned long )console_flash_run_tick_count );
    CONSOLE_Printf( "Active paths: DO 1..10 (3.3V) -> DI 1..10 (3.3V)\r\n" );
    CONSOLE_Printf( "              AI (10kHz DMA dual-ch)\r\n" );
    CONSOLE_Printf( "              PWM Gen LV (1MHz 50%%) -> PWM Cap LV (3.3V)\r\n" );
    CONSOLE_Printf( "              PWM Gen HV (1MHz 50%%) -> PWM Cap HV (12V)\r\n" );
    CONSOLE_Printf( "              UART1 (2Mbps TX+RX loopback, 16B/tick 0x55)\r\n" );
    CONSOLE_Printf( "              UART2 (2Mbps TX+RX loopback, 16B/tick 0xAA)\r\n" );
    CONSOLE_Printf( "              SPI1 (22.5Mbps TX+RX loopback, 128B/tick 0x5A)\r\n" );
    CONSOLE_Printf( "              SPI2 (45Mbps TX+RX loopback, 256B/tick 0xA5)\r\n" );
    CONSOLE_Printf( "Excluded:     AO, CAN 1/2.\r\n" );
    CONSOLE_Printf( "Next: 'run_state configure', 'run_state frequency 10000', then "
                    "'run_state execute %lu'.\r\n",
                    ( unsigned long )console_flash_run_tick_count );
    CONSOLE_Printf( "Verify: 'flash results verify_stress' and 'execution status'.\r\n" );
}

/** Uploads a deterministic framing-compatible instruction stream through Flash Manager. */
static void CONSOLE_Flash_UploadTestCommand( uint16_t argc, char* argv[] )
{
    if ( !CONSOLE_Flash_RequireIdle() )
    {
        return;
    }

    console_flash_run_tick_count = 0U;

    ExternalFlashInfo_T info = { 0 };
    if ( EXTERNAL_FLASH_GetInfo( &info ) != EXTERNAL_FLASH_STATUS_OK )
    {
        CONSOLE_Printf( "External Flash GetInfo failed.\r\n" );
        return;
    }

    const uint32_t record_length_bytes =
        ( uint32_t )sizeof( ExecutionInstructionHeader_T ) + CONSOLE_FLASH_TEST_PAYLOAD_BYTES;

    if ( ( info.page_size_bytes < record_length_bytes )
         || ( info.page_size_bytes > sizeof( console_flash_write_buffer ) ) )
    {
        CONSOLE_Printf( "Unsupported NAND page size: %lu.\r\n",
                        ( unsigned long )info.page_size_bytes );
        return;
    }

    uint32_t record_count = ( ( info.page_size_bytes * 4U ) / record_length_bytes ) + 3U;
    uint32_t seed_value   = CONSOLE_FLASH_DEFAULT_SEED;

    if ( ( argc >= 3U ) && !CONSOLE_Flash_ParseU32( argv[2], &record_count ) )
    {
        CONSOLE_Printf( "Invalid record count.\r\n" );
        return;
    }
    if ( ( argc >= 4U ) && !CONSOLE_Flash_ParseU32( argv[3], &seed_value ) )
    {
        CONSOLE_Printf( "Invalid seed.\r\n" );
        return;
    }
    if ( ( argc > 4U ) || ( record_count == 0U ) || ( seed_value > UINT8_MAX )
         || ( record_count > ( UINT32_MAX / record_length_bytes ) ) )
    {
        CONSOLE_Printf( "Usage: flash upload_test [instruction_count > 0] [seed 0..255]\r\n" );
        return;
    }

    uint32_t expected_length = record_count * record_length_bytes;
    if ( expected_length > info.instruction_capacity_bytes )
    {
        CONSOLE_Printf( "Instruction stream exceeds capacity (%lu > %lu).\r\n",
                        ( unsigned long )expected_length,
                        ( unsigned long )info.instruction_capacity_bytes );
        return;
    }

    FlashManagerInstructionUploadRequestStatus_T request_status =
        FLASH_MANAGER_RequestInstructionUploadStart( expected_length );
    if ( request_status != FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_ACCEPTED )
    {
        CONSOLE_Printf( "Flash Manager upload start rejected (status=%d).\r\n",
                        ( int )request_status );
        return;
    }

    /* A newly accepted destructive upload invalidates the preceding echo metadata. */
    console_flash_last_upload_records = 0U;
    console_flash_last_upload_bytes   = 0U;
    CONSOLE_Flash_ResetExecutionHarnessState();

    if ( !CONSOLE_Flash_WaitForState( FLASH_MANAGER_STATE_INSTRUCTION_UPLOAD,
                                      CONSOLE_FLASH_STATE_TIMEOUT_MS ) )
    {
        return;
    }

    uint32_t stream_offset  = 0U;
    uint32_t chunk_capacity = ( info.page_size_bytes / 2U ) + 7U;
    if ( chunk_capacity > info.page_size_bytes )
    {
        chunk_capacity = info.page_size_bytes;
    }

    while ( stream_offset < expected_length )
    {
        uint32_t remaining_bytes = expected_length - stream_offset;
        uint32_t chunk_length =
            ( remaining_bytes < chunk_capacity ) ? remaining_bytes : chunk_capacity;

        CONSOLE_Flash_FillInstructionChunk( console_flash_write_buffer, stream_offset, chunk_length,
                                            ( uint8_t )seed_value );

        TickType_t progress_start = xTaskGetTickCount();
        for ( ;; )
        {
            request_status = FLASH_MANAGER_SubmitInstructionUploadBytes( console_flash_write_buffer,
                                                                         chunk_length );

            if ( request_status == FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_ACCEPTED )
            {
                break;
            }

            if ( request_status != FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_BUSY )
            {
                CONSOLE_Printf( "Instruction chunk rejected at offset %lu (status=%d).\r\n",
                                ( unsigned long )stream_offset, ( int )request_status );
                return;
            }

            if ( CONSOLE_Flash_HasTimedOut( progress_start, CONSOLE_FLASH_PROGRESS_TIMEOUT_MS ) )
            {
                CONSOLE_Printf( "Timeout draining upload ring at offset %lu.\r\n",
                                ( unsigned long )stream_offset );
                return;
            }

            vTaskDelay( pdMS_TO_TICKS( CONSOLE_FLASH_POLL_PERIOD_MS ) );
        }

        stream_offset += chunk_length;
    }

    TickType_t finish_start = xTaskGetTickCount();
    for ( ;; )
    {
        request_status = FLASH_MANAGER_RequestInstructionUploadFinish();
        if ( request_status == FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_ACCEPTED )
        {
            break;
        }

        if ( request_status != FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_BUSY )
        {
            CONSOLE_Printf( "Instruction upload finish rejected (status=%d).\r\n",
                            ( int )request_status );
            return;
        }

        if ( CONSOLE_Flash_HasTimedOut( finish_start, CONSOLE_FLASH_PROGRESS_TIMEOUT_MS ) )
        {
            CONSOLE_Printf( "Timeout waiting to finalise instruction upload.\r\n" );
            return;
        }

        vTaskDelay( pdMS_TO_TICKS( CONSOLE_FLASH_POLL_PERIOD_MS ) );
    }

    if ( !CONSOLE_Flash_WaitForState( FLASH_MANAGER_STATE_IDLE, CONSOLE_FLASH_STATE_TIMEOUT_MS ) )
    {
        return;
    }

    if ( ( EXTERNAL_FLASH_GetInfo( &info ) != EXTERNAL_FLASH_STATUS_OK )
         || ( info.instruction_length_bytes != expected_length ) )
    {
        CONSOLE_Printf( "Instruction upload length verification failed.\r\n" );
        return;
    }

    console_flash_last_upload_records = record_count;
    console_flash_last_upload_bytes   = expected_length;
    console_flash_last_upload_seed    = ( uint8_t )seed_value;

    CONSOLE_Printf( "Flash Manager upload PASS: instructions=%lu bytes=%lu seed=0x%02X.\r\n",
                    ( unsigned long )record_count, ( unsigned long )expected_length,
                    ( unsigned int )seed_value );
    CONSOLE_Printf(
        "ISR echo format: one instruction per tick with 12 opaque operation bytes.\r\n" );
    CONSOLE_Printf( "Next: 'flash prepare', then 'flash execute_echo 100'.\r\n" );
}

/** Requests execution preparation and waits for instruction prefill. */
static void CONSOLE_Flash_PrepareCommand( void )
{
    FlashManagerRequestStatus_T status =
        FLASH_MANAGER_RequestExecutionPreparation( console_flash_last_upload_bytes );
    if ( status != FLASH_MANAGER_REQUEST_OK )
    {
        CONSOLE_Printf( "Execution preparation rejected (status=%d).\r\n", ( int )status );
        return;
    }

    if ( CONSOLE_Flash_WaitForState( FLASH_MANAGER_STATE_EXECUTING,
                                     CONSOLE_FLASH_STATE_TIMEOUT_MS ) )
    {
        CONSOLE_Printf( "Execution preparation PASS. Flash Manager is EXECUTING.\r\n" );
        CONSOLE_Printf( "Next: 'flash execute_echo 100'. Do not finalise while TIM4 runs.\r\n" );
    }
}

/**
 * Runs the deterministic instruction-to-result echo through the real TIM4 ISR.
 *
 * The command blocks only its own console task. Short delays in the wait loop
 * leave the Flash Manager task free to preload instructions and drain results.
 */
static void CONSOLE_Flash_ExecuteEchoCommand( uint16_t argc, char* argv[] )
{
    uint32_t frequency_hz = CONSOLE_FLASH_EXECUTION_DEFAULT_FREQUENCY_HZ;
    uint32_t prescaler    = 0U;
    uint32_t auto_reload  = 0U;

    if ( ( argc >= 3U ) && !CONSOLE_Flash_ParseU32( argv[2], &frequency_hz ) )
    {
        CONSOLE_Printf( "Invalid execution frequency.\r\n" );
        return;
    }

    if ( ( argc > 3U )
         || !CONSOLE_Flash_GetExecutionTimerSettings( frequency_hz, &prescaler, &auto_reload ) )
    {
        CONSOLE_Printf( "Usage: flash execute_echo [100|1000|10000]\r\n" );
        return;
    }

    if ( console_flash_execution_test.state == CONSOLE_FLASH_EXECUTION_TEST_RUNNING )
    {
        CONSOLE_Printf( "Execution echo test is already running.\r\n" );
        return;
    }

    if ( console_flash_last_upload_records == 0U )
    {
        CONSOLE_Printf( "No diagnostic upload is registered. Run 'flash upload_test'.\r\n" );
        return;
    }

    FlashManagerState_T manager_state = FLASH_MANAGER_STATE_UNINITIALISED;
    if ( !FLASH_MANAGER_GetState( &manager_state )
         || ( manager_state != FLASH_MANAGER_STATE_EXECUTING ) )
    {
        CONSOLE_Printf( "Flash Manager must be EXECUTING (current=%s).\r\n",
                        CONSOLE_Flash_StateName( manager_state ) );
        return;
    }

    uint32_t timer_clock_hz = HW_TIMER_Get_Clock_Hz( EXECUTION_MANAGER_TIMER );
    if ( timer_clock_hz != CONSOLE_FLASH_EXECUTION_EXPECTED_TIMER_CLOCK_HZ )
    {
        CONSOLE_Printf( "TIM4 clock mismatch: expected=%lu Hz actual=%lu Hz. "
                        "Do not run the echo test.\r\n",
                        ( unsigned long )CONSOLE_FLASH_EXECUTION_EXPECTED_TIMER_CLOCK_HZ,
                        ( unsigned long )timer_clock_hz );
        return;
    }

    /*
     * TIM4 is shared with the future production Execution Manager. Stop it
     * before replacing the callback so the two consumers can never overlap.
     */
    HW_TIMER_Stop_Timer( EXECUTION_MANAGER_TIMER );

    CONSOLE_Flash_ResetExecutionHarnessState();
    console_flash_execution_test.frequency_hz     = frequency_hz;
    console_flash_execution_test.expected_records = console_flash_last_upload_records;

    HW_TIMER_Configure_Timer( EXECUTION_MANAGER_TIMER, prescaler, auto_reload );
    HW_TIMER_Set_Execution_Callback( CONSOLE_Flash_ExecutionEchoFromISR );
    console_flash_execution_test.state = CONSOLE_FLASH_EXECUTION_TEST_RUNNING;

    CONSOLE_Printf( "Starting TIM4 execution echo: %lu Hz, instructions=%lu.\r\n",
                    ( unsigned long )frequency_hz,
                    ( unsigned long )console_flash_execution_test.expected_records );

    HW_TIMER_Start_Timer( EXECUTION_MANAGER_TIMER );

    uint32_t timeout_ms = CONSOLE_Flash_GetExecutionTimeoutMs(
        console_flash_execution_test.expected_records, frequency_hz );
    TickType_t start_tick = xTaskGetTickCount();

    while ( console_flash_execution_test.state == CONSOLE_FLASH_EXECUTION_TEST_RUNNING )
    {
        if ( CONSOLE_Flash_HasTimedOut( start_tick, timeout_ms ) )
        {
            CONSOLE_Flash_StopExecutionHarness();
            console_flash_execution_test.failure = CONSOLE_FLASH_EXECUTION_FAILURE_TIMEOUT;
            console_flash_execution_test.state   = CONSOLE_FLASH_EXECUTION_TEST_FAILED;
            break;
        }

        vTaskDelay( pdMS_TO_TICKS( CONSOLE_FLASH_POLL_PERIOD_MS ) );
    }

    /* Idempotent after ISR completion and mandatory after a task-side timeout. */
    CONSOLE_Flash_StopExecutionHarness();

    FlashManagerState_T terminal_manager_state = FLASH_MANAGER_STATE_UNINITIALISED;
    if ( ( console_flash_execution_test.state == CONSOLE_FLASH_EXECUTION_TEST_COMPLETE )
         && ( !FLASH_MANAGER_GetState( &terminal_manager_state )
              || ( terminal_manager_state != FLASH_MANAGER_STATE_EXECUTING ) ) )
    {
        console_flash_execution_test.failure = CONSOLE_FLASH_EXECUTION_FAILURE_FLASH_MANAGER_STATE;
        console_flash_execution_test.state   = CONSOLE_FLASH_EXECUTION_TEST_FAILED;
    }

    if ( console_flash_execution_test.state == CONSOLE_FLASH_EXECUTION_TEST_COMPLETE )
    {
        CONSOLE_Printf(
            "Execution echo PASS: interrupts=%lu final_tick=%lu consumed=%lu "
            "future_peeks=%lu.\r\n",
            ( unsigned long )console_flash_execution_test.timer_interrupts,
            ( unsigned long )console_flash_execution_test.current_tick,
            ( unsigned long )console_flash_execution_test.instructions_consumed,
            ( unsigned long )console_flash_execution_test.future_instruction_deferrals );
        CONSOLE_Printf( "Next: 'flash finalise', then 'flash results verify'.\r\n" );
        return;
    }

    CONSOLE_Printf( "Execution echo FAILED: reason=%s interrupts=%lu tick=%lu consumed=%lu/%lu "
                    "peek_status=%d commit_status=%d.\r\n",
                    CONSOLE_Flash_ExecutionFailureName( console_flash_execution_test.failure ),
                    ( unsigned long )console_flash_execution_test.timer_interrupts,
                    ( unsigned long )console_flash_execution_test.current_tick,
                    ( unsigned long )console_flash_execution_test.instructions_consumed,
                    ( unsigned long )console_flash_execution_test.expected_records,
                    ( int )console_flash_execution_test.last_instruction_status,
                    ( int )console_flash_execution_test.last_commit_status );
    CONSOLE_Printf(
        "Do not run verify. Inspect 'flash status'; finalise/read partial results or reset.\r\n" );
}

/** Requests final result publication/drain after execution has stopped. */
static void CONSOLE_Flash_FinaliseCommand( void )
{
    if ( console_flash_execution_test.state == CONSOLE_FLASH_EXECUTION_TEST_RUNNING )
    {
        CONSOLE_Printf( "Cannot finalise while the TIM4 execution harness is running.\r\n" );
        return;
    }

    FlashManagerRequestStatus_T status = FLASH_MANAGER_RequestResultFinalisation();
    if ( status != FLASH_MANAGER_REQUEST_OK )
    {
        CONSOLE_Printf( "Result finalisation rejected (status=%d).\r\n", ( int )status );
        return;
    }

    if ( CONSOLE_Flash_WaitForState( FLASH_MANAGER_STATE_RESULTS_READY,
                                     CONSOLE_FLASH_STATE_TIMEOUT_MS ) )
    {
        CONSOLE_Printf( "Result finalisation PASS. Next: 'flash results [verify]'.\r\n" );
    }
}

/** Retrieves the complete packed result stream through the public Host API. */
static void CONSOLE_Flash_ResultsCommand( bool verify_echo_stream )
{
    if ( verify_echo_stream && ( console_flash_last_upload_records == 0U ) )
    {
        CONSOLE_Printf( "No diagnostic upload is available to verify.\r\n" );
        return;
    }

    if ( verify_echo_stream
         && ( console_flash_execution_test.state != CONSOLE_FLASH_EXECUTION_TEST_COMPLETE ) )
    {
        CONSOLE_Printf( "Echo verification requires a completed 'flash execute_echo'.\r\n" );
        return;
    }

    if ( !RUN_STATE_MANAGER_RequestResultTransfer()
         || !CONSOLE_Flash_WaitForRunState( RUN_STATE_RESULT_TRANSFER,
                                            CONSOLE_FLASH_STATE_TIMEOUT_MS ) )
    {
        CONSOLE_Printf( "RSM result transfer start failed.\r\n" );
        return;
    }

    FlashManagerResultTransferStatus_T status = FLASH_MANAGER_RESULT_TRANSFER_OK;

    uint32_t   total_bytes              = 0U;
    uint32_t   hash                     = CONSOLE_FLASH_FNV1A_OFFSET_BASIS;
    uint32_t   busy_retries             = 0U;
    TickType_t last_progress_at         = xTaskGetTickCount();
    bool       byte_verification_passed = true;
    bool       mismatch_captured        = false;
    uint32_t   first_bad_offset         = 0U;
    uint8_t    first_expected           = 0U;
    uint8_t    first_actual             = 0U;

    for ( ;; )
    {
        uint32_t bytes_read = 0U;
        status              = FLASH_MANAGER_ReadResultBytes( console_flash_read_buffer,
                                                             CONSOLE_FLASH_RESULT_READ_BYTES, &bytes_read );

        if ( status == FLASH_MANAGER_RESULT_TRANSFER_OK )
        {
            if ( verify_echo_stream && byte_verification_passed )
            {
                if ( ( total_bytes > console_flash_last_upload_bytes )
                     || ( bytes_read > ( console_flash_last_upload_bytes - total_bytes ) ) )
                {
                    byte_verification_passed = false;
                }
                else
                {
                    CONSOLE_Flash_FillInstructionChunk( console_flash_write_buffer, total_bytes,
                                                        bytes_read,
                                                        console_flash_last_upload_seed );

                    for ( uint32_t index = 0U; index < bytes_read; index++ )
                    {
                        if ( console_flash_read_buffer[index] != console_flash_write_buffer[index] )
                        {
                            byte_verification_passed = false;
                            mismatch_captured        = true;
                            first_bad_offset         = total_bytes + index;
                            first_expected           = console_flash_write_buffer[index];
                            first_actual             = console_flash_read_buffer[index];
                            break;
                        }
                    }
                }
            }

            hash = CONSOLE_Flash_Fnv1aUpdate( hash, console_flash_read_buffer, bytes_read );
            total_bytes += bytes_read;
            last_progress_at = xTaskGetTickCount();
            continue;
        }

        if ( status == FLASH_MANAGER_RESULT_TRANSFER_BUSY )
        {
            busy_retries++;
            if ( CONSOLE_Flash_HasTimedOut( last_progress_at, CONSOLE_FLASH_PROGRESS_TIMEOUT_MS ) )
            {
                CONSOLE_Printf( "Result retrieval timeout after %lu bytes.\r\n",
                                ( unsigned long )total_bytes );
                return;
            }

            vTaskDelay( pdMS_TO_TICKS( CONSOLE_FLASH_POLL_PERIOD_MS ) );
            continue;
        }

        if ( status == FLASH_MANAGER_RESULT_TRANSFER_END_OF_STREAM )
        {
            break;
        }

        CONSOLE_Printf( "Result retrieval failed after %lu bytes (status=%d).\r\n",
                        ( unsigned long )total_bytes, ( int )status );
        return;
    }

    if ( !RUN_STATE_MANAGER_RequestResultTransferComplete()
         || !CONSOLE_Flash_WaitForRunState( RUN_STATE_ARMED, CONSOLE_FLASH_STATE_TIMEOUT_MS ) )
    {
        CONSOLE_Printf( "RSM result transfer completion failed.\r\n" );
        return;
    }

    bool length_verification_passed = total_bytes == console_flash_last_upload_bytes;

    if ( verify_echo_stream && ( !byte_verification_passed || !length_verification_passed ) )
    {
        if ( mismatch_captured )
        {
            CONSOLE_Printf( "Result echo byte verification FAIL at offset %lu: "
                            "expected=0x%02X actual=0x%02X.\r\n",
                            ( unsigned long )first_bad_offset, ( unsigned int )first_expected,
                            ( unsigned int )first_actual );
        }
        if ( !length_verification_passed )
        {
            CONSOLE_Printf( "Result echo length verification FAIL: expected=%lu actual=%lu.\r\n",
                            ( unsigned long )console_flash_last_upload_bytes,
                            ( unsigned long )total_bytes );
        }
        CONSOLE_Printf( "Result transfer was consumed and the same test was rearmed.\r\n" );
        return;
    }

    CONSOLE_Printf( "Result retrieval PASS: bytes=%lu fnv1a=0x%08lX busy_retries=%lu.\r\n",
                    ( unsigned long )total_bytes, ( unsigned long )hash,
                    ( unsigned long )busy_retries );
    CONSOLE_Printf( "Flash Manager returned to IDLE and the same test was rearmed.\r\n" );
}

/** Retrieves and verifies the DO1-to-DI10 production execution result stream. */
static void CONSOLE_Flash_VerifyDigitalLoopbackResultsCommand( uint16_t argc, char* argv[] )
{
    uint32_t   delay_ticks  = 0U;
    uint32_t   high_ticks   = 0U;
    const bool pattern_mode = argc == 3U && strcmp( argv[2], "verify_do_pattern" ) == 0;

    if ( ( pattern_mode && !console_flash_digital_pattern.valid )
         || ( !pattern_mode
              && ( argc != 5U || !CONSOLE_Flash_ParseU32( argv[3], &delay_ticks )
                   || !CONSOLE_Flash_ParseU32( argv[4], &high_ticks ) || delay_ticks == 0U
                   || high_ticks == 0U
                   || ( ( uint64_t )delay_ticks + high_ticks + 1U > UINT32_MAX ) ) ) )
    {
        CONSOLE_Printf( "Usage: flash results verify_do_di <delay_ticks> <high_ticks> | "
                        "verify_do_pattern\r\n" );
        return;
    }

    const uint32_t expected_records =
        pattern_mode ? console_flash_digital_pattern.run_ticks : delay_ticks + high_ticks + 1U;
    if ( !RUN_STATE_MANAGER_RequestResultTransfer()
         || !CONSOLE_Flash_WaitForRunState( RUN_STATE_RESULT_TRANSFER,
                                            CONSOLE_FLASH_STATE_TIMEOUT_MS ) )
    {
        CONSOLE_Printf( "RSM result transfer start failed.\r\n" );
        return;
    }

    FlashManagerResultTransferStatus_T status = FLASH_MANAGER_RESULT_TRANSFER_OK;

    uint8_t    record[CONSOLE_FLASH_DIGITAL_INPUT_RESULT_BYTES] = { 0 };
    uint32_t   record_fill                                      = 0U;
    uint32_t   record_count                                     = 0U;
    uint32_t   first_failure_tick                               = 0U;
    uint32_t   first_failure_sample                             = 0U;
    bool       verification_passed                              = true;
    TickType_t last_progress_at                                 = xTaskGetTickCount();

    for ( ;; )
    {
        uint32_t bytes_read = 0U;
        status              = FLASH_MANAGER_ReadResultBytes( console_flash_read_buffer,
                                                             CONSOLE_FLASH_RESULT_READ_BYTES, &bytes_read );

        if ( status == FLASH_MANAGER_RESULT_TRANSFER_OK )
        {
            uint32_t source_offset = 0U;
            while ( source_offset < bytes_read )
            {
                const uint32_t remaining_record =
                    CONSOLE_FLASH_DIGITAL_INPUT_RESULT_BYTES - record_fill;
                const uint32_t remaining_source = bytes_read - source_offset;
                const uint32_t copy_length =
                    remaining_source < remaining_record ? remaining_source : remaining_record;

                ( void )memcpy( &record[record_fill], &console_flash_read_buffer[source_offset],
                                copy_length );
                record_fill += copy_length;
                source_offset += copy_length;

                if ( record_fill == CONSOLE_FLASH_DIGITAL_INPUT_RESULT_BYTES )
                {
                    FlashManagerResultHeader_T header = { 0 };
                    uint32_t                   sample = 0U;
                    ( void )memcpy( &header, record, sizeof( header ) );
                    ( void )memcpy( &sample, &record[sizeof( header )], sizeof( sample ) );

                    record_count++;
                    bool expected_high = false;
                    if ( pattern_mode
                         && header.timestamp > console_flash_digital_pattern.first_tick )
                    {
                        const uint32_t pattern_bits =
                            console_flash_digital_pattern.repeat_words * 32U;
                        const uint32_t final_low_tick =
                            console_flash_digital_pattern.first_tick
                            + ( pattern_bits * console_flash_digital_pattern.interval_ticks );
                        if ( header.timestamp <= final_low_tick )
                        {
                            uint32_t applied_bit =
                                ( header.timestamp - 1U - console_flash_digital_pattern.first_tick )
                                / console_flash_digital_pattern.interval_ticks;
                            if ( applied_bit >= pattern_bits )
                            {
                                applied_bit = pattern_bits - 1U;
                            }
                            expected_high = ( console_flash_digital_pattern.pattern_word
                                              & ( UINT32_C( 1 ) << ( applied_bit % 32U ) ) )
                                            != 0U;
                        }
                    }
                    else if ( !pattern_mode )
                    {
                        expected_high = header.timestamp > delay_ticks
                                        && header.timestamp <= ( delay_ticks + high_ticks );
                    }
                    const bool valid_record =
                        header.timestamp == record_count
                        && header.peripheral_type == FLASH_MANAGER_RESULT_PERIPHERAL_DIGITAL_INPUT
                        && header.channel == 0U && header.payload_length_bytes == sizeof( uint32_t )
                        && ( ( sample != 0U ) == expected_high );

                    if ( !valid_record && verification_passed )
                    {
                        verification_passed  = false;
                        first_failure_tick   = header.timestamp;
                        first_failure_sample = sample;
                    }
                    record_fill = 0U;
                }
            }

            last_progress_at = xTaskGetTickCount();
            continue;
        }

        if ( status == FLASH_MANAGER_RESULT_TRANSFER_BUSY )
        {
            if ( CONSOLE_Flash_HasTimedOut( last_progress_at, CONSOLE_FLASH_PROGRESS_TIMEOUT_MS ) )
            {
                CONSOLE_Printf( "Digital loopback result retrieval timed out.\r\n" );
                return;
            }
            vTaskDelay( pdMS_TO_TICKS( CONSOLE_FLASH_POLL_PERIOD_MS ) );
            continue;
        }

        if ( status == FLASH_MANAGER_RESULT_TRANSFER_END_OF_STREAM )
        {
            break;
        }

        CONSOLE_Printf( "Digital loopback result retrieval failed (status=%d).\r\n",
                        ( int )status );
        return;
    }

    if ( !RUN_STATE_MANAGER_RequestResultTransferComplete()
         || !CONSOLE_Flash_WaitForRunState( RUN_STATE_ARMED, CONSOLE_FLASH_STATE_TIMEOUT_MS ) )
    {
        CONSOLE_Printf( "RSM result transfer completion failed.\r\n" );
        return;
    }

    if ( record_fill != 0U || record_count != expected_records )
    {
        verification_passed = false;
    }

    if ( verification_passed && pattern_mode )
    {
        CONSOLE_Printf( "DO-pattern-to-DI10 flash verification PASS: records=%lu.\r\n",
                        ( unsigned long )record_count );
    }
    else if ( verification_passed )
    {
        CONSOLE_Printf( "DO1-to-DI10 loopback PASS: records=%lu, LOW ticks=1..%lu, "
                        "HIGH ticks=%lu..%lu, final LOW tick=%lu.\r\n",
                        ( unsigned long )record_count, ( unsigned long )delay_ticks,
                        ( unsigned long )( delay_ticks + 1U ),
                        ( unsigned long )( delay_ticks + high_ticks ),
                        ( unsigned long )expected_records );
    }
    else
    {
        CONSOLE_Printf( "DO1-to-DI10 loopback FAIL: records=%lu/%lu first_bad_tick=%lu "
                        "sample=0x%08lX partial_bytes=%lu.\r\n",
                        ( unsigned long )record_count, ( unsigned long )expected_records,
                        ( unsigned long )first_failure_tick, ( unsigned long )first_failure_sample,
                        ( unsigned long )record_fill );
    }
}

/** Retrieves AI records from flash and compares one channel with the uploaded AO target. */
static void CONSOLE_Flash_VerifyAnalogueLoopbackResultsCommand( uint16_t argc, char* argv[] )
{
    uint32_t input_channel = 0U;
    if ( !console_flash_analogue_loopback.valid || argc != 4U
         || !CONSOLE_Flash_ParseU32( argv[3], &input_channel ) || input_channel > 1U )
    {
        CONSOLE_Printf( "Usage: flash results verify_ao_ai <input_channel 0..1>\r\n" );
        return;
    }

    if ( !RUN_STATE_MANAGER_RequestResultTransfer()
         || !CONSOLE_Flash_WaitForRunState( RUN_STATE_RESULT_TRANSFER,
                                            CONSOLE_FLASH_STATE_TIMEOUT_MS ) )
    {
        CONSOLE_Printf( "RSM result transfer start failed.\r\n" );
        return;
    }

    FlashManagerResultTransferStatus_T status = FLASH_MANAGER_RESULT_TRANSFER_OK;
    uint8_t                            record[CONSOLE_FLASH_ANALOGUE_INPUT_RESULT_BYTES] = { 0 };
    uint32_t                           record_fill                                       = 0U;
    uint32_t                           record_count                                      = 0U;
    uint32_t                           sample_before_update[2] = { 0U, 0U };
    uint32_t                           final_sample[2]         = { 0U, 0U };
    bool                               records_valid           = true;
    TickType_t                         last_progress_at        = xTaskGetTickCount();

    for ( ;; )
    {
        uint32_t bytes_read = 0U;
        status              = FLASH_MANAGER_ReadResultBytes( console_flash_read_buffer,
                                                             CONSOLE_FLASH_RESULT_READ_BYTES, &bytes_read );
        if ( status == FLASH_MANAGER_RESULT_TRANSFER_OK )
        {
            uint32_t source_offset = 0U;
            while ( source_offset < bytes_read )
            {
                const uint32_t record_remaining =
                    CONSOLE_FLASH_ANALOGUE_INPUT_RESULT_BYTES - record_fill;
                const uint32_t source_remaining = bytes_read - source_offset;
                const uint32_t copy_length =
                    source_remaining < record_remaining ? source_remaining : record_remaining;
                ( void )memcpy( &record[record_fill], &console_flash_read_buffer[source_offset],
                                copy_length );
                record_fill += copy_length;
                source_offset += copy_length;

                if ( record_fill == CONSOLE_FLASH_ANALOGUE_INPUT_RESULT_BYTES )
                {
                    FlashManagerResultHeader_T header     = { 0 };
                    uint32_t                   samples[2] = { 0U, 0U };
                    ( void )memcpy( &header, record, sizeof( header ) );
                    ( void )memcpy( samples, &record[sizeof( header )], sizeof( samples ) );
                    record_count++;

                    if ( header.timestamp != record_count
                         || header.peripheral_type != FLASH_MANAGER_RESULT_PERIPHERAL_ANALOGUE_INPUT
                         || header.channel != 0U
                         || header.payload_length_bytes != sizeof( samples ) )
                    {
                        records_valid = false;
                    }
                    if ( header.timestamp == console_flash_analogue_loopback.update_tick )
                    {
                        sample_before_update[0] = samples[0];
                        sample_before_update[1] = samples[1];
                    }
                    final_sample[0] = samples[0];
                    final_sample[1] = samples[1];
                    record_fill     = 0U;
                }
            }
            last_progress_at = xTaskGetTickCount();
            continue;
        }

        if ( status == FLASH_MANAGER_RESULT_TRANSFER_BUSY )
        {
            if ( CONSOLE_Flash_HasTimedOut( last_progress_at, CONSOLE_FLASH_PROGRESS_TIMEOUT_MS ) )
            {
                CONSOLE_Printf( "Analogue loopback result retrieval timed out.\r\n" );
                return;
            }
            vTaskDelay( pdMS_TO_TICKS( CONSOLE_FLASH_POLL_PERIOD_MS ) );
            continue;
        }
        if ( status == FLASH_MANAGER_RESULT_TRANSFER_END_OF_STREAM )
        {
            break;
        }

        CONSOLE_Printf( "Analogue loopback result retrieval failed (status=%d).\r\n",
                        ( int )status );
        return;
    }

    if ( !RUN_STATE_MANAGER_RequestResultTransferComplete()
         || !CONSOLE_Flash_WaitForRunState( RUN_STATE_ARMED, CONSOLE_FLASH_STATE_TIMEOUT_MS ) )
    {
        CONSOLE_Printf( "RSM result transfer completion failed.\r\n" );
        return;
    }

    const bool passed = records_valid && record_fill == 0U
                        && record_count == console_flash_analogue_loopback.run_ticks;

    CONSOLE_Printf( "Analogue result stream validation %s: records=%lu/%lu.\r\n",
                    passed ? "PASS" : "FAIL", ( unsigned long )record_count,
                    ( unsigned long )console_flash_analogue_loopback.run_ticks );
    CONSOLE_Printf( "Loopback route: AO%lu to AI%lu; requested AO target=%lu mV.\r\n",
                    ( unsigned long )console_flash_analogue_loopback.output_channel,
                    ( unsigned long )input_channel,
                    ( unsigned long )console_flash_analogue_loopback.expected_millivolts );
    CONSOLE_Printf( "AI before output update: ch0=%lu, ch1=%lu raw ADC counts.\r\n",
                    ( unsigned long )sample_before_update[0],
                    ( unsigned long )sample_before_update[1] );
    CONSOLE_Printf( "AI final sample: ch0=%lu, ch1=%lu raw ADC counts.\r\n",
                    ( unsigned long )final_sample[0], ( unsigned long )final_sample[1] );
    CONSOLE_Printf( "Voltage comparison: NOT PERFORMED; AI calibration is unavailable.\r\n" );
}

/** Retrieves sparse byte-stream records and verifies a repeated-byte loopback. */
static void CONSOLE_Flash_VerifyByteStreamLoopbackResults( uint16_t argc, char* argv[],
                                                           uint8_t     expected_peripheral_type,
                                                           uint32_t    channel_count,
                                                           const char* peripheral_name )
{
    uint32_t channel = 0U;
    uint32_t value   = 0U;
    uint32_t length  = 0U;
    if ( argc != 6U || !CONSOLE_Flash_ParseU32( argv[3], &channel )
         || !CONSOLE_Flash_ParseU32( argv[4], &value )
         || !CONSOLE_Flash_ParseU32( argv[5], &length ) || channel < 1U || channel > channel_count
         || value > UINT8_MAX || length == 0U )
    {
        CONSOLE_Printf( "Usage: flash results verify_<uart|spi>_loopback <channel 1..%lu> "
                        "<byte 0..255> <length>\r\n",
                        ( unsigned long )channel_count );
        return;
    }

    if ( !RUN_STATE_MANAGER_RequestResultTransfer()
         || !CONSOLE_Flash_WaitForRunState( RUN_STATE_RESULT_TRANSFER,
                                            CONSOLE_FLASH_STATE_TIMEOUT_MS ) )
    {
        CONSOLE_Printf( "RSM result transfer start failed.\r\n" );
        return;
    }

    FlashManagerResultTransferStatus_T status = FLASH_MANAGER_RESULT_TRANSFER_OK;
    uint8_t                            header_bytes[sizeof( FlashManagerResultHeader_T )] = { 0 };
    uint32_t                           header_fill                                        = 0U;
    uint32_t                           payload_remaining                                  = 0U;
    uint32_t                           received_bytes                                     = 0U;
    uint32_t                           record_count                                       = 0U;
    uint32_t                           mismatch_offset                                    = 0U;
    uint8_t                            mismatch_actual                                    = 0U;
    uint8_t                            expected = ( uint8_t )value;
    const uint32_t                     expected_bytes =
        ( console_flash_last_upload_records != 0U
          && length <= ( UINT32_MAX / console_flash_last_upload_records ) )
                                ? length * console_flash_last_upload_records
                                : length;
    bool       valid             = true;
    bool       mismatch_recorded = false;
    TickType_t last_progress_at  = xTaskGetTickCount();

    for ( ;; )
    {
        uint32_t bytes_read = 0U;
        status              = FLASH_MANAGER_ReadResultBytes( console_flash_read_buffer,
                                                             CONSOLE_FLASH_RESULT_READ_BYTES, &bytes_read );
        if ( status == FLASH_MANAGER_RESULT_TRANSFER_OK )
        {
            last_progress_at = xTaskGetTickCount();
            uint32_t offset  = 0U;
            while ( offset < bytes_read )
            {
                if ( header_fill < sizeof( header_bytes ) )
                {
                    const uint32_t copy =
                        ( bytes_read - offset < sizeof( header_bytes ) - header_fill )
                            ? bytes_read - offset
                            : sizeof( header_bytes ) - header_fill;
                    ( void )memcpy( &header_bytes[header_fill], &console_flash_read_buffer[offset],
                                    copy );
                    header_fill += copy;
                    offset += copy;
                    if ( header_fill == sizeof( header_bytes ) )
                    {
                        FlashManagerResultHeader_T header = { 0 };
                        ( void )memcpy( &header, header_bytes, sizeof( header ) );
                        if ( header.peripheral_type != expected_peripheral_type
                             || header.channel != ( uint8_t )( channel - 1U )
                             || header.payload_length_bytes == 0U )
                        {
                            valid = false;
                        }
                        payload_remaining = header.payload_length_bytes;
                        record_count++;
                    }
                    continue;
                }

                const uint32_t copy = ( bytes_read - offset < payload_remaining )
                                          ? bytes_read - offset
                                          : payload_remaining;
                for ( uint32_t index = 0U; index < copy; index++ )
                {
                    const uint8_t actual = console_flash_read_buffer[offset + index];
                    if ( actual != expected && !mismatch_recorded )
                    {
                        mismatch_recorded = true;
                        mismatch_offset   = received_bytes + index;
                        mismatch_actual   = actual;
                    }
                }
                received_bytes += copy;
                payload_remaining -= copy;
                offset += copy;
                if ( payload_remaining == 0U )
                {
                    header_fill = 0U;
                }
            }
            continue;
        }

        if ( status == FLASH_MANAGER_RESULT_TRANSFER_BUSY )
        {
            if ( CONSOLE_Flash_HasTimedOut( last_progress_at, CONSOLE_FLASH_PROGRESS_TIMEOUT_MS ) )
            {
                CONSOLE_Printf( "%s result retrieval timeout after %lu bytes.\r\n", peripheral_name,
                                ( unsigned long )received_bytes );
                return;
            }
            vTaskDelay( pdMS_TO_TICKS( CONSOLE_FLASH_POLL_PERIOD_MS ) );
            continue;
        }
        if ( status == FLASH_MANAGER_RESULT_TRANSFER_END_OF_STREAM )
        {
            break;
        }
        CONSOLE_Printf( "%s result retrieval failed after %lu bytes (status=%d).\r\n",
                        peripheral_name, ( unsigned long )received_bytes, ( int )status );
        return;
    }

    const bool passed =
        valid && !mismatch_recorded && payload_remaining == 0U && received_bytes == expected_bytes;
    if ( !RUN_STATE_MANAGER_RequestResultTransferComplete()
         || !CONSOLE_Flash_WaitForRunState( RUN_STATE_ARMED, CONSOLE_FLASH_STATE_TIMEOUT_MS ) )
    {
        CONSOLE_Printf( "RSM result transfer completion failed.\r\n" );
        return;
    }

    if ( passed )
    {
        CONSOLE_Printf( "%s loopback PASS: channel=%lu records=%lu bytes=%lu byte=0x%02lX.\r\n",
                        peripheral_name, ( unsigned long )channel, ( unsigned long )record_count,
                        ( unsigned long )received_bytes, ( unsigned long )value );
    }
    else
    {
        CONSOLE_Printf( "%s loopback FAIL: records=%lu bytes=%lu expected_bytes=%lu",
                        peripheral_name, ( unsigned long )record_count,
                        ( unsigned long )received_bytes, ( unsigned long )expected_bytes );
        if ( mismatch_recorded )
        {
            CONSOLE_Printf( " first_mismatch=%lu actual=0x%02X", ( unsigned long )mismatch_offset,
                            ( unsigned int )mismatch_actual );
        }
        CONSOLE_Printf( ".\r\n" );
    }
}

static void CONSOLE_Flash_VerifyUartLoopbackResultsCommand( uint16_t argc, char* argv[] )
{
    CONSOLE_Flash_VerifyByteStreamLoopbackResults(
        argc, argv, FLASH_MANAGER_RESULT_PERIPHERAL_UART_RECEIVE, EXEC_UART_CHANNEL_COUNT, "UART" );
}

static void CONSOLE_Flash_VerifySpiLoopbackResultsCommand( uint16_t argc, char* argv[] )
{
    CONSOLE_Flash_VerifyByteStreamLoopbackResults(
        argc, argv, FLASH_MANAGER_RESULT_PERIPHERAL_SPI_RECEIVE, EXEC_SPI_CHANNEL_COUNT, "SPI" );
}

/** Retrieves CAN result records and verifies the expected loopback frame. */
static void CONSOLE_Flash_VerifyCanLoopbackResultsCommand( uint16_t argc, char* argv[] )
{
    uint32_t channel    = 0U;
    uint32_t identifier = 0U;
    uint32_t value      = 0U;
    uint32_t dlc        = 0U;
    if ( argc != 7U || !CONSOLE_Flash_ParseU32( argv[3], &channel )
         || !CONSOLE_Flash_ParseU32( argv[4], &identifier )
         || !CONSOLE_Flash_ParseU32( argv[5], &value ) || !CONSOLE_Flash_ParseU32( argv[6], &dlc )
         || channel == 0U || channel > EXEC_CAN_CHANNEL_COUNT
         || identifier > EXEC_CAN_STANDARD_ID_MAX || value > UINT8_MAX
         || dlc > EXEC_CAN_MAX_PAYLOAD_SIZE )
    {
        CONSOLE_Printf( "Usage: flash results verify_can_loopback <channel 1..2> <id> "
                        "<byte 0..255> <dlc 0..8>\r\n" );
        return;
    }

    if ( !RUN_STATE_MANAGER_RequestResultTransfer()
         || !CONSOLE_Flash_WaitForRunState( RUN_STATE_RESULT_TRANSFER,
                                            CONSOLE_FLASH_STATE_TIMEOUT_MS ) )
    {
        CONSOLE_Printf( "RSM result transfer start failed.\r\n" );
        return;
    }

    FlashManagerResultTransferStatus_T status = FLASH_MANAGER_RESULT_TRANSFER_OK;
    uint8_t                            header_bytes[sizeof( FlashManagerResultHeader_T )] = { 0 };
    uint32_t                           header_fill                                        = 0U;
    uint32_t                           payload_remaining                                  = 0U;
    uint32_t                           records                                            = 0U;
    uint32_t                           frames                                             = 0U;
    uint8_t                            packet_bytes[sizeof( EXEC_CAN_Packet_T )]          = { 0 };
    uint32_t                           packet_fill                                        = 0U;
    bool                               valid                                              = true;
    TickType_t                         last_progress_at = xTaskGetTickCount();

    for ( ;; )
    {
        uint32_t bytes_read = 0U;
        status              = FLASH_MANAGER_ReadResultBytes( console_flash_read_buffer,
                                                             CONSOLE_FLASH_RESULT_READ_BYTES, &bytes_read );
        if ( status == FLASH_MANAGER_RESULT_TRANSFER_BUSY )
        {
            if ( CONSOLE_Flash_HasTimedOut( last_progress_at, CONSOLE_FLASH_PROGRESS_TIMEOUT_MS ) )
            {
                CONSOLE_Printf( "CAN result retrieval timeout.\r\n" );
                return;
            }
            vTaskDelay( pdMS_TO_TICKS( CONSOLE_FLASH_POLL_PERIOD_MS ) );
            continue;
        }
        if ( status == FLASH_MANAGER_RESULT_TRANSFER_END_OF_STREAM )
        {
            break;
        }
        if ( status != FLASH_MANAGER_RESULT_TRANSFER_OK )
        {
            CONSOLE_Printf( "CAN result retrieval failed (status=%d).\r\n", ( int )status );
            return;
        }
        last_progress_at = xTaskGetTickCount();

        uint32_t offset = 0U;
        while ( offset < bytes_read )
        {
            if ( header_fill < sizeof( header_bytes ) )
            {
                const uint32_t copy = ( bytes_read - offset < sizeof( header_bytes ) - header_fill )
                                          ? bytes_read - offset
                                          : sizeof( header_bytes ) - header_fill;
                ( void )memcpy( &header_bytes[header_fill], &console_flash_read_buffer[offset],
                                copy );
                header_fill += copy;
                offset += copy;
                if ( header_fill == sizeof( header_bytes ) )
                {
                    FlashManagerResultHeader_T header = { 0 };
                    ( void )memcpy( &header, header_bytes, sizeof( header ) );
                    valid = valid
                            && header.peripheral_type == FLASH_MANAGER_RESULT_PERIPHERAL_CAN_RECEIVE
                            && header.channel == ( uint8_t )( channel - 1U )
                            && header.payload_length_bytes > 0U
                            && ( header.payload_length_bytes % sizeof( EXEC_CAN_Packet_T ) ) == 0U;
                    payload_remaining = header.payload_length_bytes;
                    records++;
                }
                continue;
            }

            const uint32_t copy = ( bytes_read - offset < payload_remaining ) ? bytes_read - offset
                                                                              : payload_remaining;
            for ( uint32_t index = 0U; index < copy; index++ )
            {
                packet_bytes[packet_fill++] = console_flash_read_buffer[offset + index];
                if ( packet_fill == sizeof( EXEC_CAN_Packet_T ) )
                {
                    EXEC_CAN_Packet_T packet = { 0 };
                    ( void )memcpy( &packet, packet_bytes, sizeof( packet ) );
                    valid = valid && packet.id == identifier && packet.dlc == dlc;
                    for ( uint32_t byte = 0U; byte < dlc; byte++ )
                    {
                        valid = valid && packet.data[byte] == value;
                    }
                    packet_fill = 0U;
                    frames++;
                }
            }
            payload_remaining -= copy;
            offset += copy;
            if ( payload_remaining == 0U )
            {
                header_fill = 0U;
                packet_fill = 0U;
            }
        }
    }

    const bool passed =
        valid && records > 0U && frames > 0U && payload_remaining == 0U && packet_fill == 0U;
    const bool transfer_complete =
        RUN_STATE_MANAGER_RequestResultTransferComplete()
        && CONSOLE_Flash_WaitForRunState( RUN_STATE_ARMED, CONSOLE_FLASH_STATE_TIMEOUT_MS );
    if ( !transfer_complete )
    {
        CONSOLE_Printf( "RSM result transfer completion failed.\r\n" );
        return;
    }
    CONSOLE_Printf( "CAN loopback %s: channel=%lu records=%lu frames=%lu id=0x%03lX "
                    "byte=0x%02lX dlc=%lu.\r\n",
                    passed ? "PASS" : "FAIL", ( unsigned long )channel, ( unsigned long )records,
                    ( unsigned long )frames, ( unsigned long )identifier, ( unsigned long )value,
                    ( unsigned long )dlc );
}

/** Retrieves and validates multi-peripheral loopback results from the stress test run. */
static void CONSOLE_Flash_VerifyStressResultsCommand( uint16_t argc, char* argv[] )
{
    ( void )argv;
    if ( argc != 3U )
    {
        CONSOLE_Printf( "Usage: flash results verify_stress\r\n" );
        return;
    }

    if ( !RUN_STATE_MANAGER_RequestResultTransfer()
         || !CONSOLE_Flash_WaitForRunState( RUN_STATE_RESULT_TRANSFER,
                                            CONSOLE_FLASH_STATE_TIMEOUT_MS ) )
    {
        CONSOLE_Printf( "RSM result transfer start failed.\r\n" );
        return;
    }

    FlashManagerResultTransferStatus_T status = FLASH_MANAGER_RESULT_TRANSFER_OK;
    uint8_t                            header_bytes[sizeof( FlashManagerResultHeader_T )] = { 0 };
    uint32_t                           header_fill                                        = 0U;
    FlashManagerResultHeader_T         header                                             = { 0 };
    uint32_t                           payload_remaining                                  = 0U;
    uint32_t                           payload_offset                                     = 0U;
    uint8_t                            payload_scratch[8]                                 = { 0 };
    uint32_t                           payload_scratch_fill                               = 0U;

    uint32_t total_records      = 0U;
    uint32_t total_result_bytes = 0U;

    /* DI tracking */
    uint32_t di_record_count                                         = 0U;
    uint32_t last_di_sample                                          = 0U;
    uint32_t di_mismatch_count                                       = 0U;
    uint32_t di_channel_mismatches[EXEC_DIGITAL_INPUT_CHANNEL_COUNT] = { 0U };
    uint32_t di_first_mismatch_tick                                  = 0U;
    uint8_t  di_first_mismatch_channel                               = 0U;
    bool     di_first_expected_high                                  = false;
    bool     di_first_actual_high                                    = false;

    /* AI tracking */
    uint32_t ai_record_count = 0U;
    uint32_t last_ai_ch0     = 0U;
    uint32_t last_ai_ch1     = 0U;

    /* PWM capture tracking */
    uint32_t pwm_lv_records     = 0U;
    uint32_t pwm_hv_records     = 0U;
    uint32_t last_pwm_lv_period = 0U;
    uint32_t last_pwm_lv_high   = 0U;
    uint32_t last_pwm_hv_period = 0U;
    uint32_t last_pwm_hv_high   = 0U;

    /* UART tracking */
    uint32_t uart1_bytes       = 0U;
    uint32_t uart2_bytes       = 0U;
    bool     uart_mismatch     = false;
    uint8_t  uart_bad_channel  = 0U;
    uint8_t  uart_bad_expected = 0U;
    uint8_t  uart_bad_actual   = 0U;

    /* SPI tracking */
    uint32_t spi1_bytes                                   = 0U;
    uint32_t spi2_bytes                                   = 0U;
    bool     spi_channel_mismatch[EXEC_SPI_CHANNEL_COUNT] = { false };
    bool     spi_mismatch                                 = false;
    uint8_t  spi_bad_channel                              = 0U;
    uint8_t  spi_bad_expected                             = 0U;
    uint8_t  spi_bad_actual                               = 0U;

    /* Unexpected peripheral records */
    uint32_t unexpected_records = 0U;
    bool     structure_valid    = true;

    TickType_t last_progress_at = xTaskGetTickCount();

    for ( ;; )
    {
        uint32_t bytes_read = 0U;
        status              = FLASH_MANAGER_ReadResultBytes( console_flash_read_buffer,
                                                             CONSOLE_FLASH_RESULT_READ_BYTES, &bytes_read );
        if ( status == FLASH_MANAGER_RESULT_TRANSFER_OK )
        {
            last_progress_at = xTaskGetTickCount();
            uint32_t offset  = 0U;
            while ( offset < bytes_read )
            {
                if ( header_fill < sizeof( header_bytes ) )
                {
                    const uint32_t copy =
                        ( bytes_read - offset < sizeof( header_bytes ) - header_fill )
                            ? bytes_read - offset
                            : sizeof( header_bytes ) - header_fill;
                    ( void )memcpy( &header_bytes[header_fill], &console_flash_read_buffer[offset],
                                    copy );
                    header_fill += copy;
                    offset += copy;
                    total_result_bytes += copy;
                    if ( header_fill == sizeof( header_bytes ) )
                    {
                        ( void )memcpy( &header, header_bytes, sizeof( header ) );
                        payload_remaining    = header.payload_length_bytes;
                        payload_offset       = 0U;
                        payload_scratch_fill = 0U;
                        total_records++;
                    }
                    continue;
                }

                const uint32_t copy = ( bytes_read - offset < payload_remaining )
                                          ? bytes_read - offset
                                          : payload_remaining;

                switch ( header.peripheral_type )
                {
                    case FLASH_MANAGER_RESULT_PERIPHERAL_DIGITAL_INPUT: {
                        if ( header.payload_length_bytes != sizeof( uint32_t ) )
                        {
                            structure_valid = false;
                        }
                        const uint32_t sc =
                            ( copy < sizeof( payload_scratch ) - payload_scratch_fill )
                                ? copy
                                : sizeof( payload_scratch ) - payload_scratch_fill;
                        ( void )memcpy( &payload_scratch[payload_scratch_fill],
                                        &console_flash_read_buffer[offset], sc );
                        payload_scratch_fill += sc;
                        break;
                    }

                    case FLASH_MANAGER_RESULT_PERIPHERAL_ANALOGUE_INPUT: {
                        if ( header.payload_length_bytes != ( 2U * sizeof( uint32_t ) ) )
                        {
                            structure_valid = false;
                        }
                        const uint32_t sc =
                            ( copy < sizeof( payload_scratch ) - payload_scratch_fill )
                                ? copy
                                : sizeof( payload_scratch ) - payload_scratch_fill;
                        ( void )memcpy( &payload_scratch[payload_scratch_fill],
                                        &console_flash_read_buffer[offset], sc );
                        payload_scratch_fill += sc;
                        break;
                    }

                    case FLASH_MANAGER_RESULT_PERIPHERAL_PWM_CAPTURE: {
                        if ( header.payload_length_bytes != ( 2U * sizeof( uint32_t ) ) )
                        {
                            structure_valid = false;
                        }
                        const uint32_t sc =
                            ( copy < sizeof( payload_scratch ) - payload_scratch_fill )
                                ? copy
                                : sizeof( payload_scratch ) - payload_scratch_fill;
                        ( void )memcpy( &payload_scratch[payload_scratch_fill],
                                        &console_flash_read_buffer[offset], sc );
                        payload_scratch_fill += sc;
                        break;
                    }

                    case FLASH_MANAGER_RESULT_PERIPHERAL_UART_RECEIVE: {
                        const uint8_t expected =
                            ( header.channel == ( uint8_t )EXEC_UART_CHANNEL_1 ) ? 0x55U : 0xAAU;
                        if ( header.channel == ( uint8_t )EXEC_UART_CHANNEL_1 )
                        {
                            uart1_bytes += copy;
                        }
                        else if ( header.channel == ( uint8_t )EXEC_UART_CHANNEL_2 )
                        {
                            uart2_bytes += copy;
                        }
                        else
                        {
                            structure_valid = false;
                        }

                        for ( uint32_t i = 0U; i < copy; i++ )
                        {
                            const uint8_t byte = console_flash_read_buffer[offset + i];
                            if ( ( byte != expected ) && !uart_mismatch )
                            {
                                uart_mismatch     = true;
                                uart_bad_channel  = header.channel + 1U;
                                uart_bad_expected = expected;
                                uart_bad_actual   = byte;
                            }
                        }
                        break;
                    }

                    case FLASH_MANAGER_RESULT_PERIPHERAL_SPI_RECEIVE: {
                        uint8_t expected = 0U;
                        if ( header.channel == ( uint8_t )EXEC_SPI_CHANNEL_1 )
                        {
                            spi1_bytes += copy;
                            expected = 0x5AU;
                        }
                        else if ( header.channel == ( uint8_t )EXEC_SPI_CHANNEL_2 )
                        {
                            spi2_bytes += copy;
                            expected = 0xA5U;
                        }
                        else
                        {
                            structure_valid = false;
                        }

                        for ( uint32_t i = 0U; i < copy; i++ )
                        {
                            const uint8_t byte = console_flash_read_buffer[offset + i];
                            if ( ( byte != expected ) && !spi_mismatch )
                            {
                                spi_mismatch     = true;
                                spi_bad_channel  = header.channel + 1U;
                                spi_bad_expected = expected;
                                spi_bad_actual   = byte;
                            }
                            if ( byte != expected
                                 && header.channel < ( uint8_t )EXEC_SPI_CHANNEL_COUNT )
                            {
                                spi_channel_mismatch[header.channel] = true;
                            }
                        }
                        break;
                    }

                    default:
                        unexpected_records++;
                        structure_valid = false;
                        break;
                }

                payload_offset += copy;
                payload_remaining -= copy;
                total_result_bytes += copy;
                offset += copy;

                if ( payload_remaining == 0U )
                {
                    if ( header.peripheral_type == FLASH_MANAGER_RESULT_PERIPHERAL_DIGITAL_INPUT )
                    {
                        di_record_count++;
                        if ( payload_scratch_fill >= sizeof( uint32_t ) )
                        {
                            ( void )memcpy( &last_di_sample, payload_scratch, sizeof( uint32_t ) );
                            uint32_t expected_logical_pattern = 0U;
                            if ( header.timestamp > 1U && console_flash_stress_sample_count > 0U
                                 && console_flash_stress_interval_ticks > 0U )
                            {
                                uint32_t sample_index =
                                    ( header.timestamp - 2U ) / console_flash_stress_interval_ticks;
                                if ( sample_index >= console_flash_stress_sample_count )
                                {
                                    sample_index = console_flash_stress_sample_count - 1U;
                                }
                                expected_logical_pattern =
                                    CONSOLE_Flash_StressLogicalPattern( sample_index );
                            }

                            const uint32_t expected_sample =
                                CONSOLE_Flash_StressDigitalInputMask( expected_logical_pattern );
                            const uint32_t mismatch_bits = last_di_sample ^ expected_sample;
                            for ( uint32_t channel = 0U; channel < EXEC_DIGITAL_INPUT_CHANNEL_COUNT;
                                  channel++ )
                            {
                                const uint32_t pin_mask =
                                    1UL << console_flash_stress_di_pin_positions[channel];
                                if ( ( mismatch_bits & pin_mask ) != 0U )
                                {
                                    di_mismatch_count++;
                                    di_channel_mismatches[channel]++;
                                    if ( di_first_mismatch_tick == 0U )
                                    {
                                        di_first_mismatch_tick    = header.timestamp;
                                        di_first_mismatch_channel = ( uint8_t )( channel + 1U );
                                        di_first_expected_high =
                                            ( expected_sample & pin_mask ) != 0U;
                                        di_first_actual_high = ( last_di_sample & pin_mask ) != 0U;
                                    }
                                }
                            }
                        }
                    }
                    else if ( header.peripheral_type
                              == FLASH_MANAGER_RESULT_PERIPHERAL_ANALOGUE_INPUT )
                    {
                        ai_record_count++;
                        if ( payload_scratch_fill >= ( 2U * sizeof( uint32_t ) ) )
                        {
                            ( void )memcpy( &last_ai_ch0, payload_scratch, sizeof( uint32_t ) );
                            ( void )memcpy( &last_ai_ch1, &payload_scratch[sizeof( uint32_t )],
                                            sizeof( uint32_t ) );
                        }
                    }
                    else if ( header.peripheral_type
                              == FLASH_MANAGER_RESULT_PERIPHERAL_PWM_CAPTURE )
                    {
                        if ( header.channel == ( uint8_t )EXEC_PWM_CAPTURE_CHANNEL_1 )
                        {
                            pwm_lv_records++;
                            if ( payload_scratch_fill >= ( 2U * sizeof( uint32_t ) ) )
                            {
                                ( void )memcpy( &last_pwm_lv_period, payload_scratch,
                                                sizeof( uint32_t ) );
                                ( void )memcpy( &last_pwm_lv_high,
                                                &payload_scratch[sizeof( uint32_t )],
                                                sizeof( uint32_t ) );
                            }
                        }
                        else if ( header.channel == ( uint8_t )EXEC_PWM_CAPTURE_CHANNEL_2 )
                        {
                            pwm_hv_records++;
                            if ( payload_scratch_fill >= ( 2U * sizeof( uint32_t ) ) )
                            {
                                ( void )memcpy( &last_pwm_hv_period, payload_scratch,
                                                sizeof( uint32_t ) );
                                ( void )memcpy( &last_pwm_hv_high,
                                                &payload_scratch[sizeof( uint32_t )],
                                                sizeof( uint32_t ) );
                            }
                        }
                    }

                    header_fill          = 0U;
                    payload_scratch_fill = 0U;
                }
            }
            continue;
        }

        if ( status == FLASH_MANAGER_RESULT_TRANSFER_BUSY )
        {
            if ( CONSOLE_Flash_HasTimedOut( last_progress_at, CONSOLE_FLASH_PROGRESS_TIMEOUT_MS ) )
            {
                CONSOLE_Printf( "Stress result retrieval timeout after %lu bytes.\r\n",
                                ( unsigned long )total_result_bytes );
                return;
            }
            vTaskDelay( pdMS_TO_TICKS( CONSOLE_FLASH_POLL_PERIOD_MS ) );
            continue;
        }

        if ( status == FLASH_MANAGER_RESULT_TRANSFER_END_OF_STREAM )
        {
            break;
        }

        CONSOLE_Printf( "Stress result retrieval failed (status=%d).\r\n", ( int )status );
        return;
    }

    const bool transfer_complete =
        RUN_STATE_MANAGER_RequestResultTransferComplete()
        && CONSOLE_Flash_WaitForRunState( RUN_STATE_ARMED, CONSOLE_FLASH_STATE_TIMEOUT_MS );
    if ( !transfer_complete )
    {
        CONSOLE_Printf( "RSM result transfer completion failed.\r\n" );
        return;
    }

    const bool framing_ok = ( header_fill == 0U ) && ( payload_remaining == 0U );
    const bool passed     = framing_ok && structure_valid && ( di_mismatch_count == 0U )
                        && !uart_mismatch && !spi_mismatch && ( di_record_count > 0U )
                        && ( ai_record_count > 0U ) && ( uart1_bytes > 0U ) && ( uart2_bytes > 0U )
                        && ( spi1_bytes > 0U ) && ( spi2_bytes > 0U ) && ( pwm_lv_records > 0U )
                        && ( pwm_hv_records > 0U );

    CONSOLE_Printf( "================ STRESS TEST VERIFICATION ================\r\n" );
    CONSOLE_Printf( "Overall Result: %s\r\n", passed ? "PASS" : "FAIL" );
    CONSOLE_Printf( "Total records:  %lu (%lu bytes transferred)\r\n",
                    ( unsigned long )total_records, ( unsigned long )total_result_bytes );
    CONSOLE_Printf( "DO1..10 -> DI1..10: %s (records=%lu, mismatches=%lu, "
                    "last_sample=0x%08lX)\r\n",
                    ( di_record_count > 0U && di_mismatch_count == 0U ) ? "PASS" : "FAIL",
                    ( unsigned long )di_record_count, ( unsigned long )di_mismatch_count,
                    ( unsigned long )last_di_sample );
    for ( uint32_t channel = 0U; channel < EXEC_DIGITAL_INPUT_CHANNEL_COUNT; channel++ )
    {
        CONSOLE_Printf(
            "  DO%lu -> DI%lu: %s (mismatches=%lu/%lu)\r\n", ( unsigned long )( channel + 1U ),
            ( unsigned long )( channel + 1U ),
            ( di_record_count > 0U && di_channel_mismatches[channel] == 0U ) ? "PASS" : "FAIL",
            ( unsigned long )di_channel_mismatches[channel], ( unsigned long )di_record_count );
    }
    if ( di_first_mismatch_tick != 0U )
    {
        CONSOLE_Printf( "DI first mismatch: tick=%lu channel=%u expected=%s actual=%s\r\n",
                        ( unsigned long )di_first_mismatch_tick, di_first_mismatch_channel,
                        di_first_expected_high ? "HIGH" : "LOW",
                        di_first_actual_high ? "HIGH" : "LOW" );
    }
    CONSOLE_Printf(
        "AI (dual DMA):    COLLECTED, NOT VERIFIED (records=%lu, last ch0=%lu, ch1=%lu counts)\r\n",
        ( unsigned long )ai_record_count, ( unsigned long )last_ai_ch0,
        ( unsigned long )last_ai_ch1 );
    CONSOLE_Printf( "PWM LV (1MHz/50%%): %s (records=%lu, period=%lu, high=%lu ticks)\r\n",
                    ( pwm_lv_records > 0U ) ? "PASS" : "FAIL", ( unsigned long )pwm_lv_records,
                    ( unsigned long )last_pwm_lv_period, ( unsigned long )last_pwm_lv_high );
    CONSOLE_Printf( "PWM HV (1MHz/50%%): %s (records=%lu, period=%lu, high=%lu ticks)\r\n",
                    ( pwm_hv_records > 0U ) ? "PASS" : "FAIL", ( unsigned long )pwm_hv_records,
                    ( unsigned long )last_pwm_hv_period, ( unsigned long )last_pwm_hv_high );
    CONSOLE_Printf( "UART1 (2Mbit/s):  %s (%lu bytes received, pattern=0x55)\r\n",
                    ( uart1_bytes > 0U && !uart_mismatch ) ? "PASS" : "FAIL",
                    ( unsigned long )uart1_bytes );
    CONSOLE_Printf( "UART2 (2Mbit/s):  %s (%lu bytes received, pattern=0xAA)\r\n",
                    ( uart2_bytes > 0U && !uart_mismatch ) ? "PASS" : "FAIL",
                    ( unsigned long )uart2_bytes );
    CONSOLE_Printf( "SPI1 (22.5Mbit/s): %s (%lu bytes received, pattern=0x5A)\r\n",
                    ( spi1_bytes > 0U && !spi_channel_mismatch[EXEC_SPI_CHANNEL_1] ) ? "PASS"
                                                                                     : "FAIL",
                    ( unsigned long )spi1_bytes );
    CONSOLE_Printf( "SPI2 (45Mbit/s):  %s (%lu bytes received, pattern=0xA5)\r\n",
                    ( spi2_bytes > 0U && !spi_channel_mismatch[EXEC_SPI_CHANNEL_2] ) ? "PASS"
                                                                                     : "FAIL",
                    ( unsigned long )spi2_bytes );
    if ( uart_mismatch )
    {
        CONSOLE_Printf( "UART data mismatch on CH%u: expected 0x%02X, got 0x%02X\r\n",
                        uart_bad_channel, uart_bad_expected, uart_bad_actual );
    }
    if ( spi_mismatch )
    {
        CONSOLE_Printf( "SPI data mismatch on SPI%u: expected 0x%02X, got 0x%02X\r\n",
                        spi_bad_channel, spi_bad_expected, spi_bad_actual );
    }
    if ( unexpected_records > 0U )
    {
        CONSOLE_Printf( "Unexpected peripheral records: %lu\r\n",
                        ( unsigned long )unexpected_records );
    }
    if ( !framing_ok )
    {
        CONSOLE_Printf( "Framing error: stream ended with partial record.\r\n" );
    }
    CONSOLE_Printf( "==========================================================\r\n" );
}

/** Retrieves sparse PWM capture records and checks the final capture against the uploaded target.
 */
static void CONSOLE_Flash_VerifyPwmLoopbackResultsCommand( uint16_t argc, char* argv[] )
{
    uint32_t input_channel = 0U;
    if ( !console_flash_pwm_loopback.valid || argc != 4U
         || !CONSOLE_Flash_ParseU32( argv[3], &input_channel ) || input_channel < 1U
         || input_channel > EXEC_PWM_CAPTURE_CHANNEL_COUNT )
    {
        CONSOLE_Printf( "Usage: flash results verify_pwm_capture <input_channel 1..2>\r\n" );
        return;
    }

    if ( !RUN_STATE_MANAGER_RequestResultTransfer()
         || !CONSOLE_Flash_WaitForRunState( RUN_STATE_RESULT_TRANSFER,
                                            CONSOLE_FLASH_STATE_TIMEOUT_MS ) )
    {
        CONSOLE_Printf( "RSM result transfer start failed.\r\n" );
        return;
    }

    const ExecPwmCaptureChannel_T capture_channel =
        ( ExecPwmCaptureChannel_T )( input_channel - 1U );
    FlashManagerResultTransferStatus_T status = FLASH_MANAGER_RESULT_TRANSFER_OK;
    uint8_t                            record[CONSOLE_FLASH_PWM_CAPTURE_RESULT_BYTES] = { 0 };
    uint32_t                           record_fill                                    = 0U;
    uint32_t                           record_count                                   = 0U;
    uint32_t                           previous_timestamp                             = 0U;
    uint32_t                           first_timestamp                                = 0U;
    uint32_t                           final_timestamp                                = 0U;
    ExecPwmCaptureResult_T             first_raw_capture                              = { 0 };
    ExecPwmCaptureResult_T             final_raw_capture                              = { 0 };
    ExecPwmCapturePhysical_T           first_measurement                              = { 0 };
    ExecPwmCapturePhysical_T           final_measurement                              = { 0 };
    bool                               records_valid                                  = true;
    bool                               conversion_valid                               = true;
    TickType_t                         last_progress_at = xTaskGetTickCount();

    for ( ;; )
    {
        uint32_t bytes_read = 0U;
        status              = FLASH_MANAGER_ReadResultBytes( console_flash_read_buffer,
                                                             CONSOLE_FLASH_RESULT_READ_BYTES, &bytes_read );
        if ( status == FLASH_MANAGER_RESULT_TRANSFER_OK )
        {
            uint32_t source_offset = 0U;
            while ( source_offset < bytes_read )
            {
                const uint32_t record_remaining =
                    CONSOLE_FLASH_PWM_CAPTURE_RESULT_BYTES - record_fill;
                const uint32_t source_remaining = bytes_read - source_offset;
                const uint32_t copy_length =
                    source_remaining < record_remaining ? source_remaining : record_remaining;
                ( void )memcpy( &record[record_fill], &console_flash_read_buffer[source_offset],
                                copy_length );
                record_fill += copy_length;
                source_offset += copy_length;

                if ( record_fill == CONSOLE_FLASH_PWM_CAPTURE_RESULT_BYTES )
                {
                    FlashManagerResultHeader_T header      = { 0 };
                    ExecPwmCaptureResult_T     raw_capture = {
                            .has_new_data = true,
                            .is_valid     = true,
                    };
                    ( void )memcpy( &header, record, sizeof( header ) );
                    ( void )memcpy( &raw_capture.period_ticks, &record[sizeof( header )],
                                    2U * sizeof( uint32_t ) );
                    record_count++;

                    if ( header.peripheral_type != FLASH_MANAGER_RESULT_PERIPHERAL_PWM_CAPTURE
                         || header.channel != capture_channel
                         || header.payload_length_bytes != 2U * sizeof( uint32_t )
                         || header.timestamp <= previous_timestamp
                         || header.timestamp > console_flash_pwm_loopback.run_ticks )
                    {
                        records_valid = false;
                    }

                    ExecPwmCapturePhysical_T physical = { 0 };
                    if ( !EXEC_PWM_Capture_Convert( capture_channel, &raw_capture, &physical ) )
                    {
                        conversion_valid = false;
                    }
                    else
                    {
                        if ( record_count == 1U )
                        {
                            first_timestamp   = header.timestamp;
                            first_raw_capture = raw_capture;
                            first_measurement = physical;
                        }
                        final_timestamp   = header.timestamp;
                        final_raw_capture = raw_capture;
                        final_measurement = physical;
                    }

                    previous_timestamp = header.timestamp;
                    record_fill        = 0U;
                }
            }
            last_progress_at = xTaskGetTickCount();
            continue;
        }

        if ( status == FLASH_MANAGER_RESULT_TRANSFER_BUSY )
        {
            if ( CONSOLE_Flash_HasTimedOut( last_progress_at, CONSOLE_FLASH_PROGRESS_TIMEOUT_MS ) )
            {
                CONSOLE_Printf( "PWM capture result retrieval timed out.\r\n" );
                return;
            }
            vTaskDelay( pdMS_TO_TICKS( CONSOLE_FLASH_POLL_PERIOD_MS ) );
            continue;
        }
        if ( status == FLASH_MANAGER_RESULT_TRANSFER_END_OF_STREAM )
        {
            break;
        }

        CONSOLE_Printf( "PWM capture result retrieval failed (status=%d).\r\n", ( int )status );
        return;
    }

    const bool stream_passed =
        records_valid && conversion_valid && record_fill == 0U && record_count > 0U;
    const uint32_t expected_duty_bp = console_flash_pwm_loopback.duty_permille * 10U;
    const uint32_t duty_error_bp    = final_measurement.duty_cycle_bp > expected_duty_bp
                                          ? final_measurement.duty_cycle_bp - expected_duty_bp
                                          : expected_duty_bp - final_measurement.duty_cycle_bp;
    const bool     target_passed =
        stream_passed && final_timestamp > console_flash_pwm_loopback.update_tick
        && final_measurement.frequency_hz == console_flash_pwm_loopback.frequency_hz
        && duty_error_bp <= 100U;

    if ( !RUN_STATE_MANAGER_RequestResultTransferComplete()
         || !CONSOLE_Flash_WaitForRunState( RUN_STATE_ARMED, CONSOLE_FLASH_STATE_TIMEOUT_MS ) )
    {
        CONSOLE_Printf( "RSM result transfer completion failed.\r\n" );
        return;
    }

    CONSOLE_Printf( "PWM capture result stream validation %s: records=%lu.\r\n",
                    stream_passed ? "PASS" : "FAIL", ( unsigned long )record_count );
    if ( conversion_valid && record_count > 0U )
    {
        CONSOLE_Printf( "First capture: tick=%lu period=%lu high=%lu ticks, "
                        "frequency=%lu Hz duty=%lu.%02lu%%.\r\n",
                        ( unsigned long )first_timestamp,
                        ( unsigned long )first_raw_capture.period_ticks,
                        ( unsigned long )first_raw_capture.high_ticks,
                        ( unsigned long )first_measurement.frequency_hz,
                        ( unsigned long )( first_measurement.duty_cycle_bp / 100U ),
                        ( unsigned long )( first_measurement.duty_cycle_bp % 100U ) );
        CONSOLE_Printf( "Final capture: tick=%lu period=%lu high=%lu ticks, "
                        "frequency=%lu Hz duty=%lu.%02lu%%.\r\n",
                        ( unsigned long )final_timestamp,
                        ( unsigned long )final_raw_capture.period_ticks,
                        ( unsigned long )final_raw_capture.high_ticks,
                        ( unsigned long )final_measurement.frequency_hz,
                        ( unsigned long )( final_measurement.duty_cycle_bp / 100U ),
                        ( unsigned long )( final_measurement.duty_cycle_bp % 100U ) );
    }
    CONSOLE_Printf( "PWM%lu-to-capture%lu target comparison %s: expected=%lu Hz, %lu.%01lu%%.\r\n",
                    ( unsigned long )console_flash_pwm_loopback.output_channel,
                    ( unsigned long )input_channel, target_passed ? "PASS" : "FAIL",
                    ( unsigned long )console_flash_pwm_loopback.frequency_hz,
                    ( unsigned long )( console_flash_pwm_loopback.duty_permille / 10U ),
                    ( unsigned long )( console_flash_pwm_loopback.duty_permille % 10U ) );
}

/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */

void CONSOLE_FlashManager_Command( uint16_t argc, char* argv[] )
{
    if ( ( argc < 2U ) || ( strcmp( argv[1], "help" ) == 0 ) )
    {
        CONSOLE_Flash_PrintUsage();
        return;
    }

    if ( strcmp( argv[1], "status" ) == 0 )
    {
        if ( argc != 2U )
        {
            CONSOLE_Flash_PrintUsage();
            return;
        }
        CONSOLE_Flash_StatusCommand();
        return;
    }

    if ( strcmp( argv[1], "external_test" ) == 0 )
    {
        CONSOLE_Flash_ExternalTestCommand( argc, argv );
        return;
    }

#ifndef TEST_BUILD
    if ( strcmp( argv[1], "throughput_test" ) == 0 )
    {
        CONSOLE_Flash_ThroughputTestCommand( argc, argv );
        return;
    }
#endif

    if ( strcmp( argv[1], "upload_test" ) == 0 )
    {
        CONSOLE_Flash_UploadTestCommand( argc, argv );
        return;
    }

    if ( strcmp( argv[1], "upload_do_test" ) == 0 )
    {
        CONSOLE_Flash_UploadDigitalOutputTestCommand( argc, argv );
        return;
    }

    if ( strcmp( argv[1], "upload_do_pattern" ) == 0 )
    {
        CONSOLE_Flash_UploadDigitalPatternCommand( argc, argv );
        return;
    }

    if ( strcmp( argv[1], "upload_pwm_test" ) == 0 )
    {
        CONSOLE_Flash_UploadPwmTestCommand( argc, argv );
        return;
    }

    if ( strcmp( argv[1], "upload_ao_test" ) == 0 )
    {
        CONSOLE_Flash_UploadAnalogueOutputTestCommand( argc, argv );
        return;
    }

    if ( strcmp( argv[1], "upload_can_test" ) == 0 )
    {
        CONSOLE_Flash_UploadCanTestCommand( argc, argv );
        return;
    }

    if ( strcmp( argv[1], "upload_spi_test" ) == 0 )
    {
        CONSOLE_Flash_UploadSpiTestCommand( argc, argv );
        return;
    }

    if ( strcmp( argv[1], "upload_uart_test" ) == 0 )
    {
        CONSOLE_Flash_UploadUartTestCommand( argc, argv );
        return;
    }

    if ( strcmp( argv[1], "upload_output_stress" ) == 0 )
    {
        CONSOLE_Flash_UploadOutputStressTestCommand( argc, argv );
        return;
    }

    if ( ( strcmp( argv[1], "prepare" ) == 0 ) && ( argc == 2U ) )
    {
        CONSOLE_Flash_PrepareCommand();
        return;
    }

    if ( strcmp( argv[1], "execute_echo" ) == 0 )
    {
        CONSOLE_Flash_ExecuteEchoCommand( argc, argv );
        return;
    }

    if ( ( strcmp( argv[1], "finalise" ) == 0 ) && ( argc == 2U ) )
    {
        CONSOLE_Flash_FinaliseCommand();
        return;
    }

    if ( strcmp( argv[1], "results" ) == 0 )
    {
        if ( argc == 5U && strcmp( argv[2], "verify_do_di" ) == 0 )
        {
            CONSOLE_Flash_VerifyDigitalLoopbackResultsCommand( argc, argv );
            return;
        }

        if ( argc == 3U && strcmp( argv[2], "verify_do_pattern" ) == 0 )
        {
            CONSOLE_Flash_VerifyDigitalLoopbackResultsCommand( argc, argv );
            return;
        }

        if ( argc == 4U && strcmp( argv[2], "verify_ao_ai" ) == 0 )
        {
            CONSOLE_Flash_VerifyAnalogueLoopbackResultsCommand( argc, argv );
            return;
        }

        if ( argc == 4U && strcmp( argv[2], "verify_pwm_capture" ) == 0 )
        {
            CONSOLE_Flash_VerifyPwmLoopbackResultsCommand( argc, argv );
            return;
        }

        if ( argc == 6U && strcmp( argv[2], "verify_uart_loopback" ) == 0 )
        {
            CONSOLE_Flash_VerifyUartLoopbackResultsCommand( argc, argv );
            return;
        }

        if ( argc == 6U && strcmp( argv[2], "verify_spi_loopback" ) == 0 )
        {
            CONSOLE_Flash_VerifySpiLoopbackResultsCommand( argc, argv );
            return;
        }
        if ( argc == 7U && strcmp( argv[2], "verify_can_loopback" ) == 0 )
        {
            CONSOLE_Flash_VerifyCanLoopbackResultsCommand( argc, argv );
            return;
        }

        if ( argc == 3U && strcmp( argv[2], "verify_stress" ) == 0 )
        {
            CONSOLE_Flash_VerifyStressResultsCommand( argc, argv );
            return;
        }

        if ( ( argc == 2U ) || ( ( argc == 3U ) && ( strcmp( argv[2], "verify" ) == 0 ) ) )
        {
            CONSOLE_Flash_ResultsCommand( argc == 3U );
            return;
        }

        CONSOLE_Printf( "Usage: flash results [verify] | verify_stress\r\n" );
        CONSOLE_Printf( "  verify_do_di <delay_ticks> <high_ticks> | verify_do_pattern\r\n" );
        CONSOLE_Printf( "  verify_ao_ai <input_channel> | verify_pwm_capture <input_channel>\r\n" );
        CONSOLE_Printf( "  verify_uart_loopback <channel> <byte> <length>\r\n" );
        CONSOLE_Printf( "  verify_spi_loopback <channel> <byte> <length>\r\n" );
        CONSOLE_Printf( "  verify_can_loopback <channel> <id> <byte> <dlc>\r\n" );
        return;
    }

    CONSOLE_Printf( "Unknown flash command.\r\n" );
    CONSOLE_Flash_PrintUsage();
}
