/******************************************************************************
 *  File:       test_console.cpp
 *  Author:     Angus Corr
 *  Created:    06-Dec-2025
 *
 *  Description:
 *
 *  Notes:
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
#include "console.h" /* Module under test */
#include "command_helpers.h"
#include "exec_i2c.h"
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

class MockConsoleExecI2C
{
public:
    MOCK_METHOD( EXECI2CStatus_T, Configure,
                 ( ExecI2CChannel_T channel, const EXECI2CChannelConfig_T* config ), () );
    MOCK_METHOD( EXECI2CStatus_T, Start, ( ExecI2CChannel_T channel ), () );
    MOCK_METHOD( EXECI2CStatus_T, Stop, ( ExecI2CChannel_T channel ), () );
    MOCK_METHOD( bool, IsStarted, ( ExecI2CChannel_T channel ), () );
    MOCK_METHOD( bool, MasterTransmit,
                 ( ExecI2CChannel_T channel, uint16_t device_address_7bit, const uint8_t* payload,
                   uint16_t payload_length ),
                 () );
    MOCK_METHOD( bool, SlaveTransmit,
                 ( ExecI2CChannel_T channel, const uint8_t* payload, uint16_t payload_length ),
                 () );
    MOCK_METHOD( bool, StartMasterReceive,
                 ( ExecI2CChannel_T channel, uint16_t device_address_7bit,
                   uint16_t expected_length ),
                 () );
    MOCK_METHOD( bool, StartSlaveReceive, ( ExecI2CChannel_T channel, uint16_t expected_length ),
                 () );
    MOCK_METHOD( bool, IsQueueComplete, ( ExecI2CChannel_T channel ), () );
    MOCK_METHOD( EXECI2CStatus_T, GetAndClearResult, ( ExecI2CChannel_T channel ), () );
    MOCK_METHOD( EXECI2CStatus_T, RecoverChannel, ( ExecI2CChannel_T channel ), () );
    MOCK_METHOD( bool, Receive,
                 ( ExecI2CChannel_T channel, uint8_t* result_storage,
                   uint16_t result_storage_capacity, uint16_t* bytes_copied ),
                 () );
};

static MockConsoleExecI2C* g_mock_exec_i2c = nullptr;

extern "C"
{
void CONSOLE_Printf( const char* format, ... )
{
    ( void )format;
}

void vTaskDelay( const TickType_t ticks_to_delay )
{
    ( void )ticks_to_delay;
}

EXECI2CStatus_T EXEC_I2C_Configure_Channel( ExecI2CChannel_T              channel,
                                            const EXECI2CChannelConfig_T* config )
{
    return g_mock_exec_i2c->Configure( channel, config );
}

EXECI2CStatus_T EXEC_I2C_Start_Channel( ExecI2CChannel_T channel )
{
    return g_mock_exec_i2c->Start( channel );
}

EXECI2CStatus_T EXEC_I2C_Stop_Channel( ExecI2CChannel_T channel )
{
    return g_mock_exec_i2c->Stop( channel );
}

bool EXEC_I2C_Is_Channel_Started( ExecI2CChannel_T channel )
{
    return g_mock_exec_i2c->IsStarted( channel );
}

bool EXEC_I2C_Master_Transmit_External( ExecI2CChannel_T channel, uint16_t device_address_7bit,
                                        const uint8_t* payload, uint16_t payload_length )
{
    return g_mock_exec_i2c->MasterTransmit( channel, device_address_7bit, payload, payload_length );
}

bool EXEC_I2C_Slave_Transmit_External( ExecI2CChannel_T channel, const uint8_t* payload,
                                       uint16_t payload_length )
{
    return g_mock_exec_i2c->SlaveTransmit( channel, payload, payload_length );
}

bool EXEC_I2C_Start_Master_Receive_External( ExecI2CChannel_T channel, uint16_t device_address_7bit,
                                             uint16_t expected_length )
{
    return g_mock_exec_i2c->StartMasterReceive( channel, device_address_7bit, expected_length );
}

bool EXEC_I2C_Start_Slave_Receive_External( ExecI2CChannel_T channel, uint16_t expected_length )
{
    return g_mock_exec_i2c->StartSlaveReceive( channel, expected_length );
}

bool EXEC_I2C_Is_Transaction_Queue_Complete( ExecI2CChannel_T channel )
{
    return g_mock_exec_i2c->IsQueueComplete( channel );
}

EXECI2CStatus_T EXEC_I2C_Get_And_Clear_Transfer_Result( ExecI2CChannel_T channel )
{
    return g_mock_exec_i2c->GetAndClearResult( channel );
}

EXECI2CStatus_T EXEC_I2C_Recover_Channel( ExecI2CChannel_T channel )
{
    return g_mock_exec_i2c->RecoverChannel( channel );
}

bool EXEC_I2C_Receive_Copy_And_Consume( ExecI2CChannel_T channel, uint8_t* result_storage,
                                        uint16_t result_storage_capacity, uint16_t* bytes_copied )
{
    return g_mock_exec_i2c->Receive( channel, result_storage, result_storage_capacity,
                                     bytes_copied );
}
}

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
class ConsoleI2CTest : public ::testing::Test
{
protected:
    StrictMock<MockConsoleExecI2C> mock_exec_i2c;

    void SetUp( void ) override
    {
        g_mock_exec_i2c = &mock_exec_i2c;
    }

    void TearDown( void ) override
    {
        g_mock_exec_i2c = nullptr;
    }

    void ExpectLoopbackRecovery( CONSOLEI2CLoopbackChannels_T channels )
    {
        EXPECT_CALL( mock_exec_i2c, RecoverChannel( channels.master ) )
            .WillOnce( Return( EXEC_I2C_STATUS_ERROR ) );
        EXPECT_CALL( mock_exec_i2c, GetAndClearResult( channels.master ) )
            .WillOnce( Return( EXEC_I2C_STATUS_ERROR ) );
        EXPECT_CALL( mock_exec_i2c, RecoverChannel( channels.slave ) )
            .WillOnce( Return( EXEC_I2C_STATUS_ERROR ) );
        EXPECT_CALL( mock_exec_i2c, GetAndClearResult( channels.slave ) )
            .WillOnce( Return( EXEC_I2C_STATUS_ERROR ) );
    }
};

/**-----------------------------------------------------------------------------
 *  Test Cases
 *------------------------------------------------------------------------------
 */

TEST_F( ConsoleI2CTest, MasterToSlaveLoopbackWaitsForPhysicalCompletion )
{
    const char                         payload[] = "abc";
    char                               received[8]{};
    uint16_t                           received_length = 0U;
    const CONSOLEI2CLoopbackChannels_T channels        = { EXEC_I2C_CHANNEL_1, EXEC_I2C_CHANNEL_2 };

    InSequence sequence;
    EXPECT_CALL( mock_exec_i2c, StartSlaveReceive( EXEC_I2C_CHANNEL_2, 3U ) )
        .WillOnce( Return( true ) );
    EXPECT_CALL( mock_exec_i2c, MasterTransmit( EXEC_I2C_CHANNEL_1, 0x32U, _, 3U ) )
        .WillOnce( Return( true ) );
    EXPECT_CALL( mock_exec_i2c, IsQueueComplete( EXEC_I2C_CHANNEL_1 ) ).WillOnce( Return( false ) );
    EXPECT_CALL( mock_exec_i2c, Receive( EXEC_I2C_CHANNEL_2, _, _, _ ) )
        .WillOnce( []( ExecI2CChannel_T, uint8_t*, uint16_t, uint16_t* bytes_copied ) {
            *bytes_copied = 0U;
            return true;
        } );
    EXPECT_CALL( mock_exec_i2c, IsQueueComplete( EXEC_I2C_CHANNEL_1 ) ).WillOnce( Return( true ) );
    EXPECT_CALL( mock_exec_i2c, GetAndClearResult( EXEC_I2C_CHANNEL_1 ) )
        .WillOnce( Return( EXEC_I2C_STATUS_OK ) );
    EXPECT_CALL( mock_exec_i2c, Receive( EXEC_I2C_CHANNEL_2, _, _, _ ) )
        .WillOnce( [&]( ExecI2CChannel_T, uint8_t* output, uint16_t, uint16_t* bytes_copied ) {
            std::memcpy( output, payload, 3U );
            *bytes_copied = 3U;
            return true;
        } );

    EXPECT_TRUE( CONSOLE_Run_I2C_Loopback_M2S( channels, 0x32U, payload, 3U, received,
                                               sizeof( received ), &received_length ) );
    EXPECT_EQ( received_length, 3U );
    EXPECT_EQ( std::memcmp( received, payload, 3U ), 0 );
}

TEST_F( ConsoleI2CTest, SlaveToMasterLoopbackServicesAndConsumesCompletedReceive )
{
    const char                         payload[] = "xyz";
    char                               received[8]{};
    uint16_t                           received_length = 0U;
    const CONSOLEI2CLoopbackChannels_T channels        = { EXEC_I2C_CHANNEL_2, EXEC_I2C_CHANNEL_1 };

    InSequence sequence;
    EXPECT_CALL( mock_exec_i2c, SlaveTransmit( EXEC_I2C_CHANNEL_1, _, 3U ) )
        .WillOnce( Return( true ) );
    EXPECT_CALL( mock_exec_i2c, StartMasterReceive( EXEC_I2C_CHANNEL_2, 0x31U, 3U ) )
        .WillOnce( Return( true ) );
    EXPECT_CALL( mock_exec_i2c, IsQueueComplete( EXEC_I2C_CHANNEL_2 ) ).WillOnce( Return( true ) );
    EXPECT_CALL( mock_exec_i2c, GetAndClearResult( EXEC_I2C_CHANNEL_2 ) )
        .WillOnce( Return( EXEC_I2C_STATUS_OK ) );
    EXPECT_CALL( mock_exec_i2c, Receive( EXEC_I2C_CHANNEL_2, _, _, _ ) )
        .WillOnce( [&]( ExecI2CChannel_T, uint8_t* output, uint16_t, uint16_t* bytes_copied ) {
            std::memcpy( output, payload, 3U );
            *bytes_copied = 3U;
            return true;
        } );

    EXPECT_TRUE( CONSOLE_Run_I2C_Loopback_S2M( channels, 0x31U, payload, 3U, received,
                                               sizeof( received ), &received_length ) );
    EXPECT_EQ( received_length, 3U );
    EXPECT_EQ( std::memcmp( received, payload, 3U ), 0 );
}

TEST_F( ConsoleI2CTest, LoopbackReturnsFailureOnAsynchronousMasterError )
{
    const char                         payload[] = "bad";
    char                               received[8]{};
    uint16_t                           received_length = 0U;
    const CONSOLEI2CLoopbackChannels_T channels        = { EXEC_I2C_CHANNEL_2, EXEC_I2C_CHANNEL_1 };

    InSequence sequence;
    EXPECT_CALL( mock_exec_i2c, SlaveTransmit( EXEC_I2C_CHANNEL_1, _, 3U ) )
        .WillOnce( Return( true ) );
    EXPECT_CALL( mock_exec_i2c, StartMasterReceive( EXEC_I2C_CHANNEL_2, 0x31U, 3U ) )
        .WillOnce( Return( true ) );
    EXPECT_CALL( mock_exec_i2c, IsQueueComplete( EXEC_I2C_CHANNEL_2 ) ).WillOnce( Return( true ) );
    EXPECT_CALL( mock_exec_i2c, GetAndClearResult( EXEC_I2C_CHANNEL_2 ) )
        .WillOnce( Return( EXEC_I2C_STATUS_ERROR ) );
    EXPECT_CALL( mock_exec_i2c, Receive( _, _, _, _ ) ).Times( 0 );
    ExpectLoopbackRecovery( channels );

    EXPECT_FALSE( CONSOLE_Run_I2C_Loopback_S2M( channels, 0x31U, payload, 3U, received,
                                                sizeof( received ), &received_length ) );
}

TEST_F( ConsoleI2CTest, LoopbackReturnsFailureWhenPhysicalCompletionTimesOut )
{
    const char                         payload[] = "wait";
    char                               received[8]{};
    uint16_t                           received_length = 0U;
    const CONSOLEI2CLoopbackChannels_T channels        = { EXEC_I2C_CHANNEL_1, EXEC_I2C_CHANNEL_2 };

    EXPECT_CALL( mock_exec_i2c, StartSlaveReceive( EXEC_I2C_CHANNEL_2, 4U ) )
        .WillOnce( Return( true ) );
    EXPECT_CALL( mock_exec_i2c, MasterTransmit( EXEC_I2C_CHANNEL_1, 0x32U, _, 4U ) )
        .WillOnce( Return( true ) );
    EXPECT_CALL( mock_exec_i2c, IsQueueComplete( EXEC_I2C_CHANNEL_1 ) )
        .Times( 500 )
        .WillRepeatedly( Return( false ) );
    EXPECT_CALL( mock_exec_i2c, Receive( EXEC_I2C_CHANNEL_2, _, _, _ ) )
        .Times( 500 )
        .WillRepeatedly( []( ExecI2CChannel_T, uint8_t*, uint16_t, uint16_t* bytes_copied ) {
            *bytes_copied = 0U;
            return true;
        } );
    ExpectLoopbackRecovery( channels );

    EXPECT_FALSE( CONSOLE_Run_I2C_Loopback_M2S( channels, 0x32U, payload, 4U, received,
                                                sizeof( received ), &received_length ) );
    EXPECT_EQ( received_length, 0U );
}

TEST_F( ConsoleI2CTest, MasterStartFailureRecoversBothChannelsAfterSlaveWasArmed )
{
    const char                         payload[] = "fail";
    char                               received[8]{};
    uint16_t                           received_length = 0U;
    const CONSOLEI2CLoopbackChannels_T channels        = { EXEC_I2C_CHANNEL_1, EXEC_I2C_CHANNEL_2 };

    InSequence sequence;
    EXPECT_CALL( mock_exec_i2c, StartSlaveReceive( EXEC_I2C_CHANNEL_2, 4U ) )
        .WillOnce( Return( true ) );
    EXPECT_CALL( mock_exec_i2c, MasterTransmit( EXEC_I2C_CHANNEL_1, 0x32U, _, 4U ) )
        .WillOnce( Return( false ) );
    ExpectLoopbackRecovery( channels );

    EXPECT_FALSE( CONSOLE_Run_I2C_Loopback_M2S( channels, 0x32U, payload, 4U, received,
                                                sizeof( received ), &received_length ) );
}

TEST_F( ConsoleI2CTest, MasterReceiveStartFailureRecoversBothChannelsAfterSlaveWasArmed )
{
    const char                         payload[] = "fail";
    char                               received[8]{};
    uint16_t                           received_length = 0U;
    const CONSOLEI2CLoopbackChannels_T channels        = { EXEC_I2C_CHANNEL_2, EXEC_I2C_CHANNEL_1 };

    InSequence sequence;
    EXPECT_CALL( mock_exec_i2c, SlaveTransmit( EXEC_I2C_CHANNEL_1, _, 4U ) )
        .WillOnce( Return( true ) );
    EXPECT_CALL( mock_exec_i2c, StartMasterReceive( EXEC_I2C_CHANNEL_2, 0x31U, 4U ) )
        .WillOnce( Return( false ) );
    ExpectLoopbackRecovery( channels );

    EXPECT_FALSE( CONSOLE_Run_I2C_Loopback_S2M( channels, 0x31U, payload, 4U, received,
                                                sizeof( received ), &received_length ) );
}

TEST_F( ConsoleI2CTest, TimeoutRecoversBothChannelsWhenMasterAlreadyCompleted )
{
    const char                         payload[] = "wait";
    char                               received[8]{};
    uint16_t                           received_length = 0U;
    const CONSOLEI2CLoopbackChannels_T channels        = { EXEC_I2C_CHANNEL_1, EXEC_I2C_CHANNEL_2 };

    InSequence sequence;
    EXPECT_CALL( mock_exec_i2c, StartSlaveReceive( EXEC_I2C_CHANNEL_2, 4U ) )
        .WillOnce( Return( true ) );
    EXPECT_CALL( mock_exec_i2c, MasterTransmit( EXEC_I2C_CHANNEL_1, 0x32U, _, 4U ) )
        .WillOnce( Return( true ) );
    EXPECT_CALL( mock_exec_i2c, IsQueueComplete( EXEC_I2C_CHANNEL_1 ) ).WillOnce( Return( true ) );
    EXPECT_CALL( mock_exec_i2c, GetAndClearResult( EXEC_I2C_CHANNEL_1 ) )
        .WillOnce( Return( EXEC_I2C_STATUS_OK ) );
    EXPECT_CALL( mock_exec_i2c, Receive( EXEC_I2C_CHANNEL_2, _, _, _ ) )
        .Times( 500 )
        .WillRepeatedly( []( ExecI2CChannel_T, uint8_t*, uint16_t, uint16_t* bytes_copied ) {
            *bytes_copied = 0U;
            return true;
        } );
    ExpectLoopbackRecovery( channels );

    EXPECT_FALSE( CONSOLE_Run_I2C_Loopback_M2S( channels, 0x32U, payload, 4U, received,
                                                sizeof( received ), &received_length ) );
    EXPECT_EQ( received_length, 0U );
}
