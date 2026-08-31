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
#include <stddef.h>
#include <stdint.h>

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */

/* Reserve a representable CCR = ARR + 1 for 100% duty in prepared waveforms. */
#define MAX_ARR_COUNTS UINT16_MAX

/* PWM1 / LV hardware mapping */
#define PWM1_TIMER_HANDLE ( &htim12 )
#define PWM1_HAL_CHANNEL TIM_CHANNEL_2
#define PWM1_LL_SET_COMPARE LL_TIM_OC_SetCompareCH2
#define PWM1_START_OUTPUT HAL_TIM_PWM_Start
#define PWM1_STOP_OUTPUT HAL_TIM_PWM_Stop

/* PWM2 / HV hardware mapping */
#define PWM2_TIMER_HANDLE ( &htim8 )
#define PWM2_HAL_CHANNEL TIM_CHANNEL_2
#define PWM2_LL_SET_COMPARE LL_TIM_OC_SetCompareCH2
#define PWM2_START_OUTPUT HAL_TIMEx_PWMN_Start
#define PWM2_STOP_OUTPUT HAL_TIMEx_PWMN_Stop

/**-----------------------------------------------------------------------------
 *  Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

typedef struct
{
    bool is_configured;
    bool is_started;
} HWPwmGenChannelState_T;

/**-----------------------------------------------------------------------------
 *  Public (global) and Extern Variables
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Private (static) Variables
 *------------------------------------------------------------------------------
 */

static HWPwmGenChannelState_T hw_pwm_gen_channel_states[HW_PWM_GEN_CHANNEL_COUNT] = {
    [HW_PWM_GEN_CHANNEL_LV] =
        {
            .is_configured = false,
            .is_started    = false,
        },
    [HW_PWM_GEN_CHANNEL_HV] =
        {
            .is_configured = false,
            .is_started    = false,
        },
};

/**-----------------------------------------------------------------------------
 *  Private (static) Function Prototypes
 *------------------------------------------------------------------------------
 */
static bool HW_PWM_GEN_Is_Valid_Channel( HwPwmGenChannel_T channel );

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

static bool HW_PWM_GEN_Is_Valid_Channel( HwPwmGenChannel_T channel )
{
    return ( uint32_t )channel < ( uint32_t )HW_PWM_GEN_CHANNEL_COUNT;
}

/**-----------------------------------------------------------------------------
 *  Configure Stage Public Function Definitions
 *------------------------------------------------------------------------------
 * These functions should not be run during execution,
 * instead they should be run previously to pre-compute the required values
 */

/**
 * @brief Configures a PWM generation channel without starting its output.
 *
 * @param channel PWM generation channel to configure.
 * @return true if the channel was configured successfully; otherwise false.
 */
bool HW_PWM_GEN_Configure_Channel( HwPwmGenChannel_T channel )
{
    if ( !HW_PWM_GEN_Is_Valid_Channel( channel ) )
    {
        return false;
    }

    HWPwmGenChannelState_T* state = &hw_pwm_gen_channel_states[channel];

    if ( state->is_started )
    {
        return false;
    }

    /*
     * Timer base, PWM mode, output polarity, and GPIO alternate-function
     * configuration are currently provided by the generated board setup.
     * Configuration therefore records that this channel is available while
     * leaving its output stopped.
     */
    state->is_configured = true;
    state->is_started    = false;

    return true;
}

bool HW_PWM_GEN_Start_Channel( HwPwmGenChannel_T channel )
{
    if ( !HW_PWM_GEN_Is_Valid_Channel( channel ) )
    {
        return false;
    }

    HWPwmGenChannelState_T* state = &hw_pwm_gen_channel_states[channel];

    if ( !state->is_configured || state->is_started )
    {
        return false;
    }

    HAL_StatusTypeDef status;

    switch ( channel )
    {
        case HW_PWM_GEN_CHANNEL_LV:
            status = PWM1_START_OUTPUT( PWM1_TIMER_HANDLE, PWM1_HAL_CHANNEL );
            break;

        case HW_PWM_GEN_CHANNEL_HV:
            status = PWM2_START_OUTPUT( PWM2_TIMER_HANDLE, PWM2_HAL_CHANNEL );
            break;

        default:
            return false;
    }

    if ( status != HAL_OK )
    {
        return false;
    }

    state->is_started = true;

    return true;
}

/**
 * @brief Stops a started PWM generation channel while retaining configuration.
 *
 * @param channel PWM generation channel to stop.
 * @return true if the channel was stopped successfully; otherwise false.
 */
bool HW_PWM_GEN_Stop_Channel( HwPwmGenChannel_T channel )
{
    if ( !HW_PWM_GEN_Is_Valid_Channel( channel ) )
    {
        return false;
    }

    HWPwmGenChannelState_T* state = &hw_pwm_gen_channel_states[channel];

    if ( !state->is_configured || !state->is_started )
    {
        return false;
    }

    HAL_StatusTypeDef status;

    switch ( channel )
    {
        case HW_PWM_GEN_CHANNEL_LV:
            status = PWM1_STOP_OUTPUT( PWM1_TIMER_HANDLE, PWM1_HAL_CHANNEL );
            break;

        case HW_PWM_GEN_CHANNEL_HV:
            status = PWM2_STOP_OUTPUT( PWM2_TIMER_HANDLE, PWM2_HAL_CHANNEL );
            break;

        default:
            return false;
    }

    if ( status != HAL_OK )
    {
        return false;
    }

    state->is_started = false;

    return true;
}
/**
 * @brief Prepare a prescaler with room for 100% duty in a 16-bit CCR.
 * @return false for invalid frequency/clock or NULL output; output is unchanged.
 */
bool HW_PWM_GEN_compute_psc( uint32_t freq_hz, uint32_t timer_clk_hz, uint16_t* psc )
{
    if ( psc == NULL || freq_hz == 0U || freq_hz > 1000000U || freq_hz > timer_clk_hz )
    {
        return false;
    }

    /* Choose the smallest divider whose truncated period is <= UINT16_MAX.
     * ARR is period - 1, leaving CCR = period representable for 100% duty.
     * With a uint32_t clock and nonzero frequency, divider is at most 65536.
     */
    const uint32_t period_counts = timer_clk_hz / freq_hz;
    const uint32_t divider = period_counts / ( ( uint32_t )MAX_ARR_COUNTS + 1U ) + 1U;
    *psc = ( uint16_t )( divider - 1U );
    return true;
}

/**
 * @brief Prepare ARR from a frequency and prescaler, rounding period down.
 * @return false if the period cannot fit or output is NULL; output is unchanged.
 */
bool HW_PWM_GEN_compute_arr( uint32_t freq_hz, uint32_t timer_clk_hz,
                               uint16_t prescaler, uint16_t* arr )
{
    if ( arr == NULL || freq_hz == 0U || freq_hz > 1000000U || timer_clk_hz == 0U )
    {
        return false;
    }

    /* Widen before multiplying: frequency * (PSC + 1) can exceed uint32_t. */
    const uint64_t divider = ( uint64_t )freq_hz * ( ( uint32_t )prescaler + 1U );
    const uint64_t period_counts = timer_clk_hz / divider;
    if ( period_counts == 0U || period_counts > ( ( uint32_t )UINT16_MAX + 1U ) )
    {
        return false;
    }

    *arr = ( uint16_t )( period_counts - 1U );
    return true;
}

/**
 * @brief Prepare CCR for a duty cycle in permille (0..1000).
 * @return false for invalid duty, unrepresentable CCR or NULL output.
 *         Output is unchanged on failure.
 */
bool HW_PWM_GEN_compute_ccr( uint16_t duty_pm, uint16_t arr, uint16_t* ccr )
{
    if ( ccr == NULL || duty_pm > 1000U )
    {
        return false;
    }

    const uint32_t period_counts = ( uint32_t )arr + 1U;
    const uint32_t compare = ( period_counts * duty_pm ) / 1000U;
    if ( compare > UINT16_MAX )
    {
        /* ARR=65535 at 100% needs CCR=65536, outside the current 16-bit API. */
        return false;
    }

    *ccr = ( uint16_t )compare;
    return true;
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
