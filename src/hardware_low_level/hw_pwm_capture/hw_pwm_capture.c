/******************************************************************************
 *  File:       hw_pwm_capture.c
 *  Author:     Callum Rafferty
 *  Created:    25-Mar-2026
 *
 *  Description:
 *      <Short description of the module's purpose and responsibilities>
 *
 *  Notes:
 *      <Any design notes, dependencies, or assumptions go here>
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#ifdef TEST_BUILD
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

// Number of PWM capture channels supported by the hardware
#define PWM_CAPTURE_CHANNEL_COUNT 2

// Default safe mode for capture when disabled
#define HW_PWM_CAPTURE_DEFAULT_SAFE_MODE HW_PWM_CAPTURE_LV_3V3

/* Timer Hardware Map Definitions*/

#define HW_PWM_CAPTURE_CH_1_INSTANCE TIM2
#define HW_PWM_CAPTURE_CH_1_PERIOD_CCR CCR1
#define HW_PWM_CAPTURE_CH_1_HIGH_CCR CCR2

#define HW_PWM_CAPTURE_CH_2_INSTANCE TIM5
#define HW_PWM_CAPTURE_CH_2_PERIOD_CCR CCR2
#define HW_PWM_CAPTURE_CH_2_HIGH_CCR CCR1

/* Timer Settings*/
#define HW_PWM_CAPTURE_TIMER_PSC 0U
#define HW_PWM_CAPTURE_TIMER_ARR 0xFFFFFFFFU
/**-----------------------------------------------------------------------------
 *  Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

typedef struct
{
    TIM_TypeDef*       timer;
    Timer_T            timer_role;
    volatile uint32_t* period_ccr;
    volatile uint32_t* high_ccr;

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

static HwPWMCaptureChannelContext_T hw_pwm_capture_channels[PWM_CAPTURE_CHANNEL_COUNT] = {
    {
        .timer         = HW_PWM_CAPTURE_CH_1_INSTANCE,
        .timer_role    = PWM_CAPTURE_TIMER_CH1,
        .period_ccr    = &HW_PWM_CAPTURE_CH_1_INSTANCE->HW_PWM_CAPTURE_CH_1_PERIOD_CCR,
        .high_ccr      = &HW_PWM_CAPTURE_CH_1_INSTANCE->HW_PWM_CAPTURE_CH_1_HIGH_CCR,
        .is_configured = false,
    },
    {
        .timer         = HW_PWM_CAPTURE_CH_2_INSTANCE,
        .timer_role    = PWM_CAPTURE_TIMER_CH2,
        .period_ccr    = &HW_PWM_CAPTURE_CH_2_INSTANCE->HW_PWM_CAPTURE_CH_2_PERIOD_CCR,
        .high_ccr      = &HW_PWM_CAPTURE_CH_2_INSTANCE->HW_PWM_CAPTURE_CH_2_HIGH_CCR,
        .is_configured = false,
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

/*
 * There is no hardware disabled mode.
 * When capture is disabled, the analogue front end is forced to LV 3.3 V.
 * Timer capture must be disabled separately.
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

void HW_PWM_Capture_Get_Result( HwPWMCaptureChannel_T channel, HwPWMCaptureResult_T* result )
{
    /*
     * Contract:
     * - channel must be valid
     * - result must not be NULL
     * - channel must already be configured and enabled
     *
     * This function intentionally performs no runtime validation because it is
     * expected to be called from the deterministic execution path.
     */

    HwPWMCaptureChannelContext_T* context = &hw_pwm_capture_channels[channel];

    result->period_ticks = context->period_ccr;
    result->high_ticks   = context->high_ccr;
}
