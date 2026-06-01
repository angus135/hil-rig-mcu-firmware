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
#include "stm32f4xx_ll_tim.h"
#include "tim.h"
#include "stm32f4xx_hal_tim.h"
#else
#include "tests/hw_pwm_gen_mocks.h"
#endif

#include "hw_pwm_gen.h"
#include <stdint.h>
#include <stdbool.h>

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */

#define CHANNEL1_TIM &htim12
#define CHANNEL2_TIM &htim13

#define MAX_ARR_COUNTS 65535  // the max value of our ARR register (atm uint16_t)

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
void HW_PWM_GEN_config( PwmGenChannel_T channel, PwmGenVoltageLevel_T volt_lvl )
{
    // Call to output expander to set voltage levels

    if ( channel == PWM_GEN_CHANNEL_LV )
    {
        if ( volt_lvl == PWM_GEN_VOLTAGE_LOW )
        {
            // Call to output expander to set voltage levels
            HAL_TIM_PWM_Start( CHANNEL1_TIM, TIM_CHANNEL_1 );
        }
        else if ( volt_lvl == PWM_GEN_VOLTAGE_HIGH )
        {
            // Call to output expander to set voltage levels
            HAL_TIM_PWM_Start( CHANNEL1_TIM, TIM_CHANNEL_1 );
        }
    }
    else if ( channel == PWM_GEN_CHANNEL_HV )
    {
        if ( volt_lvl == PWM_GEN_VOLTAGE_LOW )
        {
            // Call to output expander to set voltage levels
            HAL_TIM_PWM_Start( CHANNEL2_TIM, TIM_CHANNEL_2 );
        }
        else if ( volt_lvl == PWM_GEN_VOLTAGE_HIGH )
        {
            // Call to output expander to set voltage levels
            HAL_TIM_PWM_Start( CHANNEL2_TIM, TIM_CHANNEL_2 );
        }
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
inline void HW_PWM_GEN_Set_PWM1_Direct( uint16_t arr, uint16_t ccr, uint16_t psc )
{
    TIM_TypeDef* tim = ( *CHANNEL1_TIM ).Instance;
    LL_TIM_OC_SetCompareCH2( tim, ccr );
    LL_TIM_SetAutoReload( tim, arr );
    LL_TIM_SetPrescaler( tim, psc );
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
inline void HW_PWM_GEN_Set_PWM2_Direct( uint16_t arr, uint16_t ccr, uint16_t psc )
{
    TIM_TypeDef* tim = ( *CHANNEL1_TIM ).Instance;
    LL_TIM_OC_SetCompareCH1( tim, ccr );
    LL_TIM_SetAutoReload( tim, arr );
    LL_TIM_SetPrescaler( tim, psc );
}
