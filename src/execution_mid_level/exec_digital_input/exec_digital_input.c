/******************************************************************************
 *  File:       exec_digital_input.c
 *  Author:     Coen Pasitchnyj
 *  Created:    6-April-2026
 *
 *  Description:
 *      Execution-layer digital input handling for the HIL-RIG. This module
 *      configures execution-time digital input sampling and allows higher level
 *      modules to read digital input states.
 *
 *  Notes:
 *      The functions in this module simply pass through the results from the
 *      low-level GPIO module to the caller.
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */
#ifdef TEST_BUILD
#include "tests/exec_digital_input_mocks.h"
#else
#include "main.h"
#endif

#include "exec_digital_input.h"
#include "hw_gpio.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */
#define NUM_DIGITAL_INPUTS 10

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

static const uint8_t DIGITAL_INPUT_PIN_POSITIONS[NUM_DIGITAL_INPUTS] = {
    __builtin_ctz( Digital_Input_0_Pin ), __builtin_ctz( Digital_Input_1_Pin ),
    __builtin_ctz( Digital_Input_2_Pin ), __builtin_ctz( Digital_Input_3_Pin ),
    __builtin_ctz( Digital_Input_4_Pin ), __builtin_ctz( Digital_Input_5_Pin ),
    __builtin_ctz( Digital_Input_6_Pin ), __builtin_ctz( Digital_Input_7_Pin ),
    __builtin_ctz( Digital_Input_8_Pin ), __builtin_ctz( Digital_Input_9_Pin ) };

static uint32_t EXEC_enabled_inputs_pin_mask =
    Digital_Input_0_Pin | Digital_Input_1_Pin | Digital_Input_2_Pin | Digital_Input_3_Pin
    | Digital_Input_4_Pin | Digital_Input_5_Pin | Digital_Input_6_Pin | Digital_Input_7_Pin
    | Digital_Input_8_Pin | Digital_Input_9_Pin;

/**-----------------------------------------------------------------------------
 *  Private (static) Function Prototypes
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */
/**
 * @brief Configures digital input channel modes for execution-time sampling.
 *
 * @param channel_config  Pointer to per-channel mode configuration.
 *
 * This function builds the enabled-input bitmask used during sampling. Any
 * channel with mode DIGITAL_INPUT_MODE_DISABLED is excluded from the mask.
 */
void EXEC_DigitalInput_Configure( const DigitalInputChannelConfig_T* channel_config )
{
    uint32_t enabled_mask = 0U;

    if ( channel_config == NULL )
    {
        return;
    }

    const DigitalInputMode_T channel_modes[NUM_DIGITAL_INPUTS] = {
        channel_config->channel_0_mode, channel_config->channel_1_mode,
        channel_config->channel_2_mode, channel_config->channel_3_mode,
        channel_config->channel_4_mode, channel_config->channel_5_mode,
        channel_config->channel_6_mode, channel_config->channel_7_mode,
        channel_config->channel_8_mode, channel_config->channel_9_mode };

    for ( uint8_t i = 0U; i < NUM_DIGITAL_INPUTS; ++i )
    {
        if ( channel_modes[i] != DIGITAL_INPUT_MODE_DISABLED )
        {
            enabled_mask |= ( uint32_t )1U << DIGITAL_INPUT_PIN_POSITIONS[i];
        }
    }

    EXEC_enabled_inputs_pin_mask = enabled_mask;
}

/**
 * @brief Samples all configured digital inputs and writes a masked input word.
 *
 * @param dest_addr  Pointer to destination for sampled input bits.
 *
 * This function reads the raw digital input port state and applies the current
 * enabled-input mask before returning the result through dest_addr.
 */
void EXEC_DigitalInput_SampleAll( uint32_t* dest_addr )
{
    if ( dest_addr == NULL )
    {
        return;
    }

    uint32_t port_state = HW_GPIO_Read_All_Digital_Inputs();
    *dest_addr          = port_state & EXEC_enabled_inputs_pin_mask;
}
