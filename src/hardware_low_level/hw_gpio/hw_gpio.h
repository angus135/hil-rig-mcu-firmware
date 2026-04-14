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
#include "stm32f4xx_ll_gpio.h"
#include "stm32f446xx.h"
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

typedef enum GPIO_T
{
    GPIO_GREEN_LED_INDICATOR,
    GPIO_BLUE_LED_INDICATOR,
    GPIO_RED_LED_INDICATOR,
    GPIO_TEST_INDICATOR
} GPIO_T;

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

typedef struct GPIO_PORT_PACKET{
    GPIO_TypeDef* gpiox;
    uint32_t pin_mask;
} GPIO_PORT_PACKET;

extern GPIO_TypeDef** GPIO_ports;

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
 * @brief takes a list of GPIO names and splits them into their ports.
 *
 * @param gpio_names   an array of GPIO pin names, all of which are on the same port
 * @param length       the nubmer of GPIO_T in gpio_names
 * @param destination  pointer to space for 8 GPIO_PORT_PACKET packets (to be written to)
 *
 * @return returns the port and a combined pin mask, if fault return {GPIOA, 0xFFFFFFFF}
 * mocked using GoogleMock.
 * This function is designed to split split pins into groups based on their ports
 * because we can write to an entire port at once this increases speed.
 * EXAMPLE: If we want to set DIGITALOUT0, DIGITALOUT1 and DIGITALOUT2, but DIGITALOUT2 uses a
different port,
GPIO_OUTPUT_NAMES* my_arr = [ DIGITALOUT0, DIGITALOUT1, DIGITALOUT2 ]
GPIO_PORT_PACKET destination[8];
split_about_ports(my_arr, 3, destination);
// we dont HAVE to go through all 8 ports (as only 2 are used) but for examples sake we can
for (int i=0; i<8; i++){  
    HW_GPIO_SetToPort(destination[i].gpiox, destination[i].pin_mask)
}
HW_GPIO_SetToPort(p.gpiox, p.pin_mask)
 */
GPIO_PORT_PACKET* split_about_ports( GPIO_OUTPUT_NAMES* gpio_names, uint8_t length, GPIO_PORT_PACKET* destination);

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
struct GPIO_PORT_PACKET p = combine_port_pin_masks(my_arr, 2)
if (p.pin_mask == 0xFFFF0000){
    error
} else {
    HW_GPIO_SetToPort(p.gpiox, p.pin_mask)
}
 * mocked using GoogleMock.
 */
GPIO_PORT_PACKET combine_port_pin_masks( GPIO_OUTPUT_NAMES* gpio_names, uint8_t length );

/**
 * @brief Returns the hardware port and pin associated with the  software pin name passed into it
 *
 * @param gpio_name     The name of the GPIO pin as defined in hw_gpio.h GPIO_OUTPUT_NAMES
 *
 *
 * This function maps the (software) GPIO pin name defined in hw_gpio.h GPIO_OUTPUT_NAMES
 * to the mathcing (hardware) GPIO port and GPIO pin number
 * as defined in f446ze_cubeide_project/Core/Inc/main.h to the
 * mocked using GoogleMock.
 */
GPIO_PORT_PACKET HW_GPIO_port_pin_association( GPIO_OUTPUT_NAMES gpio_name );

/**
 * @brief Sets the state of all digital inputs in a GPIO Port using the underlying GPIO LL library.
 *
 * @param PinMask   Carrys the information about which pins to set
                    If a lower 16 bit is 1 this sets the associated digital output
                    If any bit is 0 this indicates no change
                    0th bit corresponds to digital output 0, 1st bit to output 1 etc
 *
 *
 * This function wraps the LL_GPIO_ResetOutputPin( ... ) function provided by the
 * LL layer. It can be used to set a single output pin or many output pins (on the same port).
 * EXAMPLE: HW_GPIO_SetToPort( GPIOA, LL_GPIO_PIN_5 ) sets LL_GPIO_PIN_5 of port A high
 * EXAMPLE: HW_GPIO_SetToPort( GPIOA, LL_GPIO_PIN_5 | LL_GPIO_PIN_4 ) sets LL_GPIO_PIN_5 and
LL_GPIO_PIN_4 of port A high
 * Setting multiple pins works because LL_GPIO_PIN_5 and LL_GPIO_PIN_4 are just uint32_t
 * in this case likely 0x0000_0020 0x0000_0010, so 0x0000_0030 is written to the BSR register
 * 0x0000_0030 = 0000_0000_0000_0000_0000_0000_0011_0000 setting pins 4 and 5 high
 * mocked using GoogleMock.
 * Note: This implementation assumes all digital outputs are on the same GPIO port.
 * By doing so, we can set all the outputs in a single hardware access.
 */
inline void HW_GPIO_SetToPort( GPIO_TypeDef* gpiox, uint32_t pin_mask );

/**
 * @brief Resets the state of all digital inputs in a GPIO Port using the underlying GPIO LL
library.
 *
 * @param PinMask   Carrys the information about which pins to reset
                    If a lower 16 bit is 1 this resets the associated digital output
                    If any bit is 0 this indicates no change
                    0th bit corresponds to pin 0, 1st bit to pin 1 etc
 *
 *
 * This function wraps the LL_GPIO_ResetOutputPin( ... ) function provided by the
 * LL layer. It can be used to resset a single output pin or many output pins (on the same port).
 * EXAMPLE: HW_GPIO_ResetToPort( GPIOA, LL_GPIO_PIN_5 ) sets LL_GPIO_PIN_5 of port A low
 * EXAMPLE: HW_GPIO_ResetToPort( GPIOA, LL_GPIO_PIN_5 | LL_GPIO_PIN_4 ) sets LL_GPIO_PIN_5 and
LL_GPIO_PIN_4 of port A low
 * Reseting multiple pins works because LL_GPIO_PIN_5 and LL_GPIO_PIN_4 are just uint32_t
 * in this case likely 0x0000_0020 0x0000_0010, so 0x0000_0030 is written to the BSR register
 * 0x0000_0030 = 0000_0000_0000_0000_0000_0000_0011_0000 reseting pins 4 and 5 high
 * mocked using GoogleMock.
 * Note: This implementation assumes all digital outputs are on the same GPIO port.
 * By doing so, we can set all the outputs in a single hardware access.
 */
inline void HW_GPIO_ResetToPort( GPIO_TypeDef* gpiox, uint32_t pin_mask );

#ifdef __cplusplus
}
#endif

#endif /* HW_GPIO_H */
