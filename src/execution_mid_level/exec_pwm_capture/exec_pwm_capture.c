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
#include "logic_expander.h"

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */
#define EXEC_PWM_CAPTURE_CHANNEL_COUNT 2U

#define EXEC_PWM_CAPTURE_DEFAULT_MODE EXEC_PWM_CAPTURE_LV_3V3

/**-----------------------------------------------------------------------------
 *  Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */
typedef struct ExecPwmCaptureHardwareMap_T
{
    LogicExpanderIndex_T  expander;
    LogicExpanderPort_T   port;
    uint8_t               mode_0_bit_i;
    uint8_t               mode_1_bit_i;
    HwPWMCaptureChannel_T hw_channel;
} ExecPwmCaptureHardwareMap_T;

/**-----------------------------------------------------------------------------
 *  Public (global) and Extern Variables
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Private (static) Variables
 *------------------------------------------------------------------------------
 */
static const ExecPwmCaptureHardwareMap_T
    exec_pwm_capture_hardware_map[EXEC_PWM_CAPTURE_CHANNEL_COUNT] = {
        {
            .expander     = LOGIC_EXPANDER_DEVICE_PWM_SPI,  // expander
            .port         = LOGIC_EXPANDER_PORT_A,          // port
            .mode_0_bit_i = 0U,                             // mode_0_bit
            .mode_1_bit_i = 1U,                             // mode_1_bit
            .hw_channel   = HW_PWM_CAPTURE_CHANNEL_1        // hardware channel
        },
        {
            .expander     = LOGIC_EXPANDER_DEVICE_PWM_SPI,  // expander
            .port         = LOGIC_EXPANDER_PORT_A,          // port
            .mode_0_bit_i = 2U,                             // mode_0_bit
            .mode_1_bit_i = 3U,                             // mode_1_bit
            .hw_channel   = HW_PWM_CAPTURE_CHANNEL_2        // hardware channel
        },
};

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
 * @brief Apply the static analogue front-end selection for a PWM capture mode.
 *
 * The selected mode controls the PWM_MODE[1:0] hardware selection bits, which
 * choose the active LV/HV input path and threshold. The actual GPIO or expander
 * writes are implemented here once the hardware control path is available.
 *
 * @param mode PWM capture hardware mode to apply.
 *
 * @return true if the mode is valid and was accepted.
 * @return false if the mode is invalid.
 */
static bool EXEC_PWM_Capture_Apply_Static_Hardware_Selection( ExecPwmCaptureMode_T    mode,
                                                              ExecPwmCaptureChannel_T channel )
{

    bool mode_0;
    bool mode_1;

    if ( channel >= EXEC_PWM_CAPTURE_CHANNEL_COUNT )
    {
        return false;
    }

    switch ( mode )
    {
        case EXEC_PWM_CAPTURE_LV_3V3:
            // Configure hardware for 3.3V capture mode
            // SET PWM_MODE[1:0] to [0, 0] to select the 3.3V capture mode
            mode_0 = false;
            mode_1 = false;
            break;
        case EXEC_PWM_CAPTURE_LV_5V:
            // Configure hardware for 5V capture mode
            // SET PWM_MODE[1:0] to [0, 1] to select the 5V capture mode
            mode_0 = false;
            mode_1 = true;
            break;
        case EXEC_PWM_CAPTURE_HV_12V:
            // Configure hardware for 12V capture mode
            // SET PWM_MODE[1:0] to [1, 0] to select the 12V capture mode
            mode_0 = true;
            mode_1 = false;
            break;
        case EXEC_PWM_CAPTURE_HV_24V:
            // Configure hardware for 24V capture mode
            // SET PWM_MODE[1:0] to [1, 1] to select the 24V capture mode
            mode_0 = true;
            mode_1 = true;
            break;
        default:
            return false;  // Invalid mode
    }

    const ExecPwmCaptureHardwareMap_T* hw_map = &exec_pwm_capture_hardware_map[channel];

    if ( LOGIC_EXPANDER_Load_Control_Bit( hw_map->expander, hw_map->port, hw_map->mode_0_bit_i,
                                          mode_0 )
         != LOGIC_EXPANDER_STATUS_OK )
    {
        return false;
    }
    if ( LOGIC_EXPANDER_Load_Control_Bit( hw_map->expander, hw_map->port, hw_map->mode_1_bit_i,
                                          mode_1 )
         != LOGIC_EXPANDER_STATUS_OK )
    {
        return false;
    }

    // Remove this and return true when global expander update is implemented
    return LOGIC_EXPANDER_Send_Control_Bits() == LOGIC_EXPANDER_STATUS_OK;
}

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

bool EXEC_PWM_Capture_Start_Channel( ExecPwmCaptureChannel_T       channel,
                                     const ExecPwmCaptureConfig_T* config )
{
    if ( config == NULL )
    {
        return false;
    }

    if ( channel >= EXEC_PWM_CAPTURE_CHANNEL_COUNT )
    {
        return false;
    }

    if ( !config->is_enabled )
    {
        return false;
    }

    if ( exec_pwm_capture_channel_started[channel] )
    {
        return false;
    }

    const ExecPwmCaptureHardwareMap_T* hardware = &exec_pwm_capture_hardware_map[channel];

    if ( !EXEC_PWM_Capture_Apply_Static_Hardware_Selection( config->mode, channel ) )
    {
        return false;
    }

    if ( !HW_PWM_Capture_Configure_Channel( hardware->hw_channel, true ) )
    {
        return false;
    }

    exec_pwm_capture_channel_started[channel] = true;

    return true;
}

bool EXEC_PWM_Capture_Stop_Channel( ExecPwmCaptureChannel_T channel )
{
    ExecPwmCaptureConfig_T config = { 0 };

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

    if ( !HW_PWM_Capture_Configure_Channel( exec_pwm_capture_hardware_map[channel].hw_channel,
                                            config.is_enabled ) )
    {
        return false;
    }

    exec_pwm_capture_channel_started[channel] = false;

    return EXEC_PWM_Capture_Apply_Static_Hardware_Selection( EXEC_PWM_CAPTURE_DEFAULT_MODE,
                                                             channel );
}

bool EXEC_PWM_Capture_Consume( ExecPwmCaptureChannel_T channel, ExecPwmCaptureResult_T* result )
{
    HwPWMCaptureResult_T hw_result    = { 0 };
    uint32_t             period_ticks = 0U;
    uint32_t             high_ticks   = 0U;

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

    hw_result = HW_PWM_Capture_Peek_Result( exec_pwm_capture_hardware_map[channel].hw_channel );

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

    HW_PWM_Capture_Consume_Result( exec_pwm_capture_hardware_map[channel].hw_channel );
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

bool EXEC_PWM_Capture_Convert( ExecPwmCaptureChannel_T channel, const ExecPwmCaptureResult_T* raw,
                               ExecPwmCapturePhysical_T* out )
{
    uint32_t clock_hz = 0U;

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

    clock_hz =
        HW_PWM_Capture_Get_Timer_Clock_Hz( exec_pwm_capture_hardware_map[channel].hw_channel );

    if ( clock_hz == 0U )
    {
        return false;
    }

    out->frequency_hz = clock_hz / raw->period_ticks;
    out->duty_cycle_bp =
        ( uint32_t )( ( ( uint64_t )raw->high_ticks * 10000U ) / raw->period_ticks );

    return true;
}
