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
 * @brief takes a list of GPIO names and splits them into their ports.
 *
 * @param gpio_names   an array of GPIO pin names, all of which are on the same port
 * @param length       the nubmer of GPIO_T in gpio_names
 * @param destination  pointer to space for 8 GPIO_PORT_PACKET packets (to be written to)
 *
 * @return returns the port and a combined pin mask, if fault return {GPIOA, 0xFFFFFFFF}
 * mocked using GoogleMock.
 */
GPIO_PORT_PACKET* split_about_ports( GPIO_OUTPUT_NAMES* gpio_names, uint8_t length, GPIO_PORT_PACKET* destination)
{
    GPIO_PORT_PACKET port_packet;
    for ( int j = 0; j < NUM_GPIO_PORTS; j++ )
    {
        destination[j].gpiox = (GPIO_PORT)j;    // reset ports, destination[0] = GPIOA, destination[1] = GPIOB etc
        destination[j].pin_mask = 0;            // reset pin masks
    }
    for ( int i = 0; i < length; i++ )
    {
        port_packet = HW_GPIO_port_pin_association(gpio_names[i]);
        switch ( port_packet.gpiox )
        {
            case GPIOA: destination[0].pin_mask = destination[0].pin_mask | port_packet.pin_mask;
            case GPIOB: destination[1].pin_mask = destination[1].pin_mask | port_packet.pin_mask;
            case GPIOC: destination[2].pin_mask = destination[2].pin_mask | port_packet.pin_mask;
            case GPIOD: destination[3].pin_mask = destination[3].pin_mask | port_packet.pin_mask;
            case GPIOE: destination[4].pin_mask = destination[4].pin_mask | port_packet.pin_mask;
            case GPIOF: destination[5].pin_mask = destination[5].pin_mask | port_packet.pin_mask;
            case GPIOG: destination[6].pin_mask = destination[6].pin_mask | port_packet.pin_mask;
            case GPIOH: destination[7].pin_mask = destination[7].pin_mask | port_packet.pin_mask;
        }
    }
}

/**
 * @brief combines many GPIO's into one pin mask .
 *
 * @param gpio_names   an array of GPIO pin names, all of which are on the same port 
 * @param length       the nubmer of GPIO_T in gpio_names
 *
 * @return returns the port and a combined pin mask, if fault return {GPIOA, 0xFFFFFFFF}
 * This function wraps the HAL_GPIO_WritePin( ... ) function provided by the
 * HAL layer. It is a convenient seam for unit testing where the HAL call is
 * mocked using GoogleMock.
 */
GPIO_PORT_PACKET combine_port_pin_masks( GPIO_OUTPUT_NAMES* gpio_names, uint8_t length )
{
    GPIO_PORT checker = HW_GPIO_port_pin_association(*gpio_names).gpiox;
    uint32_t  pin_mask = 0;
    GPIO_PORT_PACKET port_packet;
    for ( int i = 0; i < length; i++ )  // iterate through the gpio names and combine pin_masks
    {
        port_packet = HW_GPIO_port_pin_association(gpio_names[i]);
        if ( checker != port_packet.gpiox )
        {
            // Not all of the pins had the same port, so return an error
            GPIO_PORT_PACKET p;
            p.gpiox = GPIOA; 
            p.pin_mask = 4294967295;  // 0xFFFFFFFF
            return p;
        }
        pin_mask = pin_mask | port_packet.pin_mask; // combine pin masks
    }
    return (struct GPIO_PORT_PACKET){checker, pin_mask};
}

/**
 * @brief Returns the port and pin associated with the pin name passed into it
 *
 * @param gpio_name     The name of the GPIO pin as defined in hw_gpio.h GPIO_OUTPUT_NAMES
 *
 *
 * This function maps the GPIO pin name as defined in f446ze_cubeide_project/Core/Inc/main.h to the
 * the mathcing GPIO port and GPIO pin number
 * mocked using GoogleMock.
 */
GPIO_PORT_PACKET HW_GPIO_port_pin_association( GPIO_OUTPUT_NAMES gpio_name )
{
switch ( gpio_name )
{
    // ==== HOW TO ADD DIGITAL OUTPUT PINS ====
    // the following lines (a) and (b) were taken from f446ze_cubeide_project/Core/Inc/main.h
    // they are created automatically by cube IDE from the IOC file and correspond to the actual
    // pin on the board
    // #define Test_GPIO_Output_Pin GPIO_PIN_8      // (a)
    // #define Test_GPIO_Output_GPIO_Port GPIOC     // (b)
    // if we wanted these GPIO output pins (a) (b) (hardware)
    // to be associated to some pin name (software) eg UART_enable
    // then we would have to add the relationship in this function
    // to add this new relationship we add a new case statement below.
    // Here is an example:
    // case DIGITALOUT0:
    //    return (struct GPIO_PORT_PACKET){LD1_GPIO_Port, LD1_Pin};
    // the above example associates the pin name DIGITALOUT0 (software)
    // to the physical pin name LD1_Pin and its port LD1_GPIO_Port
    // the name chosen (DIGITALOUT0)
    // would then be added to the GPIO_OUTPUT_NAMES enum in hw_gpio.h
    case DIGITALOUT0:  // Added by Tim for DEV-68
        return (struct GPIO_PORT_PACKET){LD1_GPIO_Port, LD1_Pin};
    case DIGITALOUT1:   // Added by Tim for DEV-68
        return (struct GPIO_PORT_PACKET){LD1_GPIO_Port, LD1_Pin};
    case DIGITALOUT2:   // Added by Tim for DEV-68
        return (struct GPIO_PORT_PACKET){LD1_GPIO_Port, LD1_Pin};
    case DIGITALOUT3:   // Added by Tim for DEV-68
        return (struct GPIO_PORT_PACKET){LD1_GPIO_Port, LD1_Pin};
    case DIGITALOUT4:   // Added by Tim for DEV-68
        return (struct GPIO_PORT_PACKET){LD1_GPIO_Port, LD1_Pin};
    case DIGITALOUT5:   // Added by Tim for DEV-68
        return (struct GPIO_PORT_PACKET){LD1_GPIO_Port, LD1_Pin};
    case DIGITALOUT6:   // Added by Tim for DEV-68
        return (struct GPIO_PORT_PACKET){LD1_GPIO_Port, LD1_Pin};
    case DIGITALOUT7:   // Added by Tim for DEV-68
        return (struct GPIO_PORT_PACKET){LD1_GPIO_Port, LD1_Pin};
    case DIGITALOUT8:   // Added by Tim for DEV-68
        return (struct GPIO_PORT_PACKET){LD1_GPIO_Port, LD1_Pin};
    case DIGITALOUT9:   // Added by Tim for DEV-68
        return (struct GPIO_PORT_PACKET){LD1_GPIO_Port, LD1_Pin};
    case UART_TTL_3V3_EN:   // Added by Tim as an example, whoever does UART should replace
        return (struct GPIO_PORT_PACKET){LD1_GPIO_Port, LD1_Pin};
    case UART_TTL_5V_EN:    // Added by Tim as an example, whoever does UART should replace
        return (struct GPIO_PORT_PACKET){LD1_GPIO_Port, LD1_Pin};
    default:        // Added by Tim, should be updated to set the warning LED once IOC is decided
        return (struct GPIO_PORT_PACKET){LD1_GPIO_Port, LD1_Pin};;
}
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
 * LL layer
 * mocked using GoogleMock.
 * Note: This implementation assumes all digital outputs are on the same GPIO port.
 * By doing so, we can set all the outputs in a single hardware access.
 */
inline void HW_GPIO_WriteToPort( GPIO_PORT gpiox, uint32_t pin_mask )
{
#ifdef TEST_BUILD
    // For unit testing, do nothing
    ( void )pin_mask;
    ( void )gpiox;
#else
    // Digital input pins: PF3, PF4, PF5, PF7, PF10, PF11, PF12, PF13, PF14, PF15
    LL_GPIO_SetOutputPin(gpiox, pin_mask);
    LL_GPIO_ResetOutputPin(gpiox, pin_mask);
#endif
}
