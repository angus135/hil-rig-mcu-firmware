/******************************************************************************
 *  File:       hw_gpio.c
 *  Author:     Coen Pasitchnyj, Tim Vogelsang
 *  Created:    6-April-2026
 *
 *  Description:
 *      <Short description of the module's purpose and responsibilities>
 *
 *  Notes:
 *      <Any design notes, dependencies, or assumptions go here>
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#ifndef TEST_BUILD
#include "gpio.h"
#include "stm32f4xx_ll_gpio.h"
#include "stm32f446xx.h"
#endif

#include "hw_gpio.h"
#include <stdint.h>
#include <stdbool.h>
#include "stddef.h"

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */

#define GPIO_OUTPUT_PORT GPIOA     // Place holder for the actual port

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

// Digital output pins, place holders for real values
static const uint8_t DIGITAL_OUTPUT_PIN_MAP[10] = {1,2,3,4,5,6,7,8,9,10};

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
 * @brief Toggles a digital output using the underlying GPIO HAL.
 *
 * @param gpio   The GPIO to toggle
 *
 * This function wraps the HAL_GPIO_WritePin( ... ) function provided by the
 * HAL layer. It is a convenient seam for unit testing where the HAL call is
 * mocked using GoogleMock.
 */
void HW_GPIO_Toggle( GPIO_T gpio )
{
#ifdef TEST_BUILD
    ( void )gpio;
#else
    switch ( gpio )
    {
        case GPIO_GREEN_LED_INDICATOR:
            LL_GPIO_TogglePin( LD1_GPIO_Port, LD1_Pin );
            break;
        case GPIO_BLUE_LED_INDICATOR:
            LL_GPIO_TogglePin( LD2_GPIO_Port, LD2_Pin );
            break;
        case GPIO_RED_LED_INDICATOR:
            LL_GPIO_TogglePin( LD3_GPIO_Port, LD3_Pin );
            break;
        case GPIO_TEST_INDICATOR:
            LL_GPIO_TogglePin( Test_GPIO_Output_GPIO_Port, Test_GPIO_Output_Pin );
            break;
        default:
            break;
    }
#endif
}


/**
 * @brief Sets the state of all digital inputs in a GPIO Port using the underlying GPIO LL library.
 *
 * @param PinMask   Carrys the information about which pins to set or rest
                    If an upper 16 bit is 1 this resets the associated digital output
                    If a lower 16 bit is 1 this sets the associated digital output
                    If any bit is 0 this indicates no change
                    0th bit corresponds to digital output 0, 1st bit to output 1 etc
                    16th bit corresponds to digital output 0, 17th but to output 1 etc
 *                  
 *
 * This function wraps the LL_GPIO_SetOutputPin( ... ) function provided by the
 * LL layer. It is a convenient seam for unit testing where the LL call is
 * mocked using GoogleMock.
 * Note: This implementation assumes all digital outputs are on the same GPIO port.
 * By doing so, we can set all the outputs in a single hardware access.
 */
void HW_GPIO_WriteToPort( uint32_t PinMask )
{
#ifdef TEST_BUILD
    // For unit testing, do nothing
    return;
#else
    // Digital input pins: PF3, PF4, PF5, PF7, PF10, PF11, PF12, PF13, PF14, PF15
    LL_GPIO_SetOutputPin(GPIO_OUTPUT_PORT, PinMask);
    LL_GPIO_ResetOutputPin(GPIO_OUTPUT_PORT, PinMask);
#endif
}
