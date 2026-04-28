/******************************************************************************
 *  File:       exec_pwm_capture.h
 *  Author:     Callum Rafferty
 *  Created:    28-Apr-2026
 *
 *  Description:
 *      Execution layer driver for PWM capture.
 *
 *      This module provides an interface for consuming validated PWM capture
 *      measurements from the hardware PWM capture layer. It reads raw timer
 *      capture values (period and high time), performs minimal validation,
 *      and exposes the results to the execution manager.
 *
 *      The module is designed for deterministic execution and operates on a
 *      zero-copy interface from the hardware layer.
 *
 *  Notes:
 *      Responsibilities:
 *      - Detect availability of new PWM capture data
 *      - Read raw timer capture values (period and high time)
 *      - Consume hardware capture flags
 *      - Perform minimal validation of captured data
 *
 *      Non-Responsibilities:
 *      - Timer configuration or hardware register access (handled by hw layer)
 *      - Timestamping of results (handled by execution manager)
 *      - Conversion to frequency or duty cycle units (handled by higher layers)
 *
 *      Usage:
 *      - Call EXEC_PWM_Capture_Start_Channel() during configuration
 *      - Call EXEC_PWM_Capture_Consume() during execution to retrieve new data
 *      - Call EXEC_PWM_Capture_Stop_Channel() to disable capture when no longer needed
 *
 *      Assumptions:
 *      - Channels are configured before use
 *      - The execution manager ensures valid calling context
 *      - Result validity must be checked before use
 ******************************************************************************/

#ifndef EXEC_PWM_CAPTURE_H
#define EXEC_PWM_CAPTURE_H

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
#include "hw_pwm_capture.h"

/**-----------------------------------------------------------------------------
 *  Public Defines / Macros
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Public Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

/**
 * @brief PWM capture result consumed by the execution layer.
 *
 * Stores the raw timer capture values for one PWM measurement.
 * Duty cycle is represented by high_ticks / period_ticks and should be
 * converted by higher layers only when needed.
 */
typedef struct
{
    bool     is_valid;
    uint32_t period_ticks;
    uint32_t high_ticks;
} ExecPwmCaptureResult_T;

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

/**
 * @brief Consume one newly captured PWM measurement.
 *
 * Peeks the hardware capture result for the selected channel. If a new
 * measurement is available, copies the period and high-time tick values into
 * result, consumes the hardware capture flag, validates the measurement, and
 * marks result as valid.
 *
 * If no new measurement is available, or if the captured values are invalid,
 * result->is_valid is set to false and false is returned.
 *
 * @param channel Logical PWM capture channel to consume.
 * @param result Pointer to output result storage.
 *
 * @return true if a new valid measurement was consumed.
 * @return false if no new measurement was available or the measurement was invalid.
 *
 * Contract:
 * - channel must be valid
 * - result must not be NULL
 * - channel must already be started
 *
 * These preconditions are not checked at runtime to minimise execution-path
 * overhead. Violating them is caller error.
 */
bool EXEC_PWM_Capture_Consume( HwPWMCaptureChannel_T channel, ExecPwmCaptureResult_T* result );

/**
 * @brief Start a PWM capture channel.
 *
 * Validates the requested configuration and starts the specified channel.
 * The configuration must be enabled and the channel must not already be started.
 *
 * Delegates hardware configuration to the hardware PWM capture layer.
 *
 * @param channel Logical PWM capture channel to start.
 * @param config Pointer to requested hardware capture configuration.
 *
 * @return true if the channel was started successfully.
 * @return false if:
 *         - the channel is invalid
 *         - config is NULL
 *         - config->is_enabled is false
 *         - the channel is already started
 *         - the hardware layer rejected the configuration
 */
bool EXEC_PWM_Capture_Start_Channel( HwPWMCaptureChannel_T       channel,
                                     const HwPWMCaptureConfig_T* config );

/**
 * @brief Stop PWM capture on the specified channel.
 *
 * Applies a disabled configuration to the hardware layer, stopping the
 * underlying timer capture and returning the channel to a safe default state.
 * Updates execution layer state to reflect that the channel is no longer active.
 *
 * Behaviour:
 * - Stops capture only if the channel is currently started
 * - Delegates hardware state change to the hardware layer
 * - Clears internal execution-layer started state on success
 *
 * @param channel Logical PWM capture channel to stop.
 *
 * @return true if the channel was successfully stopped.
 * @return false if:
 *         - the channel is invalid
 *         - the channel was not previously started
 *         - the hardware layer rejected the configuration request
 *
 * Contract:
 * The caller must ensure channel is valid within system context.
 */
bool EXEC_PWM_Capture_Stop_Channel( HwPWMCaptureChannel_T channel );

#ifdef TEST_BUILD
/**
 * @brief Reset internal execution-layer state for unit testing.
 *
 * Clears all channel started flags to ensure test isolation.
 * This function is only available in test builds.
 */
void EXEC_PWM_Capture_Test_Reset( void );
#endif

#ifdef __cplusplus
}
#endif

#endif /* EXEC_PWM_CAPTURE_H */
