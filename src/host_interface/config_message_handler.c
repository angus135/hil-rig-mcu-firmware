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

#include "instruction_message_handler.h"
#include "exec_analogue_output.h"
#include "exec_analogue_input.h"
#include "exec_digital_output.h"
#include "hw_pwm_gen.h"
#include "execution_manager/execution_instruction.h"
#include "execution_manager/execution_operation_payloads.h"
#include "flash_manager/flash_manager.h"
#include "hw_pwm_gen.h"
#include "test_configuration.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

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
    driver_config->analogue_input.sample_rate =  EXEC_ANALOGUE_INPUT_SAMPLE_RATE_1K_HZ;
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
            case HIL_APPLICATION_PERIPHERAL_CONFIG_3V3:
                driver_config->digital_outputs.channels[i].mode = EXEC_DIGITAL_OUTPUT_MODE_3V3;
            case HIL_APPLICATION_PERIPHERAL_CONFIG_5V:
                driver_config->digital_outputs.channels[i].mode = EXEC_DIGITAL_OUTPUT_MODE_5V;
            case HIL_APPLICATION_PERIPHERAL_CONFIG_12V:
                driver_config->digital_outputs.channels[i].mode = EXEC_DIGITAL_OUTPUT_MODE_12V;
            case HIL_APPLICATION_PERIPHERAL_CONFIG_24V:
                driver_config->digital_outputs.channels[i].mode = EXEC_DIGITAL_OUTPUT_MODE_24V;
            case HIL_APPLICATION_PERIPHERAL_CONFIG_VOLTAGE_RESERVED:
                driver_config->digital_outputs.channels[i].mode = EXEC_DIGITAL_OUTPUT_MODE_COUNT;
                driver_config->digital_outputs.channels[i].is_enabled = false;
        }
    }
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
            case HIL_APPLICATION_PERIPHERAL_CONFIG_3V3:
                driver_config->digital_inputs.channels[i] = EXEC_DIGITAL_INPUT_MODE_3V3;
            case HIL_APPLICATION_PERIPHERAL_CONFIG_5V:
                driver_config->digital_inputs.channels[i] = EXEC_DIGITAL_INPUT_MODE_5V;
            case HIL_APPLICATION_PERIPHERAL_CONFIG_12V:
                driver_config->digital_inputs.channels[i] = EXEC_DIGITAL_INPUT_MODE_12V;
            case HIL_APPLICATION_PERIPHERAL_CONFIG_24V:
                driver_config->digital_inputs.channels[i] = EXEC_DIGITAL_INPUT_MODE_24V;
            case HIL_APPLICATION_PERIPHERAL_CONFIG_VOLTAGE_RESERVED:
                driver_config->digital_inputs.channels[i] = EXEC_DIGITAL_INPUT_MODE_DISABLED;
        }
        // a bit of redundancy in the application message
        // currently the .enabled takes priority 
        if ( config_message->body.test_configuration.digital_in[i].enabled == false )
        {
            driver_config->digital_inputs.channels[i] = EXEC_DIGITAL_INPUT_MODE_DISABLED;
        }
    }
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
            case HIL_APPLICATION_I2C_PULL_UP_1K:
                driver_config->i2c_channels[i].pullup = EXEC_I2C_PULLUP_1K;
            case HIL_APPLICATION_I2C_PULL_UP_2K2:
                driver_config->i2c_channels[i].pullup = EXEC_I2C_PULLUP_2K2;
            case HIL_APPLICATION_I2C_PULL_UP_4K7:
                driver_config->i2c_channels[i].pullup = EXEC_I2C_PULLUP_4K7;
            case HIL_APPLICATION_I2C_PULL_UP_10K:
                driver_config->i2c_channels[i].pullup = EXEC_I2C_PULLUP_10K;
            case HIL_APPLICATION_I2C_PULL_UP_RESERVED:
                driver_config->i2c_channels[i].pullup = EXEC_I2C_PULLUP_COUNT;
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
                driver_config->i2c_channels[i].voltage =EXEC_I2C_VOLTAGE_COUNT;
            case HIL_APPLICATION_I2C_VOLTAGE_3V3:
                driver_config->i2c_channels[i].voltage =EXEC_I2C_VOLTAGE_3V3;
            case HIL_APPLICATION_I2C_VOLTAGE_5V:
                driver_config->i2c_channels[i].voltage =EXEC_I2C_VOLTAGE_5V;
            case HIL_APPLICATION_I2C_VOLTAGE_RESERVED:
                driver_config->i2c_channels[i].voltage =EXEC_I2C_VOLTAGE_COUNT;
        }
        //TODO add other modes in the exec level drivers
        switch ( config_message->body.test_configuration.i2c[i].role )
        {
            case HIL_APPLICATION_BUS_ROLE_INVALID:
                driver_config->i2c_channels[i].mode = HW_I2C_MODE_SLAVE;
            case HIL_APPLICATION_BUS_ROLE_MASTER:
                driver_config->i2c_channels[i].mode = HW_I2C_MODE_MASTER;
            case HIL_APPLICATION_BUS_ROLE_SLAVE:
                driver_config->i2c_channels[i].mode = HW_I2C_MODE_SLAVE;
            case HIL_APPLICATION_BUS_ROLE_RESERVED:
                driver_config->i2c_channels[i].mode = HW_I2C_MODE_SLAVE;
        }
    }
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
        uint16_t psc = 0;
        config_message->body.test_configuration.pwm_out[i].initial_period_nanoseconds
        uint16_t freq = 
        HW_PWM_GEN_compute_psc()
        HW_PWM_GEN_compute_arr();
        HW_PWM_GEN_compute_ccr();
        driver_config->pwm_generation_channels[i].is_enabled = config_message->body.test_configuration.pwm_out[i].;
    }
}

HOST_Interface_Status_T
HOST_INTERFACE_Pwm_Input_Parser( const HIL_Application_Message_T* config_message,
                                    DutDriverConfiguration_T*        driver_config )
{
}

HOST_Interface_Status_T
HOST_INTERFACE_Spi_Parser( const HIL_Application_Message_T* config_message,
                                    DutDriverConfiguration_T*        driver_config )
{
}

HOST_Interface_Status_T
HOST_INTERFACE_uart_Parser( const HIL_Application_Message_T* config_message,
                                    DutDriverConfiguration_T*        driver_config )
{

}


/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */

HOST_Interface_Status_T
HOST_INTERFACE_Config_Message_To_Driver( const HIL_Application_Message_T* config_message,
                                         DutDriverConfiguration_T*        driver_config )
{
    
}

