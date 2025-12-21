/******************************************************************************
 *  File:       command_handler.c
 *  Author:     Angus Corr
 *  Created:    21-Dec-2025
 *
 *  Description:
 *      Implementation of console commands
 *
 *  Notes:
 *
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include "console.h"
#include <stdint.h>
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

typedef struct Command_T
{
    const char* command_name;
    void (*command_handler)(uint16_t, char**);
    const char* command_description;
} Command_T;

/**-----------------------------------------------------------------------------
 *  Public (global) and Extern Variables
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Private (static) Function Prototypes
 *------------------------------------------------------------------------------
 */
void CONSOLE_Command_Help(uint16_t argc, char* argv[]);
void CONSOLE_Command_Echo(uint16_t argc, char* argv[]);

/**-----------------------------------------------------------------------------
 *  Private (static) Variables
 *------------------------------------------------------------------------------
 */

// clang-format off

const Command_T CONSOLE_COMMANDS[] = {
    {"?",       CONSOLE_Command_Help,       "Show available commands."},
    {"help",    CONSOLE_Command_Help,       "Show available commands."},
    {"echo",    CONSOLE_Command_Echo,       "Echoes the provided arguments."},
};

// clang-format on

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */
void CONSOLE_Command_Help(uint16_t argc, char* argv[])
{
    (void)argc;
    (void)argv;
    CONSOLE_Printf("Available commands:\r\n");
    for (size_t command = 0; command < ARRAY_LEN(CONSOLE_COMMANDS); command++)
    {
        CONSOLE_Printf("%s\t- %s\r\n", CONSOLE_COMMANDS[command].command_name,
                       CONSOLE_COMMANDS[command].command_description);
    }
}

void CONSOLE_Command_Echo(uint16_t argc, char* argv[])
{
    for (uint16_t i = 1U; i < argc; i++)
    {
        CONSOLE_Printf("%s%s", argv[i], (i < (argc - 1U)) ? " " : "");
    }
    CONSOLE_Printf("\r\n");
}

/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */

/**
 * @brief Handles the parsed arguments retrieved from the console
 *
 * @param argc - The number of arguments
 * @param argv - pointer to each argument string
 *
 * @returns void
 */
void CONSOLE_Command_Handler(uint16_t argc, char* argv[])
{
    if (argc == 0U)
    {
        return;
    }

    for (size_t command = 0; command < ARRAY_LEN(CONSOLE_COMMANDS); command++)
    {
        if (strcmp(argv[0], CONSOLE_COMMANDS[command].command_name) == 0)
        {
            CONSOLE_COMMANDS[command].command_handler(argc, argv);
            CONSOLE_Printf("\r\n");
            return;
        }
    }

    // unknown command
    CONSOLE_Printf("Unknown command: ");
    CONSOLE_Printf(argv[0]);
    CONSOLE_Printf("\r\n");
}
