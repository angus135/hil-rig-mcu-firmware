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

#include "hw_uart.h"
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

static void CONSOLE_Write_String(const char* str);

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

/**
 * @brief Write a NUL-terminated string to the console UART.
 *
 * @param str Pointer to string to transmit
 *
 * @returns void
 */
static void CONSOLE_Write_String(const char* str)
{
    while (*str != '\0')
    {
        HW_UART_Write_Byte(UART_CONSOLE, (uint8_t)*str);
        str++;
    }
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

    /* ---- help ------------------------------------------------------------ */
    if (strcmp(argv[0], "help") == 0)
    {
        CONSOLE_Write_String("Available commands:\r\n");
        CONSOLE_Write_String("  help           - Show this message\r\n");
        CONSOLE_Write_String("  echo <args>    - Echo arguments back\r\n");
        return;
    }

    /* ---- echo ------------------------------------------------------------ */
    if (strcmp(argv[0], "echo") == 0)
    {
        for (uint16_t i = 1U; i < argc; i++)
        {
            CONSOLE_Write_String(argv[i]);

            if (i < (argc - 1U))
            {
                CONSOLE_Write_String(" ");
            }
        }

        CONSOLE_Write_String("\r\n");
        return;
    }

    /* ---- unknown command ------------------------------------------------- */
    CONSOLE_Write_String("Unknown command: ");
    CONSOLE_Write_String(argv[0]);
    CONSOLE_Write_String("\r\n");
}
