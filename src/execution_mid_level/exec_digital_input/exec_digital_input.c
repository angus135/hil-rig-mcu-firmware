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
#include "exec_digital_input.h"
// #include "../../hardware_low_level/hw_gpio/hw_gpio.h"
#include "hw_gpio.h"
#include <stdint.h>
#include <stdbool.h>

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

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */

void EXEC_DigitalInput_Configure( const DIGITAL_INPUT_MODE_T* modes, uint8_t num_channels )
{
    // TODO: Implement configuration via multiplexer/output expander/I2C for each channel
    // 'modes' is an array of DIGITAL_INPUT_MODE_T, one per channel
    // 'num_channels' is the number of digital input channels
    ( void )modes;
    ( void )num_channels;
    // Example:
    // for (uint8_t i = 0; i < num_channels; ++i) {
    //     OutputExpander_SetMuxModeForChannel(i, modes[i]);
    // }
}

void EXEC_DigitalInput_SampleAll( bool* dest_buffer )
{
    // Call the low-level function to read all digital inputs
    HW_GPIO_ReadAllDigitalInputsSinglePort( dest_buffer );
}

bool EXEC_DigitalInput_Sample( DIGITAL_INPUT_T input )
{
    // Call the low-level function to read the specified digital input
    return HW_GPIO_ReadDigitalInput( input );
}
