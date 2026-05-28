/******************************************************************************
 *  File:       console_can.h
 *  Author:     tim vogelsang
 *  Created:    29-May-2026
 *
 *  Description:
 *      <Short description of the module, what it exposes, and how it should be used>
 *
 *  Notes:
 *      <Public assumptions, required initialisation order, dependencies, etc.>
 ******************************************************************************/

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
 * @brief Handles UART-related console commands.
 *
 * Supported command namespace:
 *   uart_loopback ...
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
