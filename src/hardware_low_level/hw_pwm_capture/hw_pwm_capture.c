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

#include "hw_pwm_capture.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
// Add other required includes here

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */

// Number of PWM capture channels supported by the hardware
#define PWM_CAPTURE_CHANNEL_COUNT 2

// Default safe mode for capture when disabled
#define HW_PWM_CAPTURE_DEFAULT_SAFE_MODE HW_PWM_CAPTURE_LV_3V3

/**-----------------------------------------------------------------------------
 *  Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

typedef enum
{
    HW_PWM_CAPTURE_LV_3V3 = 0u,  // Capture voltage level at 3.3V (Hardware state)
    HW_PWM_CAPTURE_LV_5V,        // Capture voltage level at 5V (Hardware state)
    HW_PWM_CAPTURE_HV_12V,       // Capture voltage level at 12V (Hardware state)
    HW_PWM_CAPTURE_HV_24V        // Capture voltage level at 24V (Hardware state)
} HwPWMCaptureMode_T;

typedef enum
{
    HW_PWM_CAPTURE_CHANNEL_1 = 0u,  // First PWM capture channel
    HW_PWM_CAPTURE_CHANNEL_2        // Second PWM capture channel
} HwPWMCaptureChannel_T;

typedef struct
{
    HwPWMCaptureMode_T mode;        // Desired capture mode (voltage level)
    bool               is_enabled;  // Flag to indicate if capture is enabled for this channel
} HwPWMCaptureConfig_T;

typedef struct
{
    uint32_t             timer_base;     // Base address of the timer used for capture
    HwPWMCaptureConfig_T config;         // Current configuration of the capture channel
    bool                 is_configured;  // Flag to indicate if the capture module is initialized
} HwPWMCaptureState_T;

/**-----------------------------------------------------------------------------
 *  Public (global) and Extern Variables
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Private (static) Variables
 *------------------------------------------------------------------------------
 */

static HwPWMCaptureState_T hw_pwm_capture_states[PWM_CAPTURE_CHANNEL_COUNT];

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
            // SET MODE0[0:1] to [0, 0] to select the 3.3V capture mode
            break;
        case HW_PWM_CAPTURE_LV_5V:
            // Configure hardware for 5V capture mode
            // SET MODE0[0:1] to [0, 1] to select the 5V capture mode
            break;
        case HW_PWM_CAPTURE_HV_12V:
            // Configure hardware for 12V capture mode
            // SET MODE0[0:1] to [1, 0] to select the 12V capture mode
            break;
        case HW_PWM_CAPTURE_HV_24V:
            // Configure hardware for 24V capture mode
            // SET MODE0[0:1] to [1, 1] to select the 24V capture mode
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

    HwPWMCaptureMode_T mode_to_apply = HW_PWM_CAPTURE_DEFAULT_SAFE_MODE;

    if ( channel >= PWM_CAPTURE_CHANNEL_COUNT )
    {
        return false;
    }

    if ( !HW_PWM_Capture_Configuration_Is_Valid( config ) )
    {
        return false;
    }

    if ( config->is_enabled )
    {
        mode_to_apply = config->mode;
    }

    if ( !HW_PWM_Capture_Apply_Static_Hardware_Selection( mode_to_apply ) )
    {
        return false;
    }

    hw_pwm_capture_states[channel].config        = *config;
    hw_pwm_capture_states[channel].is_configured = true;

    return true;
}