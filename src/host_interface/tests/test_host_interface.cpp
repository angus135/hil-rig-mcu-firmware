/******************************************************************************
 *  File:       test_host_interface.cpp
 *  Description:
 *      Focused Host Interface protocol integration tests.
 ******************************************************************************/

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

#include "support/transport_pair_harness.hpp"

extern "C"
{
#include "hil_rig_protocol/transport/transport.h"
#include "host_interface_test_access.h"
#include "hw_usb.h"
#include "rtos_config.h"
}

namespace {

constexpr uint32_t kExpectedRetransmitTimeoutMs = 100U;
constexpr uint8_t  kExpectedMaxRetries          = 5U;

HW_USB_Link_State_T               link_state             = HW_USB_LINK_STATE_DISCONNECTED;
uint32_t                          discard_transmit_calls = 0U;
uint32_t                          usb_transmit_calls     = 0U;
bool                              disconnect_after_usb_output_accept = false;
std::vector<std::vector<uint8_t>> accepted_usb_output;
std::vector<uint8_t>              usb_receive_bytes;
size_t                            usb_receive_offset = 0U;
TickType_t                        test_ticks         = 0U;

}  // namespace

extern "C" bool HW_USB_Init( void )
{
    return true;
}

extern "C" HW_USB_Link_State_T HW_USB_Get_Link_State( void )
{
    return link_state;
}

extern "C" bool HW_USB_Transmit( const uint8_t* const data, const uint16_t size_bytes )
{
    ++usb_transmit_calls;
    accepted_usb_output.emplace_back( data, data + size_bytes );

    if ( disconnect_after_usb_output_accept )
    {
        link_state = HW_USB_LINK_STATE_DISCONNECTED;
    }

    return true;
}

extern "C" void HW_USB_Discard_Transmit_Data( void )
{
    ++discard_transmit_calls;
    accepted_usb_output.clear();
}

extern "C" void HW_USB_Receive_From_ISR( uint8_t*, uint32_t* )
{
}

extern "C" uint32_t HW_USB_Receive( uint8_t* const destination, const uint32_t max_size_bytes )
{
    const size_t bytes_available = usb_receive_bytes.size() - usb_receive_offset;
    const size_t bytes_to_copy = std::min( bytes_available, static_cast<size_t>( max_size_bytes ) );

    if ( bytes_to_copy > 0U )
    {
        std::memcpy( destination, usb_receive_bytes.data() + usb_receive_offset, bytes_to_copy );
    }
    usb_receive_offset += bytes_to_copy;
    return static_cast<uint32_t>( bytes_to_copy );
}

extern "C" uint32_t HW_USB_Receive_With_Timeout( uint8_t*, uint32_t, uint32_t )
{
    return 0U;
}

extern "C" uint32_t HW_USB_Get_Receive_Stream_Used_Bytes( void )
{
    return static_cast<uint32_t>( usb_receive_bytes.size() - usb_receive_offset );
}

extern "C" uint32_t HW_USB_Get_Receive_Stream_Dropped_Bytes( void )
{
    return 0U;
}

extern "C" uint32_t HW_USB_Get_Receive_Stream_Free_Bytes( void )
{
    return 0U;
}

extern "C" void HW_USB_Monitor_Process( void )
{
}

extern "C" TickType_t xTaskGetTickCount( void )
{
    return test_ticks;
}

extern "C" void vTaskDelayUntil( TickType_t*, TickType_t )
{
}

class HostInterfaceTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        link_state                         = HW_USB_LINK_STATE_DISCONNECTED;
        discard_transmit_calls             = 0U;
        usb_transmit_calls                 = 0U;
        disconnect_after_usb_output_accept = false;
        accepted_usb_output.clear();
        usb_receive_bytes.clear();
        usb_receive_offset = 0U;
        test_ticks         = 0U;
    }
};

TEST_F( HostInterfaceTest, ProtocolInitConfiguresBoundedReliableDelivery )
{
    HIL_Transport_Config_T config = {};

    HOST_INTERFACE_Test_Access_Get_Transport_Config( &config );

    EXPECT_EQ( kExpectedRetransmitTimeoutMs, config.retransmit_timeout_ms );
    EXPECT_EQ( kExpectedMaxRetries, config.max_retries );
    EXPECT_EQ( HIL_TRANSPORT_SESSION_SEED_INVALID, config.session_seed );
    EXPECT_EQ( 0U, config.connection_timeout_ms );
}

TEST_F( HostInterfaceTest, ApplicationDecodeStorageMeetsPublicAlignmentRequirement )
{
    EXPECT_TRUE( HOST_INTERFACE_Test_Access_Application_Decode_Storage_Is_Aligned() );
}

TEST_F( HostInterfaceTest, FirstDisconnectedObservationDiscardsAbandonedUSBOutput )
{
    HOST_INTERFACE_Test_Access_Observe_Disconnected_Link();

    EXPECT_EQ( 1U, discard_transmit_calls );
}

TEST_F( HostInterfaceTest, LinkDropDuringOutputAcceptanceDiscardsOutputBeforeReconnect )
{
    hil_rig_protocol::test::TransportTestEndpoint host{};
    const auto                                    host_initialization = host.InitializeConnected(
        hil_rig_protocol::test::TransportTestEndpointConfig::Host(
            UINT64_C( 0x1234 ), 10U, kExpectedRetransmitTimeoutMs, kExpectedMaxRetries ),
        test_ticks );
    ASSERT_EQ( HIL_TRANSPORT_STATUS_OK, host_initialization );
    ASSERT_EQ( HIL_TRANSPORT_STATUS_OK, host.Process( test_ticks ) );

    const auto host_output = host.PeekOutput();
    ASSERT_EQ( HIL_TRANSPORT_STATUS_OK, host_output.status );
    ASSERT_EQ( HIL_TRANSPORT_STATUS_OK, host.CommitOutput( test_ticks ) );

    usb_receive_bytes                  = host_output.bytes;
    link_state                         = HW_USB_LINK_STATE_CONNECTED;
    disconnect_after_usb_output_accept = true;
    HOST_INTERFACE_Test_Access_Reset_Protocol();

    HOST_INTERFACE_Test_Access_Process_Once();

    ASSERT_EQ( 1U, usb_transmit_calls );
    EXPECT_EQ( 1U, discard_transmit_calls );
    EXPECT_TRUE( accepted_usb_output.empty() );

    const uint32_t output_count_before_reconnect = usb_transmit_calls;
    link_state                                   = HW_USB_LINK_STATE_CONNECTED;
    disconnect_after_usb_output_accept           = false;
    HOST_INTERFACE_Test_Access_Process_Once();

    EXPECT_EQ( output_count_before_reconnect, usb_transmit_calls );
    EXPECT_TRUE( accepted_usb_output.empty() );
}

TEST_F( HostInterfaceTest, MissingAcknowledgementRetransmitsThenRecoversAfterRetryExhaustion )
{
    HIL_Transport_Config_T                       config = {};
    hil_rig_protocol::test::TransportPairHarness pair{};

    HOST_INTERFACE_Test_Access_Get_Transport_Config( &config );
    const auto initialization = pair.InitializeConnected(
        hil_rig_protocol::test::TransportTestEndpointConfig::Host(
            UINT64_C( 0x1234 ), 10U, kExpectedRetransmitTimeoutMs, kExpectedMaxRetries ),
        hil_rig_protocol::test::TransportTestEndpointConfig::Rig(
            config.initial_reliable_sequence, config.retransmit_timeout_ms, config.max_retries ) );
    ASSERT_EQ( hil_rig_protocol::test::TransportTestHarnessStatus::Ok,
               initialization.harness_status );
    ASSERT_TRUE( initialization.host_status.has_value() );
    ASSERT_TRUE( initialization.rig_status.has_value() );
    ASSERT_EQ( HIL_TRANSPORT_STATUS_OK, *initialization.host_status );
    ASSERT_EQ( HIL_TRANSPORT_STATUS_OK, *initialization.rig_status );

    const auto establishment = pair.EstablishCleanSession();
    ASSERT_EQ( hil_rig_protocol::test::TransportTestHarnessStatus::Ok,
               establishment.harness_status );
    ASSERT_TRUE( establishment.transport_status.has_value() );
    ASSERT_EQ( HIL_TRANSPORT_STATUS_OK, *establishment.transport_status );
    ASSERT_EQ( HIL_TRANSPORT_STATUS_NOT_READY, pair.Host().DrainEvents().terminal_status );
    ASSERT_EQ( HIL_TRANSPORT_STATUS_NOT_READY, pair.Rig().DrainEvents().terminal_status );

    const std::vector<uint8_t> payload = { 0xA1U, 0xB2U, 0xC3U };
    ASSERT_EQ( HIL_TRANSPORT_STATUS_OK, pair.Host().SubmitApplication( payload ) );

    uint32_t now = 1000U;
    pair.SetHostTime( now );
    const auto initial = pair.Link().AcceptOutput( pair.Host(), now );
    ASSERT_EQ( hil_rig_protocol::test::TransportTestHarnessStatus::Ok, initial.harness_status );
    ASSERT_TRUE( initial.transport_status.has_value() );
    ASSERT_EQ( HIL_TRANSPORT_STATUS_OK, *initial.transport_status );
    ASSERT_TRUE( initial.handle.has_value() );
    ASSERT_TRUE( pair.Link().QueueAcceptedForDelivery( *initial.handle ) );
    const auto first_delivery = pair.Link().DeliverReady( pair.Rig() );
    ASSERT_TRUE( first_delivery.transport_status.has_value() );
    ASSERT_EQ( HIL_TRANSPORT_STATUS_OK, *first_delivery.transport_status );
    ASSERT_EQ( HIL_TRANSPORT_STATUS_OK, pair.Rig().ReadApplication().status );

    const auto lost_ack = pair.Link().AcceptOutput( pair.Rig(), now );
    ASSERT_TRUE( lost_ack.transport_status.has_value() );
    ASSERT_EQ( HIL_TRANSPORT_STATUS_OK, *lost_ack.transport_status );
    ASSERT_TRUE( lost_ack.handle.has_value() );
    ASSERT_TRUE( pair.Link().DropAccepted( *lost_ack.handle ) );

    now += kExpectedRetransmitTimeoutMs;
    pair.SetHostTime( now );
    ASSERT_EQ( HIL_TRANSPORT_STATUS_OK, pair.ProcessHost() );
    const auto retry = pair.Link().AcceptOutput( pair.Host(), now );
    ASSERT_TRUE( retry.transport_status.has_value() );
    ASSERT_EQ( HIL_TRANSPORT_STATUS_OK, *retry.transport_status );
    ASSERT_TRUE( retry.handle.has_value() );
    ASSERT_TRUE( pair.Link().QueueAcceptedForDelivery( *retry.handle ) );
    const auto retry_delivery = pair.Link().DeliverReady( pair.Rig() );
    ASSERT_TRUE( retry_delivery.transport_status.has_value() );
    ASSERT_EQ( HIL_TRANSPORT_STATUS_OK, *retry_delivery.transport_status );
    ASSERT_EQ( HIL_TRANSPORT_STATUS_NOT_READY, pair.Rig().ReadApplication().status );

    const auto replacement_ack = pair.Link().AcceptOutput( pair.Rig(), now );
    ASSERT_TRUE( replacement_ack.transport_status.has_value() );
    ASSERT_EQ( HIL_TRANSPORT_STATUS_OK, *replacement_ack.transport_status );
    ASSERT_TRUE( replacement_ack.handle.has_value() );
    ASSERT_TRUE( pair.Link().QueueAcceptedForDelivery( *replacement_ack.handle ) );
    const auto acknowledgement_delivery = pair.Link().DeliverReady( pair.Host() );
    ASSERT_TRUE( acknowledgement_delivery.transport_status.has_value() );
    ASSERT_EQ( HIL_TRANSPORT_STATUS_OK, *acknowledgement_delivery.transport_status );

    const auto delivery_confirmed = pair.Host().ReadEvent();
    ASSERT_EQ( HIL_TRANSPORT_STATUS_OK, delivery_confirmed.status );
    EXPECT_EQ( HIL_TRANSPORT_EVENT_DELIVERY_CONFIRMED, delivery_confirmed.event.type );

    ASSERT_EQ( HIL_TRANSPORT_STATUS_OK, pair.Host().SubmitApplication( payload ) );
    pair.SetHostTime( now );
    ASSERT_TRUE( pair.Link().AcceptOutput( pair.Host(), now ).handle.has_value() );

    for ( uint8_t retry_count = 0U; retry_count < kExpectedMaxRetries; ++retry_count )
    {
        now += kExpectedRetransmitTimeoutMs;
        pair.SetHostTime( now );
        ASSERT_EQ( HIL_TRANSPORT_STATUS_OK, pair.ProcessHost() );
        ASSERT_TRUE( pair.Link().AcceptOutput( pair.Host(), now ).handle.has_value() );
    }

    now += kExpectedRetransmitTimeoutMs;
    pair.SetHostTime( now );
    EXPECT_EQ( HIL_TRANSPORT_STATUS_DELIVERY_FAILED, pair.ProcessHost() );
    EXPECT_EQ( HIL_TRANSPORT_SESSION_STATE_RECOVERING,
               pair.Host().GetStatus().snapshot.session_state );
    EXPECT_EQ( 0U, pair.Host().GetStatus().snapshot.reliable_delivery_pending );
}
