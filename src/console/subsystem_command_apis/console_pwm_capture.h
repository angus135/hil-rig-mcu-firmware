/******************************************************************************
 *  File:       console_pwm_capture.h
 *  Author:     Callum Rafferty
 *  Created:    28-Apr-2026
 *
 *  Description:
 *      Public interface for the PWM Capture subsystem Console APIs.
 *
 *  Notes:
 *      None
 ******************************************************************************/

#ifndef CONSOLE_PWM_CAPTURE_H
#define CONSOLE_PWM_CAPTURE_H

#ifdef __cplusplus
extern "C"
{
#endif
/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include <stdint.h>

/**-----------------------------------------------------------------------------
 *  Public Defines / Macros
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Public Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

/**
 * @brief Console command handler for PWM capture subsystem.
 *
 * Provides a user interface to configure and read PWM capture channels.
 *
 * Supported commands:
 * - pwm_capture start <ch1|ch2> <3v3|5v|12v|24v>
 *      Configures and enables PWM capture on the selected channel.
 *
 * - pwm_capture stop <ch1|ch2>
 *      Disables PWM capture on the selected channel.
 *
 * - pwm_capture read <ch1|ch2>
 *      Attempts to read a new PWM capture result from the selected channel.
 *
 * Behaviour:
 * - Uses the execution layer API to configure and consume capture results.
 * - Outputs raw timer values (period_ticks and high_ticks).
 * - Does not perform unit conversion (frequency, duty cycle).
 *
 * @param argc Number of command arguments.
 * @param argv Array of argument strings.
 */
void CONSOLE_PWM_Capture_Command( uint16_t argc, char* argv[] );

#ifdef __cplusplus
}
#endif

#endif /* CONSOLE_PWM_CAPTURE_H */
