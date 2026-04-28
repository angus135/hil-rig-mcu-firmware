/******************************************************************************
 *  File:       hw_timer.h
 *  Author:     Angus Corr
 *  Created:    18-Dec-2025
 *
 *  Description:
 *      <Short description of the module, what it exposes, and how it should be used>
 *
 *  Notes:
 *      <Public assumptions, required initialisation order, dependencies, etc.>
 ******************************************************************************/

#ifndef HW_TIMER_H
#define HW_TIMER_H

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

/*
 * PWM_CAPTURE_TIMER_CH1 maps to PWM capture logical channel 1 (TIM2)
 * PWM_CAPTURE_TIMER_CH2 maps to PWM capture logical channel 2 (TIM5)
 *
 * This does NOT correspond to TIM_CHANNEL_1 / TIM_CHANNEL_2.
 */
typedef enum Timer_T
{
    EXECUTION_MANAGER_TIMER,
    ANALOGUE_INPUT_TIMER,
    PWM_CAPTURE_TIMER_CH1,
    PWM_CAPTURE_TIMER_CH2,

} Timer_T;

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

/**
 * @brief Configures the specified timer.
 *
 * @param timer - the timer to configure
 * @param psc - Prescalar
 * @param arr - AutoReload Register
 *
 * Configures the timer with a specified prescalar and autoreload register
value.
 */
void HW_TIMER_Configure_Timer( Timer_T timer, uint32_t psc, uint32_t arr );

/**
 * @brief Starts the specified timer
 *
 * @param timer - the timer to configure
 *
 */
void HW_TIMER_Start_Timer( Timer_T timer );

/**
 * @brief Stops the specified timer
 *
 * @param timer - the timer to configure
 *
 */
void HW_TIMER_Stop_Timer( Timer_T timer );

#ifdef __cplusplus
}
#endif

#endif /* HW_TIMER_H */
