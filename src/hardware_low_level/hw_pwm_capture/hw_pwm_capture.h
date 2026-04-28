/******************************************************************************
 *  File:       hw_pwm_capture.h
 *  Author:     Callum Rafferty
 *  Created:    25-Mar-2026
 *
 *  Description:
 *      <Short description of the module, what it exposes, and how it should be used>
 *
 *  Notes:
 *      <Public assumptions, required initialisation order, dependencies, etc.>
 ******************************************************************************/

#ifndef HW_PWM_CAPTURE_H
#define HW_PWM_CAPTURE_H

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

typedef enum
{
    HW_PWM_CAPTURE_LV_3V3 = 0U,  // Capture voltage level at 3.3V (Hardware state)
    HW_PWM_CAPTURE_LV_5V,        // Capture voltage level at 5V (Hardware state)
    HW_PWM_CAPTURE_HV_12V,       // Capture voltage level at 12V (Hardware state)
    HW_PWM_CAPTURE_HV_24V        // Capture voltage level at 24V (Hardware state)
} HwPWMCaptureMode_T;

typedef enum
{
    HW_PWM_CAPTURE_CHANNEL_1 = 0U,  // First PWM capture channel
    HW_PWM_CAPTURE_CHANNEL_2        // Second PWM capture channel
} HwPWMCaptureChannel_T;

typedef struct
{
    HwPWMCaptureMode_T mode;        // Desired capture mode (voltage level)
    bool               is_enabled;  // Flag to indicate if capture is enabled for this channel
} HwPWMCaptureConfig_T;

typedef struct
{
    volatile uint32_t* period_ticks;
    volatile uint32_t* high_ticks;
} HwPWMCaptureResult_T;

/**-----------------------------------------------------------------------------
 *  Public Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

bool HW_PWM_Capture_Configure_Channel( HwPWMCaptureChannel_T       channel,
                                       const HwPWMCaptureConfig_T* config );

void HW_PWM_Capture_Get_Result( HwPWMCaptureChannel_T channel, HwPWMCaptureResult_T* result );

#ifdef __cplusplus
}
#endif

#endif /* <FILENAME>_H */
