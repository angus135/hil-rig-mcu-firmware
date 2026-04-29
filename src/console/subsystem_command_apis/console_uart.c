/******************************************************************************
 *  File:       console_uart.c
 *  Author:     Callum Rafferty
 *  Created:    29 Apr 2026
 *
 *  Description:
 *      Console command API for DUT facing UART functionality.
 *
 *      This module owns UART related console command handling, including
 *      channel configuration and UART loopback testing commands.
 *
 *  Notes:
 *      The top level console command handler dispatches to this module for the
 *      "uart" command namespace.
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include "console_uart.h"

#include "console.h"
#include "exec_uart.h"
#include "hw_uart_dut.h"
#include "rtos_config.h"

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */

#define CONSOLE_UART_LOOPBACK_MAX_BAUD_RATE 2000000U
#define CONSOLE_UART_LOOPBACK_SETTLE_DELAY_MS 10U
#define CONSOLE_UART_LOOPBACK_DELAY_MARGIN_MS 10U
#define CONSOLE_UART_LOOPBACK_READ_POLL_MS 1U
#define CONSOLE_UART_DISCARD_BUFFER_SIZE 128U
#define CONSOLE_UART_CLEAR_RX_MAX_READS 32U

#define CONSOLE_UART_BLAST_RANDOM_MAX_ITERATIONS 1000U
#define CONSOLE_UART_BLAST_RANDOM_DEFAULT_ITERATIONS 1U
#define CONSOLE_UART_BLAST_RANDOM_DEFAULT_CHUNK_SIZE 0U
#define CONSOLE_UART_BLAST_RANDOM_MAX_LENGTH EXEC_UART_MAX_CHUNK_SIZE

/**-----------------------------------------------------------------------------
 *  Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

typedef struct
{
    bool     is_configured;
    uint32_t baud_rate;
} ConsoleUartLoopbackState_T;

/**-----------------------------------------------------------------------------
 *  Private Variables
 *------------------------------------------------------------------------------
 */

static ConsoleUartLoopbackState_T s_uart_loopback_state = { 0 };

static uint8_t s_uart_blast_tx_buf[CONSOLE_UART_BLAST_RANDOM_MAX_LENGTH];
static uint8_t s_uart_blast_rx_buf[CONSOLE_UART_BLAST_RANDOM_MAX_LENGTH];

/**-----------------------------------------------------------------------------
 *  Private Function Prototypes
 *------------------------------------------------------------------------------
 */

static void CONSOLE_UART_Print_Usage( void );
static void CONSOLE_UART_Loopback_Print_Usage( void );

static void CONSOLE_UART_Command_Loopback( uint16_t argc, char* argv[] );

static void CONSOLE_UART_Loopback_Configure( uint16_t argc, char* argv[] );
static void CONSOLE_UART_Loopback_Deconfigure( uint16_t argc, char* argv[] );
static void CONSOLE_UART_Loopback_Status( uint16_t argc, char* argv[] );
static void CONSOLE_UART_Loopback_Start( uint16_t argc, char* argv[] );
static void CONSOLE_UART_Loopback_Blast_Random( uint16_t argc, char* argv[] );

static bool CONSOLE_UART_Parse_Baud_Rate( const char* text, uint32_t* baud_rate_out );
static bool CONSOLE_UART_Parse_Channel( const char* text, HwUartChannel_T* channel_out );
static bool CONSOLE_UART_Parse_U32( const char* text, const char* field_name, uint32_t min_value,
                                    uint32_t max_value, uint32_t* value_out );

static uint32_t CONSOLE_UART_Prng_Next( uint32_t* state );
static void     CONSOLE_UART_Fill_Random_Buffer( uint8_t* buffer, uint32_t length, uint32_t seed );

static bool CONSOLE_UART_Build_Tx_Text( uint16_t argc, char* argv[], uint16_t first_text_arg,
                                        char* tx_text, uint32_t tx_text_size,
                                        uint32_t* tx_length_out );

static void     CONSOLE_UART_Clear_Rx_Data( void );
static uint32_t CONSOLE_UART_Calc_Loopback_Delay_Ms( uint32_t length_bytes );
static bool     CONSOLE_UART_Read_Until_Length( HwUartChannel_T receiver_ch, uint8_t* rx_buf,
                                                uint32_t rx_buf_size, uint32_t expected_length,
                                                uint32_t timeout_ms, uint32_t* bytes_read_out );

static bool CONSOLE_UART_Read_And_Report_Loopback_Result( HwUartChannel_T receiver_ch,
                                                          const char* tx_text, uint32_t tx_length );

static bool CONSOLE_UART_Transmit_Buffer_Chunked( HwUartChannel_T sender_ch, const uint8_t* data,
                                                  uint32_t length, uint32_t chunk_size );

static bool CONSOLE_UART_Read_And_Compare_Buffer( HwUartChannel_T receiver_ch,
                                                  const uint8_t* expected, uint32_t expected_length,
                                                  uint32_t iteration );

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

static void CONSOLE_UART_Print_Usage( void )
{
    CONSOLE_Printf( "Usage:\r\n" );
    CONSOLE_Printf( "  uart loopback <command>\r\n" );
    CONSOLE_Printf( "\r\n" );
    CONSOLE_Printf( "Commands:\r\n" );
    CONSOLE_Printf( "  loopback    Configure channels and run RX/TX loopback tests\r\n" );
}

static void CONSOLE_UART_Loopback_Print_Usage( void )
{
    CONSOLE_Printf( "Usage:\r\n" );
    CONSOLE_Printf( "  uart loopback configure <baud>\r\n" );
    CONSOLE_Printf( "  uart loopback deconfigure\r\n" );
    CONSOLE_Printf( "  uart loopback status\r\n" );
    CONSOLE_Printf( "  uart loopback start <sender_ch> <receiver_ch> <data ...>\r\n" );
    CONSOLE_Printf( "  uart loopback blast_random <sender_ch> <receiver_ch> <length> <seed> "
                    "[iterations] [chunk_size]\r\n" );
    CONSOLE_Printf( "    note: sender_ch and receiver_ch must be in {ch1,ch2}\r\n" );
    CONSOLE_Printf( "    note: configure uses fixed mode TTL_3V3, 8N1, RX+TX enabled\r\n" );
    CONSOLE_Printf( "    note: blast_random uses deterministic pseudo-random data\r\n" );
    CONSOLE_Printf( "    note: chunk_size 0 sends the payload in one transmit call\r\n" );
}

static bool CONSOLE_UART_Parse_U32( const char* text, const char* field_name, uint32_t min_value,
                                    uint32_t max_value, uint32_t* value_out )
{
    char*    end_ptr = NULL;
    uint32_t value   = ( uint32_t )strtoul( text, &end_ptr, 10 );

    if ( ( end_ptr == text ) || ( *end_ptr != '\0' ) )
    {
        CONSOLE_Printf( "Invalid %s\r\n", field_name );
        return false;
    }

    if ( value < min_value || value > max_value )
    {
        CONSOLE_Printf( "Invalid %s: valid range is %lu to %lu\r\n", field_name,
                        ( unsigned long )min_value, ( unsigned long )max_value );
        return false;
    }

    *value_out = value;
    return true;
}

static bool CONSOLE_UART_Parse_Baud_Rate( const char* text, uint32_t* baud_rate_out )
{
    return CONSOLE_UART_Parse_U32( text, "baud rate", 1U, CONSOLE_UART_LOOPBACK_MAX_BAUD_RATE,
                                   baud_rate_out );
}

static bool CONSOLE_UART_Parse_Channel( const char* text, HwUartChannel_T* channel_out )
{
    if ( strcmp( text, "ch1" ) == 0 )
    {
        *channel_out = HW_UART_CHANNEL_1;
        return true;
    }

    if ( strcmp( text, "ch2" ) == 0 )
    {
        *channel_out = HW_UART_CHANNEL_2;
        return true;
    }

    return false;
}

static uint32_t CONSOLE_UART_Prng_Next( uint32_t* state )
{
    *state = ( *state * 1664525UL ) + 1013904223UL;
    return *state;
}

static void CONSOLE_UART_Fill_Random_Buffer( uint8_t* buffer, uint32_t length, uint32_t seed )
{
    uint32_t prng_state = seed;

    for ( uint32_t i = 0U; i < length; i++ )
    {
        buffer[i] = ( uint8_t )( CONSOLE_UART_Prng_Next( &prng_state ) >> 24 );
    }
}

static bool CONSOLE_UART_Build_Tx_Text( uint16_t argc, char* argv[], uint16_t first_text_arg,
                                        char* tx_text, uint32_t tx_text_size,
                                        uint32_t* tx_length_out )
{
    uint32_t tx_length = 0U;

    tx_text[0] = '\0';

    for ( uint16_t i = first_text_arg; i < argc; i++ )
    {
        size_t token_len = strlen( argv[i] );
        size_t sep_len   = ( i > first_text_arg ) ? 1U : 0U;

        if ( ( tx_length + sep_len + token_len ) >= tx_text_size )
        {
            CONSOLE_Printf( "Data too long: max %lu bytes\r\n",
                            ( unsigned long )( tx_text_size - 1U ) );
            return false;
        }

        if ( i > first_text_arg )
        {
            tx_text[tx_length] = ' ';
            tx_length++;
        }

        memcpy( &tx_text[tx_length], argv[i], token_len );
        tx_length += ( uint32_t )token_len;
        tx_text[tx_length] = '\0';
    }

    if ( tx_length == 0U )
    {
        CONSOLE_Printf( "No data provided\r\n" );
        return false;
    }

    *tx_length_out = tx_length;
    return true;
}

static void CONSOLE_UART_Clear_Rx_Data( void )
{
    uint8_t  discard_buf[CONSOLE_UART_DISCARD_BUFFER_SIZE];
    uint32_t bytes_read = 0U;
    uint32_t ch1_reads  = 0U;
    uint32_t ch2_reads  = 0U;

    for ( ch1_reads = 0U; ch1_reads < CONSOLE_UART_CLEAR_RX_MAX_READS; ch1_reads++ )
    {
        bytes_read = 0U;

        ( void )EXEC_UART_Read( HW_UART_CHANNEL_1, discard_buf, sizeof( discard_buf ),
                                &bytes_read );

        if ( bytes_read == 0U )
        {
            break;
        }
    }

    for ( ch2_reads = 0U; ch2_reads < CONSOLE_UART_CLEAR_RX_MAX_READS; ch2_reads++ )
    {
        bytes_read = 0U;

        ( void )EXEC_UART_Read( HW_UART_CHANNEL_2, discard_buf, sizeof( discard_buf ),
                                &bytes_read );

        if ( bytes_read == 0U )
        {
            break;
        }
    }

    if ( ch1_reads >= CONSOLE_UART_CLEAR_RX_MAX_READS )
    {
        CONSOLE_Printf( "WARN: ch1 RX clear reached max reads\r\n" );
    }

    if ( ch2_reads >= CONSOLE_UART_CLEAR_RX_MAX_READS )
    {
        CONSOLE_Printf( "WARN: ch2 RX clear reached max reads\r\n" );
    }
}

static uint32_t CONSOLE_UART_Calc_Loopback_Delay_Ms( uint32_t length_bytes )
{
    uint32_t baud_rate = s_uart_loopback_state.baud_rate;

    if ( baud_rate == 0U )
    {
        return CONSOLE_UART_LOOPBACK_SETTLE_DELAY_MS;
    }

    /*
     * Approximate UART frame time for the current fixed loopback framing:
     * 1 start bit + 8 data bits + 1 stop bit = 10 bits per byte.
     *
     * A margin is added to cover scheduler latency, DMA interrupt handling, and
     * RX polling delay.
     */
    uint32_t wire_time_ms = ( ( length_bytes * 10U * 1000U ) + baud_rate - 1U ) / baud_rate;

    return wire_time_ms + CONSOLE_UART_LOOPBACK_DELAY_MARGIN_MS;
}

static bool CONSOLE_UART_Read_Until_Length( HwUartChannel_T receiver_ch, uint8_t* rx_buf,
                                            uint32_t rx_buf_size, uint32_t expected_length,
                                            uint32_t timeout_ms, uint32_t* bytes_read_out )
{
    uint32_t total_read = 0U;
    uint32_t elapsed_ms = 0U;

    while ( ( total_read < expected_length ) && ( elapsed_ms < timeout_ms ) )
    {
        uint32_t bytes_read = 0U;

        if ( !EXEC_UART_Read( receiver_ch, &rx_buf[total_read], rx_buf_size - total_read,
                              &bytes_read ) )
        {
            return false;
        }

        total_read += bytes_read;

        if ( total_read < expected_length )
        {
            vTaskDelay( pdMS_TO_TICKS( CONSOLE_UART_LOOPBACK_READ_POLL_MS ) );
            elapsed_ms += CONSOLE_UART_LOOPBACK_READ_POLL_MS;
        }
    }

    *bytes_read_out = total_read;
    return true;
}

static bool CONSOLE_UART_Read_And_Report_Loopback_Result( HwUartChannel_T receiver_ch,
                                                          const char* tx_text, uint32_t tx_length )
{
    uint8_t  rx_buf[EXEC_UART_MAX_CHUNK_SIZE];
    uint32_t bytes_read = 0U;
    uint32_t timeout_ms =
        CONSOLE_UART_Calc_Loopback_Delay_Ms( tx_length ) + CONSOLE_UART_LOOPBACK_DELAY_MARGIN_MS;

    if ( !CONSOLE_UART_Read_Until_Length( receiver_ch, rx_buf, sizeof( rx_buf ), tx_length,
                                          timeout_ms, &bytes_read ) )
    {
        CONSOLE_Printf( "RX read failed\r\n" );
        return false;
    }

    CONSOLE_Printf( "sent: %s\r\n", tx_text );
    CONSOLE_Printf( "received: " );

    for ( uint32_t i = 0U; i < bytes_read; i++ )
    {
        CONSOLE_Printf( "%c", rx_buf[i] );
    }

    CONSOLE_Printf( "\r\n" );

    if ( ( bytes_read == tx_length ) && ( memcmp( rx_buf, tx_text, tx_length ) == 0 ) )
    {
        CONSOLE_Printf( "PASS\r\n" );
        return true;
    }

    CONSOLE_Printf( "FAIL\r\n" );
    CONSOLE_Printf( "  expected length: %lu\r\n", ( unsigned long )tx_length );
    CONSOLE_Printf( "  received length: %lu\r\n", ( unsigned long )bytes_read );
    return false;
}

static void CONSOLE_UART_Loopback_Configure( uint16_t argc, char* argv[] )
{
    uint32_t baud_rate = 0U;

    if ( argc != 4U )
    {
        CONSOLE_UART_Loopback_Print_Usage();
        return;
    }

    if ( !CONSOLE_UART_Parse_Baud_Rate( argv[3], &baud_rate ) )
    {
        return;
    }

    HwUartConfig_T config = { 0 };
    config.interface_mode = HW_UART_MODE_TTL_3V3;
    config.rx_enabled     = true;
    config.tx_enabled     = true;
    config.baud_rate      = baud_rate;
    config.word_length    = HW_UART_WORD_LENGTH_8_BITS;
    config.parity         = HW_UART_PARITY_NONE;
    config.stop_bits      = HW_UART_STOP_BITS_1;

    if ( !EXEC_UART_Apply_Configuration( HW_UART_CHANNEL_1, &config ) )
    {
        CONSOLE_Printf( "Failed to configure ch1\r\n" );
        return;
    }

    if ( !EXEC_UART_Apply_Configuration( HW_UART_CHANNEL_2, &config ) )
    {
        ( void )EXEC_UART_Deconfigure( HW_UART_CHANNEL_1 );
        CONSOLE_Printf( "Failed to configure ch2\r\n" );
        return;
    }

    s_uart_loopback_state.is_configured = true;
    s_uart_loopback_state.baud_rate     = baud_rate;

    CONSOLE_Printf( "uart loopback configured\r\n" );
    CONSOLE_Printf( "  channels: ch1 + ch2\r\n" );
    CONSOLE_Printf( "  mode: TTL_3V3\r\n" );
    CONSOLE_Printf( "  baud: %lu\r\n", ( unsigned long )baud_rate );
    CONSOLE_Printf( "  framing: 8N1\r\n" );
    CONSOLE_Printf( "  rx/tx: enabled\r\n" );
}

static void CONSOLE_UART_Loopback_Deconfigure( uint16_t argc, char* argv[] )
{
    ( void )argv;

    if ( argc != 3U )
    {
        CONSOLE_UART_Loopback_Print_Usage();
        return;
    }

    bool ch1_ok = EXEC_UART_Deconfigure( HW_UART_CHANNEL_1 );
    bool ch2_ok = EXEC_UART_Deconfigure( HW_UART_CHANNEL_2 );

    s_uart_loopback_state.is_configured = false;
    s_uart_loopback_state.baud_rate     = 0U;

    if ( ch1_ok && ch2_ok )
    {
        CONSOLE_Printf( "uart loopback deconfigured: ch1 + ch2\r\n" );
    }
    else if ( !ch1_ok && !ch2_ok )
    {
        CONSOLE_Printf( "Failed to deconfigure ch1 and ch2\r\n" );
    }
    else if ( !ch1_ok )
    {
        CONSOLE_Printf( "Failed to deconfigure ch1\r\n" );
    }
    else
    {
        CONSOLE_Printf( "Failed to deconfigure ch2\r\n" );
    }
}

static bool CONSOLE_UART_Transmit_Buffer_Chunked( HwUartChannel_T sender_ch, const uint8_t* data,
                                                  uint32_t length, uint32_t chunk_size )
{
    uint32_t offset = 0U;

    while ( offset < length )
    {
        uint32_t remaining  = length - offset;
        uint32_t this_chunk = remaining;

        if ( chunk_size > 0U && this_chunk > chunk_size )
        {
            this_chunk = chunk_size;
        }

        if ( !EXEC_UART_Transmit( sender_ch, &data[offset], this_chunk ) )
        {
            CONSOLE_Printf( "TX failed at offset %lu\r\n", ( unsigned long )offset );
            return false;
        }

        offset += this_chunk;
    }

    return true;
}

static bool CONSOLE_UART_Read_And_Compare_Buffer( HwUartChannel_T receiver_ch,
                                                  const uint8_t* expected, uint32_t expected_length,
                                                  uint32_t iteration )
{
    uint32_t bytes_read = 0U;
    uint32_t timeout_ms = CONSOLE_UART_Calc_Loopback_Delay_Ms( expected_length )
                          + CONSOLE_UART_LOOPBACK_DELAY_MARGIN_MS;

    if ( !CONSOLE_UART_Read_Until_Length( receiver_ch, s_uart_blast_rx_buf,
                                          sizeof( s_uart_blast_rx_buf ), expected_length,
                                          timeout_ms, &bytes_read ) )
    {
        CONSOLE_Printf( "RX read failed\r\n" );
        return false;
    }

    if ( bytes_read != expected_length )
    {
        CONSOLE_Printf( "FAIL\r\n" );
        CONSOLE_Printf( "  iteration: %lu\r\n", ( unsigned long )iteration );
        CONSOLE_Printf( "  expected length: %lu\r\n", ( unsigned long )expected_length );
        CONSOLE_Printf( "  received length: %lu\r\n", ( unsigned long )bytes_read );
        return false;
    }

    for ( uint32_t i = 0U; i < expected_length; i++ )
    {
        if ( s_uart_blast_rx_buf[i] != expected[i] )
        {
            CONSOLE_Printf( "FAIL\r\n" );
            CONSOLE_Printf( "  iteration: %lu\r\n", ( unsigned long )iteration );
            CONSOLE_Printf( "  first mismatch index: %lu\r\n", ( unsigned long )i );
            CONSOLE_Printf( "  expected: 0x%02X\r\n", expected[i] );
            CONSOLE_Printf( "  received: 0x%02X\r\n", s_uart_blast_rx_buf[i] );
            return false;
        }
    }

    return true;
}

static void CONSOLE_UART_Loopback_Blast_Random( uint16_t argc, char* argv[] )
{
    HwUartChannel_T sender_ch;
    HwUartChannel_T receiver_ch;

    uint32_t length     = 0U;
    uint32_t seed       = 0U;
    uint32_t iterations = CONSOLE_UART_BLAST_RANDOM_DEFAULT_ITERATIONS;
    uint32_t chunk_size = CONSOLE_UART_BLAST_RANDOM_DEFAULT_CHUNK_SIZE;

    uint32_t passed = 0U;
    uint32_t failed = 0U;

    if ( argc < 7U || argc > 9U )
    {
        CONSOLE_UART_Loopback_Print_Usage();
        return;
    }

    if ( !s_uart_loopback_state.is_configured )
    {
        CONSOLE_Printf( "uart loopback not configured\r\n" );
        return;
    }

    if ( !CONSOLE_UART_Parse_Channel( argv[3], &sender_ch ) )
    {
        CONSOLE_Printf( "Invalid sender channel: use ch1 or ch2\r\n" );
        return;
    }

    if ( !CONSOLE_UART_Parse_Channel( argv[4], &receiver_ch ) )
    {
        CONSOLE_Printf( "Invalid receiver channel: use ch1 or ch2\r\n" );
        return;
    }

    if ( !CONSOLE_UART_Parse_U32( argv[5], "length", 1U, CONSOLE_UART_BLAST_RANDOM_MAX_LENGTH,
                                  &length ) )
    {
        return;
    }

    if ( !CONSOLE_UART_Parse_U32( argv[6], "seed", 0U, UINT32_MAX, &seed ) )
    {
        return;
    }

    if ( argc >= 8U )
    {
        if ( !CONSOLE_UART_Parse_U32( argv[7], "iterations", 1U,
                                      CONSOLE_UART_BLAST_RANDOM_MAX_ITERATIONS, &iterations ) )
        {
            return;
        }
    }

    if ( argc >= 9U )
    {
        if ( !CONSOLE_UART_Parse_U32( argv[8], "chunk_size", 0U,
                                      CONSOLE_UART_BLAST_RANDOM_MAX_LENGTH, &chunk_size ) )
        {
            return;
        }
    }

    CONSOLE_Printf( "uart loopback blast_random\r\n" );
    CONSOLE_Printf( "  length: %lu\r\n", ( unsigned long )length );
    CONSOLE_Printf( "  seed: %lu\r\n", ( unsigned long )seed );
    CONSOLE_Printf( "  iterations: %lu\r\n", ( unsigned long )iterations );
    CONSOLE_Printf( "  chunk_size: %lu\r\n", ( unsigned long )chunk_size );

    CONSOLE_UART_Clear_Rx_Data();

    for ( uint32_t iteration = 0U; iteration < iterations; iteration++ )
    {
        uint32_t iteration_seed = seed + iteration;

        CONSOLE_UART_Fill_Random_Buffer( s_uart_blast_tx_buf, length, iteration_seed );

        if ( !CONSOLE_UART_Transmit_Buffer_Chunked( sender_ch, s_uart_blast_tx_buf, length,
                                                    chunk_size ) )
        {
            failed++;
            break;
        }

        if ( CONSOLE_UART_Read_And_Compare_Buffer( receiver_ch, s_uart_blast_tx_buf, length,
                                                   iteration ) )
        {
            passed++;
        }
        else
        {
            failed++;
            break;
        }
    }

    if ( failed == 0U )
    {
        CONSOLE_Printf( "PASS\r\n" );
    }
    else
    {
        CONSOLE_Printf( "FAIL\r\n" );
    }

    CONSOLE_Printf( "  passed: %lu\r\n", ( unsigned long )passed );
    CONSOLE_Printf( "  failed: %lu\r\n", ( unsigned long )failed );
    CONSOLE_Printf( "  bytes tested: %lu\r\n", ( unsigned long )( passed * length ) );
}

static void CONSOLE_UART_Loopback_Status( uint16_t argc, char* argv[] )
{
    ( void )argv;

    if ( argc != 3U )
    {
        CONSOLE_UART_Loopback_Print_Usage();
        return;
    }

    if ( !s_uart_loopback_state.is_configured )
    {
        CONSOLE_Printf( "uart loopback: not configured\r\n" );
        return;
    }

    CONSOLE_Printf( "uart loopback: configured\r\n" );
    CONSOLE_Printf( "  channels: ch1 + ch2\r\n" );
    CONSOLE_Printf( "  mode: TTL_3V3\r\n" );
    CONSOLE_Printf( "  baud: %lu\r\n", ( unsigned long )s_uart_loopback_state.baud_rate );
    CONSOLE_Printf( "  framing: 8N1\r\n" );
    CONSOLE_Printf( "  rx/tx: enabled\r\n" );
}

static void CONSOLE_UART_Loopback_Start( uint16_t argc, char* argv[] )
{
    HwUartChannel_T sender_ch;
    HwUartChannel_T receiver_ch;
    char            tx_text[EXEC_UART_MAX_CHUNK_SIZE];
    uint32_t        tx_length = 0U;

    if ( argc < 6U )
    {
        CONSOLE_UART_Loopback_Print_Usage();
        return;
    }

    if ( !s_uart_loopback_state.is_configured )
    {
        CONSOLE_Printf( "uart loopback not configured\r\n" );
        return;
    }

    if ( !CONSOLE_UART_Parse_Channel( argv[3], &sender_ch ) )
    {
        CONSOLE_Printf( "Invalid sender channel: use ch1 or ch2\r\n" );
        return;
    }

    if ( !CONSOLE_UART_Parse_Channel( argv[4], &receiver_ch ) )
    {
        CONSOLE_Printf( "Invalid receiver channel: use ch1 or ch2\r\n" );
        return;
    }

    if ( !CONSOLE_UART_Build_Tx_Text( argc, argv, 5U, tx_text, sizeof( tx_text ), &tx_length ) )
    {
        return;
    }

    CONSOLE_UART_Clear_Rx_Data();

    if ( !EXEC_UART_Transmit( sender_ch, ( const uint8_t* )tx_text, tx_length ) )
    {
        CONSOLE_Printf( "TX failed\r\n" );
        return;
    }

    ( void )CONSOLE_UART_Read_And_Report_Loopback_Result( receiver_ch, tx_text, tx_length );
}

static void CONSOLE_UART_Command_Loopback( uint16_t argc, char* argv[] )
{
    if ( argc < 3U )
    {
        CONSOLE_UART_Loopback_Print_Usage();
        return;
    }

    if ( strcmp( argv[2], "configure" ) == 0 )
    {
        CONSOLE_UART_Loopback_Configure( argc, argv );
        return;
    }

    if ( strcmp( argv[2], "deconfigure" ) == 0 )
    {
        CONSOLE_UART_Loopback_Deconfigure( argc, argv );
        return;
    }

    if ( strcmp( argv[2], "status" ) == 0 )
    {
        CONSOLE_UART_Loopback_Status( argc, argv );
        return;
    }

    if ( strcmp( argv[2], "start" ) == 0 )
    {
        CONSOLE_UART_Loopback_Start( argc, argv );
        return;
    }

    if ( strcmp( argv[2], "blast_random" ) == 0 )
    {
        CONSOLE_UART_Loopback_Blast_Random( argc, argv );
        return;
    }

    CONSOLE_Printf( "Unknown uart loopback command\r\n" );
    CONSOLE_Printf( "Use 'uart loopback' for usage.\r\n" );
}

/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */

void CONSOLE_UART_Command_Handler( uint16_t argc, char* argv[] )
{
    if ( argc < 2U )
    {
        CONSOLE_UART_Print_Usage();
        return;
    }

    if ( strcmp( argv[1], "loopback" ) == 0 )
    {
        CONSOLE_UART_Command_Loopback( argc, argv );
        return;
    }

    CONSOLE_Printf( "Unknown uart command\r\n" );
    CONSOLE_Printf( "Use 'uart' for usage.\r\n" );
}
