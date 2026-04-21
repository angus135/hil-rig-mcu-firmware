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

typedef enum GPIOOutput_T
{
    DIGITAL_OUT_CH_0,  // Added by Tim for DEV-68
    DIGITAL_OUT_CH_1,  // Added by Tim for DEV-68
    DIGITAL_OUT_CH_2,  // Added by Tim for DEV-68
    DIGITAL_OUT_CH_3,  // Added by Tim for DEV-68
    DIGITAL_OUT_CH_4,  // Added by Tim for DEV-68
    DIGITAL_OUT_CH_5,  // Added by Tim for DEV-68
    DIGITAL_OUT_CH_6,  // Added by Tim for DEV-68
    DIGITAL_OUT_CH_7,  // Added by Tim for DEV-68
    DIGITAL_OUT_CH_8,  // Added by Tim for DEV-68
    DIGITAL_OUT_CH_9,  // Added by Tim for DEV-68
    UART_TTL_3V3_EN,   // Added by Tim as an example, whoever does UART should replace
    UART_TTL_5V_EN,    // Added by Tim as an example, whoever does UART should replace
    LD2,               // Added by Tim for DEV-68
    LD3                // Added by Tim for DEV-68
} GPIOOutput_T;

typedef struct
{
    const char*  name;
    GPIOOutput_T value;
} GPIO_Name_Map;

static const GPIO_Name_Map gpio_name_map[] = {
    { "DIGITAL_OUT_CH_0", DIGITAL_OUT_CH_0 },
    { "DIGITAL_OUT_CH_1", DIGITAL_OUT_CH_1 },
    { "DIGITAL_OUT_CH_2", DIGITAL_OUT_CH_2 },
    { "DIGITAL_OUT_CH_3", DIGITAL_OUT_CH_3 },
    { "DIGITAL_OUT_CH_4", DIGITAL_OUT_CH_4 },
    { "DIGITAL_OUT_CH_5", DIGITAL_OUT_CH_5 },
    { "DIGITAL_OUT_CH_6", DIGITAL_OUT_CH_6 },
    { "DIGITAL_OUT_CH_7", DIGITAL_OUT_CH_7 },
    { "DIGITAL_OUT_CH_8", DIGITAL_OUT_CH_8 },
    { "DIGITAL_OUT_CH_9", DIGITAL_OUT_CH_9 },
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
bool HW_GPIO_StringToEnum( const char* str, GPIOOutput_T* out );

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
void HW_GPIO_Set_Single_Pin( GPIOOutput_T pin );

/**
 * @brief Sets many GPIO pins
 *
 * @param pins A list of pin names we wish to set
 * @param length the number of pin names in pins (length of pins)
 *
 * This function locates the ports and pin numbers (pin_masks) associated with the pins
 * and uses them to set the pins on the STM
 */
void HW_GPIO_Set_Many_Pins( GPIOOutput_T* pins, uint16_t length );

/**
 * @brief Sets a GPIO pin
 *
 * @param pin The name of the pin we wish to set
 *
 * This function locates the port and pin number (pin_mask) associated with the pin
 * and uses them to set the pin on the STM
 */
void HW_GPIO_Reset_Single_Pin( GPIOOutput_T pin );

/**
 * @brief Resets many GPIO pins
 *
 * @param pins A list of pin names we wish to reset
 * @param length the number of pin names in pins (length of pins)
 *
 * This function locates the ports and pin numbers (pin_masks) associated with the pins
 * and uses them to reset the pins on the STM
 */
void HW_GPIO_Reset_Many_Pins( GPIOOutput_T* pins, uint16_t length );

/**
 * @brief combines many GPIO's (on the same port) into one pin mask.
 *
 * @param gpio_names   an array of GPIO pin names, all of which are on the same port
 * @param length       the nubmer of GPIOOutput_T in gpio_names
 *
 * @return returns the port and a combined pin mask, if fault return {GPIOA, 0xFFFF0000}
 *
 * Combines the pinmasks of the gpio_names so that they can be written to the BSR in one step
 * (instead of individually).
 * EXAMPLE: if we want to set both DIGITAL_OUT_CH_0 and DIGITAL_OUT_CH_1 we could write
GPIOOutput_T* my_arr = [ DIGITAL_OUT_CH_0, DIGITAL_OUT_CH_1 ]
struct GPIOPortPacket_T p = HW_GPIO_Combine_Port_Pin_Masks(my_arr, 2)
if (p.pin_mask == 0xFFFF0000){
    error
} else {
    HW_GPIO_Set_To_Port(p.gpiox, p.pin_mask)
}
 * mocked using GoogleMock.
 */
DigitalOutputPinmask_T HW_GPIO_Combine_Port_Pin_Masks( GPIOOutput_T* gpio_names, uint8_t length );

inline void HW_GPIO_Set_Output( uint32_t pin_mask );

inline void HW_GPIO_Reset_Output( uint32_t pin_mask );

#ifdef __cplusplus
}
#endif

#endif /* HW_GPIO_H */
