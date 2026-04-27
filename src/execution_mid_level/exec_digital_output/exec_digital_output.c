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
 * @brief combines many GPIO's (on the same port) into one pin mask.
 *
 * @param gpio_names   an array of GPIO pin names, all of which are on the same port
 * @param length       the nubmer of GPIOOutput_T in gpio_names
 *
 * @return returns the combined pin mask (uint32_t), if fault return 0xFFFF0000
 *
 * Combines the pinmasks of the gpio_names so that they can be written to the BSR in one step
 * (instead of individually), This is much more efficient and should be used with functions liek
EXEC_DIGITAL_OUTPUT_Set_Output.
 * EXAMPLE: if we want to set both DIGITAL_OUT_CH_0 and DIGITAL_OUT_CH_1 we could write
DigitalOutputPinmask_T p = HW_GPIO_Combine_Port_Pin_Masks({DIGITAL_OUT_CH_0, DIGITAL_OUT_CH_1 }, 2)
if (p.pin_mask == 0xFFFF0000){
    error
} else {
    EXEC_DIGITAL_OUTPUT_Set_Output(p)
}
 * mocked using GoogleMock.
 */
inline DigitalOutputPinmask_T EXEC_DIGITAL_OUTPUT_Combine_Port_Pin_Masks( GPIOOutput_T* gpio_names,
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
 * EXAMPLE: EXEC_DIGITAL_OUTPUT_Set_Output( 0x0000_0020 ) sets LL_GPIO_PIN_5 of the Digital GPIO
Port high
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
 * EXAMPLE: EXEC_DIGITAL_OUTPUT_Reset_Output( 0x0000_0020 ) resets LL_GPIO_PIN_5 of the Digital GPIO
Port high
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
