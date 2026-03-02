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
#include "test_scheduler.h"
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
    void ( *command_handler )( uint16_t, char** );
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
static void CONSOLE_Command_Help( uint16_t argc, char* argv[] );
static void CONSOLE_Command_Echo( uint16_t argc, char* argv[] );
static void CONSOLE_Command_Test_Scheduler( uint16_t argc, char* argv[] );

/**-----------------------------------------------------------------------------
 *  Private (static) Variables
 *------------------------------------------------------------------------------
 */

// clang-format off

const Command_T CONSOLE_COMMANDS[] = {
    {"?",       CONSOLE_Command_Help,       "Show available commands."},
    {"help",    CONSOLE_Command_Help,       "Show available commands."},
    {"echo",    CONSOLE_Command_Echo,       "Echoes the provided arguments."},
{"test_scheduler",    CONSOLE_Command_Test_Scheduler,       "Starts the test scheduler."},
};

// clang-format on

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

/**
 * @brief Handles the help command by providing avaiable commands to the console
 *
 * @param argc - The number of arguments
 * @param argv - pointer to each argument string
 *
 * @returns void
 */
static void CONSOLE_Command_Help( uint16_t argc, char* argv[] )
{
    ( void )argc;
    ( void )argv;
    CONSOLE_Printf( "Available commands:\r\n" );
    for ( size_t command = 0; command < ARRAY_LEN( CONSOLE_COMMANDS ); command++ )
    {
        CONSOLE_Printf( "%s\t- %s\r\n", CONSOLE_COMMANDS[command].command_name,
                        CONSOLE_COMMANDS[command].command_description );
    }
}

/**
 * @brief Handles the echo command by echoing whatever was provided in the command to the console
 *
 * @param argc - The number of arguments
 * @param argv - pointer to each argument string
 *
 * @returns void
 */
static void CONSOLE_Command_Echo( uint16_t argc, char* argv[] )
{
    for ( uint16_t i = 1U; i < argc; i++ )
    {
        CONSOLE_Printf( "%s%s", argv[i], ( i < ( argc - 1U ) ) ? " " : "" );
    }
    CONSOLE_Printf( "\r\n" );
}

/**
 * @brief Starts the test scheduler
 *
 * @param argc - The number of arguments
 * @param argv - pointer to each argument string
 *
 * @returns void
 */
static void CONSOLE_Command_Test_Scheduler( uint16_t argc, char* argv[] )
{
    if ( argc < 2 || argv[1] == NULL )
    {
        CONSOLE_Printf( "Usage:\r\n" );
        CONSOLE_Printf( "  test_scheduler start\r\n" );
        CONSOLE_Printf( "  test_scheduler stop\r\n" );
        return;
    }

    if ( strcmp( argv[1], "start" ) == 0 )
    {
        TEST_SCHEDULER_Init();
        TEST_SCHEDULER_Start();
    }
    else if ( strcmp( argv[1], "stop" ) == 0 )
    {
        TEST_SCHEDULER_Stop();
    }
    else if ( strcmp( argv[1], "frequency" ) == 0 )
    {
        if ( ( strcmp( argv[2], "10k" ) == 0 ) || ( strcmp( argv[2], "10000" ) == 0 ) )
        {
            TEST_SCHEDULER_Set_Frequency_Mode( FREQUENCY_10KHZ );
        }
        else if ( ( strcmp( argv[2], "1k" ) == 0 ) || ( strcmp( argv[2], "1000" ) == 0 ) )
        {
            TEST_SCHEDULER_Set_Frequency_Mode( FREQUENCY_1KHZ );
        }
        else if ( strcmp( argv[2], "100" ) == 0 )
        {
            TEST_SCHEDULER_Set_Frequency_Mode( FREQUENCY_100HZ );
        }
        else
        {
            CONSOLE_Printf( "Invalid: Desired frequencies can only be 100Hz, 1kHz or 10kHz\r\n" );
        }
    }
    else
    {
        CONSOLE_Printf( "Invalid argument: %s\r\n", argv[1] );
        CONSOLE_Printf( "Usage:\r\n" );
        CONSOLE_Printf( "  test_scheduler start\r\n" );
        CONSOLE_Printf( "  test_scheduler stop\r\n" );
        CONSOLE_Printf( "  test_scheduler frequency <desired frequency>\r\n" );
        CONSOLE_Printf( "    Note: Desired frequencies can only be 100Hz, 1kHz or 10kHz\r\n" );
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
void CONSOLE_Command_Handler( uint16_t argc, char* argv[] )
{
    if ( argc == 0U )
    {
        return;
    }

    for ( size_t command = 0; command < ARRAY_LEN( CONSOLE_COMMANDS ); command++ )
    {
        if ( strcmp( argv[0], CONSOLE_COMMANDS[command].command_name ) == 0 )
        {
            CONSOLE_COMMANDS[command].command_handler( argc, argv );
            CONSOLE_Printf( "\r\n" );
            return;
        }
    }

    // unknown command
    CONSOLE_Printf( "Unknown command: " );
    CONSOLE_Printf( argv[0] );
    CONSOLE_Printf( "\r\n" );
}
