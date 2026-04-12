/******************************************************************************
 *  File:       hw_gpio.h
 *  Author:     Coen Pasitchnyj, Tim Vogelsang
 *  Created:    6-April-2026
 *
 *  Description:
 *      <Short description of the module, what it exposes, and how it should be used>
 *
 *  Notes:
 *      <Public assumptions, required initialisation order, dependencies, etc.>
 ******************************************************************************/

#ifndef HW_GPIO_H
#define HW_GPIO_H

#ifdef __cplusplus
extern "C"
{
#endif

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include <stdint.h>
#include <stdbool.h>

/**-----------------------------------------------------------------------------
 *  Public Defines / Macros
 *------------------------------------------------------------------------------
 */

#define NUM_GPIO_PORTS 8

/**-----------------------------------------------------------------------------
 *  Public Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

typedef enum GPIO_T
{
    GPIO_GREEN_LED_INDICATOR,
    GPIO_BLUE_LED_INDICATOR,
    GPIO_RED_LED_INDICATOR,
    GPIO_TEST_INDICATOR
} GPIO_T;

typedef enum GPIO_PORT
{
    GPIOA,
    GPIOB,
    GPIOC,
    GPIOD,
    GPIOE,
    GPIOF,
    GPIOG,
    GPIOH
} GPIO_PORT;

typedef struct GPIO_PORT_PACKET{
    GPIO_PORT gpiox;
    uint32_t pin_mask;
} GPIO_PORT_PACKET;

typedef enum GPIO_OUTPUT_NAMES
{
    DIGITALOUT0,    // Added by Tim for DEV-68
    DIGITALOUT1,    // Added by Tim for DEV-68
    DIGITALOUT2,    // Added by Tim for DEV-68
    DIGITALOUT3,    // Added by Tim for DEV-68
    DIGITALOUT4,    // Added by Tim for DEV-68
    DIGITALOUT5,    // Added by Tim for DEV-68
    DIGITALOUT6,    // Added by Tim for DEV-68
    DIGITALOUT7,    // Added by Tim for DEV-68
    DIGITALOUT8,    // Added by Tim for DEV-68
    DIGITALOUT9,    // Added by Tim for DEV-68
    UART_TTL_3V3_EN,    // Added by Tim as an example, whoever does UART should replace
    UART_TTL_5V_EN      // Added by Tim as an example, whoever does UART should replace
} GPIO_OUTPUT_NAMES;

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
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
void HW_GPIO_Toggle( GPIO_T gpio );

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
 * LL layer
 * mocked using GoogleMock.
 * Note: This implementation assumes all digital outputs are on the same GPIO port.
 * By doing so, we can set all the outputs in a single hardware access.
 */
void HW_GPIO_WriteToPort( GPIO_PORT gpiox, uint32_t pin_mask );

/**
 * @brief Sets the state of all digital inputs in a GPIO Port using the underlying
HW_GPIO_WriteToPort function.
 *
 * @param gpio_name     The name of the GPIO pin as defined in
f446ze_cubeide_project/Core/Inc/main.h
 *
 *
 * This function maps the GPIO pin name as defined in f446ze_cubeide_project/Core/Inc/main.h to the
 * the mathcing GPIO port and GPIO pin number
 * mocked using GoogleMock.
 * Note: This implementation assumes all digital outputs are on the same GPIO port.
 * By doing so, we can set all the outputs in a single hardware access.
 */
GPIO_PORT_PACKET HW_GPIO_port_pin_association( GPIO_OUTPUT_NAMES gpio_name );

#ifdef __cplusplus
}
#endif

#endif /* HW_GPIO_H */
