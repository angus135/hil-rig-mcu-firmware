#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

extern "C"
{
#include "protocol_test_config.h"
#include "protocol_test_harness.h"
#include "hil_rig_protocol/version.h"
}

namespace {
constexpr size_t kMaxMessage = HOST_TRANSPORT_MAX_APPLICATION_MESSAGE_SIZE;
constexpr size_t kMaxPayload = kMaxMessage - PROTOCOL_TEST_HARNESS_HEADER_SIZE;

void WriteU16LE( uint8_t* data, uint16_t value )
{
    data[0] = static_cast<uint8_t>( value & 0xFFU );
    data[1] = static_cast<uint8_t>( ( value >> 8U ) & 0xFFU );
}

void WriteU32LE( uint8_t* data, uint32_t value )
{
    data[0] = static_cast<uint8_t>( value & 0xFFU );
    data[1] = static_cast<uint8_t>( ( value >> 8U ) & 0xFFU );
    data[2] = static_cast<uint8_t>( ( value >> 16U ) & 0xFFU );
    data[3] = static_cast<uint8_t>( ( value >> 24U ) & 0xFFU );
}

uint32_t ReadU32LE( const uint8_t* data )
{
    return static_cast<uint32_t>( data[0] ) | ( static_cast<uint32_t>( data[1] ) << 8U )
           | ( static_cast<uint32_t>( data[2] ) << 16U )
           | ( static_cast<uint32_t>( data[3] ) << 24U );
}

std::vector<uint8_t> Request( uint8_t opcode, uint32_t request_id,
                              const std::vector<uint8_t>& payload = {} )
{
    std::vector<uint8_t> request( PROTOCOL_TEST_HARNESS_HEADER_SIZE + payload.size(), 0U );
    request[0] = 'H';
    request[1] = 'R';
    request[2] = 'T';
    request[3] = 'P';
    request[4] = PROTOCOL_TEST_HARNESS_VERSION;
    request[5] = opcode;
    WriteU16LE( &request[6], 0U );
    WriteU32LE( &request[8], request_id );
    WriteU32LE( &request[12], static_cast<uint32_t>( payload.size() ) );
    std::copy( payload.begin(), payload.end(),
               request.begin() + PROTOCOL_TEST_HARNESS_HEADER_SIZE );
    return request;
}
}  // namespace

TEST( ProtocolTestHarness, ValidEmptyEcho )
{
    auto request = Request( PROTOCOL_TEST_HARNESS_OPCODE_ECHO_REQUEST, 0x01020304U );
    std::array<uint8_t, kMaxMessage> response{};
    size_t                           response_length = 0U;

    EXPECT_EQ( PROTOCOL_TEST_HARNESS_RESULT_OK,
               PROTOCOL_TEST_HARNESS_Build_Response( request.data(), request.size(), kMaxMessage,
                                                     nullptr, response.data(), response.size(),
                                                     &response_length ) );
    EXPECT_EQ( PROTOCOL_TEST_HARNESS_HEADER_SIZE, response_length );
    EXPECT_EQ( PROTOCOL_TEST_HARNESS_OPCODE_ECHO_RESPONSE, response[5] );
    EXPECT_EQ( 0x01020304U, ReadU32LE( &response[8] ) );
    EXPECT_EQ( 0U, ReadU32LE( &response[12] ) );
}

TEST( ProtocolTestHarness, ValidBinaryEchoContainingZeroBytes )
{
    const std::vector<uint8_t> payload = { 0x00U, 0x11U, 0x00U, 0xC0U, 0xDBU, 0xFFU };
    auto request = Request( PROTOCOL_TEST_HARNESS_OPCODE_ECHO_REQUEST, 77U, payload );
    std::array<uint8_t, kMaxMessage> response{};
    size_t                           response_length = 0U;

    ASSERT_EQ( PROTOCOL_TEST_HARNESS_RESULT_OK,
               PROTOCOL_TEST_HARNESS_Build_Response( request.data(), request.size(), kMaxMessage,
                                                     nullptr, response.data(), response.size(),
                                                     &response_length ) );
    ASSERT_EQ( request.size(), response_length );
    EXPECT_TRUE( std::equal( payload.begin(), payload.end(),
                             response.begin() + PROTOCOL_TEST_HARNESS_HEADER_SIZE ) );
}

TEST( ProtocolTestHarness, RequestIdAndLittleEndianFixedVectorArePreserved )
{
    auto request = Request( PROTOCOL_TEST_HARNESS_OPCODE_ECHO_REQUEST, 0x78563412U, { 0xAAU } );
    std::array<uint8_t, kMaxMessage> response{};
    size_t                           response_length = 0U;

    ASSERT_EQ( PROTOCOL_TEST_HARNESS_RESULT_OK,
               PROTOCOL_TEST_HARNESS_Build_Response( request.data(), request.size(), kMaxMessage,
                                                     nullptr, response.data(), response.size(),
                                                     &response_length ) );
    EXPECT_EQ( 0x12U, response[8] );
    EXPECT_EQ( 0x34U, response[9] );
    EXPECT_EQ( 0x56U, response[10] );
    EXPECT_EQ( 0x78U, response[11] );
    EXPECT_EQ( 0x01U, response[12] );
    EXPECT_EQ( 0x00U, response[13] );
    EXPECT_EQ( 0x00U, response[14] );
    EXPECT_EQ( 0x00U, response[15] );
}

TEST( ProtocolTestHarness, MaximumPayloadFits )
{
    std::vector<uint8_t> payload( kMaxPayload, 0x5AU );
    auto request = Request( PROTOCOL_TEST_HARNESS_OPCODE_ECHO_REQUEST, 9U, payload );
    std::array<uint8_t, kMaxMessage> response{};
    size_t                           response_length = 0U;

    EXPECT_EQ( PROTOCOL_TEST_HARNESS_RESULT_OK,
               PROTOCOL_TEST_HARNESS_Build_Response( request.data(), request.size(), kMaxMessage,
                                                     nullptr, response.data(), response.size(),
                                                     &response_length ) );
    EXPECT_EQ( kMaxMessage, response_length );
}

TEST( ProtocolTestHarness, InvalidMagicIsRejected )
{
    auto request = Request( PROTOCOL_TEST_HARNESS_OPCODE_ECHO_REQUEST, 1U );
    request[0]   = 'X';
    std::array<uint8_t, kMaxMessage> response{};
    size_t                           response_length = 0U;
    EXPECT_EQ( PROTOCOL_TEST_HARNESS_RESULT_INVALID_MAGIC,
               PROTOCOL_TEST_HARNESS_Build_Response( request.data(), request.size(), kMaxMessage,
                                                     nullptr, response.data(), response.size(),
                                                     &response_length ) );
}

TEST( ProtocolTestHarness, UnsupportedVersionIsRejected )
{
    auto request = Request( PROTOCOL_TEST_HARNESS_OPCODE_ECHO_REQUEST, 1U );
    request[4]   = 2U;
    std::array<uint8_t, kMaxMessage> response{};
    size_t                           response_length = 0U;
    EXPECT_EQ( PROTOCOL_TEST_HARNESS_RESULT_UNSUPPORTED_VERSION,
               PROTOCOL_TEST_HARNESS_Build_Response( request.data(), request.size(), kMaxMessage,
                                                     nullptr, response.data(), response.size(),
                                                     &response_length ) );
}

TEST( ProtocolTestHarness, UnsupportedFlagsAreRejected )
{
    auto request = Request( PROTOCOL_TEST_HARNESS_OPCODE_ECHO_REQUEST, 1U );
    WriteU16LE( &request[6], 1U );
    std::array<uint8_t, kMaxMessage> response{};
    size_t                           response_length = 0U;
    EXPECT_EQ( PROTOCOL_TEST_HARNESS_RESULT_UNSUPPORTED_FLAGS,
               PROTOCOL_TEST_HARNESS_Build_Response( request.data(), request.size(), kMaxMessage,
                                                     nullptr, response.data(), response.size(),
                                                     &response_length ) );
}

TEST( ProtocolTestHarness, InvalidOpcodeIsRejected )
{
    auto                             request = Request( 0x55U, 1U );
    std::array<uint8_t, kMaxMessage> response{};
    size_t                           response_length = 0U;
    EXPECT_EQ( PROTOCOL_TEST_HARNESS_RESULT_UNSUPPORTED_OPCODE,
               PROTOCOL_TEST_HARNESS_Build_Response( request.data(), request.size(), kMaxMessage,
                                                     nullptr, response.data(), response.size(),
                                                     &response_length ) );
}

TEST( ProtocolTestHarness, DeclaredLengthShorterThanActualIsRejected )
{
    auto request = Request( PROTOCOL_TEST_HARNESS_OPCODE_ECHO_REQUEST, 1U, { 1U, 2U } );
    WriteU32LE( &request[12], 1U );
    std::array<uint8_t, kMaxMessage> response{};
    size_t                           response_length = 0U;
    EXPECT_EQ( PROTOCOL_TEST_HARNESS_RESULT_INVALID_LENGTH,
               PROTOCOL_TEST_HARNESS_Build_Response( request.data(), request.size(), kMaxMessage,
                                                     nullptr, response.data(), response.size(),
                                                     &response_length ) );
}

TEST( ProtocolTestHarness, DeclaredLengthLongerThanActualIsRejected )
{
    auto request = Request( PROTOCOL_TEST_HARNESS_OPCODE_ECHO_REQUEST, 1U, { 1U } );
    WriteU32LE( &request[12], 2U );
    std::array<uint8_t, kMaxMessage> response{};
    size_t                           response_length = 0U;
    EXPECT_EQ( PROTOCOL_TEST_HARNESS_RESULT_INVALID_LENGTH,
               PROTOCOL_TEST_HARNESS_Build_Response( request.data(), request.size(), kMaxMessage,
                                                     nullptr, response.data(), response.size(),
                                                     &response_length ) );
}

TEST( ProtocolTestHarness, ResponseBufferTooSmallReportsRequiredSize )
{
    auto request = Request( PROTOCOL_TEST_HARNESS_OPCODE_ECHO_REQUEST, 1U, { 1U, 2U, 3U } );
    std::array<uint8_t, 4> response{};
    size_t                 response_length = 0U;
    EXPECT_EQ( PROTOCOL_TEST_HARNESS_RESULT_BUFFER_TOO_SMALL,
               PROTOCOL_TEST_HARNESS_Build_Response( request.data(), request.size(), kMaxMessage,
                                                     nullptr, response.data(), response.size(),
                                                     &response_length ) );
    EXPECT_EQ( request.size(), response_length );
}

TEST( ProtocolTestHarness, StatusResponseHasFixedLittleEndianSchema )
{
    auto request = Request( PROTOCOL_TEST_HARNESS_OPCODE_STATUS_REQUEST, 0x11223344U );
    PROTOCOL_TEST_HARNESS_Status_Data_T status{};
    status.link_state                              = 1U;
    status.link_generation                         = 2U;
    status.transport_event_count                   = 3U;
    status.usb_rx_bytes                            = 4U;
    status.usb_tx_bytes                            = 5U;
    status.application_requests_received           = 6U;
    status.responses_submitted                     = 7U;
    status.usb_tx_busy_retries                     = 8U;
    status.invalid_hrtp_messages                   = 9U;
    status.maximum_service_gap_ms                  = 10U;
    status.transport_session_state                 = 11U;
    status.compatibility_profile_id                = 0x41505031U;
    status.application_codec_initialized           = 16U;
    status.application_initialization_status       = 17U;
    status.non_hrtp_application_messages_received  = 18U;
    status.application_decode_failures             = 19U;
    status.application_semantic_rejections         = 20U;
    status.application_encode_failures             = 21U;
    status.configurations_accepted                 = 22U;
    status.instructions_accepted                   = 23U;
    status.results_encoded                         = 24U;
    status.application_harness_state               = 25U;
    status.next_expected_tick                      = 26U;
    status.active_expected_tick_count              = 27U;
    status.last_application_status                 = 28U;
    status.last_decoded_application_message_type   = 29U;
    status.configuration_digest                    = 30U;
    status.instruction_digest                      = 31U;

    std::array<uint8_t, kMaxMessage> response{};
    size_t response_length = 0U;

    ASSERT_EQ( PROTOCOL_TEST_HARNESS_RESULT_OK,
               PROTOCOL_TEST_HARNESS_Build_Response( request.data(), request.size(), kMaxMessage,
                                                     &status, response.data(), response.size(),
                                                     &response_length ) );
    ASSERT_EQ( 144U, response_length );
    ASSERT_EQ( 128U, PROTOCOL_TEST_HARNESS_STATUS_PAYLOAD_SIZE );
    EXPECT_EQ( PROTOCOL_TEST_HARNESS_OPCODE_STATUS_RESPONSE, response[5] );
    EXPECT_EQ( 0x11223344U, ReadU32LE( &response[8] ) );
    EXPECT_EQ( 128U, ReadU32LE( &response[12] ) );

    const uint8_t* payload = &response[PROTOCOL_TEST_HARNESS_HEADER_SIZE];
    const std::array<uint32_t, PROTOCOL_TEST_HARNESS_STATUS_FIELD_COUNT> expected = {
        2U,
        1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 9U, 10U, 11U,
        0x41505031U,
        HIL_RIG_PROTOCOL_VERSION_MAJOR,
        HIL_RIG_PROTOCOL_VERSION_MINOR,
        HIL_RIG_PROTOCOL_VERSION_PATCH,
        16U, 17U, 18U, 19U, 20U, 21U, 22U, 23U, 24U, 25U, 26U, 27U, 28U, 29U,
        30U, 31U,
    };
    for ( size_t i = 0U; i < expected.size(); ++i )
    {
        EXPECT_EQ( expected[i], ReadU32LE( &payload[i * sizeof( uint32_t )] ) ) << "field " << i;
    }
}

