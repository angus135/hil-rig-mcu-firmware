
/******************************************************************************
 *  File:       hw_gpio.h
 *  Author:     Coen Pasitchnyj
 *  Created:    6-April-2026
 *
 *  Description:
 *      Public interface for low-level GPIO control functions. This module exposes functions for
 *      toggling GPIO outputs and reading digital input states.
 *
 *  Notes:
 *      This module is intended to be used by higher level modules that require GPIO control without
 *      needing to know the details of the underlying hardware.
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

typedef enum DigitalInput_T
{
    DIGITAL_INPUT_CH_0,
    DIGITAL_INPUT_CH_1,
    DIGITAL_INPUT_CH_2,
    DIGITAL_INPUT_CH_3,
    DIGITAL_INPUT_CH_4,
    DIGITAL_INPUT_CH_5,
    DIGITAL_INPUT_CH_6,
    DIGITAL_INPUT_CH_7,
    DIGITAL_INPUT_CH_8,
    DIGITAL_INPUT_CH_9,
} DigitalInput_T;

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */
/**
 * @brief Checks and caches if all digital input pins are on the same port.
 *
 * This should be called at system configuration time.
 */
void HW_GPIO_CheckSamePort( void );

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
 * @brief Reads the state of all digital inputs using the underlying GPIO LL library.
 *
 * @param input_states   Array to store the states of the digital inputs
 *
 * This function wraps the LL_GPIO_ReadInputPort( ... )/LL_GPIO_IsInputPinSet( ... ) function
 * provided by the LL layer. It is a convenient seam for unit testing where the LL call is mocked
 * using GoogleMock.
 */
void HW_GPIO_Read_All_Digital_Inputs( bool* input_states );

/**
 * @brief Reads the state of all digital inputs using the underlying GPIO LL library.
 *
 * @param input  The digital input channel to read
 *
 * This function wraps the LL_GPIO_IsInputPinSet( ... ) function provided by the
 * LL layer. It is a convenient seam for unit testing where the LL call is
 * mocked using GoogleMock.
 */
bool HW_GPIO_Read_Digital_Input( DigitalInput_T input );

#ifdef __cplusplus
}
#endif

#endif /* HW_GPIO_H */
