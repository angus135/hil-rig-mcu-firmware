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
#include "hw_uart_dut.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */
#define CONSOLE_TASK_PERIOD 5  // 200Hz

#define CONSOLE_LINE_MAX 80U  // max characters in a command line (excluding NUL)
#define CONSOLE_MAX_ARGS 8U   // max argv entries

#define CONSOLE_PRINTF_BUFFER_SIZE 128U
#define CONSOLE_TX_BUFFER_SIZE 1024U
#define CONSOLE_RX_BUFFER_SIZE 32U

#define CONSOLE_UART_CHANNEL HW_UART_CHANNEL_3

#define CONSOLE_BAUD_RATE 115200U

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

static volatile uint32_t s_rx_byte_count = 0U;
static volatile uint8_t  s_last_rx_byte  = 0U;

static uint8_t  s_tx_buf[CONSOLE_TX_BUFFER_SIZE];
static uint32_t s_tx_len = 0U;

static uint8_t s_rx_buf[CONSOLE_RX_BUFFER_SIZE];
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
 * @brief Appends data to the pending console TX buffer.
 *
 * @param data    Pointer to bytes to append.
 * @param length  Number of bytes to append.
 *
 * @return void
 *
 * @note Data is truncated if there is insufficient space in the pending buffer.
 *       Transmission is not started here. The buffer is flushed later by
 *       CONSOLE_Flush_Tx() when the UART TX path is free.
 */
static void CONSOLE_Queue_Transmit( const uint8_t* data, uint32_t length )
{
    if ( ( data == NULL ) || ( length == 0U ) )
    {
        return;
    }

    uint32_t available = CONSOLE_TX_BUFFER_SIZE - s_tx_len;
    uint32_t copy_len  = ( length < available ) ? length : available;

    if ( copy_len == 0U )
    {
        return;
    }

    memcpy( &s_tx_buf[s_tx_len], data, copy_len );
    s_tx_len += copy_len;

    // Optional later: count dropped bytes if copy_len < length
}

/**
 * @brief Attempts to flush a single chunk of the pending console TX buffer through the UART driver.
 *
 * @return void
 *
 * @note At most one UART TX transaction is launched per call.
 *       If the UART TX path is busy or no pending data exists, this function does nothing.
 *
 * @note The pending console TX buffer may be larger than the low-level UART TX staging buffer.
 *       In that case, this function transmits only the first chunk that fits in the low-level
 *       staging buffer and leaves the remaining bytes queued for a later call.
 *
 * @note After a successful load and trigger, the transmitted bytes are removed from the front of
 *       the pending console TX buffer by shifting the remaining bytes down.
 */
static void CONSOLE_Flush_Tx( void )
{
    if ( s_tx_len == 0U )
    {
        return;
    }

    if ( EXEC_UART_Is_Tx_Busy( CONSOLE_UART_CHANNEL ) )
    {
        return;
    }

    uint32_t chunk_len = s_tx_len;

    if ( chunk_len > EXEC_UART_MAX_CHUNK_SIZE )
    {
        chunk_len = EXEC_UART_MAX_CHUNK_SIZE;
    }

    if ( EXEC_UART_Transmit( CONSOLE_UART_CHANNEL, s_tx_buf, chunk_len ) )
    {
        uint32_t remaining = s_tx_len - chunk_len;
        memmove( s_tx_buf, &s_tx_buf[chunk_len], remaining );
        s_tx_len = remaining;
    }
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
    s_rx_byte_count++;
    s_last_rx_byte = byte;

    if ( is_newline )
    {
        // Echo as CRLF for terminal friendliness
        const uint8_t crlf[] = { '\r', '\n' };
        CONSOLE_Queue_Transmit( crlf, sizeof( crlf ) );

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
            const uint8_t backspace[] = { 0x08U, ' ', 0x08U };
            CONSOLE_Queue_Transmit( backspace, sizeof( backspace ) );
        }
        return;
    }

    // Normal character: echo and store if there is space
    CONSOLE_Queue_Transmit( &byte, 1U );

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
static bool CONSOLE_Init( void )
{

    HwUartConfig_T config = { .interface_mode = HW_UART_MODE_TTL_3V3,
                              .baud_rate      = CONSOLE_BAUD_RATE,
                              .word_length    = HW_UART_WORD_LENGTH_8_BITS,
                              .stop_bits      = HW_UART_STOP_BITS_1,
                              .parity         = HW_UART_PARITY_NONE,
                              .rx_enabled     = true,
                              .tx_enabled     = true };

    if ( !EXEC_UART_Apply_Configuration( CONSOLE_UART_CHANNEL, &config ) )
    {
        // Handle configuration error
        return false;
    }

    // reset local parser state
    s_line_len         = 0U;
    s_last_was_newline = false;
    s_tx_len           = 0U;
    CONSOLE_Printf( "%s", WELCOME_MESSAGE );
    return true;
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
    uint32_t bytes_read = 0U;
    if ( !EXEC_UART_Read( CONSOLE_UART_CHANNEL, s_rx_buf, CONSOLE_RX_BUFFER_SIZE, &bytes_read ) )
    {
        return;
    }

    for ( uint32_t i = 0U; i < bytes_read; i++ )
    {
        CONSOLE_Process_Byte( s_rx_buf[i] );
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
 * and transmits the resulting string over the console UART.
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

    CONSOLE_Queue_Transmit( ( const uint8_t* )buffer, count );
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

    if ( !CONSOLE_Init() )
    {

        while ( true )
        {
            vTaskDelay( pdMS_TO_TICKS( 1000U ) );
        }
    }

    TickType_t initial_ticks = xTaskGetTickCount();
    while ( true )
    {
        CONSOLE_Process();
        CONSOLE_Flush_Tx();
        vTaskDelayUntil( &initial_ticks, pdMS_TO_TICKS( CONSOLE_TASK_PERIOD ) );
    }
}
