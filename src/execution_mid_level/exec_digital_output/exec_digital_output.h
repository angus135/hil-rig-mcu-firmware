/******************************************************************************
 *  File:       exec_digital_output.h
 *  Author:     Angus Corr
 *  Created:    25-Mar-2026
 *
 *  Description:
 *      <Short description of the module, what it exposes, and how it should be used>
 *
 *  Notes:
 *      <Public assumptions, required initialisation order, dependencies, etc.>
 ******************************************************************************/

#ifndef EXEC_DIGITAL_OUTPUT_H
#define EXEC_DIGITAL_OUTPUT_H

#ifdef __cplusplus
extern "C"
{
#endif

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include "hardware_low_level/hw_gpio/hw_gpio.h"
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

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

/**
 * @brief Sets the state of all digital outputs in a GPIO Port using the underlying GPIO HW
functions.
 *
 * @param pin   the name of the pin that will be set
 *
 *
 * This function wraps the HW_GPIO_Set_Single_Pin( ... ) function.
 * It can be used to set a single output pin or many output pins (on the same port).
 * EXAMPLE: GPIO_SetToPort( {gpiox = GPIOA, pin_mask = LL_GPIO_PIN_5} ) sets LL_GPIO_PIN_5 of port A
high
 * EXAMPLE: GPIO_SetToPort( {gpiox = GPIOA, pin_mask = (LL_GPIO_PIN_5 | LL_GPIO_PIN_4) } ) sets
LL_GPIO_PIN_5 and LL_GPIO_PIN_4 of port A high
 * Setting multiple pins works because LL_GPIO_PIN_5 and LL_GPIO_PIN_4 are just uint32_t
 * in this case likely 0x0000_0020 0x0000_0010, so 0x0000_0030 is written to the BSR register
 * 0x0000_0030 = 0000_0000_0000_0000_0000_0000_0011_0000 setting pins 4 and 5 high
 * mocked using GoogleMock.
 * Note: This implementation assumes all digital outputs are on the same GPIO port.
 * By doing so, we can set all the outputs in a single hardware access.
 * functions exist in hardware_low_level/hw_gpio.c to help manage ports
 */
inline void EXEC_DIGITAL_OUTPUT_set_pin( GPIOOutput_T pin );

/**
 * @brief Sets the state of all digital outputs pins given to it, regardless of their port
functions.
 *
 * @param pins   list of pin names
 * @param length   the number of pin names in pins
 *
 * This function wraps the HW_GPIO_Set_Many_Pins( ... ) function.
 * similar examples to EXEC_set_pin()
 * functions exist in hardware_low_level/hw_gpio.c to help manage ports
 */
inline void EXEC_DIGITAL_OUTPUT_set_many_pins( GPIOOutput_T* pins, uint16_t length );

/**
 * @brief Resets the state of all digital outputs in a GPIO Port using the underlying GPIO HW
functions.
 *
 * @param gpio_pack   Carrys the information about which pins to set and which port to set them on
 *
 *
 * This function wraps the HW_GPIO_Set_To_Port( ... ) function.
 * It can be used to set a single output pin or many output pins (on the same port).
 * EXAMPLE: EXEC_reset_pin( {gpiox = GPIOA, pin_mask = LL_GPIO_PIN_5} ) resets LL_GPIO_PIN_5 of port
A high
 * EXAMPLE: EXEC_reset_pin( {gpiox = GPIOA, pin_mask = (LL_GPIO_PIN_5 | LL_GPIO_PIN_4) } ) resets
LL_GPIO_PIN_5 and LL_GPIO_PIN_4 of port A high
 * Setting multiple pins works because LL_GPIO_PIN_5 and LL_GPIO_PIN_4 are just uint32_t
 * in this case likely 0x0000_0020 0x0000_0010, so 0x0000_0030 is written to the BSR register
 * 0x0000_0030 = 0000_0000_0000_0000_0000_0000_0011_0000 setting pins 4 and 5 low
 * mocked using GoogleMock.
 * Note: This implementation assumes all digital outputs are on the same GPIO port.
 * By doing so, we can set all the outputs in a single hardware access.
 * functions exist in hardware_low_level/hw_gpio.c to help manage ports
 */
inline void EXEC_DIGITAL_OUTPUT_reset_pin( GPIOOutput_T pin );

/**
 * @brief Resets the state of all digital outputs pins given to it, regardless of their port
functions.
 *
 * @param pins   list of pin names
 * @param length   the number of pin names in pins
 *
 * This function wraps the HW_GPIO_Reset_Many_Pins( ... ) function.
 * similar examples to EXEC_reset_pin()
 * functions exist in hardware_low_level/hw_gpio.c to help manage ports
 */
inline void EXEC_DIGITAL_OUTPUT_reset_many_pins( GPIOOutput_T* pins, uint16_t length );

inline DigitalOutputPinmask_T DIGITAL_OUTPUT_Combine_Port_Pin_Masks( GPIOOutput_T* gpio_names,
                                                                     uint8_t       length );

inline void                   EXEC_DIGITAL_OUTPUT_Set_Output( uint32_t pin_mask );

inline void EXEC_DIGITAL_OUTPUT_Reset_Output( uint32_t pin_mask );

#ifdef __cplusplus
}
#endif

#endif /* EXEC_DIGITAL_OUTPUT_H */
