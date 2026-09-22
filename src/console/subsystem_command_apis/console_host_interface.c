/******************************************************************************
 *  File:       console_host_interface.c
 *  Author:     Callum Rafferty
 *  Created:    22-Sep-2026
 *
 *  Description:
 *      Console command interface for Host Interface live status queries.
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include "console_host_interface.h"
#include "console.h"
#include "host_interface.h"
#include "hil_rig_protocol/application/application_message.h"
#include "hil_rig_protocol/application/application_response.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/**-----------------------------------------------------------------------------
 *  Private (static) Function Prototypes
 *------------------------------------------------------------------------------
 */

static void CONSOLE_HostInterface_PrintUsage( void );
static void CONSOLE_HostInterface_PrintStatus( void );

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

static const char* CONSOLE_HostInterface_FaultName( RunStateFaultReason_T reason )
{
    switch ( reason )
    {
        case RUN_STATE_FAULT_NONE:
            return "none";
        case RUN_STATE_FAULT_HOST_INTERFACE_RESPONSE_BLOCKED:
            return "response blocked / unconsumed";
        case RUN_STATE_FAULT_HOST_INTERFACE_USB_INIT:
            return "USB init failed";
        case RUN_STATE_FAULT_HOST_INTERFACE_CODEC_INIT:
            return "codec init failed";
        case RUN_STATE_FAULT_HOST_INTERFACE_TRANSPORT_INIT:
            return "transport init failed";
        case RUN_STATE_FAULT_HOST_INTERFACE_ERROR:
            return "general error";
        default:
            return "other RSM fault";
    }
}

static const char* CONSOLE_HostInterface_MessageTypeName( uint8_t type )
{
    switch ( type )
    {
        case HIL_APPLICATION_MESSAGE_TYPE_SYSTEM_INFO_REQUEST:
            return "SYSTEM_INFO_REQUEST (0x01)";
        case HIL_APPLICATION_MESSAGE_TYPE_SYSTEM_INFO_RESPONSE:
            return "SYSTEM_INFO_RESPONSE (0x02)";
        case HIL_APPLICATION_MESSAGE_TYPE_TEST_CONFIGURATION:
            return "TEST_CONFIGURATION (0x10)";
        case HIL_APPLICATION_MESSAGE_TYPE_TEST_INSTRUCTION:
            return "TEST_INSTRUCTION (0x11)";
        case HIL_APPLICATION_MESSAGE_TYPE_EXECUTION_CONTROL:
            return "EXECUTION_CONTROL (0x13)";
        case HIL_APPLICATION_MESSAGE_TYPE_GLOBAL_CONTROL:
            return "GLOBAL_CONTROL (0x14)";
        case HIL_APPLICATION_MESSAGE_TYPE_UPDATE_INSTRUCTION:
            return "UPDATE_INSTRUCTION (0x15)";
        case HIL_APPLICATION_MESSAGE_TYPE_FINALIZE_TEST_UPLOAD:
            return "FINALIZE_TEST_UPLOAD (0x16)";
        case HIL_APPLICATION_MESSAGE_TYPE_TEST_RESULT:
            return "TEST_RESULT (0x20)";
        case HIL_APPLICATION_MESSAGE_TYPE_VARIABLE_TEST_RESULT:
            return "VARIABLE_TEST_RESULT (0x22)";
        case HIL_APPLICATION_MESSAGE_TYPE_RESPONSE:
            return "RESPONSE (0x30)";
        case HIL_APPLICATION_MESSAGE_TYPE_ERROR:
            return "ERROR (0x31)";
        default:
            return "UNKNOWN";
    }
}

static const char* CONSOLE_HostInterface_TransportSessionStateName( HIL_Transport_Session_State_T state )
{
    switch ( state )
    {
        case HIL_TRANSPORT_SESSION_STATE_DISCONNECTED:
            return "DISCONNECTED";
        case HIL_TRANSPORT_SESSION_STATE_CONNECTING:
            return "CONNECTING";
        case HIL_TRANSPORT_SESSION_STATE_ESTABLISHED:
            return "ESTABLISHED";
        case HIL_TRANSPORT_SESSION_STATE_RECOVERING:
            return "RECOVERING";
        case HIL_TRANSPORT_SESSION_STATE_FAULT:
            return "FAULT";
        default:
            return "UNKNOWN";
    }
}

static const char* CONSOLE_HostInterface_ResponseReasonName( uint32_t reason )
{
    switch ( reason )
    {
        case HIL_APPLICATION_RESPONSE_REASON_NONE:
            return "none";
        case HIL_APPLICATION_RESPONSE_REASON_UNSUPPORTED:
            return "unsupported";
        case HIL_APPLICATION_RESPONSE_REASON_OPERATION_NOT_ALLOWED:
            return "operation not allowed";
        case HIL_APPLICATION_RESPONSE_REASON_INCONSISTENT_TEST_ID:
            return "inconsistent test ID";
        case HIL_APPLICATION_RESPONSE_REASON_INVALID_TICK:
            return "invalid tick number";
        case HIL_APPLICATION_RESPONSE_REASON_LENGTH_MISMATCH:
            return "length mismatch";
        case HIL_APPLICATION_RESPONSE_REASON_STORAGE_UNAVAILABLE:
            return "storage unavailable";
        case HIL_APPLICATION_RESPONSE_REASON_VALIDATION_FAILED:
            return "validation failed";
        case HIL_APPLICATION_RESPONSE_REASON_HARDWARE_NOT_READY:
            return "hardware not ready";
        case HIL_APPLICATION_RESPONSE_REASON_INTERNAL_FAILURE:
            return "internal failure";
        default:
            return "unknown";
    }
}

static void CONSOLE_HostInterface_PrintUsage( void )
{
    CONSOLE_Printf( "Usage: host <status|reset>\r\n" );
}

static void CONSOLE_HostInterface_PrintStatus( void )
{
    HostInterfaceStatus_T status = { 0 };
    HOST_INTERFACE_GetStatus( &status );

    CONSOLE_Printf( "Host Interface Status:\r\n" );
    CONSOLE_Printf( "  Initialized:         %s\r\n", status.is_initialized ? "yes" : "no" );
    CONSOLE_Printf( "  USB link connected:  %s\r\n", status.usb_connected ? "yes" : "no" );
    CONSOLE_Printf( "  Transport session:   %s\r\n",
                    CONSOLE_HostInterface_TransportSessionStateName( status.transport_session_state ) );
    CONSOLE_Printf( "  Reliable TX pending: %s\r\n", status.transport_reliable_pending ? "yes" : "no" );
    CONSOLE_Printf( "  Traffic:             RX=%lu, TX=%lu\r\n",
                    ( unsigned long )status.rx_message_count,
                    ( unsigned long )status.tx_message_count );
    if ( status.rx_message_count > 0U )
    {
        if ( status.last_rx_message_type == HIL_APPLICATION_MESSAGE_TYPE_TEST_INSTRUCTION )
        {
            CONSOLE_Printf( "  Last RX msg:         %s (tick=%lu)\r\n",
                            CONSOLE_HostInterface_MessageTypeName( status.last_rx_message_type ),
                            ( unsigned long )status.last_rx_tick );
        }
        else
        {
            CONSOLE_Printf( "  Last RX msg:         %s\r\n",
                            CONSOLE_HostInterface_MessageTypeName( status.last_rx_message_type ) );
        }
    }
    CONSOLE_Printf( "  Can consume input:   %s\r\n", status.can_consume_incoming ? "yes" : "no" );
    CONSOLE_Printf( "  Outgoing pending:    %s\r\n", status.outgoing_message_pending ? "yes" : "no" );
    CONSOLE_Printf( "  Overflow state:      %s\r\n", status.is_overflowing ? "OVERFLOWING" : "normal" );
    CONSOLE_Printf( "  Fault state:         %s\r\n", status.is_faulted ? "FAULTED" : "none" );
    if ( status.rejected_instruction_count > 0U )
    {
        CONSOLE_Printf( "  Rejected instr ct:   %lu (last tick=%lu, reason=%s)\r\n",
                        ( unsigned long )status.rejected_instruction_count,
                        ( unsigned long )status.last_rejected_tick,
                        CONSOLE_HostInterface_ResponseReasonName( status.last_rejected_reason ) );
    }
    CONSOLE_Printf( "  Blocked responses:   %lu\r\n", ( unsigned long )status.response_blocked_count );
    if ( status.is_faulted || status.response_blocked_count > 0U )
    {
        CONSOLE_Printf( "  Last fault reason:   %s (%u)\r\n",
                        CONSOLE_HostInterface_FaultName( status.last_fault_reason ),
                        ( unsigned int )status.last_fault_reason );
        CONSOLE_Printf( "  Last blocked msg:    %s\r\n",
                        CONSOLE_HostInterface_MessageTypeName( status.last_blocked_message_type ) );
    }
    CONSOLE_Printf( "  Expected tick count: %lu\r\n", ( unsigned long )status.expected_tick_count );
    CONSOLE_Printf( "  Notifications:       0x%08lX\r\n", ( unsigned long )status.carry_on_notifications );
}

/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */

void CONSOLE_HostInterface_Command( uint16_t argc, char* argv[] )
{
    if ( argc < 2U || argv == NULL )
    {
        CONSOLE_HostInterface_PrintUsage();
        return;
    }

    if ( strcmp( argv[1], "status" ) == 0 )
    {
        CONSOLE_HostInterface_PrintStatus();
    }
    else if ( strcmp( argv[1], "reset" ) == 0 )
    {
        HOST_INTERFACE_Reset();
        CONSOLE_Printf( "Host Interface reset queued.\r\n" );
    }
    else
    {
        CONSOLE_HostInterface_PrintUsage();
    }
}
