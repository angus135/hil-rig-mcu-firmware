/******************************************************************************
 *  File:       console_can.c
 *  Author:     Timothy Vogelsang
 *  Created:    29 Apr 2026
 *
 *  Description:
 *      Diagnostic console commands for configuring, transmitting, and
 *      receiving classical CAN data frames.
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include "console_can.h"

#include "console.h"
#include "exec_can.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */

#define CONSOLE_CAN_BITRATE ( 1000000U )
#define CONSOLE_CAN_MAX_RX_PACKETS ( EXEC_CAN_MAX_BATCH_SIZE )
#define CONSOLE_CAN_MAX_TX_PACKETS ( 4U )

/**-----------------------------------------------------------------------------
 *  Private Function Prototypes
 *------------------------------------------------------------------------------
 */

static void         CONSOLE_CAN_Print_Usage( void );
static bool         CONSOLE_CAN_Parse_Channel( const char* text, EXEC_CAN_Channel_T* channel );
static unsigned int CONSOLE_CAN_Channel_Number( EXEC_CAN_Channel_T channel );
static bool         CONSOLE_CAN_Parse_U16( const char* text, int base, uint16_t min_value,
                                           uint16_t max_value, uint16_t* value );
static void         CONSOLE_Command_Can_tx( uint16_t argc, char* argv[] );
static void         CONSOLE_Command_Can_config( uint16_t argc, char* argv[] );
static void         CONSOLE_Command_Can_start( uint16_t argc, char* argv[] );
static void         CONSOLE_Command_Can_stop( uint16_t argc, char* argv[] );
static void         CONSOLE_Command_Can_rx( uint16_t argc, char* argv[] );

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

static void CONSOLE_CAN_Print_Usage( void )
{
    CONSOLE_Printf( "Usage:\r\n" );
    CONSOLE_Printf( "  can tx <channel> <id> <payload> [<id> <payload> ...]\r\n" );
    CONSOLE_Printf( "  can rx <channel>\r\n" );
    CONSOLE_Printf( "  can config <channel> <filter_bank> <filter_id> <filter_mask>\r\n" );
    CONSOLE_Printf( "  can start <channel>\r\n" );
    CONSOLE_Printf( "  can stop <channel>\r\n" );
    CONSOLE_Printf( "    channel: 1 or 2; payload: 1 to 8 text bytes\r\n" );
    CONSOLE_Printf( "    CAN IDs and masks accept decimal or 0x-prefixed hexadecimal\r\n" );
}

static bool CONSOLE_CAN_Parse_Channel( const char* text, EXEC_CAN_Channel_T* channel )
{
    if ( text == NULL || channel == NULL )
    {
        return false;
    }
    if ( strcmp( text, "1" ) == 0 )
    {
        *channel = EXEC_CAN_CHANNEL_1;
        return true;
    }
    if ( strcmp( text, "2" ) == 0 )
    {
        *channel = EXEC_CAN_CHANNEL_2;
        return true;
    }
    return false;
}

static unsigned int CONSOLE_CAN_Channel_Number( EXEC_CAN_Channel_T channel )
{
    return channel == EXEC_CAN_CHANNEL_1 ? 1U : 2U;
}

static bool CONSOLE_CAN_Parse_U16( const char* text, int base, uint16_t min_value,
                                   uint16_t max_value, uint16_t* value )
{
    if ( text == NULL || value == NULL || text[0] == '\0' )
    {
        return false;
    }

    errno                = 0;
    char*         end    = NULL;
    unsigned long parsed = strtoul( text, &end, base );

    if ( errno == ERANGE || end == text || *end != '\0' || parsed < min_value
         || parsed > max_value )
    {
        return false;
    }

    *value = ( uint16_t )parsed;
    return true;
}

static void CONSOLE_Command_Can_tx( uint16_t argc, char* argv[] )
{
    if ( argc < 5U || ( ( argc - 3U ) % 2U ) != 0U )
    {
        CONSOLE_CAN_Print_Usage();
        return;
    }

    uint16_t packet_count = ( uint16_t )( ( argc - 3U ) / 2U );
    if ( packet_count > CONSOLE_CAN_MAX_TX_PACKETS )
    {
        CONSOLE_Printf( "Too many CAN frames; maximum is %u\r\n",
                        ( unsigned int )CONSOLE_CAN_MAX_TX_PACKETS );
        return;
    }

    EXEC_CAN_Channel_T channel;
    if ( !CONSOLE_CAN_Parse_Channel( argv[2], &channel ) )
    {
        CONSOLE_Printf( "Invalid CAN channel; expected 1 or 2\r\n" );
        return;
    }

    EXEC_CAN_Packet_T packets[CONSOLE_CAN_MAX_TX_PACKETS] = { 0 };
    for ( uint16_t i = 0U; i < packet_count; i++ )
    {
        uint16_t id_index      = ( uint16_t )( 3U + ( i * 2U ) );
        uint16_t payload_index = id_index + 1U;
        uint16_t id            = 0U;

        if ( !CONSOLE_CAN_Parse_U16( argv[id_index], 0, 0U, EXEC_CAN_STANDARD_ID_MAX, &id ) )
        {
            CONSOLE_Printf( "Invalid standard CAN ID\r\n" );
            return;
        }
        if ( argv[payload_index] == NULL )
        {
            CONSOLE_CAN_Print_Usage();
            return;
        }

        size_t payload_length = strlen( argv[payload_index] );
        if ( payload_length == 0U || payload_length > EXEC_CAN_MAX_PAYLOAD_SIZE )
        {
            CONSOLE_Printf( "CAN payload must contain 1 to 8 text bytes\r\n" );
            return;
        }

        packets[i].id  = id;
        packets[i].dlc = ( uint8_t )payload_length;
        memcpy( packets[i].data, argv[payload_index], payload_length );
    }

    EXEC_CAN_Result_T result = EXEC_CAN_Transmit( channel, packets, packet_count );
    if ( result == EXEC_CAN_RESULT_OK )
    {
        CONSOLE_Printf( "Started %u CAN frame(s) on channel %u\r\n", ( unsigned int )packet_count,
                        CONSOLE_CAN_Channel_Number( channel ) );
    }
    else if ( result == EXEC_CAN_RESULT_BUSY )
    {
        CONSOLE_Printf( "CAN channel is busy\r\n" );
    }
    else
    {
        CONSOLE_Printf( "Unable to start CAN batch\r\n" );
    }
}

static void CONSOLE_Command_Can_config( uint16_t argc, char* argv[] )
{
    if ( argc != 6U )
    {
        CONSOLE_CAN_Print_Usage();
        return;
    }

    EXEC_CAN_Channel_T channel;
    if ( !CONSOLE_CAN_Parse_Channel( argv[2], &channel ) )
    {
        CONSOLE_Printf( "Invalid CAN channel; expected 1 or 2\r\n" );
        return;
    }

    uint16_t filter_bank  = 0U;
    uint16_t filter_id    = 0U;
    uint16_t filter_mask  = 0U;
    uint16_t minimum_bank = channel == EXEC_CAN_CHANNEL_1 ? 0U : 14U;
    uint16_t maximum_bank = channel == EXEC_CAN_CHANNEL_1 ? 13U : 27U;
    if ( !CONSOLE_CAN_Parse_U16( argv[3], 10, minimum_bank, maximum_bank, &filter_bank )
         || !CONSOLE_CAN_Parse_U16( argv[4], 0, 0U, EXEC_CAN_STANDARD_ID_MAX, &filter_id )
         || !CONSOLE_CAN_Parse_U16( argv[5], 0, 0U, EXEC_CAN_STANDARD_ID_MAX, &filter_mask ) )
    {
        CONSOLE_Printf( "Invalid CAN configuration values\r\n" );
        CONSOLE_CAN_Print_Usage();
        return;
    }

    EXEC_CAN_Config_T configuration = {
        .is_enabled  = true,
        .bitrate     = CONSOLE_CAN_BITRATE,
        .filter_bank = filter_bank,
        .filter_id   = filter_id,
        .filter_mask = filter_mask,
    };
    EXEC_CAN_Result_T result = EXEC_CAN_Configure_Channel( channel, &configuration );
    if ( result != EXEC_CAN_RESULT_OK )
    {
        CONSOLE_Printf( "CAN%u configuration failed with error %d\r\n",
                        CONSOLE_CAN_Channel_Number( channel ), result );
        return;
    }

    CONSOLE_Printf( "CAN%u configured\r\n", CONSOLE_CAN_Channel_Number( channel ) );
}

static void CONSOLE_Command_Can_start( uint16_t argc, char* argv[] )
{
    EXEC_CAN_Channel_T channel;
    if ( argc != 3U || !CONSOLE_CAN_Parse_Channel( argv[2], &channel ) )
    {
        CONSOLE_CAN_Print_Usage();
        return;
    }

    EXEC_CAN_Result_T result = EXEC_CAN_Start_Channel( channel );
    if ( result == EXEC_CAN_RESULT_OK )
    {
        CONSOLE_Printf( "CAN%u started\r\n", CONSOLE_CAN_Channel_Number( channel ) );
    }
    else
    {
        CONSOLE_Printf( "CAN%u start failed with error %d\r\n",
                        CONSOLE_CAN_Channel_Number( channel ), result );
    }
}

static void CONSOLE_Command_Can_stop( uint16_t argc, char* argv[] )
{
    EXEC_CAN_Channel_T channel;
    if ( argc != 3U || !CONSOLE_CAN_Parse_Channel( argv[2], &channel ) )
    {
        CONSOLE_CAN_Print_Usage();
        return;
    }

    EXEC_CAN_Result_T result = EXEC_CAN_Stop_Channel( channel );
    if ( result == EXEC_CAN_RESULT_OK )
    {
        CONSOLE_Printf( "CAN%u stopped\r\n", CONSOLE_CAN_Channel_Number( channel ) );
    }
    else
    {
        CONSOLE_Printf( "CAN%u stop failed with error %d\r\n",
                        CONSOLE_CAN_Channel_Number( channel ), result );
    }
}

static void CONSOLE_Command_Can_rx( uint16_t argc, char* argv[] )
{
    if ( argc != 3U )
    {
        CONSOLE_CAN_Print_Usage();
        return;
    }

    EXEC_CAN_Channel_T channel;
    if ( !CONSOLE_CAN_Parse_Channel( argv[2], &channel ) )
    {
        CONSOLE_Printf( "Invalid CAN channel; expected 1 or 2\r\n" );
        return;
    }

    EXEC_CAN_Packet_T packets[CONSOLE_CAN_MAX_RX_PACKETS] = { 0 };
    uint16_t          read                                = 0U;
    EXEC_CAN_Result_T result =
        EXEC_CAN_Receive( channel, packets, CONSOLE_CAN_MAX_RX_PACKETS, &read );
    if ( result != EXEC_CAN_RESULT_OK )
    {
        CONSOLE_Printf( "Unable to read CAN channel %u\r\n",
                        CONSOLE_CAN_Channel_Number( channel ) );
        return;
    }

    uint32_t dropped = EXEC_CAN_Get_Rx_Dropped_Count( channel );

    if ( dropped != 0U )
    {
        CONSOLE_Printf( "Warning: CAN channel %u dropped %lu received frame(s)\r\n",
                        CONSOLE_CAN_Channel_Number( channel ), ( unsigned long )dropped );
    }
    if ( read == 0U )
    {
        CONSOLE_Printf( "Nothing in channel %u buffer\r\n", CONSOLE_CAN_Channel_Number( channel ) );
        return;
    }

    for ( uint16_t i = 0U; i < read; i++ )
    {
        uint8_t payload_length = packets[i].dlc <= EXEC_CAN_MAX_PAYLOAD_SIZE
                                     ? packets[i].dlc
                                     : EXEC_CAN_MAX_PAYLOAD_SIZE;
        CONSOLE_Printf( "Received id: 0x%03X, dlc: %u, data:", ( unsigned int )packets[i].id,
                        ( unsigned int )packets[i].dlc );
        for ( uint8_t j = 0U; j < payload_length; j++ )
        {
            CONSOLE_Printf( " %02X", ( unsigned int )packets[i].data[j] );
        }
        CONSOLE_Printf( "\r\n" );
    }
}

/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */

void CONSOLE_CAN_Command_Handler( uint16_t argc, char* argv[] )
{
    if ( argv == NULL || argc < 2U || argv[1] == NULL )
    {
        CONSOLE_CAN_Print_Usage();
        return;
    }

    if ( strcmp( argv[1], "tx" ) == 0 )
    {
        CONSOLE_Command_Can_tx( argc, argv );
    }
    else if ( strcmp( argv[1], "rx" ) == 0 )
    {
        CONSOLE_Command_Can_rx( argc, argv );
    }
    else if ( strcmp( argv[1], "config" ) == 0 )
    {
        CONSOLE_Command_Can_config( argc, argv );
    }
    else if ( strcmp( argv[1], "start" ) == 0 )
    {
        CONSOLE_Command_Can_start( argc, argv );
    }
    else if ( strcmp( argv[1], "stop" ) == 0 )
    {
        CONSOLE_Command_Can_stop( argc, argv );
    }
    else
    {
        CONSOLE_Printf( "Unknown CAN command: %s\r\n", argv[1] );
        CONSOLE_CAN_Print_Usage();
    }
}
