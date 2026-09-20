/******************************************************************************
 *  File:       config_message_handler.c
 *  Author:     Tim Vogelsang
 *  Created:    19-Sep-2026
 *
 *  Description:
 *
 *  Notes:
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include "host_process_message.h"
#include "instruction_message_handler.h"
#include "exec_analogue_output.h"
#include "exec_analogue_input.h"
#include "exec_digital_output.h"
#include "hw_pwm_gen.h"
#include "test_configuration.h"

#include <stdbool.h>
#include <stdint.h>

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */


/**-----------------------------------------------------------------------------
 *  Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Public (global) and Extern Variables
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Private (static) Variables
 *------------------------------------------------------------------------------
 */


/**-----------------------------------------------------------------------------
 *  Private (static) Function Prototypes
 *------------------------------------------------------------------------------
 */

HOST_Interface_Status_T
HOST_INTERFACE_Analog_Input_Parser( const HIL_Application_Message_T* config_message,
                                    DutDriverConfiguration_T*        driver_config )
{
    driver_config->analogue_input.ch_0_is_enabled = config_message->body.test_configuration.analog_in[0].enabled;
    driver_config->analogue_input.ch_1_is_enabled = config_message->body.test_configuration.analog_in[0].enabled;
    driver_config->analogue_input.is_enabled = true;
    driver_config->analogue_input.sample_rate = EXEC_ANALOGUE_INPUT_SAMPLE_RATE_1K_HZ;
    return HOST_INTERFACE_STATUS_OK;
}

HOST_Interface_Status_T
HOST_INTERFACE_Analog_Output_Parser( const HIL_Application_Message_T* config_message,
                                    DutDriverConfiguration_T*        driver_config )
{
    driver_config->analogue_output.is_enabled = false;     
    driver_config->analogue_output.use_external_vref = false;
    for ( uint8_t i = 0; i < HIL_APPLICATION_ANALOG_OUTPUT_CHANNEL_COUNT; i++ )
    {
        if ( config_message->body.test_configuration.analog_out[i].enabled )
        {
            driver_config->analogue_output.is_enabled = true;
            // TODO update later based off execution mid level drivers
            driver_config->analogue_output.use_external_vref = true;
        }
    }
    return HOST_INTERFACE_STATUS_OK;
}

HOST_Interface_Status_T
HOST_INTERFACE_Can_Parser( const HIL_Application_Message_T* config_message,
                                    DutDriverConfiguration_T*        driver_config )
{
    for ( uint8_t i = 0; i < EXEC_CAN_CHANNEL_COUNT; i++ )
    {
        // TODO implement capture_limit_bytes
        driver_config->can_channels[i].bitrate =
            config_message->body.test_configuration.can[i].bit_rate;
        // TODO update to transmit filter bank
        driver_config->can_channels[i].filter_bank = 0U;
        if ( i == 0 )
        {
            driver_config->can_channels[i].filter_bank = 0U;
        }
        else if (i == 1)
        {
            driver_config->can_channels[i].filter_bank = 14U;
        }
        driver_config->can_channels[i].filter_id =
            config_message->body.test_configuration.can[i].filter_id;
        driver_config->can_channels[i].filter_mask =
            config_message->body.test_configuration.can[i].filter_mask;
        driver_config->can_channels[i].is_enabled =
            config_message->body.test_configuration.can[i].enabled;
    }
    return HOST_INTERFACE_STATUS_OK;
}

HOST_Interface_Status_T
HOST_INTERFACE_Digital_Output_Parser( const HIL_Application_Message_T* config_message,
                                    DutDriverConfiguration_T*        driver_config )
{
    for ( uint8_t i = 0; i < EXEC_DIGITAL_OUTPUT_CHANNEL_COUNT; i++ )
    {
        driver_config->digital_outputs.channels[i].is_enabled =
            config_message->body.test_configuration.digital_out[i].enabled;
        driver_config->digital_outputs.channels[i].initial_high =
            config_message->body.test_configuration.digital_out[i].initial_high;
        switch ( config_message->body.test_configuration.digital_out[i].voltage_level )
        {
            case HIL_APPLICATION_PERIPHERAL_CONFIG_VOLTAGE_INVALID:
                driver_config->digital_outputs.channels[i].mode = EXEC_DIGITAL_OUTPUT_MODE_COUNT;
                driver_config->digital_outputs.channels[i].is_enabled = false;
                break;
            case HIL_APPLICATION_PERIPHERAL_CONFIG_3V3:
                driver_config->digital_outputs.channels[i].mode = EXEC_DIGITAL_OUTPUT_MODE_3V3;
                break;
            case HIL_APPLICATION_PERIPHERAL_CONFIG_5V:
                driver_config->digital_outputs.channels[i].mode = EXEC_DIGITAL_OUTPUT_MODE_5V;
                break;
            case HIL_APPLICATION_PERIPHERAL_CONFIG_12V:
                driver_config->digital_outputs.channels[i].mode = EXEC_DIGITAL_OUTPUT_MODE_12V;
                break;
            case HIL_APPLICATION_PERIPHERAL_CONFIG_24V:
                driver_config->digital_outputs.channels[i].mode = EXEC_DIGITAL_OUTPUT_MODE_24V;
                break;
            case HIL_APPLICATION_PERIPHERAL_CONFIG_VOLTAGE_RESERVED:
                driver_config->digital_outputs.channels[i].mode = EXEC_DIGITAL_OUTPUT_MODE_COUNT;
                driver_config->digital_outputs.channels[i].is_enabled = false;
                break;
        }
    }
    return HOST_INTERFACE_STATUS_OK;
}

HOST_Interface_Status_T
HOST_INTERFACE_Digital_Input_Parser( const HIL_Application_Message_T* config_message,
                                    DutDriverConfiguration_T*        driver_config )
{
    for ( uint8_t i = 0; i < EXEC_DIGITAL_INPUT_CHANNEL_COUNT; i++ )
    {
        switch ( config_message->body.test_configuration.digital_in[i].voltage_level )
        {
            case HIL_APPLICATION_PERIPHERAL_CONFIG_VOLTAGE_INVALID:
                driver_config->digital_inputs.channels[i] = EXEC_DIGITAL_INPUT_MODE_DISABLED;
                break;
            case HIL_APPLICATION_PERIPHERAL_CONFIG_3V3:
                driver_config->digital_inputs.channels[i] = EXEC_DIGITAL_INPUT_MODE_3V3;
                break;
            case HIL_APPLICATION_PERIPHERAL_CONFIG_5V:
                driver_config->digital_inputs.channels[i] = EXEC_DIGITAL_INPUT_MODE_5V;
                break;
            case HIL_APPLICATION_PERIPHERAL_CONFIG_12V:
                driver_config->digital_inputs.channels[i] = EXEC_DIGITAL_INPUT_MODE_12V;
                break;
            case HIL_APPLICATION_PERIPHERAL_CONFIG_24V:
                driver_config->digital_inputs.channels[i] = EXEC_DIGITAL_INPUT_MODE_24V;
                break;
            case HIL_APPLICATION_PERIPHERAL_CONFIG_VOLTAGE_RESERVED:
                driver_config->digital_inputs.channels[i] = EXEC_DIGITAL_INPUT_MODE_DISABLED;
                break;
        }
        // a bit of redundancy in the application message
        // currently the .enabled takes priority 
        if ( config_message->body.test_configuration.digital_in[i].enabled == false )
        {
            driver_config->digital_inputs.channels[i] = EXEC_DIGITAL_INPUT_MODE_DISABLED;
        }
    }
    return HOST_INTERFACE_STATUS_OK;
}

HOST_Interface_Status_T
HOST_INTERFACE_I2c_Parser( const HIL_Application_Message_T* config_message,
                                    DutDriverConfiguration_T*        driver_config )
{
    for ( uint8_t i = 0; i < EXEC_I2C_CHANNEL_COUNT; i++ )
    {
        driver_config->i2c_channels[i].is_enabled = config_message->body.test_configuration.i2c[i].enabled;
        driver_config->i2c_channels[i].own_address_7bit =
            config_message->body.test_configuration.i2c[i].own_address_7bit;
        switch ( config_message->body.test_configuration.i2c[i].pull_up )
        {
            case HIL_APPLICATION_I2C_PULL_UP_INVALID:
                driver_config->i2c_channels[i].pullup = EXEC_I2C_PULLUP_COUNT;
                driver_config->i2c_channels[i].is_enabled = false;
                break;
            case HIL_APPLICATION_I2C_PULL_UP_1K:
                driver_config->i2c_channels[i].pullup = EXEC_I2C_PULLUP_1K;
                break;
            case HIL_APPLICATION_I2C_PULL_UP_2K2:
                driver_config->i2c_channels[i].pullup = EXEC_I2C_PULLUP_2K2;
                break;
            case HIL_APPLICATION_I2C_PULL_UP_4K7:
                driver_config->i2c_channels[i].pullup = EXEC_I2C_PULLUP_4K7;
                break;
            case HIL_APPLICATION_I2C_PULL_UP_10K:
                driver_config->i2c_channels[i].pullup = EXEC_I2C_PULLUP_10K;
                break;
            case HIL_APPLICATION_I2C_PULL_UP_RESERVED:
                driver_config->i2c_channels[i].pullup = EXEC_I2C_PULLUP_COUNT;
                driver_config->i2c_channels[i].is_enabled = false;
                break;
        }
        if (config_message->body.test_configuration.i2c[i].bit_rate >= 400000 )
        {
            driver_config->i2c_channels[i].speed = HW_I2C_SPEED_400KHZ;
        }
        else
        {
            driver_config->i2c_channels[i].speed = HW_I2C_SPEED_100KHZ;
        }
        switch ( config_message->body.test_configuration.i2c[i].voltage_level )
        {
            case HIL_APPLICATION_I2C_VOLTAGE_INVALID:
                driver_config->i2c_channels[i].voltage = EXEC_I2C_VOLTAGE_COUNT;
                driver_config->i2c_channels[i].is_enabled = false;
                break;
            case HIL_APPLICATION_I2C_VOLTAGE_3V3:
                driver_config->i2c_channels[i].voltage = EXEC_I2C_VOLTAGE_3V3;
                break;
            case HIL_APPLICATION_I2C_VOLTAGE_5V:
                driver_config->i2c_channels[i].voltage = EXEC_I2C_VOLTAGE_5V;
                break;
            case HIL_APPLICATION_I2C_VOLTAGE_RESERVED:
                driver_config->i2c_channels[i].voltage = EXEC_I2C_VOLTAGE_COUNT;
                driver_config->i2c_channels[i].is_enabled = false;
                break;
        }
        //TODO add other modes in the exec level drivers
        switch ( config_message->body.test_configuration.i2c[i].role )
        {
            case HIL_APPLICATION_BUS_ROLE_INVALID:
                driver_config->i2c_channels[i].mode = HW_I2C_MODE_SLAVE;
                driver_config->i2c_channels[i].is_enabled = false;
                break;
            case HIL_APPLICATION_BUS_ROLE_MASTER:
                driver_config->i2c_channels[i].mode = HW_I2C_MODE_MASTER;
                break;
            case HIL_APPLICATION_BUS_ROLE_SLAVE:
                driver_config->i2c_channels[i].mode = HW_I2C_MODE_SLAVE;
                break;
            case HIL_APPLICATION_BUS_ROLE_RESERVED:
                driver_config->i2c_channels[i].mode = HW_I2C_MODE_SLAVE;
                driver_config->i2c_channels[i].is_enabled = false;
                break;
        }
    }
    return HOST_INTERFACE_STATUS_OK;
}

HOST_Interface_Status_T
HOST_INTERFACE_Pwm_Output_Parser( const HIL_Application_Message_T* config_message,
                                    DutDriverConfiguration_T*        driver_config )
{
    // TODO promote pwm parameter calculation functions to exec mid level
    for ( uint8_t i = 0; i < EXEC_PWM_GEN_CHANNEL_COUNT; i++ )
    {
        driver_config->pwm_generation_channels[i].is_enabled =
            config_message->body.test_configuration.pwm_out[i].enabled;
        switch ( config_message->body.test_configuration.pwm_out[i].voltage_level )
        {
            case HIL_APPLICATION_PERIPHERAL_CONFIG_VOLTAGE_INVALID:
                driver_config->pwm_generation_channels[i].voltage_level =
                    EXEC_PWM_GEN_VOLTAGE_DISABLED;
                driver_config->pwm_generation_channels[i].is_enabled = false;
                break;
            case HIL_APPLICATION_PERIPHERAL_CONFIG_3V3:
                driver_config->pwm_generation_channels[i].voltage_level = EXEC_PWM_GEN_VOLTAGE_3V3;
                break;
            case HIL_APPLICATION_PERIPHERAL_CONFIG_5V:
                driver_config->pwm_generation_channels[i].voltage_level = EXEC_PWM_GEN_VOLTAGE_5V;
                break;
            case HIL_APPLICATION_PERIPHERAL_CONFIG_12V:
                driver_config->pwm_generation_channels[i].voltage_level = EXEC_PWM_GEN_VOLTAGE_12V;
                break;
            case HIL_APPLICATION_PERIPHERAL_CONFIG_24V:
                driver_config->pwm_generation_channels[i].voltage_level = EXEC_PWM_GEN_VOLTAGE_24V;
                break;
            case HIL_APPLICATION_PERIPHERAL_CONFIG_VOLTAGE_RESERVED:
                driver_config->pwm_generation_channels[i].voltage_level =
                    EXEC_PWM_GEN_VOLTAGE_DISABLED;
                driver_config->pwm_generation_channels[i].is_enabled = false;
                break;
        }

        const uint32_t period_ns      = config_message->body.test_configuration.pwm_out[i].initial_period_nanoseconds;
        const uint16_t duty_permyriad =
            config_message->body.test_configuration.pwm_out[i].initial_duty_cycle_permyriad;
        uint16_t psc = 0;
        uint16_t arr = 0;
        uint16_t ccr = 0;
        // TODO make more rhobust checking method
        const uint32_t timer_clock_hz = ( i == 0 ? HOST_INSTRUCTION_PWM_LV_TIMER_CLOCK_HZ : HOST_INSTRUCTION_PWM_HV_TIMER_CLOCK_HZ);

        const uint32_t frequency_hz  = HOST_INSTRUCTION_NANOSECONDS_PER_SECOND / period_ns;
        const uint16_t duty_permille = ( uint16_t )( duty_permyriad / 10U );

        if ( !HW_PWM_GEN_compute_psc( frequency_hz, timer_clock_hz, &psc )
             || !HW_PWM_GEN_compute_arr( frequency_hz, timer_clock_hz, psc, &arr )
             || !HW_PWM_GEN_compute_ccr( duty_permille, arr, &ccr ) )
        {
            return HOST_INTERFACE_STATUS_VALIDATION_FAILED;
        }
        driver_config->pwm_generation_channels[i].initial_psc = psc;
        driver_config->pwm_generation_channels[i].initial_arr = arr;
        driver_config->pwm_generation_channels[i].initial_ccr = ccr;
    }
    return HOST_INTERFACE_STATUS_OK;
}

HOST_Interface_Status_T
HOST_INTERFACE_Pwm_Input_Parser( const HIL_Application_Message_T* config_message,
                                    DutDriverConfiguration_T*        driver_config )
{
    for ( uint8_t i = 0; i < TEST_CONFIGURATION_PWM_CAPTURE_CHANNEL_COUNT; i++ )
    {
        driver_config->pwm_capture_channels[i].is_enabled = config_message->body.test_configuration.pwm_in[i].enabled;
        // TODO set up invalid/disabled modes in exec PWM input
        switch ( config_message->body.test_configuration.pwm_in[i].voltage_level )
        {
            case HIL_APPLICATION_PERIPHERAL_CONFIG_VOLTAGE_INVALID:
                driver_config->pwm_capture_channels[i].mode = EXEC_PWM_CAPTURE_LV_3V3;
                driver_config->pwm_capture_channels[i].is_enabled = false;
                break;
            case HIL_APPLICATION_PERIPHERAL_CONFIG_3V3:
                driver_config->pwm_capture_channels[i].mode = EXEC_PWM_CAPTURE_LV_3V3;
                break;
            case HIL_APPLICATION_PERIPHERAL_CONFIG_5V:
                driver_config->pwm_capture_channels[i].mode = EXEC_PWM_CAPTURE_LV_5V;
                break;
            case HIL_APPLICATION_PERIPHERAL_CONFIG_12V:
                driver_config->pwm_capture_channels[i].mode = EXEC_PWM_CAPTURE_HV_12V;
                break;
            case HIL_APPLICATION_PERIPHERAL_CONFIG_24V:
                driver_config->pwm_capture_channels[i].mode = EXEC_PWM_CAPTURE_HV_24V;
                break;
            case HIL_APPLICATION_PERIPHERAL_CONFIG_VOLTAGE_RESERVED:
                driver_config->pwm_capture_channels[i].mode = EXEC_PWM_CAPTURE_LV_3V3;
                driver_config->pwm_capture_channels[i].is_enabled = false;
                break;
        }
    }
    return HOST_INTERFACE_STATUS_OK;
}

HOST_Interface_Status_T
HOST_INTERFACE_Spi_Parser( const HIL_Application_Message_T* config_message,
                                    DutDriverConfiguration_T*        driver_config )
{
    for ( uint8_t i = 0; i < TEST_CONFIGURATION_SPI_CHANNEL_COUNT; i++ )
    {
        driver_config->spi_channels[i].is_enabled =
            config_message->body.test_configuration.spi[i].enabled;
        // TODO add additional modes (e.g. invalid)
        switch ( config_message->body.test_configuration.spi[i].role )
        {
            case HIL_APPLICATION_BUS_ROLE_INVALID:
                driver_config->spi_channels[i].spi_mode = EXEC_SPI_SLAVE_MODE;
                driver_config->spi_channels[i].is_enabled = false;
                break;
            case HIL_APPLICATION_BUS_ROLE_MASTER:
                driver_config->spi_channels[i].spi_mode = EXEC_SPI_MASTER_MODE;
                break;
            case HIL_APPLICATION_BUS_ROLE_SLAVE:
                driver_config->spi_channels[i].spi_mode = EXEC_SPI_SLAVE_MODE;
                break;
            case HIL_APPLICATION_BUS_ROLE_RESERVED:
                driver_config->spi_channels[i].spi_mode = EXEC_SPI_SLAVE_MODE;
                driver_config->spi_channels[i].is_enabled = false;
                break;
        }
        // TODO add additional bit ordering (e.g. invalid)
        switch ( config_message->body.test_configuration.spi[i].bit_order )
        {
            case HIL_APPLICATION_SPI_BIT_ORDER_INVALID:
                driver_config->spi_channels[i].first_bit = EXEC_SPI_FIRST_MSB;
                driver_config->spi_channels[i].is_enabled = false;
                break;
            case HIL_APPLICATION_SPI_BIT_ORDER_MSB_FIRST:
                driver_config->spi_channels[i].first_bit = EXEC_SPI_FIRST_MSB;
                break;
            case HIL_APPLICATION_SPI_BIT_ORDER_LSB_FIRST:
                driver_config->spi_channels[i].first_bit = EXEC_SPI_FIRST_LSB;
                break;
            case HIL_APPLICATION_SPI_BIT_ORDER_RESERVED:
                driver_config->spi_channels[i].first_bit = EXEC_SPI_FIRST_MSB;
                driver_config->spi_channels[i].is_enabled = false;
                break;
        }
        if (config_message->body.test_configuration.spi[i].bit_rate >= 45000000)
        {
            driver_config->spi_channels[i].baud_rate = EXEC_SPI_BAUD_45MBIT;
        }
        else if ( config_message->body.test_configuration.spi[i].bit_rate >= 22500000 )
        {
            driver_config->spi_channels[i].baud_rate = EXEC_SPI_BAUD_22M5BIT;
        }
        else if ( config_message->body.test_configuration.spi[i].bit_rate >= 11250000 )
        {
            driver_config->spi_channels[i].baud_rate = EXEC_SPI_BAUD_11M25BIT;
        }
        else if ( config_message->body.test_configuration.spi[i].bit_rate >= 5625000 )
        {
            driver_config->spi_channels[i].baud_rate = EXEC_SPI_BAUD_5M625BIT;
        }
        else if ( config_message->body.test_configuration.spi[i].bit_rate >= 1406000 )
        {
            driver_config->spi_channels[i].baud_rate = EXEC_SPI_BAUD_1M406BIT;
        }
        else if ( config_message->body.test_configuration.spi[i].bit_rate >= 703000 )
        {
            driver_config->spi_channels[i].baud_rate = EXEC_SPI_BAUD_703KBIT;
        }
        else if ( config_message->body.test_configuration.spi[i].bit_rate >= 352000 )
        {
            driver_config->spi_channels[i].baud_rate = EXEC_SPI_BAUD_352KBIT;
        }
        else
        {
            driver_config->spi_channels[i].baud_rate = 0;
            driver_config->spi_channels[i].is_enabled = false;
        }
        // TODO use capture limit
        // TODO add invalid types to exec drivers
        switch ( config_message->body.test_configuration.spi[i].clock_polarity )
        {
            case HIL_APPLICATION_SPI_CLOCK_POLARITY_INVALID:
                driver_config->spi_channels[i].cpol = EXEC_SPI_CPOL_LOW;
                driver_config->spi_channels[i].is_enabled = false;
                break;
            case HIL_APPLICATION_SPI_CLOCK_POLARITY_IDLE_LOW:
                driver_config->spi_channels[i].cpol = EXEC_SPI_CPOL_LOW;
                break;
            case HIL_APPLICATION_SPI_CLOCK_POLARITY_IDLE_HIGH:
                driver_config->spi_channels[i].cpol = EXEC_SPI_CPOL_HIGH;
                break;
            case HIL_APPLICATION_SPI_CLOCK_POLARITY_RESERVED:
                driver_config->spi_channels[i].cpol = EXEC_SPI_CPOL_LOW;
                driver_config->spi_channels[i].is_enabled = false;
                break;
        }
        switch ( config_message->body.test_configuration.spi[i].clock_phase )
        {
            case HIL_APPLICATION_SPI_CLOCK_PHASE_INVALID:
                driver_config->spi_channels[i].cpha = EXEC_SPI_CPHA_1_EDGE;
                driver_config->spi_channels[i].is_enabled = false;
                break;
            case HIL_APPLICATION_SPI_CLOCK_PHASE_FIRST_EDGE:
                driver_config->spi_channels[i].cpha = EXEC_SPI_CPHA_1_EDGE;
                break;
            case HIL_APPLICATION_SPI_CLOCK_PHASE_SECOND_EDGE:
                driver_config->spi_channels[i].cpha = EXEC_SPI_CPHA_2_EDGE;
                break;
            case HIL_APPLICATION_SPI_CLOCK_PHASE_RESERVED:
                driver_config->spi_channels[i].cpha = EXEC_SPI_CPHA_1_EDGE;
                driver_config->spi_channels[i].is_enabled = false;
                break;
        }
        switch ( config_message->body.test_configuration.spi[i].data_width )
        {
            case HIL_APPLICATION_SPI_DATA_WIDTH_INVALID:
                driver_config->spi_channels[i].data_size = EXEC_SPI_SIZE_8_BIT;
                driver_config->spi_channels[i].is_enabled = false;
                break;
            case HIL_APPLICATION_SPI_DATA_WIDTH_8_BITS:
                driver_config->spi_channels[i].data_size = EXEC_SPI_SIZE_8_BIT;
                break;
            case HIL_APPLICATION_SPI_DATA_WIDTH_16_BITS:
                driver_config->spi_channels[i].data_size = EXEC_SPI_SIZE_16_BIT;
                break;
            case HIL_APPLICATION_SPI_DATA_WIDTH_RESERVED:
                driver_config->spi_channels[i].data_size = EXEC_SPI_SIZE_8_BIT;
                driver_config->spi_channels[i].is_enabled = false;
                break;  
        }
    }
    return HOST_INTERFACE_STATUS_OK;
}

HOST_Interface_Status_T
HOST_INTERFACE_Uart_Parser( const HIL_Application_Message_T* config_message,
                                    DutDriverConfiguration_T*        driver_config )
{
    for ( uint8_t i = 0; i < EXEC_UART_CHANNEL_COUNT; i++ )
    {
        // TODO add way to use the captured bytes limit. 
        driver_config->uart_channels[i].is_enabled =
            config_message->body.test_configuration.uart[i].enabled;
        driver_config->uart_channels[i].rx_enabled =
            config_message->body.test_configuration.uart[i].rx_enabled;
        driver_config->uart_channels[i].tx_enabled =
            config_message->body.test_configuration.uart[i].tx_enabled;
        driver_config->uart_channels[i].baud_rate =
            config_message->body.test_configuration.uart[i].baud_rate;
        switch ( config_message->body.test_configuration.uart[i].electrical_mode )
        {
            case HIL_APPLICATION_UART_ELECTRICAL_MODE_INVALID:
                driver_config->uart_channels[i].interface_mode = EXEC_UART_MODE_DISABLED;
                driver_config->uart_channels[i].is_enabled     = false;
                break;
            case HIL_APPLICATION_UART_ELECTRICAL_MODE_TTL_3V3:
                driver_config->uart_channels[i].interface_mode = EXEC_UART_MODE_TTL_3V3;
                break;
            case HIL_APPLICATION_UART_ELECTRICAL_MODE_TTL_5V:
                driver_config->uart_channels[i].interface_mode = EXEC_UART_MODE_TTL_5V0;
                break;
            case HIL_APPLICATION_UART_ELECTRICAL_MODE_RS232:
                driver_config->uart_channels[i].interface_mode = EXEC_UART_MODE_RS232;
                break;
            case HIL_APPLICATION_UART_ELECTRICAL_MODE_RESERVED:
                driver_config->uart_channels[i].interface_mode = EXEC_UART_MODE_DISABLED;
                driver_config->uart_channels[i].is_enabled     = false;
                break;
        }
        // TODO add invalid flag in uart exec config structs
        switch ( config_message->body.test_configuration.uart[i].word_length )
        {
            case HIL_APPLICATION_UART_WORD_LENGTH_INVALID:
                driver_config->uart_channels[i].word_length = HW_UART_WORD_LENGTH_8_BITS;
                driver_config->uart_channels[i].is_enabled  = false;
                break;
            case HIL_APPLICATION_UART_WORD_LENGTH_8_BITS:
                driver_config->uart_channels[i].word_length = HW_UART_WORD_LENGTH_8_BITS;
                break;
            case HIL_APPLICATION_UART_WORD_LENGTH_9_BITS:
                driver_config->uart_channels[i].word_length = HW_UART_WORD_LENGTH_9_BITS;
                break;
            case HIL_APPLICATION_UART_WORD_LENGTH_RESERVED:
                driver_config->uart_channels[i].word_length = HW_UART_WORD_LENGTH_8_BITS;
                driver_config->uart_channels[i].is_enabled  = false;
                break;
        }
        //TODO add extra options to UART exec config structs
        switch ( config_message->body.test_configuration.uart[i].parity )
        {
            case HIL_APPLICATION_UART_PARITY_INVALID:
                driver_config->uart_channels[i].parity = HW_UART_PARITY_NONE;
                driver_config->uart_channels[i].is_enabled = false;
                break;
            case HIL_APPLICATION_UART_PARITY_NONE:
                driver_config->uart_channels[i].parity = HW_UART_PARITY_NONE;
                break;
            case HIL_APPLICATION_UART_PARITY_EVEN:
                driver_config->uart_channels[i].parity = HW_UART_PARITY_EVEN;
                break;
            case HIL_APPLICATION_UART_PARITY_ODD:
                driver_config->uart_channels[i].parity = HW_UART_PARITY_ODD;
                break;
            case HIL_APPLICATION_UART_PARITY_RESERVED:
                driver_config->uart_channels[i].parity = HW_UART_PARITY_NONE;
                driver_config->uart_channels[i].is_enabled = false;
                break;
        }
        // TODO add invalid option to UART exec struct
        switch ( config_message->body.test_configuration.uart[i].stop_bits )
        {
            case HIL_APPLICATION_UART_STOP_BITS_INVALID:
                driver_config->uart_channels[i].stop_bits = HW_UART_STOP_BITS_1;
                driver_config->uart_channels[i].is_enabled = false;
                break;
            case HIL_APPLICATION_UART_STOP_BITS_1:
                driver_config->uart_channels[i].stop_bits = HW_UART_STOP_BITS_1;
                break;
            case HIL_APPLICATION_UART_STOP_BITS_2:
                driver_config->uart_channels[i].stop_bits = HW_UART_STOP_BITS_2;
                break;
            case HIL_APPLICATION_UART_STOP_BITS_RESERVED:
                driver_config->uart_channels[i].stop_bits = HW_UART_STOP_BITS_1;
                driver_config->uart_channels[i].is_enabled = false;
                break;
        }
    }
    return HOST_INTERFACE_STATUS_OK;
}


/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */

HOST_Interface_Status_T
HOST_INTERFACE_Config_Message_To_Driver( const HIL_Application_Message_T* config_message,
                                         DutDriverConfiguration_T*        driver_config )
{
    if ( HOST_INTERFACE_Analog_Input_Parser( config_message, driver_config )
         != HOST_INTERFACE_STATUS_OK )
    {
        return HOST_INTERFACE_STATUS_VALIDATION_FAILED;
    }
    if ( HOST_INTERFACE_Analog_Output_Parser( config_message, driver_config )
         != HOST_INTERFACE_STATUS_OK )
    {
        return HOST_INTERFACE_STATUS_VALIDATION_FAILED;
    }
    if ( HOST_INTERFACE_Digital_Input_Parser( config_message, driver_config )
         != HOST_INTERFACE_STATUS_OK )
    {
        return HOST_INTERFACE_STATUS_VALIDATION_FAILED;
    }
    if ( HOST_INTERFACE_Digital_Output_Parser( config_message, driver_config )
         != HOST_INTERFACE_STATUS_OK )
    {
        return HOST_INTERFACE_STATUS_VALIDATION_FAILED;
    }
    if ( HOST_INTERFACE_Pwm_Input_Parser( config_message, driver_config )
         != HOST_INTERFACE_STATUS_OK )
    {
        return HOST_INTERFACE_STATUS_VALIDATION_FAILED;
    }
    if ( HOST_INTERFACE_Pwm_Output_Parser( config_message, driver_config )
         != HOST_INTERFACE_STATUS_OK )
    {
        return HOST_INTERFACE_STATUS_VALIDATION_FAILED;
    }
    if ( HOST_INTERFACE_Can_Parser( config_message, driver_config )
         != HOST_INTERFACE_STATUS_OK )
    {
        return HOST_INTERFACE_STATUS_VALIDATION_FAILED;
    }
    if ( HOST_INTERFACE_I2c_Parser( config_message, driver_config )
         != HOST_INTERFACE_STATUS_OK )
    {
        return HOST_INTERFACE_STATUS_VALIDATION_FAILED;
    }
    if ( HOST_INTERFACE_Uart_Parser( config_message, driver_config )
         != HOST_INTERFACE_STATUS_OK )
    {
        return HOST_INTERFACE_STATUS_VALIDATION_FAILED;
    }
    if ( HOST_INTERFACE_Spi_Parser( config_message, driver_config )
         != HOST_INTERFACE_STATUS_OK )
    {
        return HOST_INTERFACE_STATUS_VALIDATION_FAILED;
    }
    return HOST_INTERFACE_STATUS_OK;
}

