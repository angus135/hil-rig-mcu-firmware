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
 */
bool EXEC_PWM_Capture_Consume( HwPWMCaptureChannel_T channel, ExecPwmCaptureResult_T* result );

/**
 * @brief Start or configure a PWM capture channel.
 *
 * Passes the requested channel configuration to the hardware PWM capture layer.
 * This applies the analogue front-end mode and starts or stops the underlying
 * timer capture path according to config->is_enabled.
 *
 * @param channel Logical PWM capture channel to configure.
 * @param config Pointer to requested hardware capture configuration.
 *
 * @return true if the channel was configured successfully.
 * @return false if the channel or configuration was invalid.
 */
bool EXEC_PWM_Capture_Start_Channel( HwPWMCaptureChannel_T       channel,
                                     const HwPWMCaptureConfig_T* config );

#ifdef __cplusplus
}
#endif

#endif /* EXEC_PWM_CAPTURE_H */
