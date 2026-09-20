#include "application_test_fixtures.hpp"

extern "C"
{
#include "application_test_harness.h"
#include "hil_rig_protocol/version.h"
}

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <utility>
#include <vector>

namespace {
using namespace application_test_fixtures;

constexpr uint32_t kRepresentativeConfigurationDigest = 0xE5B6A67EU;
constexpr uint32_t kDisabledConfigurationDigest       = 0xBA7FAE23U;
constexpr uint32_t kMaximumExtensionDigest            = 0x71EF1F9DU;

/*
 * These values are FNV-1a over exactly the typed fixed Instruction fields in
 * application_instruction.h: tick u32, ten digital u8 values, six analogue
 * u32 values, then two (period u32, duty u16) records, all little-endian.
 */
constexpr std::array<uint32_t, 3> kInstructionDigests = {
    0x80089EF8U,
    0x8DE22BBEU,
    0x6AC9DD7AU,
};
constexpr std::array<uint32_t, 3> kExpectedAnalogInput1 = {
    8048525U,
    409671U,
    11614241U,
};
constexpr uint32_t kExpectedAnalogInput0 = 13952446U;

HIL_Application_Message_T BasicSystemInfoRequest()
{
    HIL_Application_Message_T message{};
    message.type        = HIL_APPLICATION_MESSAGE_TYPE_SYSTEM_INFO_REQUEST;
    message.subtype     = HIL_APPLICATION_MESSAGE_SUBTYPE_BASIC;
    message.has_test_id = 0U;
    message.body.system_info_request.request_firmware_git_hash = 1U;
    message.body.system_info_request.query = HIL_APPLICATION_SYSTEM_INFO_QUERY_BASIC;
    message.body.system_info_request.application_protocol_major = HIL_RIG_PROTOCOL_VERSION_MAJOR;
    message.body.system_info_request.application_protocol_minor = HIL_RIG_PROTOCOL_VERSION_MINOR;
    message.body.system_info_request.application_protocol_patch = HIL_RIG_PROTOCOL_VERSION_PATCH;
    return message;
}

HIL_Application_Message_T ExecutionControl( HIL_Application_Control_Command_T command )
{
    HIL_Application_Message_T message{};
    message.type                           = HIL_APPLICATION_MESSAGE_TYPE_EXECUTION_CONTROL;
    message.subtype                        = HIL_APPLICATION_MESSAGE_SUBTYPE_NONE;
    message.has_test_id                    = 1U;
    message.test_id                        = MakeTestId( 0x50U );
    message.body.execution_control.command = command;
    message.body.execution_control.flags   = 0U;
    return message;
}

HIL_Application_Message_T ResetApplication()
{
    HIL_Application_Message_T message{};
    message.type                        = HIL_APPLICATION_MESSAGE_TYPE_GLOBAL_CONTROL;
    message.subtype                     = HIL_APPLICATION_MESSAGE_SUBTYPE_NONE;
    message.has_test_id                 = 0U;
    message.body.global_control.command = HIL_APPLICATION_GLOBAL_CONTROL_RESET_APPLICATION;
    message.body.global_control.flags   = 0U;
    return message;
}

HIL_Application_Message_T FinalizeUpload( const HIL_Application_Test_Id_T& test_id )
{
    HIL_Application_Message_T message{};
    message.type        = HIL_APPLICATION_MESSAGE_TYPE_FINALIZE_TEST_UPLOAD;
    message.subtype     = HIL_APPLICATION_MESSAGE_SUBTYPE_NONE;
    message.has_test_id = 1U;
    message.test_id     = test_id;
    message.body.finalize_test_upload.flags = 0U;
    return message;
}

HIL_Application_Message_T Start( const HIL_Application_Test_Id_T& test_id )
{
    HIL_Application_Message_T message{};
    message.type                           = HIL_APPLICATION_MESSAGE_TYPE_EXECUTION_CONTROL;
    message.subtype                        = HIL_APPLICATION_MESSAGE_SUBTYPE_NONE;
    message.has_test_id                    = 1U;
    message.test_id                        = test_id;
    message.body.execution_control.command = HIL_APPLICATION_CONTROL_START;
    message.body.execution_control.flags   = 0U;
    return message;
}

HIL_Application_Message_T Update( const HIL_Application_Test_Id_T& test_id, uint32_t tick,
                                  uint8_t flags,
                                  const HIL_Application_Logical_Operation_T* operations,
                                  uint8_t operation_count )
{
    HIL_Application_Message_T message{};
    message.type        = HIL_APPLICATION_MESSAGE_TYPE_UPDATE_INSTRUCTION;
    message.subtype     = HIL_APPLICATION_MESSAGE_SUBTYPE_NONE;
    message.has_test_id = 1U;
    message.test_id     = test_id;
    message.body.update_instruction.tick_number = tick;
    message.body.update_instruction.operation_count = operation_count;
    message.body.update_instruction.flags = flags;
    message.body.update_instruction.operations = operations;
    return message;
}

HIL_Application_Message_T ApplicationResponse( HIL_Application_Response_Scope_T scope )
{
    HIL_Application_Message_T message{};
    message.type        = HIL_APPLICATION_MESSAGE_TYPE_RESPONSE;
    message.subtype     = HIL_APPLICATION_MESSAGE_SUBTYPE_NONE;
    message.has_test_id = scope == HIL_APPLICATION_RESPONSE_SCOPE_GLOBAL_CONTROL ? 0U : 1U;
    message.test_id     = MakeTestId( 0x60U );

    auto& response                  = message.body.response;
    response.scope                  = scope;
    response.outcome                = scope == HIL_APPLICATION_RESPONSE_SCOPE_EXECUTION_CONTROL
                               || scope == HIL_APPLICATION_RESPONSE_SCOPE_GLOBAL_CONTROL
                                          ? HIL_APPLICATION_RESPONSE_OUTCOME_COMPLETED
                                          : HIL_APPLICATION_RESPONSE_OUTCOME_ACCEPTED;
    response.reason                 = HIL_APPLICATION_RESPONSE_REASON_NONE;
    response.tick_number            = 0x10203040U;
    response.control_command        = scope == HIL_APPLICATION_RESPONSE_SCOPE_EXECUTION_CONTROL
                                          ? HIL_APPLICATION_CONTROL_ABORT
                                          : HIL_APPLICATION_CONTROL_INVALID;
    response.global_control_command = scope == HIL_APPLICATION_RESPONSE_SCOPE_GLOBAL_CONTROL
                                          ? HIL_APPLICATION_GLOBAL_CONTROL_RESET_APPLICATION
                                          : HIL_APPLICATION_GLOBAL_CONTROL_INVALID;
    response.detail                 = 0x50607080U;
    return message;
}

HIL_Application_Message_T ApplicationError( uint8_t has_test_id, uint8_t has_tick_number,
                                            const uint8_t* diagnostic_data,
                                            uint8_t        diagnostic_size )
{
    HIL_Application_Message_T message{};
    message.type        = HIL_APPLICATION_MESSAGE_TYPE_ERROR;
    message.subtype     = HIL_APPLICATION_MESSAGE_SUBTYPE_NONE;
    message.has_test_id = has_test_id;
    message.test_id     = MakeTestId( 0x70U );

    auto& error                = message.body.error;
    error.category             = HIL_APPLICATION_ERROR_CATEGORY_EXECUTION;
    error.recoverable          = 1U;
    error.has_tick_number      = has_tick_number;
    error.tick_number          = has_tick_number == 1U ? 345U : 0U;
    error.detail               = 0xA0B0C0D0U;
    error.diagnostic_data.data = diagnostic_data;
    error.diagnostic_data.size = diagnostic_size;
    return message;
}

template <typename T, typename = void> struct HasTerminationMember : std::false_type
{
};

template <typename T>
struct HasTerminationMember<T, std::void_t<decltype( std::declval<T>().termination )>>
    : std::true_type
{
};

static_assert( !HasTerminationMember<HIL_Application_Can_Config_T>::value,
               "CAN termination must not be part of the Application configuration API." );

class ApplicationHarnessTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        APPLICATION_TEST_HARNESS_Test_Reset();
        ASSERT_EQ( APPLICATION_TEST_HARNESS_Init(), HIL_APPLICATION_STATUS_OK );
        ASSERT_TRUE( InitCodec( codec_ ) );

        std::vector<uint8_t> response;
        ASSERT_EQ( Handle( EncodeMessage( BasicSystemInfoRequest() ), &response ),
                   HIL_APPLICATION_STATUS_OK );
        ASSERT_FALSE( response.empty() );
    }

    std::vector<uint8_t> EncodeMessage( const HIL_Application_Message_T& message )
    {
        return Encode( codec_, message );
    }

    HIL_Application_Status_T Handle( const std::vector<uint8_t>& bytes,
                                     std::vector<uint8_t>*       response          = nullptr,
                                     size_t                      response_capacity = 512U )
    {
        response_buffer_.fill( 0xA5U );
        size_t                         response_size = 999U;
        const HIL_Application_Status_T status        = APPLICATION_TEST_HARNESS_Handle_Message(
            bytes.data(), bytes.size(), response_buffer_.data(), response_capacity,
            &response_size );
        if ( response != nullptr )
        {
            response->assign( response_buffer_.begin(), response_buffer_.begin() + response_size );
        }
        return status;
    }

    HIL_Application_Message_T DecodeMessage( const std::vector<uint8_t>& bytes )
    {
        HIL_Application_Message_T decoded{};
        size_t                    required = 0U;
        size_t                    used     = 0U;
        EXPECT_EQ(
            HIL_APPLICATION_Decode_Storage_Size( &codec_, bytes.data(), bytes.size(), &required ),
            HIL_APPLICATION_STATUS_OK );
        EXPECT_LE( required, decode_storage_.size() );
        EXPECT_EQ(
            HIL_APPLICATION_Decode_Message( &codec_, bytes.data(), bytes.size(), &decoded,
                                            required == 0U ? nullptr : decode_storage_.data(),
                                            required == 0U ? 0U : decode_storage_.size(), &used ),
            HIL_APPLICATION_STATUS_OK );
        EXPECT_EQ( used, required );
        return decoded;
    }

    HIL_Application_Message_T PollResult()
    {
        std::array<uint8_t, 512> output{};
        size_t                    output_size = 0U;
        EXPECT_EQ( APPLICATION_TEST_HARNESS_Poll_Output( output.data(), output.size(),
                                                         &output_size ),
                   HIL_APPLICATION_STATUS_OK );
        std::vector<uint8_t> encoded( output.begin(), output.begin() + output_size );
        const auto           result = DecodeMessage( encoded );
        EXPECT_EQ( APPLICATION_TEST_HARNESS_Commit_Output(), HIL_APPLICATION_STATUS_OK );
        return result;
    }

    void ExpectWorkflowCountersUnchanged( const APPLICATION_TEST_HARNESS_Diagnostics_T& before )
    {
        const auto* after = APPLICATION_TEST_HARNESS_Get_Diagnostics();
        EXPECT_EQ( after->configurations_accepted, before.configurations_accepted );
        EXPECT_EQ( after->instructions_accepted, before.instructions_accepted );
        EXPECT_EQ( after->results_encoded, before.results_encoded );
        EXPECT_EQ( after->state, before.state );
        EXPECT_EQ( after->next_expected_tick, before.next_expected_tick );
        EXPECT_EQ( after->active_expected_tick_count, before.active_expected_tick_count );
    }

    HIL_Application_Context_T codec_{};
    std::array<uint8_t, 512>  response_buffer_{};
    alignas( HIL_APPLICATION_DECODE_STORAGE_ALIGNMENT ) std::array<uint8_t, 255> decode_storage_{};
};

TEST_F( ApplicationHarnessTest, InitializesRequiredProfileAndReportsInitializationFailure )
{
    const auto* diagnostics = APPLICATION_TEST_HARNESS_Get_Diagnostics();
    ASSERT_NE( diagnostics, nullptr );
    EXPECT_EQ( diagnostics->codec_initialized, 1U );
    EXPECT_EQ( diagnostics->initialization_status, HIL_APPLICATION_STATUS_OK );
    EXPECT_EQ( diagnostics->state, APPLICATION_TEST_HARNESS_STATE_WAITING_FOR_CONFIGURATION );

    HIL_Application_Config_T invalid = CodecConfig();
    invalid.max_encoded_message_size = 1U;
    const HIL_Application_Status_T status =
        APPLICATION_TEST_HARNESS_Test_Init_With_Config( &invalid );
    EXPECT_NE( status, HIL_APPLICATION_STATUS_OK );
    diagnostics = APPLICATION_TEST_HARNESS_Get_Diagnostics();
    EXPECT_EQ( diagnostics->codec_initialized, 0U );
    EXPECT_EQ( diagnostics->initialization_status, static_cast<uint32_t>( status ) );
    EXPECT_EQ( diagnostics->last_application_status, static_cast<uint32_t>( status ) );
    EXPECT_EQ( diagnostics->state, APPLICATION_TEST_HARNESS_STATE_UNINITIALIZED );

    EXPECT_EQ( APPLICATION_TEST_HARNESS_Init(), HIL_APPLICATION_STATUS_OK );
}

TEST_F( ApplicationHarnessTest, MatchingDiscoveryReturnsExactLocalSystemInformation )
{
    APPLICATION_TEST_HARNESS_Reset_Transaction();

    std::vector<uint8_t> response;
    ASSERT_EQ( Handle( EncodeMessage( BasicSystemInfoRequest() ), &response ),
               HIL_APPLICATION_STATUS_OK );

    const HIL_Application_Message_T decoded = DecodeMessage( response );
    ASSERT_EQ( decoded.type, HIL_APPLICATION_MESSAGE_TYPE_SYSTEM_INFO_RESPONSE );
    EXPECT_EQ( decoded.subtype, HIL_APPLICATION_MESSAGE_SUBTYPE_BASIC );
    EXPECT_EQ( decoded.has_test_id, 0U );
    const auto& system_info = decoded.body.system_info_response;
    EXPECT_EQ( system_info.application_protocol_major, HIL_RIG_PROTOCOL_VERSION_MAJOR );
    EXPECT_EQ( system_info.application_protocol_minor, HIL_RIG_PROTOCOL_VERSION_MINOR );
    EXPECT_EQ( system_info.application_protocol_patch, HIL_RIG_PROTOCOL_VERSION_PATCH );
    EXPECT_EQ( system_info.firmware_version_major, 0U );
    EXPECT_EQ( system_info.firmware_version_minor, 0U );
    EXPECT_EQ( system_info.firmware_version_patch, 0U );
    EXPECT_EQ( system_info.diagnostic_data.size, 0U );
    EXPECT_EQ( system_info.firmware_git_hash.size, 0U );

    EXPECT_EQ( Handle( EncodeMessage( RepresentativeConfiguration() ) ),
               HIL_APPLICATION_STATUS_OK );
}

TEST_F( ApplicationHarnessTest, MismatchedDiscoveryReturnsLocalInformationButKeepsMessagesBlocked )
{
    APPLICATION_TEST_HARNESS_Reset_Transaction();

    std::vector<uint8_t> mismatched_request = EncodeMessage( BasicSystemInfoRequest() );
    constexpr size_t     kPatchOffset       = HIL_APPLICATION_HEADER_SIZE_BYTES + 6U;
    ASSERT_GT( mismatched_request.size(), kPatchOffset );
    mismatched_request[kPatchOffset] = static_cast<uint8_t>( HIL_RIG_PROTOCOL_VERSION_PATCH + 1U );

    std::vector<uint8_t> response;
    ASSERT_EQ( Handle( mismatched_request, &response ), HIL_APPLICATION_STATUS_OK );
    const HIL_Application_Message_T decoded = DecodeMessage( response );
    ASSERT_EQ( decoded.type, HIL_APPLICATION_MESSAGE_TYPE_SYSTEM_INFO_RESPONSE );
    EXPECT_EQ( decoded.body.system_info_response.application_protocol_major,
               HIL_RIG_PROTOCOL_VERSION_MAJOR );
    EXPECT_EQ( decoded.body.system_info_response.application_protocol_minor,
               HIL_RIG_PROTOCOL_VERSION_MINOR );
    EXPECT_EQ( decoded.body.system_info_response.application_protocol_patch,
               HIL_RIG_PROTOCOL_VERSION_PATCH );

    EXPECT_EQ( Handle( EncodeMessage( RepresentativeConfiguration() ), &response ),
               HIL_APPLICATION_STATUS_VERSION_MISMATCH );
    ASSERT_FALSE( response.empty() );
    const auto rejected = DecodeMessage( response );
    ASSERT_EQ( rejected.type, HIL_APPLICATION_MESSAGE_TYPE_RESPONSE );
    EXPECT_EQ( rejected.body.response.scope, HIL_APPLICATION_RESPONSE_SCOPE_TEST_CONFIGURATION );
    EXPECT_EQ( rejected.body.response.outcome, HIL_APPLICATION_RESPONSE_OUTCOME_REJECTED );
    EXPECT_EQ( rejected.body.response.reason,
               HIL_APPLICATION_RESPONSE_REASON_OPERATION_NOT_ALLOWED );
    const auto* diagnostics = APPLICATION_TEST_HARNESS_Get_Diagnostics();
    EXPECT_EQ( diagnostics->configurations_accepted, 0U );
    EXPECT_EQ( diagnostics->instructions_accepted, 0U );
    EXPECT_EQ( diagnostics->results_encoded, 0U );
    EXPECT_EQ( diagnostics->state, APPLICATION_TEST_HARNESS_STATE_WAITING_FOR_CONFIGURATION );
}

TEST_F( ApplicationHarnessTest, SyntheticExecutionControlResponsesPreserveTestIdAndWorkflow )
{
    std::vector<uint8_t> response;
    const auto request = ExecutionControl( HIL_APPLICATION_CONTROL_START );
    EXPECT_NE( Handle( EncodeMessage( ExecutionControl( HIL_APPLICATION_CONTROL_START ) ),
                       &response ),
               HIL_APPLICATION_STATUS_OK );
    const auto decoded = DecodeMessage( response );
    ASSERT_EQ( decoded.type, HIL_APPLICATION_MESSAGE_TYPE_RESPONSE );
    EXPECT_EQ( std::memcmp( decoded.test_id.bytes, request.test_id.bytes,
                            HIL_APPLICATION_TEST_ID_SIZE ),
               0 );
    EXPECT_EQ( decoded.body.response.scope, HIL_APPLICATION_RESPONSE_SCOPE_EXECUTION_CONTROL );
    EXPECT_EQ( decoded.body.response.outcome, HIL_APPLICATION_RESPONSE_OUTCOME_REJECTED );
    EXPECT_EQ( decoded.body.response.reason,
               HIL_APPLICATION_RESPONSE_REASON_OPERATION_NOT_ALLOWED );
}

TEST_F( ApplicationHarnessTest, SyntheticResetApplicationResponsePreservesWorkflow )
{
    ASSERT_EQ( Handle( EncodeMessage( RepresentativeConfiguration() ) ),
               HIL_APPLICATION_STATUS_OK );
    const auto before = *APPLICATION_TEST_HARNESS_Get_Diagnostics();
    std::vector<uint8_t> response;
    ASSERT_EQ( Handle( EncodeMessage( ResetApplication() ), &response ),
               HIL_APPLICATION_STATUS_OK );

    const HIL_Application_Message_T decoded = DecodeMessage( response );
    ASSERT_EQ( decoded.type, HIL_APPLICATION_MESSAGE_TYPE_RESPONSE );
    EXPECT_EQ( decoded.has_test_id, 0U );
    EXPECT_EQ( decoded.body.response.scope, HIL_APPLICATION_RESPONSE_SCOPE_GLOBAL_CONTROL );
    EXPECT_EQ( decoded.body.response.outcome, HIL_APPLICATION_RESPONSE_OUTCOME_COMPLETED );
    EXPECT_EQ( decoded.body.response.reason, HIL_APPLICATION_RESPONSE_REASON_NONE );
    EXPECT_EQ( decoded.body.response.global_control_command,
               HIL_APPLICATION_GLOBAL_CONTROL_RESET_APPLICATION );
    EXPECT_EQ( decoded.body.response.detail, 0U );

    const auto* after = APPLICATION_TEST_HARNESS_Get_Diagnostics();
    EXPECT_EQ( after->state, APPLICATION_TEST_HARNESS_STATE_WAITING_FOR_CONFIGURATION );
    EXPECT_EQ( after->next_expected_tick, 0U );
    EXPECT_EQ( after->active_expected_tick_count, 0U );
    EXPECT_EQ( after->configuration_digest, before.configuration_digest );
}

TEST_F( ApplicationHarnessTest, TestOnlyResponseRoundTripPreservesEveryScope )
{
    const auto before = *APPLICATION_TEST_HARNESS_Get_Diagnostics();
    constexpr std::array<HIL_Application_Response_Scope_T, 5> kScopes = {
        HIL_APPLICATION_RESPONSE_SCOPE_TEST_CONFIGURATION,
        HIL_APPLICATION_RESPONSE_SCOPE_TICK,
        HIL_APPLICATION_RESPONSE_SCOPE_COMPLETE_TEST,
        HIL_APPLICATION_RESPONSE_SCOPE_EXECUTION_CONTROL,
        HIL_APPLICATION_RESPONSE_SCOPE_GLOBAL_CONTROL,
    };

    for ( const HIL_Application_Response_Scope_T scope : kScopes )
    {
        const std::vector<uint8_t> encoded = EncodeMessage( ApplicationResponse( scope ) );
        ASSERT_FALSE( encoded.empty() );
        std::vector<uint8_t> response;
        ASSERT_EQ( Handle( encoded, &response ), HIL_APPLICATION_STATUS_OK );
        EXPECT_EQ( response, encoded );
        EXPECT_EQ( DecodeMessage( response ).body.response.scope, scope );
    }

    ExpectWorkflowCountersUnchanged( before );
}

TEST_F( ApplicationHarnessTest, TestOnlyErrorRoundTripPreservesAllFormsAndDiagnosticData )
{
    constexpr std::array<uint8_t, 7> kBinaryDiagnostic = {
        0x00U, 0x7EU, 0x7FU, 0x80U, 0xFEU, 0xFFU, 0xA5U,
    };
    std::array<uint8_t, 255> maximum_diagnostic{};
    for ( size_t i = 0U; i < maximum_diagnostic.size(); ++i )
    {
        maximum_diagnostic[i] = static_cast<uint8_t>( ( i * 37U ) & 0xFFU );
    }
    const std::array<HIL_Application_Message_T, 3> errors = {
        ApplicationError( 0U, 0U, nullptr, 0U ),
        ApplicationError( 1U, 0U, kBinaryDiagnostic.data(),
                          static_cast<uint8_t>( kBinaryDiagnostic.size() ) ),
        ApplicationError( 1U, 1U, maximum_diagnostic.data(),
                          static_cast<uint8_t>( maximum_diagnostic.size() ) ),
    };
    const auto before = *APPLICATION_TEST_HARNESS_Get_Diagnostics();

    for ( const HIL_Application_Message_T& error : errors )
    {
        const std::vector<uint8_t> encoded = EncodeMessage( error );
        ASSERT_FALSE( encoded.empty() );
        std::vector<uint8_t> response;
        ASSERT_EQ( Handle( encoded, &response ), HIL_APPLICATION_STATUS_OK );
        EXPECT_EQ( response, encoded );

        const HIL_Application_Message_T decoded = DecodeMessage( response );
        ASSERT_EQ( decoded.type, HIL_APPLICATION_MESSAGE_TYPE_ERROR );
        EXPECT_EQ( decoded.has_test_id, error.has_test_id );
        EXPECT_EQ( decoded.body.error.has_tick_number, error.body.error.has_tick_number );
        EXPECT_EQ( decoded.body.error.tick_number, error.body.error.tick_number );
        EXPECT_EQ( decoded.body.error.detail, error.body.error.detail );
        EXPECT_EQ( decoded.body.error.diagnostic_data.size, error.body.error.diagnostic_data.size );
        if ( error.body.error.diagnostic_data.size != 0U )
        {
            EXPECT_EQ( std::memcmp( decoded.body.error.diagnostic_data.data,
                                    error.body.error.diagnostic_data.data,
                                    error.body.error.diagnostic_data.size ),
                       0 );
        }
    }

    ExpectWorkflowCountersUnchanged( before );
}

TEST_F( ApplicationHarnessTest, TestOnlyRoundTripEncodeFailurePublishesNoOutput )
{
    const std::vector<uint8_t> encoded =
        EncodeMessage( ApplicationResponse( HIL_APPLICATION_RESPONSE_SCOPE_TICK ) );
    ASSERT_GT( encoded.size(), 1U );
    const auto before = *APPLICATION_TEST_HARNESS_Get_Diagnostics();

    std::vector<uint8_t> response;
    EXPECT_EQ( Handle( encoded, &response, encoded.size() - 1U ),
               HIL_APPLICATION_STATUS_BUFFER_TOO_SMALL );
    EXPECT_TRUE( response.empty() );
    EXPECT_EQ( APPLICATION_TEST_HARNESS_Get_Diagnostics()->encode_failures,
               before.encode_failures + 1U );
    ExpectWorkflowCountersUnchanged( before );
}

TEST_F( ApplicationHarnessTest, AcceptsRepresentativeConfigurationWithExactSizeAndDigest )
{
    const HIL_Application_Message_T config  = RepresentativeConfiguration();
    const std::vector<uint8_t>      encoded = EncodeMessage( config );
    ASSERT_EQ( encoded.size(), 210U );

    std::vector<uint8_t> response;
    EXPECT_EQ( Handle( encoded, &response ), HIL_APPLICATION_STATUS_OK );
    EXPECT_FALSE( response.empty() );

    const auto* diagnostics = APPLICATION_TEST_HARNESS_Get_Diagnostics();
    EXPECT_EQ( diagnostics->configurations_accepted, 1U );
    EXPECT_EQ( diagnostics->state, APPLICATION_TEST_HARNESS_STATE_ACCEPTING_INSTRUCTIONS );
    EXPECT_EQ( diagnostics->next_expected_tick, 0U );
    EXPECT_EQ( diagnostics->active_expected_tick_count, 3U );
    EXPECT_EQ( diagnostics->configuration_digest, kRepresentativeConfigurationDigest );
    EXPECT_EQ( diagnostics->last_decoded_message_type,
               HIL_APPLICATION_MESSAGE_TYPE_TEST_CONFIGURATION );

    const HIL_Application_Message_T decoded = DecodeMessage( encoded );
    const auto&                     body    = decoded.body.test_configuration;
    EXPECT_EQ( body.digital_in[0].voltage_level, HIL_APPLICATION_PERIPHERAL_CONFIG_3V3 );
    EXPECT_EQ( body.digital_out[9].voltage_level, HIL_APPLICATION_PERIPHERAL_CONFIG_24V );
    EXPECT_EQ( body.analog_in[0].enabled, 1U );
    EXPECT_EQ( body.analog_out[5].enabled, 1U );
    EXPECT_EQ( body.pwm_in[1].voltage_level, HIL_APPLICATION_PERIPHERAL_CONFIG_24V );
    EXPECT_EQ( body.pwm_out[0].initial_period_nanoseconds, 1000000U );
    EXPECT_EQ( body.can[0].filter_id, 0x123U );
    EXPECT_EQ( body.can[0].filter_mask, 0x7FFU );
    EXPECT_EQ( body.can[1].filter_id, 0x400U );
    EXPECT_EQ( body.can[1].filter_mask, 0x700U );
    EXPECT_EQ( body.spi[1].data_width, HIL_APPLICATION_SPI_DATA_WIDTH_16_BITS );
    EXPECT_EQ( body.uart[1].electrical_mode, HIL_APPLICATION_UART_ELECTRICAL_MODE_RS232 );
    EXPECT_EQ( body.i2c[1].own_address_7bit, 0U );
    EXPECT_EQ( body.extension_data.size, kRepresentativeExtension.size() );
}

TEST_F( ApplicationHarnessTest, AcceptsCanonicalAllDisabledConfiguration )
{
    const std::vector<uint8_t> encoded = EncodeMessage( DisabledConfiguration() );
    ASSERT_EQ( encoded.size(), 194U );
    EXPECT_EQ( Handle( encoded ), HIL_APPLICATION_STATUS_OK );
    const auto* diagnostics = APPLICATION_TEST_HARNESS_Get_Diagnostics();
    EXPECT_EQ( diagnostics->configuration_digest, kDisabledConfigurationDigest );
    EXPECT_EQ( diagnostics->active_expected_tick_count, 1U );
}

TEST_F( ApplicationHarnessTest, AcceptsMaximumExtensionUsingCorrectlyAlignedStaticStorage )
{
    std::array<uint8_t, 255> extension{};
    for ( size_t i = 0U; i < extension.size(); ++i )
    {
        extension[i] = static_cast<uint8_t>( ( i * 37U ) & 0xFFU );
    }
    const HIL_Application_Message_T config = RepresentativeConfiguration(
        extension.data(), static_cast<uint8_t>( extension.size() ), 1U );
    const std::vector<uint8_t> encoded = EncodeMessage( config );
    ASSERT_EQ( encoded.size(), 449U );
    EXPECT_EQ( APPLICATION_TEST_HARNESS_Test_Decode_Storage_Address()
                   % HIL_APPLICATION_DECODE_STORAGE_ALIGNMENT,
               0U );
    EXPECT_EQ( Handle( encoded ), HIL_APPLICATION_STATUS_OK );
    EXPECT_EQ( APPLICATION_TEST_HARNESS_Get_Diagnostics()->configuration_digest,
               kMaximumExtensionDigest );
}

TEST_F( ApplicationHarnessTest, MaximumExtensionConfigurationProducesExactTickZeroResultOnce )
{
    std::array<uint8_t, 255> extension{};
    for ( size_t i = 0U; i < extension.size(); ++i )
    {
        extension[i] = static_cast<uint8_t>( ( i * 37U ) & 0xFFU );
    }
    const auto config         = RepresentativeConfiguration( extension.data(),
                                                             static_cast<uint8_t>( extension.size() ), 1U );
    const auto encoded_config = EncodeMessage( config );
    ASSERT_EQ( encoded_config.size(), 449U );
    std::vector<uint8_t> response;
    ASSERT_EQ( Handle( encoded_config, &response ), HIL_APPLICATION_STATUS_OK );
    ASSERT_FALSE( response.empty() );

    const auto* diagnostics = APPLICATION_TEST_HARNESS_Get_Diagnostics();
    EXPECT_EQ( diagnostics->configuration_digest, kMaximumExtensionDigest );
    EXPECT_EQ( diagnostics->configurations_accepted, 1U );
    EXPECT_EQ( diagnostics->instructions_accepted, 0U );
    EXPECT_EQ( diagnostics->results_encoded, 0U );
    EXPECT_EQ( diagnostics->state, APPLICATION_TEST_HARNESS_STATE_ACCEPTING_INSTRUCTIONS );
    EXPECT_EQ( diagnostics->next_expected_tick, 0U );
    EXPECT_EQ( diagnostics->active_expected_tick_count, 1U );

    const auto source      = Instruction( 0U );
    const auto instruction = EncodeMessage( source );
    ASSERT_EQ( instruction.size(), 73U );
    ASSERT_EQ( Handle( instruction, &response ), HIL_APPLICATION_STATUS_OK );
    ASSERT_EQ( DecodeMessage( response ).body.response.scope,
                 HIL_APPLICATION_RESPONSE_SCOPE_TICK );
    EXPECT_EQ( diagnostics->instruction_digest, kInstructionDigests[0] );
    ASSERT_EQ( Handle( EncodeMessage( FinalizeUpload( source.test_id ) ), &response ),
               HIL_APPLICATION_STATUS_OK );
    ASSERT_EQ( DecodeMessage( response ).body.response.scope,
                 HIL_APPLICATION_RESPONSE_SCOPE_COMPLETE_TEST );
    ASSERT_EQ( Handle( EncodeMessage( Start( source.test_id ) ), &response ),
               HIL_APPLICATION_STATUS_OK );
    ASSERT_EQ( DecodeMessage( response ).body.response.scope,
                 HIL_APPLICATION_RESPONSE_SCOPE_EXECUTION_CONTROL );
    const auto decoded = PollResult();
    ASSERT_EQ( decoded.type, HIL_APPLICATION_MESSAGE_TYPE_TEST_RESULT );
    const auto& result = decoded.body.test_result;
    EXPECT_EQ( result.tick_number, 0U );
    EXPECT_EQ( result.condition, HIL_APPLICATION_RESULT_CONDITION_OK );
    EXPECT_EQ( result.problem_detail, 0U );
    EXPECT_EQ( result.analog_inputs[0].microvolts, 11496510U );
    EXPECT_EQ( result.analog_inputs[1].microvolts, kExpectedAnalogInput1[0] );
    for ( size_t i = 0U; i < HIL_APPLICATION_DIGITAL_INPUT_CHANNEL_COUNT; ++i )
    {
        EXPECT_EQ( result.digital_inputs[i].high,
                   source.body.test_instruction.digital_outputs[i].high );
    }
    for ( size_t i = 0U; i < HIL_APPLICATION_PWM_INPUT_CHANNEL_COUNT; ++i )
    {
        EXPECT_EQ( result.pwm_inputs[i].period_nanoseconds,
                   source.body.test_instruction.pwm_outputs[i].period_nanoseconds );
        EXPECT_EQ( result.pwm_inputs[i].duty_cycle_permyriad,
                   source.body.test_instruction.pwm_outputs[i].duty_cycle_permyriad );
    }

    EXPECT_EQ( diagnostics->application_messages_received, 5U );
    EXPECT_EQ( diagnostics->configurations_accepted, 1U );
    EXPECT_EQ( diagnostics->instructions_accepted, 1U );
    EXPECT_EQ( diagnostics->results_encoded, 1U );
    EXPECT_EQ( diagnostics->next_expected_tick, 1U );
    EXPECT_EQ( diagnostics->state, APPLICATION_TEST_HARNESS_STATE_COMPLETE );
    EXPECT_EQ( diagnostics->semantic_rejections, 0U );
    EXPECT_EQ( diagnostics->decode_failures, 0U );
    EXPECT_EQ( diagnostics->encode_failures, 0U );

    size_t no_output_size = 0U;
    EXPECT_EQ( APPLICATION_TEST_HARNESS_Poll_Output( response_buffer_.data(), response_buffer_.size(),
                                                     &no_output_size ),
               HIL_APPLICATION_STATUS_INCOMPLETE_DATA );
    EXPECT_EQ( diagnostics->application_messages_received, 5U );
    EXPECT_EQ( diagnostics->semantic_rejections, 0U );
    EXPECT_EQ( diagnostics->configurations_accepted, 1U );
    EXPECT_EQ( diagnostics->instructions_accepted, 1U );
    EXPECT_EQ( diagnostics->results_encoded, 1U );
    EXPECT_EQ( diagnostics->next_expected_tick, 1U );
    EXPECT_EQ( diagnostics->state, APPLICATION_TEST_HARNESS_STATE_COMPLETE );
    EXPECT_EQ( diagnostics->configuration_digest, kMaximumExtensionDigest );
    EXPECT_EQ( diagnostics->instruction_digest, kInstructionDigests[0] );
}

TEST_F( ApplicationHarnessTest, OmittedFixedTickUsesConfiguredInitialOutputState )
{
    auto config = RepresentativeConfiguration( nullptr, 0U, 1U );
    const auto test_id = config.test_id;
    std::vector<uint8_t> response;
    ASSERT_EQ( Handle( EncodeMessage( config ), &response ), HIL_APPLICATION_STATUS_OK );
    ASSERT_EQ( Handle( EncodeMessage( FinalizeUpload( test_id ) ), &response ),
               HIL_APPLICATION_STATUS_OK );
    ASSERT_EQ( Handle( EncodeMessage( Start( test_id ) ), &response ), HIL_APPLICATION_STATUS_OK );

    const auto decoded = PollResult();
    ASSERT_EQ( decoded.type, HIL_APPLICATION_MESSAGE_TYPE_TEST_RESULT );
    for ( size_t i = 0U; i < HIL_APPLICATION_DIGITAL_INPUT_CHANNEL_COUNT; ++i )
    {
        EXPECT_EQ( decoded.body.test_result.digital_inputs[i].high, i % 2U );
    }
    EXPECT_EQ( decoded.body.test_result.pwm_inputs[0].period_nanoseconds, 1000000U );
    EXPECT_EQ( decoded.body.test_result.pwm_inputs[0].duty_cycle_permyriad, 2500U );
    EXPECT_EQ( decoded.body.test_result.pwm_inputs[1].period_nanoseconds, 2000000U );
    EXPECT_EQ( decoded.body.test_result.pwm_inputs[1].duty_cycle_permyriad, 7500U );
}

TEST_F( ApplicationHarnessTest, ThreeGoldenInstructionsProduceExactFixedResults )
{
    ASSERT_EQ( Handle( EncodeMessage( RepresentativeConfiguration() ) ),
               HIL_APPLICATION_STATUS_OK );

    for ( uint32_t tick = 0U; tick < 3U; ++tick )
    {
        const std::vector<uint8_t> instruction = EncodeMessage( Instruction( tick ) );
        ASSERT_EQ( instruction.size(), 73U );
        std::vector<uint8_t> response;
        ASSERT_EQ( Handle( instruction, &response ), HIL_APPLICATION_STATUS_OK );
        ASSERT_EQ( DecodeMessage( response ).body.response.scope,
                   HIL_APPLICATION_RESPONSE_SCOPE_TICK );

        const auto* diagnostics = APPLICATION_TEST_HARNESS_Get_Diagnostics();
        EXPECT_EQ( diagnostics->instruction_digest, kInstructionDigests[tick] );
        EXPECT_EQ( diagnostics->next_expected_tick, tick + 1U );
    }

    std::vector<uint8_t> response;
    ASSERT_EQ( Handle( EncodeMessage( FinalizeUpload( MakeTestId() ) ), &response ),
               HIL_APPLICATION_STATUS_OK );
    ASSERT_EQ( Handle( EncodeMessage( Start( MakeTestId() ) ), &response ),
               HIL_APPLICATION_STATUS_OK );
    for ( uint32_t tick = 0U; tick < 3U; ++tick )
    {
        const auto decoded = PollResult();
        ASSERT_EQ( decoded.type, HIL_APPLICATION_MESSAGE_TYPE_TEST_RESULT );
        const auto& result = decoded.body.test_result;
        EXPECT_EQ( result.tick_number, tick );
        EXPECT_EQ( result.condition, HIL_APPLICATION_RESULT_CONDITION_OK );
        EXPECT_EQ( result.problem_detail, 0U );
        EXPECT_EQ( result.analog_inputs[0].microvolts, kExpectedAnalogInput0 );
        EXPECT_EQ( result.analog_inputs[1].microvolts, kExpectedAnalogInput1[tick] );
    }

    const auto* diagnostics = APPLICATION_TEST_HARNESS_Get_Diagnostics();
    EXPECT_EQ( diagnostics->instructions_accepted, 3U );
    EXPECT_EQ( diagnostics->results_encoded, 3U );
    EXPECT_EQ( diagnostics->state, APPLICATION_TEST_HARNESS_STATE_COMPLETE );
}

TEST_F( ApplicationHarnessTest, SparseVariableUploadRetainsTwoChunksAndEmitsEmptyOmittedTick )
{
    const auto test_id = MakeTestId();
    constexpr std::array<uint8_t, 8> profile = { 'H', 'T', 'V', '3', 1U, 0U, 0U, 16U };
    ASSERT_EQ( Handle( EncodeMessage( RepresentativeConfiguration(
        profile.data(), static_cast<uint8_t>( profile.size() ), 3U ) ) ),
               HIL_APPLICATION_STATUS_OK );

    const std::array<uint8_t, 2> digital = { 1U, 0U };
    const std::array<uint8_t, 4> analog = { 0xE4U, 0x0CU, 0U, 0U };
    const std::array<uint8_t, 6> pwm = { 0x40U, 0x42U, 0x0FU, 0U, 0xC4U, 0x09U };
    const std::array<uint8_t, 2> uart = { 'H', 'T' };
    const std::array<uint8_t, 4> spi = { 1U, 2U, 'H', 'T' };
    const std::array<uint8_t, 12> can = { 0x23U, 0x01U, 3U, 'C', 'A', 'N', 0U, 0U,
                                          0U,    0U,    0U, 0U };
    const std::array<HIL_Application_Logical_Operation_T, 3> first_chunk = {
        HIL_Application_Logical_Operation_T{ HIL_APPLICATION_PERIPHERAL_DIGITAL_OUTPUT, 0U,
                                             { digital.data(), static_cast<uint8_t>( digital.size() ) } },
        HIL_Application_Logical_Operation_T{ HIL_APPLICATION_PERIPHERAL_ANALOG_OUTPUT, 0U,
                                             { analog.data(), static_cast<uint8_t>( analog.size() ) } },
        HIL_Application_Logical_Operation_T{ HIL_APPLICATION_PERIPHERAL_PWM_OUTPUT, 0U,
                                             { pwm.data(), static_cast<uint8_t>( pwm.size() ) } },
    };
    const std::array<HIL_Application_Logical_Operation_T, 3> second_chunk = {
        HIL_Application_Logical_Operation_T{ HIL_APPLICATION_PERIPHERAL_UART, 0U,
                                             { uart.data(), static_cast<uint8_t>( uart.size() ) } },
        HIL_Application_Logical_Operation_T{ HIL_APPLICATION_PERIPHERAL_SPI, 0U,
                                             { spi.data(), static_cast<uint8_t>( spi.size() ) } },
        HIL_Application_Logical_Operation_T{ HIL_APPLICATION_PERIPHERAL_CAN, 0U,
                                             { can.data(), static_cast<uint8_t>( can.size() ) } },
    };

    std::vector<uint8_t> response;
    EXPECT_EQ( Handle( EncodeMessage( Update( test_id, 0U,
                                              HIL_APPLICATION_INSTRUCTION_FLAG_HAS_MORE_CHUNKS,
                                              first_chunk.data(),
                                              static_cast<uint8_t>( first_chunk.size() ) ) ),
                       &response ),
               HIL_APPLICATION_STATUS_OK );
    EXPECT_TRUE( response.empty() );
    EXPECT_EQ( Handle( EncodeMessage( Update( test_id, 0U,
                                              HIL_APPLICATION_INSTRUCTION_FLAG_COMPLETE_TICK,
                                              second_chunk.data(),
                                              static_cast<uint8_t>( second_chunk.size() ) ) ),
                       &response ),
               HIL_APPLICATION_STATUS_OK );
    EXPECT_EQ( DecodeMessage( response ).body.response.scope,
               HIL_APPLICATION_RESPONSE_SCOPE_TICK );

    EXPECT_EQ( Handle( EncodeMessage( Update( test_id, 2U,
                                              HIL_APPLICATION_INSTRUCTION_FLAG_HAS_MORE_CHUNKS,
                                              first_chunk.data(),
                                              static_cast<uint8_t>( first_chunk.size() ) ) ),
                       &response ),
               HIL_APPLICATION_STATUS_OK );
    EXPECT_TRUE( response.empty() );
    EXPECT_EQ( Handle( EncodeMessage( Update( test_id, 2U,
                                              HIL_APPLICATION_INSTRUCTION_FLAG_COMPLETE_TICK,
                                              second_chunk.data(),
                                              static_cast<uint8_t>( second_chunk.size() ) ) ),
                       &response ),
               HIL_APPLICATION_STATUS_OK );
    EXPECT_EQ( DecodeMessage( response ).body.response.scope,
               HIL_APPLICATION_RESPONSE_SCOPE_TICK );

    EXPECT_EQ( Handle( EncodeMessage( FinalizeUpload( test_id ) ), &response ),
               HIL_APPLICATION_STATUS_OK );
    EXPECT_EQ( Handle( EncodeMessage( Start( test_id ) ), &response ), HIL_APPLICATION_STATUS_OK );

    const auto first_result = PollResult();
    ASSERT_EQ( first_result.type, HIL_APPLICATION_MESSAGE_TYPE_VARIABLE_TEST_RESULT );
    EXPECT_EQ( first_result.body.variable_test_result.tick_number, 0U );
    EXPECT_EQ( first_result.body.variable_test_result.record_count, 6U );
    const auto omitted_result = PollResult();
    ASSERT_EQ( omitted_result.type, HIL_APPLICATION_MESSAGE_TYPE_VARIABLE_TEST_RESULT );
    EXPECT_EQ( omitted_result.body.variable_test_result.tick_number, 1U );
    EXPECT_EQ( omitted_result.body.variable_test_result.record_count, 0U );
    const auto third_result = PollResult();
    ASSERT_EQ( third_result.type, HIL_APPLICATION_MESSAGE_TYPE_VARIABLE_TEST_RESULT );
    EXPECT_EQ( third_result.body.variable_test_result.tick_number, 2U );
    EXPECT_EQ( third_result.body.variable_test_result.record_count, 6U );
    EXPECT_EQ( APPLICATION_TEST_HARNESS_Get_Diagnostics()->state,
               APPLICATION_TEST_HARNESS_STATE_COMPLETE );
}

TEST_F( ApplicationHarnessTest, CompletedFixedTransactionCanBeFollowedByVariableTransaction )
{
    const auto fixed_id = MakeTestId( 0x21U );
    auto       fixed_config = RepresentativeConfiguration( nullptr, 0U, 1U );
    fixed_config.test_id = fixed_id;
    ASSERT_EQ( Handle( EncodeMessage( fixed_config ) ), HIL_APPLICATION_STATUS_OK );

    auto fixed_instruction = Instruction( 0U );
    fixed_instruction.test_id = fixed_id;
    std::vector<uint8_t> response;
    ASSERT_EQ( Handle( EncodeMessage( fixed_instruction ), &response ),
               HIL_APPLICATION_STATUS_OK );
    ASSERT_EQ( Handle( EncodeMessage( FinalizeUpload( fixed_id ) ), &response ),
               HIL_APPLICATION_STATUS_OK );
    ASSERT_EQ( Handle( EncodeMessage( Start( fixed_id ) ), &response ),
               HIL_APPLICATION_STATUS_OK );
    ASSERT_EQ( PollResult().type, HIL_APPLICATION_MESSAGE_TYPE_TEST_RESULT );

    constexpr std::array<uint8_t, 8> profile = { 'H', 'T', 'V', '3', 1U, 0U, 0U, 16U };
    const auto variable_id = MakeTestId( 0x31U );
    auto       variable_config = RepresentativeConfiguration( profile.data(),
                                                               static_cast<uint8_t>( profile.size() ),
                                                               2U );
    variable_config.test_id = variable_id;
    ASSERT_EQ( Handle( EncodeMessage( variable_config ), &response ),
               HIL_APPLICATION_STATUS_OK );

    constexpr std::array<uint8_t, 2> uart = { 'N', 'E' };
    const std::array<HIL_Application_Logical_Operation_T, 1> operations = {
        HIL_Application_Logical_Operation_T{ HIL_APPLICATION_PERIPHERAL_UART, 0U,
                                             { uart.data(), static_cast<uint8_t>( uart.size() ) } },
    };
    ASSERT_EQ( Handle( EncodeMessage( Update( variable_id, 0U,
                                               HIL_APPLICATION_INSTRUCTION_FLAG_COMPLETE_TICK,
                                               operations.data(),
                                               static_cast<uint8_t>( operations.size() ) ) ),
                       &response ),
               HIL_APPLICATION_STATUS_OK );
    ASSERT_EQ( Handle( EncodeMessage( FinalizeUpload( variable_id ) ), &response ),
               HIL_APPLICATION_STATUS_OK );
    ASSERT_EQ( Handle( EncodeMessage( Start( variable_id ) ), &response ),
               HIL_APPLICATION_STATUS_OK );

    const auto first = PollResult();
    ASSERT_EQ( first.type, HIL_APPLICATION_MESSAGE_TYPE_VARIABLE_TEST_RESULT );
    ASSERT_EQ( first.body.variable_test_result.record_count, 1U );
    EXPECT_EQ( first.body.variable_test_result.records[0].data.size, uart.size() );
    EXPECT_EQ( std::memcmp( first.body.variable_test_result.records[0].data.data, uart.data(),
                            uart.size() ),
               0 );
    EXPECT_EQ( PollResult().body.variable_test_result.record_count, 0U );
    EXPECT_EQ( APPLICATION_TEST_HARNESS_Get_Diagnostics()->state,
               APPLICATION_TEST_HARNESS_STATE_COMPLETE );
}

TEST_F( ApplicationHarnessTest, NewVariableTransactionClearsOmittedTickAndOldRecords )
{
    constexpr std::array<uint8_t, 8> profile = { 'H', 'T', 'V', '3', 1U, 0U, 0U, 16U };
    constexpr std::array<uint8_t, 3> old_data = { 'O', 'L', 'D' };
    constexpr std::array<uint8_t, 3> new_data = { 'N', 'E', 'W' };
    const auto first_id = MakeTestId( 0x41U );
    auto first_config = RepresentativeConfiguration( profile.data(),
                                                     static_cast<uint8_t>( profile.size() ), 3U );
    first_config.test_id = first_id;
    ASSERT_EQ( Handle( EncodeMessage( first_config ) ), HIL_APPLICATION_STATUS_OK );
    const std::array<HIL_Application_Logical_Operation_T, 1> old_operation = {
        HIL_Application_Logical_Operation_T{ HIL_APPLICATION_PERIPHERAL_UART, 0U,
                                             { old_data.data(), static_cast<uint8_t>( old_data.size() ) } },
    };
    std::vector<uint8_t> response;
    ASSERT_EQ( Handle( EncodeMessage( Update( first_id, 0U,
                                               HIL_APPLICATION_INSTRUCTION_FLAG_COMPLETE_TICK,
                                               old_operation.data(), 1U ) ),
                       &response ),
               HIL_APPLICATION_STATUS_OK );
    ASSERT_EQ( Handle( EncodeMessage( FinalizeUpload( first_id ) ), &response ),
               HIL_APPLICATION_STATUS_OK );
    ASSERT_EQ( Handle( EncodeMessage( Start( first_id ) ), &response ),
               HIL_APPLICATION_STATUS_OK );
    EXPECT_EQ( PollResult().body.variable_test_result.record_count, 1U );
    EXPECT_EQ( PollResult().body.variable_test_result.record_count, 0U );
    EXPECT_EQ( PollResult().body.variable_test_result.record_count, 0U );

    const auto second_id = MakeTestId( 0x51U );
    auto second_config = RepresentativeConfiguration( profile.data(),
                                                      static_cast<uint8_t>( profile.size() ), 3U );
    second_config.test_id = second_id;
    ASSERT_EQ( Handle( EncodeMessage( second_config ) ), HIL_APPLICATION_STATUS_OK );
    const std::array<HIL_Application_Logical_Operation_T, 1> new_operation = {
        HIL_Application_Logical_Operation_T{ HIL_APPLICATION_PERIPHERAL_UART, 0U,
                                             { new_data.data(), static_cast<uint8_t>( new_data.size() ) } },
    };
    ASSERT_EQ( Handle( EncodeMessage( Update( second_id, 2U,
                                               HIL_APPLICATION_INSTRUCTION_FLAG_COMPLETE_TICK,
                                               new_operation.data(), 1U ) ),
                       &response ),
               HIL_APPLICATION_STATUS_OK );
    ASSERT_EQ( Handle( EncodeMessage( FinalizeUpload( second_id ) ), &response ),
               HIL_APPLICATION_STATUS_OK );
    ASSERT_EQ( Handle( EncodeMessage( Start( second_id ) ), &response ),
               HIL_APPLICATION_STATUS_OK );

    EXPECT_EQ( PollResult().body.variable_test_result.record_count, 0U );
    EXPECT_EQ( PollResult().body.variable_test_result.record_count, 0U );
    const auto third = PollResult();
    ASSERT_EQ( third.body.variable_test_result.record_count, 1U );
    EXPECT_EQ( std::memcmp( third.body.variable_test_result.records[0].data.data, new_data.data(),
                            new_data.size() ),
               0 );
}

TEST_F( ApplicationHarnessTest, RepeatedCommunicationRecordsEmitEightOrderedResultChunks )
{
    constexpr std::array<uint8_t, 8> profile = { 'H', 'T', 'V', '3', 1U, 0U, 0U, 16U };
    const auto test_id = MakeTestId( 0x61U );
    auto config = RepresentativeConfiguration( profile.data(),
                                               static_cast<uint8_t>( profile.size() ), 1U );
    config.test_id = test_id;
    ASSERT_EQ( Handle( EncodeMessage( config ) ), HIL_APPLICATION_STATUS_OK );

    constexpr std::array<uint8_t, 2> uart = { 'U', '0' };
    constexpr std::array<uint8_t, 4> spi = { 1U, 2U, 'S', '0' };
    constexpr std::array<uint8_t, 12> can = { 0x23U, 0x01U, 3U, 'C', '0', 0U, 0U,
                                              0U,    0U,    0U, 0U, 0U };
    std::vector<uint8_t> response;
    for ( uint8_t chunk = 0U; chunk < 8U; ++chunk )
    {
        const std::array<HIL_Application_Logical_Operation_T, 6> operations = {
            HIL_Application_Logical_Operation_T{ HIL_APPLICATION_PERIPHERAL_UART, 0U,
                                                 { uart.data(), static_cast<uint8_t>( uart.size() ) } },
            HIL_Application_Logical_Operation_T{ HIL_APPLICATION_PERIPHERAL_UART, 1U,
                                                 { uart.data(), static_cast<uint8_t>( uart.size() ) } },
            HIL_Application_Logical_Operation_T{ HIL_APPLICATION_PERIPHERAL_SPI, 0U,
                                                 { spi.data(), static_cast<uint8_t>( spi.size() ) } },
            HIL_Application_Logical_Operation_T{ HIL_APPLICATION_PERIPHERAL_SPI, 1U,
                                                 { spi.data(), static_cast<uint8_t>( spi.size() ) } },
            HIL_Application_Logical_Operation_T{ HIL_APPLICATION_PERIPHERAL_CAN, 0U,
                                                 { can.data(), static_cast<uint8_t>( can.size() ) } },
            HIL_Application_Logical_Operation_T{ HIL_APPLICATION_PERIPHERAL_CAN, 1U,
                                                 { can.data(), static_cast<uint8_t>( can.size() ) } },
        };
        const uint8_t flags = chunk < 7U ? HIL_APPLICATION_INSTRUCTION_FLAG_HAS_MORE_CHUNKS
                                         : HIL_APPLICATION_INSTRUCTION_FLAG_COMPLETE_TICK;
        ASSERT_EQ( Handle( EncodeMessage( Update( test_id, 0U, flags, operations.data(),
                                                  static_cast<uint8_t>( operations.size() ) ) ),
                           &response ),
                   HIL_APPLICATION_STATUS_OK );
        if ( chunk < 7U ) EXPECT_TRUE( response.empty() );
    }
    ASSERT_EQ( Handle( EncodeMessage( FinalizeUpload( test_id ) ), &response ),
               HIL_APPLICATION_STATUS_OK );
    ASSERT_EQ( Handle( EncodeMessage( Start( test_id ) ), &response ), HIL_APPLICATION_STATUS_OK );

    for ( uint8_t chunk = 0U; chunk < 8U; ++chunk )
    {
        const auto decoded = PollResult();
        ASSERT_EQ( decoded.type, HIL_APPLICATION_MESSAGE_TYPE_VARIABLE_TEST_RESULT );
        const auto& result = decoded.body.variable_test_result;
        EXPECT_EQ( result.tick_number, 0U );
        EXPECT_EQ( result.record_count, 6U );
        EXPECT_EQ( result.flags, chunk < 7U ? HIL_APPLICATION_RESULT_FLAG_HAS_MORE_CHUNKS
                                            : HIL_APPLICATION_RESULT_FLAG_COMPLETE_TICK );
        EXPECT_EQ( result.records[0].peripheral_type, HIL_APPLICATION_PERIPHERAL_UART );
        EXPECT_EQ( result.records[2].peripheral_type, HIL_APPLICATION_PERIPHERAL_SPI );
        EXPECT_EQ( result.records[2].data.size, 2U );
        EXPECT_EQ( std::memcmp( result.records[2].data.data, "S0", 2U ), 0 );
        EXPECT_EQ( result.records[4].peripheral_type, HIL_APPLICATION_PERIPHERAL_CAN );
    }
    const auto* diagnostics = APPLICATION_TEST_HARNESS_Get_Diagnostics();
    EXPECT_EQ( diagnostics->result_messages_emitted, 8U );
    EXPECT_EQ( diagnostics->result_records_emitted, 48U );
    EXPECT_EQ( diagnostics->results_encoded, 1U );
    EXPECT_EQ( diagnostics->maximum_chunk_count, 8U );
}

TEST_F( ApplicationHarnessTest, RejectsNinthVariableUploadChunkWithCorrelatedTickResponse )
{
    constexpr std::array<uint8_t, 8> profile = { 'H', 'T', 'V', '3', 1U, 0U, 0U, 16U };
    const auto test_id = MakeTestId( 0x62U );
    auto config = RepresentativeConfiguration( profile.data(), static_cast<uint8_t>( profile.size() ), 1U );
    config.test_id = test_id;
    ASSERT_EQ( Handle( EncodeMessage( config ) ), HIL_APPLICATION_STATUS_OK );

    constexpr std::array<uint8_t, 2> uart = { 'U', '9' };
    const std::array<HIL_Application_Logical_Operation_T, 1> operations = {
        HIL_Application_Logical_Operation_T{ HIL_APPLICATION_PERIPHERAL_UART, 0U,
                                             { uart.data(), static_cast<uint8_t>( uart.size() ) } },
    };
    std::vector<uint8_t> response;
    for ( uint8_t chunk = 0U; chunk < 8U; ++chunk )
    {
        ASSERT_EQ( Handle( EncodeMessage( Update( test_id, 0U,
                                                   HIL_APPLICATION_INSTRUCTION_FLAG_HAS_MORE_CHUNKS,
                                                   operations.data(), 1U ) ),
                           &response ),
                   HIL_APPLICATION_STATUS_OK );
        EXPECT_TRUE( response.empty() );
    }

    EXPECT_EQ( Handle( EncodeMessage( Update( test_id, 0U,
                                               HIL_APPLICATION_INSTRUCTION_FLAG_HAS_MORE_CHUNKS,
                                               operations.data(), 1U ) ),
                       &response ),
               HIL_APPLICATION_STATUS_BUFFER_TOO_SMALL );
    ASSERT_FALSE( response.empty() );
    const auto decoded = DecodeMessage( response );
    EXPECT_EQ( decoded.test_id.bytes[0], 0x62U );
    EXPECT_EQ( decoded.body.response.scope, HIL_APPLICATION_RESPONSE_SCOPE_TICK );
    EXPECT_EQ( decoded.body.response.outcome, HIL_APPLICATION_RESPONSE_OUTCOME_REJECTED );
    EXPECT_EQ( decoded.body.response.reason, HIL_APPLICATION_RESPONSE_REASON_STORAGE_UNAVAILABLE );
    EXPECT_EQ( APPLICATION_TEST_HARNESS_Get_Diagnostics()->state,
                APPLICATION_TEST_HARNESS_STATE_UPLOAD_INVALID );
}

TEST_F( ApplicationHarnessTest, DisabledInputsProduceZeroOracleValues )
{
    ASSERT_EQ( Handle( EncodeMessage( DisabledConfiguration() ) ), HIL_APPLICATION_STATUS_OK );
    HIL_Application_Message_T instruction = Instruction( 0U );
    instruction.test_id                   = MakeTestId( 0x30U );
    std::vector<uint8_t> response;
    ASSERT_EQ( Handle( EncodeMessage( instruction ), &response ), HIL_APPLICATION_STATUS_OK );
    ASSERT_EQ( Handle( EncodeMessage( FinalizeUpload( instruction.test_id ) ), &response ),
               HIL_APPLICATION_STATUS_OK );
    ASSERT_EQ( Handle( EncodeMessage( Start( instruction.test_id ) ), &response ),
               HIL_APPLICATION_STATUS_OK );
    const auto decoded = PollResult();
    for ( const auto& input : decoded.body.test_result.digital_inputs )
        EXPECT_EQ( input.high, 0U );
    for ( const auto& input : decoded.body.test_result.analog_inputs )
        EXPECT_EQ( input.microvolts, 0U );
    for ( const auto& input : decoded.body.test_result.pwm_inputs )
    {
        EXPECT_EQ( input.period_nanoseconds, 0U );
        EXPECT_EQ( input.duty_cycle_permyriad, 0U );
    }
}

TEST_F( ApplicationHarnessTest, RejectsInstructionBeforeConfiguration )
{
    std::vector<uint8_t> response;
    EXPECT_EQ( Handle( EncodeMessage( Instruction( 0U ) ), &response ),
               HIL_APPLICATION_STATUS_VALIDATION_FAILED );
    ASSERT_FALSE( response.empty() );
    const auto decoded = DecodeMessage( response );
    EXPECT_EQ( decoded.body.response.scope, HIL_APPLICATION_RESPONSE_SCOPE_TICK );
    EXPECT_EQ( decoded.body.response.outcome, HIL_APPLICATION_RESPONSE_OUTCOME_REJECTED );
    EXPECT_EQ( decoded.body.response.reason,
               HIL_APPLICATION_RESPONSE_REASON_OPERATION_NOT_ALLOWED );
    const auto* diagnostics = APPLICATION_TEST_HARNESS_Get_Diagnostics();
    EXPECT_EQ( diagnostics->state, APPLICATION_TEST_HARNESS_STATE_WAITING_FOR_CONFIGURATION );
    EXPECT_EQ( diagnostics->next_expected_tick, 0U );
    EXPECT_EQ( diagnostics->semantic_rejections, 1U );
}

TEST_F( ApplicationHarnessTest, RejectsWrongTestIdWithoutChangingTransaction )
{
    ASSERT_EQ( Handle( EncodeMessage( RepresentativeConfiguration() ) ),
               HIL_APPLICATION_STATUS_OK );
    HIL_Application_Message_T instruction = Instruction( 0U );
    instruction.test_id                   = MakeTestId( 0x70U );
    std::vector<uint8_t> response;
    EXPECT_EQ( Handle( EncodeMessage( instruction ), &response ),
               HIL_APPLICATION_STATUS_INCONSISTENT_TEST_ID );
    ASSERT_FALSE( response.empty() );
    const auto decoded = DecodeMessage( response );
    EXPECT_EQ( decoded.test_id.bytes[0], 0x70U );
    EXPECT_EQ( decoded.body.response.scope, HIL_APPLICATION_RESPONSE_SCOPE_TICK );
    EXPECT_EQ( decoded.body.response.outcome, HIL_APPLICATION_RESPONSE_OUTCOME_REJECTED );
    EXPECT_EQ( decoded.body.response.reason, HIL_APPLICATION_RESPONSE_REASON_INCONSISTENT_TEST_ID );
    const auto* diagnostics = APPLICATION_TEST_HARNESS_Get_Diagnostics();
    EXPECT_EQ( diagnostics->next_expected_tick, 0U );
    EXPECT_EQ( diagnostics->state, APPLICATION_TEST_HARNESS_STATE_UPLOAD_INVALID );
    EXPECT_EQ( diagnostics->instructions_accepted, 0U );
}

TEST_F( ApplicationHarnessTest, RejectsWrongDuplicateAndSkippedTicks )
{
    ASSERT_EQ( Handle( EncodeMessage( RepresentativeConfiguration() ) ),
               HIL_APPLICATION_STATUS_OK );
    EXPECT_EQ( Handle( EncodeMessage( Instruction( 1U ) ) ), HIL_APPLICATION_STATUS_OK );
    EXPECT_EQ( APPLICATION_TEST_HARNESS_Get_Diagnostics()->next_expected_tick, 2U );
    EXPECT_EQ( APPLICATION_TEST_HARNESS_Get_Diagnostics()->state,
               APPLICATION_TEST_HARNESS_STATE_ACCEPTING_INSTRUCTIONS );
    EXPECT_EQ( Handle( EncodeMessage( Instruction( 1U ) ) ),
               HIL_APPLICATION_STATUS_INCONSISTENT_TICK );
    EXPECT_EQ( APPLICATION_TEST_HARNESS_Get_Diagnostics()->state,
               APPLICATION_TEST_HARNESS_STATE_UPLOAD_INVALID );
}

TEST_F( ApplicationHarnessTest, RejectsConfigurationWhileActiveAndAcceptsNewOneAfterCompletion )
{
    ASSERT_EQ( Handle( EncodeMessage( RepresentativeConfiguration() ) ),
               HIL_APPLICATION_STATUS_OK );
    EXPECT_EQ( Handle( EncodeMessage( DisabledConfiguration() ) ),
               HIL_APPLICATION_STATUS_VALIDATION_FAILED );
    EXPECT_EQ( APPLICATION_TEST_HARNESS_Get_Diagnostics()->state,
               APPLICATION_TEST_HARNESS_STATE_UPLOAD_INVALID );

    APPLICATION_TEST_HARNESS_Reset_Transaction();
    std::vector<uint8_t> discovery_response;
    ASSERT_EQ( Handle( EncodeMessage( BasicSystemInfoRequest() ), &discovery_response ),
               HIL_APPLICATION_STATUS_OK );
    EXPECT_EQ( Handle( EncodeMessage( DisabledConfiguration() ) ), HIL_APPLICATION_STATUS_OK );
    const auto* diagnostics = APPLICATION_TEST_HARNESS_Get_Diagnostics();
    EXPECT_EQ( diagnostics->state, APPLICATION_TEST_HARNESS_STATE_ACCEPTING_INSTRUCTIONS );
    EXPECT_EQ( diagnostics->active_expected_tick_count, 1U );
    EXPECT_EQ( diagnostics->next_expected_tick, 0U );
    EXPECT_EQ( diagnostics->configuration_digest, kDisabledConfigurationDigest );
}

TEST_F( ApplicationHarnessTest, RejectsDecodedUnsupportedInboundResult )
{
    HIL_Application_Message_T result{};
    result.type                         = HIL_APPLICATION_MESSAGE_TYPE_TEST_RESULT;
    result.subtype                      = HIL_APPLICATION_MESSAGE_SUBTYPE_NONE;
    result.has_test_id                  = 1U;
    result.test_id                      = MakeTestId();
    result.body.test_result.tick_number = 0U;
    result.body.test_result.condition   = HIL_APPLICATION_RESULT_CONDITION_OK;
    EXPECT_EQ( Handle( EncodeMessage( result ) ), HIL_APPLICATION_STATUS_UNSUPPORTED_MESSAGE );
    const auto* diagnostics = APPLICATION_TEST_HARNESS_Get_Diagnostics();
    EXPECT_EQ( diagnostics->last_decoded_message_type, HIL_APPLICATION_MESSAGE_TYPE_TEST_RESULT );
    EXPECT_EQ( diagnostics->state, APPLICATION_TEST_HARNESS_STATE_WAITING_FOR_CONFIGURATION );
}

TEST_F( ApplicationHarnessTest, DecodeFailuresDoNotCorruptActiveTransaction )
{
    ASSERT_EQ( Handle( EncodeMessage( RepresentativeConfiguration() ) ),
               HIL_APPLICATION_STATUS_OK );
    const uint32_t digest_before = APPLICATION_TEST_HARNESS_Get_Diagnostics()->configuration_digest;

    std::vector<uint8_t> truncated = EncodeMessage( Instruction( 0U ) );
    ASSERT_GT( truncated.size(), 1U );
    truncated.pop_back();
    EXPECT_NE( Handle( truncated ), HIL_APPLICATION_STATUS_OK );

    std::vector<uint8_t> oversized = EncodeMessage( Instruction( 0U ) );
    oversized.push_back( 0U );
    EXPECT_NE( Handle( oversized ), HIL_APPLICATION_STATUS_OK );

    std::vector<uint8_t> structurally_invalid = EncodeMessage( Instruction( 0U ) );
    ASSERT_GT( structurally_invalid.size(), 27U );
    structurally_invalid[27] = 2U;  // First fixed digital-output value.
    EXPECT_NE( Handle( structurally_invalid ), HIL_APPLICATION_STATUS_OK );

    const auto* diagnostics = APPLICATION_TEST_HARNESS_Get_Diagnostics();
    EXPECT_EQ( diagnostics->state, APPLICATION_TEST_HARNESS_STATE_ACCEPTING_INSTRUCTIONS );
    EXPECT_EQ( diagnostics->next_expected_tick, 0U );
    EXPECT_EQ( diagnostics->active_expected_tick_count, 3U );
    EXPECT_EQ( diagnostics->configuration_digest, digest_before );
    EXPECT_EQ( diagnostics->instructions_accepted, 0U );
    EXPECT_GE( diagnostics->decode_failures, 3U );
}

TEST_F( ApplicationHarnessTest, ResultEncodeFailureDoesNotAdvanceTick )
{
    ASSERT_EQ( Handle( EncodeMessage( RepresentativeConfiguration() ) ),
               HIL_APPLICATION_STATUS_OK );
    std::vector<uint8_t> response;
    ASSERT_EQ( Handle( EncodeMessage( Instruction( 0U ) ), &response ), HIL_APPLICATION_STATUS_OK );
    ASSERT_EQ( Handle( EncodeMessage( FinalizeUpload( MakeTestId() ) ), &response ),
               HIL_APPLICATION_STATUS_OK );
    ASSERT_EQ( Handle( EncodeMessage( Start( MakeTestId() ) ), &response ),
               HIL_APPLICATION_STATUS_OK );
    const auto* diagnostics = APPLICATION_TEST_HARNESS_Get_Diagnostics();
    EXPECT_EQ( diagnostics->next_expected_tick, 1U );
    EXPECT_EQ( diagnostics->instructions_accepted, 1U );
    EXPECT_EQ( diagnostics->results_encoded, 0U );
    std::array<uint8_t, 512> output{};
    size_t                    output_size = 0U;
    EXPECT_EQ( APPLICATION_TEST_HARNESS_Poll_Output( output.data(), 61U, &output_size ),
               HIL_APPLICATION_STATUS_BUFFER_TOO_SMALL );
    EXPECT_EQ( diagnostics->next_expected_tick, 1U );
    EXPECT_EQ( diagnostics->results_encoded, 0U );
    EXPECT_GE( diagnostics->encode_failures, 1U );

    ASSERT_EQ( APPLICATION_TEST_HARNESS_Poll_Output( output.data(), output.size(), &output_size ),
               HIL_APPLICATION_STATUS_OK );
    ASSERT_EQ( APPLICATION_TEST_HARNESS_Commit_Output(), HIL_APPLICATION_STATUS_OK );
    EXPECT_EQ( output_size, 62U );
    EXPECT_EQ( APPLICATION_TEST_HARNESS_Get_Diagnostics()->next_expected_tick, 1U );
}

TEST_F( ApplicationHarnessTest, TransactionResetPreservesInitializationAndCumulativeDiagnostics )
{
    ASSERT_EQ( Handle( EncodeMessage( RepresentativeConfiguration() ) ),
               HIL_APPLICATION_STATUS_OK );
    ASSERT_EQ( Handle( EncodeMessage( Instruction( 0U ) ) ), HIL_APPLICATION_STATUS_OK );
    const auto before = *APPLICATION_TEST_HARNESS_Get_Diagnostics();

    APPLICATION_TEST_HARNESS_Reset_Transaction();
    const auto* after = APPLICATION_TEST_HARNESS_Get_Diagnostics();
    EXPECT_EQ( after->codec_initialized, 1U );
    EXPECT_EQ( after->state, APPLICATION_TEST_HARNESS_STATE_WAITING_FOR_CONFIGURATION );
    EXPECT_EQ( after->next_expected_tick, 0U );
    EXPECT_EQ( after->active_expected_tick_count, 0U );
    EXPECT_EQ( after->configurations_accepted, before.configurations_accepted );
    EXPECT_EQ( after->instructions_accepted, before.instructions_accepted );
    EXPECT_EQ( after->results_encoded, before.results_encoded );
    EXPECT_EQ( after->configuration_digest, before.configuration_digest );
    EXPECT_EQ( after->instruction_digest, before.instruction_digest );

    std::vector<uint8_t> response;
    EXPECT_EQ( Handle( EncodeMessage( RepresentativeConfiguration() ), &response ),
               HIL_APPLICATION_STATUS_OK );
    ASSERT_FALSE( response.empty() );
    EXPECT_EQ( DecodeMessage( response ).body.response.scope,
               HIL_APPLICATION_RESPONSE_SCOPE_TEST_CONFIGURATION );
}

}  // namespace
