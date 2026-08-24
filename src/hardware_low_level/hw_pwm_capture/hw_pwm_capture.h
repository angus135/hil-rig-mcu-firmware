/******************************************************************************
 *  File:       hw_pwm_capture.h
 *  Author:     Callum Rafferty
 *  Created:    28-04-2026
 *
 *  Description:
 *      Hardware layer interface for PWM capture.
 *
 *      This module provides an abstraction over the underlying timer capture
 *      peripherals used to measure PWM signals. It exposes zero-copy access
 *      to raw timer capture registers and provides mechanisms to detect and
 *      consume new capture events.
 *
 *  Notes:
 *      Responsibilities:
 *      - Start and stop timer-based PWM capture
 *      - Map logical channels to timer capture registers (CCR)
 *      - Expose new capture availability via hardware flags
 *
 *      Non-Responsibilities:
 *      - Validating captured data
 *      - Interpreting duty cycle or frequency
 *      - Timestamping or result ownership
 *
 *      Usage:
 *      - Configure channels using HW_PWM_Capture_Configure_Channel()
 *      - Start capture with HW_PWM_Capture_Start_Channel()
 *      - Use HW_PWM_Capture_Peek_Result() to inspect new data
 *      - Use HW_PWM_Capture_Consume_Result() to clear capture flags
 *      - Stop capture with HW_PWM_Capture_Stop_Channel()
 *
 *      Assumptions:
 *      - Timer PWM input mode is configured via IOC
 *      - Execution layer consumes data before clearing flags
 *      - Hardware mapping is fixed at compile time
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
    bool               has_new_data;
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
 * @brief Configure or disable a PWM capture channel.
 *
 * When enabled, stops the timer, configures its period and prescaler, caches
 * the timer clock, and leaves the channel stopped and ready to start.
 *
 * When disabled, stops the timer and clears the channel's configured state and
 * cached timer clock.
 *
 * @param channel Logical PWM capture channel to configure.
 * @param is_enabled True to configure the channel; false to disable it.
 *
 * @return true if the requested state was applied.
 * @return false if the channel is invalid or configuration fails.
 */
bool HW_PWM_Capture_Configure_Channel( HwPWMCaptureChannel_T channel, bool is_enabled );

/**
 * @brief Start a configured PWM capture channel.
 *
 * Starts timer capture without repeating peripheral configuration.
 *
 * @param channel Logical PWM capture channel to start.
 *
 * @return true if capture was started.
 * @return false if the channel is invalid, unconfigured, or already started.
 */
bool HW_PWM_Capture_Start_Channel( HwPWMCaptureChannel_T channel );

/**
 * @brief Stop a running PWM capture channel.
 *
 * Stops timer capture while retaining peripheral configuration and the cached
 * timer clock so the channel can be restarted.
 *
 * @param channel Logical PWM capture channel to stop.
 *
 * @return true if capture was stopped.
 * @return false if the channel is invalid, unconfigured, or not started.
 */
bool HW_PWM_Capture_Stop_Channel( HwPWMCaptureChannel_T channel );

/**
 * @brief Peek the latest PWM capture result without consuming it.
 *
 * Checks the period capture flag for the selected channel. If a new complete
 * PWM measurement is available, returns direct pointers to the period and
 * high-time capture registers.
 *
 * If no new measurement is available, returns a zero-initialised result with
 * has_new_data set to false and pointer fields set to NULL.
 *
 * @note A new result is only available once per completed PWM period. For slow
 * signals, this function may return has_new_data = false for multiple execution
 * ticks between capture events. This is expected behaviour.
 *
 * @param channel Logical PWM capture channel to inspect.
 *
 * @return Zero-copy capture result descriptor.
 *
 * Contract:
 * The caller must ensure channel is valid and configured.
 */
HwPWMCaptureResult_T HW_PWM_Capture_Peek_Result( HwPWMCaptureChannel_T channel );

/**
 * @brief Consume the current PWM capture result.
 *
 * Clears the period capture flag for the selected channel. This marks the
 * current hardware capture result as consumed by the execution layer.
 *
 * @param channel Logical PWM capture channel to consume.
 *
 * Contract:
 * This must only be called after a successful peek where has_new_data is true.
 * Calling this without a corresponding peek may result in lost capture events.
 */
void HW_PWM_Capture_Consume_Result( HwPWMCaptureChannel_T channel );

/**
 * @brief Return the timer input clock frequency for a PWM capture channel.
 *
 * The value is cached at configure time. Returns 0 if the channel has not
 * been configured or is currently disabled.
 *
 * @param channel Logical PWM capture channel.
 * @return Timer clock in Hz, or 0 if unavailable.
 */
uint32_t HW_PWM_Capture_Get_Timer_Clock_Hz( HwPWMCaptureChannel_T channel );

#ifdef __cplusplus
}
#endif

#endif /* HW_PWM_CAPTURE_H */
