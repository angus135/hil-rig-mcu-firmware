#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

extern "C"
{
#include "hil_rig_protocol/transport/transport.h"
#include "host_transport.h"
#include "hw_usb.h"
#include "protocol_test_config.h"
#include "protocol_test_harness.h"
}

namespace {
bool                              g_usb_connected     = false;
bool                              g_usb_accept_tx     = true;
uint32_t                          g_discard_calls     = 0U;
uint32_t                          g_usb_receive_calls = 0U;
std::vector<uint8_t>              g_usb_rx;
std::vector<std::vector<uint8_t>> g_tx_attempts;

std::vector<uint8_t> MakeHostInitiate()
{
    alignas( HIL_TRANSPORT_WORKSPACE_ALIGNMENT ) std::array<uint8_t, 4096> workspace{};
    HIL_Transport_Context_T                                                context{};
    HIL_Transport_Config_T                                                 config{};
    HIL_Transport_Storage_T storage{ workspace.data(), workspace.size() };
    std::array<uint8_t, HOST_TRANSPORT_MAX_ENCODED_FRAME_SIZE> output{};
    size_t                                                     output_size = 0U;

    HIL_TRANSPORT_Default_Config( &config );
    config.max_application_message_size = HOST_TRANSPORT_MAX_APPLICATION_MESSAGE_SIZE;
    config.max_encoded_frame_size       = HOST_TRANSPORT_MAX_ENCODED_FRAME_SIZE;
    config.session_seed                 = 0x12345678ULL;
    config.retransmit_timeout_ms        = HOST_TRANSPORT_RETRANSMIT_TIMEOUT_MS;
    config.max_retries                  = HOST_TRANSPORT_MAX_RETRIES;

    EXPECT_EQ( HIL_TRANSPORT_STATUS_OK,
               HIL_TRANSPORT_Init( &context, HIL_TRANSPORT_ROLE_HOST, &config, &storage ) );
    EXPECT_EQ( HIL_TRANSPORT_STATUS_OK, HIL_TRANSPORT_Notify_Link_State(
                                            &context, HIL_TRANSPORT_LINK_STATE_CONNECTED, 0U ) );
    EXPECT_EQ( HIL_TRANSPORT_STATUS_OK,
               HIL_TRANSPORT_Process( &context, 0U, HIL_TRANSPORT_OPERATING_MODE_NORMAL ) );
    EXPECT_EQ( HIL_TRANSPORT_STATUS_OK,
               HIL_TRANSPORT_Peek_Output( &context, output.data(), output.size(), &output_size ) );

    return { output.begin(), output.begin() + static_cast<std::ptrdiff_t>( output_size ) };
}

struct HostPeerHarness
{
    alignas( HIL_TRANSPORT_WORKSPACE_ALIGNMENT ) std::array<uint8_t, 4096> workspace{};
    HIL_Transport_Context_T context{};
    size_t                  next_firmware_output = 0U;
};

bool InitializeHostPeer( HostPeerHarness& peer )
{
    HIL_Transport_Config_T  config{};
    HIL_Transport_Storage_T storage{ peer.workspace.data(), peer.workspace.size() };

    HIL_TRANSPORT_Default_Config( &config );
    config.max_application_message_size = HOST_TRANSPORT_MAX_APPLICATION_MESSAGE_SIZE;
    config.max_encoded_frame_size       = HOST_TRANSPORT_MAX_ENCODED_FRAME_SIZE;
    config.session_seed                 = 0x12345678ULL;
    config.initial_reliable_sequence    = HOST_TRANSPORT_INITIAL_RELIABLE_SEQUENCE;
    config.connection_timeout_ms        = HOST_TRANSPORT_CONNECTION_TIMEOUT_MS;
    config.retransmit_timeout_ms        = HOST_TRANSPORT_RETRANSMIT_TIMEOUT_MS;
    config.max_retries                  = HOST_TRANSPORT_MAX_RETRIES;

    if ( HIL_TRANSPORT_Init( &peer.context, HIL_TRANSPORT_ROLE_HOST, &config, &storage )
         != HIL_TRANSPORT_STATUS_OK )
    {
        return false;
    }

    return HIL_TRANSPORT_Notify_Link_State( &peer.context, HIL_TRANSPORT_LINK_STATE_CONNECTED, 0U )
           == HIL_TRANSPORT_STATUS_OK;
}

bool DrainHostPeerEvents( HostPeerHarness& peer )
{
    for ( size_t event_index = 0U; event_index < 32U; event_index++ )
    {
        HIL_Transport_Event_T        event{};
        const HIL_Transport_Status_T status = HIL_TRANSPORT_Read_Event( &peer.context, &event );

        if ( status == HIL_TRANSPORT_STATUS_NOT_READY )
        {
            return true;
        }
        if ( status != HIL_TRANSPORT_STATUS_OK )
        {
            return false;
        }
    }

    return false;
}

bool QueueHostPeerOutputForFirmware( HostPeerHarness& peer, uint32_t now_ms )
{
    for ( size_t output_index = 0U; output_index < 8U; output_index++ )
    {
        std::array<uint8_t, HOST_TRANSPORT_MAX_ENCODED_FRAME_SIZE> output{};
        size_t                                                     output_size = 0U;
        HIL_Transport_Status_T                                     status =
            HIL_TRANSPORT_Peek_Output( &peer.context, output.data(), output.size(), &output_size );

        if ( status == HIL_TRANSPORT_STATUS_NOT_READY )
        {
            return true;
        }
        if ( status != HIL_TRANSPORT_STATUS_OK )
        {
            return false;
        }

        g_usb_rx.insert( g_usb_rx.end(), output.begin(),
                         output.begin() + static_cast<std::ptrdiff_t>( output_size ) );
        status = HIL_TRANSPORT_Commit_Output( &peer.context, now_ms );
        if ( status != HIL_TRANSPORT_STATUS_OK )
        {
            return false;
        }
    }

    return false;
}

bool DeliverFirmwareOutputToHostPeer( HostPeerHarness& peer )
{
    while ( peer.next_firmware_output < g_tx_attempts.size() )
    {
        const auto& output = g_tx_attempts[peer.next_firmware_output];
        size_t      offset = 0U;
        peer.next_firmware_output++;

        while ( offset < output.size() )
        {
            size_t                       consumed = 0U;
            const HIL_Transport_Status_T status   = HIL_TRANSPORT_Receive_Bytes(
                &peer.context, output.data() + offset, output.size() - offset, &consumed );
            offset += consumed;

            if ( status == HIL_TRANSPORT_STATUS_OK )
            {
                continue;
            }
            if ( status == HIL_TRANSPORT_STATUS_CAPACITY_EXHAUSTED && consumed > 0U )
            {
                continue;
            }
            return false;
        }
    }

    return true;
}

bool ServiceHostPeerAndFirmware( HostPeerHarness& peer, uint32_t now_ms )
{
    HIL_Transport_Status_T status =
        HIL_TRANSPORT_Process( &peer.context, now_ms, HIL_TRANSPORT_OPERATING_MODE_NORMAL );
    if ( status != HIL_TRANSPORT_STATUS_OK && status != HIL_TRANSPORT_STATUS_NOT_READY )
    {
        return false;
    }
    if ( !DrainHostPeerEvents( peer ) || !QueueHostPeerOutputForFirmware( peer, now_ms ) )
    {
        return false;
    }

    HOST_TRANSPORT_Service( now_ms );
    if ( !DeliverFirmwareOutputToHostPeer( peer ) || !DrainHostPeerEvents( peer ) )
    {
        return false;
    }

    size_t consumed = 0U;
    status          = HIL_TRANSPORT_Receive_Bytes( &peer.context, nullptr, 0U, &consumed );
    return status == HIL_TRANSPORT_STATUS_OK || status == HIL_TRANSPORT_STATUS_NOT_READY;
}

bool HostPeerAndFirmwareAreEstablished( HostPeerHarness& peer )
{
    HIL_Transport_Status_Snapshot_T peer_status{};
    if ( HIL_TRANSPORT_Get_Status( &peer.context, &peer_status ) != HIL_TRANSPORT_STATUS_OK )
    {
        return false;
    }

    return peer_status.session_state == HIL_TRANSPORT_SESSION_STATE_ESTABLISHED
           && HOST_TRANSPORT_Get_Diagnostics()->transport_session_state
                  == HIL_TRANSPORT_SESSION_STATE_ESTABLISHED;
}

std::vector<uint8_t> MakeEchoRequest()
{
    return { static_cast<uint8_t>( 'H' ),
             static_cast<uint8_t>( 'R' ),
             static_cast<uint8_t>( 'T' ),
             static_cast<uint8_t>( 'P' ),
             PROTOCOL_TEST_HARNESS_VERSION,
             PROTOCOL_TEST_HARNESS_OPCODE_ECHO_REQUEST,
             0x00U,
             0x00U,
             0x12U,
             0x34U,
             0x56U,
             0x78U,
             0x04U,
             0x00U,
             0x00U,
             0x00U,
             0x00U,
             0x11U,
             0x00U,
             0x22U };
}
}  // namespace

extern "C" bool HW_USB_Transmit( const uint8_t* data, uint16_t size_bytes )
{
    g_tx_attempts.emplace_back( data, data + size_bytes );
    return g_usb_accept_tx;
}

extern "C" uint32_t HW_USB_Receive( uint8_t* destination, uint32_t max_size_bytes )
{
    g_usb_receive_calls++;
    const size_t count = std::min<size_t>( max_size_bytes, g_usb_rx.size() );
    if ( count == 0U )
    {
        return 0U;
    }
    std::memcpy( destination, g_usb_rx.data(), count );
    g_usb_rx.erase( g_usb_rx.begin(), g_usb_rx.begin() + static_cast<std::ptrdiff_t>( count ) );
    return static_cast<uint32_t>( count );
}

extern "C" bool HW_USB_Is_Connected( void )
{
    return g_usb_connected;
}

extern "C" void HW_USB_Discard_Protocol_Buffers( void )
{
    g_discard_calls++;
    g_usb_rx.clear();
}

extern "C" uint32_t HW_USB_Get_Receive_Stream_Dropped_Bytes( void )
{
    return 0U;
}

extern "C" uint32_t HW_USB_Get_Transmit_Buffer_High_Water_Bytes( void )
{
    return 0U;
}

class HostTransportTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        HOST_TRANSPORT_Test_Reset();
        g_usb_connected     = false;
        g_usb_accept_tx     = true;
        g_discard_calls     = 0U;
        g_usb_receive_calls = 0U;
        g_usb_rx.clear();
        g_tx_attempts.clear();
    }
};

TEST_F( HostTransportTest, RequiredWorkspaceFitsAndAlignmentCheckPasses )
{
    ASSERT_TRUE( HOST_TRANSPORT_Init() );
    const auto* diagnostics = HOST_TRANSPORT_Get_Diagnostics();
    EXPECT_EQ( 1U, diagnostics->initialization_successful );
    EXPECT_LE( diagnostics->required_workspace_bytes, diagnostics->available_workspace_bytes );
    EXPECT_EQ( HIL_TRANSPORT_WORKSPACE_ALIGNMENT, diagnostics->workspace_alignment_bytes );
}

TEST_F( HostTransportTest, InitializationFailureIsReportedSafely )
{
    EXPECT_FALSE( HOST_TRANSPORT_Test_Init_With_Workspace_Capacity( 64U ) );
    const auto* diagnostics = HOST_TRANSPORT_Get_Diagnostics();
    EXPECT_EQ( 1U, diagnostics->initialization_attempted );
    EXPECT_EQ( 0U, diagnostics->initialization_successful );
    EXPECT_EQ( 64U, diagnostics->available_workspace_bytes );
    EXPECT_GT( diagnostics->required_workspace_bytes, diagnostics->available_workspace_bytes );
    EXPECT_EQ( HIL_TRANSPORT_STATUS_BUFFER_TOO_SMALL, diagnostics->initialization_status );
    EXPECT_FALSE( HOST_TRANSPORT_Is_Initialized() );
}

TEST_F( HostTransportTest, MaximumEchoAndTransportOutputFitConfiguredBuffers )
{
    EXPECT_EQ( HIL_TRANSPORT_DEFAULT_MAX_APPLICATION_MESSAGE_SIZE,
               HOST_TRANSPORT_MAX_APPLICATION_MESSAGE_SIZE );
    EXPECT_LE( HOST_TRANSPORT_MAX_ENCODED_FRAME_SIZE, HW_USB_TRANSMIT_CAPACITY_BYTES );
    EXPECT_EQ( HOST_TRANSPORT_MAX_APPLICATION_MESSAGE_SIZE - PROTOCOL_TEST_HARNESS_HEADER_SIZE,
               496U );
}

TEST_F( HostTransportTest, ZeroByteReceiveAndEventsAreServiced )
{
    ASSERT_TRUE( HOST_TRANSPORT_Init() );
    g_usb_connected = true;
    HOST_TRANSPORT_Set_Link_State( true, 1U );
    HOST_TRANSPORT_Service( 2U );

    const auto* diagnostics = HOST_TRANSPORT_Get_Diagnostics();
    EXPECT_GT( diagnostics->transport_rx_zero_byte_calls, 0U );
    EXPECT_GT( diagnostics->transport_event_count, 0U );
}

TEST_F( HostTransportTest, BusyUsbDoesNotCommitAndRetryUsesSamePinnedOutput )
{
    ASSERT_TRUE( HOST_TRANSPORT_Init() );
    g_usb_connected = true;
    HOST_TRANSPORT_Set_Link_State( true, 0U );
    HOST_TRANSPORT_Service( 0U );

    g_usb_rx        = MakeHostInitiate();
    g_usb_accept_tx = false;
    HOST_TRANSPORT_Service( 1U );
    ASSERT_FALSE( g_tx_attempts.empty() );
    const auto first_attempt = g_tx_attempts.back();
    EXPECT_EQ( 0U, HOST_TRANSPORT_Get_Diagnostics()->output_commits );

    HOST_TRANSPORT_Service( 2U );
    ASSERT_GE( g_tx_attempts.size(), 2U );
    EXPECT_EQ( first_attempt, g_tx_attempts.back() );
    EXPECT_EQ( 0U, HOST_TRANSPORT_Get_Diagnostics()->output_commits );

    g_usb_accept_tx = true;
    HOST_TRANSPORT_Service( 3U );
    EXPECT_EQ( 1U, HOST_TRANSPORT_Get_Diagnostics()->output_commits );
}

TEST_F( HostTransportTest, PendingResponseSurvivesNotReadySubmission )
{
    ASSERT_TRUE( HOST_TRANSPORT_Init() );
    g_usb_connected = true;
    HOST_TRANSPORT_Set_Link_State( true, 0U );
    const uint8_t response[] = { 0xAAU };
    ASSERT_TRUE( HOST_TRANSPORT_Test_Seed_Pending_Response( response, sizeof( response ) ) );

    HOST_TRANSPORT_Service( 1U );

    EXPECT_TRUE( HOST_TRANSPORT_Test_Response_Is_Pending() );
    EXPECT_GT( HOST_TRANSPORT_Get_Diagnostics()->response_submit_retries, 0U );
}

TEST_F( HostTransportTest, OccupiedResponseSlotPreventsApplicationRead )
{
    ASSERT_TRUE( HOST_TRANSPORT_Init() );
    const uint8_t response[] = { 0x5AU };
    ASSERT_TRUE( HOST_TRANSPORT_Test_Seed_Pending_Response( response, sizeof( response ) ) );

    EXPECT_FALSE( HOST_TRANSPORT_Test_Try_Read_Application_Message() );
    EXPECT_EQ( 0U, HOST_TRANSPORT_Get_Diagnostics()->application_requests_received );
}

TEST_F( HostTransportTest, DisconnectClearsCallerOwnedResponseState )
{
    ASSERT_TRUE( HOST_TRANSPORT_Init() );
    g_usb_connected = true;
    HOST_TRANSPORT_Set_Link_State( true, 0U );
    const uint32_t generation = HOST_TRANSPORT_Get_Diagnostics()->link_generation;
    const uint8_t  response[] = { 0x22U };
    ASSERT_TRUE( HOST_TRANSPORT_Test_Seed_Pending_Response( response, sizeof( response ) ) );

    g_usb_connected = false;
    HOST_TRANSPORT_Set_Link_State( false, 10U );

    EXPECT_FALSE( HOST_TRANSPORT_Test_Response_Is_Pending() );
    EXPECT_EQ( 0U, HOST_TRANSPORT_Get_Diagnostics()->pending_rx_length );
    EXPECT_EQ( generation, HOST_TRANSPORT_Get_Diagnostics()->link_generation );
    EXPECT_GE( g_discard_calls, 2U );
}

TEST_F( HostTransportTest, DisconnectDropsPinnedOldGenerationOutput )
{
    ASSERT_TRUE( HOST_TRANSPORT_Init() );
    g_usb_connected = true;
    HOST_TRANSPORT_Set_Link_State( true, 0U );
    HOST_TRANSPORT_Service( 0U );

    g_usb_rx        = MakeHostInitiate();
    g_usb_accept_tx = false;
    HOST_TRANSPORT_Service( 1U );
    ASSERT_FALSE( g_tx_attempts.empty() );
    EXPECT_EQ( 0U, HOST_TRANSPORT_Get_Diagnostics()->output_commits );

    g_usb_connected = false;
    HOST_TRANSPORT_Set_Link_State( false, 2U );
    g_usb_connected = true;
    HOST_TRANSPORT_Set_Link_State( true, 3U );
    g_usb_accept_tx                = true;
    const uint32_t attempts_before = static_cast<uint32_t>( g_tx_attempts.size() );

    HOST_TRANSPORT_Service( 4U );

    EXPECT_EQ( 0U, HOST_TRANSPORT_Get_Diagnostics()->output_commits );
    EXPECT_EQ( attempts_before, g_tx_attempts.size() );
}

TEST_F( HostTransportTest, PartiallyConsumedInputRetainsExactSuffix )
{
    ASSERT_TRUE( HOST_TRANSPORT_Init() );
    g_usb_connected = true;
    HOST_TRANSPORT_Set_Link_State( true, 0U );
    HOST_TRANSPORT_Service( 0U );

    std::vector<uint8_t> original;
    for ( uint8_t marker = 0U; marker < 30U; marker++ )
    {
        /* One-byte decoded frames are deliberately malformed Transport frames. */
        original.push_back( 0x02U );
        original.push_back( static_cast<uint8_t>( 0x40U + marker ) );
        original.push_back( 0x00U );
    }
    g_usb_rx = original;

    HOST_TRANSPORT_Service( 1U );

    const auto* diagnostics = HOST_TRANSPORT_Get_Diagnostics();
    ASSERT_GT( diagnostics->transport_rx_partial_consumption_calls, 0U );
    ASSERT_GT( diagnostics->pending_rx_length, 0U );
    ASSERT_LE( diagnostics->pending_rx_length, original.size() );

    std::array<uint8_t, HOST_TRANSPORT_PENDING_RX_CAPACITY_BYTES> retained{};
    const uint32_t                                                retained_length =
        HOST_TRANSPORT_Test_Copy_Pending_RX( retained.data(), retained.size() );
    ASSERT_EQ( diagnostics->pending_rx_length, retained_length );
    ASSERT_GE( diagnostics->usb_rx_bytes, retained_length );
    const size_t suffix_start = diagnostics->usb_rx_bytes - retained_length;
    ASSERT_LE( suffix_start + retained_length, original.size() );
    EXPECT_TRUE( std::equal( retained.begin(), retained.begin() + retained_length,
                             original.begin() + static_cast<std::ptrdiff_t>( suffix_start ) ) );
}

TEST_F( HostTransportTest, ReceiveWorkIsBoundedPerServiceIteration )
{
    ASSERT_TRUE( HOST_TRANSPORT_Init() );
    g_usb_connected = true;
    HOST_TRANSPORT_Set_Link_State( true, 0U );
    HOST_TRANSPORT_Service( 0U );
    g_usb_receive_calls = 0U;
    g_usb_rx.assign( 4096U, 0x55U );

    HOST_TRANSPORT_Service( 1U );

    EXPECT_LE( g_usb_receive_calls, HOST_TRANSPORT_MAX_RECEIVE_OPERATIONS_PER_SERVICE );
    EXPECT_FALSE( g_usb_rx.empty() );
}

TEST_F( HostTransportTest, EndToEndHostPeerHandshakeAndEchoUsesFirmwareServicePath )
{
    ASSERT_TRUE( HOST_TRANSPORT_Init() );
    g_usb_connected = true;
    HOST_TRANSPORT_Set_Link_State( true, 0U );

    HostPeerHarness peer{};
    ASSERT_TRUE( InitializeHostPeer( peer ) );

    bool session_established = false;
    for ( uint32_t now_ms = 1U; now_ms < 30U; now_ms++ )
    {
        ASSERT_TRUE( ServiceHostPeerAndFirmware( peer, now_ms ) );
        if ( HostPeerAndFirmwareAreEstablished( peer ) )
        {
            session_established = true;
            break;
        }
    }
    ASSERT_TRUE( session_established );

    const std::vector<uint8_t> request = MakeEchoRequest();
    ASSERT_EQ( HIL_TRANSPORT_STATUS_OK, HIL_TRANSPORT_Submit_Application_Data(
                                            &peer.context, request.data(), request.size() ) );

    bool response_ready = false;
    for ( uint32_t now_ms = 30U; now_ms < 80U; now_ms++ )
    {
        ASSERT_TRUE( ServiceHostPeerAndFirmware( peer, now_ms ) );

        HIL_Transport_Status_Snapshot_T peer_status{};
        ASSERT_EQ( HIL_TRANSPORT_STATUS_OK,
                   HIL_TRANSPORT_Get_Status( &peer.context, &peer_status ) );
        if ( peer_status.application_message_pending != 0U )
        {
            response_ready = true;
            break;
        }
    }
    ASSERT_TRUE( response_ready );

    std::array<uint8_t, HOST_TRANSPORT_MAX_APPLICATION_MESSAGE_SIZE> response{};
    size_t                                                           response_size = 0U;
    ASSERT_EQ( HIL_TRANSPORT_STATUS_OK,
               HIL_TRANSPORT_Read_Application_Data( &peer.context, response.data(), response.size(),
                                                    &response_size ) );
    ASSERT_EQ( request.size(), response_size );

    std::vector<uint8_t> expected_response = request;
    expected_response[5]                   = PROTOCOL_TEST_HARNESS_OPCODE_ECHO_RESPONSE;
    EXPECT_TRUE(
        std::equal( expected_response.begin(), expected_response.end(), response.begin() ) );

    const auto* diagnostics = HOST_TRANSPORT_Get_Diagnostics();
    EXPECT_EQ( 1U, diagnostics->application_requests_received );
    EXPECT_EQ( 1U, diagnostics->responses_submitted );
}
