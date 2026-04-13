/******************************************************************************
 *  File:       console.c
 *  Author:     Angus Corr & Callum Rafferty
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
#include "hw_uart_console.h"
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
#define CONSOLE_RX_BUFFER_SIZE 32U

#define CONSOLE_TX_BUFFER_SIZE 512U
#define CONSOLE_TX_FLUSH_CHUNK_SIZE 64U
#define CONSOLE_TX_TIMEOUT_MS 100U

#define CONSOLE_BAUD_RATE 115200U

#define CONSOLE_UART_CHANNEL HW_UART_CHANNEL_3

/**-----------------------------------------------------------------------------
 *  Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Public (global) and Extern Variables
 *------------------------------------------------------------------------------
 */

TaskHandle_t ConsoleTaskHandle = NULL;  // NOLINT(readability-identifier-naming)

/**-----------------------------------------------------------------------------
 *  Private (static) Variables
 *------------------------------------------------------------------------------
 */
static const uint8_t WELCOME_MESSAGE[] = "Welcome to HIL-RIG MCU!\r\n";
static uint8_t       s_tx_buf[CONSOLE_TX_BUFFER_SIZE];
static uint32_t      s_tx_head           = 0U;
static uint32_t      s_tx_tail           = 0U;
static uint32_t      s_tx_overflow_count = 0U;

// Line buffer and state
static char   s_line_buf[CONSOLE_LINE_MAX + 1U];
static size_t s_line_len = 0U;

// Used to swallow the second char of CRLF / LFCR so you don't process two "empty" commands.
static bool s_last_was_newline = false;

/**-----------------------------------------------------------------------------
 *  Private (static) Function Prototypes
 *------------------------------------------------------------------------------
 */

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
 * @brief Reset the current console line editing state.
 *
 * Clears the in-progress command line length and newline tracking state so
 * that the next received characters begin a fresh command line.
 *
 * @returns void
 */
static void CONSOLE_Reset_Line_State( void )
{
    s_line_len         = 0U;
    s_last_was_newline = false;
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

    // Reset line state
    CONSOLE_Reset_Line_State();
}

/**
 * @brief Get the number of bytes currently queued in the console TX ring buffer.
 *
 * Computes the occupied length of the software-managed TX ring buffer using
 * the current head and tail indices.
 *
 * @returns Number of bytes currently pending transmission.
 */
static inline uint32_t CONSOLE_Tx_Used( void )
{
    return ( s_tx_head >= s_tx_tail ) ? ( s_tx_head - s_tx_tail )
                                      : ( CONSOLE_TX_BUFFER_SIZE - ( s_tx_tail - s_tx_head ) );
}

/**
 * @brief Get the remaining free space in the console TX ring buffer.
 *
 * Computes how many additional bytes can be appended to the software-managed
 * TX ring buffer while preserving the empty/full distinction by leaving one
 * slot unused.
 *
 * @returns Number of free bytes available for enqueue.
 */
static inline uint32_t CONSOLE_Tx_Free( void )
{
    return ( CONSOLE_TX_BUFFER_SIZE - CONSOLE_Tx_Used() ) - 1U;
}

/**
 * @brief Append raw bytes to the console TX ring buffer.
 *
 * Attempts to enqueue the provided bytes into the console-owned TX ring buffer.
 * Queued bytes are transmitted later by CONSOLE_Flush_Tx() from task context
 * using a blocking UART transmit call.
 *
 * If insufficient space is available, only the bytes that fit are queued and
 * the overflow counter is incremented.
 *
 * @param data    Pointer to bytes to enqueue.
 * @param length  Number of bytes to enqueue.
 *
 * @returns void
 */
static void CONSOLE_Write_Raw( const uint8_t* data, uint32_t length )
{
    if ( ( data == NULL ) || ( length == 0U ) )
    {
        return;
    }

    uint32_t free_space = CONSOLE_Tx_Free();
    uint32_t copy_len   = ( length < free_space ) ? length : free_space;

    if ( copy_len < length )
    {
        s_tx_overflow_count++;
    }

    for ( uint32_t i = 0U; i < copy_len; i++ )
    {
        s_tx_buf[s_tx_head] = data[i];
        s_tx_head           = ( s_tx_head + 1U ) % CONSOLE_TX_BUFFER_SIZE;
    }
}

/**
 * @brief Transmit one pending chunk from the console TX ring buffer.
 *
 * If queued TX data is available, this function selects the next contiguous
 * region starting at the TX tail, limits the transfer to the configured flush
 * chunk size, and sends it using the blocking console UART transmit function.
 *
 * On successful transmission, the TX tail is advanced by the transmitted
 * length. If transmission fails, the pending data remains queued for a later
 * retry.
 *
 * @returns void
 */
static void CONSOLE_Flush_Tx( void )
{
    if ( s_tx_head == s_tx_tail )
    {
        return;
    }

    uint32_t contiguous_len = 0U;

    if ( s_tx_head > s_tx_tail )
    {
        contiguous_len = s_tx_head - s_tx_tail;
    }
    else
    {
        contiguous_len = CONSOLE_TX_BUFFER_SIZE - s_tx_tail;
    }

    uint32_t chunk_len = ( contiguous_len > CONSOLE_TX_FLUSH_CHUNK_SIZE )
                             ? CONSOLE_TX_FLUSH_CHUNK_SIZE
                             : contiguous_len;

    if ( HW_UART_CONSOLE_Write_Blocking( &s_tx_buf[s_tx_tail], chunk_len, CONSOLE_TX_TIMEOUT_MS ) )
    {
        s_tx_tail = ( s_tx_tail + chunk_len ) % CONSOLE_TX_BUFFER_SIZE;
    }
}

/**
 * @brief Helper function to transmit data over the console UART.
 *
 * @param data        Pointer to the data to transmit
 * @param length      Number of bytes to transmit
 *
 * @returns void
 */
static void CONSOLE_Transmit( const uint8_t* data, uint32_t length )
{
    if ( HW_UART_Tx_Load_Buffer( CONSOLE_UART_CHANNEL, data, length ) )
    {
        HW_UART_Tx_Trigger( CONSOLE_UART_CHANNEL );
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
        CONSOLE_Write_Raw( crlf, sizeof( crlf ) );

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
            CONSOLE_Write_Raw( backspace, sizeof( backspace ) );
        }
        return;
    }

    // Normal character: echo and store if there is space
    CONSOLE_Write_Raw( &byte, 1U );

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
 * @brief Initialise the console subsystem and queue the welcome banner.
 *
 * Initialises the low-level console UART driver, resets local TX and line
 * editing state, queues the console welcome banner, and performs an initial
 * TX flush so the banner is sent immediately.
 *
 * @returns true if console initialisation succeeds, otherwise false.
 */
bool CONSOLE_Init( void )
{
    if ( !HW_UART_CONSOLE_Init( CONSOLE_BAUD_RATE ) )
    {
        return false;
    }

    s_tx_head = 0U;
    s_tx_tail = 0U;
    CONSOLE_Reset_Line_State();

    CONSOLE_Write_Raw( WELCOME_MESSAGE, sizeof( WELCOME_MESSAGE ) - 1U );
    CONSOLE_Flush_Tx();
    return true;
}

/**
 * @brief Drain and process all currently available console RX data.
 *
 * Repeatedly reads available bytes from the low-level console UART RX buffer
 * into a small local buffer and forwards each byte to the console character
 * processing logic until no unread data remains.
 *
 * @returns void
 */
static void CONSOLE_Process( void )
{
    uint8_t rx_buf[CONSOLE_RX_BUFFER_SIZE];
    while ( true )
    {
        uint32_t bytes_read = 0U;

        if ( !HW_UART_CONSOLE_Read( rx_buf, CONSOLE_RX_BUFFER_SIZE, &bytes_read ) )
        {
            return;
        }

        if ( bytes_read == 0U )
        {
            return;
        }

        for ( uint32_t i = 0U; i < bytes_read; i++ )
        {
            CONSOLE_Process_Byte( rx_buf[i] );
        }
    }

    uint32_t processed = 0U;

    for ( uint32_t i = 0U; i < spans.first_span.length_bytes; i++ )
    {
        CONSOLE_Process_Byte( spans.first_span.data[i] );
        processed++;
    }

    for ( uint32_t i = 0U; i < spans.second_span.length_bytes; i++ )
    {
        CONSOLE_Process_Byte( spans.second_span.data[i] );
        processed++;
    }
    HW_UART_Rx_Consume( CONSOLE_UART_CHANNEL, processed );
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

    CONSOLE_Write_Raw( ( const uint8_t* )buffer, count );
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
