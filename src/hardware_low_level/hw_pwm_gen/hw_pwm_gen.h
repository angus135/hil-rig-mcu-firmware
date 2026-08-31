/******************************************************************************
 *  File:       hw_pwm_gen.h
 *  Author:     Callum Rafferty
 *  Created:    25-Mar-2026
 *
 *  Description:
 *      Low-level PWM generation interface. Provides strict per-channel
 *      configure/start/stop lifecycle control, timer-value calculations, and
 *      direct execution-time register updates.
 *
 *  Notes:
 *      Timer and GPIO base initialisation is supplied by generated board code.
 *      Board-level voltage selection belongs to the execution layer.
 ******************************************************************************/

#ifndef HW_PWM_GEN_H
#define HW_PWM_GEN_H

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

/**-----------------------------------------------------------------------------
 *  Public Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

typedef enum
{
    HW_PWM_GEN_CHANNEL_LV,
    HW_PWM_GEN_CHANNEL_HV,
    HW_PWM_GEN_CHANNEL_COUNT
} HwPwmGenChannel_T;

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

/**
 * @brief Configures a PWM generation channel without starting its output.
 *
 * @param channel PWM generation channel to configure.
 *
 * @return true if the channel was configured successfully; otherwise false.
 */
bool HW_PWM_GEN_Configure_Channel( HwPwmGenChannel_T channel );

/**
 * @brief Starts a previously configured PWM generation channel.
 *
 * @param channel PWM generation channel to start.
 * @return true if the channel was started successfully; otherwise false.
 */
bool HW_PWM_GEN_Start_Channel( HwPwmGenChannel_T channel );

/**
 * @brief Stops a started PWM channel while retaining its configuration.
 *
 * @param channel PWM generation channel to stop.
 * @return true if the channel was stopped successfully; otherwise false.
 */
bool HW_PWM_GEN_Stop_Channel( HwPwmGenChannel_T channel );

/**
 * @brief Prepare PSC for a frequency in 1..1000000 Hz, not above timer_clk_hz.
 *
 * Chooses the smallest divider giving at most 65535 period counts, so the
 * resulting ARR + 1 also fits the 16-bit CCR at 100% duty.
 * @param psc Output register value.
 * @return false for invalid inputs or NULL output; output is unchanged.
 */
bool HW_PWM_GEN_compute_psc( uint32_t freq_hz, uint32_t timer_clk_hz, uint16_t* psc );

/**
 * @brief Prepare ARR using floor(timer_clk_hz / (freq_hz * (prescaler + 1))) - 1.
 *
 * Accepts frequencies in 1..1000000 Hz and periods of 1..65536 counts.
 * Integer rounding may produce a frequency above the requested value.
 * @param arr Output register value.
 * @return false for invalid inputs, unrepresentable period or NULL output;
 *         output is unchanged.
 */
bool HW_PWM_GEN_compute_arr( uint32_t freq_hz, uint32_t timer_clk_hz,
                               uint16_t prescaler, uint16_t* arr );

/**
 * @brief Prepare CCR for duty in permille (0..1000), rounding down.
 *
 * ARR=65535 with 100% duty requires CCR=65536 and is rejected by the 16-bit API.
 * Use the PSC/ARR preparation sequence to leave room for a full-duty CCR.
 * @param ccr Output register value.
 * @return false for invalid duty, unrepresentable CCR or NULL output;
 *         output is unchanged.
 */
bool HW_PWM_GEN_compute_ccr( uint16_t duty_pm, uint16_t arr, uint16_t* ccr );

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
void HW_PWM_GEN_Set_PWM1_Direct( uint16_t arr, uint16_t ccr, uint16_t psc );

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
void HW_PWM_GEN_Set_PWM2_Direct( uint16_t arr, uint16_t ccr, uint16_t psc );

#ifdef __cplusplus
}
#endif

#endif /* HW_PWM_GEN_H */
