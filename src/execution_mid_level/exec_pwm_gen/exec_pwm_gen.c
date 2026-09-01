/******************************************************************************
 *  File:       exec_pwm_gen.c
 *  Author:     Tim Vogelsang
 *  Created:    25-Mar-2026
 *
 *  Description:
 *      Execution-layer PWM generation lifecycle and board-level voltage
 *      selection for the independent LV and HV output channels.
 *
 *  Notes:
 *      Direct waveform update functions remain intentionally lightweight for
 *      use in the execution path.
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include "exec_pwm_gen.h"
#include "hw_pwm_gen.h"
#include <stdint.h>
#include <stdbool.h>
#include "logic_expander.h"
#include <stddef.h>

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */

#define EXEC_PWM_GEN_CONTROL_EXPANDER LOGIC_EXPANDER_PWM_SPI
#define EXEC_PWM_GEN_CONTROL_PORT LOGIC_EXPANDER_PORT_A

#define EXEC_PWM_GEN_HV_12V_BIT 4U
#define EXEC_PWM_GEN_HV_24V_BIT 5U
#define EXEC_PWM_GEN_LV_VSEL_BIT 6U

/**-----------------------------------------------------------------------------
 *  Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

typedef enum
{
    EXEC_PWM_GEN_STATE_DISABLED = 0,
    EXEC_PWM_GEN_STATE_CONFIGURED,
    EXEC_PWM_GEN_STATE_STARTED
} ExecPwmGenLifecycleState_T;

typedef struct
{
    ExecPwmGenLifecycleState_T state;
    ExecPwmGenVoltageLevel_T   voltage_level;
} ExecPwmGenChannelState_T;

/**-----------------------------------------------------------------------------
 *  Public (global) and Extern Variables
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Private (static) Variables
 *------------------------------------------------------------------------------
 */

static ExecPwmGenChannelState_T exec_pwm_gen_channel_states[EXEC_PWM_GEN_CHANNEL_COUNT] = {
    {
        .state         = EXEC_PWM_GEN_STATE_DISABLED,
        .voltage_level = EXEC_PWM_GEN_VOLTAGE_3V3,
    },
    {
        .state         = EXEC_PWM_GEN_STATE_DISABLED,
        .voltage_level = EXEC_PWM_GEN_VOLTAGE_12V,
    },
};

static const HwPwmGenChannel_T exec_pwm_gen_hardware_channels[EXEC_PWM_GEN_CHANNEL_COUNT] = {
    HW_PWM_GEN_CHANNEL_LV,
    HW_PWM_GEN_CHANNEL_HV,
};

/**-----------------------------------------------------------------------------
 *  Private (static) Function Prototypes
 *------------------------------------------------------------------------------
 */

static bool EXEC_PWM_GEN_Is_Valid_Channel( ExecPwmGenChannel_T channel );

static bool EXEC_PWM_GEN_Is_Valid_Voltage( ExecPwmGenChannel_T      channel,
                                           ExecPwmGenVoltageLevel_T voltage_level );

static bool EXEC_PWM_GEN_Apply_Voltage_Selection( ExecPwmGenChannel_T      channel,
                                                  ExecPwmGenVoltageLevel_T voltage_level );

static bool EXEC_PWM_GEN_Apply_Initial_State( ExecPwmGenChannel_T       channel,
                                              const ExecPwmGenConfig_T* config );

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

static bool EXEC_PWM_GEN_Is_Valid_Channel( ExecPwmGenChannel_T channel )
{
    return ( uint32_t )channel < ( uint32_t )EXEC_PWM_GEN_CHANNEL_COUNT;
}

static bool EXEC_PWM_GEN_Is_Valid_Voltage( ExecPwmGenChannel_T      channel,
                                           ExecPwmGenVoltageLevel_T voltage_level )
{
    switch ( channel )
    {
        case EXEC_PWM_GEN_CHANNEL_LV:
            return voltage_level == EXEC_PWM_GEN_VOLTAGE_3V3
                   || voltage_level == EXEC_PWM_GEN_VOLTAGE_5V;

        case EXEC_PWM_GEN_CHANNEL_HV:
            return voltage_level == EXEC_PWM_GEN_VOLTAGE_12V
                   || voltage_level == EXEC_PWM_GEN_VOLTAGE_24V
                   || voltage_level == EXEC_PWM_GEN_VOLTAGE_DISABLED;

        default:
            return false;
    }
}

static bool EXEC_PWM_GEN_Apply_Voltage_Selection( ExecPwmGenChannel_T      channel,
                                                  ExecPwmGenVoltageLevel_T voltage_level )
{
    if ( !EXEC_PWM_GEN_Is_Valid_Voltage( channel, voltage_level ) )
    {
        return false;
    }

    if ( channel == EXEC_PWM_GEN_CHANNEL_LV )
    {
        const bool select_5v = voltage_level == EXEC_PWM_GEN_VOLTAGE_5V;

        if ( LOGIC_EXPANDER_Load_Control_Bit( EXEC_PWM_GEN_CONTROL_EXPANDER,
                                              EXEC_PWM_GEN_CONTROL_PORT, EXEC_PWM_GEN_LV_VSEL_BIT,
                                              select_5v )
             != LOGIC_EXPANDER_STATUS_OK )
        {
            return false;
        }
    }
    else
    {
        const bool select_12v = voltage_level == EXEC_PWM_GEN_VOLTAGE_12V;
        const bool select_24v = voltage_level == EXEC_PWM_GEN_VOLTAGE_24V;

        if ( LOGIC_EXPANDER_Load_Control_Bit( EXEC_PWM_GEN_CONTROL_EXPANDER,
                                              EXEC_PWM_GEN_CONTROL_PORT, EXEC_PWM_GEN_HV_12V_BIT,
                                              select_12v )
             != LOGIC_EXPANDER_STATUS_OK )
        {
            return false;
        }

        if ( LOGIC_EXPANDER_Load_Control_Bit( EXEC_PWM_GEN_CONTROL_EXPANDER,
                                              EXEC_PWM_GEN_CONTROL_PORT, EXEC_PWM_GEN_HV_24V_BIT,
                                              select_24v )
             != LOGIC_EXPANDER_STATUS_OK )
        {
            return false;
        }
    }

    /*
     * TODO: Remove this direct send when the global configuration operation
     * commits all staged Logic Expander changes.
     */
    return LOGIC_EXPANDER_Send_Control_Bits() == LOGIC_EXPANDER_STATUS_OK;
}

static bool EXEC_PWM_GEN_Apply_Initial_State( ExecPwmGenChannel_T       channel,
                                              const ExecPwmGenConfig_T* config )
{
    if ( config == NULL )
    {
        return false;
    }

    /*
     * CCR may equal ARR + 1 to represent 100% duty cycle.
     */
    if ( ( uint32_t )config->initial_ccr > ( ( uint32_t )config->initial_arr + 1U ) )
    {
        return false;
    }

    switch ( channel )
    {
        case EXEC_PWM_GEN_CHANNEL_LV:
            HW_PWM_GEN_Set_PWM1_Direct( config->initial_arr, config->initial_ccr,
                                        config->initial_psc );
            break;

        case EXEC_PWM_GEN_CHANNEL_HV:
            HW_PWM_GEN_Set_PWM2_Direct( config->initial_arr, config->initial_ccr,
                                        config->initial_psc );
            break;

        default:
            return false;
    }

    return true;
}

/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */
bool EXEC_PWM_GEN_Configure_Channel( ExecPwmGenChannel_T channel, const ExecPwmGenConfig_T* config )
{
    if ( !EXEC_PWM_GEN_Is_Valid_Channel( channel ) || config == NULL )
    {
        return false;
    }

    ExecPwmGenChannelState_T* state = &exec_pwm_gen_channel_states[channel];

    if ( !config->is_enabled )
    {
        if ( state->state == EXEC_PWM_GEN_STATE_STARTED )
        {
            if ( !EXEC_PWM_GEN_Stop_Channel( channel ) )
            {
                return false;
            }
        }

        const ExecPwmGenVoltageLevel_T safe_voltage = channel == EXEC_PWM_GEN_CHANNEL_LV
                                                          ? EXEC_PWM_GEN_VOLTAGE_3V3
                                                          : EXEC_PWM_GEN_VOLTAGE_DISABLED;

        /* A failed disable must not leave a stopped channel startable. */
        state->state = EXEC_PWM_GEN_STATE_DISABLED;

        if ( !EXEC_PWM_GEN_Apply_Voltage_Selection( channel, safe_voltage ) )
        {
            return false;
        }

        state->state         = EXEC_PWM_GEN_STATE_DISABLED;
        state->voltage_level = safe_voltage;

        return true;
    }

    if ( state->state == EXEC_PWM_GEN_STATE_STARTED
         || !EXEC_PWM_GEN_Is_Valid_Voltage( channel, config->voltage_level )
         || ( uint32_t )config->initial_ccr > ( ( uint32_t )config->initial_arr + 1U ) )
    {
        return false;
    }

    const HwPwmGenChannel_T hardware_channel = exec_pwm_gen_hardware_channels[channel];

    /*
     * A valid reconfiguration may partially change the timer or voltage
     * selection. Revoke the previous start permission until all steps succeed.
     */
    state->state = EXEC_PWM_GEN_STATE_DISABLED;

    if ( !HW_PWM_GEN_Configure_Channel( hardware_channel ) )
    {
        return false;
    }

    if ( !EXEC_PWM_GEN_Apply_Initial_State( channel, config ) )
    {
        return false;
    }

    if ( !EXEC_PWM_GEN_Apply_Voltage_Selection( channel, config->voltage_level ) )
    {
        return false;
    }

    state->state         = EXEC_PWM_GEN_STATE_CONFIGURED;
    state->voltage_level = config->voltage_level;

    return true;
}

bool EXEC_PWM_GEN_Start_Channel( ExecPwmGenChannel_T channel )
{
    if ( !EXEC_PWM_GEN_Is_Valid_Channel( channel ) )
    {
        return false;
    }

    ExecPwmGenChannelState_T* state = &exec_pwm_gen_channel_states[channel];

    if ( state->state != EXEC_PWM_GEN_STATE_CONFIGURED )
    {
        return false;
    }

    const HwPwmGenChannel_T hardware_channel = exec_pwm_gen_hardware_channels[channel];

    if ( !HW_PWM_GEN_Start_Channel( hardware_channel ) )
    {
        return false;
    }

    state->state = EXEC_PWM_GEN_STATE_STARTED;

    return true;
}

bool EXEC_PWM_GEN_Stop_Channel( ExecPwmGenChannel_T channel )
{
    if ( !EXEC_PWM_GEN_Is_Valid_Channel( channel ) )
    {
        return false;
    }

    ExecPwmGenChannelState_T* state = &exec_pwm_gen_channel_states[channel];

    if ( state->state != EXEC_PWM_GEN_STATE_STARTED )
    {
        return false;
    }

    const HwPwmGenChannel_T hardware_channel = exec_pwm_gen_hardware_channels[channel];

    if ( !HW_PWM_GEN_Stop_Channel( hardware_channel ) )
    {
        return false;
    }

    state->state = EXEC_PWM_GEN_STATE_CONFIGURED;

    return true;
}

bool EXEC_PWM_GEN_Is_Configured( ExecPwmGenChannel_T channel )
{
    if ( !EXEC_PWM_GEN_Is_Valid_Channel( channel ) )
    {
        return false;
    }

    const ExecPwmGenLifecycleState_T state = exec_pwm_gen_channel_states[channel].state;

    return state == EXEC_PWM_GEN_STATE_CONFIGURED || state == EXEC_PWM_GEN_STATE_STARTED;
}

bool EXEC_PWM_GEN_Is_Started( ExecPwmGenChannel_T channel )
{
    if ( !EXEC_PWM_GEN_Is_Valid_Channel( channel ) )
    {
        return false;
    }

    return exec_pwm_gen_channel_states[channel].state == EXEC_PWM_GEN_STATE_STARTED;
}

/**
 * @brief Updates the PWM registers associated with channel 1.
 *
 * @param arr   the value of the auto reloader register (ARR) associated with this PWM signal
 * @param ccr the value of the compare register (CCR) associated with this PWM signal
 *
 * This function sets the values of the PWM channel 1 registers
 * To calculate the required values functions like HW_PWM_GEN_compute_arr should be used
 * This function is designed to be very fast and should be implemented in the execution phase
 */
void EXEC_PWM_GEN_Set_PWM_LV( uint16_t arr, uint16_t ccr, uint16_t psc )
{
    HW_PWM_GEN_Set_PWM1_Direct( arr, ccr, psc );
}

/**
 * @brief Updates the PWM registers associated with channel 2.
 *
 * @param arr   the value of the auto reloader register (ARR) associated with this PWM signal
 * @param ccr the value of the compare register (CCR) associated with this PWM signal
 *
 * This function sets the values of the PWM channel 2 registers
 * To calculate the required values functions like HW_PWM_GEN_compute_arr should be used
 * This function is designed to be very fast and should be implemented in the execution phase
 */
void EXEC_PWM_GEN_Set_PWM_HV( uint16_t arr, uint16_t ccr, uint16_t psc )
{
    HW_PWM_GEN_Set_PWM2_Direct( arr, ccr, psc );
}
