/******************************************************************************
 *  File:       exec_pwm_capture.c
 *  Author:     Callum Rafferty
 *  Created:    28-Apr-2026
 *
 *  Description:
 *      Execution layer implementation for PWM capture.
 *
 *      This module consumes raw PWM capture data from the hardware PWM
 *      capture layer and produces validated measurements for use by the
 *      execution manager. It operates on a zero-copy interface, reading
 *      timer capture registers directly and performing minimal processing
 *      to maintain deterministic execution timing.
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
 *      Design Considerations:
 *      - Execution path is kept minimal to meet real-time constraints
 *      - Zero-copy access avoids unnecessary memory operations
 *      - Validation is limited to essential checks only
 *
 *      Assumptions:
 *      - Channels are configured and enabled prior to use
 *      - Caller provides a valid result pointer
 *      - Hardware layer guarantees coherent capture semantics
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "exec_pwm_capture.h"

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */
#define EXEC_PWM_CAPTURE_CHANNEL_COUNT 2U

#define EXEC_PWM_CAPTURE_DEFAULT_MODE HW_PWM_CAPTURE_LV_3V3

/**-----------------------------------------------------------------------------
 *  Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Public (global) and Extern Variables
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Private (static) Variables
 *------------------------------------------------------------------------------
 */

static bool exec_pwm_capture_channel_started[EXEC_PWM_CAPTURE_CHANNEL_COUNT];

/**-----------------------------------------------------------------------------
 *  Private (static) Function Prototypes
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

/**
 * @brief Validate raw PWM capture values.
 *
 * Ensures the captured timer values represent a physically valid PWM signal.
 *
 * A valid measurement must satisfy:
 * - period_ticks > 0
 * - high_ticks <= period_ticks
 *
 * @param period_ticks Captured period in timer ticks.
 * @param high_ticks Captured high time in timer ticks.
 *
 * @return true if the values are valid.
 * @return false if the values are invalid.
 */
static inline bool EXEC_PWM_Capture_Result_Is_Valid( uint32_t period_ticks, uint32_t high_ticks )
{
    if ( period_ticks == 0U )
    {
        return false;
    }

    if ( high_ticks > period_ticks )
    {
        return false;
    }

    return true;
}

/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */

bool EXEC_PWM_Capture_Start_Channel( HwPWMCaptureChannel_T       channel,
                                     const HwPWMCaptureConfig_T* config )
{
    /*
     * Contract:
     * - channel must be valid
     * - config must not be NULL
     * - config->is_enabled must be true
     * - channel must not already be started
     */

    if ( config == NULL )
    {
        return false;
    }

    if ( channel >= EXEC_PWM_CAPTURE_CHANNEL_COUNT )
    {
        return false;
    }

    if ( config->is_enabled == false )
    {
        return false;
    }

    if ( exec_pwm_capture_channel_started[channel] )
    {
        return false;
    }

    if ( !HW_PWM_Capture_Configure_Channel( channel, config ) )
    {
        return false;
    }

    exec_pwm_capture_channel_started[channel] = true;

    return true;
}

bool EXEC_PWM_Capture_Stop_Channel( HwPWMCaptureChannel_T channel )
{
    HwPWMCaptureConfig_T config = {};

    if ( channel >= EXEC_PWM_CAPTURE_CHANNEL_COUNT )
    {
        return false;
    }

    if ( !exec_pwm_capture_channel_started[channel] )
    {
        return false;
    }

    config.mode       = EXEC_PWM_CAPTURE_DEFAULT_MODE;
    config.is_enabled = false;

    if ( !HW_PWM_Capture_Configure_Channel( channel, &config ) )
    {
        return false;
    }

    exec_pwm_capture_channel_started[channel] = false;

    return true;
}

bool EXEC_PWM_Capture_Consume( HwPWMCaptureChannel_T channel, ExecPwmCaptureResult_T* result )
{
    HwPWMCaptureResult_T hw_result;
    uint32_t             period_ticks;
    uint32_t             high_ticks;

    /*
     * Contract:
     * - channel must be valid
     * - result must not be NULL
     * - channel must already be configured and enabled
     *
     * Behaviour:
     * - returns true only when a new valid capture was consumed
     * - returns false if no new data or invalid capture
     */

    result->has_new_data = false;
    result->is_valid     = false;
    result->period_ticks = 0U;
    result->high_ticks   = 0U;

    hw_result = HW_PWM_Capture_Peek_Result( channel );

    if ( !hw_result.has_new_data )
    {
        return false;
    }

    /*
     * Read CCR values before clearing the capture flag to avoid losing
     * a new capture event that occurs between operations.
     */
    /*
     * Hardware contract:
     * If has_new_data is true, period_ticks and high_ticks point to valid
     * capture registers.
     */
    period_ticks = *( hw_result.period_ticks );
    high_ticks   = *( hw_result.high_ticks );

    HW_PWM_Capture_Consume_Result( channel );
    /*
     * A new capture event has been consumed at this point. Mark has_new_data true
     * before validation so callers can distinguish "no new data" from
     * "new data was captured but rejected as invalid".
     */
    result->has_new_data = true;

    if ( !EXEC_PWM_Capture_Result_Is_Valid( period_ticks, high_ticks ) )
    {
        return false;
    }

    result->period_ticks = period_ticks;
    result->high_ticks   = high_ticks;
    result->is_valid     = true;

    return true;
}

bool EXEC_PWM_Capture_Convert( HwPWMCaptureChannel_T channel, const ExecPwmCaptureResult_T* raw,
                               ExecPwmCapturePhysical_T* out )
{
    uint32_t clock_hz;

    if ( raw == NULL || out == NULL )
    {
        return false;
    }

    if ( !raw->is_valid )
    {
        return false;
    }

    if ( raw->period_ticks == 0U || raw->high_ticks > raw->period_ticks )
    {
        return false;
    }

    clock_hz = HW_PWM_Capture_Get_Timer_Clock_Hz( channel );

    if ( clock_hz == 0U )
    {
        return false;
    }

    out->frequency_hz = clock_hz / raw->period_ticks;
    out->duty_cycle_bp =
        ( uint32_t )( ( ( uint64_t )raw->high_ticks * 10000U ) / raw->period_ticks );

    return true;
}
