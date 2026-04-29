/******************************************************************************
 *  File:       console_uart.h
 *  Author:     Callum Rafferty
 *  Created:    29-Apr-2026
 *
 *  Description:
 *      <Short description of the module, what it exposes, and how it should be used>
 *
 *  Notes:
 *      <Public assumptions, required initialisation order, dependencies, etc.>
 ******************************************************************************/

#ifndef CONSOLE_UART_H
#define CONSOLE_UART_H

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
void CONSOLE_UART_Command_Handler( uint16_t argc, char* argv[] );

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

#ifdef __cplusplus
}
#endif

#endif /* CONSOLE_UART_H */
