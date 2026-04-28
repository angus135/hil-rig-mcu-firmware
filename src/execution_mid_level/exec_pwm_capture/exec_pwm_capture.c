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
 *      - Detect new PWM capture events via hardware flags
 *      - Read period and high-time timer capture values
 *      - Consume hardware capture flags
 *      - Validate captured values for logical correctness
 *
 *      Non-Responsibilities:
 *      - Timer configuration or hardware register access (handled by hw layer)
 *      - Timestamping of results (handled by execution manager)
 *      - Conversion to physical units such as frequency or duty cycle
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
#include "exec_pwm_capture.h"

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */
#define EXEC_PWM_CAPTURE_CHANNEL_COUNT 2U

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
     * - channel must be valid and not already configured
     */

    return HW_PWM_Capture_Configure_Channel( channel, config );
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

    hw_result = HW_PWM_Capture_Peek_Result( channel );

    if ( !hw_result.has_new_data )
    {
        result->is_valid = false;
        return false;
    }

    /*
     * Read CCR values before clearing the capture flag to avoid losing
     * a new capture event that occurs between operations.
     */
    period_ticks = *( hw_result.period_ticks );
    high_ticks   = *( hw_result.high_ticks );

    HW_PWM_Capture_Consume_Result( channel );

    if ( !EXEC_PWM_Capture_Result_Is_Valid( period_ticks, high_ticks ) )
    {
        result->is_valid = false;
        return false;
    }

    result->period_ticks = period_ticks;
    result->high_ticks   = high_ticks;
    result->is_valid     = true;

    return true;
}
