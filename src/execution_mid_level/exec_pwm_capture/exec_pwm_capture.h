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
 *      - Convert validated tick values to frequency and duty cycle
 *
 *      Non-Responsibilities:
 *      - Timer configuration or hardware register access (handled by hw layer)
 *      - Timestamping of results (handled by execution manager)
 *
 *      Usage:
 *      - Call EXEC_PWM_Capture_Configure_Channel() during configuration
 *      - Call EXEC_PWM_Capture_Start_Channel() to begin capture
 *      - Call EXEC_PWM_Capture_Consume() during execution to retrieve new data
 *      - Call EXEC_PWM_Capture_Stop_Channel() to pause capture while retaining configuration
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
    bool     has_new_data;
    bool     is_valid;
    uint32_t period_ticks;
    uint32_t high_ticks;
} ExecPwmCaptureResult_T;

/**
 * @brief Physical PWM measurement converted from raw timer ticks.
 *
 * frequency_hz is the PWM signal frequency in Hz.
 * duty_cycle_bp is the duty cycle in basis points (0–10000, where 10000 = 100%).
 */
typedef struct
{
    uint32_t frequency_hz;
    uint32_t duty_cycle_bp;  // basis points, 0–10000 (1bp = 0.01%)
} ExecPwmCapturePhysical_T;

typedef enum
{
    EXEC_PWM_CAPTURE_LV_3V3 = 0U,
    EXEC_PWM_CAPTURE_LV_5V,
    EXEC_PWM_CAPTURE_HV_12V,
    EXEC_PWM_CAPTURE_HV_24V,
} ExecPwmCaptureMode_T;

typedef enum
{
    EXEC_PWM_CAPTURE_CHANNEL_1 = 0U,
    EXEC_PWM_CAPTURE_CHANNEL_2,
} ExecPwmCaptureChannel_T;

typedef struct ExecPwmCaptureConfig_T
{
    ExecPwmCaptureMode_T mode;
    bool                 is_enabled;
} ExecPwmCaptureConfig_T;
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
bool EXEC_PWM_Capture_Consume( ExecPwmCaptureChannel_T channel, ExecPwmCaptureResult_T* result );

/**
 * @brief Configure or disable a PWM capture channel.
 *
 * Enabled configuration applies the requested analogue frontend mode and
 * configures the mapped hardware channel without starting capture. Disabled
 * configuration stops and disables the hardware channel, then applies the
 * safe frontend mode. The mode field is ignored when disabled.
 *
 * @param channel Logical PWM capture channel to configure.
 * @param config Requested mode and enabled state.
 *
 * @return true if the requested configuration was applied.
 * @return false for invalid arguments, enabled reconfiguration while started,
 *         or a rejected LogicExpander or hardware operation.
 */
bool EXEC_PWM_Capture_Configure_Channel( ExecPwmCaptureChannel_T       channel,
                                         const ExecPwmCaptureConfig_T* config );

/**
 * @brief Start a PWM capture channel.
 *
 * Starts timer capture for a channel previously configured and enabled.
 *
 * @param channel Logical PWM capture channel to start.
 * @return true if the channel was started successfully.
 * @return false if:
 *         - the channel is invalid
 *         - the channel is not configured or is already started
 *         - the hardware layer rejected the start request
 */
bool EXEC_PWM_Capture_Start_Channel( ExecPwmCaptureChannel_T channel );

/**
 * @brief Stop PWM capture on the specified channel.
 *
 * Stops timer capture while retaining the channel's peripheral configuration
 * and analogue frontend mode so it can be started again without reconfiguration.
 *
 * Behaviour:
 * - Stops capture only if the channel is currently started
 * - Delegates the stop operation to the hardware layer
 * - Returns the channel to configured state on success
 *
 * @param channel Logical PWM capture channel to stop.
 *
 * @return true if the channel was successfully stopped.
 * @return false if:
 *         - the channel is invalid
 *         - the channel was not previously started
 *         - the hardware layer rejected the stop request
 *
 * Contract:
 * The caller must ensure channel is valid within system context.
 */
bool EXEC_PWM_Capture_Stop_Channel( ExecPwmCaptureChannel_T channel );

/**
 * @brief Convert a validated PWM capture result to physical units.
 *
 * Converts raw timer tick values into frequency and duty cycle using the
 * timer clock frequency cached by the hardware layer at configuration time.
 *
 * @param channel Logical PWM capture channel the result was consumed from.
 * @param raw     Pointer to a validated ExecPwmCaptureResult_T.
 * @param out     Pointer to output physical measurement storage.
 *
 * @return true if conversion succeeded.
 * @return false if:
 *         - raw or out is NULL
 *         - raw->is_valid is false
 *         - the hardware layer reports no clock (channel disabled or unconfigured)
 */
bool EXEC_PWM_Capture_Convert( ExecPwmCaptureChannel_T channel, const ExecPwmCaptureResult_T* raw,
                               ExecPwmCapturePhysical_T* out );

#ifdef __cplusplus
}
#endif

#endif /* EXEC_PWM_CAPTURE_H */
