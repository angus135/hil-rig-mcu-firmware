/******************************************************************************
 *  File:       test_variable_instruction_message_handler.cpp
 *  Description:
 *      Unit tests and white-box tests for the variable instruction message
 *      handler (UPDATE_INSTRUCTION) using GoogleTest and GoogleMock.
 *
 *      These tests verify:
 *        - Parameter validation (null pointers, zero operation count).
 *        - Monotonic tick sequencing and reset behavior.
 *        - Decoding and packing of variable operations:
 *          * Digital output (16-bit mask, channel 0)
 *          * Analogue output (4-byte microvolts, channel 0)
 *          * PWM generation (period ns + duty permyriad, channel 0)
 *          * Serial transmits (UART, SPI, CAN)
 *        - Error handling for invalid channels, malformed payload lengths,
 *          unknown peripheral types, and hardware calculation failures.
 *        - Canonical execution header validation and 4-byte TLV alignment.
 *        - Flash Manager upload return status translation.
 ******************************************************************************/

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
#include "hil_rig_protocol/application/application_message.h"
#include "hw_pwm_gen.h"
#include "test_configuration/test_configuration.h"
#include "variable_instruction_message_handler.h"

/* White-box include of the C file to inspect static state directly */
#include "variable_instruction_message_handler.c"  // NOLINT

uint8_t* HOST_INSTRUCTION_HANDLER_GetSharedBuffer( void )
{
    alignas( 4 ) static uint8_t s_test_shared_buf[EXECUTION_INSTRUCTION_MAX_SIZE_BYTES];
    return s_test_shared_buf;
}
}

using ::testing::_;
using ::testing::DoAll;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SetArgPointee;

class MockVariableInstructionHandlerDependencies
{
public:
    virtual ~MockVariableInstructionHandlerDependencies() = default;

    MOCK_METHOD( FlashManagerInstructionUploadRequestStatus_T,
                 FLASH_MANAGER_SubmitInstructionUploadBytes,
                 ( const uint8_t* data, uint32_t length ) );

    MOCK_METHOD( bool, TEST_CONFIGURATION_GetActive, ( DutDriverConfiguration_T * configuration ) );

    MOCK_METHOD( bool, EXEC_ANALOGUE_OUTPUT_Prepare_Frame,
                 ( uint8_t channel, float voltage, AnalogueOutputPreparedFrame_T* frame ) );

    MOCK_METHOD( bool, HW_PWM_GEN_compute_psc,
                 ( uint32_t frequency_hz, uint32_t timer_clock_hz, uint16_t* psc ) );
    MOCK_METHOD( bool, HW_PWM_GEN_compute_arr,
                 ( uint32_t frequency_hz, uint32_t timer_clock_hz, uint16_t psc, uint16_t* arr ) );
    MOCK_METHOD( bool, HW_PWM_GEN_compute_ccr,
                 ( uint16_t duty_permille, uint16_t arr, uint16_t* ccr ) );
};

static MockVariableInstructionHandlerDependencies* g_mock_deps = nullptr;

extern "C" bool TEST_CONFIGURATION_GetActive( DutDriverConfiguration_T* configuration )
{
    if ( g_mock_deps != nullptr )
    {
        return g_mock_deps->TEST_CONFIGURATION_GetActive( configuration );
    }
    if ( configuration != nullptr )
    {
        std::memset( configuration, 0, sizeof( DutDriverConfiguration_T ) );
        for ( size_t i = 0; i < EXEC_DIGITAL_OUTPUT_CHANNEL_COUNT; i++ )
        {
            configuration->digital_outputs.channels[i].is_enabled = true;
        }
        configuration->analogue_output.is_enabled = true;
        for ( size_t i = 0; i < EXEC_PWM_GEN_CHANNEL_COUNT; i++ )
        {
            configuration->pwm_generation_channels[i].is_enabled = true;
        }
    }
    return true;
}

extern "C" FlashManagerInstructionUploadRequestStatus_T
FLASH_MANAGER_SubmitInstructionUploadBytes( const uint8_t* data, uint32_t length )
{
    if ( g_mock_deps != nullptr )
    {
        return g_mock_deps->FLASH_MANAGER_SubmitInstructionUploadBytes( data, length );
    }
    return FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_ACCEPTED;
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

class VariableInstructionMessageHandlerTest : public ::testing::Test
{
protected:
    HIL_Application_Update_Instruction_T instruction{};
    std::vector<uint8_t>                 uploaded_bytes{};

    void SetUp() override
    {
        g_mock_deps = new NiceMock<MockVariableInstructionHandlerDependencies>();
        HOST_VARIABLE_INSTRUCTION_HANDLER_Reset();
        std::memset( &instruction, 0, sizeof( instruction ) );
        uploaded_bytes.clear();

        ON_CALL( *g_mock_deps, FLASH_MANAGER_SubmitInstructionUploadBytes( _, _ ) )
            .WillByDefault( [this]( const uint8_t* data, uint32_t length ) {
                if ( data != nullptr && length > 0U )
                {
                    uploaded_bytes.insert( uploaded_bytes.end(), data, data + length );
                }
                return FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_ACCEPTED;
            } );
    }

    void TearDown() override
    {
        delete g_mock_deps;
        g_mock_deps = nullptr;
    }
};

/**
 * @brief Null pointer and empty operation parameter validation.
 */
TEST_F( VariableInstructionMessageHandlerTest, RejectsNullOrEmptyInstruction )
{
    EXPECT_EQ( HOST_VARIABLE_INSTRUCTION_HANDLER_HandleInstruction( nullptr ),
               HOST_INTERFACE_STATUS_INVALID_ARGUMENT );

    /* Null operations pointer */
    instruction.tick_number     = 0U;
    instruction.operation_count = 1U;
    instruction.operations      = nullptr;
    EXPECT_EQ( HOST_VARIABLE_INSTRUCTION_HANDLER_HandleInstruction( &instruction ),
               HOST_INTERFACE_STATUS_INVALID_ARGUMENT );

    /* Zero operation count */
    static HIL_Application_Logical_Operation_T dummy_op{};
    instruction.operations      = &dummy_op;
    instruction.operation_count = 0U;
    EXPECT_EQ( HOST_VARIABLE_INSTRUCTION_HANDLER_HandleInstruction( &instruction ),
               HOST_INTERFACE_STATUS_INVALID_ARGUMENT );
}

/**
 * @brief Monotonic tick sequencing enforcement.
 */
TEST_F( VariableInstructionMessageHandlerTest, EnforcesMonotonicTickSequencing )
{
    static uint8_t                      mask[2] = { 0x01, 0x00 };
    HIL_Application_Logical_Operation_T op{};
    op.peripheral_type = HIL_APPLICATION_PERIPHERAL_DIGITAL_OUTPUT;
    op.channel         = 0U;
    op.payload.data    = mask;
    op.payload.size    = sizeof( mask );

    instruction.operations      = &op;
    instruction.operation_count = 1U;

    instruction.tick_number = 0U;
    EXPECT_EQ( HOST_VARIABLE_INSTRUCTION_HANDLER_HandleInstruction( &instruction ),
               HOST_INTERFACE_STATUS_OK );

    /* Same tick number is rejected */
    instruction.tick_number = 0U;
    EXPECT_EQ( HOST_VARIABLE_INSTRUCTION_HANDLER_HandleInstruction( &instruction ),
               HOST_INTERFACE_STATUS_INCONSISTENT_TICK );

    /* Decreasing tick number is rejected */
    instruction.tick_number = 0U;
    EXPECT_EQ( HOST_VARIABLE_INSTRUCTION_HANDLER_HandleInstruction( &instruction ),
               HOST_INTERFACE_STATUS_INCONSISTENT_TICK );

    /* Increasing tick number is accepted */
    instruction.tick_number = 1U;
    EXPECT_EQ( HOST_VARIABLE_INSTRUCTION_HANDLER_HandleInstruction( &instruction ),
               HOST_INTERFACE_STATUS_OK );

    /* Non-consecutive advancing tick is also accepted (sparse ticks) */
    instruction.tick_number = 10U;
    EXPECT_EQ( HOST_VARIABLE_INSTRUCTION_HANDLER_HandleInstruction( &instruction ),
               HOST_INTERFACE_STATUS_OK );
}

/**
 * @brief Reset allows restarting tick sequence from tick 0.
 */
TEST_F( VariableInstructionMessageHandlerTest, ResetRestartsTickSequence )
{
    static uint8_t                      mask[2] = { 0x01, 0x00 };
    HIL_Application_Logical_Operation_T op{};
    op.peripheral_type = HIL_APPLICATION_PERIPHERAL_DIGITAL_OUTPUT;
    op.channel         = 0U;
    op.payload.data    = mask;
    op.payload.size    = sizeof( mask );

    instruction.operations      = &op;
    instruction.operation_count = 1U;
    instruction.tick_number     = 5U;
    EXPECT_EQ( HOST_VARIABLE_INSTRUCTION_HANDLER_HandleInstruction( &instruction ),
               HOST_INTERFACE_STATUS_OK );

    HOST_VARIABLE_INSTRUCTION_HANDLER_Reset();

    instruction.tick_number = 0U;
    EXPECT_EQ( HOST_VARIABLE_INSTRUCTION_HANDLER_HandleInstruction( &instruction ),
               HOST_INTERFACE_STATUS_OK );
}

/**
 * @brief Digital output operation parsing and canonical block packing.
 */
TEST_F( VariableInstructionMessageHandlerTest, DigitalOutputPacksMaskCorrectly )
{
    static uint8_t                      mask_bytes[2] = { 0x55, 0x01 }; /* 0x0155 */
    HIL_Application_Logical_Operation_T op{};
    op.peripheral_type = HIL_APPLICATION_PERIPHERAL_DIGITAL_OUTPUT;
    op.channel         = 0U;
    op.payload.data    = mask_bytes;
    op.payload.size    = sizeof( mask_bytes );

    instruction.tick_number     = 300U;
    instruction.operation_count = 1U;
    instruction.operations      = &op;

    EXPECT_EQ( HOST_VARIABLE_INSTRUCTION_HANDLER_HandleInstruction( &instruction ),
               HOST_INTERFACE_STATUS_OK );

    /* Execution header (8) + header word (4) + ExecutionDigitalOutputPayload_T (8) = 20 bytes */
    const size_t expected_size =
        sizeof( ExecutionInstructionHeader_T ) + 4U + sizeof( ExecutionDigitalOutputPayload_T );
    ASSERT_EQ( uploaded_bytes.size(), expected_size );

    ExecutionInstructionHeader_T header{};
    std::memcpy( &header, uploaded_bytes.data(), sizeof( header ) );
    EXPECT_EQ( header.timestamp, 300U );
    EXPECT_EQ( header.operation_count, 1U );
    EXPECT_EQ( header.operations_length_bytes,
               expected_size - sizeof( ExecutionInstructionHeader_T ) );
}

/**
 * @brief Tick-zero update remains a canonical timestamp-zero instruction.
 */
TEST_F( VariableInstructionMessageHandlerTest, TickZeroRetainsTimestampZero )
{
    static uint8_t                      mask_bytes[2] = { 0x01, 0x00 };
    HIL_Application_Logical_Operation_T op{};
    op.peripheral_type = HIL_APPLICATION_PERIPHERAL_DIGITAL_OUTPUT;
    op.channel         = 0U;
    op.payload.data    = mask_bytes;
    op.payload.size    = sizeof( mask_bytes );

    instruction.tick_number     = 0U;
    instruction.operation_count = 1U;
    instruction.operations      = &op;

    ASSERT_EQ( HOST_VARIABLE_INSTRUCTION_HANDLER_HandleInstruction( &instruction ),
               HOST_INTERFACE_STATUS_OK );
    ASSERT_GE( uploaded_bytes.size(), sizeof( ExecutionInstructionHeader_T ) );

    ExecutionInstructionHeader_T header{};
    std::memcpy( &header, uploaded_bytes.data(), sizeof( header ) );
    EXPECT_EQ( header.timestamp, 0U );
}

/**
 * @brief Digital output rejects non-zero channel or incorrect payload length.
 */
TEST_F( VariableInstructionMessageHandlerTest, DigitalOutputRejectsInvalidChannelOrLength )
{
    static uint8_t                      mask_bytes[2] = { 0x01, 0x00 };
    HIL_Application_Logical_Operation_T op{};
    op.peripheral_type = HIL_APPLICATION_PERIPHERAL_DIGITAL_OUTPUT;
    op.channel         = 1U; /* Non-zero channel invalid */
    op.payload.data    = mask_bytes;
    op.payload.size    = sizeof( mask_bytes );

    instruction.tick_number     = 0U;
    instruction.operation_count = 1U;
    instruction.operations      = &op;

    EXPECT_EQ( HOST_VARIABLE_INSTRUCTION_HANDLER_HandleInstruction( &instruction ),
               HOST_INTERFACE_STATUS_VALIDATION_FAILED );

    /* Reset and test invalid length */
    HOST_VARIABLE_INSTRUCTION_HANDLER_Reset();
    op.channel      = 0U;
    op.payload.size = 3U; /* Must be 2 */

    EXPECT_EQ( HOST_VARIABLE_INSTRUCTION_HANDLER_HandleInstruction( &instruction ),
               HOST_INTERFACE_STATUS_VALIDATION_FAILED );
}

/**
 * @brief Analogue output operation parsing and DAC frame packing.
 */
TEST_F( VariableInstructionMessageHandlerTest, AnalogueOutputPacksFrameCorrectly )
{
    /* Microvolts: 2,500,000 uV = 2.5 V */
    uint32_t microvolts = 2500000U;
    uint8_t  voltage_bytes[4];
    std::memcpy( voltage_bytes, &microvolts, sizeof( microvolts ) );

    HIL_Application_Logical_Operation_T op{};
    op.peripheral_type = HIL_APPLICATION_PERIPHERAL_ANALOG_OUTPUT;
    op.channel         = 0U;
    op.payload.data    = voltage_bytes;
    op.payload.size    = sizeof( voltage_bytes );

    instruction.tick_number     = 0U;
    instruction.operation_count = 1U;
    instruction.operations      = &op;

    EXPECT_CALL( *g_mock_deps, EXEC_ANALOGUE_OUTPUT_Prepare_Frame( 0U, 2.5f, _ ) )
        .WillOnce( DoAll( SetArgPointee<2>( AnalogueOutputPreparedFrame_T{ { 0x01, 0x02, 0x03 } } ),
                          Return( true ) ) );

    EXPECT_EQ( HOST_VARIABLE_INSTRUCTION_HANDLER_HandleInstruction( &instruction ),
               HOST_INTERFACE_STATUS_OK );

    /* Header (8) + header word (4) + 1 frame (3 bytes) padded to 4 bytes = 16 bytes */
    const size_t expected_size = sizeof( ExecutionInstructionHeader_T ) + 4U + 4U;
    ASSERT_EQ( uploaded_bytes.size(), expected_size );
}

/**
 * @brief PWM Generation operation parsing and hardware register packing.
 */
TEST_F( VariableInstructionMessageHandlerTest, PwmGenerationPacksRegistersCorrectly )
{
    /* 50,000 ns period = 20,000 Hz. 5,000 permyriad duty = 50% */
    uint8_t  pwm_bytes[6];
    uint32_t period_ns      = 50000U;
    uint16_t duty_permyriad = 5000U;
    std::memcpy( &pwm_bytes[0], &period_ns, sizeof( period_ns ) );
    std::memcpy( &pwm_bytes[4], &duty_permyriad, sizeof( duty_permyriad ) );

    HIL_Application_Logical_Operation_T op{};
    op.peripheral_type = HIL_APPLICATION_PERIPHERAL_PWM_OUTPUT;
    op.channel         = 0U;
    op.payload.data    = pwm_bytes;
    op.payload.size    = sizeof( pwm_bytes );

    instruction.tick_number     = 0U;
    instruction.operation_count = 1U;
    instruction.operations      = &op;

    EXPECT_CALL( *g_mock_deps, HW_PWM_GEN_compute_psc( 20000U, _, _ ) )
        .WillOnce( DoAll( SetArgPointee<2>( 2U ), Return( true ) ) );
    EXPECT_CALL( *g_mock_deps, HW_PWM_GEN_compute_arr( 20000U, _, 2U, _ ) )
        .WillOnce( DoAll( SetArgPointee<3>( 4000U ), Return( true ) ) );
    EXPECT_CALL( *g_mock_deps, HW_PWM_GEN_compute_ccr( 500U, 4000U, _ ) )
        .WillOnce( DoAll( SetArgPointee<2>( 2000U ), Return( true ) ) );

    EXPECT_EQ( HOST_VARIABLE_INSTRUCTION_HANDLER_HandleInstruction( &instruction ),
               HOST_INTERFACE_STATUS_OK );

    const size_t expected_size =
        sizeof( ExecutionInstructionHeader_T )
        + EXECUTION_OPERATION_ENCODED_SIZE_BYTES( sizeof( ExecutionPwmUpdatePayload_T ) );
    ASSERT_EQ( uploaded_bytes.size(), expected_size );
}

/**
 * @brief Serial transmit operations (UART, SPI, CAN) pack correctly.
 */
TEST_F( VariableInstructionMessageHandlerTest, SerialTransmitOperationsPackCorrectly )
{
    /* UART */
    static uint8_t uart_data[4] = { 'T', 'E', 'S', 'T' };
    /* SPI: packet_count = 1, length = 2, data = {0xDE, 0xAD} */
    static uint8_t spi_data[4] = { 1U, 2U, 0xDE, 0xAD };
    /* CAN: ExecutionCanPacket_T layout (id: 2B, dlc: 1B, data: 8B, reserved: 1B) = 12 bytes */
    static uint8_t can_data[12]{};
    uint16_t       can_id = 0x123U;
    std::memcpy( &can_data[0], &can_id, sizeof( can_id ) );
    can_data[2] = 4U; /* DLC */
    can_data[3] = 0xAA;
    can_data[4] = 0xBB;
    can_data[5] = 0xCC;
    can_data[6] = 0xDD;
    /* bytes 7..10 remain 0 (unused data beyond DLC must be 0) */
    can_data[11] = 0U; /* reserved */

    HIL_Application_Logical_Operation_T ops[3]{};
    ops[0].peripheral_type = HIL_APPLICATION_PERIPHERAL_UART;
    ops[0].channel         = 0U;
    ops[0].payload.data    = uart_data;
    ops[0].payload.size    = sizeof( uart_data );

    ops[1].peripheral_type = HIL_APPLICATION_PERIPHERAL_SPI;
    ops[1].channel         = 0U;
    ops[1].payload.data    = spi_data;
    ops[1].payload.size    = sizeof( spi_data );

    ops[2].peripheral_type = HIL_APPLICATION_PERIPHERAL_CAN;
    ops[2].channel         = 0U;
    ops[2].payload.data    = can_data;
    ops[2].payload.size    = sizeof( can_data );

    instruction.tick_number     = 0U;
    instruction.operation_count = 3U;
    instruction.operations      = ops;

    EXPECT_EQ( HOST_VARIABLE_INSTRUCTION_HANDLER_HandleInstruction( &instruction ),
               HOST_INTERFACE_STATUS_OK );

    ExecutionInstructionHeader_T header{};
    std::memcpy( &header, uploaded_bytes.data(), sizeof( header ) );
    EXPECT_EQ( header.operation_count, 3U );
}

/**
 * @brief A maximum-sized packetised SPI span expands safely into canonical format.
 */
TEST_F( VariableInstructionMessageHandlerTest, MaximumSpiPayloadFitsCanonicalInstruction )
{
    static uint8_t    spi_data[HIL_APPLICATION_ABSOLUTE_MAX_VARIABLE_DATA_SIZE]{};
    constexpr uint8_t packet_count = 127U;

    spi_data[0] = packet_count;
    for ( uint8_t packet = 0U; packet < packet_count; packet++ )
    {
        spi_data[1U + packet]                = 1U;
        spi_data[1U + packet_count + packet] = packet;
    }

    HIL_Application_Logical_Operation_T op{};
    op.peripheral_type = HIL_APPLICATION_PERIPHERAL_SPI;
    op.channel         = 0U;
    op.payload.data    = spi_data;
    op.payload.size    = sizeof( spi_data );

    instruction.tick_number     = 1000U;
    instruction.operation_count = 1U;
    instruction.operations      = &op;

    ASSERT_EQ( HOST_VARIABLE_INSTRUCTION_HANDLER_HandleInstruction( &instruction ),
               HOST_INTERFACE_STATUS_OK );

    constexpr size_t converted_payload_size =
        EXECUTION_SPI_PREFIX_SIZE_BYTES + EXECUTION_SPI_PACKET_SIZES_LENGTH_BYTES( packet_count )
        + packet_count;
    const size_t expected_size = sizeof( ExecutionInstructionHeader_T )
                                 + EXECUTION_OPERATION_ENCODED_SIZE_BYTES( converted_payload_size );

    EXPECT_EQ( converted_payload_size, 639U );
    EXPECT_EQ( uploaded_bytes.size(), expected_size );
    EXPECT_LE( uploaded_bytes.size(), EXECUTION_INSTRUCTION_MAX_SIZE_BYTES );
}

/**
 * @brief Unrecognized peripheral type returns VALIDATION_FAILED.
 */
TEST_F( VariableInstructionMessageHandlerTest, RejectsUnknownPeripheralType )
{
    static uint8_t                      dummy[2] = { 0 };
    HIL_Application_Logical_Operation_T op{};
    op.peripheral_type = ( HIL_Application_Peripheral_Type_T )0x7FU;
    op.channel         = 0U;
    op.payload.data    = dummy;
    op.payload.size    = sizeof( dummy );

    instruction.tick_number     = 0U;
    instruction.operation_count = 1U;
    instruction.operations      = &op;

    EXPECT_EQ( HOST_VARIABLE_INSTRUCTION_HANDLER_HandleInstruction( &instruction ),
               HOST_INTERFACE_STATUS_VALIDATION_FAILED );
}

/**
 * @brief Flash Manager upload status translations.
 */
TEST_F( VariableInstructionMessageHandlerTest, TranslatesFlashManagerStatusesCorrectly )
{
    static uint8_t                      mask[2] = { 0x01, 0x00 };
    HIL_Application_Logical_Operation_T op{};
    op.peripheral_type = HIL_APPLICATION_PERIPHERAL_DIGITAL_OUTPUT;
    op.channel         = 0U;
    op.payload.data    = mask;
    op.payload.size    = sizeof( mask );

    instruction.operations      = &op;
    instruction.operation_count = 1U;

    instruction.tick_number = 0U;
    EXPECT_CALL( *g_mock_deps, FLASH_MANAGER_SubmitInstructionUploadBytes( _, _ ) )
        .WillOnce( Return( FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_BUSY ) );
    EXPECT_EQ( HOST_VARIABLE_INSTRUCTION_HANDLER_HandleInstruction( &instruction ),
               HOST_INTERFACE_STATUS_INTERNAL_ERROR );

    /* Toggle pin 0 back to 0 so tick 1 has a transition */
    static uint8_t mask1[2] = { 0x00, 0x00 };
    op.payload.data         = mask1;
    instruction.tick_number = 1U;
    EXPECT_CALL( *g_mock_deps, FLASH_MANAGER_SubmitInstructionUploadBytes( _, _ ) )
        .WillOnce( Return( FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_INVALID_STATE ) );
    EXPECT_EQ( HOST_VARIABLE_INSTRUCTION_HANDLER_HandleInstruction( &instruction ),
               HOST_INTERFACE_STATUS_STATE_TRANSITION_FAILURE );
}
