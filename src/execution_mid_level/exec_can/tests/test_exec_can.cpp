/******************************************************************************
 *  File:       test_exec_can.cpp
 *
 *  Description:
 *      Unit tests for execution-layer CAN routing, validation, conversion,
 *      result mapping, and combined buffered transmission.
 ******************************************************************************/

#include <gtest/gtest.h>

extern "C"
{
#include "exec_can.c"  // NOLINT
}

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

struct ConfigureCall
{
    uint32_t bitrate;
    uint16_t bank;
    uint16_t id;
    uint16_t mask;
};

static ConfigureCall             configure_calls[2];
static uint16_t                  configure_call_count[2];
static HW_CAN_Result_T           configure_results[2];
static HW_CAN_Result_T           start_results[2];
static HW_CAN_Result_T           stop_results[2];
static uint16_t                  start_call_count[2];
static uint16_t                  stop_call_count[2];
static bool                      hardware_configured[2];
static bool                      hardware_started[2];
static HW_CAN_Tx_Status_T        hardware_status[2];
static uint16_t                  status_call_count[2];
static HW_CAN_Result_T           recover_results[2];
static uint16_t                  recover_call_count[2];
static uint32_t                  dropped_counts[2];
static uint16_t                  dropped_call_count[2];
static HW_CAN_Result_T           load_results[2];
static uint16_t                  load_call_count[2];
static HW_CAN_Result_T           trigger_results[2];
static uint16_t                  trigger_call_count[2];
static uint16_t                  cancel_call_count[2];
static std::vector<CAN_Packet_T> hardware_tx_queue[2];
static std::vector<CAN_Packet_T> hardware_rx_queue[2];
static uint16_t                  receive_call_count[2];
static uint16_t                  last_receive_capacity[2];
static const void*               last_hardware_tx_source;
static const void*               last_hardware_rx_destination;

static HW_CAN_Result_T Configure( size_t channel, uint32_t bitrate, uint16_t bank, uint16_t id,
                                  uint16_t mask )
{
    configure_call_count[channel]++;
    configure_calls[channel] = { bitrate, bank, id, mask };
    return configure_results[channel];
}

extern "C" HW_CAN_Result_T HW_CAN_Configure1( uint32_t bitrate, uint16_t bank, uint16_t id,
                                              uint16_t mask )
{
    return Configure( 0U, bitrate, bank, id, mask );
}

extern "C" HW_CAN_Result_T HW_CAN_Configure2( uint32_t bitrate, uint16_t bank, uint16_t id,
                                              uint16_t mask )
{
    return Configure( 1U, bitrate, bank, id, mask );
}

extern "C" HW_CAN_Result_T HW_CAN_Start1( void )
{
    start_call_count[0]++;
    return start_results[0];
}

extern "C" HW_CAN_Result_T HW_CAN_Start2( void )
{
    start_call_count[1]++;
    return start_results[1];
}

extern "C" HW_CAN_Result_T HW_CAN_Stop1( void )
{
    stop_call_count[0]++;
    return stop_results[0];
}

extern "C" HW_CAN_Result_T HW_CAN_Stop2( void )
{
    stop_call_count[1]++;
    return stop_results[1];
}

extern "C" bool HW_CAN_Is_Configured1( void )
{
    return hardware_configured[0];
}

extern "C" bool HW_CAN_Is_Configured2( void )
{
    return hardware_configured[1];
}

extern "C" bool HW_CAN_Is_Started1( void )
{
    return hardware_started[0];
}

extern "C" bool HW_CAN_Is_Started2( void )
{
    return hardware_started[1];
}

extern "C" HW_CAN_Tx_Status_T HW_CAN_Tx_Status1( void )
{
    status_call_count[0]++;
    return hardware_status[0];
}

extern "C" HW_CAN_Tx_Status_T HW_CAN_Tx_Status2( void )
{
    status_call_count[1]++;
    return hardware_status[1];
}

extern "C" HW_CAN_Result_T HW_CAN_Recover1( void )
{
    recover_call_count[0]++;
    return recover_results[0];
}

extern "C" HW_CAN_Result_T HW_CAN_Recover2( void )
{
    recover_call_count[1]++;
    return recover_results[1];
}

extern "C" uint32_t HW_CAN_Rx_Dropped_Count1( void )
{
    dropped_call_count[0]++;
    return dropped_counts[0];
}

extern "C" uint32_t HW_CAN_Rx_Dropped_Count2( void )
{
    dropped_call_count[1]++;
    return dropped_counts[1];
}

static HW_CAN_Result_T Load( size_t channel, CAN_Packet_T source[], uint16_t count )
{
    load_call_count[channel]++;
    last_hardware_tx_source = source;
    if ( load_results[channel] == HW_CAN_RESULT_OK )
    {
        hardware_tx_queue[channel].assign( source, source + count );
    }
    return load_results[channel];
}

extern "C" HW_CAN_Result_T HW_CAN_Tx_Buffer_Write1( CAN_Packet_T source[], uint16_t count )
{
    return Load( 0U, source, count );
}

extern "C" HW_CAN_Result_T HW_CAN_Tx_Buffer_Write2( CAN_Packet_T source[], uint16_t count )
{
    return Load( 1U, source, count );
}

extern "C" HW_CAN_Result_T HW_CAN_Tx_Trigger1( void )
{
    trigger_call_count[0]++;
    return trigger_results[0];
}

extern "C" HW_CAN_Result_T HW_CAN_Tx_Trigger2( void )
{
    trigger_call_count[1]++;
    return trigger_results[1];
}

extern "C" void HW_CAN_Tx_Buffer_Cancel1( void )
{
    cancel_call_count[0]++;
    hardware_tx_queue[0].clear();
}

extern "C" void HW_CAN_Tx_Buffer_Cancel2( void )
{
    cancel_call_count[1]++;
    hardware_tx_queue[1].clear();
}

static uint16_t Receive( size_t channel, CAN_Packet_T destination[], uint16_t capacity )
{
    receive_call_count[channel]++;
    last_receive_capacity[channel] = capacity;
    last_hardware_rx_destination   = destination;
    uint16_t count =
        static_cast<uint16_t>( std::min<size_t>( hardware_rx_queue[channel].size(), capacity ) );
    std::copy_n( hardware_rx_queue[channel].begin(), count, destination );
    hardware_rx_queue[channel].erase( hardware_rx_queue[channel].begin(),
                                      hardware_rx_queue[channel].begin() + count );
    return count;
}

extern "C" uint16_t HW_CAN_Rx_Buffer_Read1( CAN_Packet_T destination[], uint16_t capacity )
{
    return Receive( 0U, destination, capacity );
}

extern "C" uint16_t HW_CAN_Rx_Buffer_Read2( CAN_Packet_T destination[], uint16_t capacity )
{
    return Receive( 1U, destination, capacity );
}

class ExecCANTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        std::memset( configure_calls, 0, sizeof( configure_calls ) );
        std::memset( configure_call_count, 0, sizeof( configure_call_count ) );
        std::memset( configure_results, 0, sizeof( configure_results ) );
        std::memset( start_call_count, 0, sizeof( start_call_count ) );
        std::memset( stop_call_count, 0, sizeof( stop_call_count ) );
        std::memset( hardware_configured, 0, sizeof( hardware_configured ) );
        std::memset( hardware_started, 0, sizeof( hardware_started ) );
        std::memset( exec_can_state, 0, sizeof( exec_can_state ) );
        std::memset( status_call_count, 0, sizeof( status_call_count ) );
        std::memset( recover_call_count, 0, sizeof( recover_call_count ) );
        std::memset( dropped_counts, 0, sizeof( dropped_counts ) );
        std::memset( dropped_call_count, 0, sizeof( dropped_call_count ) );
        std::memset( load_call_count, 0, sizeof( load_call_count ) );
        std::memset( trigger_call_count, 0, sizeof( trigger_call_count ) );
        std::memset( cancel_call_count, 0, sizeof( cancel_call_count ) );
        std::memset( receive_call_count, 0, sizeof( receive_call_count ) );
        std::memset( last_receive_capacity, 0, sizeof( last_receive_capacity ) );
        hardware_status[0] = hardware_status[1] = HW_CAN_TX_STATUS_IDLE;
        start_results[0] = start_results[1] = HW_CAN_RESULT_OK;
        stop_results[0] = stop_results[1] = HW_CAN_RESULT_OK;
        recover_results[0] = recover_results[1] = HW_CAN_RESULT_OK;
        load_results[0] = load_results[1] = HW_CAN_RESULT_OK;
        trigger_results[0] = trigger_results[1] = HW_CAN_RESULT_OK;
        hardware_tx_queue[0].clear();
        hardware_tx_queue[1].clear();
        hardware_rx_queue[0].clear();
        hardware_rx_queue[1].clear();
        last_hardware_tx_source      = nullptr;
        last_hardware_rx_destination = nullptr;
    }
};

TEST_F( ExecCANTest, ConfigureRoutesBothChannelsAndMapsResults )
{
    configure_results[0]       = HW_CAN_RESULT_OK;
    configure_results[1]       = HW_CAN_RESULT_FILTER_ERROR;
    EXEC_CAN_Config_T channel1 = { true, 500000U, 13U, 0x123U, 0x7FFU };
    EXEC_CAN_Config_T channel2 = { true, 250000U, 14U, 0x456U, 0x700U };

    EXPECT_EQ( EXEC_CAN_Configure_Channel( EXEC_CAN_CHANNEL_1, &channel1 ), EXEC_CAN_RESULT_OK );
    EXPECT_EQ( EXEC_CAN_Configure_Channel( EXEC_CAN_CHANNEL_2, &channel2 ),
               EXEC_CAN_RESULT_FILTER_ERROR );

    EXPECT_EQ( configure_call_count[0], 1U );
    EXPECT_EQ( configure_call_count[1], 1U );
    EXPECT_EQ( configure_calls[0].bitrate, 500000U );
    EXPECT_EQ( configure_calls[0].bank, 13U );
    EXPECT_EQ( configure_calls[0].id, 0x123U );
    EXPECT_EQ( configure_calls[0].mask, 0x7FFU );
    EXPECT_EQ( configure_calls[1].bitrate, 250000U );
    EXPECT_EQ( configure_calls[1].bank, 14U );
    EXPECT_TRUE( EXEC_CAN_Is_Configured( EXEC_CAN_CHANNEL_1 ) );
    EXPECT_FALSE( EXEC_CAN_Is_Started( EXEC_CAN_CHANNEL_1 ) );
    EXPECT_FALSE( EXEC_CAN_Is_Configured( EXEC_CAN_CHANNEL_2 ) );
}

TEST_F( ExecCANTest, ConfigurationResultMappingCoversEveryHardwareCode )
{
    const std::array<HW_CAN_Result_T, 8> hardware_results = {
        HW_CAN_RESULT_OK,           HW_CAN_RESULT_ERROR,          HW_CAN_RESULT_BUSY,
        HW_CAN_RESULT_EMPTY,        HW_CAN_RESULT_NOT_CONFIGURED, HW_CAN_RESULT_NOT_STARTED,
        HW_CAN_RESULT_TIMING_ERROR, HW_CAN_RESULT_FILTER_ERROR,
    };
    const std::array<EXEC_CAN_Result_T, 8> execution_results = {
        EXEC_CAN_RESULT_OK,           EXEC_CAN_RESULT_ERROR,          EXEC_CAN_RESULT_BUSY,
        EXEC_CAN_RESULT_EMPTY,        EXEC_CAN_RESULT_NOT_CONFIGURED, EXEC_CAN_RESULT_NOT_STARTED,
        EXEC_CAN_RESULT_TIMING_ERROR, EXEC_CAN_RESULT_FILTER_ERROR,
    };
    for ( size_t i = 0U; i < hardware_results.size(); i++ )
    {
        configure_results[0]            = hardware_results[i];
        EXEC_CAN_Config_T configuration = { true, 500000U, 0U, 0U, 0U };
        EXPECT_EQ( EXEC_CAN_Configure_Channel( EXEC_CAN_CHANNEL_1, &configuration ),
                   execution_results[i] );
    }
}

TEST_F( ExecCANTest, DisabledConfigurationStopsStartedChannelAndClearsState )
{
    exec_can_state[EXEC_CAN_CHANNEL_1] = { true, true };
    EXEC_CAN_Config_T configuration    = { false, 0U, 0U, 0U, 0U };

    EXPECT_EQ( EXEC_CAN_Configure_Channel( EXEC_CAN_CHANNEL_1, &configuration ),
               EXEC_CAN_RESULT_OK );
    EXPECT_EQ( stop_call_count[0], 1U );
    EXPECT_FALSE( EXEC_CAN_Is_Configured( EXEC_CAN_CHANNEL_1 ) );
    EXPECT_FALSE( EXEC_CAN_Is_Started( EXEC_CAN_CHANNEL_1 ) );
}

TEST_F( ExecCANTest, DisabledConfigurationPreservesStateWhenStopFails )
{
    exec_can_state[EXEC_CAN_CHANNEL_2] = { true, true };
    stop_results[1]                    = HW_CAN_RESULT_BUSY;
    EXEC_CAN_Config_T configuration    = { false, 0U, 0U, 0U, 0U };

    EXPECT_EQ( EXEC_CAN_Configure_Channel( EXEC_CAN_CHANNEL_2, &configuration ),
               EXEC_CAN_RESULT_BUSY );
    EXPECT_TRUE( EXEC_CAN_Is_Configured( EXEC_CAN_CHANNEL_2 ) );
    EXPECT_TRUE( EXEC_CAN_Is_Started( EXEC_CAN_CHANNEL_2 ) );
}

TEST_F( ExecCANTest, StartAndStopEnforceLifecycleAndUpdateStateOnlyOnSuccess )
{
    EXPECT_EQ( EXEC_CAN_Start_Channel( EXEC_CAN_CHANNEL_1 ), EXEC_CAN_RESULT_NOT_CONFIGURED );
    EXPECT_EQ( start_call_count[0], 0U );

    exec_can_state[EXEC_CAN_CHANNEL_1] = { true, false };
    EXPECT_EQ( EXEC_CAN_Start_Channel( EXEC_CAN_CHANNEL_1 ), EXEC_CAN_RESULT_OK );
    EXPECT_TRUE( EXEC_CAN_Is_Started( EXEC_CAN_CHANNEL_1 ) );
    EXPECT_EQ( start_call_count[0], 1U );
    EXPECT_EQ( EXEC_CAN_Start_Channel( EXEC_CAN_CHANNEL_1 ), EXEC_CAN_RESULT_BUSY );

    stop_results[0] = HW_CAN_RESULT_ERROR;
    EXPECT_EQ( EXEC_CAN_Stop_Channel( EXEC_CAN_CHANNEL_1 ), EXEC_CAN_RESULT_ERROR );
    EXPECT_TRUE( EXEC_CAN_Is_Started( EXEC_CAN_CHANNEL_1 ) );

    stop_results[0] = HW_CAN_RESULT_OK;
    EXPECT_EQ( EXEC_CAN_Stop_Channel( EXEC_CAN_CHANNEL_1 ), EXEC_CAN_RESULT_OK );
    EXPECT_FALSE( EXEC_CAN_Is_Started( EXEC_CAN_CHANNEL_1 ) );
    EXPECT_EQ( EXEC_CAN_Stop_Channel( EXEC_CAN_CHANNEL_1 ), EXEC_CAN_RESULT_NOT_STARTED );
}

TEST_F( ExecCANTest, StartFailureLeavesConfiguredChannelStopped )
{
    exec_can_state[EXEC_CAN_CHANNEL_2] = { true, false };
    start_results[1]                   = HW_CAN_RESULT_ERROR;

    EXPECT_EQ( EXEC_CAN_Start_Channel( EXEC_CAN_CHANNEL_2 ), EXEC_CAN_RESULT_ERROR );
    EXPECT_TRUE( EXEC_CAN_Is_Configured( EXEC_CAN_CHANNEL_2 ) );
    EXPECT_FALSE( EXEC_CAN_Is_Started( EXEC_CAN_CHANNEL_2 ) );
}

TEST_F( ExecCANTest, CombinedTransmitRoutesBothChannelsAndConvertsPackets )
{
    EXEC_CAN_Packet_T packets[2] = {
        { 0x123U, 3U, { 1U, 2U, 3U, 0xA5U, 0xA5U, 0xA5U, 0xA5U, 0xA5U } },
        { 0x7FFU, 1U, { 9U, 0xA5U, 0xA5U, 0xA5U, 0xA5U, 0xA5U, 0xA5U, 0xA5U } },
    };

    EXPECT_EQ( EXEC_CAN_Transmit( EXEC_CAN_CHANNEL_1, packets, 2U ), EXEC_CAN_RESULT_OK );
    EXPECT_EQ( load_call_count[0], 1U );
    EXPECT_EQ( trigger_call_count[0], 1U );
    EXPECT_EQ( cancel_call_count[0], 0U );
    ASSERT_EQ( hardware_tx_queue[0].size(), 2U );
    EXPECT_NE( last_hardware_tx_source, packets );
    EXPECT_EQ( hardware_tx_queue[0][0].id, 0x123U );
    EXPECT_EQ( hardware_tx_queue[0][0].dlc, 3U );
    EXPECT_EQ( hardware_tx_queue[0][0].data[2], 3U );
    EXPECT_EQ( hardware_tx_queue[0][0].data[3], 0U );

    EXPECT_EQ( EXEC_CAN_Transmit( EXEC_CAN_CHANNEL_2, packets, 1U ), EXEC_CAN_RESULT_OK );
    EXPECT_EQ( load_call_count[1], 1U );
    EXPECT_EQ( trigger_call_count[1], 1U );
    ASSERT_EQ( hardware_tx_queue[1].size(), 1U );
    EXPECT_EQ( hardware_tx_queue[1][0].id, 0x123U );
}

TEST_F( ExecCANTest, LoadFailureDoesNotTriggerOrCancel )
{
    EXEC_CAN_Packet_T packet = { 1U, 0U, {} };
    load_results[0]          = HW_CAN_RESULT_BUSY;

    EXPECT_EQ( EXEC_CAN_Transmit( EXEC_CAN_CHANNEL_1, &packet, 1U ), EXEC_CAN_RESULT_BUSY );
    EXPECT_EQ( load_call_count[0], 1U );
    EXPECT_EQ( trigger_call_count[0], 0U );
    EXPECT_EQ( cancel_call_count[0], 0U );
}

TEST_F( ExecCANTest, TriggerFailureDiscardsLoadedBatch )
{
    EXEC_CAN_Packet_T packet = { 1U, 1U, { 0x55U } };
    trigger_results[1]       = HW_CAN_RESULT_ERROR;

    EXPECT_EQ( EXEC_CAN_Transmit( EXEC_CAN_CHANNEL_2, &packet, 1U ), EXEC_CAN_RESULT_ERROR );
    EXPECT_EQ( load_call_count[1], 1U );
    EXPECT_EQ( trigger_call_count[1], 1U );
    EXPECT_EQ( cancel_call_count[1], 1U );
    EXPECT_TRUE( hardware_tx_queue[1].empty() );
}

TEST_F( ExecCANTest, TransmitRejectsInvalidArgumentsBeforeHardwareCalls )
{
    EXEC_CAN_Packet_T valid = { 0x7FFU, 8U, {} };
    EXPECT_EQ( EXEC_CAN_Transmit( EXEC_CAN_CHANNEL_1, nullptr, 1U ),
               EXEC_CAN_RESULT_INVALID_ARGUMENT );
    EXPECT_EQ( EXEC_CAN_Transmit( EXEC_CAN_CHANNEL_1, &valid, 0U ),
               EXEC_CAN_RESULT_INVALID_ARGUMENT );

    std::array<EXEC_CAN_Packet_T, EXEC_CAN_MAX_BATCH_SIZE + 1U> oversized{};
    EXPECT_EQ( EXEC_CAN_Transmit( EXEC_CAN_CHANNEL_1, oversized.data(), oversized.size() ),
               EXEC_CAN_RESULT_INVALID_ARGUMENT );

    EXEC_CAN_Packet_T bad_id = { 0x800U, 0U, {} };
    EXPECT_EQ( EXEC_CAN_Transmit( EXEC_CAN_CHANNEL_1, &bad_id, 1U ),
               EXEC_CAN_RESULT_INVALID_ARGUMENT );
    EXEC_CAN_Packet_T bad_dlc = { 1U, 9U, {} };
    EXPECT_EQ( EXEC_CAN_Transmit( EXEC_CAN_CHANNEL_1, &bad_dlc, 1U ),
               EXEC_CAN_RESULT_INVALID_ARGUMENT );
    EXPECT_EQ( load_call_count[0], 0U );
    EXPECT_EQ( trigger_call_count[0], 0U );
}

TEST_F( ExecCANTest, ReceiveCopiesOnlyUpToCapacityAndConsumesPacketsExactlyOnce )
{
    hardware_rx_queue[0] = {
        { 0x100U, 2U, { 1U, 2U, 0xA5U, 0xA5U, 0xA5U, 0xA5U, 0xA5U, 0xA5U } },
        { 0x400U, 1U, { 3U, 0xA5U, 0xA5U, 0xA5U, 0xA5U, 0xA5U, 0xA5U, 0xA5U } },
        { 0x7FFU, 0U, {} },
    };
    EXEC_CAN_Packet_T destination[3];
    std::memset( destination, 0xCC, sizeof( destination ) );
    uint16_t read = 0U;

    EXPECT_EQ( EXEC_CAN_Receive( EXEC_CAN_CHANNEL_1, destination, 2U, &read ), EXEC_CAN_RESULT_OK );
    EXPECT_EQ( read, 2U );
    EXPECT_NE( last_hardware_rx_destination, destination );
    EXPECT_EQ( destination[0].id, 0x100U );
    EXPECT_EQ( destination[0].dlc, 2U );
    EXPECT_EQ( destination[0].data[1], 2U );
    EXPECT_EQ( destination[0].data[2], 0U );
    EXPECT_EQ( destination[1].id, 0x400U );
    EXPECT_EQ( destination[2].id, 0xCCCCU );
    ASSERT_EQ( hardware_rx_queue[0].size(), 1U );

    EXPECT_EQ( EXEC_CAN_Receive( EXEC_CAN_CHANNEL_1, destination, 3U, &read ), EXEC_CAN_RESULT_OK );
    EXPECT_EQ( read, 1U );
    EXPECT_EQ( destination[0].id, 0x7FFU );
    EXPECT_TRUE( hardware_rx_queue[0].empty() );

    EXPECT_EQ( EXEC_CAN_Receive( EXEC_CAN_CHANNEL_1, destination, 3U, &read ), EXEC_CAN_RESULT_OK );
    EXPECT_EQ( read, 0U );
}

TEST_F( ExecCANTest, ReceiveRoutesChannelTwoAndBoundsHardwareTemporaryStorage )
{
    hardware_rx_queue[1] = { { 0x222U, 1U, { 0x5AU } } };
    std::array<EXEC_CAN_Packet_T, EXEC_CAN_MAX_BATCH_SIZE + 4U> destination{};
    uint16_t                                                    read = 0U;

    EXPECT_EQ(
        EXEC_CAN_Receive( EXEC_CAN_CHANNEL_2, destination.data(), destination.size(), &read ),
        EXEC_CAN_RESULT_OK );
    EXPECT_EQ( receive_call_count[1], 1U );
    EXPECT_EQ( last_receive_capacity[1], EXEC_CAN_MAX_BATCH_SIZE );
    EXPECT_EQ( read, 1U );
    EXPECT_EQ( destination[0].id, 0x222U );
}

TEST_F( ExecCANTest, ReceiveValidatesPointersAndZeroCapacity )
{
    EXEC_CAN_Packet_T destination{};
    uint16_t          read = 9U;

    EXPECT_EQ( EXEC_CAN_Receive( EXEC_CAN_CHANNEL_1, nullptr, 1U, &read ),
               EXEC_CAN_RESULT_INVALID_ARGUMENT );
    EXPECT_EQ( EXEC_CAN_Receive( EXEC_CAN_CHANNEL_1, &destination, 1U, nullptr ),
               EXEC_CAN_RESULT_INVALID_ARGUMENT );
    EXPECT_EQ( receive_call_count[0], 0U );

    EXPECT_EQ( EXEC_CAN_Receive( EXEC_CAN_CHANNEL_1, &destination, 0U, &read ),
               EXEC_CAN_RESULT_OK );
    EXPECT_EQ( read, 0U );
    EXPECT_EQ( receive_call_count[0], 0U );
}

TEST_F( ExecCANTest, StatusRecoveryAndDroppedCountsRouteBothChannelsAndMapTypes )
{
    exec_can_state[0]      = { true, true };
    exec_can_state[1]      = { true, true };
    hardware_configured[0] = hardware_configured[1] = true;
    hardware_started[0] = hardware_started[1] = true;
    hardware_status[0]                        = HW_CAN_TX_STATUS_ACTIVE;
    hardware_status[1]                        = HW_CAN_TX_STATUS_COMPLETE;
    recover_results[0]                        = HW_CAN_RESULT_BUSY;
    recover_results[1]                        = HW_CAN_RESULT_EMPTY;
    dropped_counts[0]                         = 4U;
    dropped_counts[1]                         = 7U;

    EXPECT_EQ( EXEC_CAN_Get_Tx_Status( EXEC_CAN_CHANNEL_1 ), EXEC_CAN_TX_STATUS_ACTIVE );
    EXPECT_EQ( EXEC_CAN_Get_Tx_Status( EXEC_CAN_CHANNEL_2 ), EXEC_CAN_TX_STATUS_COMPLETE );
    EXPECT_EQ( EXEC_CAN_Recover( EXEC_CAN_CHANNEL_1 ), EXEC_CAN_RESULT_BUSY );
    EXPECT_EQ( EXEC_CAN_Recover( EXEC_CAN_CHANNEL_2 ), EXEC_CAN_RESULT_EMPTY );
    EXPECT_EQ( EXEC_CAN_Get_Rx_Dropped_Count( EXEC_CAN_CHANNEL_1 ), 4U );
    EXPECT_EQ( EXEC_CAN_Get_Rx_Dropped_Count( EXEC_CAN_CHANNEL_2 ), 7U );
    EXPECT_EQ( status_call_count[0], 1U );
    EXPECT_EQ( status_call_count[1], 1U );
    EXPECT_EQ( recover_call_count[0], 1U );
    EXPECT_EQ( recover_call_count[1], 1U );
    EXPECT_EQ( dropped_call_count[0], 1U );
    EXPECT_EQ( dropped_call_count[1], 1U );
}

TEST_F( ExecCANTest, RecoveryFailureSynchronizesExecutionLifecycleWithHardware )
{
    exec_can_state[EXEC_CAN_CHANNEL_1] = { true, true };
    recover_results[0]                 = HW_CAN_RESULT_ERROR;
    hardware_configured[0]             = true;
    hardware_started[0]                = false;

    EXPECT_EQ( EXEC_CAN_Recover( EXEC_CAN_CHANNEL_1 ), EXEC_CAN_RESULT_ERROR );
    EXPECT_TRUE( EXEC_CAN_Is_Configured( EXEC_CAN_CHANNEL_1 ) );
    EXPECT_FALSE( EXEC_CAN_Is_Started( EXEC_CAN_CHANNEL_1 ) );
}

TEST_F( ExecCANTest, HardwareResultAndStatusMappingCoversAllValues )
{
    const std::array<HW_CAN_Result_T, 4> hardware_results = {
        HW_CAN_RESULT_OK,
        HW_CAN_RESULT_ERROR,
        HW_CAN_RESULT_BUSY,
        HW_CAN_RESULT_EMPTY,
    };
    const std::array<EXEC_CAN_Result_T, 4> execution_results = {
        EXEC_CAN_RESULT_OK,
        EXEC_CAN_RESULT_ERROR,
        EXEC_CAN_RESULT_BUSY,
        EXEC_CAN_RESULT_EMPTY,
    };
    for ( size_t i = 0U; i < hardware_results.size(); i++ )
    {
        exec_can_state[0]      = { true, true };
        hardware_configured[0] = true;
        hardware_started[0]    = true;
        recover_results[0]     = hardware_results[i];
        EXPECT_EQ( EXEC_CAN_Recover( EXEC_CAN_CHANNEL_1 ), execution_results[i] );
    }

    const std::array<HW_CAN_Tx_Status_T, 4> hardware_statuses = {
        HW_CAN_TX_STATUS_IDLE,
        HW_CAN_TX_STATUS_ACTIVE,
        HW_CAN_TX_STATUS_COMPLETE,
        HW_CAN_TX_STATUS_ERROR,
    };
    const std::array<EXEC_CAN_Tx_Status_T, 4> execution_statuses = {
        EXEC_CAN_TX_STATUS_IDLE,
        EXEC_CAN_TX_STATUS_ACTIVE,
        EXEC_CAN_TX_STATUS_COMPLETE,
        EXEC_CAN_TX_STATUS_ERROR,
    };
    for ( size_t i = 0U; i < hardware_statuses.size(); i++ )
    {
        hardware_status[1] = hardware_statuses[i];
        EXPECT_EQ( EXEC_CAN_Get_Tx_Status( EXEC_CAN_CHANNEL_2 ), execution_statuses[i] );
    }
}

TEST_F( ExecCANTest, InvalidChannelNeverCallsHardware )
{
    const EXEC_CAN_Channel_T invalid = EXEC_CAN_CHANNEL_COUNT;
    EXEC_CAN_Packet_T        packet{};
    uint16_t                 read = 0U;

    EXEC_CAN_Config_T configuration = { true, 500000U, 0U, 0U, 0U };
    EXPECT_EQ( EXEC_CAN_Configure_Channel( invalid, &configuration ),
               EXEC_CAN_RESULT_INVALID_ARGUMENT );
    EXPECT_EQ( EXEC_CAN_Start_Channel( invalid ), EXEC_CAN_RESULT_INVALID_ARGUMENT );
    EXPECT_EQ( EXEC_CAN_Stop_Channel( invalid ), EXEC_CAN_RESULT_INVALID_ARGUMENT );
    EXPECT_FALSE( EXEC_CAN_Is_Configured( invalid ) );
    EXPECT_FALSE( EXEC_CAN_Is_Started( invalid ) );
    EXPECT_EQ( EXEC_CAN_Transmit( invalid, &packet, 1U ), EXEC_CAN_RESULT_INVALID_ARGUMENT );
    EXPECT_EQ( EXEC_CAN_Receive( invalid, &packet, 1U, &read ), EXEC_CAN_RESULT_INVALID_ARGUMENT );
    EXPECT_EQ( EXEC_CAN_Get_Tx_Status( invalid ), EXEC_CAN_TX_STATUS_INVALID_CHANNEL );
    EXPECT_EQ( EXEC_CAN_Recover( invalid ), EXEC_CAN_RESULT_INVALID_ARGUMENT );
    EXPECT_EQ( EXEC_CAN_Get_Rx_Dropped_Count( invalid ), 0U );

    EXPECT_EQ( configure_call_count[0] + configure_call_count[1], 0U );
    EXPECT_EQ( load_call_count[0] + load_call_count[1], 0U );
    EXPECT_EQ( receive_call_count[0] + receive_call_count[1], 0U );
    EXPECT_EQ( status_call_count[0] + status_call_count[1], 0U );
    EXPECT_EQ( recover_call_count[0] + recover_call_count[1], 0U );
    EXPECT_EQ( dropped_call_count[0] + dropped_call_count[1], 0U );
}
