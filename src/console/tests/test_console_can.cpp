/******************************************************************************
 *  File:       test_console_can.cpp
 *
 *  Description:
 *      Focused tests for CAN diagnostic console parsing and API routing.
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

static std::string               console_output;
static std::vector<CAN_Packet_T> loaded_packets1;
static std::vector<CAN_Packet_T> loaded_packets2;
static CAN_Packet_T              received_packets1[4];
static CAN_Packet_T              received_packets2[4];
static uint16_t                  received_count1;
static uint16_t                  received_count2;
static uint32_t                  dropped_count1;
static uint32_t                  dropped_count2;
static HW_CAN_Result_T           load_result1;
static HW_CAN_Result_T           load_result2;
static HW_CAN_Result_T           trigger_result1;
static HW_CAN_Result_T           trigger_result2;
static uint16_t                  trigger_count1;
static uint16_t                  trigger_count2;
static uint16_t                  configure_count1;
static uint16_t                  configure_count2;
static uint32_t                  configured_bitrate1;
static uint32_t                  configured_bitrate2;
static uint16_t                  configured_bank1;
static uint16_t                  configured_bank2;
static uint16_t                  configured_id1;
static uint16_t                  configured_id2;
static uint16_t                  configured_mask1;
static uint16_t                  configured_mask2;
static int                       configure_result1;
static int                       configure_result2;

extern "C" void CONSOLE_Printf( const char* format, ... )
{
    char    output[256] = {};
    va_list args;
    va_start( args, format );
    vsnprintf( output, sizeof( output ), format, args );
    va_end( args );
    console_output += output;
}

extern "C" HW_CAN_Result_T EXEC_CAN_Load_Tx1( CAN_Packet_T source[], uint16_t length )
{
    loaded_packets1.assign( source, source + length );
    return load_result1;
}

extern "C" HW_CAN_Result_T EXEC_CAN_Load_Tx2( CAN_Packet_T source[], uint16_t length )
{
    loaded_packets2.assign( source, source + length );
    return load_result2;
}

extern "C" HW_CAN_Result_T EXEC_CAN_Tx_Trigger1( void )
{
    trigger_count1++;
    return trigger_result1;
}

extern "C" HW_CAN_Result_T EXEC_CAN_Tx_Trigger2( void )
{
    trigger_count2++;
    return trigger_result2;
}

extern "C" uint16_t EXEC_CAN_Rx_Buffer_Read1( CAN_Packet_T dest[], uint16_t capacity )
{
    uint16_t copied = std::min( received_count1, capacity );
    std::copy_n( received_packets1, copied, dest );
    return copied;
}

extern "C" uint16_t EXEC_CAN_Rx_Buffer_Read2( CAN_Packet_T dest[], uint16_t capacity )
{
    uint16_t copied = std::min( received_count2, capacity );
    std::copy_n( received_packets2, copied, dest );
    return copied;
}

extern "C" uint32_t EXEC_CAN_Rx_Dropped_Count1( void )
{
    return dropped_count1;
}

extern "C" uint32_t EXEC_CAN_Rx_Dropped_Count2( void )
{
    return dropped_count2;
}

extern "C" int HW_CAN_Configure1( uint32_t bitrate, uint16_t filter_bank, uint16_t filter_id,
                                  uint16_t filter_mask )
{
    configure_count1++;
    configured_bitrate1 = bitrate;
    configured_bank1    = filter_bank;
    configured_id1      = filter_id;
    configured_mask1    = filter_mask;
    return configure_result1;
}

extern "C" int HW_CAN_Configure2( uint32_t bitrate, uint16_t filter_bank, uint16_t filter_id,
                                  uint16_t filter_mask )
{
    configure_count2++;
    configured_bitrate2 = bitrate;
    configured_bank2    = filter_bank;
    configured_id2      = filter_id;
    configured_mask2    = filter_mask;
    return configure_result2;
}

class ConsoleCANTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        console_output.clear();
        loaded_packets1.clear();
        loaded_packets2.clear();
        memset( received_packets1, 0, sizeof( received_packets1 ) );
        memset( received_packets2, 0, sizeof( received_packets2 ) );
        received_count1     = 0U;
        received_count2     = 0U;
        dropped_count1      = 0U;
        dropped_count2      = 0U;
        load_result1        = HW_CAN_RESULT_OK;
        load_result2        = HW_CAN_RESULT_OK;
        trigger_result1     = HW_CAN_RESULT_OK;
        trigger_result2     = HW_CAN_RESULT_OK;
        trigger_count1      = 0U;
        trigger_count2      = 0U;
        configure_count1    = 0U;
        configure_count2    = 0U;
        configured_bitrate1 = 0U;
        configured_bitrate2 = 0U;
        configured_bank1    = 0U;
        configured_bank2    = 0U;
        configured_id1      = 0U;
        configured_id2      = 0U;
        configured_mask1    = 0U;
        configured_mask2    = 0U;
        configure_result1   = 0;
        configure_result2   = 0;
    }
};

TEST_F( ConsoleCANTest, ShortConfigurationCommandPrintsUsageWithoutConfiguring )
{
    char  can[]    = "can";
    char  config[] = "config";
    char* argv[]   = { can, config };

    CONSOLE_CAN_Command_Handler( 2U, argv );

    EXPECT_EQ( configure_count1, 0U );
    EXPECT_EQ( configure_count2, 0U );
    EXPECT_NE( console_output.find( "can config" ), std::string::npos );
}

TEST_F( ConsoleCANTest, LongTransmitCommandIsRejectedBeforeLoading )
{
    char  can[]  = "can";
    char  tx[]   = "tx";
    char  ch[]   = "1";
    char  id[]   = "0x123";
    char  data[] = "a";
    char* argv[] = { can, tx, ch, id, data, id, data, id, data, id, data, id, data };

    CONSOLE_CAN_Command_Handler( 13U, argv );

    EXPECT_TRUE( loaded_packets1.empty() );
    EXPECT_EQ( trigger_count1, 0U );
    EXPECT_NE( console_output.find( "Too many CAN frames" ), std::string::npos );
}

TEST_F( ConsoleCANTest, InvalidFilterBankCombinationIsRejectedBeforeConfiguration )
{
    char  can[]    = "can";
    char  config[] = "config";
    char  bank1[]  = "0";
    char  bank2[]  = "13";
    char  id[]     = "0x123";
    char  mask[]   = "0x7FF";
    char* argv[]   = { can, config, bank1, bank2, id, mask };

    CONSOLE_CAN_Command_Handler( 6U, argv );

    EXPECT_EQ( configure_count1, 0U );
    EXPECT_EQ( configure_count2, 0U );
    EXPECT_NE( console_output.find( "Invalid CAN configuration" ), std::string::npos );
}

TEST_F( ConsoleCANTest, ValidDualChannelConfigurationUsesSeparateBanks )
{
    char  can[]    = "can";
    char  config[] = "config";
    char  bank1[]  = "13";
    char  bank2[]  = "14";
    char  id[]     = "0x123";
    char  mask[]   = "0x7FF";
    char* argv[]   = { can, config, bank1, bank2, id, mask };

    CONSOLE_CAN_Command_Handler( 6U, argv );

    EXPECT_EQ( configure_count1, 1U );
    EXPECT_EQ( configure_count2, 1U );
    EXPECT_EQ( configured_bitrate1, 1000000U );
    EXPECT_EQ( configured_bitrate2, 1000000U );
    EXPECT_EQ( configured_bank1, 13U );
    EXPECT_EQ( configured_bank2, 14U );
    EXPECT_EQ( configured_id1, 0x123U );
    EXPECT_EQ( configured_id2, 0x123U );
    EXPECT_EQ( configured_mask1, 0x7FFU );
    EXPECT_EQ( configured_mask2, 0x7FFU );
    EXPECT_NE( console_output.find( "CAN1 and CAN2 configured" ), std::string::npos );
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

    ASSERT_EQ( loaded_packets2.size(), 2U );
    EXPECT_EQ( loaded_packets2[0].id, 0x123U );
    EXPECT_EQ( loaded_packets2[0].dlc, 3U );
    EXPECT_EQ( loaded_packets2[0].data[0], 'a' );
    EXPECT_EQ( loaded_packets2[0].data[3], 0U );
    EXPECT_EQ( loaded_packets2[1].id, 0x7FFU );
    EXPECT_EQ( loaded_packets2[1].dlc, 1U );
    EXPECT_EQ( loaded_packets2[1].data[0], 'Z' );
    EXPECT_EQ( loaded_packets2[1].data[1], 0U );
    EXPECT_EQ( trigger_count2, 1U );
    EXPECT_NE( console_output.find( "Started 2 CAN frame(s)" ), std::string::npos );
    EXPECT_EQ( console_output.find( "Transmitted" ), std::string::npos );
}

TEST_F( ConsoleCANTest, InvalidTransmitPairDoesNotPartiallyLoadBatch )
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

    EXPECT_TRUE( loaded_packets1.empty() );
    EXPECT_EQ( trigger_count1, 0U );
}

TEST_F( ConsoleCANTest, ReceivePrintsIDAndDLCBoundedHexPayload )
{
    received_packets1[0].id      = 0x7FFU;
    received_packets1[0].dlc     = 3U;
    received_packets1[0].data[0] = 0x00U;
    received_packets1[0].data[1] = 0xFFU;
    received_packets1[0].data[2] = 0x41U;
    received_packets1[0].data[3] = 0x42U;
    received_count1              = 1U;
    char  can[]                  = "can";
    char  rx[]                   = "rx";
    char  channel[]              = "1";
    char* argv[]                 = { can, rx, channel };

    CONSOLE_CAN_Command_Handler( 3U, argv );

    EXPECT_NE( console_output.find( "id: 0x7FF, dlc: 3, data: 00 FF 41" ), std::string::npos );
    EXPECT_EQ( console_output.find( "42" ), std::string::npos );
}
