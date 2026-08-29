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
    PROTOCOL_TEST_HARNESS_Status_Data_T status = {
        1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 9U, 10U, 11U,
    };
    std::array<uint8_t, kMaxMessage> response{};
    size_t                           response_length = 0U;

    ASSERT_EQ( PROTOCOL_TEST_HARNESS_RESULT_OK,
               PROTOCOL_TEST_HARNESS_Build_Response( request.data(), request.size(), kMaxMessage,
                                                     &status, response.data(), response.size(),
                                                     &response_length ) );
    EXPECT_EQ( PROTOCOL_TEST_HARNESS_HEADER_SIZE + PROTOCOL_TEST_HARNESS_STATUS_PAYLOAD_SIZE,
               response_length );
    EXPECT_EQ( PROTOCOL_TEST_HARNESS_OPCODE_STATUS_RESPONSE, response[5] );
    EXPECT_EQ( 0x11223344U, ReadU32LE( &response[8] ) );
    EXPECT_EQ( PROTOCOL_TEST_HARNESS_STATUS_PAYLOAD_SIZE, ReadU32LE( &response[12] ) );
    EXPECT_EQ( PROTOCOL_TEST_HARNESS_STATUS_SCHEMA_VERSION, ReadU32LE( &response[16] ) );
    EXPECT_EQ( 1U, ReadU32LE( &response[20] ) );
    EXPECT_EQ( 11U, ReadU32LE( &response[60] ) );
}
