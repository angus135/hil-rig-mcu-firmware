/******************************************************************************
 *  File:       console_run_state_manager.c
 *  Author:     Callum Rafferty
 *  Created:    26-Aug-2026
 *
 *  Description:
 *      Console command interface for manual Run State Manager control.
 *
 *  Notes:
 *      State requests are asynchronous. An accepted request means it was
 *      delivered to the Run State Manager task, not that processing completed.
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */
#include "console_run_state_manager.h"
#include "console.h"
#include "run_state_manager.h"
#include <stdbool.h>
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
static const char* CONSOLE_RunStateManager_StateName( RunState_T state );
static void        CONSOLE_RunStateManager_PrintUsage( void );
static void        CONSOLE_RunStateManager_PrintRequestResult( bool accepted );

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

/**
 * @brief Converts a Run State Manager state to console text.
 *
 * @param state State to convert.
 *
 * @returns Constant state-name string.
 */
static const char* CONSOLE_RunStateManager_StateName( RunState_T state )
{
    switch ( state )
    {
        case RUN_STATE_IDLE:
            return "IDLE";
        case RUN_STATE_TEST_PACKAGE_RECEIVE:
            return "TEST_PACKAGE_RECEIVE";
        case RUN_STATE_CONFIGURATION:
            return "CONFIGURATION";
        case RUN_STATE_EXECUTION:
            return "EXECUTION";
        case RUN_STATE_RESULT_TRANSFER:
            return "RESULT_TRANSFER";
        case RUN_STATE_FAULT:
            return "FAULT";
        default:
            return "UNKNOWN";
    }
}

/**
 * @brief Prints the Run State Manager command usage.
 */
static void CONSOLE_RunStateManager_PrintUsage( void )
{
    CONSOLE_Printf( "Usage:\r\n" );
    CONSOLE_Printf( "  run_state <status|step|fault|reset>\r\n" );
    CONSOLE_Printf( "  run_state timer <start|stop>\r\n" );
}

/**
 * @brief Prints whether an asynchronous state request was accepted.
 *
 * @param accepted Whether the request was delivered to the task.
 */
static void CONSOLE_RunStateManager_PrintRequestResult( bool accepted )
{
    if ( accepted )
    {
        CONSOLE_Printf( "Run state request accepted.\r\n" );
    }
    else
    {
        CONSOLE_Printf( "Run state request rejected: task is not ready.\r\n" );
    }
}

/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */

void CONSOLE_RunStateManager_Command( uint16_t argc, char* argv[] )
{
    if ( argc == 3U && strcmp( argv[1], "timer" ) == 0 )
    {
        if ( strcmp( argv[2], "start" ) == 0 )
        {
            CONSOLE_RunStateManager_PrintRequestResult(
                RUN_STATE_MANAGER_RequestExecutionTimerStart() );
        }
        else if ( strcmp( argv[2], "stop" ) == 0 )
        {
            CONSOLE_RunStateManager_PrintRequestResult(
                RUN_STATE_MANAGER_RequestExecutionTimerStop() );
        }
        else
        {
            CONSOLE_RunStateManager_PrintUsage();
        }
        return;
    }

    if ( argc != 2U )
    {
        CONSOLE_RunStateManager_PrintUsage();
        return;
    }

    if ( strcmp( argv[1], "status" ) == 0 )
    {
        CONSOLE_Printf(
            "Run state: %s%s\r\n",
            CONSOLE_RunStateManager_StateName( RUN_STATE_MANAGER_GetState() ),
            RUN_STATE_MANAGER_IsTransitionPending() ? " (transition pending)" : "" );
    }
    else if ( strcmp( argv[1], "step" ) == 0 )
    {
        CONSOLE_RunStateManager_PrintRequestResult( RUN_STATE_MANAGER_RequestStep() );
    }
    else if ( strcmp( argv[1], "fault" ) == 0 )
    {
        CONSOLE_RunStateManager_PrintRequestResult( RUN_STATE_MANAGER_RequestFault() );
    }
    else if ( strcmp( argv[1], "reset" ) == 0 )
    {
        CONSOLE_RunStateManager_PrintRequestResult( RUN_STATE_MANAGER_RequestReset() );
    }
    else
    {
        CONSOLE_RunStateManager_PrintUsage();
    }
}
