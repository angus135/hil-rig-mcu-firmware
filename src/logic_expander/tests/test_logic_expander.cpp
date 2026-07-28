/******************************************************************************
 *  Unit tests for transaction-queued MCP23017 logic-expander behavior.
 ******************************************************************************/

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <array>
#include <cstring>

extern "C"
{
#include "logic_expander.h"
#include "../../hardware_low_level/hw_i2c/hw_i2c.h"
#include <stdbool.h>
#include <stdint.h>

#include "../logic_expander.c" /* Module under test */  // NOLINT
}

class MockHWI2C
{
public:
    MOCK_METHOD( HWI2CStatus_T, ConfigureInternal, ( uint16_t own_address_7bit ), () );
    MOCK_METHOD( HWI2CStatus_T, EnqueueMasterTransmit,
                 ( HWI2CChannel_T channel, uint16_t device_address_7bit, const uint8_t* payload,
                   uint16_t payload_length ),
                 () );
    MOCK_METHOD( HWI2CStatus_T, EnqueueMasterReceive,
                 ( HWI2CChannel_T channel, uint16_t device_address_7bit, uint16_t expected_length ),
                 () );
    MOCK_METHOD( void, ServiceTransactionQueue, ( HWI2CChannel_T channel ), () );
    MOCK_METHOD( bool, IsTransactionQueueComplete, ( HWI2CChannel_T channel ), () );
    MOCK_METHOD( HWI2CStatus_T, GetAndClearTransferResult, ( HWI2CChannel_T channel ), () );
};

static MockHWI2C* g_mock_hw_i2c = nullptr;

extern "C"
{
HWI2CStatus_T HW_I2C_Configure_Internal_FMPI2C1( uint16_t own_address_7bit )
{
    return g_mock_hw_i2c->ConfigureInternal( own_address_7bit );
}

HWI2CStatus_T HW_I2C_Enqueue_Master_Transmit( HWI2CChannel_T channel, uint16_t device_address_7bit,
                                              const uint8_t* payload, uint16_t payload_length )
{
    return g_mock_hw_i2c->EnqueueMasterTransmit( channel, device_address_7bit, payload,
                                                 payload_length );
}

HWI2CStatus_T HW_I2C_Enqueue_Master_Receive( HWI2CChannel_T channel, uint16_t device_address_7bit,
                                             uint16_t expected_length )
{
    return g_mock_hw_i2c->EnqueueMasterReceive( channel, device_address_7bit, expected_length );
}

void HW_I2C_Service_Transaction_Queue( HWI2CChannel_T channel )
{
    g_mock_hw_i2c->ServiceTransactionQueue( channel );
}

bool HW_I2C_Is_Transaction_Queue_Complete( HWI2CChannel_T channel )
{
    return g_mock_hw_i2c->IsTransactionQueueComplete( channel );
}

HWI2CStatus_T HW_I2C_Get_And_Clear_Transfer_Result( HWI2CChannel_T channel )
{
    return g_mock_hw_i2c->GetAndClearTransferResult( channel );
}
}

using ::testing::_;
using ::testing::InSequence;
using ::testing::Return;
using ::testing::StrictMock;

class LogicExpanderTest : public ::testing::Test
{
protected:
    StrictMock<MockHWI2C> mock_hw_i2c;

    void SetUp( void ) override
    {
        g_mock_hw_i2c = &mock_hw_i2c;
        std::memset( logic_expander_state, 0, sizeof( logic_expander_state ) );
        logic_expander_ready          = false;
        logic_expander_active_bitmask = LOGIC_EXPANDER_DEFAULT_ACTIVE_BITMASK;
        logic_expander_dirty_bitmask  = 0U;
        logic_expander_config_state   = LOGIC_EXPANDER_CONFIG_NOT_STARTED;
        logic_expander_config_index   = 0U;
        logic_expander_config_write   = 0U;
    }

    void TearDown( void ) override
    {
        g_mock_hw_i2c = nullptr;
    }

    void ExpectEightConfigurationWrites( void )
    {
        EXPECT_CALL( mock_hw_i2c, EnqueueMasterTransmit( HW_I2C_CHANNEL_FMPI2C1, 0x20U, _, _ ) )
            .Times( 8 )
            .WillRepeatedly( Return( HW_I2C_STATUS_OK ) );
    }
};

TEST_F( LogicExpanderTest, FunctionalIndexValuesMatchAddressTableIndices )
{
    EXPECT_EQ( LOGIC_EXPANDER_DIGITAL_OUTPUT_SELECT, 0 );
    EXPECT_EQ( LOGIC_EXPANDER_UNASSIGNED_7, 7 );
    EXPECT_EQ( LOGIC_EXPANDER_COUNT, 8 );
    EXPECT_EQ( LOGIC_EXPANDER_I2C_ADDRESSES[LOGIC_EXPANDER_DIGITAL_OUTPUT_SELECT], 0x20U );
}

TEST_F( LogicExpanderTest, SelfConfigWaitsForPhysicalCompletionBeforeReady )
{
    EXPECT_CALL( mock_hw_i2c, ConfigureInternal( 0x33U ) ).WillOnce( Return( HW_I2C_STATUS_OK ) );
    EXPECT_CALL( mock_hw_i2c, ServiceTransactionQueue( HW_I2C_CHANNEL_FMPI2C1 ) ).Times( 2 );
    ExpectEightConfigurationWrites();
    EXPECT_CALL( mock_hw_i2c, IsTransactionQueueComplete( HW_I2C_CHANNEL_FMPI2C1 ) )
        .WillOnce( Return( false ) )
        .WillOnce( Return( true ) );
    EXPECT_CALL( mock_hw_i2c, GetAndClearTransferResult( HW_I2C_CHANNEL_FMPI2C1 ) )
        .WillOnce( Return( HW_I2C_STATUS_OK ) );

    EXPECT_EQ( LOGIC_EXPANDER_Self_Config(), LOGIC_EXPANDER_STATUS_BUSY );
    EXPECT_FALSE( logic_expander_ready );
    EXPECT_EQ( logic_expander_state[LOGIC_EXPANDER_DIGITAL_OUTPUT_SELECT].olat_a, 0x00U );
    EXPECT_EQ( logic_expander_state[LOGIC_EXPANDER_DIGITAL_OUTPUT_SELECT].olat_b, 0xFFU );

    EXPECT_EQ( LOGIC_EXPANDER_Process(), LOGIC_EXPANDER_STATUS_OK );
    EXPECT_TRUE( logic_expander_ready );
}

TEST_F( LogicExpanderTest, SelfConfigPhysicalErrorNeverMarksReady )
{
    EXPECT_CALL( mock_hw_i2c, ConfigureInternal( 0x33U ) ).WillOnce( Return( HW_I2C_STATUS_OK ) );
    EXPECT_CALL( mock_hw_i2c, ServiceTransactionQueue( HW_I2C_CHANNEL_FMPI2C1 ) );
    ExpectEightConfigurationWrites();
    EXPECT_CALL( mock_hw_i2c, IsTransactionQueueComplete( HW_I2C_CHANNEL_FMPI2C1 ) )
        .WillOnce( Return( true ) );
    EXPECT_CALL( mock_hw_i2c, GetAndClearTransferResult( HW_I2C_CHANNEL_FMPI2C1 ) )
        .WillOnce( Return( HW_I2C_STATUS_ERROR ) );

    EXPECT_EQ( LOGIC_EXPANDER_Self_Config(), LOGIC_EXPANDER_STATUS_ERROR );
    EXPECT_FALSE( logic_expander_ready );
}

TEST_F( LogicExpanderTest, SelfConfigPropagatesConfigurationBusyWithoutQueueing )
{
    EXPECT_CALL( mock_hw_i2c, ConfigureInternal( 0x33U ) ).WillOnce( Return( HW_I2C_STATUS_BUSY ) );
    EXPECT_CALL( mock_hw_i2c, ServiceTransactionQueue( _ ) ).Times( 0 );
    EXPECT_CALL( mock_hw_i2c, EnqueueMasterTransmit( _, _, _, _ ) ).Times( 0 );

    EXPECT_EQ( LOGIC_EXPANDER_Self_Config(), LOGIC_EXPANDER_STATUS_BUSY );
    EXPECT_FALSE( logic_expander_ready );
}

TEST_F( LogicExpanderTest, InternalTransmitAndReceiveUseAtomicMasterQueueApis )
{
    const std::array<uint8_t, 2U> payload = { 0xA1U, 0xB2U };
    EXPECT_CALL( mock_hw_i2c, EnqueueMasterTransmit( HW_I2C_CHANNEL_FMPI2C1, 0x33U, payload.data(),
                                                     payload.size() ) )
        .WillOnce( Return( HW_I2C_STATUS_OK ) );
    EXPECT_TRUE( LOGIC_EXPANDER_Master_Transmit_Internal( 0x33U, payload.data(), payload.size() ) );

    EXPECT_CALL( mock_hw_i2c, EnqueueMasterReceive( HW_I2C_CHANNEL_FMPI2C1, 0x33U, 12U ) )
        .WillOnce( Return( HW_I2C_STATUS_OK ) );
    EXPECT_TRUE( LOGIC_EXPANDER_Start_Master_Receive_Internal( 0x33U, 12U ) );
}

TEST_F( LogicExpanderTest, LoadControlBitMarksDirtyOnlyWhenShadowChanges )
{
    EXPECT_EQ( LOGIC_EXPANDER_Load_Control_Bit( LOGIC_EXPANDER_DIGITAL_OUTPUT_SELECT,
                                                LOGIC_EXPANDER_PORT_A, 8U, true ),
               LOGIC_EXPANDER_STATUS_INVALID_PARAM );

    EXPECT_EQ( LOGIC_EXPANDER_Load_Control_Bit( LOGIC_EXPANDER_DIGITAL_OUTPUT_SELECT,
                                                LOGIC_EXPANDER_PORT_A, 3U, false ),
               LOGIC_EXPANDER_STATUS_OK );
    EXPECT_EQ( logic_expander_dirty_bitmask, 0U );

    EXPECT_EQ( LOGIC_EXPANDER_Load_Control_Bit( LOGIC_EXPANDER_DIGITAL_OUTPUT_SELECT,
                                                LOGIC_EXPANDER_PORT_A, 3U, true ),
               LOGIC_EXPANDER_STATUS_OK );
    EXPECT_EQ( logic_expander_state[0].olat_a, 0x08U );
    EXPECT_EQ( logic_expander_dirty_bitmask, 0x01U );

    EXPECT_EQ( LOGIC_EXPANDER_Load_Control_Bit( LOGIC_EXPANDER_DIGITAL_OUTPUT_SELECT,
                                                LOGIC_EXPANDER_PORT_A, 3U, true ),
               LOGIC_EXPANDER_STATUS_OK );
    EXPECT_EQ( logic_expander_dirty_bitmask, 0x01U );
}

TEST_F( LogicExpanderTest, SendControlBitsReturnsNotReadyBeforePhysicalConfiguration )
{
    EXPECT_EQ( LOGIC_EXPANDER_Send_Control_Bits(), LOGIC_EXPANDER_STATUS_NOT_READY );
}

TEST_F( LogicExpanderTest, SendControlBitsEnqueuesOnlyDirtyExpanders )
{
    logic_expander_ready                           = true;
    logic_expander_dirty_bitmask                   = 0x01U;
    logic_expander_state[0]                        = { 0x5AU, 0xA5U };
    const std::array<uint8_t, 3U> expected_payload = { 0x14U, 0x5AU, 0xA5U };

    EXPECT_CALL( mock_hw_i2c, EnqueueMasterTransmit( HW_I2C_CHANNEL_FMPI2C1, 0x20U, _, 3U ) )
        .WillOnce( [&]( HWI2CChannel_T, uint16_t, const uint8_t* data, uint16_t length ) {
            EXPECT_EQ( std::memcmp( data, expected_payload.data(), length ), 0 );
            return HW_I2C_STATUS_OK;
        } );

    EXPECT_EQ( LOGIC_EXPANDER_Send_Control_Bits(), LOGIC_EXPANDER_STATUS_OK );
    EXPECT_EQ( logic_expander_dirty_bitmask, 0U );
    EXPECT_EQ( LOGIC_EXPANDER_Send_Control_Bits(), LOGIC_EXPANDER_STATUS_OK );
}

TEST_F( LogicExpanderTest, PartialQueueFullRetryDoesNotDuplicateAcceptedExpander )
{
    logic_expander_ready          = true;
    logic_expander_active_bitmask = 0x03U;
    logic_expander_dirty_bitmask  = 0x03U;
    logic_expander_state[0]       = { 0x10U, 0x11U };
    logic_expander_state[1]       = { 0x20U, 0x21U };

    {
        InSequence sequence;
        EXPECT_CALL( mock_hw_i2c, EnqueueMasterTransmit( HW_I2C_CHANNEL_FMPI2C1, 0x20U, _, 3U ) )
            .WillOnce( Return( HW_I2C_STATUS_OK ) );
        EXPECT_CALL( mock_hw_i2c, EnqueueMasterTransmit( HW_I2C_CHANNEL_FMPI2C1, 0x21U, _, 3U ) )
            .WillOnce( Return( HW_I2C_STATUS_BUSY ) );
    }

    EXPECT_EQ( LOGIC_EXPANDER_Send_Control_Bits(), LOGIC_EXPANDER_STATUS_BUSY );
    EXPECT_EQ( logic_expander_dirty_bitmask, 0x02U );

    EXPECT_CALL( mock_hw_i2c, EnqueueMasterTransmit( HW_I2C_CHANNEL_FMPI2C1, 0x21U, _, 3U ) )
        .WillOnce( Return( HW_I2C_STATUS_OK ) );
    EXPECT_EQ( LOGIC_EXPANDER_Send_Control_Bits(), LOGIC_EXPANDER_STATUS_OK );
    EXPECT_EQ( logic_expander_dirty_bitmask, 0U );
}

TEST_F( LogicExpanderTest, GetStateSnapshotUsesRoleIndexedAddressAndShadowTables )
{
    logic_expander_state[LOGIC_EXPANDER_UNASSIGNED_2] = { 0x11U, 0x22U };
    LogicExpanderStateSnapshot_T snapshot{};

    EXPECT_EQ( LOGIC_EXPANDER_Get_State_Snapshot( LOGIC_EXPANDER_UNASSIGNED_2, &snapshot ),
               LOGIC_EXPANDER_STATUS_OK );
    EXPECT_EQ( snapshot.device_address_7bit, 0x22U );
    EXPECT_EQ( snapshot.olat_a, 0x11U );
    EXPECT_EQ( snapshot.olat_b, 0x22U );

    EXPECT_EQ( LOGIC_EXPANDER_Get_State_Snapshot( LOGIC_EXPANDER_COUNT, nullptr ),
               LOGIC_EXPANDER_STATUS_INVALID_PARAM );
}
