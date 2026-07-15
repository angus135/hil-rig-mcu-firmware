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
#include "tim.h"
#include "stm32f4xx_hal_tim.h"
#include "stm32f4xx_ll_tim.h"
#else
#include "tests/hw_pwm_gen_mocks.h"
#endif

#include "hw_pwm_gen.h"
#include <stdint.h>

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */

#define MAX_ARR_COUNTS 65535  // the max value of our ARR register (atm uint16_t)

/* PWM1 / LV hardware mapping */
#define PWM1_TIMER_HANDLE ( &htim12 )
#define PWM1_HAL_CHANNEL TIM_CHANNEL_2
#define PWM1_LL_SET_COMPARE LL_TIM_OC_SetCompareCH2
#define PWM1_START_OUTPUT HAL_TIM_PWM_Start

/* PWM2 / HV hardware mapping */
#define PWM2_TIMER_HANDLE ( &htim8 )
#define PWM2_HAL_CHANNEL TIM_CHANNEL_2
#define PWM2_LL_SET_COMPARE LL_TIM_OC_SetCompareCH2
#define PWM2_START_OUTPUT HAL_TIMEx_PWMN_Start

/**-----------------------------------------------------------------------------
 *  Typedefs / Enums / Structures
 *------
------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Public (global) and Extern Variables
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
 *  Configure Stage Public Function Definitions
 *------------------------------------------------------------------------------
 * These functions should not be run during execution,
 * instead they should be run previously to pre-compute the required values
 */

/**
 * @brief Configures the pwm output.
 *
 * @param channel   The channel you want to configure <1|2|3|4>
 * @param volt_lvl  The voltage level you want (low or high <0|1>)
 *
 */
void HW_PWM_GEN_Config( PwmGenChannel_T channel, PwmGenVoltageLevel_T volt_lvl )
{
    if ( volt_lvl != PWM_GEN_VOLTAGE_LOW && volt_lvl != PWM_GEN_VOLTAGE_HIGH )
    {
        return;
    }

    if ( channel == PWM_GEN_CHANNEL_LV )
    {
        // Call to output expander to set voltage levels
        ( void )PWM1_START_OUTPUT( PWM1_TIMER_HANDLE, PWM1_HAL_CHANNEL );
    }
    else if ( channel == PWM_GEN_CHANNEL_HV )
    {
        // Call to output expander to set voltage levels
        ( void )PWM2_START_OUTPUT( PWM2_TIMER_HANDLE, PWM2_HAL_CHANNEL );
    }
}

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
uint16_t HW_PWM_GEN_compute_psc( uint32_t freq_hz, uint32_t timer_clk_hz )
{
    if ( freq_hz > timer_clk_hz || freq_hz > 1000000 )
    {
        return 0xFFFF;  // TO DO - replace with proper error communication
    }
    if ( freq_hz == 0 )
    {
        return 0xFFFF;  // TO DO - replace with proper error communication
    }
    uint32_t temp = ( timer_clk_hz / freq_hz );
    uint32_t divider;
    // if temp is an exact multiple then we don't need rounding
    if ( temp % MAX_ARR_COUNTS == 0 )
    {
        return ( uint16_t )( ( timer_clk_hz / freq_hz ) / MAX_ARR_COUNTS ) - 1;
    }
    // if temp is not exact multiple then we need rounding (+1)
    divider = ( ( timer_clk_hz / freq_hz ) / MAX_ARR_COUNTS ) + 1;  // the prescaler plus 1
    if ( divider == 0 )
    {
        return 0xFFFF;  // TO DO - replace with proper error communication
    }
    return ( uint16_t )( divider - 1 );
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
uint16_t HW_PWM_GEN_compute_arr( uint32_t freq_hz, uint32_t timer_clk_hz, uint16_t prescaler )
{
    uint32_t divider = freq_hz * ( ( uint32_t )prescaler + 1U );

    if ( divider == 0U )
    {
        return 0xFFFF;
    }

    return ( uint16_t )( ( timer_clk_hz / divider ) - 1U );
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
        return ( uint16_t )( arr + 1 );
    }
    return ( uint16_t )( ( ( arr + 1 ) * duty_pm ) / 1000 );
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
void HW_PWM_GEN_Set_PWM1_Direct( uint16_t arr, uint16_t ccr, uint16_t psc )
{
    TIM_TypeDef* timer = PWM1_TIMER_HANDLE->Instance;

    PWM1_LL_SET_COMPARE( timer, ccr );
    LL_TIM_SetAutoReload( timer, arr );
    LL_TIM_SetPrescaler( timer, psc );
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
void HW_PWM_GEN_Set_PWM2_Direct( uint16_t arr, uint16_t ccr, uint16_t psc )
{
    TIM_TypeDef* timer = PWM2_TIMER_HANDLE->Instance;

    PWM2_LL_SET_COMPARE( timer, ccr );
    LL_TIM_SetAutoReload( timer, arr );
    LL_TIM_SetPrescaler( timer, psc );
}
