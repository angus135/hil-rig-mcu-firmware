/******************************************************************************
 *  File:       hw_pwm_capture.c
 *  Author:     Callum Rafferty
 *  Created:    25-Mar-2026
 *
 *  Description:
 *      Hardware layer implementation for PWM capture.
 *
 *      This module configures the PWM capture analogue front end, manages the
 *      associated timer capture paths, and exposes zero-copy access to raw
 *      timer capture registers for the execution layer.
 *
 *  Notes:
 *      Responsibilities:
 *      - Apply PWM capture hardware mode selection
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
 *      - Timer capture is stopped before analogue mode changes
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
 * Safe analogue front-end mode applied when PWM capture is disabled.
 * The hardware has no true disabled mode, so disabled capture forces the
 * lowest-voltage input path while the timer capture path is stopped.
 */
#define HW_PWM_CAPTURE_DEFAULT_SAFE_MODE HW_PWM_CAPTURE_LV_3V3

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
 * and the latest software configuration for one logical PWM capture channel.
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

    uint32_t period_capture_flag;  // SR flag bit corresponding to a new period capture event for
                                   // this channel

    HwPWMCaptureConfig_T config;
    bool                 is_configured;
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
    },
    {
        .timer               = HW_PWM_CAPTURE_CH_2_INSTANCE,
        .timer_role          = PWM_CAPTURE_TIMER_CH2,
        .period_ccr          = &HW_PWM_CAPTURE_CH_2_INSTANCE->HW_PWM_CAPTURE_CH_2_PERIOD_CCR,
        .high_ccr            = &HW_PWM_CAPTURE_CH_2_INSTANCE->HW_PWM_CAPTURE_CH_2_HIGH_CCR,
        .period_capture_flag = HW_PWM_CAPTURE_CH_2_PERIOD_FLAG,
        .is_configured       = false,
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

/**
 * @brief Apply the static analogue front-end selection for a PWM capture mode.
 *
 * The selected mode controls the PWM_MODE[1:0] hardware selection bits, which
 * choose the active LV/HV input path and threshold. The actual GPIO or expander
 * writes are implemented here once the hardware control path is available.
 *
 * @param mode PWM capture hardware mode to apply.
 *
 * @return true if the mode is valid and was accepted.
 * @return false if the mode is invalid.
 */
static bool HW_PWM_Capture_Apply_Static_Hardware_Selection( HwPWMCaptureMode_T mode )
{
    switch ( mode )
    {
        case HW_PWM_CAPTURE_LV_3V3:
            // Configure hardware for 3.3V capture mode
            // SET PWM_MODE[1:0] to [0, 0] to select the 3.3V capture mode
            break;
        case HW_PWM_CAPTURE_LV_5V:
            // Configure hardware for 5V capture mode
            // SET PWM_MODE[1:0] to [0, 1] to select the 5V capture mode
            break;
        case HW_PWM_CAPTURE_HV_12V:
            // Configure hardware for 12V capture mode
            // SET PWM_MODE[1:0] to [1, 0] to select the 12V capture mode
            break;
        case HW_PWM_CAPTURE_HV_24V:
            // Configure hardware for 24V capture mode
            // SET PWM_MODE[1:0] to [1, 1] to select the 24V capture mode
            break;
        default:
            return false;  // Invalid mode
    }

    return true;
}

/**
 * @brief Validate a PWM capture channel configuration.
 *
 * Checks that the configuration pointer is valid and that the requested capture
 * mode is one of the supported hardware modes.
 *
 * @param config Pointer to configuration to validate.
 *
 * @return true if the configuration is valid.
 * @return false if the configuration is NULL or contains an invalid mode.
 */
static bool HW_PWM_Capture_Configuration_Is_Valid( const HwPWMCaptureConfig_T* config )
{
    if ( config == NULL )
    {
        return false;
    }

    switch ( config->mode )
    {
        case HW_PWM_CAPTURE_LV_3V3:
        case HW_PWM_CAPTURE_LV_5V:
        case HW_PWM_CAPTURE_HV_12V:
        case HW_PWM_CAPTURE_HV_24V:
            return true;

        default:
            return false;
    }
}
/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */

bool HW_PWM_Capture_Configure_Channel( HwPWMCaptureChannel_T       channel,
                                       const HwPWMCaptureConfig_T* config )
{
    HwPWMCaptureChannelContext_T* context;

    if ( channel >= PWM_CAPTURE_CHANNEL_COUNT )
    {
        return false;
    }

    if ( !HW_PWM_Capture_Configuration_Is_Valid( config ) )
    {
        return false;
    }

    context = &hw_pwm_capture_channels[channel];

    // Stop the timer to ensure safe configuration of hardware and to prevent unintended captures
    // during reconfiguration
    HW_TIMER_Stop_Timer( context->timer_role );

    if ( !config->is_enabled )
    {
        if ( !HW_PWM_Capture_Apply_Static_Hardware_Selection( HW_PWM_CAPTURE_DEFAULT_SAFE_MODE ) )
        {
            return false;
        }

        context->config        = *config;
        context->is_configured = true;
        return true;
    }

    if ( !HW_PWM_Capture_Apply_Static_Hardware_Selection( config->mode ) )
    {
        return false;
    }

    HW_TIMER_Configure_Timer( context->timer_role, HW_PWM_CAPTURE_TIMER_PSC,
                              HW_PWM_CAPTURE_TIMER_ARR );

    HW_TIMER_Start_Timer( context->timer_role );

    context->config        = *config;
    context->is_configured = true;

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
    context->timer->SR = ~( context->period_capture_flag );
}
