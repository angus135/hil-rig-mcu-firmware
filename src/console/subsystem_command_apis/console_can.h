/******************************************************************************
 *  File:       console_can.h
 *  Author:     tim vogelsang
 *  Created:    29-May-2026
 *
 *  Description:
 *      Public command handler for CAN console diagnostics.
 ******************************************************************************/

#ifndef CONSOLE_CAN_H
#define CONSOLE_CAN_H

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

/**
 * @brief Handles CAN diagnostic console commands.
 *
 * Supported command namespace:
 *   can tx <channel> <id> <payload> [<id> <payload> ...]
 *   can rx <channel>
 *   can config <channel> <filter_bank> <filter_id> <filter_mask>
 *   can start <channel>
 *   can stop <channel>
 *
 * @param argc Number of parsed command arguments.
 * @param argv Parsed command argument array.
 *
 * @returns void
 */
void CONSOLE_CAN_Command_Handler( uint16_t argc, char* argv[] );

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

#ifdef __cplusplus
}
#endif

#endif /* CONSOLE_CAN_H */
