#include "application_test_fixtures.hpp"

extern "C"
{
#include "application_test_harness.h"
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

constexpr uint32_t kRepresentativeConfigurationDigest = 0xDF35534CU;
constexpr uint32_t kDisabledConfigurationDigest       = 0x98E57BA3U;
constexpr uint32_t kMaximumExtensionDigest            = 0x60672F03U;

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
constexpr uint32_t kExpectedAnalogInput0 = 4813713U;

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
        HIL_Application_Message_T                                                    decoded{};
        alignas( HIL_APPLICATION_DECODE_STORAGE_ALIGNMENT ) std::array<uint8_t, 255> storage{};
        size_t                                                                       required = 0U;
        size_t                                                                       used     = 0U;
        EXPECT_EQ(
            HIL_APPLICATION_Decode_Storage_Size( &codec_, bytes.data(), bytes.size(), &required ),
            HIL_APPLICATION_STATUS_OK );
        EXPECT_LE( required, storage.size() );
        EXPECT_EQ( HIL_APPLICATION_Decode_Message( &codec_, bytes.data(), bytes.size(), &decoded,
                                                   required == 0U ? nullptr : storage.data(),
                                                   required == 0U ? 0U : storage.size(), &used ),
                   HIL_APPLICATION_STATUS_OK );
        EXPECT_EQ( used, required );
        return decoded;
    }

    HIL_Application_Context_T codec_{};
    std::array<uint8_t, 512>  response_buffer_{};
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

TEST_F( ApplicationHarnessTest, AcceptsRepresentativeConfigurationWithExactSizeAndDigest )
{
    const HIL_Application_Message_T config  = RepresentativeConfiguration();
    const std::vector<uint8_t>      encoded = EncodeMessage( config );
    ASSERT_EQ( encoded.size(), 242U );

    std::vector<uint8_t> response;
    EXPECT_EQ( Handle( encoded, &response ), HIL_APPLICATION_STATUS_OK );
    EXPECT_TRUE( response.empty() );

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
    EXPECT_EQ( body.i2c[1].own_address_7bit, 0x42U );
    EXPECT_EQ( body.extension_data.size, kRepresentativeExtension.size() );
}

TEST_F( ApplicationHarnessTest, AcceptsCanonicalAllDisabledConfiguration )
{
    const std::vector<uint8_t> encoded = EncodeMessage( DisabledConfiguration() );
    ASSERT_EQ( encoded.size(), 226U );
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
    ASSERT_EQ( encoded.size(), 481U );
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
    ASSERT_EQ( encoded_config.size(), 481U );
    std::vector<uint8_t> response;
    ASSERT_EQ( Handle( encoded_config, &response ), HIL_APPLICATION_STATUS_OK );
    EXPECT_TRUE( response.empty() );

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
    EXPECT_EQ( diagnostics->instruction_digest, kInstructionDigests[0] );
    ASSERT_EQ( response.size(), 62U );

    const auto decoded = DecodeMessage( response );
    ASSERT_EQ( decoded.type, HIL_APPLICATION_MESSAGE_TYPE_TEST_RESULT );
    EXPECT_EQ(
        std::memcmp( decoded.test_id.bytes, source.test_id.bytes, HIL_APPLICATION_TEST_ID_SIZE ),
        0 );
    const auto& result = decoded.body.test_result;
    EXPECT_EQ( result.tick_number, 0U );
    EXPECT_EQ( result.condition, HIL_APPLICATION_RESULT_CONDITION_OK );
    EXPECT_EQ( result.problem_detail, 0U );
    EXPECT_EQ( result.analog_inputs[0].microvolts, 17374899U );
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

    EXPECT_EQ( diagnostics->application_messages_received, 2U );
    EXPECT_EQ( diagnostics->configurations_accepted, 1U );
    EXPECT_EQ( diagnostics->instructions_accepted, 1U );
    EXPECT_EQ( diagnostics->results_encoded, 1U );
    EXPECT_EQ( diagnostics->next_expected_tick, 1U );
    EXPECT_EQ( diagnostics->state, APPLICATION_TEST_HARNESS_STATE_COMPLETE );
    EXPECT_EQ( diagnostics->semantic_rejections, 0U );
    EXPECT_EQ( diagnostics->decode_failures, 0U );
    EXPECT_EQ( diagnostics->encode_failures, 0U );

    EXPECT_EQ( Handle( instruction, &response ), HIL_APPLICATION_STATUS_VALIDATION_FAILED );
    EXPECT_TRUE( response.empty() );
    EXPECT_EQ( diagnostics->application_messages_received, 3U );
    EXPECT_EQ( diagnostics->semantic_rejections, 1U );
    EXPECT_EQ( diagnostics->configurations_accepted, 1U );
    EXPECT_EQ( diagnostics->instructions_accepted, 1U );
    EXPECT_EQ( diagnostics->results_encoded, 1U );
    EXPECT_EQ( diagnostics->next_expected_tick, 1U );
    EXPECT_EQ( diagnostics->state, APPLICATION_TEST_HARNESS_STATE_COMPLETE );
    EXPECT_EQ( diagnostics->configuration_digest, kMaximumExtensionDigest );
    EXPECT_EQ( diagnostics->instruction_digest, kInstructionDigests[0] );
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
        ASSERT_EQ( response.size(), 62U );

        const HIL_Application_Message_T decoded = DecodeMessage( response );
        ASSERT_EQ( decoded.type, HIL_APPLICATION_MESSAGE_TYPE_TEST_RESULT );
        EXPECT_EQ(
            std::memcmp( decoded.test_id.bytes, MakeTestId().bytes, HIL_APPLICATION_TEST_ID_SIZE ),
            0 );
        const auto& result = decoded.body.test_result;
        EXPECT_EQ( result.tick_number, tick );
        EXPECT_EQ( result.condition, HIL_APPLICATION_RESULT_CONDITION_OK );
        EXPECT_EQ( result.problem_detail, 0U );
        EXPECT_EQ( result.analog_inputs[0].microvolts, kExpectedAnalogInput0 );
        EXPECT_EQ( result.analog_inputs[1].microvolts, kExpectedAnalogInput1[tick] );

        const HIL_Application_Message_T source = Instruction( tick );
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

        const auto* diagnostics = APPLICATION_TEST_HARNESS_Get_Diagnostics();
        EXPECT_EQ( diagnostics->instruction_digest, kInstructionDigests[tick] );
        EXPECT_EQ( diagnostics->next_expected_tick, tick + 1U );
    }

    const auto* diagnostics = APPLICATION_TEST_HARNESS_Get_Diagnostics();
    EXPECT_EQ( diagnostics->instructions_accepted, 3U );
    EXPECT_EQ( diagnostics->results_encoded, 3U );
    EXPECT_EQ( diagnostics->state, APPLICATION_TEST_HARNESS_STATE_COMPLETE );
}

TEST_F( ApplicationHarnessTest, DisabledInputsProduceZeroOracleValues )
{
    ASSERT_EQ( Handle( EncodeMessage( DisabledConfiguration() ) ), HIL_APPLICATION_STATUS_OK );
    HIL_Application_Message_T instruction = Instruction( 0U );
    instruction.test_id                   = MakeTestId( 0x30U );
    std::vector<uint8_t> response;
    ASSERT_EQ( Handle( EncodeMessage( instruction ), &response ), HIL_APPLICATION_STATUS_OK );
    const auto decoded = DecodeMessage( response );
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
    EXPECT_EQ( Handle( EncodeMessage( Instruction( 0U ) ) ),
               HIL_APPLICATION_STATUS_VALIDATION_FAILED );
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
    EXPECT_EQ( Handle( EncodeMessage( instruction ) ),
               HIL_APPLICATION_STATUS_INCONSISTENT_TEST_ID );
    const auto* diagnostics = APPLICATION_TEST_HARNESS_Get_Diagnostics();
    EXPECT_EQ( diagnostics->next_expected_tick, 0U );
    EXPECT_EQ( diagnostics->state, APPLICATION_TEST_HARNESS_STATE_ACCEPTING_INSTRUCTIONS );
    EXPECT_EQ( diagnostics->instructions_accepted, 0U );
}

TEST_F( ApplicationHarnessTest, RejectsWrongDuplicateAndSkippedTicks )
{
    ASSERT_EQ( Handle( EncodeMessage( RepresentativeConfiguration() ) ),
               HIL_APPLICATION_STATUS_OK );
    EXPECT_EQ( Handle( EncodeMessage( Instruction( 1U ) ) ),
               HIL_APPLICATION_STATUS_INCONSISTENT_TICK );
    EXPECT_EQ( APPLICATION_TEST_HARNESS_Get_Diagnostics()->next_expected_tick, 0U );

    ASSERT_EQ( Handle( EncodeMessage( Instruction( 0U ) ) ), HIL_APPLICATION_STATUS_OK );
    EXPECT_EQ( Handle( EncodeMessage( Instruction( 0U ) ) ),
               HIL_APPLICATION_STATUS_INCONSISTENT_TICK );
    EXPECT_EQ( Handle( EncodeMessage( Instruction( 2U ) ) ),
               HIL_APPLICATION_STATUS_INCONSISTENT_TICK );
    EXPECT_EQ( APPLICATION_TEST_HARNESS_Get_Diagnostics()->next_expected_tick, 1U );
}

TEST_F( ApplicationHarnessTest, RejectsConfigurationWhileActiveAndAcceptsNewOneAfterCompletion )
{
    ASSERT_EQ( Handle( EncodeMessage( RepresentativeConfiguration() ) ),
               HIL_APPLICATION_STATUS_OK );
    EXPECT_EQ( Handle( EncodeMessage( DisabledConfiguration() ) ),
               HIL_APPLICATION_STATUS_VALIDATION_FAILED );
    EXPECT_EQ( APPLICATION_TEST_HARNESS_Get_Diagnostics()->active_expected_tick_count, 3U );

    ASSERT_EQ( Handle( EncodeMessage( Instruction( 0U ) ) ), HIL_APPLICATION_STATUS_OK );
    ASSERT_EQ( Handle( EncodeMessage( Instruction( 1U ) ) ), HIL_APPLICATION_STATUS_OK );
    ASSERT_EQ( Handle( EncodeMessage( Instruction( 2U ) ) ), HIL_APPLICATION_STATUS_OK );
    ASSERT_EQ( APPLICATION_TEST_HARNESS_Get_Diagnostics()->state,
               APPLICATION_TEST_HARNESS_STATE_COMPLETE );

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
    const std::vector<uint8_t> instruction = EncodeMessage( Instruction( 0U ) );

    std::vector<uint8_t> response;
    EXPECT_EQ( Handle( instruction, &response, 61U ), HIL_APPLICATION_STATUS_BUFFER_TOO_SMALL );
    EXPECT_TRUE( response.empty() );
    const auto* diagnostics = APPLICATION_TEST_HARNESS_Get_Diagnostics();
    EXPECT_EQ( diagnostics->next_expected_tick, 0U );
    EXPECT_EQ( diagnostics->instructions_accepted, 0U );
    EXPECT_EQ( diagnostics->results_encoded, 0U );
    EXPECT_EQ( diagnostics->instruction_digest, 0U );
    EXPECT_EQ( diagnostics->encode_failures, 1U );

    ASSERT_EQ( Handle( instruction, &response ), HIL_APPLICATION_STATUS_OK );
    EXPECT_EQ( response.size(), 62U );
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
}

}  // namespace
