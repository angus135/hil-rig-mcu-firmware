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

typedef enum Timer_T
{
    EXECUTION_MANAGER_TIMER,
    ANALOGUE_INPUT_TIMER,

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
