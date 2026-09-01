/******************************************************************************
 *  File:       test_exec_i2c.cpp
 *  Author:     Coen Pasitchnyj
 *  Created:    20-Apr-2026
 *
 *  Description:
 *      Unit tests for the execution-level I2C wrapper.
 *
 *      These tests verify configuration validation, low-level forwarding,
 *      and receive-copy orchestration for the exec_i2c layer.
 *
 *  Notes:
 *      - Production code is written in C; tests are written in C++.
 *      - C headers and implementation files are included inside extern "C".
 *      - The low-level hw_i2c API is mocked so the wrapper can be tested in
 *        isolation.
 *      - The fixture resets mocked driver ownership before each test.
 *
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <cstring>

extern "C"
{
#include "exec_i2c.h"
#include "logic_expander.h"
#include <stdint.h>
#include <stdbool.h>
}

/**-----------------------------------------------------------------------------
 *  Test Constants / Macros
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Test Doubles / Mocks
 *------------------------------------------------------------------------------
 */

class MockHWI2C
{
public:
    MOCK_METHOD( HWI2CStatus_T, ConfigureChannel,
                 ( HWI2CChannel_T channel, const HWI2CChannelConfig_T* config ), () );
    MOCK_METHOD( HWI2CStatus_T, StartChannel, ( HWI2CChannel_T channel ), () );
    MOCK_METHOD( HWI2CStatus_T, StopChannel, ( HWI2CChannel_T channel ), () );
    MOCK_METHOD( HWI2CStatus_T, EnqueueMasterTransmit,
                 ( HWI2CChannel_T channel, uint16_t device_address_7bit, const uint8_t* payload,
                   uint16_t payload_length ),
                 () );
    MOCK_METHOD( HWI2CStatus_T, EnqueueMasterReceive,
                 ( HWI2CChannel_T channel, uint16_t device_address_7bit, uint16_t expected_length ),
                 () );
    MOCK_METHOD( bool, LoadStageBuffer,
                 ( HWI2CChannel_T channel, const uint8_t* data, uint16_t length ), () );
    MOCK_METHOD( bool, TriggerSlaveTransmitExternal, ( HWI2CChannel_T channel ), () );
    MOCK_METHOD( bool, TriggerSlaveReceiveExternal,
                 ( HWI2CChannel_T channel, uint16_t expected_length ), () );
    MOCK_METHOD( void, ServiceTransactionQueue, ( HWI2CChannel_T channel ), () );
    MOCK_METHOD( bool, IsTransactionQueueComplete, ( HWI2CChannel_T channel ), () );
    MOCK_METHOD( HWI2CStatus_T, GetAndClearTransferResult, ( HWI2CChannel_T channel ), () );
    MOCK_METHOD( HWI2CStatus_T, RecoverChannel, ( HWI2CChannel_T channel ), () );
    MOCK_METHOD( bool, PeekReceivedMessage,
                 ( HWI2CChannel_T channel, HWI2CRxMessagePeek_T* message ), () );
    MOCK_METHOD( bool, ConsumeReceivedMessage, ( HWI2CChannel_T channel ), () );
    MOCK_METHOD( bool, GetOverflowStatus, ( HWI2CChannel_T channel ), () );
    MOCK_METHOD( LogicExpanderStatus_T, LoadControlBit,
                 ( LogicExpanderIndex_T expander, LogicExpanderPort_T port, uint8_t bit_index,
                   bool value ),
                 () );
    MOCK_METHOD( LogicExpanderStatus_T, SendControlBits, (), () );
};

static MockHWI2C* g_mock_hw_i2c = nullptr;

extern "C"
{
HWI2CStatus_T HW_I2C_Configure_Channel( HWI2CChannel_T channel, const HWI2CChannelConfig_T* config )
{
    return g_mock_hw_i2c->ConfigureChannel( channel, config );
}

HWI2CStatus_T HW_I2C_Start_Channel( HWI2CChannel_T channel )
{
    return g_mock_hw_i2c->StartChannel( channel );
}

HWI2CStatus_T HW_I2C_Stop_Channel( HWI2CChannel_T channel )
{
    return g_mock_hw_i2c->StopChannel( channel );
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

bool HW_I2C_Load_Stage_Buffer( HWI2CChannel_T channel, const uint8_t* data, uint16_t length )
{
    return g_mock_hw_i2c->LoadStageBuffer( channel, data, length );
}

bool HW_I2C_Trigger_Slave_Transmit_External( HWI2CChannel_T channel )
{
    return g_mock_hw_i2c->TriggerSlaveTransmitExternal( channel );
}

bool HW_I2C_Trigger_Slave_Receive_External( HWI2CChannel_T channel, uint16_t expected_length )
{
    return g_mock_hw_i2c->TriggerSlaveReceiveExternal( channel, expected_length );
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

HWI2CStatus_T HW_I2C_Recover_Channel( HWI2CChannel_T channel )
{
    return g_mock_hw_i2c->RecoverChannel( channel );
}

bool HW_I2C_Peek_Received_Message( HWI2CChannel_T channel, HWI2CRxMessagePeek_T* message )
{
    return g_mock_hw_i2c->PeekReceivedMessage( channel, message );
}

bool HW_I2C_Consume_Received_Message( HWI2CChannel_T channel )
{
    return g_mock_hw_i2c->ConsumeReceivedMessage( channel );
}

bool HW_I2C_Get_Overflow_Status( HWI2CChannel_T channel )
{
    return g_mock_hw_i2c->GetOverflowStatus( channel );
}

LogicExpanderStatus_T LOGIC_EXPANDER_Load_Control_Bit( LogicExpanderIndex_T expander,
                                                       LogicExpanderPort_T port, uint8_t bit_index,
                                                       bool value )
{
    return g_mock_hw_i2c->LoadControlBit( expander, port, bit_index, value );
}

LogicExpanderStatus_T LOGIC_EXPANDER_Send_Control_Bits( void )
{
    return g_mock_hw_i2c->SendControlBits();
}
}

#include "../exec_i2c.c" /* Module under test */  // NOLINT

using ::testing::_;
using ::testing::InSequence;
using ::testing::Return;
using ::testing::StrictMock;

/**-----------------------------------------------------------------------------
 *  Test Fixture
 *------------------------------------------------------------------------------
 */

/**
 * @brief Test fixture for module tests.
 *
 * Provides a consistent setup/teardown environment for all test cases.
 */
class ExecI2CTest : public ::testing::Test
{
protected:
    StrictMock<MockHWI2C> mock_hw_i2c;

    void SetUp( void ) override
    {
        g_mock_hw_i2c = &mock_hw_i2c;
        std::memset( exec_i2c_channel_state, 0, sizeof( exec_i2c_channel_state ) );
    }

    void TearDown( void ) override
    {
        g_mock_hw_i2c = nullptr;
    }

    void ExpectInterfaceControl( ExecI2CChannel_T channel, ExecI2CVoltage_T voltage,
                                 ExecI2CPullup_T pullup, bool enabled )
    {
        const uint8_t first_bit = channel == EXEC_I2C_CHANNEL_1 ? 0U : 4U;
        const bool    a0        = ( ( uint8_t )pullup & 0x01U ) != 0U;
        const bool    a1        = ( ( uint8_t )pullup & 0x02U ) != 0U;

        EXPECT_CALL( mock_hw_i2c, LoadControlBit( LOGIC_EXPANDER_I2C_AO, LOGIC_EXPANDER_PORT_A,
                                                  first_bit, voltage == EXEC_I2C_VOLTAGE_5V ) )
            .WillOnce( Return( LOGIC_EXPANDER_STATUS_OK ) );
        EXPECT_CALL( mock_hw_i2c, LoadControlBit( LOGIC_EXPANDER_I2C_AO, LOGIC_EXPANDER_PORT_A,
                                                  ( uint8_t )( first_bit + 1U ), a0 ) )
            .WillOnce( Return( LOGIC_EXPANDER_STATUS_OK ) );
        EXPECT_CALL( mock_hw_i2c, LoadControlBit( LOGIC_EXPANDER_I2C_AO, LOGIC_EXPANDER_PORT_A,
                                                  ( uint8_t )( first_bit + 2U ), a1 ) )
            .WillOnce( Return( LOGIC_EXPANDER_STATUS_OK ) );
        EXPECT_CALL( mock_hw_i2c, LoadControlBit( LOGIC_EXPANDER_I2C_AO, LOGIC_EXPANDER_PORT_A,
                                                  ( uint8_t )( first_bit + 3U ), enabled ) )
            .WillOnce( Return( LOGIC_EXPANDER_STATUS_OK ) );
        EXPECT_CALL( mock_hw_i2c, SendControlBits() )
            .WillOnce( Return( LOGIC_EXPANDER_STATUS_OK ) );
    }
};

/**-----------------------------------------------------------------------------
 *  Test Cases
 *------------------------------------------------------------------------------
 */

TEST_F( ExecI2CTest, ConfigureRejectsInvalidExternalConfigWithoutLowLevelCalls )
{
    EXECI2CChannelConfig_T invalid_config = {
        .is_enabled       = true,
        .mode             = HW_I2C_MODE_MASTER,
        .speed            = HW_I2C_SPEED_100KHZ,
        .own_address_7bit = 0x12U,
        .pullup           = EXEC_I2C_PULLUP_COUNT,
        .voltage          = EXEC_I2C_VOLTAGE_3V3,
    };

    EXPECT_EQ( EXEC_I2C_Configure_Channel( EXEC_I2C_CHANNEL_1, &invalid_config ),
               EXEC_I2C_STATUS_INVALID_PARAM );
}

TEST_F( ExecI2CTest, ConfigureMapsChannel1ToInterruptAndLeavesItStopped )
{
    const EXECI2CChannelConfig_T config = {
        .is_enabled       = true,
        .mode             = HW_I2C_MODE_MASTER,
        .speed            = HW_I2C_SPEED_100KHZ,
        .own_address_7bit = 0x12U,
        .pullup           = EXEC_I2C_PULLUP_10K,
        .voltage          = EXEC_I2C_VOLTAGE_5V,
    };

    ExpectInterfaceControl( EXEC_I2C_CHANNEL_1, EXEC_I2C_VOLTAGE_5V, EXEC_I2C_PULLUP_10K, false );
    EXPECT_CALL( mock_hw_i2c, ConfigureChannel( HW_I2C_CHANNEL_1, _ ) )
        .WillOnce( [&]( HWI2CChannel_T, const HWI2CChannelConfig_T* hw_config ) {
            EXPECT_EQ( hw_config->tx_transfer_path, HW_I2C_TRANSFER_INTERRUPT );
            EXPECT_EQ( hw_config->rx_transfer_path, HW_I2C_TRANSFER_INTERRUPT );
            return HW_I2C_STATUS_OK;
        } );

    EXPECT_EQ( EXEC_I2C_Configure_Channel( EXEC_I2C_CHANNEL_1, &config ), EXEC_I2C_STATUS_OK );
    EXPECT_TRUE( EXEC_I2C_Is_Channel_Configured( EXEC_I2C_CHANNEL_1 ) );
    EXPECT_FALSE( EXEC_I2C_Is_Channel_Started( EXEC_I2C_CHANNEL_1 ) );
}

TEST_F( ExecI2CTest, ConfigureMapsChannel2ToDma )
{
    const EXECI2CChannelConfig_T config = {
        .is_enabled       = true,
        .mode             = HW_I2C_MODE_SLAVE,
        .speed            = HW_I2C_SPEED_400KHZ,
        .own_address_7bit = 0x34U,
        .pullup           = EXEC_I2C_PULLUP_4K7,
        .voltage          = EXEC_I2C_VOLTAGE_3V3,
    };

    ExpectInterfaceControl( EXEC_I2C_CHANNEL_2, EXEC_I2C_VOLTAGE_3V3, EXEC_I2C_PULLUP_4K7, false );
    EXPECT_CALL( mock_hw_i2c, ConfigureChannel( HW_I2C_CHANNEL_2, _ ) )
        .WillOnce( [&]( HWI2CChannel_T, const HWI2CChannelConfig_T* hw_config ) {
            EXPECT_EQ( hw_config->tx_transfer_path, HW_I2C_TRANSFER_DMA );
            EXPECT_EQ( hw_config->rx_transfer_path, HW_I2C_TRANSFER_DMA );
            return HW_I2C_STATUS_OK;
        } );

    EXPECT_EQ( EXEC_I2C_Configure_Channel( EXEC_I2C_CHANNEL_2, &config ), EXEC_I2C_STATUS_OK );
}

TEST_F( ExecI2CTest, FailedStoppedChannelReconfigurationRevokesStartPermission )
{
    EXECI2CChannelState_T& state = exec_i2c_channel_state[EXEC_I2C_CHANNEL_1];
    state.is_configured          = true;

    const EXECI2CChannelConfig_T config = {
        .is_enabled       = true,
        .mode             = HW_I2C_MODE_MASTER,
        .speed            = HW_I2C_SPEED_100KHZ,
        .own_address_7bit = 0x12U,
        .pullup           = EXEC_I2C_PULLUP_10K,
        .voltage          = EXEC_I2C_VOLTAGE_5V,
    };

    InSequence sequence;
    ExpectInterfaceControl( EXEC_I2C_CHANNEL_1, EXEC_I2C_VOLTAGE_5V, EXEC_I2C_PULLUP_10K, false );
    EXPECT_CALL( mock_hw_i2c, ConfigureChannel( HW_I2C_CHANNEL_1, _ ) )
        .WillOnce( Return( HW_I2C_STATUS_ERROR ) );
    ExpectInterfaceControl( EXEC_I2C_CHANNEL_1, EXEC_I2C_VOLTAGE_3V3, EXEC_I2C_PULLUP_1K, false );

    EXPECT_EQ( EXEC_I2C_Configure_Channel( EXEC_I2C_CHANNEL_1, &config ),
               EXEC_I2C_STATUS_ERROR );
    EXPECT_FALSE( EXEC_I2C_Is_Channel_Configured( EXEC_I2C_CHANNEL_1 ) );

    EXPECT_CALL( mock_hw_i2c, StartChannel( _ ) ).Times( 0 );
    EXPECT_EQ( EXEC_I2C_Start_Channel( EXEC_I2C_CHANNEL_1 ), EXEC_I2C_STATUS_NOT_CONFIGURED );
}

TEST_F( ExecI2CTest, StartAndStopApplyLifecycleInSafeOrder )
{
    EXECI2CChannelState_T& state = exec_i2c_channel_state[EXEC_I2C_CHANNEL_1];
    state.is_configured          = true;
    state.configuration          = {
                 .is_enabled       = true,
                 .mode             = HW_I2C_MODE_MASTER,
                 .speed            = HW_I2C_SPEED_100KHZ,
                 .own_address_7bit = 0x31U,
                 .pullup           = EXEC_I2C_PULLUP_2K2,
                 .voltage          = EXEC_I2C_VOLTAGE_5V,
    };

    {
        InSequence sequence;
        EXPECT_CALL( mock_hw_i2c, StartChannel( HW_I2C_CHANNEL_1 ) )
            .WillOnce( Return( HW_I2C_STATUS_OK ) );
        ExpectInterfaceControl( EXEC_I2C_CHANNEL_1, EXEC_I2C_VOLTAGE_5V, EXEC_I2C_PULLUP_2K2,
                                true );
    }
    EXPECT_EQ( EXEC_I2C_Start_Channel( EXEC_I2C_CHANNEL_1 ), EXEC_I2C_STATUS_OK );
    EXPECT_TRUE( EXEC_I2C_Is_Channel_Started( EXEC_I2C_CHANNEL_1 ) );

    {
        InSequence sequence;
        ExpectInterfaceControl( EXEC_I2C_CHANNEL_1, EXEC_I2C_VOLTAGE_3V3, EXEC_I2C_PULLUP_1K,
                                false );
        EXPECT_CALL( mock_hw_i2c, StopChannel( HW_I2C_CHANNEL_1 ) )
            .WillOnce( Return( HW_I2C_STATUS_OK ) );
    }
    EXPECT_EQ( EXEC_I2C_Stop_Channel( EXEC_I2C_CHANNEL_1 ), EXEC_I2C_STATUS_OK );
    EXPECT_TRUE( EXEC_I2C_Is_Channel_Configured( EXEC_I2C_CHANNEL_1 ) );
    EXPECT_FALSE( EXEC_I2C_Is_Channel_Started( EXEC_I2C_CHANNEL_1 ) );
}

TEST_F( ExecI2CTest, DisabledConfigurationAppliesSafeStateWithoutConfiguringHw )
{
    const EXECI2CChannelConfig_T config = { .is_enabled = false };

    ExpectInterfaceControl( EXEC_I2C_CHANNEL_2, EXEC_I2C_VOLTAGE_3V3, EXEC_I2C_PULLUP_1K, false );

    EXPECT_EQ( EXEC_I2C_Configure_Channel( EXEC_I2C_CHANNEL_2, &config ), EXEC_I2C_STATUS_OK );
    EXPECT_FALSE( EXEC_I2C_Is_Channel_Configured( EXEC_I2C_CHANNEL_2 ) );
    EXPECT_FALSE( EXEC_I2C_Is_Channel_Started( EXEC_I2C_CHANNEL_2 ) );
}

TEST_F( ExecI2CTest, StartRollsBackHwWhenPullupControlFails )
{
    EXECI2CChannelState_T& state = exec_i2c_channel_state[EXEC_I2C_CHANNEL_1];
    state.is_configured          = true;
    state.configuration          = {
                 .is_enabled       = true,
                 .mode             = HW_I2C_MODE_MASTER,
                 .speed            = HW_I2C_SPEED_100KHZ,
                 .own_address_7bit = 0x31U,
                 .pullup           = EXEC_I2C_PULLUP_4K7,
                 .voltage          = EXEC_I2C_VOLTAGE_3V3,
    };

    {
        InSequence sequence;
        EXPECT_CALL( mock_hw_i2c, StartChannel( HW_I2C_CHANNEL_1 ) )
            .WillOnce( Return( HW_I2C_STATUS_OK ) );
        EXPECT_CALL( mock_hw_i2c,
                     LoadControlBit( LOGIC_EXPANDER_I2C_AO, LOGIC_EXPANDER_PORT_A, 0U, false ) )
            .WillOnce( Return( LOGIC_EXPANDER_STATUS_ERROR ) );
        EXPECT_CALL( mock_hw_i2c, StopChannel( HW_I2C_CHANNEL_1 ) )
            .WillOnce( Return( HW_I2C_STATUS_OK ) );
    }

    EXPECT_EQ( EXEC_I2C_Start_Channel( EXEC_I2C_CHANNEL_1 ), EXEC_I2C_STATUS_ERROR );
    EXPECT_FALSE( EXEC_I2C_Is_Channel_Started( EXEC_I2C_CHANNEL_1 ) );
}

TEST_F( ExecI2CTest, StopPreservesStartedStateWhenHwIsBusy )
{
    EXECI2CChannelState_T& state = exec_i2c_channel_state[EXEC_I2C_CHANNEL_1];
    state.is_configured          = true;
    state.is_started             = true;

    ExpectInterfaceControl( EXEC_I2C_CHANNEL_1, EXEC_I2C_VOLTAGE_3V3, EXEC_I2C_PULLUP_1K, false );
    EXPECT_CALL( mock_hw_i2c, StopChannel( HW_I2C_CHANNEL_1 ) )
        .WillOnce( Return( HW_I2C_STATUS_BUSY ) );

    EXPECT_EQ( EXEC_I2C_Stop_Channel( EXEC_I2C_CHANNEL_1 ), EXEC_I2C_STATUS_BUSY );
    EXPECT_TRUE( EXEC_I2C_Is_Channel_Started( EXEC_I2C_CHANNEL_1 ) );
}

TEST_F( ExecI2CTest, MasterTransmitExternalSubmitsOneAtomicQueueRequest )
{
    const uint8_t payload[] = { 0xA1U, 0xB2U };

    EXPECT_CALL( mock_hw_i2c,
                 EnqueueMasterTransmit( HW_I2C_CHANNEL_2, 0x45U, payload, sizeof( payload ) ) )
        .WillOnce( Return( HW_I2C_STATUS_OK ) );

    EXPECT_TRUE( EXEC_I2C_Master_Transmit_External( EXEC_I2C_CHANNEL_2, 0x45U, payload,
                                                    sizeof( payload ) ) );
}

TEST_F( ExecI2CTest, MasterTransmitExternalReturnsFalseWhenQueueRejectsRequest )
{
    const uint8_t payload[] = { 1U, 2U, 3U, 4U };
    EXPECT_CALL( mock_hw_i2c,
                 EnqueueMasterTransmit( HW_I2C_CHANNEL_1, 0x11U, payload, sizeof( payload ) ) )
        .WillOnce( Return( HW_I2C_STATUS_BUSY ) );

    EXPECT_FALSE( EXEC_I2C_Master_Transmit_External( EXEC_I2C_CHANNEL_1, 0x11U, payload,
                                                     sizeof( payload ) ) );
}

TEST_F( ExecI2CTest, MasterTransmitExternalRejectsNonExternalChannelWithoutLowLevelCall )
{
    const uint8_t payload = 0x12U;
    EXPECT_CALL( mock_hw_i2c, EnqueueMasterTransmit( _, _, _, _ ) ).Times( 0 );

    EXPECT_FALSE( EXEC_I2C_Master_Transmit_External( EXEC_I2C_CHANNEL_COUNT, 0x20U, &payload,
                                                     sizeof( payload ) ) );
}

TEST_F( ExecI2CTest, SlaveTransmitExternal_ForwardsBothCalls )
{
    const uint8_t payload[] = { 0x55U };

    {
        InSequence sequence;
        EXPECT_CALL( mock_hw_i2c, LoadStageBuffer( HW_I2C_CHANNEL_2, payload, sizeof( payload ) ) )
            .WillOnce( Return( true ) );
        EXPECT_CALL( mock_hw_i2c, TriggerSlaveTransmitExternal( HW_I2C_CHANNEL_2 ) )
            .WillOnce( Return( true ) );
    }

    EXPECT_TRUE(
        EXEC_I2C_Slave_Transmit_External( EXEC_I2C_CHANNEL_2, payload, sizeof( payload ) ) );
}

TEST_F( ExecI2CTest, MasterReceiveExternal_ForwardsToLowLevel )
{
    EXPECT_CALL( mock_hw_i2c, EnqueueMasterReceive( HW_I2C_CHANNEL_2, 0x55U, 9U ) )
        .WillOnce( Return( HW_I2C_STATUS_OK ) );

    EXPECT_TRUE( EXEC_I2C_Start_Master_Receive_External( EXEC_I2C_CHANNEL_2, 0x55U, 9U ) );
}

TEST_F( ExecI2CTest, MasterReceiveExternalRejectsNonExternalChannelWithoutLowLevelCall )
{
    EXPECT_CALL( mock_hw_i2c, EnqueueMasterReceive( _, _, _ ) ).Times( 0 );

    EXPECT_FALSE( EXEC_I2C_Start_Master_Receive_External( EXEC_I2C_CHANNEL_COUNT, 0x20U, 1U ) );
}

TEST_F( ExecI2CTest, SlaveReceiveExternal_ForwardsToLowLevel )
{
    EXPECT_CALL( mock_hw_i2c, TriggerSlaveReceiveExternal( HW_I2C_CHANNEL_1, 6U ) )
        .WillOnce( Return( true ) );

    EXPECT_TRUE( EXEC_I2C_Start_Slave_Receive_External( EXEC_I2C_CHANNEL_1, 6U ) );
}

TEST_F( ExecI2CTest, QueueProgressAndAsynchronousResultAreForwarded )
{
    EXPECT_CALL( mock_hw_i2c, ServiceTransactionQueue( HW_I2C_CHANNEL_2 ) );
    EXEC_I2C_Service_Transaction_Queue( EXEC_I2C_CHANNEL_2 );

    EXPECT_CALL( mock_hw_i2c, IsTransactionQueueComplete( HW_I2C_CHANNEL_2 ) )
        .WillOnce( Return( false ) );
    EXPECT_FALSE( EXEC_I2C_Is_Transaction_Queue_Complete( EXEC_I2C_CHANNEL_2 ) );

    EXPECT_CALL( mock_hw_i2c, GetAndClearTransferResult( HW_I2C_CHANNEL_2 ) )
        .WillOnce( Return( HW_I2C_STATUS_ERROR ) );
    EXPECT_EQ( EXEC_I2C_Get_And_Clear_Transfer_Result( EXEC_I2C_CHANNEL_2 ),
               EXEC_I2C_STATUS_ERROR );
}

TEST_F( ExecI2CTest, RecoveryStatusIsForwarded )
{
    EXPECT_CALL( mock_hw_i2c, RecoverChannel( HW_I2C_CHANNEL_2 ) )
        .WillOnce( Return( HW_I2C_STATUS_ERROR ) );

    EXPECT_EQ( EXEC_I2C_Recover_Channel( EXEC_I2C_CHANNEL_2 ), EXEC_I2C_STATUS_ERROR );
}

TEST_F( ExecI2CTest, ReceiveMessageCopiesOneCompleteMessageAndConsumesIt )
{
    const uint8_t        source[] = { 0x10U, 0x11U, 0x12U };
    HWI2CRxMessagePeek_T message  = {
         .descriptor = { .transfer_kind       = HW_I2C_TRANSFER_KIND_MASTER_RX,
                         .target_address_7bit = 0x45U,
                         .length              = 3U,
                         .status              = HW_I2C_STATUS_OK },
         .first      = { .data = source, .length = 3U },
         .second     = { .data = nullptr, .length = 0U },
    };
    uint8_t                    destination[4] = { 0 };
    HWI2CRxMessageDescriptor_T descriptor;
    uint16_t                   bytes_copied    = 99U;
    uint16_t                   required_length = 99U;

    {
        InSequence sequence;
        EXPECT_CALL( mock_hw_i2c, ServiceTransactionQueue( HW_I2C_CHANNEL_1 ) );
        EXPECT_CALL( mock_hw_i2c, PeekReceivedMessage( HW_I2C_CHANNEL_1, _ ) )
            .WillOnce( [&]( HWI2CChannel_T, HWI2CRxMessagePeek_T* out_message ) {
                *out_message = message;
                return true;
            } );
        EXPECT_CALL( mock_hw_i2c, ConsumeReceivedMessage( HW_I2C_CHANNEL_1 ) )
            .WillOnce( Return( true ) );
    }

    EXPECT_EQ( EXEC_I2C_Receive_Message_Copy_And_Consume( EXEC_I2C_CHANNEL_1, destination,
                                                          sizeof( destination ), &descriptor,
                                                          &bytes_copied, &required_length ),
               EXEC_I2C_STATUS_OK );
    EXPECT_EQ( bytes_copied, 3U );
    EXPECT_EQ( required_length, 3U );
    EXPECT_EQ( descriptor.transfer_kind, HW_I2C_TRANSFER_KIND_MASTER_RX );
    EXPECT_EQ( descriptor.target_address_7bit, 0x45U );
    EXPECT_EQ( std::memcmp( destination, source, 3U ), 0 );
}

TEST_F( ExecI2CTest, ReceiveMessageCopiesWrappedSpansInOrder )
{
    const uint8_t        first[]  = { 0x21U, 0x22U };
    const uint8_t        second[] = { 0x23U, 0x24U };
    HWI2CRxMessagePeek_T message  = {
         .descriptor = { .transfer_kind       = HW_I2C_TRANSFER_KIND_SLAVE_RX,
                         .target_address_7bit = 0U,
                         .length              = 4U,
                         .status              = HW_I2C_STATUS_OK },
         .first      = { .data = first, .length = 2U },
         .second     = { .data = second, .length = 2U },
    };
    uint8_t                    destination[4] = { 0 };
    HWI2CRxMessageDescriptor_T descriptor;
    uint16_t                   bytes_copied    = 0U;
    uint16_t                   required_length = 0U;

    {
        InSequence sequence;
        EXPECT_CALL( mock_hw_i2c, ServiceTransactionQueue( HW_I2C_CHANNEL_2 ) );
        EXPECT_CALL( mock_hw_i2c, PeekReceivedMessage( HW_I2C_CHANNEL_2, _ ) )
            .WillOnce( [&]( HWI2CChannel_T, HWI2CRxMessagePeek_T* out_message ) {
                *out_message = message;
                return true;
            } );
        EXPECT_CALL( mock_hw_i2c, ConsumeReceivedMessage( HW_I2C_CHANNEL_2 ) )
            .WillOnce( Return( true ) );
    }

    EXPECT_EQ( EXEC_I2C_Receive_Message_Copy_And_Consume( EXEC_I2C_CHANNEL_2, destination,
                                                          sizeof( destination ), &descriptor,
                                                          &bytes_copied, &required_length ),
               EXEC_I2C_STATUS_OK );
    EXPECT_EQ( bytes_copied, 4U );
    EXPECT_EQ( destination[0], 0x21U );
    EXPECT_EQ( destination[1], 0x22U );
    EXPECT_EQ( destination[2], 0x23U );
    EXPECT_EQ( destination[3], 0x24U );
}

TEST_F( ExecI2CTest, InsufficientDestinationLeavesCompleteMessageUnconsumed )
{
    const uint8_t        source[] = { 0x31U, 0x32U, 0x33U, 0x34U };
    HWI2CRxMessagePeek_T message  = {
         .descriptor = { .transfer_kind       = HW_I2C_TRANSFER_KIND_MASTER_RX,
                         .target_address_7bit = 0x21U,
                         .length              = 4U,
                         .status              = HW_I2C_STATUS_OK },
         .first      = { .data = source, .length = 4U },
         .second     = { .data = nullptr, .length = 0U },
    };
    uint8_t                    destination[3] = { 0 };
    HWI2CRxMessageDescriptor_T descriptor;
    uint16_t                   bytes_copied    = 10U;
    uint16_t                   required_length = 0U;

    {
        InSequence sequence;
        EXPECT_CALL( mock_hw_i2c, ServiceTransactionQueue( HW_I2C_CHANNEL_1 ) );
        EXPECT_CALL( mock_hw_i2c, PeekReceivedMessage( HW_I2C_CHANNEL_1, _ ) )
            .WillOnce( [&]( HWI2CChannel_T, HWI2CRxMessagePeek_T* out_message ) {
                *out_message = message;
                return true;
            } );
    }
    EXPECT_CALL( mock_hw_i2c, ConsumeReceivedMessage( _ ) ).Times( 0 );

    EXPECT_EQ( EXEC_I2C_Receive_Message_Copy_And_Consume( EXEC_I2C_CHANNEL_1, destination,
                                                          sizeof( destination ), &descriptor,
                                                          &bytes_copied, &required_length ),
               EXEC_I2C_STATUS_BUFFER_TOO_SMALL );
    EXPECT_EQ( bytes_copied, 0U );
    EXPECT_EQ( required_length, 4U );
}

TEST_F( ExecI2CTest, TwoReceivedTransactionsRequireTwoCalls )
{
    const uint8_t        first[]       = { 0x41U, 0x42U };
    const uint8_t        second[]      = { 0x51U };
    HWI2CRxMessagePeek_T first_message = {
        .descriptor = { .transfer_kind       = HW_I2C_TRANSFER_KIND_MASTER_RX,
                        .target_address_7bit = 0x31U,
                        .length              = 2U,
                        .status              = HW_I2C_STATUS_OK },
        .first      = { .data = first, .length = 2U },
        .second     = { .data = nullptr, .length = 0U },
    };
    HWI2CRxMessagePeek_T second_message = {
        .descriptor = { .transfer_kind       = HW_I2C_TRANSFER_KIND_SLAVE_RX,
                        .target_address_7bit = 0U,
                        .length              = 1U,
                        .status              = HW_I2C_STATUS_OK },
        .first      = { .data = second, .length = 1U },
        .second     = { .data = nullptr, .length = 0U },
    };
    uint8_t  destination[4] = { 0 };
    uint16_t bytes_copied   = 0U;

    {
        InSequence sequence;
        EXPECT_CALL( mock_hw_i2c, ServiceTransactionQueue( HW_I2C_CHANNEL_1 ) );
        EXPECT_CALL( mock_hw_i2c, PeekReceivedMessage( HW_I2C_CHANNEL_1, _ ) )
            .WillOnce( [&]( HWI2CChannel_T, HWI2CRxMessagePeek_T* output ) {
                *output = first_message;
                return true;
            } );
        EXPECT_CALL( mock_hw_i2c, ConsumeReceivedMessage( HW_I2C_CHANNEL_1 ) )
            .WillOnce( Return( true ) );
        EXPECT_CALL( mock_hw_i2c, ServiceTransactionQueue( HW_I2C_CHANNEL_1 ) );
        EXPECT_CALL( mock_hw_i2c, PeekReceivedMessage( HW_I2C_CHANNEL_1, _ ) )
            .WillOnce( [&]( HWI2CChannel_T, HWI2CRxMessagePeek_T* output ) {
                *output = second_message;
                return true;
            } );
        EXPECT_CALL( mock_hw_i2c, ConsumeReceivedMessage( HW_I2C_CHANNEL_1 ) )
            .WillOnce( Return( true ) );
    }

    EXPECT_TRUE( EXEC_I2C_Receive_Copy_And_Consume( EXEC_I2C_CHANNEL_1, destination,
                                                    sizeof( destination ), &bytes_copied ) );
    EXPECT_EQ( bytes_copied, 2U );
    EXPECT_TRUE( EXEC_I2C_Receive_Copy_And_Consume( EXEC_I2C_CHANNEL_1, destination,
                                                    sizeof( destination ), &bytes_copied ) );
    EXPECT_EQ( bytes_copied, 1U );
    EXPECT_EQ( destination[0], 0x51U );
}

TEST_F( ExecI2CTest, LegacyReceivePollingReturnsSuccessWithZeroBytesWhenNoMessageExists )
{
    uint8_t  destination[4] = { 0 };
    uint16_t bytes_copied   = 123U;

    {
        InSequence sequence;
        EXPECT_CALL( mock_hw_i2c, ServiceTransactionQueue( HW_I2C_CHANNEL_1 ) );
        EXPECT_CALL( mock_hw_i2c, PeekReceivedMessage( HW_I2C_CHANNEL_1, _ ) )
            .WillOnce( []( HWI2CChannel_T, HWI2CRxMessagePeek_T* output ) {
                std::memset( output, 0, sizeof( *output ) );
                output->descriptor.transfer_kind = HW_I2C_TRANSFER_KIND_IDLE;
                return true;
            } );
    }
    EXPECT_CALL( mock_hw_i2c, ConsumeReceivedMessage( _ ) ).Times( 0 );

    EXPECT_TRUE( EXEC_I2C_Receive_Copy_And_Consume( EXEC_I2C_CHANNEL_1, destination,
                                                    sizeof( destination ), &bytes_copied ) );
    EXPECT_EQ( bytes_copied, 0U );
}
