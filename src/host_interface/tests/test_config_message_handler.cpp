/******************************************************************************
 *  File:       test_config_message_handler.cpp
 *  Author:     Callum Rafferty
 *  Created:    22-Sep-2026
 *
 *  Description:
 *      Unit tests and white-box tests for config_message_handler using GoogleTest
 *      and GoogleMock.
 *
 *      These tests verify:
 *        - Disabled baseline (inert configuration) translation.
 *        - Analogue Input channel indexing and dynamic enable logic.
 *        - Analogue Output channel and external VREF selection.
 *        - Digital Output voltage mode mapping, initial state, and disabled 3V3 fallback.
 *        - Digital Input voltage mode mapping and disabled fallback.
 *        - PWM Output frequency/duty cycle translation and validation failure.
 *        - PWM Capture voltage mode mapping.
 *        - CAN channel bitrate, filter bank, ID, and mask mapping.
 *        - SPI mode, bit order, clock polarity/phase, and baud rate brackets.
 *        - UART electrical interface, word length, parity, stop bits, and direction flags.
 *        - I2C speed, pull-up, role, and address parsing.
 *        - Overall Config_Message_To_Driver workflow and Commit_Config_Message handling.
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>

extern "C"
{
#include "config_message_handler.h"
#include "exec_analogue_input.h"
#include "exec_analogue_output.h"
#include "exec_can.h"
#include "exec_digital_input.h"
#include "exec_digital_output.h"
#include "exec_i2c.h"
#include "exec_pwm_capture.h"
#include "exec_pwm_gen.h"
#include "exec_spi.h"
#include "exec_uart.h"
#include "hil_rig_protocol/application/application_message.h"
#include "hil_rig_protocol/application/application_test_config.h"
#include "hw_pwm_gen.h"
#include "test_configuration.h"

/* Include .c directly for complete white-box testing of static parser functions */
#include "config_message_handler.c"  // NOLINT
}

using ::testing::_;
using ::testing::DoAll;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SetArgPointee;

/**-----------------------------------------------------------------------------
 *  Test Doubles / Mocks
 *------------------------------------------------------------------------------
 */

class MockConfigHandlerDependencies
{
public:
    virtual ~MockConfigHandlerDependencies() = default;

    /* PWM Generation Low-Level Driver Mocks */
    MOCK_METHOD( bool, HW_PWM_GEN_compute_psc,
                 ( uint32_t frequency_hz, uint32_t timer_clock_hz, uint16_t* psc ) );
    MOCK_METHOD( bool, HW_PWM_GEN_compute_arr,
                 ( uint32_t frequency_hz, uint32_t timer_clock_hz, uint16_t psc, uint16_t* arr ) );
    MOCK_METHOD( bool, HW_PWM_GEN_compute_ccr,
                 ( uint16_t duty_permille, uint16_t arr, uint16_t* ccr ) );

    /* Test Configuration Storage Mock */
    MOCK_METHOD( bool, TEST_CONFIGURATION_Commit, ( const DutDriverConfiguration_T* configuration ) );
};

static MockConfigHandlerDependencies* g_mock_deps = nullptr;

/**-----------------------------------------------------------------------------
 *  Link Seams: Mocked C Function Definitions
 *------------------------------------------------------------------------------
 */

extern "C" bool HW_PWM_GEN_compute_psc( uint32_t frequency_hz, uint32_t timer_clock_hz,
                                        uint16_t* psc )
{
    if ( g_mock_deps != nullptr )
    {
        return g_mock_deps->HW_PWM_GEN_compute_psc( frequency_hz, timer_clock_hz, psc );
    }
    if ( psc != nullptr )
    {
        *psc = 10U;
    }
    return true;
}

extern "C" bool HW_PWM_GEN_compute_arr( uint32_t frequency_hz, uint32_t timer_clock_hz,
                                        uint16_t psc, uint16_t* arr )
{
    if ( g_mock_deps != nullptr )
    {
        return g_mock_deps->HW_PWM_GEN_compute_arr( frequency_hz, timer_clock_hz, psc, arr );
    }
    if ( arr != nullptr )
    {
        *arr = 1000U;
    }
    return true;
}

extern "C" bool HW_PWM_GEN_compute_ccr( uint16_t duty_permille, uint16_t arr, uint16_t* ccr )
{
    if ( g_mock_deps != nullptr )
    {
        return g_mock_deps->HW_PWM_GEN_compute_ccr( duty_permille, arr, ccr );
    }
    if ( ccr != nullptr )
    {
        *ccr = 500U;
    }
    return true;
}

extern "C" bool TEST_CONFIGURATION_Commit( const DutDriverConfiguration_T* configuration )
{
    if ( g_mock_deps != nullptr )
    {
        return g_mock_deps->TEST_CONFIGURATION_Commit( configuration );
    }
    return true;
}

/**-----------------------------------------------------------------------------
 *  Test Fixture
 *------------------------------------------------------------------------------
 */

class ConfigMessageHandlerTest : public ::testing::Test
{
protected:
    HIL_Application_Message_T app_msg{};
    DutDriverConfiguration_T  driver_config{};

    void SetUp() override
    {
        g_mock_deps = new NiceMock<MockConfigHandlerDependencies>();
        std::memset( &app_msg, 0, sizeof( app_msg ) );
        std::memset( &driver_config, 0, sizeof( driver_config ) );
        app_msg.type = HIL_APPLICATION_MESSAGE_TYPE_TEST_CONFIGURATION;
    }

    void TearDown() override
    {
        delete g_mock_deps;
        g_mock_deps = nullptr;
    }
};

/**-----------------------------------------------------------------------------
 *  Tests
 *------------------------------------------------------------------------------
 */

/* Disabled Baseline (Inert Configuration) */
TEST_F( ConfigMessageHandlerTest, InertBaselineTranslatesToAllDisabledWithSafe3V3Defaults )
{
    EXPECT_EQ( HOST_INTERFACE_Config_Message_To_Driver( &app_msg, &driver_config ),
               HOST_INTERFACE_STATUS_OK );

    /* Analogue Input */
    EXPECT_FALSE( driver_config.analogue_input.is_enabled );
    EXPECT_FALSE( driver_config.analogue_input.ch_0_is_enabled );
    EXPECT_FALSE( driver_config.analogue_input.ch_1_is_enabled );

    /* Analogue Output */
    EXPECT_FALSE( driver_config.analogue_output.is_enabled );
    EXPECT_FALSE( driver_config.analogue_output.use_external_vref );

    /* Digital Output */
    for ( uint8_t i = 0; i < EXEC_DIGITAL_OUTPUT_CHANNEL_COUNT; i++ )
    {
        EXPECT_FALSE( driver_config.digital_outputs.channels[i].is_enabled );
        EXPECT_EQ( driver_config.digital_outputs.channels[i].mode, EXEC_DIGITAL_OUTPUT_MODE_3V3 );
        EXPECT_FALSE( driver_config.digital_outputs.channels[i].initial_high );
    }

    /* Digital Input */
    for ( uint8_t i = 0; i < EXEC_DIGITAL_INPUT_CHANNEL_COUNT; i++ )
    {
        EXPECT_EQ( driver_config.digital_inputs.channels[i], EXEC_DIGITAL_INPUT_MODE_DISABLED );
    }

    /* PWM Generation */
    for ( uint8_t i = 0; i < EXEC_PWM_GEN_CHANNEL_COUNT; i++ )
    {
        EXPECT_FALSE( driver_config.pwm_generation_channels[i].is_enabled );
        EXPECT_EQ( driver_config.pwm_generation_channels[i].voltage_level,
                   EXEC_PWM_GEN_VOLTAGE_DISABLED );
        EXPECT_EQ( driver_config.pwm_generation_channels[i].initial_psc, 0U );
        EXPECT_EQ( driver_config.pwm_generation_channels[i].initial_arr, 0U );
        EXPECT_EQ( driver_config.pwm_generation_channels[i].initial_ccr, 0U );
    }

    /* CAN */
    for ( uint8_t i = 0; i < EXEC_CAN_CHANNEL_COUNT; i++ )
    {
        EXPECT_FALSE( driver_config.can_channels[i].is_enabled );
    }

    /* SPI */
    for ( uint8_t i = 0; i < TEST_CONFIGURATION_SPI_CHANNEL_COUNT; i++ )
    {
        EXPECT_FALSE( driver_config.spi_channels[i].is_enabled );
    }

    /* UART */
    for ( uint8_t i = 0; i < EXEC_UART_CHANNEL_COUNT; i++ )
    {
        EXPECT_FALSE( driver_config.uart_channels[i].is_enabled );
        EXPECT_EQ( driver_config.uart_channels[i].interface_mode, EXEC_UART_MODE_DISABLED );
    }

    /* I2C */
    for ( uint8_t i = 0; i < EXEC_I2C_CHANNEL_COUNT; i++ )
    {
        EXPECT_FALSE( driver_config.i2c_channels[i].is_enabled );
    }
}

/* Analogue Input Parser */
TEST_F( ConfigMessageHandlerTest, AnalogInputChannel0Only )
{
    app_msg.body.test_configuration.analog_in[0].enabled = 1U;
    app_msg.body.test_configuration.analog_in[1].enabled = 0U;

    EXPECT_EQ( HOST_INTERFACE_Analog_Input_Parser( &app_msg, &driver_config ),
               HOST_INTERFACE_STATUS_OK );
    EXPECT_TRUE( driver_config.analogue_input.is_enabled );
    EXPECT_TRUE( driver_config.analogue_input.ch_0_is_enabled );
    EXPECT_FALSE( driver_config.analogue_input.ch_1_is_enabled );
    EXPECT_EQ( driver_config.analogue_input.sample_rate, EXEC_ANALOGUE_INPUT_SAMPLE_RATE_1K_HZ );
}

TEST_F( ConfigMessageHandlerTest, AnalogInputChannel1Only )
{
    app_msg.body.test_configuration.analog_in[0].enabled = 0U;
    app_msg.body.test_configuration.analog_in[1].enabled = 1U;

    EXPECT_EQ( HOST_INTERFACE_Analog_Input_Parser( &app_msg, &driver_config ),
               HOST_INTERFACE_STATUS_OK );
    EXPECT_TRUE( driver_config.analogue_input.is_enabled );
    EXPECT_FALSE( driver_config.analogue_input.ch_0_is_enabled );
    EXPECT_TRUE( driver_config.analogue_input.ch_1_is_enabled );
}

TEST_F( ConfigMessageHandlerTest, AnalogInputBothChannels )
{
    app_msg.body.test_configuration.analog_in[0].enabled = 1U;
    app_msg.body.test_configuration.analog_in[1].enabled = 1U;

    EXPECT_EQ( HOST_INTERFACE_Analog_Input_Parser( &app_msg, &driver_config ),
               HOST_INTERFACE_STATUS_OK );
    EXPECT_TRUE( driver_config.analogue_input.is_enabled );
    EXPECT_TRUE( driver_config.analogue_input.ch_0_is_enabled );
    EXPECT_TRUE( driver_config.analogue_input.ch_1_is_enabled );
}

/* Analogue Output Parser */
TEST_F( ConfigMessageHandlerTest, AnalogOutputEnabledSetsExternalVref )
{
    app_msg.body.test_configuration.analog_out[0].enabled = 1U;

    EXPECT_EQ( HOST_INTERFACE_Analog_Output_Parser( &app_msg, &driver_config ),
               HOST_INTERFACE_STATUS_OK );
    EXPECT_TRUE( driver_config.analogue_output.is_enabled );
    EXPECT_TRUE( driver_config.analogue_output.use_external_vref );
}

/* Digital Output Parser */
TEST_F( ConfigMessageHandlerTest, DigitalOutputVoltageModesAndInitialStates )
{
    app_msg.body.test_configuration.digital_out[0].enabled      = 1U;
    app_msg.body.test_configuration.digital_out[0].voltage_level = HIL_APPLICATION_PERIPHERAL_CONFIG_3V3;
    app_msg.body.test_configuration.digital_out[0].initial_high  = 0U;

    app_msg.body.test_configuration.digital_out[1].enabled      = 1U;
    app_msg.body.test_configuration.digital_out[1].voltage_level = HIL_APPLICATION_PERIPHERAL_CONFIG_5V;
    app_msg.body.test_configuration.digital_out[1].initial_high  = 1U;

    app_msg.body.test_configuration.digital_out[2].enabled      = 1U;
    app_msg.body.test_configuration.digital_out[2].voltage_level = HIL_APPLICATION_PERIPHERAL_CONFIG_12V;

    app_msg.body.test_configuration.digital_out[3].enabled      = 1U;
    app_msg.body.test_configuration.digital_out[3].voltage_level = HIL_APPLICATION_PERIPHERAL_CONFIG_24V;

    app_msg.body.test_configuration.digital_out[4].enabled      = 0U;
    app_msg.body.test_configuration.digital_out[4].voltage_level =
        HIL_APPLICATION_PERIPHERAL_CONFIG_VOLTAGE_INVALID;

    EXPECT_EQ( HOST_INTERFACE_Digital_Output_Parser( &app_msg, &driver_config ),
               HOST_INTERFACE_STATUS_OK );

    EXPECT_TRUE( driver_config.digital_outputs.channels[0].is_enabled );
    EXPECT_EQ( driver_config.digital_outputs.channels[0].mode, EXEC_DIGITAL_OUTPUT_MODE_3V3 );
    EXPECT_FALSE( driver_config.digital_outputs.channels[0].initial_high );

    EXPECT_TRUE( driver_config.digital_outputs.channels[1].is_enabled );
    EXPECT_EQ( driver_config.digital_outputs.channels[1].mode, EXEC_DIGITAL_OUTPUT_MODE_5V );
    EXPECT_TRUE( driver_config.digital_outputs.channels[1].initial_high );

    EXPECT_TRUE( driver_config.digital_outputs.channels[2].is_enabled );
    EXPECT_EQ( driver_config.digital_outputs.channels[2].mode, EXEC_DIGITAL_OUTPUT_MODE_12V );

    EXPECT_TRUE( driver_config.digital_outputs.channels[3].is_enabled );
    EXPECT_EQ( driver_config.digital_outputs.channels[3].mode, EXEC_DIGITAL_OUTPUT_MODE_24V );

    EXPECT_FALSE( driver_config.digital_outputs.channels[4].is_enabled );
    EXPECT_EQ( driver_config.digital_outputs.channels[4].mode, EXEC_DIGITAL_OUTPUT_MODE_3V3 );
}

/* Digital Input Parser */
TEST_F( ConfigMessageHandlerTest, DigitalInputVoltageModes )
{
    app_msg.body.test_configuration.digital_in[0].enabled       = 1U;
    app_msg.body.test_configuration.digital_in[0].voltage_level = HIL_APPLICATION_PERIPHERAL_CONFIG_3V3;

    app_msg.body.test_configuration.digital_in[1].enabled       = 1U;
    app_msg.body.test_configuration.digital_in[1].voltage_level = HIL_APPLICATION_PERIPHERAL_CONFIG_5V;

    app_msg.body.test_configuration.digital_in[2].enabled       = 1U;
    app_msg.body.test_configuration.digital_in[2].voltage_level = HIL_APPLICATION_PERIPHERAL_CONFIG_12V;

    app_msg.body.test_configuration.digital_in[3].enabled       = 1U;
    app_msg.body.test_configuration.digital_in[3].voltage_level = HIL_APPLICATION_PERIPHERAL_CONFIG_24V;

    app_msg.body.test_configuration.digital_in[4].enabled       = 0U;
    app_msg.body.test_configuration.digital_in[4].voltage_level =
        HIL_APPLICATION_PERIPHERAL_CONFIG_VOLTAGE_INVALID;

    EXPECT_EQ( HOST_INTERFACE_Digital_Input_Parser( &app_msg, &driver_config ),
               HOST_INTERFACE_STATUS_OK );

    EXPECT_EQ( driver_config.digital_inputs.channels[0], EXEC_DIGITAL_INPUT_MODE_3V3 );
    EXPECT_EQ( driver_config.digital_inputs.channels[1], EXEC_DIGITAL_INPUT_MODE_5V );
    EXPECT_EQ( driver_config.digital_inputs.channels[2], EXEC_DIGITAL_INPUT_MODE_12V );
    EXPECT_EQ( driver_config.digital_inputs.channels[3], EXEC_DIGITAL_INPUT_MODE_24V );
    EXPECT_EQ( driver_config.digital_inputs.channels[4], EXEC_DIGITAL_INPUT_MODE_DISABLED );
}

/* PWM Output Parser */
TEST_F( ConfigMessageHandlerTest, PwmOutputComputeRegistersSuccess )
{
    app_msg.body.test_configuration.pwm_out[0].enabled                     = 1U;
    app_msg.body.test_configuration.pwm_out[0].voltage_level               = HIL_APPLICATION_PERIPHERAL_CONFIG_5V;
    app_msg.body.test_configuration.pwm_out[0].initial_period_nanoseconds  = 1000000U; /* 1 kHz */
    app_msg.body.test_configuration.pwm_out[0].initial_duty_cycle_permyriad = 5000U;    /* 50% */

    EXPECT_CALL( *g_mock_deps, HW_PWM_GEN_compute_psc( 1000U, HOST_INSTRUCTION_PWM_LV_TIMER_CLOCK_HZ, _ ) )
        .WillOnce( DoAll( SetArgPointee<2>( 89U ), Return( true ) ) );
    EXPECT_CALL( *g_mock_deps, HW_PWM_GEN_compute_arr( 1000U, HOST_INSTRUCTION_PWM_LV_TIMER_CLOCK_HZ, 89U, _ ) )
        .WillOnce( DoAll( SetArgPointee<3>( 999U ), Return( true ) ) );
    EXPECT_CALL( *g_mock_deps, HW_PWM_GEN_compute_ccr( 500U, 999U, _ ) )
        .WillOnce( DoAll( SetArgPointee<2>( 500U ), Return( true ) ) );

    EXPECT_EQ( HOST_INTERFACE_Pwm_Output_Parser( &app_msg, &driver_config ),
               HOST_INTERFACE_STATUS_OK );

    EXPECT_TRUE( driver_config.pwm_generation_channels[0].is_enabled );
    EXPECT_EQ( driver_config.pwm_generation_channels[0].voltage_level, EXEC_PWM_GEN_VOLTAGE_5V );
    EXPECT_EQ( driver_config.pwm_generation_channels[0].initial_psc, 89U );
    EXPECT_EQ( driver_config.pwm_generation_channels[0].initial_arr, 999U );
    EXPECT_EQ( driver_config.pwm_generation_channels[0].initial_ccr, 500U );
}

TEST_F( ConfigMessageHandlerTest, PwmOutputComputeFailureRejectsConfiguration )
{
    app_msg.body.test_configuration.pwm_out[0].enabled                     = 1U;
    app_msg.body.test_configuration.pwm_out[0].voltage_level               = HIL_APPLICATION_PERIPHERAL_CONFIG_3V3;
    app_msg.body.test_configuration.pwm_out[0].initial_period_nanoseconds  = 1000U;
    app_msg.body.test_configuration.pwm_out[0].initial_duty_cycle_permyriad = 5000U;

    EXPECT_CALL( *g_mock_deps, HW_PWM_GEN_compute_psc( _, _, _ ) )
        .WillOnce( Return( false ) );

    EXPECT_EQ( HOST_INTERFACE_Pwm_Output_Parser( &app_msg, &driver_config ),
               HOST_INTERFACE_STATUS_VALIDATION_FAILED );
}

/* PWM Capture Parser */
TEST_F( ConfigMessageHandlerTest, PwmCaptureVoltageModes )
{
    app_msg.body.test_configuration.pwm_in[0].enabled       = 1U;
    app_msg.body.test_configuration.pwm_in[0].voltage_level = HIL_APPLICATION_PERIPHERAL_CONFIG_3V3;

    app_msg.body.test_configuration.pwm_in[1].enabled       = 1U;
    app_msg.body.test_configuration.pwm_in[1].voltage_level = HIL_APPLICATION_PERIPHERAL_CONFIG_12V;

    EXPECT_EQ( HOST_INTERFACE_Pwm_Input_Parser( &app_msg, &driver_config ),
               HOST_INTERFACE_STATUS_OK );

    EXPECT_TRUE( driver_config.pwm_capture_channels[0].is_enabled );
    EXPECT_EQ( driver_config.pwm_capture_channels[0].mode, EXEC_PWM_CAPTURE_LV_3V3 );

    EXPECT_TRUE( driver_config.pwm_capture_channels[1].is_enabled );
    EXPECT_EQ( driver_config.pwm_capture_channels[1].mode, EXEC_PWM_CAPTURE_HV_12V );
}

/* CAN Parser */
TEST_F( ConfigMessageHandlerTest, CanChannelsFilterBanksAndBitrates )
{
    app_msg.body.test_configuration.can[0].enabled     = 1U;
    app_msg.body.test_configuration.can[0].bit_rate    = 500000U;
    app_msg.body.test_configuration.can[0].filter_id   = 0x123U;
    app_msg.body.test_configuration.can[0].filter_mask = 0x7FFU;

    app_msg.body.test_configuration.can[1].enabled     = 1U;
    app_msg.body.test_configuration.can[1].bit_rate    = 250000U;
    app_msg.body.test_configuration.can[1].filter_id   = 0x456U;
    app_msg.body.test_configuration.can[1].filter_mask = 0x7FFU;

    EXPECT_EQ( HOST_INTERFACE_Can_Parser( &app_msg, &driver_config ),
               HOST_INTERFACE_STATUS_OK );

    EXPECT_TRUE( driver_config.can_channels[0].is_enabled );
    EXPECT_EQ( driver_config.can_channels[0].bitrate, 500000U );
    EXPECT_EQ( driver_config.can_channels[0].filter_bank, 0U );
    EXPECT_EQ( driver_config.can_channels[0].filter_id, 0x123U );
    EXPECT_EQ( driver_config.can_channels[0].filter_mask, 0x7FFU );

    EXPECT_TRUE( driver_config.can_channels[1].is_enabled );
    EXPECT_EQ( driver_config.can_channels[1].bitrate, 250000U );
    EXPECT_EQ( driver_config.can_channels[1].filter_bank, 14U );
    EXPECT_EQ( driver_config.can_channels[1].filter_id, 0x456U );
    EXPECT_EQ( driver_config.can_channels[1].filter_mask, 0x7FFU );
}

/* SPI Parser */
TEST_F( ConfigMessageHandlerTest, SpiBaudRateBracketsAndModes )
{
    app_msg.body.test_configuration.spi[0].enabled        = 1U;
    app_msg.body.test_configuration.spi[0].role           = HIL_APPLICATION_BUS_ROLE_MASTER;
    app_msg.body.test_configuration.spi[0].data_width     = HIL_APPLICATION_SPI_DATA_WIDTH_16_BITS;
    app_msg.body.test_configuration.spi[0].bit_order      = HIL_APPLICATION_SPI_BIT_ORDER_LSB_FIRST;
    app_msg.body.test_configuration.spi[0].clock_polarity = HIL_APPLICATION_SPI_CLOCK_POLARITY_IDLE_HIGH;
    app_msg.body.test_configuration.spi[0].clock_phase    = HIL_APPLICATION_SPI_CLOCK_PHASE_SECOND_EDGE;
    app_msg.body.test_configuration.spi[0].bit_rate       = 22500000U;

    EXPECT_EQ( HOST_INTERFACE_Spi_Parser( &app_msg, &driver_config ),
               HOST_INTERFACE_STATUS_OK );

    EXPECT_TRUE( driver_config.spi_channels[0].is_enabled );
    EXPECT_EQ( driver_config.spi_channels[0].spi_mode, EXEC_SPI_MASTER_MODE );
    EXPECT_EQ( driver_config.spi_channels[0].data_size, EXEC_SPI_SIZE_16_BIT );
    EXPECT_EQ( driver_config.spi_channels[0].first_bit, EXEC_SPI_FIRST_LSB );
    EXPECT_EQ( driver_config.spi_channels[0].cpol, EXEC_SPI_CPOL_HIGH );
    EXPECT_EQ( driver_config.spi_channels[0].cpha, EXEC_SPI_CPHA_2_EDGE );
    EXPECT_EQ( driver_config.spi_channels[0].baud_rate, EXEC_SPI_BAUD_22M5BIT );
}

/* UART Parser */
TEST_F( ConfigMessageHandlerTest, UartElectricalModesAndOptions )
{
    app_msg.body.test_configuration.uart[0].enabled         = 1U;
    app_msg.body.test_configuration.uart[0].baud_rate       = 115200U;
    app_msg.body.test_configuration.uart[0].electrical_mode = HIL_APPLICATION_UART_ELECTRICAL_MODE_RS232;
    app_msg.body.test_configuration.uart[0].word_length     = HIL_APPLICATION_UART_WORD_LENGTH_9_BITS;
    app_msg.body.test_configuration.uart[0].parity          = HIL_APPLICATION_UART_PARITY_EVEN;
    app_msg.body.test_configuration.uart[0].stop_bits       = HIL_APPLICATION_UART_STOP_BITS_2;
    app_msg.body.test_configuration.uart[0].rx_enabled      = 1U;
    app_msg.body.test_configuration.uart[0].tx_enabled      = 1U;

    EXPECT_EQ( HOST_INTERFACE_Uart_Parser( &app_msg, &driver_config ),
               HOST_INTERFACE_STATUS_OK );

    EXPECT_TRUE( driver_config.uart_channels[0].is_enabled );
    EXPECT_EQ( driver_config.uart_channels[0].baud_rate, 115200U );
    EXPECT_EQ( driver_config.uart_channels[0].interface_mode, EXEC_UART_MODE_RS232 );
    EXPECT_EQ( driver_config.uart_channels[0].word_length, HW_UART_WORD_LENGTH_9_BITS );
    EXPECT_EQ( driver_config.uart_channels[0].parity, HW_UART_PARITY_EVEN );
    EXPECT_EQ( driver_config.uart_channels[0].stop_bits, HW_UART_STOP_BITS_2 );
    EXPECT_TRUE( driver_config.uart_channels[0].rx_enabled );
    EXPECT_TRUE( driver_config.uart_channels[0].tx_enabled );
}

/* I2C Parser */
TEST_F( ConfigMessageHandlerTest, I2cSpeedPullupAndAddress )
{
    app_msg.body.test_configuration.i2c[0].enabled          = 1U;
    app_msg.body.test_configuration.i2c[0].role             = HIL_APPLICATION_BUS_ROLE_MASTER;
    app_msg.body.test_configuration.i2c[0].bit_rate         = 400000U;
    app_msg.body.test_configuration.i2c[0].pull_up          = HIL_APPLICATION_I2C_PULL_UP_4K7;
    app_msg.body.test_configuration.i2c[0].voltage_level    = HIL_APPLICATION_I2C_VOLTAGE_3V3;
    app_msg.body.test_configuration.i2c[0].own_address_7bit = 0x50U;

    EXPECT_EQ( HOST_INTERFACE_I2c_Parser( &app_msg, &driver_config ),
               HOST_INTERFACE_STATUS_OK );

    EXPECT_TRUE( driver_config.i2c_channels[0].is_enabled );
    EXPECT_EQ( driver_config.i2c_channels[0].mode, HW_I2C_MODE_MASTER );
    EXPECT_EQ( driver_config.i2c_channels[0].speed, HW_I2C_SPEED_400KHZ );
    EXPECT_EQ( driver_config.i2c_channels[0].pullup, EXEC_I2C_PULLUP_4K7 );
    EXPECT_EQ( driver_config.i2c_channels[0].voltage, EXEC_I2C_VOLTAGE_3V3 );
    EXPECT_EQ( driver_config.i2c_channels[0].own_address_7bit, 0x50U );
}

/* Commit Config Message */
TEST_F( ConfigMessageHandlerTest, CommitConfigNullPointerReturnsInvalidArgument )
{
    EXPECT_EQ( HOST_INTERFACE_Commit_Config_Message( nullptr ),
               HOST_INTERFACE_STATUS_INVALID_ARGUMENT );
}

TEST_F( ConfigMessageHandlerTest, CommitConfigSuccessPassesThrough )
{
    EXPECT_CALL( *g_mock_deps, TEST_CONFIGURATION_Commit( &driver_config ) )
        .WillOnce( Return( true ) );

    EXPECT_EQ( HOST_INTERFACE_Commit_Config_Message( &driver_config ),
               HOST_INTERFACE_STATUS_OK );
}

TEST_F( ConfigMessageHandlerTest, CommitConfigStorageFailureReturnsInternalError )
{
    EXPECT_CALL( *g_mock_deps, TEST_CONFIGURATION_Commit( &driver_config ) )
        .WillOnce( Return( false ) );

    EXPECT_EQ( HOST_INTERFACE_Commit_Config_Message( &driver_config ),
               HOST_INTERFACE_STATUS_INTERNAL_ERROR );
}
