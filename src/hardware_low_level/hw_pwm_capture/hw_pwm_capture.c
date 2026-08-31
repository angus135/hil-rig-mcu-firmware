/******************************************************************************
 *  File:       hw_pwm_capture.c
 *  Author:     Callum Rafferty
 *  Created:    25-Mar-2026
 *
 *  Description:
 *      Hardware layer implementation for PWM capture.
 *
 *      This module manages the timer capture paths and exposes zero-copy
 *      access to raw timer capture registers for the execution layer.
 *
 *  Notes:
 *      Responsibilities:
 *      - Start and stop the associated timer capture path
 *      - Map logical PWM capture channels to timer CCR registers
 *      - Expose new capture availability through hardware capture flags
 *
 *      Non-Responsibilities:
 *      - Interpreting captured values
 *      - Validating duty cycle or frequency measurements
 *      - Timestamping captured data
 *      - Packaging results for transfer
 *
 *      Assumptions:
 *      - Timer PWM input mode is configured in the IOC
 *      - The execution layer consumes capture results before clearing flags
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#ifndef TEST_BUILD
#include "stm32f4xx.h"
#else
#include "tests/hw_pwm_capture_mocks.h"
#endif

#include "hw_pwm_capture.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "hw_timer.h"

// Add other required includes here

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */

/* Number of logical PWM capture channels supported by this driver. */
#define PWM_CAPTURE_CHANNEL_COUNT 2

/*
 * PWM capture channel 1 timer mapping.
 *
 * IOC configuration:
 * - TIM2 PWM input mode on CH1
 * - CCR1 stores measured period
 * - CCR2 stores measured high time
 */
#define HW_PWM_CAPTURE_CH_1_INSTANCE TIM2
#define HW_PWM_CAPTURE_CH_1_PERIOD_CCR CCR1
#define HW_PWM_CAPTURE_CH_1_HIGH_CCR CCR2
#define HW_PWM_CAPTURE_CH_1_PERIOD_FLAG TIM_SR_CC1IF

/*
 * PWM capture channel 2 timer mapping.
 *
 * IOC configuration:
 * - TIM5 PWM input mode on CH2
 * - CCR2 stores measured period
 * - CCR1 stores measured high time
 */
#define HW_PWM_CAPTURE_CH_2_INSTANCE TIM5
#define HW_PWM_CAPTURE_CH_2_PERIOD_CCR CCR2
#define HW_PWM_CAPTURE_CH_2_HIGH_CCR CCR1
#define HW_PWM_CAPTURE_CH_2_PERIOD_FLAG TIM_SR_CC2IF

/*
 * PWM capture timers run at full timer resolution.
 * PSC = 0 gives maximum timestamp precision.
 * ARR = 0xFFFFFFFF uses the full 32-bit counter range.
 */
#define HW_PWM_CAPTURE_TIMER_PSC 0U
#define HW_PWM_CAPTURE_TIMER_ARR 0xFFFFFFFFU

/**-----------------------------------------------------------------------------
 *  Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

/**
 * @brief Per-channel PWM capture hardware and software context.
 *
 * Stores the timer binding, CCR register mapping, timer role used by hw_timer,
 * and the timer clock for one logical PWM capture channel.
 *
 * period_ccr and high_ccr point directly to timer capture registers so the
 * execution path can read captured values without copying.
 */
typedef struct
{
    TIM_TypeDef*       timer;
    Timer_T            timer_role;
    volatile uint32_t* period_ccr;
    volatile uint32_t* high_ccr;

    uint32_t period_capture_flag;
    uint32_t timer_clock_hz;

    bool is_configured;
    bool is_started;
} HwPWMCaptureChannelContext_T;

/**-----------------------------------------------------------------------------
 *  Public (global) and Extern Variables
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Private (static) Variables
 *------------------------------------------------------------------------------
 */

/*
 * Static context table for all PWM capture channels.
 *
 * This table is the single source of truth for mapping logical PWM capture
 * channels to timer peripherals, hw_timer roles, and CCR register meanings.
 */
static HwPWMCaptureChannelContext_T hw_pwm_capture_channels[PWM_CAPTURE_CHANNEL_COUNT] = {
    {
        .timer               = HW_PWM_CAPTURE_CH_1_INSTANCE,
        .timer_role          = PWM_CAPTURE_TIMER_CH1,
        .period_ccr          = &HW_PWM_CAPTURE_CH_1_INSTANCE->HW_PWM_CAPTURE_CH_1_PERIOD_CCR,
        .high_ccr            = &HW_PWM_CAPTURE_CH_1_INSTANCE->HW_PWM_CAPTURE_CH_1_HIGH_CCR,
        .period_capture_flag = HW_PWM_CAPTURE_CH_1_PERIOD_FLAG,
        .is_configured       = false,
        .is_started          = false,
    },
    {
        .timer               = HW_PWM_CAPTURE_CH_2_INSTANCE,
        .timer_role          = PWM_CAPTURE_TIMER_CH2,
        .period_ccr          = &HW_PWM_CAPTURE_CH_2_INSTANCE->HW_PWM_CAPTURE_CH_2_PERIOD_CCR,
        .high_ccr            = &HW_PWM_CAPTURE_CH_2_INSTANCE->HW_PWM_CAPTURE_CH_2_HIGH_CCR,
        .period_capture_flag = HW_PWM_CAPTURE_CH_2_PERIOD_FLAG,
        .is_configured       = false,
        .is_started          = false,
    },
};

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

bool HW_PWM_Capture_Configure_Channel( HwPWMCaptureChannel_T channel, bool is_enabled )
{
    if ( channel >= PWM_CAPTURE_CHANNEL_COUNT )
    {
        return false;
    }

    HwPWMCaptureChannelContext_T* context = &hw_pwm_capture_channels[channel];

    /*
     * Always stop before configuring or disabling the timer.
     */
    HW_TIMER_Stop_Timer( context->timer_role );
    context->is_started = false;

    if ( !is_enabled )
    {
        context->timer_clock_hz = 0U;
        context->is_configured  = false;
        return true;
    }

    HW_TIMER_Configure_Timer( context->timer_role, HW_PWM_CAPTURE_TIMER_PSC,
                              HW_PWM_CAPTURE_TIMER_ARR );

    context->timer_clock_hz = HW_TIMER_Get_Clock_Hz( context->timer_role );

    context->is_configured = true;

    return true;
}

bool HW_PWM_Capture_Start_Channel( HwPWMCaptureChannel_T channel )
{
    if ( channel >= PWM_CAPTURE_CHANNEL_COUNT )
    {
        return false;
    }

    HwPWMCaptureChannelContext_T* context = &hw_pwm_capture_channels[channel];

    if ( !context->is_configured || context->is_started )
    {
        return false;
    }

    if ( !HW_TIMER_Start_Timer( context->timer_role ) )
    {
        return false;
    }
    context->is_started = true;

    return true;
}

bool HW_PWM_Capture_Stop_Channel( HwPWMCaptureChannel_T channel )
{
    if ( channel >= PWM_CAPTURE_CHANNEL_COUNT )
    {
        return false;
    }

    HwPWMCaptureChannelContext_T* context = &hw_pwm_capture_channels[channel];

    if ( !context->is_configured || !context->is_started )
    {
        return false;
    }

    HW_TIMER_Stop_Timer( context->timer_role );
    context->is_started = false;

    return true;
}

HwPWMCaptureResult_T HW_PWM_Capture_Peek_Result( HwPWMCaptureChannel_T channel )
{
    HwPWMCaptureChannelContext_T* context = &hw_pwm_capture_channels[channel];
    HwPWMCaptureResult_T          result  = { 0 };

    /*
     * The period capture flag indicates a new complete PWM measurement.
     * Direct SR access is used to keep the implementation table-driven, since
     * the period flag (CC1 or CC2) depends on the IOC configuration.
     */
    if ( ( context->timer->SR & context->period_capture_flag ) == 0U )
    {
        return result;
    }

    result.has_new_data = true;
    result.period_ticks = context->period_ccr;
    result.high_ticks   = context->high_ccr;

    return result;
}

void HW_PWM_Capture_Consume_Result( HwPWMCaptureChannel_T channel )
{
    HwPWMCaptureChannelContext_T* context = &hw_pwm_capture_channels[channel];

    /*
     * The period capture flag indicates a new complete PWM measurement.
     * Direct SR access is used to keep the implementation table-driven, since
     * the period flag (CC1 or CC2) depends on the IOC configuration.
     *
     * TIM status flags are cleared by writing 0 to the target flag bit.
     * Avoid read-modify-write here because hardware may set another flag between
     * the read and write, which could cause an event to be lost.
     */

    /*
     * Clear only the period capture flag.
     * Writing 0 clears the target flag, writing 1 preserves all others.
     */
    context->timer->SR = ~( context->period_capture_flag );
}

uint32_t HW_PWM_Capture_Get_Timer_Clock_Hz( HwPWMCaptureChannel_T channel )
{
    if ( channel >= PWM_CAPTURE_CHANNEL_COUNT )
    {
        return 0U;
    }

    return hw_pwm_capture_channels[channel].timer_clock_hz;
}
