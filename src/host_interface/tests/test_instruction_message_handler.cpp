/******************************************************************************
 *  File:       test_instruction_message_handler.cpp
 *  Author:     Callum Rafferty
 *  Created:    15-Sep-2026
 *
 *  Description:
 *      Unit tests and white-box tests for the instruction message handler using
 *      GoogleTest and GoogleMock.
 *
 *      These tests verify:
 *        - Parameter validation (null pointer handling).
 *        - Baseline reset behavior.
 *        - Peripheral state delta tracking (output-free ticks omitted).
 *        - Canonical Execution Manager formatting and 4-byte operation alignment.
 *        - Low-level internal operation appending and buffer bounds (white-box).
 *        - Hardware validation failure propagation.
 *        - Flash Manager upload return status translation.
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <vector>

extern "C"
{
#include "exec_analogue_output.h"
#include "exec_digital_output.h"
#include "execution_manager/execution_instruction.h"
#include "execution_manager/execution_operation_payloads.h"
#include "flash_manager/flash_manager.h"
#include "hil_rig_protocol/application/application_instruction.h"
#include "hw_pwm_gen.h"
#include "instruction_message_handler.h"

/* Include .c directly for complete white-box testing of static internals */
#include "instruction_message_handler.c"  // NOLINT
}

using ::testing::_;
using ::testing::DoAll;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SetArgPointee;

/**-----------------------------------------------------------------------------
 *  Test Doubles / Mocks
 *------------------------------------------------------------------------------
 */

class MockInstructionHandlerDependencies
{
public:
    virtual ~MockInstructionHandlerDependencies() = default;

    /* Flash Manager Upload Mock */
    MOCK_METHOD( FlashManagerInstructionUploadRequestStatus_T,
                 FLASH_MANAGER_SubmitInstructionUploadBytes,
                 ( const uint8_t* data, uint32_t length ) );

    /* Digital Output Driver Mock */
    MOCK_METHOD( DigitalOutputPinmask_T, EXEC_DIGITAL_OUTPUT_Combine_Port_Pin_Masks,
                 ( GPIOOutput_T * pins, uint8_t count ) );

    /* Analogue Output Driver Mock */
    MOCK_METHOD( bool, EXEC_ANALOGUE_OUTPUT_Prepare_Frame,
                 ( uint8_t channel, float voltage, AnalogueOutputPreparedFrame_T* frame ) );

    /* PWM Generation Low-Level Driver Mocks */
    MOCK_METHOD( bool, HW_PWM_GEN_compute_psc,
                 ( uint32_t frequency_hz, uint32_t timer_clock_hz, uint16_t* psc ) );
    MOCK_METHOD( bool, HW_PWM_GEN_compute_arr,
                 ( uint32_t frequency_hz, uint32_t timer_clock_hz, uint16_t psc, uint16_t* arr ) );
    MOCK_METHOD( bool, HW_PWM_GEN_compute_ccr,
                 ( uint16_t duty_permille, uint16_t arr, uint16_t* ccr ) );
};

static MockInstructionHandlerDependencies* g_mock_deps = nullptr;

/**-----------------------------------------------------------------------------
 *  Link Seams: Mocked C Function Definitions
 *------------------------------------------------------------------------------
 */

extern "C" FlashManagerInstructionUploadRequestStatus_T
FLASH_MANAGER_SubmitInstructionUploadBytes( const uint8_t* data, uint32_t length )
{
    if ( g_mock_deps != nullptr )
    {
        return g_mock_deps->FLASH_MANAGER_SubmitInstructionUploadBytes( data, length );
    }
    return FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_ACCEPTED;
}

extern "C" DigitalOutputPinmask_T EXEC_DIGITAL_OUTPUT_Combine_Port_Pin_Masks( GPIOOutput_T* pins,
                                                                              uint8_t       count )
{
    if ( g_mock_deps != nullptr )
    {
        return g_mock_deps->EXEC_DIGITAL_OUTPUT_Combine_Port_Pin_Masks( pins, count );
    }
    uint32_t mask = 0U;
    for ( uint8_t i = 0U; i < count; i++ )
    {
        mask |= ( 1U << ( ( uint32_t )pins[i] - ( uint32_t )DIGITAL_OUTPUT_0 ) );
    }
    return mask;
}

extern "C" bool EXEC_ANALOGUE_OUTPUT_Prepare_Frame( uint8_t channel, float voltage,
                                                    AnalogueOutputPreparedFrame_T* frame )
{
    if ( g_mock_deps != nullptr )
    {
        return g_mock_deps->EXEC_ANALOGUE_OUTPUT_Prepare_Frame( channel, voltage, frame );
    }
    if ( frame != nullptr )
    {
        frame->bytes[0] = channel;
        frame->bytes[1] = 0xAA;
        frame->bytes[2] = 0x55;
    }
    return true;
}

extern "C" bool HW_PWM_GEN_compute_psc( uint32_t frequency_hz, uint32_t timer_clock_hz,
                                        uint16_t* psc )
{
    if ( g_mock_deps != nullptr )
    {
        return g_mock_deps->HW_PWM_GEN_compute_psc( frequency_hz, timer_clock_hz, psc );
    }
    ( void )frequency_hz;
    ( void )timer_clock_hz;
    if ( psc != nullptr )
    {
        *psc = 0U;
    }
    return true;
}

extern "C" bool HW_PWM_GEN_compute_arr( uint32_t frequency_hz, uint32_t timer_clock_hz,
                                        uint16_t psc, uint16_t* arr )
{
    if ( g_mock_deps != nullptr )
    {
        return g_mock_deps->HW_PWM_GEN_compute_arr( frequency_hz, timer_clock_hz, psc, arr );
    }
    ( void )frequency_hz;
    ( void )timer_clock_hz;
    ( void )psc;
    if ( arr != nullptr )
    {
        *arr = 1000U;
    }
    return true;
}

extern "C" bool HW_PWM_GEN_compute_ccr( uint16_t duty_permille, uint16_t arr, uint16_t* ccr )
{
    if ( g_mock_deps != nullptr )
    {
        return g_mock_deps->HW_PWM_GEN_compute_ccr( duty_permille, arr, ccr );
    }
    ( void )duty_permille;
    ( void )arr;
    if ( ccr != nullptr )
    {
        *ccr = 500U;
    }
    return true;
}

/**-----------------------------------------------------------------------------
 *  Test Fixture
 *------------------------------------------------------------------------------
 */

class InstructionMessageHandlerTest : public ::testing::Test
{
protected:
    std::vector<uint8_t> uploaded_data;

    void SetUp() override
    {
        g_mock_deps = new NiceMock<MockInstructionHandlerDependencies>();
        uploaded_data.clear();

        /* Default mock behaviors */
        ON_CALL( *g_mock_deps, FLASH_MANAGER_SubmitInstructionUploadBytes( _, _ ) )
            .WillByDefault( [this]( const uint8_t* data, uint32_t length ) {
                uploaded_data.assign( data, data + length );
                return FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_ACCEPTED;
            } );

        ON_CALL( *g_mock_deps, EXEC_DIGITAL_OUTPUT_Combine_Port_Pin_Masks( _, _ ) )
            .WillByDefault( []( GPIOOutput_T* pins, uint8_t count ) {
                uint32_t mask = 0U;
                for ( uint8_t i = 0U; i < count; i++ )
                {
                    mask |= ( 1U << ( ( uint32_t )pins[i] - ( uint32_t )DIGITAL_OUTPUT_0 ) );
                }
                return mask;
            } );

        ON_CALL( *g_mock_deps, EXEC_ANALOGUE_OUTPUT_Prepare_Frame( _, _, _ ) )
            .WillByDefault( []( uint8_t channel, float, AnalogueOutputPreparedFrame_T* frame ) {
                if ( frame != nullptr )
                {
                    frame->bytes[0] = channel;
                    frame->bytes[1] = 0x12;
                    frame->bytes[2] = 0x34;
                }
                return true;
            } );

        ON_CALL( *g_mock_deps, HW_PWM_GEN_compute_psc( _, _, _ ) )
            .WillByDefault( []( uint32_t, uint32_t, uint16_t* psc ) {
                if ( psc != nullptr )
                {
                    *psc = 4U;
                }
                return true;
            } );

        ON_CALL( *g_mock_deps, HW_PWM_GEN_compute_arr( _, _, _, _ ) )
            .WillByDefault( []( uint32_t, uint32_t, uint16_t, uint16_t* arr ) {
                if ( arr != nullptr )
                {
                    *arr = 999U;
                }
                return true;
            } );

        ON_CALL( *g_mock_deps, HW_PWM_GEN_compute_ccr( _, _, _ ) )
            .WillByDefault( []( uint16_t, uint16_t, uint16_t* ccr ) {
                if ( ccr != nullptr )
                {
                    *ccr = 500U;
                }
                return true;
            } );

        HOST_INSTRUCTION_HANDLER_Reset();
    }

    void TearDown() override
    {
        delete g_mock_deps;
        g_mock_deps = nullptr;
    }
};

/**-----------------------------------------------------------------------------
 *  White-Box Tests: Internal Operation Writer & Alignment
 *------------------------------------------------------------------------------
 */

TEST_F( InstructionMessageHandlerTest, AppendOperationEnforces4ByteAlignmentAndPadding )
{
    uint8_t buffer[64];
    ( void )memset( buffer, 0xEE, sizeof( buffer ) );

    HostInstructionWriter_T writer;
    writer.buffer          = buffer;
    writer.capacity        = sizeof( buffer );
    writer.offset          = 0U;
    writer.operation_count = 0U;

    /* 3-byte payload: Header (4 bytes) + Payload (3 bytes) + Padding (1 byte) = 8 bytes total */
    const uint8_t                 payload[3] = { 0x11, 0x22, 0x33 };
    const HOST_Interface_Status_T status     = HOST_INSTRUCTION_HANDLER_AppendOperation(
        &writer, EXECUTION_OPERATION_OPCODE_ANALOGUE_OUTPUT_BATCH,
        EXECUTION_OPERATION_CHANNEL_UNUSED, payload, sizeof( payload ) );

    EXPECT_EQ( status, HOST_INTERFACE_STATUS_OK );
    EXPECT_EQ( writer.offset, 8U );
    EXPECT_EQ( writer.operation_count, 1U );

    /* Verify header word: [length:16][channel:8][opcode:8] */
    const ExecutionOperationHeaderWord_T header_word =
        *reinterpret_cast<const ExecutionOperationHeaderWord_T*>( buffer );
    EXPECT_EQ( header_word & EXECUTION_OPERATION_OPCODE_MASK,
               static_cast<uint32_t>( EXECUTION_OPERATION_OPCODE_ANALOGUE_OUTPUT_BATCH ) );
    EXPECT_EQ( ( header_word & EXECUTION_OPERATION_CHANNEL_MASK )
                   >> EXECUTION_OPERATION_CHANNEL_SHIFT,
               EXECUTION_OPERATION_CHANNEL_UNUSED );
    EXPECT_EQ( ( header_word & EXECUTION_OPERATION_PAYLOAD_LENGTH_MASK )
                   >> EXECUTION_OPERATION_PAYLOAD_LENGTH_SHIFT,
               3U );

    /* Verify payload copied */
    EXPECT_EQ( buffer[4], 0x11 );
    EXPECT_EQ( buffer[5], 0x22 );
    EXPECT_EQ( buffer[6], 0x33 );
    /* Verify 4-byte alignment padding byte is zeroed */
    EXPECT_EQ( buffer[7], 0x00 );
}

TEST_F( InstructionMessageHandlerTest, AppendOperationReturnsBufferTooSmallWhenCapacityExceeded )
{
    uint8_t buffer[6];

    HostInstructionWriter_T writer;
    writer.buffer          = buffer;
    writer.capacity        = sizeof( buffer );
    writer.offset          = 0U;
    writer.operation_count = 0U;

    const uint8_t payload[4] = { 1, 2, 3, 4 };
    /* Required = 4 (header) + 4 (payload) = 8 bytes > 6 bytes capacity */
    const HOST_Interface_Status_T status = HOST_INSTRUCTION_HANDLER_AppendOperation(
        &writer, EXECUTION_OPERATION_OPCODE_DIGITAL_OUTPUT_UPDATE,
        EXECUTION_OPERATION_CHANNEL_UNUSED, payload, sizeof( payload ) );

    EXPECT_EQ( status, HOST_INTERFACE_STATUS_BUFFER_TOO_SMALL );
}

/**-----------------------------------------------------------------------------
 *  White-Box Tests: Internal State Tracker & Monotonicity
 *------------------------------------------------------------------------------
 */

TEST_F( InstructionMessageHandlerTest, ResetInitializesInternalStateVariables )
{
    EXPECT_TRUE( tracked_peripheral_state.initialized );
    EXPECT_EQ( last_instruction_timestamp, 0U );

    for ( uint8_t i = 0U; i < HIL_APPLICATION_DIGITAL_OUTPUT_CHANNEL_COUNT; i++ )
    {
        EXPECT_EQ( tracked_peripheral_state.digital_outputs[i], 0U );
    }
    for ( uint8_t i = 0U; i < HIL_APPLICATION_ANALOG_OUTPUT_CHANNEL_COUNT; i++ )
    {
        EXPECT_EQ( tracked_peripheral_state.analog_outputs[i], 0U );
    }
    for ( uint8_t i = 0U; i < HIL_APPLICATION_PWM_OUTPUT_CHANNEL_COUNT; i++ )
    {
        EXPECT_EQ( tracked_peripheral_state.pwm_outputs[i].period_nanoseconds, 0U );
        EXPECT_EQ( tracked_peripheral_state.pwm_outputs[i].duty_cycle_permyriad, 0U );
    }
}

/**-----------------------------------------------------------------------------
 *  Public API Tests: Parameter Validation
 *------------------------------------------------------------------------------
 */

TEST_F( InstructionMessageHandlerTest, NullInstructionReturnsInvalidArgument )
{
    EXPECT_EQ( HOST_INSTRUCTION_HANDLER_HandleInstruction( nullptr ),
               HOST_INTERFACE_STATUS_INVALID_ARGUMENT );
}

/**-----------------------------------------------------------------------------
 *  Public API Tests: Tick Ordering & Monotonicity Validation
 *------------------------------------------------------------------------------
 */

TEST_F( InstructionMessageHandlerTest, NonConsecutiveIncreasingTicksAreAccepted )
{
    HIL_Application_Test_Instruction_T instruction = {};

    /* First tick: 1 */
    instruction.tick_number             = 1U;
    instruction.digital_outputs[0].high = 1U;
    EXPECT_EQ( HOST_INSTRUCTION_HANDLER_HandleInstruction( &instruction ),
               HOST_INTERFACE_STATUS_OK );
    EXPECT_EQ( last_instruction_timestamp, 1U );

    /* Non-consecutive increasing tick: 5 */
    instruction.tick_number             = 5U;
    instruction.digital_outputs[1].high = 1U;
    EXPECT_EQ( HOST_INSTRUCTION_HANDLER_HandleInstruction( &instruction ),
               HOST_INTERFACE_STATUS_OK );
    EXPECT_EQ( last_instruction_timestamp, 5U );

    /* Non-consecutive increasing tick: 100 */
    instruction.tick_number             = 100U;
    instruction.digital_outputs[2].high = 1U;
    EXPECT_EQ( HOST_INSTRUCTION_HANDLER_HandleInstruction( &instruction ),
               HOST_INTERFACE_STATUS_OK );
    EXPECT_EQ( last_instruction_timestamp, 100U );
}

TEST_F( InstructionMessageHandlerTest, DuplicateTickNumberIsRejected )
{
    HIL_Application_Test_Instruction_T instruction = {};

    /* Tick 5 */
    instruction.tick_number             = 5U;
    instruction.digital_outputs[0].high = 1U;
    EXPECT_EQ( HOST_INSTRUCTION_HANDLER_HandleInstruction( &instruction ),
               HOST_INTERFACE_STATUS_OK );

    /* Duplicate tick 5 */
    instruction.digital_outputs[1].high = 1U;
    EXPECT_EQ( HOST_INSTRUCTION_HANDLER_HandleInstruction( &instruction ),
               HOST_INTERFACE_STATUS_INCONSISTENT_TICK );
}

TEST_F( InstructionMessageHandlerTest, DecreasingOutOfOrderTickIsRejected )
{
    HIL_Application_Test_Instruction_T instruction = {};

    /* Tick 10 */
    instruction.tick_number             = 10U;
    instruction.digital_outputs[0].high = 1U;
    EXPECT_EQ( HOST_INSTRUCTION_HANDLER_HandleInstruction( &instruction ),
               HOST_INTERFACE_STATUS_OK );

    /* Out-of-order earlier tick 8 */
    instruction.tick_number             = 8U;
    instruction.digital_outputs[1].high = 1U;
    EXPECT_EQ( HOST_INSTRUCTION_HANDLER_HandleInstruction( &instruction ),
               HOST_INTERFACE_STATUS_INCONSISTENT_TICK );
}

TEST_F( InstructionMessageHandlerTest, ResetAllowsNewTickSequenceFromLowerTick )
{
    HIL_Application_Test_Instruction_T instruction = {};

    /* First run: tick 50 */
    instruction.tick_number             = 50U;
    instruction.digital_outputs[0].high = 1U;
    EXPECT_EQ( HOST_INSTRUCTION_HANDLER_HandleInstruction( &instruction ),
               HOST_INTERFACE_STATUS_OK );

    /* Reset handler */
    HOST_INSTRUCTION_HANDLER_Reset();

    /* Second run can start from lower tick, e.g., tick 2 */
    instruction.tick_number             = 2U;
    instruction.digital_outputs[0].high = 1U;
    EXPECT_EQ( HOST_INSTRUCTION_HANDLER_HandleInstruction( &instruction ),
               HOST_INTERFACE_STATUS_OK );
    EXPECT_EQ( last_instruction_timestamp, 2U );
}

TEST_F( InstructionMessageHandlerTest, DigitalOutputValueAboveOneIsRejected )
{
    HIL_Application_Test_Instruction_T instruction = {};
    instruction.tick_number                        = 1U;
    instruction.digital_outputs[3].high            = 2U; /* Invalid state > 1 */

    EXPECT_EQ( HOST_INSTRUCTION_HANDLER_HandleInstruction( &instruction ),
               HOST_INTERFACE_STATUS_VALIDATION_FAILED );
}

TEST_F( InstructionMessageHandlerTest, PwmDutyAboveTenThousandIsRejected )
{
    HIL_Application_Test_Instruction_T instruction  = {};
    instruction.tick_number                         = 1U;
    instruction.pwm_outputs[0].period_nanoseconds   = 1000000U;
    instruction.pwm_outputs[0].duty_cycle_permyriad = 10001U; /* Invalid duty > 10000 */

    EXPECT_EQ( HOST_INSTRUCTION_HANDLER_HandleInstruction( &instruction ),
               HOST_INTERFACE_STATUS_VALIDATION_FAILED );
}

TEST_F( InstructionMessageHandlerTest, PwmDutyNonZeroWithZeroPeriodIsRejected )
{
    HIL_Application_Test_Instruction_T instruction = {};
    instruction.tick_number                        = 1U;
    instruction.pwm_outputs[0].period_nanoseconds  = 0U; /* Disabled output */
    instruction.pwm_outputs[0].duty_cycle_permyriad =
        5000U; /* Non-zero duty invalid when disabled */

    EXPECT_EQ( HOST_INSTRUCTION_HANDLER_HandleInstruction( &instruction ),
               HOST_INTERFACE_STATUS_VALIDATION_FAILED );
}

/**-----------------------------------------------------------------------------
 *  Public API Tests: Delta Tracking & Upload Logic
 *------------------------------------------------------------------------------
 */

TEST_F( InstructionMessageHandlerTest, AllZeroInstructionAfterResetIsOutputFreeTick )
{
    HIL_Application_Test_Instruction_T instruction = {};
    instruction.tick_number                        = 1U;

    /* Baseline after Reset() has all outputs at 0. An all-zero instruction has 0 deltas. */
    EXPECT_CALL( *g_mock_deps, FLASH_MANAGER_SubmitInstructionUploadBytes( _, _ ) ).Times( 0 );

    const HOST_Interface_Status_T status =
        HOST_INSTRUCTION_HANDLER_HandleInstruction( &instruction );

    EXPECT_EQ( status, HOST_INTERFACE_STATUS_OK );
    EXPECT_EQ( last_instruction_timestamp, 1U );
    EXPECT_TRUE( uploaded_data.empty() );
}

TEST_F( InstructionMessageHandlerTest, InitialNonZeroInstructionUploadsCanonicalOperations )
{
    HIL_Application_Test_Instruction_T instruction = {};
    instruction.tick_number                        = 1U;

    /* Set 2 digital outputs high */
    instruction.digital_outputs[0].high = 1U;
    instruction.digital_outputs[2].high = 1U;

    /* Set 1 analogue output to 2.5V (2,500,000 uV) */
    instruction.analog_outputs[1].microvolts = 2500000U;

    /* Set LV PWM output to 1 kHz (1,000,000 ns) @ 50% duty (5000 permyriad) */
    instruction.pwm_outputs[0].period_nanoseconds   = 1000000U;
    instruction.pwm_outputs[0].duty_cycle_permyriad = 5000U;

    EXPECT_CALL( *g_mock_deps, FLASH_MANAGER_SubmitInstructionUploadBytes( _, _ ) ).Times( 1 );

    const HOST_Interface_Status_T status =
        HOST_INSTRUCTION_HANDLER_HandleInstruction( &instruction );

    EXPECT_EQ( status, HOST_INTERFACE_STATUS_OK );
    EXPECT_EQ( last_instruction_timestamp, 1U );
    ASSERT_FALSE( uploaded_data.empty() );

    /* Validate ExecutionInstructionHeader_T */
    ASSERT_GE( uploaded_data.size(), sizeof( ExecutionInstructionHeader_T ) );
    const ExecutionInstructionHeader_T* header =
        reinterpret_cast<const ExecutionInstructionHeader_T*>( uploaded_data.data() );

    EXPECT_EQ( header->timestamp, 1U );
    EXPECT_EQ( header->operation_count, 3U ); /* Digital + Analogue + PWM */
    EXPECT_EQ( header->operations_length_bytes,
               uploaded_data.size() - sizeof( ExecutionInstructionHeader_T ) );
}

TEST_F( InstructionMessageHandlerTest, SubsequentIdenticalInstructionSkipsFlashUpload )
{
    HIL_Application_Test_Instruction_T instruction = {};
    instruction.tick_number                        = 1U;
    instruction.digital_outputs[1].high            = 1U;
    instruction.analog_outputs[0].microvolts       = 1000000U;

    /* Tick 1: Uploads changes */
    EXPECT_CALL( *g_mock_deps, FLASH_MANAGER_SubmitInstructionUploadBytes( _, _ ) ).Times( 1 );
    EXPECT_EQ( HOST_INSTRUCTION_HANDLER_HandleInstruction( &instruction ),
               HOST_INTERFACE_STATUS_OK );

    /* Tick 2: Exactly same state */
    instruction.tick_number = 2U;
    EXPECT_CALL( *g_mock_deps, FLASH_MANAGER_SubmitInstructionUploadBytes( _, _ ) ).Times( 0 );
    EXPECT_EQ( HOST_INSTRUCTION_HANDLER_HandleInstruction( &instruction ),
               HOST_INTERFACE_STATUS_OK );
    EXPECT_EQ( last_instruction_timestamp, 2U );
}

TEST_F( InstructionMessageHandlerTest, SinglePeripheralChangeEmitsOnlyThatOperation )
{
    HIL_Application_Test_Instruction_T instruction = {};
    instruction.tick_number                        = 1U;
    instruction.digital_outputs[0].high            = 1U;
    instruction.analog_outputs[0].microvolts       = 1000000U;

    /* Tick 1 establishes state */
    EXPECT_EQ( HOST_INSTRUCTION_HANDLER_HandleInstruction( &instruction ),
               HOST_INTERFACE_STATUS_OK );

    /* Tick 2: Only change analogue channel 1 */
    instruction.tick_number                  = 2U;
    instruction.analog_outputs[1].microvolts = 3000000U;

    EXPECT_CALL( *g_mock_deps, FLASH_MANAGER_SubmitInstructionUploadBytes( _, _ ) ).Times( 1 );
    EXPECT_EQ( HOST_INSTRUCTION_HANDLER_HandleInstruction( &instruction ),
               HOST_INTERFACE_STATUS_OK );

    /* Header should state 1 operation */
    const ExecutionInstructionHeader_T* header =
        reinterpret_cast<const ExecutionInstructionHeader_T*>( uploaded_data.data() );
    EXPECT_EQ( header->timestamp, 2U );
    EXPECT_EQ( header->operation_count, 1U );

    /* Operation should be opcode ANALOGUE_OUTPUT_BATCH */
    const ExecutionOperationHeaderWord_T op_header =
        *reinterpret_cast<const ExecutionOperationHeaderWord_T*>(
            uploaded_data.data() + sizeof( ExecutionInstructionHeader_T ) );
    EXPECT_EQ( op_header & EXECUTION_OPERATION_OPCODE_MASK,
               static_cast<uint32_t>( EXECUTION_OPERATION_OPCODE_ANALOGUE_OUTPUT_BATCH ) );
}

/**-----------------------------------------------------------------------------
 *  Public API Tests: Driver Validation Failures
 *------------------------------------------------------------------------------
 */

TEST_F( InstructionMessageHandlerTest, AnalogueFramePreparationFailureReturnsValidationFailed )
{
    HIL_Application_Test_Instruction_T instruction = {};
    instruction.tick_number                        = 1U;
    instruction.analog_outputs[0].microvolts       = 5000000U;

    /* Simulate DAC frame preparation failure */
    EXPECT_CALL( *g_mock_deps, EXEC_ANALOGUE_OUTPUT_Prepare_Frame( _, _, _ ) )
        .WillOnce( Return( false ) );

    EXPECT_EQ( HOST_INSTRUCTION_HANDLER_HandleInstruction( &instruction ),
               HOST_INTERFACE_STATUS_VALIDATION_FAILED );
}

TEST_F( InstructionMessageHandlerTest, PwmComputationFailureReturnsValidationFailed )
{
    HIL_Application_Test_Instruction_T instruction  = {};
    instruction.tick_number                         = 1U;
    instruction.pwm_outputs[0].period_nanoseconds   = 1000U;
    instruction.pwm_outputs[0].duty_cycle_permyriad = 5000U;

    /* Simulate PWM PSC computation failure */
    EXPECT_CALL( *g_mock_deps, HW_PWM_GEN_compute_psc( _, _, _ ) ).WillOnce( Return( false ) );

    EXPECT_EQ( HOST_INSTRUCTION_HANDLER_HandleInstruction( &instruction ),
               HOST_INTERFACE_STATUS_VALIDATION_FAILED );
}

/**-----------------------------------------------------------------------------
 *  Public API Tests: Flash Manager Status Translation
 *------------------------------------------------------------------------------
 */

TEST_F( InstructionMessageHandlerTest, FlashManagerBusyReturnsInternalError )
{
    HIL_Application_Test_Instruction_T instruction = {};
    instruction.tick_number                        = 1U;
    instruction.digital_outputs[0].high            = 1U;

    EXPECT_CALL( *g_mock_deps, FLASH_MANAGER_SubmitInstructionUploadBytes( _, _ ) )
        .WillOnce( Return( FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_BUSY ) );

    EXPECT_EQ( HOST_INSTRUCTION_HANDLER_HandleInstruction( &instruction ),
               HOST_INTERFACE_STATUS_INTERNAL_ERROR );
}

TEST_F( InstructionMessageHandlerTest, FlashManagerInvalidStateReturnsStateTransitionFailure )
{
    HIL_Application_Test_Instruction_T instruction = {};
    instruction.tick_number                        = 1U;
    instruction.digital_outputs[0].high            = 1U;

    EXPECT_CALL( *g_mock_deps, FLASH_MANAGER_SubmitInstructionUploadBytes( _, _ ) )
        .WillOnce( Return( FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_INVALID_STATE ) );

    EXPECT_EQ( HOST_INSTRUCTION_HANDLER_HandleInstruction( &instruction ),
               HOST_INTERFACE_STATUS_STATE_TRANSITION_FAILURE );
}
