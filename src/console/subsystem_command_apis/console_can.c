/******************************************************************************
 *  File:       console_can.c
 *  Author:     HIL-RIG Firmware Team
 *  Created:    19-Jul-2026
 *
 *  Description:
 *      Console command API for the DUT-facing classic-CAN controllers.
 *
 *      This module owns parsing, validation and presentation for the top-level
 *      "can" namespace. It translates human-readable command arguments into
 *      complete HwCanFrame_T and HwCanConfig_T values, then delegates all driver
 *      lifecycle and data movement to EXEC_CAN.
 *
 *  Notes:
 *      Console commands run in task context. No parsing, formatting, allocation
 *      or blocking wait is introduced into the CAN ISR or 100 us scheduler path.
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include "command_helpers.h"

#include "console.h"
#include "exec_can.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */

// ISO classic CAN permits at most 1 Mbit/s and eight data bytes in one frame.
#define CONSOLE_CAN_MAX_BITRATE 1000000U
#define CONSOLE_CAN_MAX_RX_FRAMES_PER_COMMAND 8U

// Named argument positions mirror the UART console namespace and make each
// command's grammar visible without scattering numeric array indices.
#define CONSOLE_CAN_ARGV_SUBCOMMAND 1U
#define CONSOLE_CAN_ARGV_CHANNEL 2U
#define CONSOLE_CAN_ARGV_PARAM_1 3U
#define CONSOLE_CAN_ARGV_PARAM_2 4U
#define CONSOLE_CAN_ARGV_PARAM_3 5U
#define CONSOLE_CAN_ARGV_PARAM_4 6U
#define CONSOLE_CAN_ARGV_PARAM_5 7U
#define CONSOLE_CAN_ARGV_PARAM_6 8U
#define CONSOLE_CAN_ARGV_PARAM_7 9U

/**-----------------------------------------------------------------------------
 *  Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

/**
 * @brief Maps one word in the "can" namespace to its command implementation.
 */
typedef struct ConsoleCanSubcommand_T
{
    const char* name;
    void ( *handler )( uint16_t argc, char* argv[] );
} ConsoleCanSubcommand_T;

/**-----------------------------------------------------------------------------
 *  Private Function Prototypes
 *------------------------------------------------------------------------------
 */

static void CONSOLE_CAN_Print_Usage( void );
static void CONSOLE_CAN_Command_Configure( uint16_t argc, char* argv[] );
static void CONSOLE_CAN_Command_Deconfigure( uint16_t argc, char* argv[] );
static void CONSOLE_CAN_Command_Transmit( uint16_t argc, char* argv[] );
static void CONSOLE_CAN_Command_Remote_Request( uint16_t argc, char* argv[] );
static void CONSOLE_CAN_Command_Receive( uint16_t argc, char* argv[] );
static void CONSOLE_CAN_Command_Status( uint16_t argc, char* argv[] );
static void CONSOLE_CAN_Command_Recover( uint16_t argc, char* argv[] );

static bool CONSOLE_CAN_Parse_Channel( const char* text, HwCanChannel_T* channel );
static bool CONSOLE_CAN_Parse_Id_Format( const char* text, bool* is_extended_id );
static bool CONSOLE_CAN_Parse_Mode( const char* text, HwCanMode_T* mode );
static bool CONSOLE_CAN_Parse_Retry_Policy( const char* text, bool* automatic_retransmission );
static bool CONSOLE_CAN_Parse_Tx_Priority( const char* text, HwCanTxPriority_T* priority );
static bool CONSOLE_CAN_Parse_U32( const char* text, const char* field_name, int base,
                                   uint32_t minimum, uint32_t maximum, uint32_t* value );

static const char* CONSOLE_CAN_Result_To_String( HwCanResult_T result );
static const char* CONSOLE_CAN_State_To_String( HwCanState_T state );
static const char* CONSOLE_CAN_Last_Error_To_String( HwCanLastError_T error );
static const char* CONSOLE_CAN_Channel_To_String( HwCanChannel_T channel );

static void CONSOLE_CAN_Print_Frame( const HwCanRxFrame_T* received_frame );
static void CONSOLE_CAN_Print_Latched_Faults( HwCanFaultFlags_T faults );

/**-----------------------------------------------------------------------------
 *  Private Variables: Dispatch Table
 *------------------------------------------------------------------------------
 */

static const ConsoleCanSubcommand_T CAN_SUBCOMMANDS[] = {
    { "configure", CONSOLE_CAN_Command_Configure },
    { "deconfigure", CONSOLE_CAN_Command_Deconfigure },
    { "tx", CONSOLE_CAN_Command_Transmit },
    { "rtr", CONSOLE_CAN_Command_Remote_Request },
    { "rx", CONSOLE_CAN_Command_Receive },
    { "status", CONSOLE_CAN_Command_Status },
    { "recover", CONSOLE_CAN_Command_Recover },
};

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

/**
 * @brief Print complete usage for the classic-CAN console namespace.
 *
 * CAN is a broadcast bus rather than a point-to-point connection. The identifier
 * supplied to 'tx' or 'rtr' labels a frame, participates in arbitration and is
 * used by receiver filters; it is not a node address. Standard identifiers are
 * 11 bits wide, while extended identifiers are 29 bits wide.
 *
 * 'tx' creates a data frame and infers its Data Length Code (DLC) from the number
 * of byte arguments. 'rtr' creates a remote-request frame, for which the DLC is
 * the requested response length and no payload bytes are transmitted.
 */
static void CONSOLE_CAN_Print_Usage( void )
{
    CONSOLE_Printf( "Usage:\r\n" );
    CONSOLE_Printf( "  can help\r\n" );
    CONSOLE_Printf( "  can configure <ch1|ch2> <bitrate> [mode] [retry|one_shot] "
                    "[request_order|identifier_priority]\r\n" );
    CONSOLE_Printf( "                [accept_all|accept_none|<std|ext> <id> <mask>]\r\n" );
    CONSOLE_Printf( "  can deconfigure <ch1|ch2>\r\n" );
    CONSOLE_Printf( "  can tx <ch1|ch2> <std|ext> <id> [byte0 ... byte7]\r\n" );
    CONSOLE_Printf( "  can rtr <ch1|ch2> <std|ext> <id> <dlc>\r\n" );
    CONSOLE_Printf( "  can rx <ch1|ch2> [frame_count]\r\n" );
    CONSOLE_Printf( "  can status <ch1|ch2>\r\n" );
    CONSOLE_Printf( "  can recover <ch1|ch2>\r\n" );
    CONSOLE_Printf( "Modes: normal, loopback, silent, silent_loopback\r\n" );
    CONSOLE_Printf(
        "IDs may be decimal or 0x-prefixed hexadecimal; data bytes are hexadecimal.\r\n" );
    CONSOLE_Printf(
        "A successful tx/rtr response means queued, not acknowledged on the CAN bus.\r\n" );
}

/**
 * @brief Parse a bounded unsigned integer without accepting partial input.
 *
 * strtoul() by itself accepts a numeric prefix followed by arbitrary text and
 * also accepts a leading minus sign. Neither behaviour is appropriate for a
 * hardware configuration command. This helper therefore checks the entire token,
 * rejects negative input and applies the caller's inclusive range before the
 * value is narrowed to 32 bits.
 *
 * @param text Console token to parse.
 * @param field_name Human-readable field name used in validation messages.
 * @param base Numeric base passed to strtoul. Zero selects decimal unless the
 * token has an explicit 0x hexadecimal prefix; implicit octal is not accepted.
 * @param minimum Smallest accepted value.
 * @param maximum Largest accepted value.
 * @param value Receives the validated value.
 *
 * @return true when the complete token is valid and in range.
 */
static bool CONSOLE_CAN_Parse_U32( const char* text, const char* field_name, int base,
                                   uint32_t minimum, uint32_t maximum, uint32_t* value )
{
    if ( text == NULL || value == NULL || text[0] == '\0' || text[0] == '-' )
    {
        CONSOLE_Printf( "Invalid %s\r\n", field_name );
        return false;
    }

    int parse_base = base;
    if ( parse_base == 0 )
    {
        // CAN IDs accept ordinary decimal or explicit hexadecimal. Avoid base
        // zero's surprising interpretation of a leading zero as octal.
        parse_base = 10;
        if ( text[0] == '0' && ( text[1] == 'x' || text[1] == 'X' ) )
        {
            parse_base = 16;
        }
    }

    char*         end_ptr = NULL;
    unsigned long parsed  = strtoul( text, &end_ptr, parse_base );

    if ( end_ptr == text || *end_ptr != '\0' || parsed < ( unsigned long )minimum
         || parsed > ( unsigned long )maximum )
    {
        CONSOLE_Printf( "Invalid %s: expected %lu to %lu\r\n", field_name, ( unsigned long )minimum,
                        ( unsigned long )maximum );
        return false;
    }

    *value = ( uint32_t )parsed;
    return true;
}

/**
 * @brief Convert a console channel token to the driver's channel enum.
 *
 * Both the descriptive ch1/ch2 form used by UART and the shorter 1/2 aliases are
 * accepted. Internally the enum starts at zero, so callers must never cast the
 * printed channel number directly to HwCanChannel_T.
 */
static bool CONSOLE_CAN_Parse_Channel( const char* text, HwCanChannel_T* channel )
{
    if ( text == NULL || channel == NULL )
    {
        return false;
    }

    if ( strcmp( text, "ch1" ) == 0 || strcmp( text, "1" ) == 0 )
    {
        *channel = HW_CAN_CHANNEL_1;
        return true;
    }

    if ( strcmp( text, "ch2" ) == 0 || strcmp( text, "2" ) == 0 )
    {
        *channel = HW_CAN_CHANNEL_2;
        return true;
    }

    CONSOLE_Printf( "Invalid CAN channel: expected ch1 or ch2\r\n" );
    return false;
}

/**
 * @brief Parse whether a frame uses an 11-bit or 29-bit identifier.
 */
static bool CONSOLE_CAN_Parse_Id_Format( const char* text, bool* is_extended_id )
{
    if ( text == NULL || is_extended_id == NULL )
    {
        return false;
    }

    if ( strcmp( text, "std" ) == 0 )
    {
        *is_extended_id = false;
        return true;
    }

    if ( strcmp( text, "ext" ) == 0 )
    {
        *is_extended_id = true;
        return true;
    }

    CONSOLE_Printf( "Invalid identifier format: expected std or ext\r\n" );
    return false;
}

/**
 * @brief Parse one of the four operating modes supported by STM32 bxCAN.
 */
static bool CONSOLE_CAN_Parse_Mode( const char* text, HwCanMode_T* mode )
{
    if ( text == NULL || mode == NULL )
    {
        return false;
    }

    if ( strcmp( text, "normal" ) == 0 )
    {
        *mode = HW_CAN_MODE_NORMAL;
        return true;
    }

    if ( strcmp( text, "loopback" ) == 0 )
    {
        *mode = HW_CAN_MODE_LOOPBACK;
        return true;
    }

    if ( strcmp( text, "silent" ) == 0 )
    {
        *mode = HW_CAN_MODE_SILENT;
        return true;
    }

    if ( strcmp( text, "silent_loopback" ) == 0 )
    {
        *mode = HW_CAN_MODE_SILENT_LOOPBACK;
        return true;
    }

    CONSOLE_Printf( "Invalid CAN mode: expected normal, loopback, silent or silent_loopback\r\n" );
    return false;
}

/**
 * @brief Parse whether bxCAN should retry a frame after a failed bus attempt.
 *
 * In retry mode the peripheral keeps requesting transmission until it succeeds,
 * is aborted, or enters bus-off. One-shot mode attempts the frame once and makes
 * a missing acknowledgement or other transmission error observable immediately.
 */
static bool CONSOLE_CAN_Parse_Retry_Policy( const char* text, bool* automatic_retransmission )
{
    if ( text == NULL || automatic_retransmission == NULL )
    {
        return false;
    }

    if ( strcmp( text, "retry" ) == 0 )
    {
        *automatic_retransmission = true;
        return true;
    }

    if ( strcmp( text, "one_shot" ) == 0 )
    {
        *automatic_retransmission = false;
        return true;
    }

    CONSOLE_Printf( "Invalid retry policy: expected retry or one_shot\r\n" );
    return false;
}

/**
 * @brief Parse local priority between multiple frames already in bxCAN mailboxes.
 *
 * Request order preserves software queue order. Identifier priority allows the
 * lower CAN identifier to transmit first, matching normal CAN bus arbitration,
 * even when that frame entered a mailbox later.
 */
static bool CONSOLE_CAN_Parse_Tx_Priority( const char* text, HwCanTxPriority_T* priority )
{
    if ( text == NULL || priority == NULL )
    {
        return false;
    }

    if ( strcmp( text, "request_order" ) == 0 )
    {
        *priority = HW_CAN_TX_PRIORITY_REQUEST_ORDER;
        return true;
    }

    if ( strcmp( text, "identifier_priority" ) == 0 )
    {
        *priority = HW_CAN_TX_PRIORITY_IDENTIFIER;
        return true;
    }

    CONSOLE_Printf( "Invalid TX priority: expected request_order or identifier_priority\r\n" );
    return false;
}

/**
 * @brief Return a stable printable name for one execution-layer result.
 */
static const char* CONSOLE_CAN_Result_To_String( HwCanResult_T result )
{
    switch ( result )
    {
        case HW_CAN_RESULT_OK:
            return "ok";
        case HW_CAN_RESULT_INVALID_CHANNEL:
            return "invalid channel";
        case HW_CAN_RESULT_INVALID_ARGUMENT:
            return "invalid argument";
        case HW_CAN_RESULT_NOT_CONFIGURED:
            return "not configured";
        case HW_CAN_RESULT_NOT_STARTED:
            return "not started";
        case HW_CAN_RESULT_ALREADY_STARTED:
            return "already started";
        case HW_CAN_RESULT_UNSUPPORTED_TIMING:
            return "unsupported bit timing";
        case HW_CAN_RESULT_FILTER_ERROR:
            return "filter configuration error";
        case HW_CAN_RESULT_QUEUE_FULL:
            return "transmit queue full";
        case HW_CAN_RESULT_BUS_OFF:
            return "bus off";
        case HW_CAN_RESULT_BUSY:
            return "busy";
        case HW_CAN_RESULT_HARDWARE_ERROR:
            return "hardware error";
        default:
            return "unknown result";
    }
}

/**
 * @brief Return a printable name for the low-level lifecycle state.
 */
static const char* CONSOLE_CAN_State_To_String( HwCanState_T state )
{
    switch ( state )
    {
        case HW_CAN_STATE_UNCONFIGURED:
            return "unconfigured";
        case HW_CAN_STATE_CONFIGURED:
            return "configured";
        case HW_CAN_STATE_ACTIVE:
            return "active";
        case HW_CAN_STATE_FAULT:
            return "fault";
        default:
            return "unknown";
    }
}

/**
 * @brief Return a printable description of the last bxCAN protocol error.
 */
static const char* CONSOLE_CAN_Last_Error_To_String( HwCanLastError_T error )
{
    switch ( error )
    {
        case HW_CAN_LAST_ERROR_NONE:
            return "none";
        case HW_CAN_LAST_ERROR_STUFF:
            return "bit stuffing";
        case HW_CAN_LAST_ERROR_FORM:
            return "frame format";
        case HW_CAN_LAST_ERROR_ACKNOWLEDGEMENT:
            return "missing acknowledgement";
        case HW_CAN_LAST_ERROR_BIT_RECESSIVE:
            return "recessive bit";
        case HW_CAN_LAST_ERROR_BIT_DOMINANT:
            return "dominant bit";
        case HW_CAN_LAST_ERROR_CRC:
            return "CRC";
        case HW_CAN_LAST_ERROR_SOFTWARE:
            return "software";
        default:
            return "unknown";
    }
}

/**
 * @brief Return the user-facing name for a validated CAN channel.
 */
static const char* CONSOLE_CAN_Channel_To_String( HwCanChannel_T channel )
{
    return channel == HW_CAN_CHANNEL_1 ? "ch1" : "ch2";
}

/**
 * @brief Configure and start one CAN channel through the execution layer.
 *
 * Configuration is intentionally performed in console task context, never in
 * the 100 us scheduler ISR. Defaults are normal mode, automatic retry, request-
 * order mailbox priority, an 80 percent sample point, SJW of one time quantum,
 * automatic bus-off recovery, and an explicit accept-all receive filter.
 *
 * The optional mask-filter form compares the selected standard or extended
 * identifier bits. A one in the mask means that identifier bit must match; a
 * zero makes that bit irrelevant. The driver always compares the identifier
 * format, so a standard filter cannot accidentally accept an extended frame.
 *
 * Syntax:
 * can configure <channel> <bitrate> [mode] [retry|one_shot]
 *               [request_order|identifier_priority]
 *               [accept_all|accept_none|<std|ext> <id> <mask>]
 */
static void CONSOLE_CAN_Command_Configure( uint16_t argc, char* argv[] )
{
    if ( argc < 4U || argc > 10U || argc == 9U )
    {
        CONSOLE_CAN_Print_Usage();
        return;
    }

    HwCanChannel_T channel;
    uint32_t       bitrate = 0U;

    if ( !CONSOLE_CAN_Parse_Channel( argv[CONSOLE_CAN_ARGV_CHANNEL], &channel )
         || !CONSOLE_CAN_Parse_U32( argv[CONSOLE_CAN_ARGV_PARAM_1], "CAN bitrate", 10, 1U,
                                    CONSOLE_CAN_MAX_BITRATE, &bitrate ) )
    {
        return;
    }

    HwCanFilter_T filter                  = { 0 };
    HwCanConfig_T config                  = { 0 };
    const char*   filter_description      = "accept_all";
    const char*   tx_priority_description = "request_order";

    config.bitrate                    = bitrate;
    config.sample_point_permill       = HW_CAN_DEFAULT_SAMPLE_POINT_PERMILL;
    config.sync_jump_width_tq         = 1U;
    config.mode                       = HW_CAN_MODE_NORMAL;
    config.tx_priority                = HW_CAN_TX_PRIORITY_REQUEST_ORDER;
    config.filter_policy              = HW_CAN_FILTER_ACCEPT_ALL;
    config.filters                    = NULL;
    config.filter_count               = 0U;
    config.automatic_retransmission   = true;
    config.automatic_bus_off_recovery = true;
    config.automatic_wake_up          = false;
    config.receive_fifo_locked        = false;

    if ( argc >= 5U && !CONSOLE_CAN_Parse_Mode( argv[CONSOLE_CAN_ARGV_PARAM_2], &config.mode ) )
    {
        return;
    }

    if ( argc >= 6U
         && !CONSOLE_CAN_Parse_Retry_Policy( argv[CONSOLE_CAN_ARGV_PARAM_3],
                                             &config.automatic_retransmission ) )
    {
        return;
    }

    if ( argc >= 7U
         && !CONSOLE_CAN_Parse_Tx_Priority( argv[CONSOLE_CAN_ARGV_PARAM_4], &config.tx_priority ) )
    {
        return;
    }

    if ( config.tx_priority == HW_CAN_TX_PRIORITY_IDENTIFIER )
    {
        tx_priority_description = "identifier_priority";
    }

    if ( argc >= 8U )
    {
        const char* filter_selector = argv[CONSOLE_CAN_ARGV_PARAM_5];

        if ( strcmp( filter_selector, "accept_all" ) == 0 && argc == 8U )
        {
            config.filter_policy = HW_CAN_FILTER_ACCEPT_ALL;
        }
        else if ( strcmp( filter_selector, "accept_none" ) == 0 && argc == 8U )
        {
            config.filter_policy = HW_CAN_FILTER_ACCEPT_NONE;
            filter_description   = "accept_none";
        }
        else if ( ( strcmp( filter_selector, "std" ) == 0 || strcmp( filter_selector, "ext" ) == 0 )
                  && argc == 10U )
        {
            bool     is_extended_id = strcmp( filter_selector, "ext" ) == 0;
            uint32_t identifier_max =
                is_extended_id ? HW_CAN_EXTENDED_IDENTIFIER_MAX : HW_CAN_STANDARD_IDENTIFIER_MAX;
            uint32_t identifier      = 0U;
            uint32_t identifier_mask = 0U;

            if ( !CONSOLE_CAN_Parse_U32( argv[CONSOLE_CAN_ARGV_PARAM_6], "filter identifier", 0, 0U,
                                         identifier_max, &identifier )
                 || !CONSOLE_CAN_Parse_U32( argv[CONSOLE_CAN_ARGV_PARAM_7],
                                            "filter identifier mask", 0, 0U, identifier_max,
                                            &identifier_mask ) )
            {
                return;
            }

            filter.mode                  = HW_CAN_FILTER_MODE_MASK;
            filter.first.identifier      = identifier;
            filter.first.is_extended_id  = is_extended_id;
            filter.first.is_remote_frame = false;
            filter.identifier_mask       = identifier_mask;
            filter.match_remote_frame    = false;

            config.filter_policy = HW_CAN_FILTER_CONFIGURED;
            config.filters       = &filter;
            config.filter_count  = 1U;
            filter_description   = is_extended_id ? "extended_mask" : "standard_mask";
        }
        else
        {
            CONSOLE_Printf( "Invalid filter: expected accept_all, accept_none, std <id> <mask>, "
                            "or ext <id> <mask>\r\n" );
            return;
        }
    }

    HwCanResult_T result = EXEC_CAN_Configure_Channel( channel, &config );
    if ( result != HW_CAN_RESULT_OK )
    {
        CONSOLE_Printf( "Failed to configure %s: %s\r\n", CONSOLE_CAN_Channel_To_String( channel ),
                        CONSOLE_CAN_Result_To_String( result ) );
        return;
    }

    HwCanStatus_T status = { 0 };
    result               = EXEC_CAN_Get_Status( channel, &status );

    if ( result == HW_CAN_RESULT_OK )
    {
        CONSOLE_Printf( "%s configured and active: requested=%lu actual=%lu bit/s\r\n",
                        CONSOLE_CAN_Channel_To_String( channel ),
                        ( unsigned long )status.requested_bitrate,
                        ( unsigned long )status.actual_bitrate );
        CONSOLE_Printf( "  sample=%u.%u%% error=%u ppm filter=%s retry=%s priority=%s\r\n",
                        ( unsigned int )( status.actual_sample_point_permill / 10U ),
                        ( unsigned int )( status.actual_sample_point_permill % 10U ),
                        ( unsigned int )status.bitrate_error_ppm, filter_description,
                        config.automatic_retransmission ? "automatic" : "one_shot",
                        tx_priority_description );
    }
    else
    {
        // Configuration succeeded even if a later diagnostic snapshot is unavailable.
        CONSOLE_Printf( "%s configured and active at requested bitrate %lu bit/s\r\n",
                        CONSOLE_CAN_Channel_To_String( channel ), ( unsigned long )bitrate );
    }
}

/**
 * @brief Stop one channel and discard its queued frames and stored configuration.
 */
static void CONSOLE_CAN_Command_Deconfigure( uint16_t argc, char* argv[] )
{
    if ( argc != 3U )
    {
        CONSOLE_CAN_Print_Usage();
        return;
    }

    HwCanChannel_T channel;
    if ( !CONSOLE_CAN_Parse_Channel( argv[CONSOLE_CAN_ARGV_CHANNEL], &channel ) )
    {
        return;
    }

    HwCanResult_T result = EXEC_CAN_Deconfigure_Channel( channel );
    if ( result == HW_CAN_RESULT_OK )
    {
        CONSOLE_Printf( "%s deconfigured\r\n", CONSOLE_CAN_Channel_To_String( channel ) );
    }
    else
    {
        CONSOLE_Printf( "Failed to deconfigure %s: %s\r\n",
                        CONSOLE_CAN_Channel_To_String( channel ),
                        CONSOLE_CAN_Result_To_String( result ) );
    }
}

/**
 * @brief Build and asynchronously queue one classic-CAN data frame.
 *
 * The number of data-byte arguments is the DLC, including zero for a valid empty
 * data frame. Every byte is parsed before EXEC_CAN is called, so malformed input
 * cannot leave a partially constructed frame in the transmit queue.
 */
static void CONSOLE_CAN_Command_Transmit( uint16_t argc, char* argv[] )
{
    if ( argc < 5U || argc > ( 5U + HW_CAN_CLASSIC_MAX_DATA_BYTES ) )
    {
        CONSOLE_CAN_Print_Usage();
        return;
    }

    HwCanChannel_T channel;
    bool           is_extended_id = false;

    if ( !CONSOLE_CAN_Parse_Channel( argv[CONSOLE_CAN_ARGV_CHANNEL], &channel )
         || !CONSOLE_CAN_Parse_Id_Format( argv[CONSOLE_CAN_ARGV_PARAM_1], &is_extended_id ) )
    {
        return;
    }

    uint32_t identifier = 0U;
    uint32_t identifier_max =
        is_extended_id ? HW_CAN_EXTENDED_IDENTIFIER_MAX : HW_CAN_STANDARD_IDENTIFIER_MAX;
    if ( !CONSOLE_CAN_Parse_U32( argv[CONSOLE_CAN_ARGV_PARAM_2], "CAN identifier", 0, 0U,
                                 identifier_max, &identifier ) )
    {
        return;
    }

    HwCanFrame_T frame    = { 0 };
    frame.identifier      = identifier;
    frame.dlc             = ( uint8_t )( argc - 5U );
    frame.is_extended_id  = is_extended_id;
    frame.is_remote_frame = false;

    for ( uint32_t byte_index = 0U; byte_index < ( uint32_t )frame.dlc; byte_index++ )
    {
        uint32_t byte_value = 0U;
        if ( !CONSOLE_CAN_Parse_U32( argv[CONSOLE_CAN_ARGV_PARAM_3 + byte_index], "CAN data byte",
                                     16, 0U, 0xFFU, &byte_value ) )
        {
            return;
        }

        frame.data[byte_index] = ( uint8_t )byte_value;
    }

    HwCanResult_T result = EXEC_CAN_Transmit( channel, &frame, 1U );
    if ( result == HW_CAN_RESULT_BUS_OFF )
    {
        CONSOLE_Printf( "Queued data frame on %s, but the controller is bus-off.\r\n",
                        CONSOLE_CAN_Channel_To_String( channel ) );
        CONSOLE_Printf( "The frame remains queued; use 'can recover %s' and do not requeue it.\r\n",
                        CONSOLE_CAN_Channel_To_String( channel ) );
        return;
    }

    if ( result != HW_CAN_RESULT_OK )
    {
        CONSOLE_Printf( "Failed to queue CAN data frame on %s: %s\r\n",
                        CONSOLE_CAN_Channel_To_String( channel ),
                        CONSOLE_CAN_Result_To_String( result ) );
        return;
    }

    CONSOLE_Printf( "Queued data frame on %s: %s ID=0x%lX DLC=%u\r\n",
                    CONSOLE_CAN_Channel_To_String( channel ),
                    is_extended_id ? "extended" : "standard", ( unsigned long )identifier,
                    ( unsigned int )frame.dlc );
    CONSOLE_Printf( "Transmission is asynchronous; use 'can status %s' for completion/errors.\r\n",
                    CONSOLE_CAN_Channel_To_String( channel ) );
}

/**
 * @brief Build and asynchronously queue one classic-CAN remote-request frame.
 *
 * A remote frame carries no data bytes. Its DLC asks another node for a data
 * frame of the same identifier and requested length. Remote frames remain part
 * of classic CAN but are uncommon in newer higher-level protocols.
 */
static void CONSOLE_CAN_Command_Remote_Request( uint16_t argc, char* argv[] )
{
    if ( argc != 6U )
    {
        CONSOLE_CAN_Print_Usage();
        return;
    }

    HwCanChannel_T channel;
    bool           is_extended_id = false;

    if ( !CONSOLE_CAN_Parse_Channel( argv[CONSOLE_CAN_ARGV_CHANNEL], &channel )
         || !CONSOLE_CAN_Parse_Id_Format( argv[CONSOLE_CAN_ARGV_PARAM_1], &is_extended_id ) )
    {
        return;
    }

    uint32_t identifier = 0U;
    uint32_t identifier_max =
        is_extended_id ? HW_CAN_EXTENDED_IDENTIFIER_MAX : HW_CAN_STANDARD_IDENTIFIER_MAX;
    uint32_t dlc = 0U;

    if ( !CONSOLE_CAN_Parse_U32( argv[CONSOLE_CAN_ARGV_PARAM_2], "CAN identifier", 0, 0U,
                                 identifier_max, &identifier )
         || !CONSOLE_CAN_Parse_U32( argv[CONSOLE_CAN_ARGV_PARAM_3], "CAN DLC", 10, 0U,
                                    HW_CAN_CLASSIC_MAX_DATA_BYTES, &dlc ) )
    {
        return;
    }

    HwCanFrame_T frame    = { 0 };
    frame.identifier      = identifier;
    frame.dlc             = ( uint8_t )dlc;
    frame.is_extended_id  = is_extended_id;
    frame.is_remote_frame = true;

    HwCanResult_T result = EXEC_CAN_Transmit( channel, &frame, 1U );
    if ( result == HW_CAN_RESULT_BUS_OFF )
    {
        CONSOLE_Printf( "Queued remote frame on %s, but the controller is bus-off.\r\n",
                        CONSOLE_CAN_Channel_To_String( channel ) );
        CONSOLE_Printf( "The frame remains queued; use 'can recover %s' and do not requeue it.\r\n",
                        CONSOLE_CAN_Channel_To_String( channel ) );
        return;
    }

    if ( result != HW_CAN_RESULT_OK )
    {
        CONSOLE_Printf( "Failed to queue CAN remote frame on %s: %s\r\n",
                        CONSOLE_CAN_Channel_To_String( channel ),
                        CONSOLE_CAN_Result_To_String( result ) );
        return;
    }

    CONSOLE_Printf( "Queued remote frame on %s: %s ID=0x%lX requested DLC=%u\r\n",
                    CONSOLE_CAN_Channel_To_String( channel ),
                    is_extended_id ? "extended" : "standard", ( unsigned long )identifier,
                    ( unsigned int )frame.dlc );
    CONSOLE_Printf( "Transmission is asynchronous; use 'can status %s' for completion/errors.\r\n",
                    CONSOLE_CAN_Channel_To_String( channel ) );
}

/**
 * @brief Print one received frame without treating binary payload as a C string.
 */
static void CONSOLE_CAN_Print_Frame( const HwCanRxFrame_T* received_frame )
{
    const HwCanFrame_T* frame = &received_frame->frame;

    CONSOLE_Printf( "  %s %s ID=0x%lX DLC=%u filter=%u timestamp=%u\r\n",
                    frame->is_extended_id ? "extended" : "standard",
                    frame->is_remote_frame ? "remote" : "data", ( unsigned long )frame->identifier,
                    ( unsigned int )frame->dlc, ( unsigned int )received_frame->filter_match_index,
                    ( unsigned int )received_frame->timestamp );

    if ( frame->is_remote_frame )
    {
        CONSOLE_Printf( "    requested response length: %u byte(s)\r\n",
                        ( unsigned int )frame->dlc );
        return;
    }

    CONSOLE_Printf( "    data:" );
    for ( uint32_t byte_index = 0U;
          byte_index < ( uint32_t )frame->dlc && byte_index < HW_CAN_CLASSIC_MAX_DATA_BYTES;
          byte_index++ )
    {
        CONSOLE_Printf( " %02X", ( unsigned int )frame->data[byte_index] );
    }
    CONSOLE_Printf( "\r\n" );
}

/**
 * @brief Copy and display a bounded number of received frames.
 *
 * EXEC_CAN_Receive uses the driver's peek/consume contract internally. The local
 * array fixes the maximum work and stack use of one console command, and the
 * execution layer consumes exactly the number successfully copied.
 */
static void CONSOLE_CAN_Command_Receive( uint16_t argc, char* argv[] )
{
    if ( argc < 3U || argc > 4U )
    {
        CONSOLE_CAN_Print_Usage();
        return;
    }

    HwCanChannel_T channel;
    if ( !CONSOLE_CAN_Parse_Channel( argv[CONSOLE_CAN_ARGV_CHANNEL], &channel ) )
    {
        return;
    }

    uint32_t requested_frames = 1U;
    if ( argc == 4U
         && !CONSOLE_CAN_Parse_U32( argv[CONSOLE_CAN_ARGV_PARAM_1], "receive frame count", 10, 1U,
                                    CONSOLE_CAN_MAX_RX_FRAMES_PER_COMMAND, &requested_frames ) )
    {
        return;
    }

    HwCanRxFrame_T frames[CONSOLE_CAN_MAX_RX_FRAMES_PER_COMMAND] = { 0 };
    uint32_t       frames_read                                   = 0U;

    HwCanResult_T result = EXEC_CAN_Receive( channel, frames, requested_frames, &frames_read );
    if ( result != HW_CAN_RESULT_OK )
    {
        CONSOLE_Printf( "Failed to read %s: %s\r\n", CONSOLE_CAN_Channel_To_String( channel ),
                        CONSOLE_CAN_Result_To_String( result ) );
        return;
    }

    if ( frames_read == 0U )
    {
        CONSOLE_Printf( "No unread CAN frames on %s\r\n",
                        CONSOLE_CAN_Channel_To_String( channel ) );
        return;
    }

    CONSOLE_Printf( "Read %lu CAN frame(s) from %s:\r\n", ( unsigned long )frames_read,
                    CONSOLE_CAN_Channel_To_String( channel ) );

    for ( uint32_t frame_index = 0U; frame_index < frames_read; frame_index++ )
    {
        CONSOLE_CAN_Print_Frame( &frames[frame_index] );
    }
}

/**
 * @brief Print human-readable descriptions for every currently latched fault bit.
 */
static void CONSOLE_CAN_Print_Latched_Faults( HwCanFaultFlags_T faults )
{
    if ( faults == HW_CAN_FAULT_NONE )
    {
        CONSOLE_Printf( "  latched faults: none\r\n" );
        return;
    }

    CONSOLE_Printf( "  latched faults: 0x%08lX\r\n", ( unsigned long )faults );

    if ( ( faults & HW_CAN_FAULT_TX_ARBITRATION_LOST ) != 0U )
    {
        CONSOLE_Printf( "    - transmit arbitration lost\r\n" );
    }
    if ( ( faults & HW_CAN_FAULT_TX_ERROR ) != 0U )
    {
        CONSOLE_Printf( "    - transmit error\r\n" );
    }
    if ( ( faults & HW_CAN_FAULT_TX_ABORTED ) != 0U )
    {
        CONSOLE_Printf( "    - transmit aborted\r\n" );
    }
    if ( ( faults & HW_CAN_FAULT_RX_SOFTWARE_OVERFLOW ) != 0U )
    {
        CONSOLE_Printf( "    - receive software queue overflow\r\n" );
    }
    if ( ( faults & HW_CAN_FAULT_RX_FIFO_FULL ) != 0U )
    {
        CONSOLE_Printf( "    - hardware receive FIFO became full\r\n" );
    }
    if ( ( faults & HW_CAN_FAULT_RX_FIFO_OVERRUN ) != 0U )
    {
        CONSOLE_Printf( "    - hardware receive FIFO overrun\r\n" );
    }
    if ( ( faults & HW_CAN_FAULT_ERROR_WARNING ) != 0U )
    {
        CONSOLE_Printf( "    - controller entered error-warning state\r\n" );
    }
    if ( ( faults & HW_CAN_FAULT_ERROR_PASSIVE ) != 0U )
    {
        CONSOLE_Printf( "    - controller entered error-passive state\r\n" );
    }
    if ( ( faults & HW_CAN_FAULT_BUS_OFF ) != 0U )
    {
        CONSOLE_Printf( "    - controller entered bus-off state\r\n" );
    }
    if ( ( faults & HW_CAN_FAULT_PROTOCOL_ERROR ) != 0U )
    {
        CONSOLE_Printf( "    - CAN protocol error observed\r\n" );
    }
}

/**
 * @brief Print a non-blocking lifecycle, queue and diagnostic snapshot.
 *
 * Queue state is deliberately separate from the success counters. An empty TX
 * queue can still have frames in hardware mailboxes, and an idle controller can
 * have completed frames that failed arbitration or transmission.
 */
static void CONSOLE_CAN_Command_Status( uint16_t argc, char* argv[] )
{
    if ( argc != 3U )
    {
        CONSOLE_CAN_Print_Usage();
        return;
    }

    HwCanChannel_T channel;
    if ( !CONSOLE_CAN_Parse_Channel( argv[CONSOLE_CAN_ARGV_CHANNEL], &channel ) )
    {
        return;
    }

    HwCanStatus_T status = { 0 };
    HwCanResult_T result = EXEC_CAN_Get_Status( channel, &status );
    if ( result != HW_CAN_RESULT_OK )
    {
        CONSOLE_Printf( "Failed to read %s status: %s\r\n",
                        CONSOLE_CAN_Channel_To_String( channel ),
                        CONSOLE_CAN_Result_To_String( result ) );
        return;
    }

    CONSOLE_Printf( "%s CAN status: %s\r\n", CONSOLE_CAN_Channel_To_String( channel ),
                    CONSOLE_CAN_State_To_String( status.state ) );
    CONSOLE_Printf( "  bitrate: requested=%lu actual=%lu error=%u ppm sample=%u.%u%%\r\n",
                    ( unsigned long )status.requested_bitrate,
                    ( unsigned long )status.actual_bitrate,
                    ( unsigned int )status.bitrate_error_ppm,
                    ( unsigned int )( status.actual_sample_point_permill / 10U ),
                    ( unsigned int )( status.actual_sample_point_permill % 10U ) );
    CONSOLE_Printf( "  timing: prescaler=%u BS1=%u TQ BS2=%u TQ SJW=%u TQ\r\n",
                    ( unsigned int )status.prescaler, ( unsigned int )status.bit_segment_1_tq,
                    ( unsigned int )status.bit_segment_2_tq,
                    ( unsigned int )status.sync_jump_width_tq );
    CONSOLE_Printf( "  queues: tx=%lu in_flight=%u rx=%lu tx_idle=%s\r\n",
                    ( unsigned long )status.tx_queue_frame_count,
                    ( unsigned int )status.tx_in_flight_count,
                    ( unsigned long )status.rx_queue_frame_count,
                    EXEC_CAN_Is_Transmission_Complete( channel ) ? "yes" : "no" );
    CONSOLE_Printf( "  bus: warning=%s passive=%s bus_off=%s TEC=%u REC=%u\r\n",
                    status.error_warning ? "yes" : "no", status.error_passive ? "yes" : "no",
                    status.bus_off ? "yes" : "no", ( unsigned int )status.transmit_error_count,
                    ( unsigned int )status.receive_error_count );
    CONSOLE_Printf( "  last protocol error: %s\r\n",
                    CONSOLE_CAN_Last_Error_To_String( status.last_error ) );
    CONSOLE_Printf(
        "  tx: queued=%lu rejected=%lu submitted=%lu succeeded=%lu\r\n",
        ( unsigned long )status.tx_frames_queued, ( unsigned long )status.tx_frames_rejected,
        ( unsigned long )status.tx_frames_submitted, ( unsigned long )status.tx_frames_succeeded );
    CONSOLE_Printf( "      arbitration_lost=%lu errors=%lu aborted=%lu\r\n",
                    ( unsigned long )status.tx_arbitration_losses,
                    ( unsigned long )status.tx_errors, ( unsigned long )status.tx_aborted );
    CONSOLE_Printf(
        "  rx: received=%lu software_drops=%lu fifo_full=%lu overruns=%lu\r\n",
        ( unsigned long )status.rx_frames_received, ( unsigned long )status.rx_software_drops,
        ( unsigned long )status.rx_fifo_full_events, ( unsigned long )status.rx_fifo_overruns );
    CONSOLE_Printf( "  state events: warning=%lu passive=%lu bus_off=%lu\r\n",
                    ( unsigned long )status.error_warning_events,
                    ( unsigned long )status.error_passive_events,
                    ( unsigned long )status.bus_off_events );

    CONSOLE_CAN_Print_Latched_Faults( status.latched_faults );
}

/**
 * @brief Reapply stored configuration after a recoverable CAN hardware fault.
 *
 * Recovery is intentionally explicit and runs in console task context. Frames
 * still waiting in the software queue are retained by the driver; any frame whose
 * outcome was ambiguous after entering a hardware mailbox is counted as aborted.
 */
static void CONSOLE_CAN_Command_Recover( uint16_t argc, char* argv[] )
{
    if ( argc != 3U )
    {
        CONSOLE_CAN_Print_Usage();
        return;
    }

    HwCanChannel_T channel;
    if ( !CONSOLE_CAN_Parse_Channel( argv[CONSOLE_CAN_ARGV_CHANNEL], &channel ) )
    {
        return;
    }

    HwCanResult_T result = EXEC_CAN_Recover_Channel( channel );
    if ( result == HW_CAN_RESULT_OK )
    {
        CONSOLE_Printf( "%s recovered and active\r\n", CONSOLE_CAN_Channel_To_String( channel ) );
    }
    else
    {
        CONSOLE_Printf( "Failed to recover %s: %s\r\n", CONSOLE_CAN_Channel_To_String( channel ),
                        CONSOLE_CAN_Result_To_String( result ) );
    }
}

/**
 * @brief Dispatch a command from the top-level classic-CAN namespace.
 *
 * Command parsing and all formatted output occur in the low-priority console
 * task. The CAN ISR sees only prevalidated frame descriptors and never performs
 * string parsing, formatting, allocation or a blocking wait.
 *
 * @param argc Number of parsed words, including can.
 * @param argv Parsed command words.
 */
void CONSOLE_CAN_Command_Handler( uint16_t argc, char* argv[] )
{
    if ( argc == 1U || ( argc == 2U && strcmp( argv[CONSOLE_CAN_ARGV_SUBCOMMAND], "help" ) == 0 ) )
    {
        CONSOLE_CAN_Print_Usage();
        return;
    }

    if ( argc < 2U )
    {
        CONSOLE_CAN_Print_Usage();
        return;
    }

    for ( size_t command = 0U; command < ARRAY_LEN( CAN_SUBCOMMANDS ); command++ )
    {
        if ( strcmp( argv[CONSOLE_CAN_ARGV_SUBCOMMAND], CAN_SUBCOMMANDS[command].name ) == 0 )
        {
            CAN_SUBCOMMANDS[command].handler( argc, argv );
            return;
        }
    }

    CONSOLE_Printf( "Unknown CAN command: %s\r\n", argv[CONSOLE_CAN_ARGV_SUBCOMMAND] );
    CONSOLE_CAN_Print_Usage();
}
