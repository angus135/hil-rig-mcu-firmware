/******************************************************************************
 *  File:       test_console_can.cpp
 *
 *  Description:
 *      Focused tests for CAN diagnostic console parsing and execution-API
 *      routing.
 ******************************************************************************/

#include <gtest/gtest.h>

extern "C"
{
#include "console.h"
#include "console_can.h"
#include "exec_can.h"
}

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static std::string                    console_output;
static std::vector<EXEC_CAN_Packet_T> transmitted_packets[2];
static EXEC_CAN_Result_T              transmit_results[2];
static uint16_t                       transmit_counts[2];
static EXEC_CAN_Packet_T              received_packets[2][4];
static uint16_t                       received_counts[2];
static EXEC_CAN_Result_T              receive_results[2];
static uint32_t                       dropped_counts[2];
static uint16_t                       configure_counts[2];
static bool                           configured_enabled[2];
static uint32_t                       configured_bitrates[2];
static uint16_t                       configured_banks[2];
static uint16_t                       configured_ids[2];
static uint16_t                       configured_masks[2];
static EXEC_CAN_Result_T              configure_results[2];
static uint16_t                       start_counts[2];
static uint16_t                       stop_counts[2];
static EXEC_CAN_Result_T              start_results[2];
static EXEC_CAN_Result_T              stop_results[2];

static size_t ChannelIndex( EXEC_CAN_Channel_T channel )
{
    return channel == EXEC_CAN_CHANNEL_1 ? 0U : 1U;
}

extern "C" void CONSOLE_Printf( const char* format, ... )
{
    char    output[256] = {};
    va_list args;
    va_start( args, format );
    vsnprintf( output, sizeof( output ), format, args );
    va_end( args );
    console_output += output;
}

extern "C" EXEC_CAN_Result_T EXEC_CAN_Transmit( EXEC_CAN_Channel_T      channel,
                                                const EXEC_CAN_Packet_T packets[],
                                                uint16_t                packet_count )
{
    size_t index = ChannelIndex( channel );
    transmit_counts[index]++;
    transmitted_packets[index].assign( packets, packets + packet_count );
    return transmit_results[index];
}

extern "C" EXEC_CAN_Result_T EXEC_CAN_Receive( EXEC_CAN_Channel_T channel,
                                               EXEC_CAN_Packet_T destination[], uint16_t capacity,
                                               uint16_t* packets_read )
{
    size_t   index  = ChannelIndex( channel );
    uint16_t copied = std::min( received_counts[index], capacity );
    std::copy_n( received_packets[index], copied, destination );
    *packets_read = copied;
    return receive_results[index];
}

extern "C" uint32_t EXEC_CAN_Get_Rx_Dropped_Count( EXEC_CAN_Channel_T channel )
{
    return dropped_counts[ChannelIndex( channel )];
}

extern "C" EXEC_CAN_Result_T EXEC_CAN_Configure_Channel( EXEC_CAN_Channel_T channel,
                                                         EXEC_CAN_Config_T  configuration )
{
    size_t index = ChannelIndex( channel );
    configure_counts[index]++;
    configured_enabled[index]  = configuration.is_enabled;
    configured_bitrates[index] = configuration.bitrate;
    configured_banks[index]    = configuration.filter_bank;
    configured_ids[index]      = configuration.filter_id;
    configured_masks[index]    = configuration.filter_mask;
    return configure_results[index];
}

extern "C" EXEC_CAN_Result_T EXEC_CAN_Start_Channel( EXEC_CAN_Channel_T channel )
{
    size_t index = ChannelIndex( channel );
    start_counts[index]++;
    return start_results[index];
}

extern "C" EXEC_CAN_Result_T EXEC_CAN_Stop_Channel( EXEC_CAN_Channel_T channel )
{
    size_t index = ChannelIndex( channel );
    stop_counts[index]++;
    return stop_results[index];
}

class ConsoleCANTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        console_output.clear();
        transmitted_packets[0].clear();
        transmitted_packets[1].clear();
        std::memset( received_packets, 0, sizeof( received_packets ) );
        std::memset( transmit_counts, 0, sizeof( transmit_counts ) );
        std::memset( received_counts, 0, sizeof( received_counts ) );
        std::memset( dropped_counts, 0, sizeof( dropped_counts ) );
        std::memset( configure_counts, 0, sizeof( configure_counts ) );
        std::memset( configured_enabled, 0, sizeof( configured_enabled ) );
        std::memset( configured_bitrates, 0, sizeof( configured_bitrates ) );
        std::memset( configured_banks, 0, sizeof( configured_banks ) );
        std::memset( configured_ids, 0, sizeof( configured_ids ) );
        std::memset( configured_masks, 0, sizeof( configured_masks ) );
        std::memset( start_counts, 0, sizeof( start_counts ) );
        std::memset( stop_counts, 0, sizeof( stop_counts ) );
        transmit_results[0] = transmit_results[1] = EXEC_CAN_RESULT_OK;
        receive_results[0] = receive_results[1] = EXEC_CAN_RESULT_OK;
        configure_results[0] = configure_results[1] = EXEC_CAN_RESULT_OK;
        start_results[0] = start_results[1] = EXEC_CAN_RESULT_OK;
        stop_results[0] = stop_results[1] = EXEC_CAN_RESULT_OK;
    }
};

TEST_F( ConsoleCANTest, ShortConfigurationCommandPrintsUsageWithoutConfiguring )
{
    char  can[]    = "can";
    char  config[] = "config";
    char* argv[]   = { can, config };

    CONSOLE_CAN_Command_Handler( 2U, argv );

    EXPECT_EQ( configure_counts[0], 0U );
    EXPECT_EQ( configure_counts[1], 0U );
    EXPECT_NE( console_output.find( "can config" ), std::string::npos );
}

TEST_F( ConsoleCANTest, LongTransmitCommandIsRejectedBeforeTransmitting )
{
    char  can[]  = "can";
    char  tx[]   = "tx";
    char  ch[]   = "1";
    char  id[]   = "0x123";
    char  data[] = "a";
    char* argv[] = { can, tx, ch, id, data, id, data, id, data, id, data, id, data };

    CONSOLE_CAN_Command_Handler( 13U, argv );

    EXPECT_EQ( transmit_counts[0], 0U );
    EXPECT_NE( console_output.find( "Too many CAN frames" ), std::string::npos );
}

TEST_F( ConsoleCANTest, InvalidFilterBankCombinationIsRejectedBeforeConfiguration )
{
    char  can[]     = "can";
    char  config[]  = "config";
    char  channel[] = "2";
    char  bank[]    = "13";
    char  id[]      = "0x123";
    char  mask[]    = "0x7FF";
    char* argv[]    = { can, config, channel, bank, id, mask };

    CONSOLE_CAN_Command_Handler( 6U, argv );

    EXPECT_EQ( configure_counts[0], 0U );
    EXPECT_EQ( configure_counts[1], 0U );
    EXPECT_NE( console_output.find( "Invalid CAN configuration" ), std::string::npos );
}

TEST_F( ConsoleCANTest, ValidConfigurationRoutesSelectedChannel )
{
    char  can[]     = "can";
    char  config[]  = "config";
    char  channel[] = "2";
    char  bank[]    = "14";
    char  id[]      = "0x123";
    char  mask[]    = "0x7FF";
    char* argv[]    = { can, config, channel, bank, id, mask };

    CONSOLE_CAN_Command_Handler( 6U, argv );

    EXPECT_EQ( configure_counts[0], 0U );
    EXPECT_EQ( configure_counts[1], 1U );
    EXPECT_TRUE( configured_enabled[1] );
    EXPECT_EQ( configured_bitrates[1], 1000000U );
    EXPECT_EQ( configured_banks[1], 14U );
    EXPECT_EQ( configured_ids[1], 0x123U );
    EXPECT_EQ( configured_masks[1], 0x7FFU );
    EXPECT_NE( console_output.find( "CAN2 configured" ), std::string::npos );
}

TEST_F( ConsoleCANTest, StartAndStopRouteSelectedChannel )
{
    char  can[]        = "can";
    char  start[]      = "start";
    char  stop[]       = "stop";
    char  channel[]    = "1";
    char* start_argv[] = { can, start, channel };
    char* stop_argv[]  = { can, stop, channel };

    CONSOLE_CAN_Command_Handler( 3U, start_argv );
    CONSOLE_CAN_Command_Handler( 3U, stop_argv );

    EXPECT_EQ( start_counts[0], 1U );
    EXPECT_EQ( stop_counts[0], 1U );
    EXPECT_NE( console_output.find( "CAN1 started" ), std::string::npos );
    EXPECT_NE( console_output.find( "CAN1 stopped" ), std::string::npos );
}

TEST_F( ConsoleCANTest, MultiFrameTransmitSetsDeterministicIDsAndDLCs )
{
    char  can[]      = "can";
    char  tx[]       = "tx";
    char  channel[]  = "2";
    char  id1[]      = "0x123";
    char  payload1[] = "abc";
    char  id2[]      = "0x7FF";
    char  payload2[] = "Z";
    char* argv[]     = { can, tx, channel, id1, payload1, id2, payload2 };

    CONSOLE_CAN_Command_Handler( 7U, argv );

    ASSERT_EQ( transmitted_packets[1].size(), 2U );
    EXPECT_EQ( transmitted_packets[1][0].id, 0x123U );
    EXPECT_EQ( transmitted_packets[1][0].dlc, 3U );
    EXPECT_EQ( transmitted_packets[1][0].data[0], 'a' );
    EXPECT_EQ( transmitted_packets[1][0].data[3], 0U );
    EXPECT_EQ( transmitted_packets[1][1].id, 0x7FFU );
    EXPECT_EQ( transmitted_packets[1][1].dlc, 1U );
    EXPECT_EQ( transmitted_packets[1][1].data[0], 'Z' );
    EXPECT_EQ( transmitted_packets[1][1].data[1], 0U );
    EXPECT_EQ( transmit_counts[1], 1U );
    EXPECT_NE( console_output.find( "Started 2 CAN frame(s) on channel 2" ), std::string::npos );
}

TEST_F( ConsoleCANTest, InvalidTransmitPairDoesNotPartiallyTransmitBatch )
{
    char  can[]      = "can";
    char  tx[]       = "tx";
    char  channel[]  = "1";
    char  id1[]      = "0x123";
    char  payload1[] = "abc";
    char  id2[]      = "0x800";
    char  payload2[] = "def";
    char* argv[]     = { can, tx, channel, id1, payload1, id2, payload2 };

    CONSOLE_CAN_Command_Handler( 7U, argv );

    EXPECT_EQ( transmit_counts[0], 0U );
}

TEST_F( ConsoleCANTest, ReceivePrintsIDAndDLCBoundedHexPayload )
{
    received_packets[0][0].id      = 0x7FFU;
    received_packets[0][0].dlc     = 3U;
    received_packets[0][0].data[0] = 0x00U;
    received_packets[0][0].data[1] = 0xFFU;
    received_packets[0][0].data[2] = 0x41U;
    received_packets[0][0].data[3] = 0x42U;
    received_counts[0]             = 1U;
    char  can[]                    = "can";
    char  rx[]                     = "rx";
    char  channel[]                = "1";
    char* argv[]                   = { can, rx, channel };

    CONSOLE_CAN_Command_Handler( 3U, argv );

    EXPECT_NE( console_output.find( "id: 0x7FF, dlc: 3, data: 00 FF 41" ), std::string::npos );
    EXPECT_EQ( console_output.find( "42" ), std::string::npos );
}
