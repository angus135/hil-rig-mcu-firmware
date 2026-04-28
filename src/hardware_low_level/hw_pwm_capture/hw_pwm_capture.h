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

/**
 * @brief PWM capture analogue front-end mode.
 *
 * Selects the hardware input path and threshold used to interpret the incoming
 * PWM signal. This configures the analogue front end prior to timer capture.
 *
 * These modes correspond directly to hardware voltage domains supported by the
 * input conditioning circuitry.
 */
typedef enum
{
    HW_PWM_CAPTURE_LV_3V3 = 0U,  // Capture voltage level at 3.3V (Hardware state)
    HW_PWM_CAPTURE_LV_5V,        // Capture voltage level at 5V (Hardware state)
    HW_PWM_CAPTURE_HV_12V,       // Capture voltage level at 12V (Hardware state)
    HW_PWM_CAPTURE_HV_24V        // Capture voltage level at 24V (Hardware state)
} HwPWMCaptureMode_T;

/**
 * @brief Logical PWM capture channel identifier.
 *
 * Represents a software-defined PWM capture channel. Each channel is mapped
 * to a specific timer instance and input pin in the hardware configuration.
 *
 * This does not correspond to TIM_CHANNEL_x or CCRx directly.
 */
typedef enum
{
    HW_PWM_CAPTURE_CHANNEL_1 = 0U,  // First PWM capture channel
    HW_PWM_CAPTURE_CHANNEL_2        // Second PWM capture channel
} HwPWMCaptureChannel_T;

/**
 * @brief Configuration for a PWM capture channel.
 *
 * Defines the desired analogue front-end mode and whether the channel is
 * actively capturing.
 *
 * When disabled, the timer capture is stopped and the analogue front end
 * is forced to a safe default mode.
 */
typedef struct
{
    HwPWMCaptureMode_T mode;        // Desired capture mode (voltage level)
    bool               is_enabled;  // Flag to indicate if capture is enabled for this channel
} HwPWMCaptureConfig_T;

/**
 * @brief Zero-copy PWM capture result.
 *
 * Provides direct pointers to the timer capture registers containing the
 * most recent period and high-time measurements.
 *
 * These pointers reference hardware registers (CCR) and must be dereferenced
 * by the caller to obtain the latest captured values.
 *
 * No validation or copying is performed to preserve deterministic execution.
 */
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

/**
 * @brief Configure a PWM capture channel.
 *
 * Applies the requested PWM capture front-end mode and starts or stops the
 * associated timer capture path.
 *
 * If the channel is enabled, the requested mode is applied, the capture timer
 * is configured, and input capture is started.
 *
 * If the channel is disabled, the capture timer is stopped and the analogue
 * front end is forced to the default safe mode.
 *
 * @param channel Logical PWM capture channel to configure.
 * @param config Pointer to the requested channel configuration.
 *
 * @return true if the configuration was accepted and applied.
 * @return false if the channel or configuration is invalid.
 */
bool HW_PWM_Capture_Configure_Channel( HwPWMCaptureChannel_T       channel,
                                       const HwPWMCaptureConfig_T* config );

/**
 * @brief Get zero-copy access to the latest PWM capture timer registers.
 *
 * Provides direct pointers to the timer capture registers containing the
 * captured period and high-time tick counts.
 *
 * Contract:
 * The caller must ensure that channel is valid, result is not NULL, and the
 * channel has already been configured and enabled.
 *
 * This function intentionally performs no runtime validation so it can be used
 * in the deterministic execution path.
 *
 * @param channel Logical PWM capture channel to read.
 * @param result Output structure populated with period and high-time register pointers.
 */
void HW_PWM_Capture_Get_Result( HwPWMCaptureChannel_T channel, HwPWMCaptureResult_T* result );

#ifdef __cplusplus
}
#endif

#endif /* HW_PWM_CAPTURE_H */
