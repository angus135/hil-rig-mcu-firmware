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

typedef enum
{
    ESC_IDLE,
    ESC_GOT_ESCAPE,
    ESC_GOT_BRACKET
} ConsoleEscapeState_T;

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

static ConsoleEscapeState_T s_escape_state = ESC_IDLE;
static char                 s_history[CONSOLE_HISTORY_DEPTH][CONSOLE_LINE_MAX + 1U];
static size_t               s_history_count        = 0U;
static size_t               s_history_next_index   = 0U;
static int32_t              s_history_browse_index = -1;

// Line buffer and state
static char   s_line_buf[CONSOLE_LINE_MAX + 1U];
static size_t s_line_len = 0U;

// Used to swallow the second char of CRLF / LFCR so you don't process two "empty" commands.
static bool s_last_was_newline = false;

/**-----------------------------------------------------------------------------
 *  Private (static) Function Prototypes
 *------------------------------------------------------------------------------
 */
static void CONSOLE_Save_History( const char* line );
static void CONSOLE_Redraw_Line( void );
static void CONSOLE_Load_Line( const char* line );
static void CONSOLE_History_Up( void );
static void CONSOLE_History_Down( void );

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
    s_line_len             = 0U;
    s_line_buf[0]          = '\0';
    s_last_was_newline     = false;
    s_escape_state         = ESC_IDLE;
    s_history_browse_index = -1;
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

    if ( s_line_len > 0U )
    {
        CONSOLE_Save_History( s_line_buf );
    }

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

    if ( HW_UART_Tx_Is_Busy( CONSOLE_UART_CHANNEL ) )
    {
        return;
    }

    uint32_t chunk_len = s_tx_len;

    if ( chunk_len > HW_UART_TX_MAX_CHUNK_SIZE )
    {
        chunk_len = HW_UART_TX_MAX_CHUNK_SIZE;
    }

    if ( HW_UART_Tx_Load_Buffer( CONSOLE_UART_CHANNEL, s_tx_buf, chunk_len ) )
    {
        if ( HW_UART_Tx_Trigger( CONSOLE_UART_CHANNEL ) )
        {
            uint32_t remaining = s_tx_len - chunk_len;

            memmove( s_tx_buf, &s_tx_buf[chunk_len], remaining );
            s_tx_len = remaining;
        }
    }
}

/**
 * @brief Process a single received console character.
 *
 * Handles console line editing, newline completion, backspace processing,
 * escape-sequence handling for history recall, and echoing of accepted input
 * back to the terminal.
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
        const uint8_t crlf[] = { '\r', '\n' };
        CONSOLE_Write_Raw( crlf, sizeof( crlf ) );

        if ( s_last_was_newline )
        {
            s_last_was_newline = false;
            return;
        }

        s_last_was_newline = true;
        CONSOLE_On_Line_Complete();
        return;
    }

    s_last_was_newline = false;

    if ( is_backspace )
    {
        if ( s_line_len > 0U )
        {
            s_line_len--;

            const uint8_t backspace[] = { 0x08U, ' ', 0x08U };
            CONSOLE_Write_Raw( backspace, sizeof( backspace ) );
        }

        s_history_browse_index = -1;
        return;
    }

    if ( is_control )
    {
        return;
    }

    if ( s_line_len < CONSOLE_LINE_MAX )
    {
        s_history_browse_index = -1;
        s_line_buf[s_line_len] = ( char )byte;
        s_line_len++;
        CONSOLE_Write_Raw( &byte, 1U );
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

/**
 * @brief Save a completed console line into the history buffer.
 *
 * Stores the provided line in the fixed-depth circular history buffer unless
 * the line is empty or is identical to the most recently stored entry.
 *
 * @param line  NUL-terminated console line to save.
 *
 * @returns void
 */
static void CONSOLE_Save_History( const char* line )
{
    if ( line[0] != '\0' )
    {
        if ( s_history_count > 0U )
        {
            size_t last_index =
                ( s_history_next_index + CONSOLE_HISTORY_DEPTH - 1U ) % CONSOLE_HISTORY_DEPTH;

            // Avoid saving duplicate consecutive entries
            if ( strncmp( s_history[last_index], line, CONSOLE_LINE_MAX ) == 0 )
            {
                return;
            }
        }

        // Store to buffer at current index
        snprintf( s_history[s_history_next_index], sizeof( s_history[s_history_next_index] ), "%s",
                  line );

        // Update the next index (wrap around if necessary)
        s_history_next_index = ( s_history_next_index + 1U ) % CONSOLE_HISTORY_DEPTH;

        // Update the history count
        if ( s_history_count < CONSOLE_HISTORY_DEPTH )
        {
            s_history_count++;
        }
        // Reset browse index
        s_history_browse_index = -1;
    }
}

/**
 * @brief Redraw the current editable console line on the terminal.
 *
 * Clears the current terminal line and rewrites the contents of the active
 * console input buffer so that recalled history entries or edited lines are
 * visible to the user.
 *
 * @returns void
 */
static void CONSOLE_Redraw_Line( void )
{
    CONSOLE_Printf( "\r\033[2K" );
    CONSOLE_Printf( "%s", s_line_buf );
}

/**
 * @brief Replace the active console input line with the provided text.
 *
 * Copies the provided line into the editable console input buffer, updates the
 * tracked line length, and redraws the terminal line so the new contents are
 * visible to the user.
 *
 * @param line  NUL-terminated line to load into the active console input buffer.
 *
 * @returns void
 */
static void CONSOLE_Load_Line( const char* line )
{
    snprintf( s_line_buf, sizeof( s_line_buf ), "%s", line );
    s_line_len = strlen( s_line_buf );
    CONSOLE_Redraw_Line();
}

/**
 * @brief Recall the previous entry from the console history buffer.
 *
 * Moves the current history browse position toward older stored commands and
 * loads the selected history entry into the active console input buffer.
 *
 * @returns void
 */
static void CONSOLE_History_Up( void )
{
    if ( s_history_count == 0U )
    {
        return;
    }

    if ( s_history_browse_index < 0 )
    {
        s_history_browse_index = ( int32_t )( ( s_history_next_index + CONSOLE_HISTORY_DEPTH - 1U )
                                              % CONSOLE_HISTORY_DEPTH );
    }
    else
    {
        size_t oldest_index = ( s_history_next_index + CONSOLE_HISTORY_DEPTH - s_history_count )
                              % CONSOLE_HISTORY_DEPTH;

        if ( ( size_t )s_history_browse_index != oldest_index )
        {
            s_history_browse_index =
                ( int32_t )( ( ( size_t )s_history_browse_index + CONSOLE_HISTORY_DEPTH - 1U )
                             % CONSOLE_HISTORY_DEPTH );
        }
    }

    CONSOLE_Load_Line( s_history[s_history_browse_index] );
}

/**
 * @brief Recall the next entry from the console history buffer.
 *
 * Moves the current history browse position toward newer stored commands. If
 * the newest history entry is passed, browsing ends and the active console
 * input line is cleared.
 *
 * @returns void
 */
static void CONSOLE_History_Down( void )
{
    if ( s_history_count == 0U || s_history_browse_index < 0 )
    {
        return;
    }

    size_t newest_index =
        ( s_history_next_index + CONSOLE_HISTORY_DEPTH - 1U ) % CONSOLE_HISTORY_DEPTH;

    if ( ( size_t )s_history_browse_index == newest_index )
    {
        s_history_browse_index = -1;
        s_line_buf[0]          = '\0';
        s_line_len             = 0U;
        CONSOLE_Redraw_Line();
        return;
    }

    s_history_browse_index =
        ( int32_t )( ( ( size_t )s_history_browse_index + 1U ) % CONSOLE_HISTORY_DEPTH );

    CONSOLE_Load_Line( s_history[s_history_browse_index] );
}

/**
 * @brief Save a completed console line into the history buffer.
 *
 * Stores the provided line in the fixed-depth circular history buffer unless
 * the line is empty or is identical to the most recently stored entry.
 *
 * @param line  NUL-terminated console line to save.
 *
 * @returns void
 */
static void CONSOLE_Save_History( const char* line )
{
    if ( line[0] != '\0' )
    {
        if ( s_history_count > 0U )
        {
            size_t last_index =
                ( s_history_next_index + CONSOLE_HISTORY_DEPTH - 1U ) % CONSOLE_HISTORY_DEPTH;

            // Avoid saving duplicate consecutive entries
            if ( strncmp( s_history[last_index], line, CONSOLE_LINE_MAX ) == 0 )
            {
                return;
            }
        }

        // Store to buffer at current index
        snprintf( s_history[s_history_next_index], sizeof( s_history[s_history_next_index] ), "%s",
                  line );

        // Update the next index (wrap around if necessary)
        s_history_next_index = ( s_history_next_index + 1U ) % CONSOLE_HISTORY_DEPTH;

        // Update the history count
        if ( s_history_count < CONSOLE_HISTORY_DEPTH )
        {
            s_history_count++;
        }
        // Reset browse index
        s_history_browse_index = -1;
    }
}

/**
 * @brief Redraw the current editable console line on the terminal.
 *
 * Clears the current terminal line and rewrites the contents of the active
 * console input buffer so that recalled history entries or edited lines are
 * visible to the user.
 *
 * @returns void
 */
static void CONSOLE_Redraw_Line( void )
{
    CONSOLE_Printf( "\r\033[2K" );
    CONSOLE_Printf( "%s", s_line_buf );
}

/**
 * @brief Replace the active console input line with the provided text.
 *
 * Copies the provided line into the editable console input buffer, updates the
 * tracked line length, and redraws the terminal line so the new contents are
 * visible to the user.
 *
 * @param line  NUL-terminated line to load into the active console input buffer.
 *
 * @returns void
 */
static void CONSOLE_Load_Line( const char* line )
{
    snprintf( s_line_buf, sizeof( s_line_buf ), "%s", line );
    s_line_len = strlen( s_line_buf );
    CONSOLE_Redraw_Line();
}

/**
 * @brief Recall the previous entry from the console history buffer.
 *
 * Moves the current history browse position toward older stored commands and
 * loads the selected history entry into the active console input buffer.
 *
 * @returns void
 */
static void CONSOLE_History_Up( void )
{
    if ( s_history_count == 0U )
    {
        return;
    }

    if ( s_history_browse_index < 0 )
    {
        s_history_browse_index = ( int32_t )( ( s_history_next_index + CONSOLE_HISTORY_DEPTH - 1U )
                                              % CONSOLE_HISTORY_DEPTH );
    }
    else
    {
        size_t oldest_index = ( s_history_next_index + CONSOLE_HISTORY_DEPTH - s_history_count )
                              % CONSOLE_HISTORY_DEPTH;

        if ( ( size_t )s_history_browse_index != oldest_index )
        {
            s_history_browse_index =
                ( int32_t )( ( ( size_t )s_history_browse_index + CONSOLE_HISTORY_DEPTH - 1U )
                             % CONSOLE_HISTORY_DEPTH );
        }
    }

    CONSOLE_Load_Line( s_history[s_history_browse_index] );
}

/**
 * @brief Recall the next entry from the console history buffer.
 *
 * Moves the current history browse position toward newer stored commands. If
 * the newest history entry is passed, browsing ends and the active console
 * input line is cleared.
 *
 * @returns void
 */
static void CONSOLE_History_Down( void )
{
    if ( s_history_count == 0U || s_history_browse_index < 0 )
    {
        return;
    }

    size_t newest_index =
        ( s_history_next_index + CONSOLE_HISTORY_DEPTH - 1U ) % CONSOLE_HISTORY_DEPTH;

    if ( ( size_t )s_history_browse_index == newest_index )
    {
        s_history_browse_index = -1;
        s_line_buf[0]          = '\0';
        s_line_len             = 0U;
        CONSOLE_Redraw_Line();
        return;
    }

    s_history_browse_index =
        ( int32_t )( ( ( size_t )s_history_browse_index + 1U ) % CONSOLE_HISTORY_DEPTH );

    CONSOLE_Load_Line( s_history[s_history_browse_index] );
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
