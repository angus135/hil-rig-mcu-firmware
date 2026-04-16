/******************************************************************************
 *  File:       console.c
 *  Author:     Angus Corr
 *  Created:    6-Dec-2025
 *
 *  Description:
 *      Console module implementation.
 *
 *      This module provides:
 *        - A simple command line console which can be accessed to interface with the MCU
 *
 *  Notes:
 *     None
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */
#include "rtos_config.h"
#include "console.h"
#include "hw_uart.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */
#define CONSOLE_TASK_PERIOD 5  // 200Hz

#define CONSOLE_LINE_MAX 80U  // max characters in a command line (excluding NUL)
#define CONSOLE_MAX_ARGS 8U   // max argv entries

#define CONSOLE_PRINTF_BUFFER_SIZE 128U

/**-----------------------------------------------------------------------------
 *  Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Public (global) and Extern Variables
 *------------------------------------------------------------------------------
 */

TaskHandle_t* ConsoleTaskHandle = NULL;  // NOLINT(readability-identifier-naming)

/**-----------------------------------------------------------------------------
 *  Private (static) Variables
 *------------------------------------------------------------------------------
 */
static const uint8_t WELCOME_MESSAGE[] = "Welcome to HIL-RIG MCU!\r\n";

/**-----------------------------------------------------------------------------
 *  Private (static) Function Prototypes
 *------------------------------------------------------------------------------
 */

// Line buffer and state
static char   s_line_buf[CONSOLE_LINE_MAX + 1U];
static size_t s_line_len = 0U;

// Used to swallow the second char of CRLF / LFCR so you don't process two "empty" commands.
static bool s_last_was_newline = false;

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

/**
 * @brief Split a NUL-terminated command line into argc/argv (in-place).
 *
 * This function tokenises the provided line buffer by replacing whitespace
 * delimiters with NUL characters and populating an argv-style array of
 * pointers into the original buffer.
 *
 * Parsing rules:
 *  - Tokens are separated by spaces or tabs
 *  - Multiple adjacent whitespace characters collapse
 *  - No support for quoted strings or escape characters
 *
 * @param line      Pointer to a mutable, NUL-terminated command line buffer.
 *                  The buffer is modified in-place during parsing.
 * @param argc_out  Pointer to where the parsed argument count will be written.
 * @param argv_out  Output array of pointers to each parsed argument string.
 *                  Pointers reference locations within @p line.
 *
 * @returns void
 */
static void CONSOLE_Parse_Args( char* line, uint16_t* argc_out, char* argv_out[CONSOLE_MAX_ARGS] )
{
    uint16_t argc = 0;
    char*    p    = line;

    while ( *p != '\0' )
    {
        // Skip leading whitespace
        while ( ( *p == ' ' ) || ( *p == '\t' ) )
        {
            p++;
        }

        if ( *p == '\0' )
        {
            break;
        }

        // Start of token
        if ( argc < ( uint16_t )CONSOLE_MAX_ARGS )
        {
            argv_out[argc] = p;
            argc++;
        }
        else
        {
            // Too many args: stop collecting further tokens
            break;
        }

        // Advance to end of token
        while ( ( *p != '\0' ) && ( *p != ' ' ) && ( *p != '\t' ) )
        {
            p++;
        }

        // Terminate token if not end of string
        if ( *p != '\0' )
        {
            *p = '\0';
            p++;
        }
    }

    *argc_out = argc;
}

/**
 * @brief Handle completion of a full console command line.
 *
 * This function is called once a complete command line has been received
 * (terminated by a newline character). It finalises the line buffer,
 * parses it into argc/argv form, and invokes the registered command handler.
 *
 * Empty command lines are ignored.
 *
 * @returns void
 */
static void CONSOLE_On_Line_Complete( void )
{
    // NUL-terminate
    s_line_buf[s_line_len] = '\0';

    // Parse into argv in-place
    char*    argv[CONSOLE_MAX_ARGS] = { 0 };
    uint16_t argc                   = 0;

    CONSOLE_Parse_Args( s_line_buf, &argc, argv );

    // Ignore empty lines
    if ( argc > 0 )
    {
        CONSOLE_Command_Handler( argc, argv );
    }

    // Reset buffer for next command
    s_line_len = 0U;
}

/**
 * @brief Process a single received console character.
 *
 * This function handles echoing input back to the terminal, assembling
 * characters into the command line buffer, detecting line termination,
 * and performing basic editing actions such as backspace handling.
 *
 * Newline characters ('\\r' or '\\n') trigger command completion.
 *
 * @param byte  The received character byte from the console UART.
 *
 * @returns void
 */
static void CONSOLE_Process_Byte( uint8_t byte )
{
    const bool is_newline = ( byte == '\r' ) || ( byte == '\n' );

    if ( is_newline )
    {
        // Echo as CRLF for terminal friendliness
        HW_UART_Write_Byte( UART_CONSOLE, '\r' );
        HW_UART_Write_Byte( UART_CONSOLE, '\n' );

        // Swallow the second newline char in CRLF or LFCR
        if ( s_last_was_newline )
        {
            s_last_was_newline = false;
            return;
        }

        s_last_was_newline = true;

        // Finish the command
        CONSOLE_On_Line_Complete();
        return;
    }

    s_last_was_newline = false;

    // Optional: handle backspace for a nicer UX
    if ( ( byte == 0x08U ) || ( byte == 0x7FU ) )
    {
        if ( s_line_len > 0U )
        {
            s_line_len--;

            // "Erase" character on terminal: BS, space, BS
            HW_UART_Write_Byte( UART_CONSOLE, 0x08U );
            HW_UART_Write_Byte( UART_CONSOLE, ' ' );
            HW_UART_Write_Byte( UART_CONSOLE, 0x08U );
        }
        return;
    }

    // Normal character: echo and store if there is space
    HW_UART_Write_Byte( UART_CONSOLE, byte );

    if ( s_line_len < CONSOLE_LINE_MAX )
    {
        s_line_buf[s_line_len] = ( char )byte;
        s_line_len++;
    }
    else
    {
        // Buffer full: simplest behaviour is to ignore further chars until newline.
        // You could also beep or print an error here if you want.
    }
}

/**
 * @brief Initialise the console subsystem.
 *
 * Sends the console welcome banner to the configured UART interface.
 * This function must be called before any console processing occurs.
 *
 * @returns void
 */
static void CONSOLE_Init( void )
{
    CONSOLE_Printf( "%s", WELCOME_MESSAGE );
}

/**
 * @brief Poll and process incoming console UART data.
 *
 * Attempts to read a single byte from the console UART. If a byte is
 * available, it is forwarded to the console character processing logic.
 *
 * @returns void
 */
static void CONSOLE_Process( void )
{
    uint8_t      byte   = 0;
    UARTStatus_T status = HW_UART_Read_Byte( UART_CONSOLE, &byte );
    if ( status == UART_SUCCESS )
    {
        CONSOLE_Process_Byte( byte );
    }
}

/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */

/**
 * @brief Printf-style formatted output to the console UART
 *
 * Formats the input string into a fixed-size buffer using vsnprintf()
 * and transmits the resulting characters byte-by-byte over the console UART.
 *
 * Output is truncated if it exceeds the internal buffer size.
 *
 * @param format  Standard printf-style format string
 * @param ...     Variable arguments corresponding to the format string
 *
 * @returns void
 */
void CONSOLE_Printf( const char* format, ... )
{
    char buffer[CONSOLE_PRINTF_BUFFER_SIZE];

    va_list args;
    va_start( args, format );

    const int len = vsnprintf( buffer, sizeof( buffer ), format, args );

    va_end( args );

    if ( len <= 0 )
    {
        return;
    }

    // Clamp length to buffer size
    uint32_t count =
        ( len < ( int )sizeof( buffer ) ) ? ( uint32_t )len : ( uint32_t )( sizeof( buffer ) - 1U );

    for ( uint32_t i = 0U; i < count; i++ )
    {
        HW_UART_Write_Byte( UART_CONSOLE, ( uint8_t )buffer[i] );
    }
}

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
void CONSOLE_Task( void* task_parameters )
{
    ( void )task_parameters;

    CONSOLE_Init();

    TickType_t initial_ticks = xTaskGetTickCount();
    while ( true )
    {
        CONSOLE_Process();
        vTaskDelayUntil( &initial_ticks, pdMS_TO_TICKS( CONSOLE_TASK_PERIOD ) );
    }
}
