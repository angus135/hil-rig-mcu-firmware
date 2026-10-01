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
#include "host_process_message.h"
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
static void CONSOLE_HostInterface_PrintTrace( void );

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
        case RUN_STATE_FAULT_HOST_INTERFACE_INSTRUCTION_UPLOAD_TIMEOUT:
            return "instruction upload inactivity timeout";
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

static const char* CONSOLE_HostInterface_ResultEventName( const HostInterfaceResultTxEvent_T event )
{
    switch ( event )
    {
        case HOST_INTERFACE_RESULT_TX_EVENT_PRODUCED:
            return "produced";
        case HOST_INTERFACE_RESULT_TX_EVENT_STAGED:
            return "staged";
        case HOST_INTERFACE_RESULT_TX_EVENT_USB_QUEUED:
            return "usb-queued";
        case HOST_INTERFACE_RESULT_TX_EVENT_USB_REJECTED:
            return "usb-rejected";
        case HOST_INTERFACE_RESULT_TX_EVENT_CDC_COMPLETED:
            return "cdc-completed";
        case HOST_INTERFACE_RESULT_TX_EVENT_USB_DISCARDED:
            return "usb-discarded";
        case HOST_INTERFACE_RESULT_TX_EVENT_INVARIANT_FAULT:
            return "invariant-fault";
        default:
            return "unknown";
    }
}

static const char*
CONSOLE_HostInterface_ResultInvariantName( const HostInterfaceResultTxInvariant_T invariant )
{
    switch ( invariant )
    {
        case HOST_INTERFACE_RESULT_TX_INVARIANT_NONE:
            return "none";
        case HOST_INTERFACE_RESULT_TX_INVARIANT_PRODUCED_TICK:
            return "produced tick discontinuity";
        case HOST_INTERFACE_RESULT_TX_INVARIANT_STAGED_TICK:
            return "staged tick discontinuity";
        case HOST_INTERFACE_RESULT_TX_INVARIANT_QUEUED_TICK:
            return "queued tick discontinuity";
        case HOST_INTERFACE_RESULT_TX_INVARIANT_COMPLETED_TICK:
            return "completed tick discontinuity";
        case HOST_INTERFACE_RESULT_TX_INVARIANT_OUTSTANDING_BATCH_OVERFLOW:
            return "outstanding batch overflow";
        case HOST_INTERFACE_RESULT_TX_INVARIANT_USB_ADMISSION_TIMEOUT:
            return "USB admission timeout";
        case HOST_INTERFACE_RESULT_TX_INVARIANT_CDC_COMPLETION_TIMEOUT:
            return "CDC completion timeout";
        case HOST_INTERFACE_RESULT_TX_INVARIANT_USB_COUNTER_REGRESSION:
            return "USB counter regression";
        case HOST_INTERFACE_RESULT_TX_INVARIANT_USB_STREAM_INTEGRITY:
            return "USB ring stream integrity";
        case HOST_INTERFACE_RESULT_TX_INVARIANT_ACTIVE_TRANSFER_DISCARDED:
            return "active transfer discarded";
        case HOST_INTERFACE_RESULT_TX_INVARIANT_FINAL_COUNTS:
            return "final custody counts";
        default:
            return "unknown";
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

static const char* CONSOLE_HostInterface_StatusName( HOST_Interface_Status_T status )
{
    switch ( status )
    {
        case HOST_INTERFACE_STATUS_OK:
            return "OK";
        case HOST_INTERFACE_STATUS_INVALID_ARGUMENT:
            return "INVALID_ARGUMENT";
        case HOST_INTERFACE_STATUS_UNINITIALIZED:
            return "UNINITIALIZED";
        case HOST_INTERFACE_STATUS_BUFFER_TOO_SMALL:
            return "BUFFER_TOO_SMALL";
        case HOST_INTERFACE_STATUS_INVALID_MESSAGE_TYPE:
            return "INVALID_MESSAGE_TYPE";
        case HOST_INTERFACE_STATUS_INVALID_SUBTYPE:
            return "INVALID_SUBTYPE";
        case HOST_INTERFACE_STATUS_MALFORMED_MESSAGE:
            return "MALFORMED_MESSAGE";
        case HOST_INTERFACE_STATUS_TRUNCATED_MESSAGE:
            return "TRUNCATED_MESSAGE";
        case HOST_INTERFACE_STATUS_INVALID_LENGTH:
            return "INVALID_LENGTH";
        case HOST_INTERFACE_STATUS_INVALID_COUNT:
            return "INVALID_COUNT";
        case HOST_INTERFACE_STATUS_UNSUPPORTED_MESSAGE:
            return "UNSUPPORTED_MESSAGE";
        case HOST_INTERFACE_STATUS_INCONSISTENT_TEST_ID:
            return "INCONSISTENT_TEST_ID";
        case HOST_INTERFACE_STATUS_INCONSISTENT_TICK:
            return "INCONSISTENT_TICK";
        case HOST_INTERFACE_STATUS_INCOMPLETE_DATA:
            return "INCOMPLETE_DATA";
        case HOST_INTERFACE_STATUS_VALIDATION_FAILED:
            return "VALIDATION_FAILED";
        case HOST_INTERFACE_STATUS_NOT_IMPLEMENTED:
            return "NOT_IMPLEMENTED";
        case HOST_INTERFACE_STATUS_INTERNAL_ERROR:
            return "INTERNAL_ERROR";
        case HOST_INTERFACE_STATUS_OUT_OF_DATE:
            return "OUT_OF_DATE";
        case HOST_INTERFACE_STATUS_STATE_TRANSITION_FAILURE:
            return "STATE_TRANSITION_FAILURE";
        case HOST_INTERFACE_STATUS_OUTGOING_REQUIRED:
            return "OUTGOING_REQUIRED";
        case HOST_INTERFACE_STATUS_UNSUPPORTED_NOTIFICATION:
            return "UNSUPPORTED_NOTIFICATION";
        default:
            return "UNKNOWN";
    }
}

static void CONSOLE_HostInterface_PrintUsage( void )
{
    CONSOLE_Printf( "Usage: host <status|trace|reset>\r\n" );
}

static void CONSOLE_HostInterface_PrintStatus( void )
{
    HostInterfaceStatus_T status = { 0 };
    HOST_INTERFACE_GetStatus( &status );

    CONSOLE_Printf( "Host Interface Status:\r\n" );
    CONSOLE_Printf( "  Initialized:         %s\r\n", status.is_initialized ? "yes" : "no" );
    CONSOLE_Printf( "  USB link connected:  %s (state=%s, rx_stream_used=%lu, rx_dropped=%lu)\r\n",
                    status.usb_connected ? "yes" : "no",
                    CONSOLE_HostInterface_USBConnectionStateName( status.usb_connection_state ),
                    ( unsigned long )status.usb_rx_stream_used_bytes,
                    ( unsigned long )status.usb_rx_stream_dropped_bytes );
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
        CONSOLE_Printf( "  Rejected instr ct:   %lu (last tick=%lu, reason=%s, detail=%s [%lu], "
                        "stage=%lu, code=%lu)\r\n",
                        ( unsigned long )status.rejected_instruction_count,
                        ( unsigned long )status.last_rejected_tick,
                        CONSOLE_HostInterface_ResponseReasonName( status.last_rejected_reason ),
                        CONSOLE_HostInterface_StatusName(
                            ( HOST_Interface_Status_T )status.last_rejected_detail ),
                        ( unsigned long )status.last_rejected_detail,
                        ( unsigned long )status.var_instruction_last_stage,
                        ( unsigned long )status.var_instruction_last_stage_code );
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
    CONSOLE_Printf( "  Result custody:      produced=%lu[%lu], staged=%lu[%lu], queued=%lu[%lu], "
                    "cdc_done=%lu[%lu]\r\n",
                    ( unsigned long )status.result_produced_count,
                    ( unsigned long )status.result_last_produced_tick,
                    ( unsigned long )status.result_staged_count,
                    ( unsigned long )status.result_last_staged_tick,
                    ( unsigned long )status.result_usb_queued_count,
                    ( unsigned long )status.result_last_usb_queued_tick,
                    ( unsigned long )status.result_cdc_completed_count,
                    ( unsigned long )status.result_last_cdc_completed_tick );
    CONSOLE_Printf(
        "  Result pending:      staged=%u, USB batches=%u, USB rejects=%lu, complete=%s, "
        "invariant=%s\r\n",
        ( unsigned int )status.result_staged_message_count,
        ( unsigned int )status.result_outstanding_batch_count,
        ( unsigned long )status.result_usb_batch_rejection_count,
        status.result_custody_complete ? "yes" : "no",
        CONSOLE_HostInterface_ResultInvariantName( status.result_invariant_failure ) );
    CONSOLE_Printf(
        "  USB TX bytes:        accepted=%lu, submitted=%lu, completed=%lu, discarded=%lu, "
        "buffered=%lu, active=%lu, peak=%lu\r\n",
        ( unsigned long )status.usb_tx_diags.accepted_bytes,
        ( unsigned long )status.usb_tx_diags.submitted_bytes,
        ( unsigned long )status.usb_tx_diags.completed_bytes,
        ( unsigned long )status.usb_tx_diags.discarded_bytes,
        ( unsigned long )status.usb_tx_diags.current_buffered_bytes,
        ( unsigned long )status.usb_tx_diags.current_active_bytes,
        ( unsigned long )status.usb_tx_diags.peak_buffered_bytes );
    CONSOLE_Printf(
        "  USB TX operations:   accepted=%lu, CDC submit=%lu (busy/fail=%lu), complete=%lu, "
        "discard=%lu, ring=%lu->%lu\r\n",
        ( unsigned long )status.usb_tx_diags.accepted_request_count,
        ( unsigned long )status.usb_tx_diags.cdc_submit_count,
        ( unsigned long )status.usb_tx_diags.cdc_submit_failure_count,
        ( unsigned long )status.usb_tx_diags.completed_transfer_count,
        ( unsigned long )status.usb_tx_diags.discard_count,
        ( unsigned long )status.usb_tx_diags.live_start,
        ( unsigned long )status.usb_tx_diags.waiting_end );
    CONSOLE_Printf( "  USB TX integrity:    epoch=%lu bytes=%lu/%lu crc=%08lX/%08lX\r\n",
                    ( unsigned long )status.usb_tx_diags.integrity_epoch,
                    ( unsigned long )status.usb_tx_diags.integrity_accepted_bytes,
                    ( unsigned long )status.usb_tx_diags.integrity_submitted_bytes,
                    ( unsigned long )status.usb_tx_diags.accepted_stream_crc32,
                    ( unsigned long )status.usb_tx_diags.submitted_stream_crc32 );
    CONSOLE_Printf( "  Result trace:        retained=%lu, overwritten=%lu\r\n",
                    ( unsigned long )status.result_audit_entry_count,
                    ( unsigned long )status.result_audit_overwrite_count );
    CONSOLE_Printf( "  Effective period:    %lu ms\r\n",
                    ( unsigned long )status.effective_period_ms );
}

static void CONSOLE_HostInterface_PrintTrace( void )
{
    HostInterfaceStatus_T status = { 0 };
    HOST_INTERFACE_GetStatus( &status );
    uint32_t entry_count = status.result_audit_entry_count;
    if ( entry_count > HOST_INTERFACE_RESULT_TX_AUDIT_DEPTH )
    {
        entry_count = HOST_INTERFACE_RESULT_TX_AUDIT_DEPTH;
    }

    CONSOLE_Printf( "Result TX trace: %lu retained, %lu overwritten (oldest to newest)\r\n",
                    ( unsigned long )entry_count,
                    ( unsigned long )status.result_audit_overwrite_count );
    for ( uint32_t remaining = entry_count; remaining > 0U; remaining-- )
    {
        HostInterfaceResultTxAuditEntry_T entry = { 0 };
        if ( !HOST_INTERFACE_GetResultTxAuditEntry( remaining - 1U, &entry ) )
        {
            CONSOLE_Printf( "  trace changed while reading; retry command\r\n" );
            return;
        }
        if ( entry.event == HOST_INTERFACE_RESULT_TX_EVENT_INVARIANT_FAULT )
        {
            CONSOLE_Printf( "  #%lu %-15s expected=%lu actual=%lu invariant=%lu usb=%lu/%lu\r\n",
                            ( unsigned long )entry.sequence,
                            CONSOLE_HostInterface_ResultEventName( entry.event ),
                            ( unsigned long )entry.first_tick, ( unsigned long )entry.last_tick,
                            ( unsigned long )entry.crc32, ( unsigned long )entry.usb_accepted_bytes,
                            ( unsigned long )entry.usb_completed_bytes );
            continue;
        }
        CONSOLE_Printf( "  #%lu %-15s ticks=%lu..%lu msgs=%u bytes=%u crc=%08lX usb=%lu/%lu\r\n",
                        ( unsigned long )entry.sequence,
                        CONSOLE_HostInterface_ResultEventName( entry.event ),
                        ( unsigned long )entry.first_tick, ( unsigned long )entry.last_tick,
                        ( unsigned int )entry.message_count, ( unsigned int )entry.size_bytes,
                        ( unsigned long )entry.crc32, ( unsigned long )entry.usb_accepted_bytes,
                        ( unsigned long )entry.usb_completed_bytes );
    }
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
    else if ( strcmp( argv[1], "trace" ) == 0 )
    {
        CONSOLE_HostInterface_PrintTrace();
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
