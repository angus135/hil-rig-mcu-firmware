/******************************************************************************
 *  File:       console_run_state_manager.h
 *  Author:     Callum Rafferty
 *  Created:    26-Aug-2026
 *
 *  Description:
 *      Console command interface for manual Run State Manager control.
 *
 *  Notes:
 *      Commands submit requests through the Run State Manager public API and
 *      never perform state transitions or subsystem actions directly.
 ******************************************************************************/

#ifndef CONSOLE_RUN_STATE_MANAGER_H
#define CONSOLE_RUN_STATE_MANAGER_H

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
 * @brief Handles manual Run State Manager console commands.
 *
 * @param argc Number of command arguments.
 * @param argv Command argument array.
 */
void CONSOLE_RunStateManager_Command( uint16_t argc, char* argv[] );

#ifdef __cplusplus
}
#endif

#endif /* CONSOLE_RUN_STATE_MANAGER_H */
