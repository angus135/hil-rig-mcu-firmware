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

#ifndef TEST_BUILD
#include "stm32f4xx_ll_gpio.h"
#include "stm32f446xx.h"
#endif
#include <stdint.h>
#include <stdbool.h>

/**-----------------------------------------------------------------------------
 *  Public Defines / Macros
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Public Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

typedef uint32_t DigitalOutputPinmask_T;

typedef enum GPIO_T
{
    GPIO_GREEN_LED_INDICATOR,
    GPIO_BLUE_LED_INDICATOR,
    GPIO_RED_LED_INDICATOR,
    GPIO_TEST_INDICATOR
} GPIO_T;

typedef enum GPIO_OUTPUT_NAMES
{
    DIGITALOUT0,      // Added by Tim for DEV-68
    DIGITALOUT1,      // Added by Tim for DEV-68
    DIGITALOUT2,      // Added by Tim for DEV-68
    DIGITALOUT3,      // Added by Tim for DEV-68
    DIGITALOUT4,      // Added by Tim for DEV-68
    DIGITALOUT5,      // Added by Tim for DEV-68
    DIGITALOUT6,      // Added by Tim for DEV-68
    DIGITALOUT7,      // Added by Tim for DEV-68
    DIGITALOUT8,      // Added by Tim for DEV-68
    DIGITALOUT9,      // Added by Tim for DEV-68
    UART_TTL_3V3_EN,  // Added by Tim as an example, whoever does UART should replace
    UART_TTL_5V_EN,   // Added by Tim as an example, whoever does UART should replace
    LD2,              // Added by Tim for DEV-68
    LD3               // Added by Tim for DEV-68
} GPIO_OUTPUT_NAMES;

typedef struct
{
    const char*       name;
    GPIO_OUTPUT_NAMES value;
} GPIO_Name_Map;

static const GPIO_Name_Map gpio_name_map[] = {
    { "DIGITALOUT0", DIGITALOUT0 },
    { "DIGITALOUT1", DIGITALOUT1 },
    { "DIGITALOUT2", DIGITALOUT2 },
    { "DIGITALOUT3", DIGITALOUT3 },
    { "DIGITALOUT4", DIGITALOUT4 },
    { "DIGITALOUT5", DIGITALOUT5 },
    { "DIGITALOUT6", DIGITALOUT6 },
    { "DIGITALOUT7", DIGITALOUT7 },
    { "DIGITALOUT8", DIGITALOUT8 },
    { "DIGITALOUT9", DIGITALOUT9 },
    { "UART_TTL_3V3_EN", UART_TTL_3V3_EN },
    { "UART_TTL_5V_EN", UART_TTL_5V_EN },
    { "LD2", LD2 },
    { "LD3", LD3 },
};

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

/**
 * @brief Converts a string to a digital pin name
 *
 * @param str   the input string
 * @param out   a space to write the associated pin name enum
 *
 * @return returns true if a match was found and false otherwise
 * This function is designed to split split pins into groups based on their ports
 * because we can write to an entire port at once this increases speed.
 */
bool GPIO_StringToEnum( const char* str, GPIO_OUTPUT_NAMES* out );

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
 * @brief Sets a GPIO pin
 *
 * @param pin The name of the pin we wish to set
 *
 * This function locates the port and pin number (pin_mask) associated with the pin
 * and uses them to set the pin on the STM
 */
void HW_GPIO_set_pin( GPIO_OUTPUT_NAMES pin );

/**
 * @brief Sets many GPIO pins
 *
 * @param pins A list of pin names we wish to set
 * @param length the number of pin names in pins (length of pins)
 *
 * This function locates the ports and pin numbers (pin_masks) associated with the pins
 * and uses them to set the pins on the STM
 */
void HW_GPIO_set_many_pins( GPIO_OUTPUT_NAMES* pins, uint16_t length );

/**
 * @brief Sets a GPIO pin
 *
 * @param pin The name of the pin we wish to set
 *
 * This function locates the port and pin number (pin_mask) associated with the pin
 * and uses them to set the pin on the STM
 */
void HW_GPIO_reset_pin( GPIO_OUTPUT_NAMES pin );

/**
 * @brief Resets many GPIO pins
 *
 * @param pins A list of pin names we wish to reset
 * @param length the number of pin names in pins (length of pins)
 *
 * This function locates the ports and pin numbers (pin_masks) associated with the pins
 * and uses them to reset the pins on the STM
 */
void HW_GPIO_reset_many_pins( GPIO_OUTPUT_NAMES* pins, uint16_t length );

/**
 * @brief combines many GPIO's (on the same port) into one pin mask.
 *
 * @param gpio_names   an array of GPIO pin names, all of which are on the same port
 * @param length       the nubmer of GPIO_OUTPUT_NAMES in gpio_names
 *
 * @return returns the port and a combined pin mask, if fault return {GPIOA, 0xFFFF0000}
 *
 * Combines the pinmasks of the gpio_names so that they can be written to the BSR in one step
 * (instead of individually).
 * EXAMPLE: if we want to set both DIGITALOUT0 and DIGITALOUT1 we could write
GPIO_OUTPUT_NAMES* my_arr = [ DIGITALOUT0, DIGITALOUT1 ]
struct GPIOPortPacket_T p = combine_port_pin_masks(my_arr, 2)
if (p.pin_mask == 0xFFFF0000){
    error
} else {
    HW_GPIO_SetToPort(p.gpiox, p.pin_mask)
}
 * mocked using GoogleMock.
 */
DigitalOutputPinmask_T combine_port_pin_masks( GPIO_OUTPUT_NAMES* gpio_names, uint8_t length );

#ifdef __cplusplus
}
#endif

#endif /* HW_GPIO_H */
