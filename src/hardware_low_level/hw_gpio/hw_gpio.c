/******************************************************************************
 *  File:       hw_gpio.c
 *  Author:     Coen Pasitchnyj
 *  Created:    6-April-2026
 *
 *  Description:
 *      Low-level GPIO control functions. This module provides an abstraction layer over the
 *      underlying GPIO hardware. It includes functions to toggle GPIO outputs and read digital
 *input states. Reading can be done either by individual pin or by reading the entire port at once
 *for efficiency.
 *
 *
 *  Notes:
 *      There are two implementations of the "read all digital inputs" function:
 *      - HW_GPIO_ReadAllDigitalInputs: Reads each input individually using LL_GPIO_IsInputPinSet,
 *        which is straightforward but may be less efficient due to multiple hardware accesses.
 *      - HW_GPIO_ReadAllDigitalInputsSinglePort: Reads the entire GPIO port state once using
 *        LL_GPIO_ReadInputPort and then extracts individual pin states, which is more efficient
 *        but assumes all inputs are on the same GPIO port.
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#ifdef TEST_BUILD
#include "tests/hw_gpio_mocks.h"
#else
#include "gpio.h"
#include "stm32f4xx_ll_gpio.h"
#endif

#include "hw_gpio.h"
#include <stdint.h>
#include <stdbool.h>

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */
#define NUM_DIGITAL_INPUTS 10

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

// Digital input pins: PF3, PF4, PF5, PF7, PF10, PF11, PF12, PF13, PF14, PF15
// static const uint8_t DIGITAL_INPUT_PIN_MAP[NUM_DIGITAL_INPUTS] = { 3, 4, 5, 7, 10, 11, 12, 13,
// 14, 15 };
static bool        s_all_digital_inputs_same_port = false;
static const void* s_digital_inputs_port          = NULL;

static const uint8_t DIGITAL_INPUT_PIN_POSITIONS[NUM_DIGITAL_INPUTS] = {
    __builtin_ctz( Digital_Input_0_Pin ), __builtin_ctz( Digital_Input_1_Pin ),
    __builtin_ctz( Digital_Input_2_Pin ), __builtin_ctz( Digital_Input_3_Pin ),
    __builtin_ctz( Digital_Input_4_Pin ), __builtin_ctz( Digital_Input_5_Pin ),
    __builtin_ctz( Digital_Input_6_Pin ), __builtin_ctz( Digital_Input_7_Pin ),
    __builtin_ctz( Digital_Input_8_Pin ), __builtin_ctz( Digital_Input_9_Pin ) };
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
void HW_GPIO_CheckSamePort( void )
{
    s_digital_inputs_port          = Digital_Input_0_GPIO_Port;
    s_all_digital_inputs_same_port = Digital_Input_1_GPIO_Port == s_digital_inputs_port
                                     && Digital_Input_2_GPIO_Port == s_digital_inputs_port
                                     && Digital_Input_3_GPIO_Port == s_digital_inputs_port
                                     && Digital_Input_4_GPIO_Port == s_digital_inputs_port
                                     && Digital_Input_5_GPIO_Port == s_digital_inputs_port
                                     && Digital_Input_6_GPIO_Port == s_digital_inputs_port
                                     && Digital_Input_7_GPIO_Port == s_digital_inputs_port
                                     && Digital_Input_8_GPIO_Port == s_digital_inputs_port
                                     && Digital_Input_9_GPIO_Port == s_digital_inputs_port;
}

/**
 * @brief Toggles a digital output using the underlying GPIO LL library.
 *
 * @param gpio   The GPIO to toggle
 *
 * This function wraps the LL_GPIO_TogglePin( ... ) function provided by the
 * LL layer. It is a convenient seam for unit testing where the LL call is
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
 * @brief Reads the state of all digital inputs using the underlying GPIO LL library.
 *
 * @param input_states   Array to store the states of the digital inputs
 *
 * This function wraps the LL_GPIO_ReadInputPort( ... )/LL_GPIO_IsInputPinSet( ... ) function
 * provided by the LL layer. It is a convenient seam for unit testing where the LL call is mocked
 * using GoogleMock.
 */
inline void HW_GPIO_ReadAllDigitalInputs( bool* input_states )
{
#ifdef TEST_BUILD
    // For unit testing, just set all to false or mock as needed
    for ( uint8_t i = 0; i < NUM_DIGITAL_INPUTS; ++i )
    {
        input_states[i] = false;
    }
#else
    if ( s_all_digital_inputs_same_port && s_digital_inputs_port != NULL )
    {
        // Use fast single-port read
        uint32_t port_state = LL_GPIO_ReadInputPort( s_digital_inputs_port );
        for ( uint8_t i = 0; i < NUM_DIGITAL_INPUTS; ++i )
            input_states[i] = ( port_state >> DIGITAL_INPUT_PIN_POSITIONS[i] ) & 0x1;
    }
    else
    {
        // Use slow per-pin read
        for ( uint8_t i = 0; i < NUM_DIGITAL_INPUTS; ++i )
            input_states[i] = HW_GPIO_ReadDigitalInput( ( DIGITAL_INPUT_T )i );
    }
#endif
}

/**
 * @brief Reads the state of all digital inputs using the underlying GPIO LL library.
 *
 * @param input  The digital input channel to read
 *
 * This function wraps the LL_GPIO_IsInputPinSet( ... ) function provided by the
 * LL layer. It is a convenient seam for unit testing where the LL call is
 * mocked using GoogleMock.
 */
inline bool HW_GPIO_ReadDigitalInput( DIGITAL_INPUT_T input )
{
#ifdef TEST_BUILD
    // For unit testing, always return false or mock as needed
    ( void )input;
    return false;
#else
    switch ( input )
    {
        case DIGITAL_INPUT_CH_0:
            // Replace with actual pin read, e.g.:
            // return LL_GPIO_IsInputPinSet(GPIOx, GPIO_PIN_y);
            return LL_GPIO_IsInputPinSet( Digital_Input_0_GPIO_Port, Digital_Input_0_Pin );
        case DIGITAL_INPUT_CH_1:
            return LL_GPIO_IsInputPinSet( Digital_Input_1_GPIO_Port, Digital_Input_1_Pin );
        case DIGITAL_INPUT_CH_2:
            return LL_GPIO_IsInputPinSet( Digital_Input_2_GPIO_Port, Digital_Input_2_Pin );
        case DIGITAL_INPUT_CH_3:
            return LL_GPIO_IsInputPinSet( Digital_Input_3_GPIO_Port, Digital_Input_3_Pin );
        case DIGITAL_INPUT_CH_4:
            return LL_GPIO_IsInputPinSet( Digital_Input_4_GPIO_Port, Digital_Input_4_Pin );
        case DIGITAL_INPUT_CH_5:
            return LL_GPIO_IsInputPinSet( Digital_Input_5_GPIO_Port, Digital_Input_5_Pin );
        case DIGITAL_INPUT_CH_6:
            return LL_GPIO_IsInputPinSet( Digital_Input_6_GPIO_Port, Digital_Input_6_Pin );
        case DIGITAL_INPUT_CH_7:
            return LL_GPIO_IsInputPinSet( Digital_Input_7_GPIO_Port, Digital_Input_7_Pin );
        case DIGITAL_INPUT_CH_8:
            return LL_GPIO_IsInputPinSet( Digital_Input_8_GPIO_Port, Digital_Input_8_Pin );
        case DIGITAL_INPUT_CH_9:
            return LL_GPIO_IsInputPinSet( Digital_Input_9_GPIO_Port, Digital_Input_9_Pin );
        default:
            return false;
    }
#endif
}
