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

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

/**
 * @brief Configures the test scheduling timer.
 *
 * @param psc - Prescalar
 * @param arr - AutoReload Register
 *
 * Configures the timer used for test scheduling with a specified prescalar and autoreload register
value.
 * Note: Also stops the timer if it is currently running.
 */
void HW_TIMER_Configure_Test_Scheduling_Timer( uint32_t psc, uint32_t arr );

/**
 * @brief Starts the timer for test scheduling
 *
 * Starts the timer that generates interrupts at a certain frequency as configured by
 * HW_TIMER_Configure_Test_Scheduling_Timer.
 */
void HW_TIMER_Start_Test_Scheduling_Timer( void );

/**
 * @brief Stops the timer for test scheduling
 */
void HW_TIMER_Stop_Test_Scheduling_Timer( void );

#ifdef __cplusplus
}
#endif

#endif /* HW_TIMER_H */
