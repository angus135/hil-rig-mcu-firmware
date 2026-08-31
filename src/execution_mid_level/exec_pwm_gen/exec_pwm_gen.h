/******************************************************************************
 *  File:       exec_pwm_gen.h
 *  Author:     Angus Corr
 *  Created:    25-Mar-2026
 *
 *  Description:
 *      Public execution-layer interface for independently configuring,
 *      starting, stopping, and updating the LV and HV PWM outputs.
 *
 *  Notes:
 *      Configure preloads the initial waveform state so a successful channel
 *      is ready to Start. Direct update functions perform no lifecycle checks.
 ******************************************************************************/

#ifndef EXEC_PWM_GEN_H
#define EXEC_PWM_GEN_H

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

/**-----------------------------------------------------------------------------
 *  Public Defines / Macros
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Public Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

typedef enum
{
    EXEC_PWM_GEN_CHANNEL_LV = 0,
    EXEC_PWM_GEN_CHANNEL_HV,
    EXEC_PWM_GEN_CHANNEL_COUNT
} ExecPwmGenChannel_T;

typedef enum
{
    EXEC_PWM_GEN_VOLTAGE_DISABLED = 0,
    EXEC_PWM_GEN_VOLTAGE_3V3,
    EXEC_PWM_GEN_VOLTAGE_5V,
    EXEC_PWM_GEN_VOLTAGE_12V,
    EXEC_PWM_GEN_VOLTAGE_24V,
    EXEC_PWM_GEN_VOLTAGE_COUNT
} ExecPwmGenVoltageLevel_T;

typedef struct
{
    bool                     is_enabled;
    ExecPwmGenVoltageLevel_T voltage_level;
    uint16_t                 initial_arr;
    uint16_t                 initial_ccr;  // 100% duty requires ARR + 1 <= UINT16_MAX
    uint16_t                 initial_psc;
} ExecPwmGenConfig_T;

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

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
void EXEC_PWM_GEN_Set_PWM_LV( uint16_t arr, uint16_t ccr, uint16_t psc );

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
void EXEC_PWM_GEN_Set_PWM_HV( uint16_t arr, uint16_t ccr, uint16_t psc );

/**
 * @brief Configures or disables one independent PWM generation channel.
 *
 * Enabled configuration validates and preloads the initial ARR, CCR, and PSC,
 * selects a channel-compatible voltage, and leaves the timer output stopped
 * and ready to start. Disabled configuration stops an active channel and
 * selects its lowest supported voltage.
 */
bool EXEC_PWM_GEN_Configure_Channel( ExecPwmGenChannel_T       channel,
                                     const ExecPwmGenConfig_T* config );

/** @brief Starts a configured PWM generation channel. */
bool EXEC_PWM_GEN_Start_Channel( ExecPwmGenChannel_T channel );

/** @brief Stops a started channel while retaining its configuration. */
bool EXEC_PWM_GEN_Stop_Channel( ExecPwmGenChannel_T channel );

/** @brief Returns true when a channel is configured, including while started. */
bool EXEC_PWM_GEN_Is_Configured( ExecPwmGenChannel_T channel );

/** @brief Returns true only while the selected channel is started. */
bool EXEC_PWM_GEN_Is_Started( ExecPwmGenChannel_T channel );
#ifdef __cplusplus
}
#endif

#endif /* EXEC_PWM_GEN_H */
