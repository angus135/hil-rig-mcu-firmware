/******************************************************************************
 *  File:       hw_pwm_gen.c
 *  Author:     Tim Vogelsang
 *  Created:    25-Mar-2026
 *
 *  Description:
 *      This file is responsible for the low level hardware interraction neccesary for controlling
 *PWM generation
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
#include "tim.h"
#endif

#include "hw_pwm_gen.h"
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

/**
 * @brief Updates the PWM registers with the given ARR and CCR values.
 *
 * @param arr   the value of the auto reloader register (ARR) associated with this PWM signal
 * @param ccr the value of the compare register (CCR) associated with this PWM signal
 *
 * This function sets the values of the PWM registers
 * To calculate the required values functions like HW_PWM_GEN_compute_arr should be used
 * This function is designed to be very fast and should be implemented in the execution phase
 */
#ifndef TEST_BUILD
inline void HW_PWM_GEN_set_pwm_direct( uint16_t arr, uint16_t ccr, uint16_t psc, TIM_TypeDef* tim )
{
    tim->CCER = ccr;
    tim->ARR  = arr;
    tim->PSC  = psc;
    return;
}
#endif

/**-----------------------------------------------------------------------------
 *  Configure Stage Public Function Definitions
 *------------------------------------------------------------------------------
 */

/**
 * @brief Computes the prescaler register (PSC).
 *
 * @param freq_hz   the desired frequency of the PWM signal
 * @param timer_clk_hz the frequency of the timer being used to drive the PWM
 *
 * @return a uint16_t which can be placed directly in the PSC,
 * This function computes the value of the prescaler (PSC)
 * which is needed to achieve the desired frequency
 * These functions should be use during configuration to prepare
 * the frequency and duty cycle instructions for quick running
 */
uint16_t HW_PWM_GEN_compute_psc( uint16_t freq_hz, uint32_t timer_clk_hz )
{
    uint16_t prescaler_p1 = 1;  // the prescaler plus 1
    uint16_t temp         = ( timer_clk_hz / ( freq_hz * ( prescaler_p1 ) ) ) - 1;
    // Inverted Binary search to find prescaler value
    while ( temp > 65535 )
    {
        prescaler_p1 = prescaler_p1 * 2;
        temp         = ( timer_clk_hz / ( freq_hz * ( prescaler_p1 ) ) ) - 1;
    }
    while ( temp < 65535 )
    {
        prescaler_p1 -= 1;
        temp = ( timer_clk_hz / ( freq_hz * ( prescaler_p1 ) ) ) - 1;
    }
    // above -1 loop overcompensated by 1 so we plus 1
    return prescaler_p1;
}

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
uint16_t HW_PWM_GEN_compute_arr( uint16_t freq_hz, uint32_t timer_clk_hz, uint16_t prescaler )
{

    return ( timer_clk_hz / ( freq_hz * ( prescaler + 1 ) ) ) - 1;
}

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
uint16_t HW_PWM_GEN_compute_ccr( uint16_t duty_pm, uint16_t arr )
{
    if ( duty_pm >= 1000 )
    {
        return ( arr + 1 );
    }
    return ( ( arr + 1 ) * duty_pm ) / 1000;
}

/**-----------------------------------------------------------------------------
 *  Execution Stage Public Function Definitions
 *------------------------------------------------------------------------------
 */

/**
 * @brief Updates the PWM registers associated with channel 1.
 *
 * @param arr   the value of the auto reloader register (ARR) associated with this PWM signal
 * @param ccr the value of the compare register (CCR) associated with this PWM signal
 *
 * This function sets the values of the PWM channel 1 registers
 * To calculate the required values functions like HW_PWM_GEN_compute_arr should be used
 * This function is designed to be very fast and should be implemented in the execution phase
 */
inline void HW_PWM_GEN_set_pwm1_direct( uint16_t arr, uint16_t ccr, uint16_t psc )
{
#ifndef TEST_BUILD
    HW_PWM_GEN_set_pwm_direct(
        arr, ccr, psc,
        htim5.Instance );  // TOO DO - UPDAET THIS htim FOR THE ACTUAL TIMER CHANNEL AFTER IOC
#endif
    ( void )arr;
    ( void )ccr;
    ( void )psc;
}

/**
 * @brief Updates the PWM registers associated with channel 2.
 *
 * @param arr   the value of the auto reloader register (ARR) associated with this PWM signal
 * @param ccr the value of the compare register (CCR) associated with this PWM signal
 *
 * This function sets the values of the PWM channel 2 registers
 * To calculate the required values functions like HW_PWM_GEN_compute_arr should be used
 * This function is designed to be very fast and should be implemented in the execution phase
 */
inline void HW_PWM_GEN_set_pwm2_direct( uint16_t arr, uint16_t ccr, uint16_t psc )
{
#ifndef TEST_BUILD
    HW_PWM_GEN_set_pwm_direct(
        arr, ccr, psc,
        htim5.Instance );  // TOO DO - UPDAET THIS htim FOR THE ACTUAL TIMER CHANNEL AFTER IOC
#endif
    ( void )arr;
    ( void )ccr;
    ( void )psc;
}

/**
 * @brief Updates the PWM registers associated with channel 3.
 *
 * @param arr   the value of the auto reloader register (ARR) associated with this PWM signal
 * @param ccr the value of the compare register (CCR) associated with this PWM signal
 *
 * This function sets the values of the PWM channel 3 registers
 * To calculate the required values functions like HW_PWM_GEN_compute_arr should be used
 * This function is designed to be very fast and should be implemented in the execution phase
 */
inline void HW_PWM_GEN_set_pwm3_direct( uint16_t arr, uint16_t ccr, uint16_t psc )
{
#ifndef TEST_BUILD
    HW_PWM_GEN_set_pwm_direct(
        arr, ccr, psc,
        htim5.Instance );  // TOO DO - UPDAET THIS htim FOR THE ACTUAL TIMER CHANNEL AFTER IOC
#endif
    ( void )arr;
    ( void )ccr;
    ( void )psc;
}
