#include <gtest/gtest.h>
#include <cstring>

extern "C"
{
#include "execution_operation_adapters.h"
#include "execution_operation_payloads.h"
#include "exec_digital_output.h"
#include "exec_pwm_gen.h"
#include "exec_uart.h"
}

static uint32_t                    set_masks[2];
static uint32_t                    reset_masks[2];
static uint32_t                    set_calls;
static uint32_t                    reset_calls;
static uint32_t                    pwm_lv_calls;
static uint32_t                    pwm_hv_calls;
static ExecutionPwmUpdatePayload_T last_pwm_lv;
static ExecutionPwmUpdatePayload_T last_pwm_hv;
static uint32_t                    uart_calls;
static ExecUartChannel_T           last_uart_channel;
static uint8_t                     last_uart_payload[8];
static uint32_t                    last_uart_length;
static bool                        uart_accept;

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

struct alignas( 4 ) EncodedUartOperation
{
    ExecutionOperationHeaderWord_T header;
    uint8_t                        payload[5];
    uint8_t                        padding[3];
};

static_assert( sizeof( EncodedUartOperation ) == 12U );

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
        uart_calls                  = 0U;
        last_uart_channel           = EXEC_UART_CHANNEL_1;
        std::memset( last_uart_payload, 0, sizeof( last_uart_payload ) );
        last_uart_length = 0U;
        uart_accept      = true;
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
}
