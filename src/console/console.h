/******************************************************************************
 *  File:       console.h
 *  Author:     Angus Corr
 *  Created:    6-Dec-2025
 *
 *  Description:
 *      Public interface for the Console module.
 *
 *  Notes:
 *      None
 ******************************************************************************/

#ifndef CONSOLE_H
#define CONSOLE_H

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
#include "rtos_config.h"

/**-----------------------------------------------------------------------------
 *  Public Defines / Macros
 *------------------------------------------------------------------------------
 */
#define CONSOLE_TASK_MEMORY 256
#define CONSOLE_TASK_PRIORITY 3

#define ARRAY_LEN( a )                                                                             \
    ( sizeof( a ) / sizeof( ( a )[0] ) )  // TODO: move this to a common helper file

/**-----------------------------------------------------------------------------
 *  Public Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

/**
 * @brief Format text and queue it for console transmission.
 *
 * Formats the provided message and appends the result to the console TX buffer.
 * Transmission occurs later from console task context.
 *
 * @param format  Standard printf-style format string.
 * @param ...     Variable arguments corresponding to the format string.
 *
 * @returns void
 */
void CONSOLE_Printf( const char* format, ... );

/**
 * @brief Console task entry point.
 *
 * This task initialises the console subsystem, drains buffered RX data from the
 * low-level console UART driver, processes command line input, and flushes
 * buffered console TX output from task context.
 *
 * @param task_parameters  Unused task parameter.
 *
 * @returns void
 */
void CONSOLE_Task( void* task_parameters );

/**
 * @brief Handles the parsed arguments retrieved from the console
 *
 * @param argc - The number of arguments
 * @param argv - pointer to each argument string
 *
 * @returns void
 */
void CONSOLE_Command_Handler( uint16_t argc, char* argv[] );

#ifdef __cplusplus
}
#endif

#endif /* CONSOLE_H */
