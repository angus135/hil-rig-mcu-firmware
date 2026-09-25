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
#include "hw_usb.h"
#include "hil_rig_protocol/application/application_message.h"
#include "hil_rig_protocol/application/application_response.h"
#include "hil_rig_protocol/application/application_status.h"
#include "hil_rig_protocol/transport/transport_types.h"
#include "flash_manager/flash_manager.h"
#include "variable_result_message_producer.h"

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
        case RUN_STATE_FAULT_EXTERNAL_REQUEST:
            return "external request";
        case RUN_STATE_FAULT_INVALID_TRANSITION:
            return "invalid transition";
        case RUN_STATE_FAULT_LOGIC_EXPANDER_NOT_READY:
            return "logic expander not ready";
        case RUN_STATE_FAULT_CONFIGURATION_UNAVAILABLE:
            return "configuration unavailable";
        case RUN_STATE_FAULT_DRIVER_CONFIGURATION:
            return "driver configuration failed";
        case RUN_STATE_FAULT_DRIVER_CONFIGURATION_TIMEOUT:
            return "driver configuration timeout";
        case RUN_STATE_FAULT_DRIVER_START:
            return "driver start failed";
        case RUN_STATE_FAULT_DRIVER_START_TIMEOUT:
            return "driver start timeout";
        case RUN_STATE_FAULT_ACQUISITION_EPOCH:
            return "acquisition epoch fault";
        case RUN_STATE_FAULT_DRIVER_STOP:
            return "driver stop failed";
        case RUN_STATE_FAULT_DRIVER_STOP_TIMEOUT:
            return "driver stop timeout";
        case RUN_STATE_FAULT_EXECUTION_TIMER:
            return "execution timer fault";
        case RUN_STATE_FAULT_EXECUTION_MANAGER:
            return "execution manager fault";
        case RUN_STATE_FAULT_FLASH_EXECUTION_PREPARATION:
            return "flash execution prep failed";
        case RUN_STATE_FAULT_FLASH_EXECUTION_PREPARATION_TIMEOUT:
            return "flash execution prep timeout";
        case RUN_STATE_FAULT_FLASH_RESULT_FINALISATION:
            return "flash result finalisation failed";
        case RUN_STATE_FAULT_FLASH_RESULT_FINALISATION_TIMEOUT:
            return "flash result finalisation timeout";
        case RUN_STATE_FAULT_FLASH_RESULT_TRANSFER:
            return "flash result transfer fault";
        case RUN_STATE_FAULT_FLASH_RESULT_DISPOSITION:
            return "flash result disposition fault";
        case RUN_STATE_FAULT_FLASH_MANAGER:
            return "flash manager fault";
        case RUN_STATE_FAULT_HOST_INTERFACE_RESPONSE_BLOCKED:
            return "response blocked / unconsumed";
        case RUN_STATE_FAULT_HOST_INTERFACE_USB_INIT:
            return "USB init failed";
        case RUN_STATE_FAULT_HOST_INTERFACE_CODEC_INIT:
            return "codec init failed";
        case RUN_STATE_FAULT_HOST_INTERFACE_TRANSPORT_INIT:
            return "transport init failed";
        case RUN_STATE_FAULT_HOST_INTERFACE_ERROR:
            return "host interface error";
        case RUN_STATE_FAULT_INTERNAL:
            return "internal fault";
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

static const char*
CONSOLE_HostInterface_TransportSessionStateName( HIL_Transport_Session_State_T state )
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

static const char* CONSOLE_HostInterface_ApplicationStatusName( HIL_Application_Status_T status )
{
    switch ( status )
    {
        case HIL_APPLICATION_STATUS_OK:
            return "OK (0)";
        case HIL_APPLICATION_STATUS_INVALID_ARGUMENT:
            return "INVALID_ARGUMENT (1)";
        case HIL_APPLICATION_STATUS_UNINITIALIZED:
            return "UNINITIALIZED (2)";
        case HIL_APPLICATION_STATUS_BUFFER_TOO_SMALL:
            return "BUFFER_TOO_SMALL (3)";
        case HIL_APPLICATION_STATUS_INVALID_MESSAGE_TYPE:
            return "INVALID_MESSAGE_TYPE (4)";
        case HIL_APPLICATION_STATUS_INVALID_SUBTYPE:
            return "INVALID_SUBTYPE (5)";
        case HIL_APPLICATION_STATUS_MALFORMED_MESSAGE:
            return "MALFORMED_MESSAGE (6)";
        case HIL_APPLICATION_STATUS_TRUNCATED_MESSAGE:
            return "TRUNCATED_MESSAGE (7)";
        case HIL_APPLICATION_STATUS_INVALID_LENGTH:
            return "INVALID_LENGTH (8)";
        case HIL_APPLICATION_STATUS_INVALID_COUNT:
            return "INVALID_COUNT (9)";
        case HIL_APPLICATION_STATUS_UNSUPPORTED_MESSAGE:
            return "UNSUPPORTED_MESSAGE (10)";
        case HIL_APPLICATION_STATUS_INCONSISTENT_TEST_ID:
            return "INCONSISTENT_TEST_ID (11)";
        case HIL_APPLICATION_STATUS_INCONSISTENT_TICK:
            return "INCONSISTENT_TICK (12)";
        case HIL_APPLICATION_STATUS_INCOMPLETE_DATA:
            return "INCOMPLETE_DATA (13)";
        case HIL_APPLICATION_STATUS_VALIDATION_FAILED:
            return "VALIDATION_FAILED (14)";
        case HIL_APPLICATION_STATUS_NOT_IMPLEMENTED:
            return "NOT_IMPLEMENTED (15)";
        case HIL_APPLICATION_STATUS_INTERNAL_ERROR:
            return "INTERNAL_ERROR (16)";
        case HIL_APPLICATION_STATUS_VERSION_MISMATCH:
            return "VERSION_MISMATCH (17)";
        default:
            return "UNKNOWN";
    }
}

static const char* CONSOLE_HostInterface_TransportStatusName( HIL_Transport_Status_T status )
{
    switch ( status )
    {
        case HIL_TRANSPORT_STATUS_OK:
            return "OK";
        case HIL_TRANSPORT_STATUS_INVALID_ARGUMENT:
            return "INVALID_ARGUMENT";
        case HIL_TRANSPORT_STATUS_BUFFER_TOO_SMALL:
            return "BUFFER_TOO_SMALL";
        case HIL_TRANSPORT_STATUS_UNSUPPORTED_CONFIGURATION:
            return "UNSUPPORTED_CONFIGURATION";
        case HIL_TRANSPORT_STATUS_MESSAGE_TOO_LARGE:
            return "MESSAGE_TOO_LARGE";
        case HIL_TRANSPORT_STATUS_CAPACITY_EXHAUSTED:
            return "CAPACITY_EXHAUSTED";
        case HIL_TRANSPORT_STATUS_DELIVERY_FAILED:
            return "DELIVERY_FAILED";
        case HIL_TRANSPORT_STATUS_TIMEOUT:
            return "TIMEOUT";
        case HIL_TRANSPORT_STATUS_NOT_READY:
            return "NOT_READY";
        case HIL_TRANSPORT_STATUS_NOT_IMPLEMENTED:
            return "NOT_IMPLEMENTED";
        case HIL_TRANSPORT_STATUS_INTERNAL_ERROR:
            return "INTERNAL_ERROR";
        default:
            return "UNKNOWN";
    }
}

static const char* CONSOLE_HostInterface_TransportFailureName( HIL_Transport_Failure_T failure )
{
    switch ( failure )
    {
        case HIL_TRANSPORT_FAILURE_NONE:
            return "NONE";
        case HIL_TRANSPORT_FAILURE_LINK_LOST:
            return "LINK_LOST";
        case HIL_TRANSPORT_FAILURE_CONNECTION_TIMEOUT:
            return "CONNECTION_TIMEOUT";
        case HIL_TRANSPORT_FAILURE_DELIVERY:
            return "DELIVERY";
        case HIL_TRANSPORT_FAILURE_PROTOCOL:
            return "PROTOCOL";
        case HIL_TRANSPORT_FAILURE_CAPACITY:
            return "CAPACITY";
        case HIL_TRANSPORT_FAILURE_LOCAL_RESET:
            return "LOCAL_RESET";
        case HIL_TRANSPORT_FAILURE_INTERNAL:
            return "INTERNAL";
        default:
            return "UNKNOWN";
    }
}

static const char*
CONSOLE_HostInterface_ProducerStatusName( Result_Message_Producer_Status_T status )
{
    switch ( status )
    {
        case RESULT_MESSAGE_PRODUCER_STATUS_OK:
            return "OK";
        case RESULT_MESSAGE_PRODUCER_STATUS_NO_DATA_AVAILABLE:
            return "NO_DATA_AVAILABLE";
        case RESULT_MESSAGE_PRODUCER_STATUS_END_OF_STREAM:
            return "END_OF_STREAM";
        case RESULT_MESSAGE_PRODUCER_STATUS_INVALID_ARGUMENT:
            return "INVALID_ARGUMENT";
        case RESULT_MESSAGE_PRODUCER_STATUS_CORRUPT_DATA:
            return "CORRUPT_DATA";
        case RESULT_MESSAGE_PRODUCER_STATUS_INTERNAL_ERROR:
            return "INTERNAL_ERROR";
        default:
            return "UNKNOWN";
    }
}

static const char* CONSOLE_HostInterface_USBConnectionStateName( HW_USB_Connection_State_T state )
{
    switch ( state )
    {
        case HW_USB_CONNECTION_STATE_DISCONNECTED:
            return "DISCONNECTED";
        case HW_USB_CONNECTION_STATE_CONFIGURED_SUSPENDED:
            return "CONFIGURED_SUSPENDED";
        case HW_USB_CONNECTION_STATE_ACTIVE:
            return "ACTIVE";
        default:
            return "UNKNOWN";
    }
}

static const char*
CONSOLE_HostInterface_FlashTransferStatusName( FlashManagerResultTransferStatus_T status )
{
    switch ( status )
    {
        case FLASH_MANAGER_RESULT_TRANSFER_OK:
            return "OK";
        case FLASH_MANAGER_RESULT_TRANSFER_INVALID_STATE:
            return "INVALID_STATE";
        case FLASH_MANAGER_RESULT_TRANSFER_INCOMPLETE:
            return "INCOMPLETE";
        case FLASH_MANAGER_RESULT_TRANSFER_INVALID_ARGUMENT:
            return "INVALID_ARGUMENT";
        case FLASH_MANAGER_RESULT_TRANSFER_BUSY:
            return "BUSY";
        case FLASH_MANAGER_RESULT_TRANSFER_END_OF_STREAM:
            return "END_OF_STREAM";
        case FLASH_MANAGER_RESULT_TRANSFER_NOT_INITIALISED:
            return "NOT_INITIALISED";
        case FLASH_MANAGER_RESULT_TRANSFER_TASK_NOT_READY:
            return "TASK_NOT_READY";
        case FLASH_MANAGER_RESULT_TRANSFER_NOTIFY_FAILED:
            return "NOTIFY_FAILED";
        case FLASH_MANAGER_RESULT_TRANSFER_INTERNAL_ERROR:
            return "INTERNAL_ERROR";
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
    CONSOLE_Printf( "  USB link connected:  %s (state=%s, rx_stream_used=%lu)\r\n",
                    status.usb_connected ? "yes" : "no",
                    CONSOLE_HostInterface_USBConnectionStateName( status.usb_connection_state ),
                    ( unsigned long )status.usb_rx_stream_used_bytes );
    CONSOLE_Printf(
        "  Transport session:   %s (submit_status=%s, reliable_pending=%s, failure=%s)\r\n",
        CONSOLE_HostInterface_TransportSessionStateName( status.transport_session_state ),
        CONSOLE_HostInterface_TransportStatusName( status.transport_last_submit_status ),
        status.transport_reliable_pending ? "yes" : "no",
        CONSOLE_HostInterface_TransportFailureName( status.transport_last_failure ) );
    CONSOLE_Printf( "  Codec Diagnostics:   encode=%s [size=%u], decode=%s\r\n",
                    CONSOLE_HostInterface_ApplicationStatusName( status.last_encode_status ),
                    ( unsigned int )status.last_encode_size,
                    CONSOLE_HostInterface_ApplicationStatusName( status.last_decode_status ) );
    if ( status.last_encode_status != HIL_APPLICATION_STATUS_OK )
    {
        CONSOLE_Printf( "  Last Encode Failed:  %s\r\n",
                        CONSOLE_HostInterface_MessageTypeName( status.last_encode_failed_type ) );
    }
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
    if ( status.tx_message_count > 0U )
    {
        CONSOLE_Printf( "  Last TX msg:         %s (tick=%lu)\r\n",
                        CONSOLE_HostInterface_MessageTypeName( status.last_tx_message_type ),
                        ( unsigned long )status.last_tx_tick );
    }
    CONSOLE_Printf( "  Can consume input:   %s\r\n", status.can_consume_incoming ? "yes" : "no" );
    if ( status.outgoing_message_pending )
    {
        CONSOLE_Printf(
            "  Outgoing pending:    yes (%s, tick=%lu)\r\n",
            CONSOLE_HostInterface_MessageTypeName( status.outgoing_pending_message_type ),
            ( unsigned long )status.outgoing_pending_tick );
    }
    else
    {
        CONSOLE_Printf( "  Outgoing pending:    no\r\n" );
    }
    if ( status.is_overflowing )
    {
        CONSOLE_Printf( "  Overflow state:      OVERFLOWING (%s, tick=%lu, elapsed=%lu ms)\r\n",
                        CONSOLE_HostInterface_MessageTypeName( status.overflow_message_type ),
                        ( unsigned long )status.overflow_message_tick,
                        ( unsigned long )status.overflow_duration_ms );
    }
    else
    {
        CONSOLE_Printf( "  Overflow state:      normal\r\n" );
    }
    CONSOLE_Printf( "  Fault state:         %s\r\n", status.is_faulted ? "FAULTED" : "none" );
    if ( status.rejected_instruction_count > 0U )
    {
        CONSOLE_Printf( "  Rejected instr ct:   %lu (last tick=%lu, reason=%s)\r\n",
                        ( unsigned long )status.rejected_instruction_count,
                        ( unsigned long )status.last_rejected_tick,
                        CONSOLE_HostInterface_ResponseReasonName( status.last_rejected_reason ) );
    }
    CONSOLE_Printf( "  Blocked responses:   %lu\r\n",
                    ( unsigned long )status.response_blocked_count );
    if ( status.is_faulted || status.response_blocked_count > 0U )
    {
        CONSOLE_Printf( "  Last fault reason:   %s (%u)\r\n",
                        CONSOLE_HostInterface_FaultName( status.last_fault_reason ),
                        ( unsigned int )status.last_fault_reason );
        CONSOLE_Printf( "  Last blocked msg:    %s (tick=%lu)\r\n",
                        CONSOLE_HostInterface_MessageTypeName( status.last_blocked_message_type ),
                        ( unsigned long )status.last_blocked_message_tick );
    }
    CONSOLE_Printf( "  Expected tick count: %lu\r\n", ( unsigned long )status.expected_tick_count );
    CONSOLE_Printf( "  Notifications:       0x%08lX\r\n",
                    ( unsigned long )status.carry_on_notifications );
    CONSOLE_Printf(
        "  Producer Status:     last=%s, next_tick=%lu, active_tick=%lu, staged_recs=%u%s\r\n",
        CONSOLE_HostInterface_ProducerStatusName( status.var_producer_diags.last_status ),
        ( unsigned long )status.var_producer_diags.next_result_tick,
        ( unsigned long )status.var_producer_diags.active_tick_number,
        ( unsigned int )status.var_producer_diags.staged_record_count,
        status.var_producer_diags.is_flash_end_of_stream ? " (flash EOS)" : "" );
    CONSOLE_Printf(
        "  Producer Stream:     buffered=%u, read_offset=%u, write_offset=%u, flash_status=%s\r\n",
        ( unsigned int )status.var_producer_diags.buffered_bytes,
        ( unsigned int )status.var_producer_diags.read_offset,
        ( unsigned int )status.var_producer_diags.write_offset,
        CONSOLE_HostInterface_FlashTransferStatusName(
            status.var_producer_diags.last_flash_status ) );
    if ( status.instruction_rx_count > 0U || status.instruction_phase_active )
    {
        CONSOLE_Printf( "  Instruction Rx:      count=%lu, duration=%lu ms, rate=%lu msgs/s%s\r\n",
                        ( unsigned long )status.instruction_rx_count,
                        ( unsigned long )status.instruction_duration_ms,
                        ( unsigned long )status.instruction_rate_msgs_per_sec,
                        status.instruction_phase_active ? " (active)" : "" );
    }
    if ( status.result_tx_count > 0U || status.result_phase_active )
    {
        CONSOLE_Printf( "  Result Tx:           count=%lu, duration=%lu ms, rate=%lu msgs/s%s\r\n",
                        ( unsigned long )status.result_tx_count,
                        ( unsigned long )status.result_duration_ms,
                        ( unsigned long )status.result_rate_msgs_per_sec,
                        status.result_phase_active ? " (active)" : "" );
    }
    CONSOLE_Printf( "  Effective period:    %lu ms\r\n",
                    ( unsigned long )status.effective_period_ms );
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
