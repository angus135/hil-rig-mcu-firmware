#ifndef HOST_INTERFACE_TESTS_APPLICATION_TEST_FIXTURES_HPP
#define HOST_INTERFACE_TESTS_APPLICATION_TEST_FIXTURES_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

extern "C"
{
#include "hil_rig_protocol/application/application.h"
}

namespace application_test_fixtures {

constexpr std::array<uint8_t, 16> kRepresentativeExtension = {
    0x00U, 0x01U, 0x7EU, 0x7FU, 0x80U, 0xFEU, 0xFFU, 0x48U,
    0x52U, 0x54U, 0x50U, 0x00U, 0xA5U, 0x5AU, 0xC3U, 0x3CU,
};

inline HIL_Application_Test_Id_T MakeTestId( uint8_t base = 0x10U )
{
    HIL_Application_Test_Id_T id{};
    for ( size_t i = 0U; i < HIL_APPLICATION_TEST_ID_SIZE; ++i )
    {
        id.bytes[i] = static_cast<uint8_t>( base + static_cast<uint8_t>( i ) );
    }
    return id;
}

inline HIL_Application_Config_T CodecConfig()
{
    HIL_Application_Config_T config{};
    config.max_encoded_message_size          = 512U;
    config.max_variable_data_size            = 255U;
    config.max_variable_transfers_per_tick   = 8U;
    config.max_expected_tick_count           = 1000000U;
    return config;
}

inline bool InitCodec( HIL_Application_Context_T& context )
{
    const HIL_Application_Config_T config = CodecConfig();
    return HIL_APPLICATION_Init( &context, &config ) == HIL_APPLICATION_STATUS_OK;
}

inline HIL_Application_Message_T RepresentativeConfiguration(
    const uint8_t* extension = kRepresentativeExtension.data(), uint8_t extension_size = 16U,
    uint32_t expected_ticks = 3U )
{
    HIL_Application_Message_T message{};
    message.type        = HIL_APPLICATION_MESSAGE_TYPE_TEST_CONFIGURATION;
    message.subtype     = HIL_APPLICATION_MESSAGE_SUBTYPE_NONE;
    message.has_test_id = 1U;
    message.test_id     = MakeTestId();

    auto& config = message.body.test_configuration;
    config.tick_duration_us.microseconds = 1000U;
    config.expected_tick_count           = expected_ticks;
    config.flags                         = 0U;

    constexpr std::array<HIL_Application_Peripheral_Config_Voltage_Level_T, 10> voltages = {
        HIL_APPLICATION_PERIPHERAL_CONFIG_3V3,
        HIL_APPLICATION_PERIPHERAL_CONFIG_5V,
        HIL_APPLICATION_PERIPHERAL_CONFIG_12V,
        HIL_APPLICATION_PERIPHERAL_CONFIG_24V,
        HIL_APPLICATION_PERIPHERAL_CONFIG_3V3,
        HIL_APPLICATION_PERIPHERAL_CONFIG_5V,
        HIL_APPLICATION_PERIPHERAL_CONFIG_12V,
        HIL_APPLICATION_PERIPHERAL_CONFIG_24V,
        HIL_APPLICATION_PERIPHERAL_CONFIG_3V3,
        HIL_APPLICATION_PERIPHERAL_CONFIG_24V,
    };

    for ( size_t i = 0U; i < voltages.size(); ++i )
    {
        config.digital_in[i].enabled       = 1U;
        config.digital_in[i].voltage_level = voltages[i];
        config.digital_out[i].enabled       = 1U;
        config.digital_out[i].voltage_level = voltages[i];
        config.digital_out[i].initial_high  = static_cast<uint8_t>( i % 2U );
    }
    for ( auto& input : config.analog_in )
    {
        input.enabled = 1U;
    }
    for ( auto& output : config.analog_out )
    {
        output.enabled = 1U;
    }

    config.pwm_in[0].enabled       = 1U;
    config.pwm_in[0].voltage_level = HIL_APPLICATION_PERIPHERAL_CONFIG_3V3;
    config.pwm_in[1].enabled       = 1U;
    config.pwm_in[1].voltage_level = HIL_APPLICATION_PERIPHERAL_CONFIG_24V;

    config.pwm_out[0].enabled                       = 1U;
    config.pwm_out[0].voltage_level                 = HIL_APPLICATION_PERIPHERAL_CONFIG_3V3;
    config.pwm_out[0].initial_period_nanoseconds    = 1000000U;
    config.pwm_out[0].initial_duty_cycle_permyriad  = 2500U;
    config.pwm_out[1].enabled                       = 1U;
    config.pwm_out[1].voltage_level                 = HIL_APPLICATION_PERIPHERAL_CONFIG_24V;
    config.pwm_out[1].initial_period_nanoseconds    = 2000000U;
    config.pwm_out[1].initial_duty_cycle_permyriad  = 7500U;

    config.can[0].enabled             = 1U;
    config.can[0].bit_rate            = 500000U;
    config.can[0].capture_limit_bytes = 64U;
    config.can[0].filter_id           = 0x123U;
    config.can[0].filter_mask         = 0x7FFU;
    config.can[1].enabled             = 1U;
    config.can[1].bit_rate            = 250000U;
    config.can[1].capture_limit_bytes = 64U;
    config.can[1].filter_id           = 0x400U;
    config.can[1].filter_mask         = 0x700U;

    config.spi[0].enabled             = 1U;
    config.spi[0].bit_rate            = 5625000U;
    config.spi[0].role                = HIL_APPLICATION_BUS_ROLE_MASTER;
    config.spi[0].data_width          = HIL_APPLICATION_SPI_DATA_WIDTH_8_BITS;
    config.spi[0].bit_order           = HIL_APPLICATION_SPI_BIT_ORDER_MSB_FIRST;
    config.spi[0].clock_polarity      = HIL_APPLICATION_SPI_CLOCK_POLARITY_IDLE_LOW;
    config.spi[0].clock_phase         = HIL_APPLICATION_SPI_CLOCK_PHASE_FIRST_EDGE;
    config.spi[0].capture_limit_bytes = 64U;
    config.spi[1].enabled             = 1U;
    config.spi[1].bit_rate            = 703125U;
    config.spi[1].role                = HIL_APPLICATION_BUS_ROLE_SLAVE;
    config.spi[1].data_width          = HIL_APPLICATION_SPI_DATA_WIDTH_16_BITS;
    config.spi[1].bit_order           = HIL_APPLICATION_SPI_BIT_ORDER_LSB_FIRST;
    config.spi[1].clock_polarity      = HIL_APPLICATION_SPI_CLOCK_POLARITY_IDLE_HIGH;
    config.spi[1].clock_phase         = HIL_APPLICATION_SPI_CLOCK_PHASE_SECOND_EDGE;
    config.spi[1].capture_limit_bytes = 64U;

    config.uart[0].enabled             = 1U;
    config.uart[0].baud_rate           = 115200U;
    config.uart[0].electrical_mode     = HIL_APPLICATION_UART_ELECTRICAL_MODE_TTL_3V3;
    config.uart[0].word_length         = HIL_APPLICATION_UART_WORD_LENGTH_8_BITS;
    config.uart[0].parity              = HIL_APPLICATION_UART_PARITY_NONE;
    config.uart[0].stop_bits           = HIL_APPLICATION_UART_STOP_BITS_1;
    config.uart[0].rx_enabled          = 1U;
    config.uart[0].tx_enabled          = 1U;
    config.uart[0].capture_limit_bytes = 64U;
    config.uart[1].enabled             = 1U;
    config.uart[1].baud_rate           = 57600U;
    config.uart[1].electrical_mode     = HIL_APPLICATION_UART_ELECTRICAL_MODE_RS232;
    config.uart[1].word_length         = HIL_APPLICATION_UART_WORD_LENGTH_9_BITS;
    config.uart[1].parity              = HIL_APPLICATION_UART_PARITY_EVEN;
    config.uart[1].stop_bits           = HIL_APPLICATION_UART_STOP_BITS_2;
    config.uart[1].rx_enabled          = 1U;
    config.uart[1].tx_enabled          = 1U;
    config.uart[1].capture_limit_bytes = 64U;

    config.i2c[0].enabled             = 1U;
    config.i2c[0].bit_rate            = 100000U;
    config.i2c[0].role                = HIL_APPLICATION_BUS_ROLE_MASTER;
    config.i2c[0].own_address_7bit    = 0U;
    config.i2c[0].voltage_level       = HIL_APPLICATION_I2C_VOLTAGE_3V3;
    config.i2c[0].pull_up             = HIL_APPLICATION_I2C_PULL_UP_4K7;
    config.i2c[0].capture_limit_bytes = 64U;
    config.i2c[1].enabled             = 1U;
    config.i2c[1].bit_rate            = 400000U;
    config.i2c[1].role                = HIL_APPLICATION_BUS_ROLE_SLAVE;
    config.i2c[1].own_address_7bit    = 0x42U;
    config.i2c[1].voltage_level       = HIL_APPLICATION_I2C_VOLTAGE_5V;
    config.i2c[1].pull_up             = HIL_APPLICATION_I2C_PULL_UP_2K2;
    config.i2c[1].capture_limit_bytes = 64U;

    config.extension_data.data = extension_size == 0U ? nullptr : extension;
    config.extension_data.size = extension_size;
    return message;
}

inline HIL_Application_Message_T DisabledConfiguration()
{
    HIL_Application_Message_T message{};
    message.type        = HIL_APPLICATION_MESSAGE_TYPE_TEST_CONFIGURATION;
    message.subtype     = HIL_APPLICATION_MESSAGE_SUBTYPE_NONE;
    message.has_test_id = 1U;
    message.test_id     = MakeTestId( 0x30U );
    message.body.test_configuration.tick_duration_us.microseconds = 10000U;
    message.body.test_configuration.expected_tick_count           = 1U;
    message.body.test_configuration.flags                         = 0U;
    message.body.test_configuration.extension_data.data           = nullptr;
    message.body.test_configuration.extension_data.size           = 0U;
    return message;
}

inline HIL_Application_Message_T Instruction( uint32_t tick )
{
    HIL_Application_Message_T message{};
    message.type        = HIL_APPLICATION_MESSAGE_TYPE_TEST_INSTRUCTION;
    message.subtype     = HIL_APPLICATION_MESSAGE_SUBTYPE_NONE;
    message.has_test_id = 1U;
    message.test_id     = MakeTestId();
    auto& instruction   = message.body.test_instruction;
    instruction.tick_number = tick;

    if ( tick == 0U )
    {
        constexpr std::array<uint8_t, 10> digital = { 0U, 1U, 0U, 1U, 0U, 1U, 0U, 1U, 0U, 1U };
        constexpr std::array<uint32_t, 6> analog = { 0U, 1000U, 5000U, 10000U, 15000U, 20000U };
        for ( size_t i = 0U; i < digital.size(); ++i ) instruction.digital_outputs[i].high = digital[i];
        for ( size_t i = 0U; i < analog.size(); ++i ) instruction.analog_outputs[i].microvolts = analog[i];
        instruction.pwm_outputs[0].period_nanoseconds = 1000000U;
        instruction.pwm_outputs[0].duty_cycle_permyriad = 2500U;
        instruction.pwm_outputs[1].period_nanoseconds = 2000000U;
        instruction.pwm_outputs[1].duty_cycle_permyriad = 7500U;
    }
    else if ( tick == 1U )
    {
        constexpr std::array<uint8_t, 10> digital = { 1U, 0U, 1U, 0U, 1U, 0U, 1U, 0U, 1U, 0U };
        constexpr std::array<uint32_t, 6> analog = { 20000U, 15000U, 10000U, 5000U, 1000U, 0U };
        for ( size_t i = 0U; i < digital.size(); ++i ) instruction.digital_outputs[i].high = digital[i];
        for ( size_t i = 0U; i < analog.size(); ++i ) instruction.analog_outputs[i].microvolts = analog[i];
        instruction.pwm_outputs[0].period_nanoseconds = 500000U;
        instruction.pwm_outputs[0].duty_cycle_permyriad = 5000U;
        instruction.pwm_outputs[1].period_nanoseconds = 4000000U;
        instruction.pwm_outputs[1].duty_cycle_permyriad = 1000U;
    }
    else
    {
        constexpr std::array<uint32_t, 6> analog = { 3300U, 5000U, 12000U, 18000U, 20000U, 0U };
        for ( size_t i = 0U; i < analog.size(); ++i ) instruction.analog_outputs[i].microvolts = analog[i];
        instruction.pwm_outputs[0].period_nanoseconds = 10000000U;
        instruction.pwm_outputs[0].duty_cycle_permyriad = 0U;
        instruction.pwm_outputs[1].period_nanoseconds = 1000000U;
        instruction.pwm_outputs[1].duty_cycle_permyriad = 10000U;
    }
    return message;
}

inline std::vector<uint8_t> Encode( HIL_Application_Context_T& context,
                                    const HIL_Application_Message_T& message )
{
    size_t size = 0U;
    if ( HIL_APPLICATION_Encoded_Size( &context, &message, &size ) != HIL_APPLICATION_STATUS_OK )
    {
        return {};
    }
    std::vector<uint8_t> bytes( size );
    size_t output_size = 0U;
    if ( HIL_APPLICATION_Encode_Message( &context, &message, bytes.data(), bytes.size(),
                                         &output_size ) != HIL_APPLICATION_STATUS_OK
         || output_size != size )
    {
        return {};
    }
    return bytes;
}

}  // namespace application_test_fixtures

#endif
