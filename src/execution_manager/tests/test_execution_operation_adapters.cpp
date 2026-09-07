#include <gtest/gtest.h>

extern "C"
{
#include "execution_operation_adapters.h"
#include "execution_operation_payloads.h"
}

static uint32_t set_masks[2];
static uint32_t reset_masks[2];
static uint32_t set_calls;
static uint32_t reset_calls;

extern "C" void EXEC_DIGITAL_OUTPUT_Set_Output( uint32_t pin_mask )
{
    set_masks[set_calls++] = pin_mask;
}

extern "C" void EXEC_DIGITAL_OUTPUT_Reset_Output( uint32_t pin_mask )
{
    reset_masks[reset_calls++] = pin_mask;
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
    alignas( 4 ) uint32_t operation[] = {
        EXECUTION_OPERATION_OPCODE_DIGITAL_OUTPUT_UPDATE
            | ( EXECUTION_DIGITAL_OUTPUT_PAYLOAD_SIZE_BYTES << 16U ),
        UINT32_C( 0x00000020 ),
        UINT32_C( 0x00000040 ),
    };

    EXPECT_EQ( EXECUTION_OPERATION_ADAPTER_ApplyOperations(
                   reinterpret_cast<const uint8_t*>( operation ), 1U ),
               EXECUTION_OPERATION_ADAPTER_ACCEPTED );
    ASSERT_EQ( set_calls, 1U );
    ASSERT_EQ( reset_calls, 1U );
    EXPECT_EQ( set_masks[0], UINT32_C( 0x00000020 ) );
    EXPECT_EQ( reset_masks[0], UINT32_C( 0x00000040 ) );
}

TEST_F( ExecutionOperationAdaptersTest, WalkerAdvancesToNextEncodedOperation )
{
    alignas( 4 ) uint32_t operations[] = {
        EXECUTION_OPERATION_OPCODE_DIGITAL_OUTPUT_UPDATE
            | ( EXECUTION_DIGITAL_OUTPUT_PAYLOAD_SIZE_BYTES << 16U ),
        UINT32_C( 0x00000001 ),
        UINT32_C( 0x00000002 ),
        EXECUTION_OPERATION_OPCODE_DIGITAL_OUTPUT_UPDATE
            | ( EXECUTION_DIGITAL_OUTPUT_PAYLOAD_SIZE_BYTES << 16U ),
        UINT32_C( 0x00000004 ),
        UINT32_C( 0x00000008 ),
    };

    EXPECT_EQ( EXECUTION_OPERATION_ADAPTER_ApplyOperations(
                   reinterpret_cast<const uint8_t*>( operations ), 2U ),
               EXECUTION_OPERATION_ADAPTER_ACCEPTED );
    ASSERT_EQ( set_calls, 2U );
    ASSERT_EQ( reset_calls, 2U );
    EXPECT_EQ( set_masks[1], UINT32_C( 0x00000004 ) );
    EXPECT_EQ( reset_masks[1], UINT32_C( 0x00000008 ) );
}
