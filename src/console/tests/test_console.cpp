/******************************************************************************
 *  File:       test_console.cpp
 *  Author:     HIL-RIG Firmware Team
 *  Created:    06-Dec-2025
 *
 *  Description:
 *      Focused tests for the rewritten classic-CAN console namespace.
 *
 *      The existing console test target compiles console_can.c as C and supplies
 *      execution-layer link seams from this C++ fixture. This validates parsing
 *      and frame/config construction without adding a second console test suite
 *      or exercising unrelated console transport buffering.
 ******************************************************************************/

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

extern "C"
{
#include "command_helpers.h"
#include "console.h"
#include "exec_can.h"
}

using ::testing::_;
using ::testing::Invoke;
using ::testing::Return;
using ::testing::StrictMock;

/**-----------------------------------------------------------------------------
 *  Execution-Layer Test Double
 *------------------------------------------------------------------------------
 */

class MockExecCan
{
public:
    MOCK_METHOD( HwCanResult_T, Configure,
                 ( HwCanChannel_T channel, const HwCanConfig_T* config ) );
    MOCK_METHOD( HwCanResult_T, Deconfigure, ( HwCanChannel_T channel ) );
    MOCK_METHOD( HwCanResult_T, Recover, ( HwCanChannel_T channel ) );
    MOCK_METHOD( HwCanResult_T, Transmit,
                 ( HwCanChannel_T channel, const HwCanFrame_T* frames, uint32_t frame_count ) );
    MOCK_METHOD( HwCanResult_T, Receive,
                 ( HwCanChannel_T channel, HwCanRxFrame_T* destination, uint32_t capacity,
                   uint32_t* frames_read ) );
    MOCK_METHOD( bool, IsTransmissionComplete, ( HwCanChannel_T channel ) );
    MOCK_METHOD( HwCanResult_T, GetStatus, ( HwCanChannel_T channel, HwCanStatus_T* status ) );
};

static MockExecCan* g_mock_exec_can = nullptr;
static std::string  g_console_output;

extern "C" HwCanResult_T EXEC_CAN_Configure_Channel( HwCanChannel_T       channel,
                                                     const HwCanConfig_T* config )
{
    return g_mock_exec_can->Configure( channel, config );
}

extern "C" HwCanResult_T EXEC_CAN_Deconfigure_Channel( HwCanChannel_T channel )
{
    return g_mock_exec_can->Deconfigure( channel );
}

extern "C" HwCanResult_T EXEC_CAN_Recover_Channel( HwCanChannel_T channel )
{
    return g_mock_exec_can->Recover( channel );
}

extern "C" HwCanResult_T EXEC_CAN_Transmit( HwCanChannel_T channel, const HwCanFrame_T* frames,
                                            uint32_t frame_count )
{
    return g_mock_exec_can->Transmit( channel, frames, frame_count );
}

extern "C" HwCanResult_T EXEC_CAN_Receive( HwCanChannel_T channel, HwCanRxFrame_T* destination,
                                           uint32_t capacity, uint32_t* frames_read )
{
    return g_mock_exec_can->Receive( channel, destination, capacity, frames_read );
}

extern "C" bool EXEC_CAN_Is_Transmission_Complete( HwCanChannel_T channel )
{
    return g_mock_exec_can->IsTransmissionComplete( channel );
}

extern "C" HwCanResult_T EXEC_CAN_Get_Status( HwCanChannel_T channel, HwCanStatus_T* status )
{
    return g_mock_exec_can->GetStatus( channel, status );
}

extern "C" void CONSOLE_Printf( const char* format, ... )
{
    char buffer[512] = {};

    va_list arguments;
    va_start( arguments, format );
    int written = std::vsnprintf( buffer, sizeof( buffer ), format, arguments );
    va_end( arguments );

    if ( written > 0 )
    {
        g_console_output.append( buffer );
    }
}

/**-----------------------------------------------------------------------------
 *  Fixture
 *------------------------------------------------------------------------------
 */

class ConsoleCanTest : public ::testing::Test
{
protected:
    StrictMock<MockExecCan> mock_exec_can;

    void SetUp() override
    {
        g_mock_exec_can = &mock_exec_can;
        g_console_output.clear();
    }

    void TearDown() override
    {
        g_mock_exec_can = nullptr;
    }
};

/**-----------------------------------------------------------------------------
 *  Focused CAN Command Tests
 *------------------------------------------------------------------------------
 */

TEST_F( ConsoleCanTest, TxBuildsSpecificStandardIdentifierDlcAndBinaryPayload )
{
    HwCanFrame_T captured = {};

    EXPECT_CALL( mock_exec_can, Transmit( HW_CAN_CHANNEL_1, _, 1U ) )
        .WillOnce( Invoke( [&]( HwCanChannel_T, const HwCanFrame_T* frames, uint32_t ) {
            captured = frames[0];
            return HW_CAN_RESULT_OK;
        } ) );

    char  command[] = "can";
    char  tx[]      = "tx";
    char  channel[] = "ch1";
    char  format[]  = "std";
    char  id[]      = "0x321";
    char  byte0[]   = "0A";
    char  byte1[]   = "ff";
    char  byte2[]   = "10";
    char* argv[]    = { command, tx, channel, format, id, byte0, byte1, byte2 };

    CONSOLE_CAN_Command_Handler( 8U, argv );

    EXPECT_EQ( captured.identifier, 0x321U );
    EXPECT_EQ( captured.dlc, 3U );
    EXPECT_FALSE( captured.is_extended_id );
    EXPECT_FALSE( captured.is_remote_frame );
    EXPECT_EQ( captured.data[0], 0x0AU );
    EXPECT_EQ( captured.data[1], 0xFFU );
    EXPECT_EQ( captured.data[2], 0x10U );
}

TEST_F( ConsoleCanTest, TxRejectsOutOfRangeStandardIdentifierBeforeExecutionCall )
{
    EXPECT_CALL( mock_exec_can, Transmit( _, _, _ ) ).Times( 0 );

    char  command[] = "can";
    char  tx[]      = "tx";
    char  channel[] = "ch1";
    char  format[]  = "std";
    char  id[]      = "0x800";
    char* argv[]    = { command, tx, channel, format, id };

    CONSOLE_CAN_Command_Handler( 5U, argv );
}

TEST_F( ConsoleCanTest, BusOffReportsThatTheAcceptedFrameRemainsQueued )
{
    EXPECT_CALL( mock_exec_can, Transmit( HW_CAN_CHANNEL_1, _, 1U ) )
        .WillOnce( Return( HW_CAN_RESULT_BUS_OFF ) );

    char  command[] = "can";
    char  tx[]      = "tx";
    char  channel[] = "ch1";
    char  format[]  = "std";
    char  id[]      = "0x321";
    char* argv[]    = { command, tx, channel, format, id };

    CONSOLE_CAN_Command_Handler( 5U, argv );

    EXPECT_NE( g_console_output.find( "remains queued" ), std::string::npos );
    EXPECT_EQ( g_console_output.find( "Failed to queue" ), std::string::npos );
}

TEST_F( ConsoleCanTest, RemoteRequestPreservesExtendedIdentifierAndRequestedDlc )
{
    HwCanFrame_T captured = {};

    EXPECT_CALL( mock_exec_can, Transmit( HW_CAN_CHANNEL_2, _, 1U ) )
        .WillOnce( Invoke( [&]( HwCanChannel_T, const HwCanFrame_T* frames, uint32_t ) {
            captured = frames[0];
            return HW_CAN_RESULT_OK;
        } ) );

    char  command[] = "can";
    char  rtr[]     = "rtr";
    char  channel[] = "ch2";
    char  format[]  = "ext";
    char  id[]      = "0x1ABCDE";
    char  dlc[]     = "8";
    char* argv[]    = { command, rtr, channel, format, id, dlc };

    CONSOLE_CAN_Command_Handler( 6U, argv );

    EXPECT_EQ( captured.identifier, 0x1ABCDEU );
    EXPECT_EQ( captured.dlc, 8U );
    EXPECT_TRUE( captured.is_extended_id );
    EXPECT_TRUE( captured.is_remote_frame );
}

TEST_F( ConsoleCanTest, ConfigureBuildsExtendedMaskFilterAndPriorityPolicy )
{
    HwCanConfig_T captured_config = {};
    HwCanFilter_T captured_filter = {};

    EXPECT_CALL( mock_exec_can, Configure( HW_CAN_CHANNEL_2, _ ) )
        .WillOnce( Invoke( [&]( HwCanChannel_T, const HwCanConfig_T* config ) {
            captured_config = *config;
            if ( config->filters != nullptr )
            {
                captured_filter = config->filters[0];
            }
            return HW_CAN_RESULT_OK;
        } ) );
    EXPECT_CALL( mock_exec_can, GetStatus( HW_CAN_CHANNEL_2, _ ) )
        .WillOnce( Return( HW_CAN_RESULT_HARDWARE_ERROR ) );

    char  command[]   = "can";
    char  configure[] = "configure";
    char  channel[]   = "ch2";
    char  bitrate[]   = "500000";
    char  mode[]      = "loopback";
    char  retry[]     = "one_shot";
    char  priority[]  = "identifier_priority";
    char  format[]    = "ext";
    char  id[]        = "0x1ABCDE";
    char  mask[]      = "0x1FFFFF";
    char* argv[]      = { command, configure, channel, bitrate, mode,
                          retry,   priority,  format,  id,      mask };

    CONSOLE_CAN_Command_Handler( 10U, argv );

    EXPECT_EQ( captured_config.bitrate, 500000U );
    EXPECT_EQ( captured_config.sample_point_permill, HW_CAN_DEFAULT_SAMPLE_POINT_PERMILL );
    EXPECT_EQ( captured_config.mode, HW_CAN_MODE_LOOPBACK );
    EXPECT_EQ( captured_config.tx_priority, HW_CAN_TX_PRIORITY_IDENTIFIER );
    EXPECT_FALSE( captured_config.automatic_retransmission );
    EXPECT_TRUE( captured_config.automatic_bus_off_recovery );
    EXPECT_EQ( captured_config.filter_policy, HW_CAN_FILTER_CONFIGURED );
    EXPECT_EQ( captured_config.filter_count, 1U );

    EXPECT_EQ( captured_filter.mode, HW_CAN_FILTER_MODE_MASK );
    EXPECT_EQ( captured_filter.first.identifier, 0x1ABCDEU );
    EXPECT_TRUE( captured_filter.first.is_extended_id );
    EXPECT_EQ( captured_filter.identifier_mask, 0x1FFFFFU );
    EXPECT_FALSE( captured_filter.match_remote_frame );
}

TEST_F( ConsoleCanTest, ReceivePassesTheRequestedBoundToExecutionLayer )
{
    EXPECT_CALL( mock_exec_can, Receive( HW_CAN_CHANNEL_2, _, 8U, _ ) )
        .WillOnce( Invoke(
            []( HwCanChannel_T, HwCanRxFrame_T* destination, uint32_t, uint32_t* frames_read ) {
                destination[0].frame.identifier = 0x55U;
                destination[0].frame.dlc        = 1U;
                destination[0].frame.data[0]    = 0x00U;
                *frames_read                    = 1U;
                return HW_CAN_RESULT_OK;
            } ) );

    char  command[] = "can";
    char  rx[]      = "rx";
    char  channel[] = "ch2";
    char  count[]   = "8";
    char* argv[]    = { command, rx, channel, count };

    CONSOLE_CAN_Command_Handler( 4U, argv );
}
