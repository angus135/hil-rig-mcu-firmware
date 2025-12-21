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

/**-----------------------------------------------------------------------------
 *  Public Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

/**
 * @brief Console FreeRTOS task entry point.
 *
 * This task is responsible for initialising the console subsystem and
 * periodically polling the console UART for incoming data. All command
 * parsing and dispatch occurs within this task context.
 *
 * @param task_parameters  Unused task parameter (reserved for future use).
 *
 * @returns void
 */
void CONSOLE_Task(void* task_parameters);

/**
 * @brief Handles the parsed arguments retrieved from the console
 *
 * @param argc - The number of arguments
 * @param argv - pointer to each argument string
 *
 * @returns void
 */
void CONSOLE_Command_Handler(uint16_t argc, char* argv[]);

#ifdef __cplusplus
}
#endif

#endif /* CONSOLE_H */
