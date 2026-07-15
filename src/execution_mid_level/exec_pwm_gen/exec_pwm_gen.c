/******************************************************************************
 *  File:       exec_pwm_gen.c
 *  Author:     Tim Vogelsang
 *  Created:    25-Mar-2026
 *
 *  Description:
 *      <Short description of the module's purpose and responsibilities>
 *
 *  Notes:
 *      <Any design notes, dependencies, or assumptions go here>
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include "exec_pwm_gen.h"
#include "hw_pwm_gen.h"
#include <stdint.h>
#include <stdbool.h>

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */

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

/**-----------------------------------------------------------------------------
 *  Public Function Definitions
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

/**
 * @brief Configures the pwm output.
 *
 * @param channel   The channel you want to configure
 * @param volt_lvl  The voltage level you want
 *
 */
void Exec_PWM_GEN_Config( PwmGenChannel_T channel, PwmGenVoltageLevel_T volt_lvl )
{
    HW_PWM_GEN_Config( channel, volt_lvl );
}
