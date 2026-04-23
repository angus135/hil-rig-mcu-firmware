/******************************************************************************
 *  File:       exec_digital_output.c
 *  Author:     Tim Vogelsang
 *  Created:    25-Mar-2026
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

#include "exec_digital_output.h"
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

/**
 * @brief Sets the state of a digital output pin using the underlying GPIO HW
functions. NOT TO BE USED DURING EXECUTION
 *
 * @param pin   the name of the pin that will be set
 *
 *
 * This function wraps the HW_GPIO_Set_Single_Pin( ... ) function.
 * This is a high level function that does lots of processing to match the pin name to a pin port.
 * As such it should not be used during execution, instead use EXEC_DIGITAL_OUTPUT_Set_Output
 * EXAMPLE: EXEC_DIGITAL_OUTPUT_set_pin( DIGITALoutput1 ) sets DIGITALoutput1
 * If you are struggling with ports the functions exist in hardware_low_level/hw_gpio.c to help manage ports
 */
inline void EXEC_DIGITAL_OUTPUT_set_pin( GPIOOutput_T pin )
{
    HW_GPIO_Set_Single_Pin( pin );
}

/**
 * @brief Sets the state of all digital outputs pins given to it, regardless of their port
functions. NOT TO BE USED DURING EXECUTION
 *
 * @param pins   list of pin names
 * @param length   the number of pin names in pins
 *
 * This function wraps the HW_GPIO_Set_Many_Pins( ... ) function.
 * It does lots or processing in the background to match the pin names to the pins, and as such
should not be used during execution
 * EXAMPLE: EXEC_DIGITAL_OUTPUT_set_many_pins({DIGITALoutput1, UART3V3_En}, 2) sets DIGITALoutput1 and UART3V3_En
 * If you are struggling with ports the functions exist in hardware_low_level/hw_gpio.c to help manage ports
 */
inline void EXEC_DIGITAL_OUTPUT_set_many_pins( GPIOOutput_T* pins, uint16_t length )
{
    HW_GPIO_Set_Many_Pins( pins, length );
}

/**
 * @brief Resets the state of a digital output pin using the underlying GPIO HW
functions. NOT TO BE USED DURING EXECUTION
 *
 * @param pin   the name of the pin that will be reset
 *
 * This function wraps the HW_GPIO_Reset_Single_Pin( ... ) function.
 * This is a high level function that does lots of processing to match the pin name to a pin port.
 * As such it should not be used during execution, instead use EXEC_DIGITAL_OUTPUT_Reset_Output
 * EXAMPLE: EXEC_DIGITAL_OUTPUT_reset_pin( DIGITALoutput1 ) resets DIGITALoutput1
 * If you are struggling with ports the functions exist in hardware_low_level/hw_gpio.c to help manage ports
 */
inline void EXEC_DIGITAL_OUTPUT_reset_pin( GPIOOutput_T pin )
{
    HW_GPIO_Reset_Single_Pin( pin );
}

/**
 * @brief Resets the state of many digital output pins given to it, regardless of their port.
THIS SHOULD NOT BE USED DURING EXECUTION
 *
 * @param pins   list of pin names
 * @param length   the number of pin names in pins
 *
 * This function wraps the HW_GPIO_Reset_Many_Pins( ... ) function.
 * similar examples to EXEC_DIGITAL_OUTPUT_set_many_pins()
 * functions exist in hardware_low_level/hw_gpio.c to help manage ports
 */
inline void EXEC_DIGITAL_OUTPUT_reset_many_pins( GPIOOutput_T* pins, uint16_t length )
{
    HW_GPIO_Reset_Many_Pins( pins, length );
}

/**
 * @brief combines many GPIO's (on the same port) into one pin mask. 
 *
 * @param gpio_names   an array of GPIO pin names, all of which are on the same port
 * @param length       the nubmer of GPIOOutput_T in gpio_names
 *
 * @return returns the combined pin mask (uint32_t), if fault return 0xFFFF0000
 *
 * Combines the pinmasks of the gpio_names so that they can be written to the BSR in one step
 * (instead of individually), This is much more efficient and should be used with functions liek EXEC_DIGITAL_OUTPUT_Set_Output.
 * EXAMPLE: if we want to set both DIGITAL_OUT_CH_0 and DIGITAL_OUT_CH_1 we could write
DigitalOutputPinmask_T p = HW_GPIO_Combine_Port_Pin_Masks({DIGITAL_OUT_CH_0, DIGITAL_OUT_CH_1 }, 2)
if (p.pin_mask == 0xFFFF0000){
    error
} else {
    EXEC_DIGITAL_OUTPUT_Set_Output(p)
}
 * mocked using GoogleMock.
 */
inline DigitalOutputPinmask_T DIGITAL_OUTPUT_Combine_Port_Pin_Masks( GPIOOutput_T* gpio_names,
                                                                     uint8_t       length )
{
    return HW_GPIO_Combine_Port_Pin_Masks( gpio_names, length );
}

/**
 * @brief Sets the state of all digital pins on the Digital GPIO Port (assigned in hw_gpio).
 *
 * @param PinMask   Carrys the information about which pins to set
                    If a lower 16 bit is 1 this sets the associated digital output pin
                    If any bit is 0 this indicates no change
                    0th bit corresponds to digital output 0, 1st bit to output 1 etc
 *
 *
 * This function wraps the HW_GPIO_Set_Output( ... ) function provided by the
 * LL layer. It can be used to set a single output pin or many output pins (on the same port).
 * EXAMPLE: EXEC_DIGITAL_OUTPUT_Set_Output( 0x0000_0020 ) sets LL_GPIO_PIN_5 of the Digital GPIO Port high
 * EXAMPLE: EXEC_DIGITAL_OUTPUT_Set_Output( 0x0000_0020 | 0x0000_0001 ) sets LL_GPIO_PIN_5 and
LL_GPIO_PIN_1 of the Digital GPIO Port high
 * Setting multiple pins works because LL_GPIO_PIN_5 and LL_GPIO_PIN_1 are just uint32_t
 * in this case likely 0x0000_0020 0x0000_0001, so 0x0000_0021 is written to the BSR register
 * 0x0000_0021 = 0000_0000_0000_0000_0000_0000_0010_0001 setting pins 1 and 5 high
 * Note: This implementation assumes all digital outputs are on the same GPIO port.
 * By doing so, we can set all the outputs in a single hardware access.
 */
inline void EXEC_DIGITAL_OUTPUT_Set_Output( uint32_t pin_mask )
{
    HW_GPIO_Set_Output( pin_mask );
}

/**
 * @brief Resets the state of all digital pins on the Digital GPIO Port (assigned in hw_gpio).
 *
 * @param PinMask   Carrys the information about which pins to reset
                    If a lower 16 bit is 1 this resets the associated digital output pin
                    If any bit is 0 this indicates no change
                    0th bit corresponds to digital output 0, 1st bit to output 1 etc
 *
 *
 * This function wraps the HW_GPIO_Reset_Output( ... ).
 * It can be used to reset a single output pin or many output pins (on the same port).
 * EXAMPLE: EXEC_DIGITAL_OUTPUT_Reset_Output( 0x0000_0020 ) resets LL_GPIO_PIN_5 of the Digital GPIO Port high
 * EXAMPLE: EXEC_DIGITAL_OUTPUT_Reset_Output( 0x0000_0020 | 0x0000_0001 ) resets LL_GPIO_PIN_5 and
LL_GPIO_PIN_1 of the Digital GPIO Port high
 * Resetting multiple pins works because LL_GPIO_PIN_5 and LL_GPIO_PIN_1 are just uint32_t
 * in this case likely 0x0000_0020 0x0000_0001, so 0x0000_0021 is written to the BSR register
 * 0x0000_0021 = 0000_0000_0000_0000_0000_0000_0010_0001 resetting pins 1 and 5 high
 * Note: This implementation assumes all digital outputs are on the same GPIO port.
 * By doing so, we can reset all the outputs in a single hardware access.
 */
inline void EXEC_DIGITAL_OUTPUT_Reset_Output( uint32_t pin_mask )
{
    HW_GPIO_Reset_Output( pin_mask );
}
