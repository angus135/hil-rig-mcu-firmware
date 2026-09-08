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
 *      1. `flash init`
 *         Initialise Flash Manager and start its task. Normal firmware startup
 *         has already adopted hqspi and initialised External Flash; the command
 *         retains a hardware-only fallback for isolated bring-up builds.
 *
 *      2. `flash status`
 *         Require IDLE and inspect NAND page size, partition lengths/capacity,
 *         bad-block count, ECC state, QSPI busy state and echo-harness state.
 *
 *      3. `flash external_test [seed]`
 *         Exercise QSPI -> NAND -> External Flash directly. One full and one
 *         partial page are programmed and read back in both logical
 *         partitions. This overwrites both partitions.
 *
 *      4. `flash upload_test [instruction_count] [seed]`
 *         Exercise Host Interface -> Flash Manager -> External Flash. The
 *         default deterministic instruction stream crosses the three-page RAM
 *         ring and finishes on a partial page. Instruction N is assigned tick
 *         N and contains one diagnostic operation with eleven opaque bytes.
 *
 *      5. `flash prepare`
 *         Start a result session and preload instructions. Do not continue
 *         until the command reports FLASH_MANAGER_STATE_EXECUTING.
 *
 *      6. `flash execute_echo [100|1000|10000]`
 *         Temporarily route TIM4 to the console's genuine ISR test callback;
 *         100 Hz is the safe first-bring-up default. For every due instruction,
 *         the ISR peeks, reserves result storage, copies the eleven operation
 *         bytes, commits byte-compatible diagnostic metadata, and consumes the
 *         complete instruction. A future instruction is left unconsumed for
 *         the next tick. The command waits while the Flash Manager task
 *         concurrently refills/drains pages, stops TIM4, and restores the
 *         production callback.
 *
 *      7. `flash finalise`
 *         After execute_echo has returned, publish and drain the final partial
 *         result page and wait for RESULTS_READY.
 *
 *      8. `flash results verify`
 *         Exercise Flash Manager -> Host Interface retrieval and compare the
 *         entire packed result stream byte-for-byte with the diagnostic
 *         instruction stream. The command prints length and FNV-1a checksum,
 *         consumes the transfer, and returns Flash Manager to IDLE.
 *
 *      9. Repeat step 4 onward at 1 kHz and 10 kHz, then with larger record
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
#include "execution_operation_payloads.h"
#include "exec_analogue_output.h"
#include "exec_can.h"
#include "exec_digital_output.h"
#include "exec_spi.h"
#include "exec_uart.h"
#include "flash_manager.h"
#include "hw_nand.h"
#include "hw_pwm_gen.h"
#include "hw_qspi.h"
#include "hw_timer.h"
#ifndef TEST_BUILD
#include "quadspi.h"
#endif
#include "rtos_config.h"
#include "test_configuration.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */

#define CONSOLE_FLASH_QSPI_TIMEOUT_MS ( 1000U )
#define CONSOLE_FLASH_STATE_TIMEOUT_MS ( 30000U )
#define CONSOLE_FLASH_PROGRESS_TIMEOUT_MS ( 30000U )
#define CONSOLE_FLASH_POLL_PERIOD_MS ( 10U )
#define CONSOLE_FLASH_RESULT_READ_BYTES ( 256U )

#define CONSOLE_FLASH_TEST_PAYLOAD_BYTES ( 12U )
#define CONSOLE_FLASH_TEST_OPERATION_COUNT ( 1U )
#define CONSOLE_FLASH_DEFAULT_SEED ( 0x31U )
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

/**-----------------------------------------------------------------------------
 *  Private (static) Variables
 *------------------------------------------------------------------------------
 */

static TaskHandle_t console_flash_manager_task_handle = NULL;
static bool         console_flash_manager_needs_task  = false;

static uint8_t console_flash_write_buffer[EXTERNAL_FLASH_MAX_PAGE_SIZE_BYTES];
static uint8_t console_flash_read_buffer[EXTERNAL_FLASH_MAX_PAGE_SIZE_BYTES];

static uint32_t console_flash_last_upload_records = 0U;
static uint32_t console_flash_last_upload_bytes   = 0U;
static uint8_t  console_flash_last_upload_seed    = 0U;
static uint32_t console_flash_run_tick_count      = 0U;

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
static void     CONSOLE_Flash_ExecutionEchoFromISR( void );
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
static void     CONSOLE_Flash_FillPattern( uint8_t* destination, uint32_t stream_offset,
                                           uint32_t length, uint8_t seed );
static bool     CONSOLE_Flash_VerifyPattern( const uint8_t* data, uint32_t stream_offset,
                                             uint32_t length, uint8_t seed,
                                             uint32_t* first_bad_offset );
static void     CONSOLE_Flash_FillInstructionChunk( uint8_t* destination, uint32_t stream_offset,
                                                    uint32_t length, uint8_t seed );
static uint32_t CONSOLE_Flash_Fnv1aUpdate( uint32_t hash, const uint8_t* data, uint32_t length );

static void CONSOLE_Flash_InitCommand( void );
static void CONSOLE_Flash_StatusCommand( void );
static void CONSOLE_Flash_ExternalTestCommand( uint16_t argc, char* argv[] );
static void CONSOLE_Flash_UploadTestCommand( uint16_t argc, char* argv[] );
static void CONSOLE_Flash_UploadDigitalOutputTestCommand( uint16_t argc, char* argv[] );
static void CONSOLE_Flash_UploadPwmTestCommand( uint16_t argc, char* argv[] );
static void CONSOLE_Flash_UploadAnalogueOutputTestCommand( uint16_t argc, char* argv[] );
static void CONSOLE_Flash_UploadCanTestCommand( uint16_t argc, char* argv[] );
static void CONSOLE_Flash_UploadSpiTestCommand( uint16_t argc, char* argv[] );
static void CONSOLE_Flash_UploadUartTestCommand( uint16_t argc, char* argv[] );
static void CONSOLE_Flash_PrepareCommand( void );
static void CONSOLE_Flash_ExecuteEchoCommand( uint16_t argc, char* argv[] );
static void CONSOLE_Flash_FinaliseCommand( void );
static void CONSOLE_Flash_ResultsCommand( bool verify_echo_stream );

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

/** Prints the staged destructive-test workflow. */
static void CONSOLE_Flash_PrintUsage( void )
{
    CONSOLE_Printf( "Flash hardware bring-up (destructive):\r\n" );
    CONSOLE_Printf( "  flash init\r\n" );
    CONSOLE_Printf( "  flash status\r\n" );
    CONSOLE_Printf( "  flash external_test [seed]\r\n" );
    CONSOLE_Printf( "  flash upload_test [instruction_count] [seed]\r\n" );
    CONSOLE_Printf( "  flash upload_do_test <channel 1..10> [delay_ticks] [high_ticks]\r\n" );
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
    CONSOLE_Printf( "  flash prepare\r\n" );
    CONSOLE_Printf( "  flash execute_echo [100|1000|10000]\r\n" );
    CONSOLE_Printf( "  flash finalise\r\n" );
    CONSOLE_Printf( "  flash results [verify]\r\n" );
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

/** Requires an initialised and idle Flash Manager before destructive direct tests. */
static bool CONSOLE_Flash_RequireIdle( void )
{
    FlashManagerState_T state = FLASH_MANAGER_STATE_UNINITIALISED;

    if ( !FLASH_MANAGER_GetState( &state ) )
    {
        CONSOLE_Printf( "Flash Manager is not initialised. Run 'flash init'.\r\n" );
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
static void CONSOLE_Flash_ExecutionEchoFromISR( void )
{
    BaseType_t higher_priority_task_woken = pdFALSE;

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
            &higher_priority_task_woken );

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

        if ( !FLASH_MANAGER_ConsumeInstructionFromISR( &higher_priority_task_woken ) )
        {
            CONSOLE_Flash_EndExecutionHarnessFromISR(
                CONSOLE_FLASH_EXECUTION_TEST_FAILED,
                CONSOLE_FLASH_EXECUTION_FAILURE_INSTRUCTION_CONSUME );
            break;
        }

        console_flash_execution_test.instructions_consumed++;
    }

    /* Pend any requested context switch only after the complete tick sequence. */
    portYIELD_FROM_ISR( higher_priority_task_woken );
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

/** Initialises the complete storage stack and starts the Flash Manager task. */
static void CONSOLE_Flash_InitCommand( void )
{
    if ( !EXTERNAL_FLASH_IsInitialised() )
    {
#ifdef TEST_BUILD
        /*
         * Host builds use the QSPI mock types and do not contain CubeMX's
         * global hqspi handle. Hardware startup normally initialises this
         * layer before the console becomes available.
         */
        CONSOLE_Printf( "External Flash is not initialised. "
                        "Initialise the hardware storage stack at startup.\r\n" );
        return;
#else
        HW_QSPI_Status_T qspi_status = HW_QSPI_AdoptHandle( &hqspi, CONSOLE_FLASH_QSPI_TIMEOUT_MS );
        if ( qspi_status != HW_QSPI_STATUS_OK )
        {
            CONSOLE_Printf( "QSPI adopt failed (status=%d).\r\n", ( int )qspi_status );
            return;
        }

        ExternalFlashStatus_T flash_status = EXTERNAL_FLASH_Init();
        if ( flash_status != EXTERNAL_FLASH_STATUS_OK )
        {
            CONSOLE_Printf( "External Flash init failed (status=%d).\r\n", ( int )flash_status );
            return;
        }
#endif
    }

    FlashManagerState_T state                  = FLASH_MANAGER_STATE_UNINITIALISED;
    bool                manager_is_initialised = FLASH_MANAGER_GetState( &state );

    if ( !manager_is_initialised )
    {
        if ( !FLASH_MANAGER_Init() )
        {
            CONSOLE_Printf( "Flash Manager init failed.\r\n" );
            return;
        }

        console_flash_manager_needs_task = true;
    }

    if ( console_flash_manager_needs_task )
    {
        if ( CREATE_TASK( FLASH_MANAGER_Task, "Flash Manager Task", FLASH_MANAGER_TASK_MEMORY,
                          FLASH_MANAGER_TASK_PRIORITY, &console_flash_manager_task_handle )
             != pdPASS )
        {
            CONSOLE_Printf( "Flash Manager task creation failed. Retry 'flash init'.\r\n" );
            return;
        }

        console_flash_manager_needs_task = false;
        vTaskDelay( 1U );
    }

    ExternalFlashInfo_T info = { 0 };
    if ( EXTERNAL_FLASH_GetInfo( &info ) != EXTERNAL_FLASH_STATUS_OK )
    {
        CONSOLE_Printf( "Flash stack initialised but GetInfo failed.\r\n" );
        return;
    }

    CONSOLE_Printf( "Flash stack ready: page=%lu, bad_blocks=%lu.\r\n",
                    ( unsigned long )info.page_size_bytes, ( unsigned long )info.bad_block_count );
    CONSOLE_Printf( "Run 'flash status', then 'flash external_test'.\r\n" );
}

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
    CONSOLE_Flash_ResetExecutionHarnessState();

    CONSOLE_Printf( "PWM upload PASS: channel=%lu target=%lu Hz duty=%lu/1000 "
                    "arr=%u ccr=%u psc=%u update_tick=%lu run_ticks=%lu.\r\n",
                    ( unsigned long )channel, ( unsigned long )frequency_hz,
                    ( unsigned long )duty_permille, ( unsigned int )payload.arr,
                    ( unsigned int )payload.ccr, ( unsigned int )payload.psc,
                    ( unsigned long )update_tick, ( unsigned long )run_ticks );
    CONSOLE_Printf( "Next: finish configuring the RSM, then 'run_state execute %lu 0'.\r\n",
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

    CONSOLE_Printf( "Analogue-output upload PASS: channel=%lu voltage=%s "
                    "update_tick=%lu run_ticks=%lu.\r\n",
                    ( unsigned long )channel, argv[3], ( unsigned long )update_tick,
                    ( unsigned long )run_ticks );
    CONSOLE_Printf( "Next: finish configuring the RSM, then 'run_state execute %lu 0'.\r\n",
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
         || !CONSOLE_Flash_ParseU32( argv[4], &value )
         || !CONSOLE_Flash_ParseU32( argv[5], &dlc )
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

    const uint32_t upload_bytes = repeat_count * instruction_bytes;
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
            if ( CONSOLE_Flash_HasTimedOut( finish_started_at,
                                            CONSOLE_FLASH_PROGRESS_TIMEOUT_MS ) )
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
                    ( unsigned long )channel, ( unsigned long )identifier,
                    ( unsigned long )value, ( unsigned long )dlc, ( unsigned long )first_tick,
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
    CONSOLE_Printf( "Next: finish configuring the RSM, then 'run_state execute %lu 0'.\r\n",
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
    CONSOLE_Printf( "Next: finish configuring the RSM, then 'run_state execute %lu 0'.\r\n",
                    ( unsigned long )console_flash_run_tick_count );
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

    FlashManagerResultTransferStatus_T status = FLASH_MANAGER_RequestResultTransferStart();
    if ( status != FLASH_MANAGER_RESULT_TRANSFER_OK )
    {
        CONSOLE_Printf( "Result transfer start rejected (status=%d).\r\n", ( int )status );
        return;
    }

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

    status = FLASH_MANAGER_FinishResultTransfer();
    if ( status != FLASH_MANAGER_RESULT_TRANSFER_OK )
    {
        CONSOLE_Printf( "Result transfer finish failed (status=%d).\r\n", ( int )status );
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
        CONSOLE_Printf( "Result transfer was consumed and Flash Manager returned to IDLE.\r\n" );
        return;
    }

    CONSOLE_Printf( "Result retrieval PASS: bytes=%lu fnv1a=0x%08lX busy_retries=%lu.\r\n",
                    ( unsigned long )total_bytes, ( unsigned long )hash,
                    ( unsigned long )busy_retries );
    CONSOLE_Printf( "Flash Manager returned to IDLE.\r\n" );
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

    if ( strcmp( argv[1], "init" ) == 0 )
    {
        if ( argc != 2U )
        {
            CONSOLE_Flash_PrintUsage();
            return;
        }
        CONSOLE_Flash_InitCommand();
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
        if ( ( argc == 2U ) || ( ( argc == 3U ) && ( strcmp( argv[2], "verify" ) == 0 ) ) )
        {
            CONSOLE_Flash_ResultsCommand( argc == 3U );
            return;
        }

        CONSOLE_Printf( "Usage: flash results [verify]\r\n" );
        return;
    }

    CONSOLE_Printf( "Unknown flash command.\r\n" );
    CONSOLE_Flash_PrintUsage();
}
