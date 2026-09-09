#include <gtest/gtest.h>
#include <cstring>

extern "C"
{
#include "execution_operation_adapters.h"
#include "execution_operation_payloads.h"
#include "exec_digital_output.h"
#include "exec_pwm_gen.h"
#include "exec_spi.h"
#include "exec_uart.h"
#include "exec_analogue_output.h"
#include "exec_can.h"
}

static uint32_t                    set_masks[2];
static uint32_t                    reset_masks[2];
static uint32_t                    set_calls;
static uint32_t                    reset_calls;
static uint32_t                    pwm_lv_calls;
static uint32_t                    pwm_hv_calls;
static ExecutionPwmUpdatePayload_T last_pwm_lv;
static ExecutionPwmUpdatePayload_T last_pwm_hv;
static uint32_t                    spi_calls;
static ExecSPIChannel_T            last_spi_channel;
static const uint8_t*              last_spi_data;
static const uint32_t*             last_spi_packet_sizes;
static uint32_t                    last_spi_packet_count;
static bool                        spi_accept;
static uint32_t                    uart_calls;
static ExecUartChannel_T           last_uart_channel;
static uint8_t                     last_uart_payload[8];
static uint32_t                    last_uart_length;
static bool                        uart_accept;
static uint32_t                    analogue_output_calls;
static const uint8_t*              last_analogue_output_payload;
static uint32_t                    last_analogue_output_length;
static bool                        analogue_output_accept;
static uint32_t                    can_calls;
static EXEC_CAN_Channel_T          last_can_channel;
static const EXEC_CAN_Packet_T*    last_can_packets;
static uint16_t                    last_can_packet_count;
static EXEC_CAN_Result_T           can_result;

struct alignas( 4 ) EncodedDigitalOutputOperation
{
    ExecutionOperationHeaderWord_T  header;
    ExecutionDigitalOutputPayload_T payload;
};

struct alignas( 4 ) EncodedPwmOperation
{
    ExecutionOperationHeaderWord_T header;
    ExecutionPwmUpdatePayload_T    payload;
    uint8_t                        padding[2];
};

static_assert( sizeof( EncodedDigitalOutputOperation ) == 12U );
static_assert( sizeof( EncodedPwmOperation ) == 12U );

struct alignas( 4 ) EncodedSpiOperation
{
    ExecutionOperationHeaderWord_T      header;
    ExecutionSpiTransmitPayloadPrefix_T prefix;
    uint32_t                            packet_sizes[2];
    uint8_t                             data[5];
    uint8_t                             padding[3];
};

static_assert( sizeof( EncodedSpiOperation ) == 24U );
static constexpr uint16_t SPI_TEST_PAYLOAD_LENGTH_BYTES =
    EXECUTION_SPI_DATA_OFFSET_BYTES( 2U ) + 5U;

struct alignas( 4 ) EncodedUartOperation
{
    ExecutionOperationHeaderWord_T header;
    uint8_t                        payload[5];
    uint8_t                        padding[3];
};

static_assert( sizeof( EncodedUartOperation ) == 12U );

struct alignas( 4 ) EncodedAnalogueOutputOperation
{
    ExecutionOperationHeaderWord_T header;
    uint8_t                        payload[6];
    uint8_t                        padding[2];
};

static_assert( sizeof( EncodedAnalogueOutputOperation ) == 12U );

struct alignas( 4 ) EncodedCanOperation
{
    ExecutionOperationHeaderWord_T header;
    ExecutionCanPacket_T           packets[2];
};

static_assert( sizeof( EncodedCanOperation ) == 28U );

extern "C" void EXEC_DIGITAL_OUTPUT_Set_Output( uint32_t pin_mask )
{
    set_masks[set_calls++] = pin_mask;
}

extern "C" void EXEC_DIGITAL_OUTPUT_Reset_Output( uint32_t pin_mask )
{
    reset_masks[reset_calls++] = pin_mask;
}

extern "C" void EXEC_PWM_GEN_Set_PWM_LV( uint16_t arr, uint16_t ccr, uint16_t psc )
{
    pwm_lv_calls++;
    last_pwm_lv = { arr, ccr, psc };
}

extern "C" void EXEC_PWM_GEN_Set_PWM_HV( uint16_t arr, uint16_t ccr, uint16_t psc )
{
    pwm_hv_calls++;
    last_pwm_hv = { arr, ccr, psc };
}

extern "C" bool EXEC_SPI_Transmit( ExecSPIChannel_T channel, const uint8_t* data_src,
                                   const uint32_t* packet_sizes_bytes, uint32_t num_packets )
{
    spi_calls             = spi_calls + 1U;
    last_spi_channel      = channel;
    last_spi_data         = data_src;
    last_spi_packet_sizes = packet_sizes_bytes;
    last_spi_packet_count = num_packets;
    return spi_accept;
}

extern "C" bool EXEC_UART_Transmit( ExecUartChannel_T channel, const uint8_t* data,
                                    uint32_t length_bytes )
{
    uart_calls++;
    last_uart_channel = channel;
    last_uart_length  = length_bytes;
    if ( length_bytes <= sizeof( last_uart_payload ) )
    {
        std::memcpy( last_uart_payload, data, length_bytes );
    }
    return uart_accept;
}

extern "C" bool EXEC_ANALOGUE_OUTPUT_Submit_Prepared_Batch( const uint8_t* payload,
                                                            uint32_t       byte_count )
{
    analogue_output_calls++;
    last_analogue_output_payload = payload;
    last_analogue_output_length  = byte_count;
    return analogue_output_accept;
}

extern "C" EXEC_CAN_Result_T EXEC_CAN_Transmit( EXEC_CAN_Channel_T       channel,
                                                const EXEC_CAN_Packet_T* packets,
                                                uint16_t                 packet_count )
{
    can_calls             = can_calls + 1U;
    last_can_channel      = channel;
    last_can_packets      = packets;
    last_can_packet_count = packet_count;
    return can_result;
}

class ExecutionOperationAdaptersTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        set_calls    = 0U;
        reset_calls  = 0U;
        set_masks[0] = set_masks[1] = 0U;
        reset_masks[0] = reset_masks[1] = 0U;
        pwm_lv_calls = pwm_hv_calls = 0U;
        last_pwm_lv                 = { 0U, 0U, 0U };
        last_pwm_hv                 = { 0U, 0U, 0U };
        spi_calls                   = 0U;
        last_spi_channel            = EXEC_SPI_CHANNEL_1;
        last_spi_data               = nullptr;
        last_spi_packet_sizes       = nullptr;
        last_spi_packet_count       = 0U;
        spi_accept                  = true;
        uart_calls                  = 0U;
        last_uart_channel           = EXEC_UART_CHANNEL_1;
        std::memset( last_uart_payload, 0, sizeof( last_uart_payload ) );
        last_uart_length             = 0U;
        uart_accept                  = true;
        analogue_output_calls        = 0U;
        last_analogue_output_payload = nullptr;
        last_analogue_output_length  = 0U;
        analogue_output_accept       = true;
        can_calls                    = 0U;
        last_can_channel             = EXEC_CAN_CHANNEL_1;
        last_can_packets             = nullptr;
        last_can_packet_count        = 0U;
        can_result                   = EXEC_CAN_RESULT_OK;
        EXECUTION_OPERATION_ADAPTER_ResetFailure();
    }
};

TEST_F( ExecutionOperationAdaptersTest, EmptyOperationListDoesNothing )
{
    alignas( 4 ) uint32_t unused = 0U;

    EXPECT_EQ( EXECUTION_OPERATION_ADAPTER_ApplyOperations(
                   reinterpret_cast<const uint8_t*>( &unused ), 0U ),
               EXECUTION_OPERATION_ADAPTER_ACCEPTED );
    EXPECT_EQ( set_calls, 0U );
    EXPECT_EQ( reset_calls, 0U );
}

TEST_F( ExecutionOperationAdaptersTest, DigitalOutputUsesAlignedPayloadWithoutCopy )
{
    const EncodedDigitalOutputOperation operation = {
        EXECUTION_OPERATION_OPCODE_DIGITAL_OUTPUT_UPDATE
            | ( EXECUTION_DIGITAL_OUTPUT_PAYLOAD_SIZE_BYTES << 16U ),
        { UINT32_C( 0x00000020 ), UINT32_C( 0x00000040 ) },
    };

    EXPECT_EQ( EXECUTION_OPERATION_ADAPTER_ApplyOperations(
                   reinterpret_cast<const uint8_t*>( &operation ), 1U ),
               EXECUTION_OPERATION_ADAPTER_ACCEPTED );
    ASSERT_EQ( set_calls, 1U );
    ASSERT_EQ( reset_calls, 1U );
    EXPECT_EQ( set_masks[0], UINT32_C( 0x00000020 ) );
    EXPECT_EQ( reset_masks[0], UINT32_C( 0x00000040 ) );
}

TEST_F( ExecutionOperationAdaptersTest, WalkerAdvancesToNextEncodedOperation )
{
    const EncodedDigitalOutputOperation operations[] = {
        {
            EXECUTION_OPERATION_OPCODE_DIGITAL_OUTPUT_UPDATE
                | ( EXECUTION_DIGITAL_OUTPUT_PAYLOAD_SIZE_BYTES << 16U ),
            { UINT32_C( 0x00000001 ), UINT32_C( 0x00000002 ) },
        },
        {
            EXECUTION_OPERATION_OPCODE_DIGITAL_OUTPUT_UPDATE
                | ( EXECUTION_DIGITAL_OUTPUT_PAYLOAD_SIZE_BYTES << 16U ),
            { UINT32_C( 0x00000004 ), UINT32_C( 0x00000008 ) },
        },
    };

    EXPECT_EQ( EXECUTION_OPERATION_ADAPTER_ApplyOperations(
                   reinterpret_cast<const uint8_t*>( operations ), 2U ),
               EXECUTION_OPERATION_ADAPTER_ACCEPTED );
    ASSERT_EQ( set_calls, 2U );
    ASSERT_EQ( reset_calls, 2U );
    EXPECT_EQ( set_masks[1], UINT32_C( 0x00000004 ) );
    EXPECT_EQ( reset_masks[1], UINT32_C( 0x00000008 ) );
}

TEST_F( ExecutionOperationAdaptersTest, PwmUpdateDispatchesPreparedValuesToSelectedChannel )
{
    const EncodedPwmOperation operations[] = {
        {
            EXECUTION_OPERATION_OPCODE_PWM_UPDATE | ( EXECUTION_OPERATION_PWM_CHANNEL_LV << 8U )
                | ( EXECUTION_PWM_UPDATE_PAYLOAD_SIZE_BYTES << 16U ),
            { 1000U, 500U, 4U },
            { 0U, 0U },
        },
        {
            EXECUTION_OPERATION_OPCODE_PWM_UPDATE | ( EXECUTION_OPERATION_PWM_CHANNEL_HV << 8U )
                | ( EXECUTION_PWM_UPDATE_PAYLOAD_SIZE_BYTES << 16U ),
            { 2000U, 1500U, 8U },
            { 0U, 0U },
        },
    };

    EXPECT_EQ( EXECUTION_OPERATION_ADAPTER_ApplyOperations(
                   reinterpret_cast<const uint8_t*>( operations ), 2U ),
               EXECUTION_OPERATION_ADAPTER_ACCEPTED );
    EXPECT_EQ( pwm_lv_calls, 1U );
    EXPECT_EQ( pwm_hv_calls, 1U );
    EXPECT_EQ( last_pwm_lv.arr, 1000U );
    EXPECT_EQ( last_pwm_lv.ccr, 500U );
    EXPECT_EQ( last_pwm_lv.psc, 4U );
    EXPECT_EQ( last_pwm_hv.arr, 2000U );
    EXPECT_EQ( last_pwm_hv.ccr, 1500U );
    EXPECT_EQ( last_pwm_hv.psc, 8U );
}

TEST_F( ExecutionOperationAdaptersTest, WalkerSkipsPwmAlignmentPaddingBeforeNextOperation )
{
    struct alignas( 4 )
    {
        EncodedPwmOperation           pwm;
        EncodedDigitalOutputOperation digital_output;
    } operations = {
        {
            EXECUTION_OPERATION_OPCODE_PWM_UPDATE | ( EXECUTION_OPERATION_PWM_CHANNEL_LV << 8U )
                | ( EXECUTION_PWM_UPDATE_PAYLOAD_SIZE_BYTES << 16U ),
            { 999U, 250U, 3U },
            { 0U, 0U },
        },
        {
            EXECUTION_OPERATION_OPCODE_DIGITAL_OUTPUT_UPDATE
                | ( EXECUTION_DIGITAL_OUTPUT_PAYLOAD_SIZE_BYTES << 16U ),
            { UINT32_C( 0x10 ), UINT32_C( 0x20 ) },
        },
    };

    EXPECT_EQ( EXECUTION_OPERATION_ADAPTER_ApplyOperations(
                   reinterpret_cast<const uint8_t*>( &operations ), 2U ),
               EXECUTION_OPERATION_ADAPTER_ACCEPTED );
    EXPECT_EQ( pwm_lv_calls, 1U );
    EXPECT_EQ( set_masks[0], UINT32_C( 0x10 ) );
    EXPECT_EQ( reset_masks[0], UINT32_C( 0x20 ) );
}

TEST_F( ExecutionOperationAdaptersTest, SpiTransmitPassesAlignedPayloadViewsWithoutCopy )
{
    const EncodedSpiOperation operation = {
        EXECUTION_OPERATION_OPCODE_SPI_TRANSMIT | ( EXECUTION_OPERATION_SPI_CHANNEL_2 << 8U )
            | ( SPI_TEST_PAYLOAD_LENGTH_BYTES << 16U ),
        { 2U },
        { 2U, 3U },
        { 0x10U, 0x11U, 0x20U, 0x21U, 0x22U },
        { 0U, 0U, 0U },
    };

    EXPECT_EQ( EXECUTION_OPERATION_ADAPTER_ApplyOperations(
                   reinterpret_cast<const uint8_t*>( &operation ), 1U ),
               EXECUTION_OPERATION_ADAPTER_ACCEPTED );
    EXPECT_EQ( spi_calls, 1U );
    EXPECT_EQ( last_spi_channel, EXEC_SPI_CHANNEL_2 );
    EXPECT_EQ( last_spi_packet_count, 2U );
    EXPECT_EQ( last_spi_packet_sizes, operation.packet_sizes );
    EXPECT_EQ( last_spi_data, operation.data );
}

TEST_F( ExecutionOperationAdaptersTest, SpiDriverRejectionPropagatesToOperationWalker )
{
    const EncodedSpiOperation operation = {
        EXECUTION_OPERATION_OPCODE_SPI_TRANSMIT | ( EXECUTION_OPERATION_SPI_CHANNEL_1 << 8U )
            | ( SPI_TEST_PAYLOAD_LENGTH_BYTES << 16U ),
        { 2U },
        { 2U, 3U },
        { 1U, 2U, 3U, 4U, 5U },
        { 0U, 0U, 0U },
    };
    spi_accept = false;

    EXPECT_EQ( EXECUTION_OPERATION_ADAPTER_ApplyOperations(
                   reinterpret_cast<const uint8_t*>( &operation ), 1U ),
               EXECUTION_OPERATION_ADAPTER_REJECTED );
    EXPECT_EQ( spi_calls, 1U );
}

TEST_F( ExecutionOperationAdaptersTest, WalkerSkipsSpiAlignmentPaddingBeforeNextOperation )
{
    struct alignas( 4 )
    {
        EncodedSpiOperation           spi;
        EncodedDigitalOutputOperation digital_output;
    } operations = {
        {
            EXECUTION_OPERATION_OPCODE_SPI_TRANSMIT | ( EXECUTION_OPERATION_SPI_CHANNEL_1 << 8U )
                | ( SPI_TEST_PAYLOAD_LENGTH_BYTES << 16U ),
            { 2U },
            { 2U, 3U },
            { 1U, 2U, 3U, 4U, 5U },
            { 0U, 0U, 0U },
        },
        {
            EXECUTION_OPERATION_OPCODE_DIGITAL_OUTPUT_UPDATE
                | ( EXECUTION_DIGITAL_OUTPUT_PAYLOAD_SIZE_BYTES << 16U ),
            { UINT32_C( 0x40 ), UINT32_C( 0x80 ) },
        },
    };

    EXPECT_EQ( EXECUTION_OPERATION_ADAPTER_ApplyOperations(
                   reinterpret_cast<const uint8_t*>( &operations ), 2U ),
               EXECUTION_OPERATION_ADAPTER_ACCEPTED );
    EXPECT_EQ( spi_calls, 1U );
    EXPECT_EQ( set_masks[0], UINT32_C( 0x40 ) );
    EXPECT_EQ( reset_masks[0], UINT32_C( 0x80 ) );
}

TEST_F( ExecutionOperationAdaptersTest, UartTransmitPassesRawPayloadAndLengthToDriver )
{
    const EncodedUartOperation operation = {
        EXECUTION_OPERATION_OPCODE_UART_TRANSMIT | ( EXECUTION_OPERATION_UART_CHANNEL_2 << 8U )
            | ( 5U << 16U ),
        { 0x11U, 0x22U, 0x33U, 0x44U, 0x55U },
        { 0U, 0U, 0U },
    };

    EXPECT_EQ( EXECUTION_OPERATION_ADAPTER_ApplyOperations(
                   reinterpret_cast<const uint8_t*>( &operation ), 1U ),
               EXECUTION_OPERATION_ADAPTER_ACCEPTED );
    EXPECT_EQ( uart_calls, 1U );
    EXPECT_EQ( last_uart_channel, EXEC_UART_CHANNEL_2 );
    EXPECT_EQ( last_uart_length, 5U );
    EXPECT_EQ( std::memcmp( last_uart_payload, operation.payload, 5U ), 0 );
}

TEST_F( ExecutionOperationAdaptersTest, UartDriverRejectionPropagatesToOperationWalker )
{
    const EncodedUartOperation operation = {
        EXECUTION_OPERATION_OPCODE_UART_TRANSMIT | ( EXECUTION_OPERATION_UART_CHANNEL_1 << 8U )
            | ( 5U << 16U ),
        { 1U, 2U, 3U, 4U, 5U },
        { 0U, 0U, 0U },
    };
    uart_accept = false;

    EXPECT_EQ( EXECUTION_OPERATION_ADAPTER_ApplyOperations(
                   reinterpret_cast<const uint8_t*>( &operation ), 1U ),
               EXECUTION_OPERATION_ADAPTER_REJECTED );
    EXPECT_EQ( uart_calls, 1U );

    ExecutionOperationAdapterFailure_T failure = {};
    ASSERT_TRUE( EXECUTION_OPERATION_ADAPTER_GetFailure( &failure ) );
    EXPECT_EQ( failure.operation_index, 0U );
    EXPECT_EQ( failure.opcode, EXECUTION_OPERATION_OPCODE_UART_TRANSMIT );
    EXPECT_EQ( failure.channel, EXECUTION_OPERATION_UART_CHANNEL_1 );
}

TEST_F( ExecutionOperationAdaptersTest, AnalogueOutputPassesPreparedPayloadAndLengthWithoutCopy )
{
    const EncodedAnalogueOutputOperation operation = {
        EXECUTION_OPERATION_OPCODE_ANALOGUE_OUTPUT_BATCH
            | ( EXECUTION_OPERATION_CHANNEL_UNUSED << 8U ) | ( 6U << 16U ),
        { 0x00U, 0x00U, 0x00U, 0x28U, 0x0FU, 0xFFU },
        { 0U, 0U },
    };

    EXPECT_EQ( EXECUTION_OPERATION_ADAPTER_ApplyOperations(
                   reinterpret_cast<const uint8_t*>( &operation ), 1U ),
               EXECUTION_OPERATION_ADAPTER_ACCEPTED );
    EXPECT_EQ( analogue_output_calls, 1U );
    EXPECT_EQ( last_analogue_output_payload, operation.payload );
    EXPECT_EQ( last_analogue_output_length, 6U );
}

TEST_F( ExecutionOperationAdaptersTest, AnalogueOutputRejectionPropagatesToOperationWalker )
{
    const EncodedAnalogueOutputOperation operation = {
        EXECUTION_OPERATION_OPCODE_ANALOGUE_OUTPUT_BATCH
            | ( EXECUTION_OPERATION_CHANNEL_UNUSED << 8U ) | ( 3U << 16U ),
        { 0x18U, 0x09U, 0xDFU, 0x00U, 0x00U, 0x00U },
        { 0U, 0U },
    };
    analogue_output_accept = false;

    EXPECT_EQ( EXECUTION_OPERATION_ADAPTER_ApplyOperations(
                   reinterpret_cast<const uint8_t*>( &operation ), 1U ),
               EXECUTION_OPERATION_ADAPTER_REJECTED );
    EXPECT_EQ( analogue_output_calls, 1U );
    EXPECT_EQ( last_analogue_output_payload, operation.payload );
    EXPECT_EQ( last_analogue_output_length, 3U );
}

TEST_F( ExecutionOperationAdaptersTest, CanTransmitPassesPacketBatchWithoutAdapterCopy )
{
    const EncodedCanOperation operation = {
        EXECUTION_OPERATION_OPCODE_CAN_TRANSMIT | ( EXECUTION_OPERATION_CAN_CHANNEL_2 << 8U )
            | ( ( 2U * EXECUTION_CAN_PACKET_SIZE_BYTES ) << 16U ),
        {
            { 0x123U, 2U, { 0x10U, 0x20U, 0U, 0U, 0U, 0U, 0U, 0U }, 0U },
            { 0x456U, 8U, { 1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U }, 0U },
        },
    };

    EXPECT_EQ( EXECUTION_OPERATION_ADAPTER_ApplyOperations(
                   reinterpret_cast<const uint8_t*>( &operation ), 1U ),
               EXECUTION_OPERATION_ADAPTER_ACCEPTED );
    EXPECT_EQ( can_calls, 1U );
    EXPECT_EQ( last_can_channel, EXEC_CAN_CHANNEL_2 );
    EXPECT_EQ( last_can_packet_count, 2U );
    EXPECT_EQ( last_can_packets, reinterpret_cast<const EXEC_CAN_Packet_T*>( operation.packets ) );
}

TEST_F( ExecutionOperationAdaptersTest, CanTransmitRejectionPropagatesToOperationWalker )
{
    const EncodedCanOperation operation = {
        EXECUTION_OPERATION_OPCODE_CAN_TRANSMIT | ( EXECUTION_OPERATION_CAN_CHANNEL_1 << 8U )
            | ( EXECUTION_CAN_PACKET_SIZE_BYTES << 16U ),
        {
            { 0x321U, 1U, { 0xAAU, 0U, 0U, 0U, 0U, 0U, 0U, 0U }, 0U },
            { 0U, 0U, { 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U }, 0U },
        },
    };
    can_result = EXEC_CAN_RESULT_BUSY;

    EXPECT_EQ( EXECUTION_OPERATION_ADAPTER_ApplyOperations(
                   reinterpret_cast<const uint8_t*>( &operation ), 1U ),
               EXECUTION_OPERATION_ADAPTER_REJECTED );
    EXPECT_EQ( can_calls, 1U );
    EXPECT_EQ( last_can_packet_count, 1U );
}
