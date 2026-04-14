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

#include <math.h>
#ifndef TEST_BUILD
#include "gpio.h"
#include "stm32f4xx_ll_gpio.h"
#include "stm32f446xx.h"
#else
#include "tests/hw_gpio_mocks.h"
#endif

#include "hw_gpio.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

GPIO_TypeDef* GPIO_ports[] = {
#ifdef GPIOA
    GPIOA,
#endif
#ifdef GPIOB
    GPIOB,
#endif
#ifdef GPIOC
    GPIOC,
#endif
#ifdef GPIOD
    GPIOD,
#endif
#ifdef GPIOE
    GPIOE,
#endif
#ifdef GPIOF
    GPIOF,
#endif
#ifdef GPIOG
    GPIOG,
#endif
#ifdef GPIOH
    GPIOH
#endif
};

#define NUM_GPIO_PORTS (sizeof(GPIO_ports) / sizeof(GPIO_ports[0]))
#define MAX_NUM_GPIO_PORTS 8



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
 * @brief Converts a string to a digital pin name
 *
 * @param str   the input string
 * @param out   a space to write the associated pin name enum 
 *
 * @return returns true if a match was found and false otherwise
 * This function is designed to split split pins into groups based on their ports
 * because we can write to an entire port at once this increases speed.
 */
bool GPIO_StringToEnum(const char* str, GPIO_OUTPUT_NAMES* out)
{
    for (size_t i = 0; i < sizeof(gpio_name_map)/sizeof(gpio_name_map[0]); i++)
    {
        if (strcmp(str, gpio_name_map[i].name) == 0)
        {
            *out = gpio_name_map[i].value;
            return true;
        }
    }
    return false;
}


/**
 * @brief takes a list of GPIO names and splits them into their ports.
 *
 * @param gpio_names   an array of GPIO pin names, all of which are on the same port
 * @param length       the nubmer of GPIO_T in gpio_names
 * @param destination  pointer to space for 8 GPIO_PORT_PACKET packets (to be written to)
 *
 * @return returns the number of GPIO_PORT_PACKET written to destination
 * This function is designed to split split pins into groups based on their ports
 * because we can write to an entire port at once this increases speed.
 * EXAMPLE: If we want to set DIGITALOUT0, DIGITALOUT1 and DIGITALOUT2, but DIGITALOUT2 uses a
different port,
GPIO_OUTPUT_NAMES* my_arr = { DIGITALOUT0, DIGITALOUT1, DIGITALOUT2 };
GPIO_PORT_PACKET destination[8];
split_about_ports(my_arr, 3, destination);
// we dont HAVE to go through all 8 ports (as only 2 are used) but for examples sake we can
for (int i=0; i<8; i++){  
    HW_GPIO_SetToPort(destination[i].gpiox, destination[i].pin_mask)
}
HW_GPIO_SetToPort(p.gpiox, p.pin_mask)
 */
int split_about_ports( GPIO_OUTPUT_NAMES* gpio_names, uint8_t length, GPIO_PORT_PACKET* destination)
{
    GPIO_PORT_PACKET port_packet;
    GPIO_PORT_PACKET temp[MAX_NUM_GPIO_PORTS];
    int counter = 0;
    // reset data at destination
    for ( int j = 0; j < MAX_NUM_GPIO_PORTS; j++ )
    {
        destination[j].gpiox = GPIO_ports[j];    // reset ports, destination[0] = GPIOA, destination[1] = GPIOB etc
        destination[j].pin_mask = 0;             // reset pin masks
        temp[j].gpiox = GPIO_ports[j];          // reset ports, temp[0] = GPIOA, temp[1] = GPIOB etc
        temp[j].pin_mask = 0;                   // reset pin masks
    }
    for ( int i = 0; i < length; i++ )
    {
        port_packet = HW_GPIO_port_pin_association(gpio_names[i]);
        for (int m=0; m<MAX_NUM_GPIO_PORTS; m++)
        {
            if (port_packet.gpiox == GPIO_ports[m])
            {
                temp[m].pin_mask = temp[m].pin_mask | port_packet.pin_mask;
                break;
            }
        }
    }
    for ( int k = 0; k < MAX_NUM_GPIO_PORTS; k++ )
    {
        if ( temp[k].pin_mask != 0 )
        {
            destination[counter] = temp[k];
            counter += 1;
        }
    }
    return counter;
}

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
GPIO_OUTPUT_NAMES* my_arr = {} DIGITALOUT0, DIGITALOUT1 };
struct GPIO_PORT_PACKET p = combine_port_pin_masks(my_arr, 2)
if (p.pin_mask == 0xFFFF0000){
    error
} else {
    HW_GPIO_SetToPort(p.gpiox, p.pin_mask)
}
 * mocked using GoogleMock.
 */
GPIO_PORT_PACKET combine_port_pin_masks( GPIO_OUTPUT_NAMES* gpio_names, uint8_t length )
{
    GPIO_TypeDef* checker = HW_GPIO_port_pin_association( *gpio_names ).gpiox;
    int pin_mask = 0;
    GPIO_PORT_PACKET port_packet;
    for ( int i = 0; i < length; i++ )  // iterate through the gpio names and combine pin_masks
    {
        port_packet = HW_GPIO_port_pin_association(gpio_names[i]);
        if ( checker != port_packet.gpiox )
        {
            // Not all of the pins had the same port, so return an error
            return (struct GPIO_PORT_PACKET){GPIO_ports[0], 4294901760};    // 4294901760 = 0xFFFF0000
        }
        pin_mask = pin_mask | port_packet.pin_mask; // combine pin masks
    }
    return (struct GPIO_PORT_PACKET){checker, pin_mask};
}

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
GPIO_PORT_PACKET HW_GPIO_port_pin_association( GPIO_OUTPUT_NAMES gpio_name )
{
#ifndef TEST_BUILD
switch ( gpio_name )
{
    // ==== HOW TO ADD DIGITAL OUTPUT PINS ====
    // the following lines (a) and (b) were taken from f446ze_cubeide_project/Core/Inc/main.h
    // they are created automatically by cube IDE from the IOC file and correspond to the actual
    // pins on the board
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
    // as well as to the string mapping gpio_name_map in hw_gpio.h
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
    case LD2:       // Added by Tim for DEV-68
        return (struct GPIO_PORT_PACKET){LD2_GPIO_Port, LD2_Pin};
    case LD3:       // Added by Tim for DEV-68
        return ( struct GPIO_PORT_PACKET ){ LD3_GPIO_Port, LD3_Pin };
    default:        // Added by Tim, should be updated to set the warning LED once IOC is decided
        return (struct GPIO_PORT_PACKET){LD1_GPIO_Port, LD1_Pin};
}
#else
    return HW_GPIO_port_pin_association_to_return;
#endif
}

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
inline void HW_GPIO_SetToPort( GPIO_TypeDef* gpiox, uint32_t pin_mask )
{
#ifdef TEST_BUILD
    // For unit testing, do nothing
    ( void )pin_mask;
    ( void )gpiox;
#else
    // LL_GPIO_XOutputPin functions write to the BSR port register,
    // The lower 16 bits of the pin_mask bit 0 = pin0, bit 1 = pin1 etc
    LL_GPIO_SetOutputPin(gpiox, pin_mask);
#endif
}

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
inline void HW_GPIO_ResetToPort( GPIO_TypeDef* gpiox, uint32_t pin_mask )
{
#ifdef TEST_BUILD
    // For unit testing, do nothing
    ( void )pin_mask;
    ( void )gpiox;
#else
    // LL_GPIO_XOutputPin functions write to the BSR port register,
    // The lower 16 bits of the pin_mask bit 0 = pin0, bit 1 = pin1 etc
    LL_GPIO_ResetOutputPin(gpiox, pin_mask);
#endif
}
