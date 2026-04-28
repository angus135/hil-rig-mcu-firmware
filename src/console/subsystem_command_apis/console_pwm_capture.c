/******************************************************************************
 *  File:       console_pwm_capture.c
 *  Author:     Callum Rafferty
 *  Created:    28-Apr-2026
 *
 *  Description:
 *      Console command implementation for the PWM capture subsystem.
 *
 *      Provides a user-facing interface to configure and read PWM capture
 *      channels via the development console. This module parses command
 *      arguments, validates inputs, and delegates functionality to the
 *      execution layer PWM capture driver.
 *
 *  Notes:
 *      Responsibilities:
 *      - Parse console command arguments
 *      - Validate channel and mode inputs
 *      - Invoke execution layer APIs for configuration and data capture
 *      - Format and print results to the console
 *
 *      Non-Responsibilities:
 *      - Generating PWM signals
 *      - Interpreting captured values into physical units
 *      - Managing timing, scheduling, or data ownership
 *
 *      Assumptions:
 *      - An external PWM signal is present on the selected input channel
 *      - Channels are correctly wired and hardware is initialised
 *      - Execution layer guarantees valid operation of capture APIs
 *****************************************************************************

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include <stdint.h>
#include <stdbool.h>
#include "exec_pwm_capture.h"
#include "console_pwm_capture.h"
#include "console.h"
#include <string.h>

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
static bool CONSOLE_PWM_Capture_Parse_Channel( const char* arg, HwPWMCaptureChannel_T* channel );
static bool CONSOLE_PWM_Capture_Parse_Mode( const char* arg, HwPWMCaptureMode_T* mode );

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

/**
 * @brief Parse console argument into PWM capture channel.
 *
 * Converts a string argument into the corresponding PWM capture channel enum.
 *
 * Supported values:
 * - "ch1"
 * - "ch2"
 *
 * @param arg Input argument string.
 * @param channel Output channel enum.
 *
 * @return true if parsing was successful.
 * @return false if the argument is invalid.
 */
static bool CONSOLE_PWM_Capture_Parse_Channel( const char* arg, HwPWMCaptureChannel_T* channel )
{
    if ( strcmp( arg, "ch1" ) == 0 )
    {
        *channel = HW_PWM_CAPTURE_CHANNEL_1;
        return true;
    }

    if ( strcmp( arg, "ch2" ) == 0 )
    {
        *channel = HW_PWM_CAPTURE_CHANNEL_2;
        return true;
    }

    return false;
}

/**
 * @brief Parse console argument into PWM capture mode.
 *
 * Converts a string argument into the corresponding analogue capture mode.
 *
 * Supported values:
 * - "3v3"
 * - "5v"
 * - "12v"
 * - "24v"
 *
 * @param arg Input argument string.
 * @param mode Output capture mode enum.
 *
 * @return true if parsing was successful.
 * @return false if the argument is invalid.
 */
static bool CONSOLE_PWM_Capture_Parse_Mode( const char* arg, HwPWMCaptureMode_T* mode )
{
    if ( strcmp( arg, "3v3" ) == 0 )
    {
        *mode = HW_PWM_CAPTURE_LV_3V3;
        return true;
    }

    if ( strcmp( arg, "5v" ) == 0 )
    {
        *mode = HW_PWM_CAPTURE_LV_5V;
        return true;
    }

    if ( strcmp( arg, "12v" ) == 0 )
    {
        *mode = HW_PWM_CAPTURE_HV_12V;
        return true;
    }

    if ( strcmp( arg, "24v" ) == 0 )
    {
        *mode = HW_PWM_CAPTURE_HV_24V;
        return true;
    }

    return false;
}

/**
 * @brief Print PWM capture command usage information.
 *
 * Outputs supported command formats and expected arguments to the console.
 *
 * @return void
 */
static void CONSOLE_PWM_Capture_Print_Usage( void )
{
    CONSOLE_Printf( "Usage:\r\n" );
    CONSOLE_Printf( "  pwm_capture start <ch1|ch2> <3v3|5v|12v|24v>\r\n" );
    CONSOLE_Printf( "  pwm_capture stop <ch1|ch2>\r\n" );
    CONSOLE_Printf( "  pwm_capture read <ch1|ch2>\r\n" );
}

/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */

void CONSOLE_PWM_Capture_Command( uint16_t argc, char* argv[] )
{
    HwPWMCaptureChannel_T channel;

    if ( argc < 2U )
    {
        CONSOLE_PWM_Capture_Print_Usage();
        return;
    }

    if ( strcmp( argv[1], "start" ) == 0 )
    {
        HwPWMCaptureMode_T   mode;
        HwPWMCaptureConfig_T config;

        if ( argc != 4U )
        {
            CONSOLE_PWM_Capture_Print_Usage();
            return;
        }

        if ( !CONSOLE_PWM_Capture_Parse_Channel( argv[2], &channel ) )
        {
            CONSOLE_Printf( "Invalid channel. Use ch1 or ch2\r\n" );
            return;
        }

        if ( !CONSOLE_PWM_Capture_Parse_Mode( argv[3], &mode ) )
        {
            CONSOLE_Printf( "Invalid mode. Use 3v3, 5v, 12v, or 24v\r\n" );
            return;
        }

        config.mode       = mode;
        config.is_enabled = true;

        if ( !EXEC_PWM_Capture_Start_Channel( channel, &config ) )
        {
            CONSOLE_Printf( "PWM capture start failed\r\n" );
            return;
        }

        CONSOLE_Printf( "PWM capture started\r\n" );
        return;
    }

    if ( strcmp( argv[1], "stop" ) == 0 )
    {
        HwPWMCaptureConfig_T config;

        if ( argc != 3U )
        {
            CONSOLE_PWM_Capture_Print_Usage();
            return;
        }

        if ( !CONSOLE_PWM_Capture_Parse_Channel( argv[2], &channel ) )
        {
            CONSOLE_Printf( "Invalid channel. Use ch1 or ch2\r\n" );
            return;
        }

        config.mode       = HW_PWM_CAPTURE_LV_3V3;
        config.is_enabled = false;

        if ( !EXEC_PWM_Capture_Start_Channel( channel, &config ) )
        {
            CONSOLE_Printf( "PWM capture stop failed\r\n" );
            return;
        }

        CONSOLE_Printf( "PWM capture stopped\r\n" );
        return;
    }

    if ( strcmp( argv[1], "read" ) == 0 )
    {
        ExecPwmCaptureResult_T result;

        if ( argc != 3U )
        {
            CONSOLE_PWM_Capture_Print_Usage();
            return;
        }

        if ( !CONSOLE_PWM_Capture_Parse_Channel( argv[2], &channel ) )
        {
            CONSOLE_Printf( "Invalid channel. Use ch1 or ch2\r\n" );
            return;
        }

        if ( !EXEC_PWM_Capture_Consume( channel, &result ) )
        {
            CONSOLE_Printf( "No new valid PWM capture result\r\n" );
            return;
        }

        CONSOLE_Printf( "PWM capture result:\r\n" );
        CONSOLE_Printf( "  period_ticks: %lu\r\n", ( unsigned long )result.period_ticks );
        CONSOLE_Printf( "  high_ticks:   %lu\r\n", ( unsigned long )result.high_ticks );
        return;
    }

    CONSOLE_Printf( "Unknown pwm_capture command\r\n" );
    CONSOLE_PWM_Capture_Print_Usage();
}
