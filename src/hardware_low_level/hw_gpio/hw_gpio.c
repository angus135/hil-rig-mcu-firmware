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

typedef struct GPIOPortPacket_T
{
    GPIO_TypeDef* gpiox;
    uint32_t      pin_mask;
} GPIOPortPacket_T;

GPIO_TypeDef* gpio_ports[] = {
    GPIOA, GPIOB, GPIOC, GPIOD, GPIOE, GPIOF, GPIOG, GPIOH,
};

#define NUM_GPIO_PORTS ( sizeof( gpio_ports ) / sizeof( gpio_ports[0] ) )
#define MAX_NUM_GPIO_PORTS 8
#ifndef TEST_BUILD
#define DIGITAL_OUTPUT_PORT ( GPIO_TypeDef* )GPIOG
#endif

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

/**
 * @brief Returns the hardware port and pin associated with the  software pin name passed into it
 *
 * @param gpio_name     The name of the GPIO pin as defined in hw_gpio.h GPIOOutput_T
 *
 *
 * This function maps the (software) GPIO pin name defined in hw_gpio.h GPIOOutput_T
 * to the mathcing (hardware) GPIO port and GPIO pin number
 * as defined in f446ze_cubeide_project/Core/Inc/main.h to the
 */

static GPIOPortPacket_T HW_GPIO_Port_Pin_Association( GPIOOutput_T gpio_name )
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
        // case DIGITAL_OUT_CH_0:
        //    return (struct GPIOPortPacket_T){LD1_GPIO_Port, LD1_Pin};
        // the above example associates the pin name DIGITAL_OUT_CH_0 (software)
        // to the physical pin name LD1_Pin and its port LD1_GPIO_Port
        // the name chosen (DIGITAL_OUT_CH_0)
        // would then be added to the GPIOOutput_T enum in hw_gpio.h
        // as well as to the string mapping gpio_name_map in hw_gpio.h
        case DIGITAL_OUTPUT_0:  // Added by Tim for DEV-68
            return ( struct GPIOPortPacket_T ){ DIGITAL_OUTPUT_PORT, Digital_Input_0_Pin };
        case DIGITAL_OUTPUT_1:  // Added by Tim for DEV-68
            return ( struct GPIOPortPacket_T ){ DIGITAL_OUTPUT_PORT, Digital_Input_1_Pin };
        case DIGITAL_OUTPUT_2:  // Added by Tim for DEV-68
            return ( struct GPIOPortPacket_T ){ DIGITAL_OUTPUT_PORT, Digital_Input_2_Pin };
        case DIGITAL_OUTPUT_3:  // Added by Tim for DEV-68
            return ( struct GPIOPortPacket_T ){ DIGITAL_OUTPUT_PORT, Digital_Input_3_Pin };
        case DIGITAL_OUTPUT_4:  // Added by Tim for DEV-68
            return ( struct GPIOPortPacket_T ){ DIGITAL_OUTPUT_PORT, Digital_Input_4_Pin };
        case DIGITAL_OUTPUT_5:  // Added by Tim for DEV-68
            return ( struct GPIOPortPacket_T ){ DIGITAL_OUTPUT_PORT, Digital_Input_5_Pin };
        case DIGITAL_OUTPUT_6:  // Added by Tim for DEV-68
            return ( struct GPIOPortPacket_T ){ DIGITAL_OUTPUT_PORT, Digital_Input_6_Pin };
        case DIGITAL_OUTPUT_7:  // Added by Tim for DEV-68
            return ( struct GPIOPortPacket_T ){ DIGITAL_OUTPUT_PORT, Digital_Input_7_Pin };
        case DIGITAL_OUTPUT_8:  // Added by Tim for DEV-68
            return ( struct GPIOPortPacket_T ){ DIGITAL_OUTPUT_PORT, Digital_Input_8_Pin };
        case DIGITAL_OUTPUT_9:  // Added by Tim for DEV-68
            return ( struct GPIOPortPacket_T ){ DIGITAL_OUTPUT_PORT, Digital_Input_9_Pin };
        case USB_POWERSWITCH:  // Added by Tim as an example in DEV-68
            return ( struct GPIOPortPacket_T ){ USB_PowerSwitchOn_GPIO_Port,
                                                USB_PowerSwitchOn_Pin };
        case USB_OVER_CURRENT:  // Added by Tim as an example in DEV-68
            return ( struct GPIOPortPacket_T ){ USB_OverCurrent_GPIO_Port, USB_OverCurrent_Pin };
        case LD1:  // Added by Tim for DEV-68
            return ( struct GPIOPortPacket_T ){ LD1_GPIO_Port, LD1_Pin };
        case LD2:  // Added by Tim for DEV-68
            return ( struct GPIOPortPacket_T ){ LD2_GPIO_Port, LD2_Pin };
        case LD3:  // Added by Tim for DEV-68
            return ( struct GPIOPortPacket_T ){ LD3_GPIO_Port, LD3_Pin };
        default:  // Added by Tim, should be updated to set the warning LED once IOC is decided
            return ( struct GPIOPortPacket_T ){ LD1_GPIO_Port, LD1_Pin };
    }
    // Added by Tim, should be updated to set the warning LED once IOC is decided
    return ( struct GPIOPortPacket_T ){ LD1_GPIO_Port, LD1_Pin };
#else
    // Added by Tim, should be updated to set the warning LED once IOC is decided
    return ( struct GPIOPortPacket_T ){ LD1_GPIO_Port, LD1_Pin };
#endif
}

/**
 * @brief takes a list of GPIO names and splits them into their ports.
 *
 * @param gpio_names   an array of GPIO pin names, which may be on different ports
 * @param length       the nubmer of GPIOOutput_T in gpio_names
 * @param destination  pointer to space for 8 GPIOPortPacket_T packets (to be written to)
 *
 * @return returns the number of GPIOPortPacket_T's written to destination
 * This function is designed to split split pins into groups based on their ports
 * because we can write to an entire port at once this can increas speed.
 * EXAMPLE: If we want to set DIGITAL_OUT_CH_0, DIGITAL_OUT_CH_1 and DIGITAL_OUT_CH_2, but
DIGITAL_OUT_CH_2 uses a different port,
    GPIOOutput_T* my_arr = { DIGITAL_OUT_CH_0, DIGITAL_OUT_CH_1, DIGITAL_OUT_CH_2 };
    GPIOPortPacket_T destination[8];
    HW_GPIO_split_about_ports(my_arr, 3, destination);
// we dont HAVE to go through all 8 ports (as only 2 are used) but for examples sake we can
for (int i=0; i<8; i++){
    HW_GPIO_Set_To_Port(destination[i].gpiox, destination[i].pin_mask)
}
HW_GPIO_Set_To_Port(p.gpiox, p.pin_mask)
 */
int HW_GPIO_split_about_ports( GPIOOutput_T* gpio_names, uint16_t length,
                               GPIOPortPacket_T* destination )
{
    GPIOPortPacket_T port_packet;
    GPIOPortPacket_T temp[MAX_NUM_GPIO_PORTS];
    int              counter = 0;
    // reset data at destination
    for ( int j = 0; j < MAX_NUM_GPIO_PORTS; j++ )
    {
        destination[j].gpiox =
            gpio_ports[j];  // reset ports, destination[0] = GPIOA, destination[1] = GPIOB etc
        destination[j].pin_mask = 0;       // reset pin masks
        temp[j].gpiox    = gpio_ports[j];  // reset ports, temp[0] = GPIOA, temp[1] = GPIOB etc
        temp[j].pin_mask = 0;              // reset pin masks
    }
    for ( int i = 0; i < length; i++ )
    {
        port_packet = HW_GPIO_Port_Pin_Association( gpio_names[i] );
        for ( int j = 0; j < MAX_NUM_GPIO_PORTS; j++ )
        {
            if ( port_packet.gpiox == gpio_ports[j] )
            {
                temp[j].pin_mask = temp[j].pin_mask | port_packet.pin_mask;
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
 * @brief Sets the state of all digital outputs in a GPIO Port using the underlying GPIO LL library.
 *
 * @param PinMask   Carrys the information about which pins to set
                    If a lower 16 bit is 1 this sets the associated digital output
                    If any bit is 0 this indicates no change
                    0th bit corresponds to digital output 0, 1st bit to output 1 etc
 *
 *
 * This function wraps the LL_GPIO_ResetOutputPin( ... ) function provided by the
 * LL layer. It can be used to set a single output pin or many output pins (on the same port).
 * EXAMPLE: HW_GPIO_Set_To_Port( GPIOA, LL_GPIO_PIN_5 ) sets LL_GPIO_PIN_5 of port A high
 * EXAMPLE: HW_GPIO_Set_To_Port( GPIOA, LL_GPIO_PIN_5 | LL_GPIO_PIN_4 ) sets LL_GPIO_PIN_5 and
LL_GPIO_PIN_4 of port A high
 * Setting multiple pins works because LL_GPIO_PIN_5 and LL_GPIO_PIN_4 are just uint32_t
 * in this case likely 0x0000_0020 0x0000_0010, so 0x0000_0030 is written to the BSR register
 * 0x0000_0030 = 0000_0000_0000_0000_0000_0000_0011_0000 setting pins 4 and 5 high
 * mocked using GoogleMock.
 * Note: This implementation assumes all digital outputs are on the same GPIO port.
 * By doing so, we can set all the outputs in a single hardware access.
 */
static void HW_GPIO_Set_To_Port( GPIO_TypeDef* gpiox, int32_t pin_mask )
{
#ifdef TEST_BUILD
    // For unit testing, do nothing
    ( void )pin_mask;
    ( void )gpiox;
#else
    // LL_GPIO_XOutputPin functions write to the BSR port register,
    // The lower 16 bits of the pin_mask bit 0 = pin0, bit 1 = pin1 etc
    LL_GPIO_SetOutputPin( gpiox, pin_mask );
#endif
}

/**
 * @brief Resets the state of all digital outputs in a GPIO Port using the underlying GPIO LL
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
 * EXAMPLE: HW_GPIO_Reset_To_Port( GPIOA, LL_GPIO_PIN_5 ) sets LL_GPIO_PIN_5 of port A low
 * EXAMPLE: HW_GPIO_Reset_To_Port( GPIOA, LL_GPIO_PIN_5 | LL_GPIO_PIN_4 ) sets LL_GPIO_PIN_5 and
LL_GPIO_PIN_4 of port A low
 * Reseting multiple pins works because LL_GPIO_PIN_5 and LL_GPIO_PIN_4 are just uint32_t
 * in this case likely 0x0000_0020 0x0000_0010, so 0x0000_0030 is written to the BSR register
 * 0x0000_0030 = 0000_0000_0000_0000_0000_0000_0011_0000 reseting pins 4 and 5 high
 * mocked using GoogleMock.
 * Note: This implementation assumes all digital outputs are on the same GPIO port.
 * By doing so, we can set all the outputs in a single hardware access.
 */
static void HW_GPIO_Reset_To_Port( GPIO_TypeDef* gpiox, int32_t pin_mask )
{
#ifdef TEST_BUILD
    // For unit testing, do nothing
    ( void )pin_mask;
    ( void )gpiox;
#else
    // LL_GPIO_XOutputPin functions write to the BSR port register,
    // The lower 16 bits of the pin_mask bit 0 = pin0, bit 1 = pin1 etc
    LL_GPIO_ResetOutputPin( gpiox, pin_mask );
#endif
}

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
 * @brief Sets the state of all digital outputs in a GPIO Port using the underlying GPIO LL library.
 *
 * @param PinMask   Carrys the information about which pins to set
                    If a lower 16 bit is 1 this sets the associated digital output
                    If any bit is 0 this indicates no change
                    0th bit corresponds to digital output 0, 1st bit to output 1 etc
 *
 *
 * This function wraps the LL_GPIO_ResetOutputPin( ... ) function provided by the
 * LL layer. It can be used to set a single output pin or many output pins (on the same port).
 * EXAMPLE: HW_GPIO_Set_To_Port( GPIOA, LL_GPIO_PIN_5 ) sets LL_GPIO_PIN_5 of port A high
 * EXAMPLE: HW_GPIO_Set_To_Port( GPIOA, LL_GPIO_PIN_5 | LL_GPIO_PIN_4 ) sets LL_GPIO_PIN_5 and
LL_GPIO_PIN_4 of port A high
 * Setting multiple pins works because LL_GPIO_PIN_5 and LL_GPIO_PIN_4 are just uint32_t
 * in this case likely 0x0000_0020 0x0000_0010, so 0x0000_0030 is written to the BSR register
 * 0x0000_0030 = 0000_0000_0000_0000_0000_0000_0011_0000 setting pins 4 and 5 high
 * mocked using GoogleMock.
 * Note: This implementation assumes all digital outputs are on the same GPIO port.
 * By doing so, we can set all the outputs in a single hardware access.
 */
inline void HW_GPIO_Set_Output( uint32_t pin_mask )
{
#ifdef TEST_BUILD
    // For unit testing, do nothing
    ( void )pin_mask;
#else
    // LL_GPIO_XOutputPin functions write to the BSR port register,
    // The lower 16 bits of the pin_mask bit 0 = pin0, bit 1 = pin1 etc
    LL_GPIO_SetOutputPin( DIGITAL_OUTPUT_PORT, pin_mask );
#endif
}

/**
 * @brief Sets the state of all digital outputs in a GPIO Port using the underlying GPIO LL library.
 *
 * @param PinMask   Carrys the information about which pins to set
                    If a lower 16 bit is 1 this sets the associated digital output
                    If any bit is 0 this indicates no change
                    0th bit corresponds to digital output 0, 1st bit to output 1 etc
 *
 *
 * This function wraps the LL_GPIO_ResetOutputPin( ... ) function provided by the
 * LL layer. It can be used to set a single output pin or many output pins (on the same port).
 * EXAMPLE: HW_GPIO_Set_To_Port( GPIOA, LL_GPIO_PIN_5 ) sets LL_GPIO_PIN_5 of port A high
 * EXAMPLE: HW_GPIO_Set_To_Port( GPIOA, LL_GPIO_PIN_5 | LL_GPIO_PIN_4 ) sets LL_GPIO_PIN_5 and
LL_GPIO_PIN_4 of port A high
 * Setting multiple pins works because LL_GPIO_PIN_5 and LL_GPIO_PIN_4 are just uint32_t
 * in this case likely 0x0000_0020 0x0000_0010, so 0x0000_0030 is written to the BSR register
 * 0x0000_0030 = 0000_0000_0000_0000_0000_0000_0011_0000 setting pins 4 and 5 high
 * mocked using GoogleMock.
 * Note: This implementation assumes all digital outputs are on the same GPIO port.
 * By doing so, we can set all the outputs in a single hardware access.
 */
inline void HW_GPIO_Reset_Output( uint32_t pin_mask )
{
#ifdef TEST_BUILD
    // For unit testing, do nothing
    ( void )pin_mask;
    return;
#else
    // LL_GPIO_XOutputPin functions write to the BSR port register,
    // The lower 16 bits of the pin_mask bit 0 = pin0, bit 1 = pin1 etc
    LL_GPIO_ResetOutputPin( DIGITAL_OUTPUT_PORT, pin_mask );
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
bool HW_GPIO_StringToEnum( const char* str, GPIOOutput_T* out )
{
    for ( size_t i = 0; i < sizeof( gpio_name_map ) / sizeof( gpio_name_map[0] ); i++ )
    {
        if ( strcmp( str, gpio_name_map[i].name ) == 0 )
        {
            *out = gpio_name_map[i].value;
            return true;
        }
    }
    return false;
}

/**
 * @brief Sets a GPIO pin
 *
 * @param pin The name of the pin we wish to set
 *
 * This function locates the port and pin number (pin_mask) associated with the pin
 * and uses them to set the pin on the STM
 */
void HW_GPIO_Set_Single_Pin( GPIOOutput_T pin )
{
#ifndef TEST_BUILD
    GPIOPortPacket_T pack = HW_GPIO_Port_Pin_Association( pin );
    HW_GPIO_Set_To_Port( pack.gpiox, pack.pin_mask );
    return;
#endif
    ( void )pin;  // If testing then do nothing
}

/**
 * @brief Sets many GPIO pins
 *
 * @param pins A list of pin names we wish to set
 * @param length the number of pin names in pins (length of pins)
 *
 * This function locates the ports and pin numbers (pin_masks) associated with the pins
 * and uses them to set the pins on the STM
 */
void HW_GPIO_Set_Many_Pins( GPIOOutput_T* pins, uint16_t length )
{
#ifndef TEST_BUILD
    GPIOPortPacket_T packs[MAX_NUM_GPIO_PORTS];
    int              num_ports = HW_GPIO_split_about_ports( pins, length, packs );
    for ( int i = 0; i < num_ports; i++ )
    {
        HW_GPIO_Set_To_Port( packs[i].gpiox, packs[i].pin_mask );
    }
#endif
    ( void )pins;  // If testing then do nothing
}

/**
 * @brief Sets a GPIO pin
 *
 * @param pin The name of the pin we wish to set
 *
 * This function locates the port and pin number (pin_mask) associated with the pin
 * and uses them to set the pin on the STM
 */
void HW_GPIO_Reset_Single_Pin( GPIOOutput_T pin )
{
#ifndef TEST_BUILD
    GPIOPortPacket_T pack = HW_GPIO_Port_Pin_Association( pin );
    HW_GPIO_Reset_To_Port( pack.gpiox, pack.pin_mask );
#endif
    ( void )pin;  // If testing then do nothing
}

/**
 * @brief Resets many GPIO pins
 *
 * @param pins A list of pin names we wish to reset
 * @param length the number of pin names in pins (length of pins)
 *
 * This function locates the ports and pin numbers (pin_masks) associated with the pins
 * and uses them to reset the pins on the STM
 */
void HW_GPIO_Reset_Many_Pins( GPIOOutput_T* pins, uint16_t length )
{
#ifndef TEST_BUILD
    GPIOPortPacket_T packs[MAX_NUM_GPIO_PORTS];
    int              num_ports = HW_GPIO_split_about_ports( pins, length, packs );
    for ( int i = 0; i < num_ports; i++ )
    {
        HW_GPIO_Reset_To_Port( packs[i].gpiox, packs[i].pin_mask );
    }
#endif
    ( void )pins;  // If testing then do nothing
}

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
GPIOOutput_T* my_arr = {} DIGITAL_OUT_CH_0, DIGITAL_OUT_CH_1 };
struct GPIOPortPacket_T p = HW_GPIO_Combine_Port_Pin_Masks(my_arr, 2)
if (p.pin_mask == 0xFFFF0000){
    error
} else {
    HW_GPIO_Set_To_Port(p.gpiox, p.pin_mask)
}
 * mocked using GoogleMock.
 */
DigitalOutputPinmask_T HW_GPIO_Combine_Port_Pin_Masks( GPIOOutput_T* gpio_names, uint8_t length )
{
    GPIO_TypeDef*    checker  = HW_GPIO_Port_Pin_Association( *gpio_names ).gpiox;
    int              pin_mask = 0;
    GPIOPortPacket_T port_packet;
    for ( int i = 0; i < length; i++ )  // iterate through the gpio names and combine pin_masks
    {
        port_packet = HW_GPIO_Port_Pin_Association( gpio_names[i] );
        if ( checker != port_packet.gpiox )
        {
            // Not all of the pins had the same port, so return an error
            return 0xFFFF0000;  // 4294901760 = 0xFFFF0000
        }
        pin_mask = pin_mask | port_packet.pin_mask;  // combine pin masks
    }
    return pin_mask;
}
