/******************************************************************************
 *  File:       hw_pwm_gen.h
 *  Author:     Callum Rafferty
 *  Created:    25-Mar-2026
 *
 *  Description:
 *      <Short description of the module, what it exposes, and how it should be used>
 *
 *  Notes:
 *      <Public assumptions, required initialisation order, dependencies, etc.>
 ******************************************************************************/

#ifndef HW_PWM_GEN_H
#define HW_PWM_GEN_H

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
// Add any needed standard or project-specific includes here

/**-----------------------------------------------------------------------------
 *  Public Defines / Macros
 *------------------------------------------------------------------------------
 */

// #define MODULE_FEATURE_FLAG   (1U)
// Add macros intended for use outside this module here

/**-----------------------------------------------------------------------------
 *  Public Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

// typedef enum { STATE_IDLE, STATE_BUSY } Module_State_T;
// typedef struct { uint16_t value; bool ready; } Module_Data_T;
// Add types that must be visible to other modules here

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

/**
 * @brief Computes the auto reloader register (ARR).
 *
 * @param freq_hz   the desired frequency of the PWM signal
 * @param timer_clk_hz the frequency of the timer being used to drive the PWM
 * @param prescaler the prescaler associated with the driving timer
 *
 * @return a uint16_t which can be placed directly in the ARR, (some advanced timers eg TIM1 use 32
bits)
 * This function computes the value of the auto reloader register (ARR)
 * which is needed to achieve the desired frequency
 * These functions should be use during configuration to prepare
 * the frequency and duty cycle instructions for quick running
 */
uint16_t HW_PWM_GEN_compute_arr( uint32_t freq_hz, uint32_t timer_clk_hz, uint32_t prescaler );

/**
 * @brief Computes the compare register (CCR) for a given duty cycle.
 *
 * @param duty_pm   the desired duty cycle (0>=duty_pm<=1000)
 * @param arr the value of the auto reloader register ARR associated with this PWM signal
 *
 * @return a uint16_t which can be placed directly in the CCR, (some advanced timers eg TIM1 use 32
bits)
 * This function computes the value of the compare register (CCR)
 * which is needed to achieve the desired duty cycle.
 * These functions should be use during configuration to prepare
 * the frequency and duty cycle instructions for quick running
 */
uint16_t HW_PWM_GEN_compute_ccr( uint16_t duty_pm, uint16_t arr );



#ifdef __cplusplus
}
#endif

#endif /* <FILENAME>_H */
