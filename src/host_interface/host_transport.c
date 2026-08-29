/******************************************************************************
 *  File:       host_transport.c
 *  Author:     OpenAI
 *  Created:    30-Aug-2026
 *
 *  Description:
 *      Single-owner firmware integration for the shared HIL-RIG Transport.
 *      This temporary hardware-test module owns all caller-side buffers and
 *      bridges the opaque Transport byte stream to the existing USB CDC layer.
 ******************************************************************************/

#include "host_transport.h"

#include "hil_rig_protocol/transport/transport.h"
#include "hw_usb.h"
#include "protocol_test_config.h"
#include "protocol_test_harness.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

_Static_assert( HOST_TRANSPORT_RECENT_EVENT_COUNT == HOST_TRANSPORT_RECENT_EVENT_DIAGNOSTIC_COUNT,
                "Recent event diagnostic capacities must match." );
_Static_assert( HOST_TRANSPORT_MAX_ENCODED_FRAME_SIZE <= HW_USB_TRANSMIT_CAPACITY_BYTES,
                "USB TX ring must accept one complete maximum Transport output." );
_Static_assert( HOST_TRANSPORT_MAX_ENCODED_FRAME_SIZE <= UINT16_MAX,
                "HW_USB_Transmit length is uint16_t." );
_Static_assert( HOST_TRANSPORT_MAX_APPLICATION_MESSAGE_SIZE >= PROTOCOL_TEST_HARNESS_HEADER_SIZE,
                "Harness header must fit one configured Application message." );
_Static_assert( HOST_TRANSPORT_STATUS_COUNT == 11U,
                "Update status diagnostics for a changed public Transport status enum." );
_Static_assert( HOST_TRANSPORT_EVENT_TYPE_COUNT == 8U,
                "Update event diagnostics for a changed public Transport event enum." );

typedef struct
{
    uint32_t receive_operations;
    uint32_t zero_receive_operations;
    uint32_t process_operations;
    uint32_t output_operations;
    uint32_t event_operations;
} HOST_TRANSPORT_Service_Budget_T;

typedef struct
{
    HIL_Transport_Context_T context;
    HIL_Transport_Config_T  config;

    uint8_t pending_rx[HOST_TRANSPORT_PENDING_RX_CAPACITY_BYTES];
    size_t  pending_rx_length;

    uint8_t application_message[HOST_TRANSPORT_MAX_APPLICATION_MESSAGE_SIZE];
    uint8_t pending_response[HOST_TRANSPORT_MAX_APPLICATION_MESSAGE_SIZE];
    size_t  pending_response_length;
    bool    response_pending;

    uint8_t output_buffer[HOST_TRANSPORT_MAX_ENCODED_FRAME_SIZE];

    bool     initialized;
    bool     link_connected;
    bool     service_time_valid;
    uint32_t previous_service_ms;
    uint32_t consecutive_usb_busy_iterations;
} HOST_TRANSPORT_State_T;

_Alignas( HIL_TRANSPORT_WORKSPACE_ALIGNMENT ) static uint8_t
    s_transport_workspace[HOST_TRANSPORT_WORKSPACE_CAPACITY_BYTES];
static HOST_TRANSPORT_State_T s_host_transport = { 0 };

HOST_TRANSPORT_Diagnostics_T g_host_transport_diagnostics = { 0 };

static void HOST_TRANSPORT_Increment( uint32_t* counter )
{
    if ( *counter != UINT32_MAX )
    {
        ( *counter )++;
    }
}

static void HOST_TRANSPORT_Add( uint32_t* counter, size_t value )
{
    uint32_t remaining = UINT32_MAX - *counter;

    if ( value >= remaining )
    {
        *counter = UINT32_MAX;
    }
    else
    {
        *counter += ( uint32_t )value;
    }
}

static void HOST_TRANSPORT_Record_Status( HIL_Transport_Status_T status )
{
    g_host_transport_diagnostics.current_transport_status = ( uint32_t )status;
}

static void HOST_TRANSPORT_Record_Receive_Status( HIL_Transport_Status_T status )
{
    HOST_TRANSPORT_Record_Status( status );
    if ( ( uint32_t )status < HOST_TRANSPORT_STATUS_COUNT )
    {
        HOST_TRANSPORT_Increment(
            &g_host_transport_diagnostics.receive_status_counts[( uint32_t )status] );
    }
}

static void HOST_TRANSPORT_Update_Pending_RX_Diagnostics( void )
{
    g_host_transport_diagnostics.pending_rx_length = ( uint32_t )s_host_transport.pending_rx_length;
    if ( g_host_transport_diagnostics.pending_rx_length
         > g_host_transport_diagnostics.pending_rx_high_water )
    {
        g_host_transport_diagnostics.pending_rx_high_water =
            g_host_transport_diagnostics.pending_rx_length;
    }
}

static void HOST_TRANSPORT_Clear_Caller_State( void )
{
    s_host_transport.pending_rx_length               = 0U;
    s_host_transport.pending_response_length         = 0U;
    s_host_transport.response_pending                = false;
    s_host_transport.consecutive_usb_busy_iterations = 0U;
    g_host_transport_diagnostics.pending_rx_length   = 0U;
}

static void HOST_TRANSPORT_Update_Status_Snapshot( void )
{
    HIL_Transport_Status_Snapshot_T snapshot = { 0 };
    HIL_Transport_Status_T          status =
        HIL_TRANSPORT_Get_Status( &s_host_transport.context, &snapshot );

    HOST_TRANSPORT_Record_Status( status );
    if ( status == HIL_TRANSPORT_STATUS_OK )
    {
        g_host_transport_diagnostics.link_state              = ( uint32_t )snapshot.link_state;
        g_host_transport_diagnostics.transport_session_state = ( uint32_t )snapshot.session_state;
        g_host_transport_diagnostics.transport_last_failure  = ( uint32_t )snapshot.last_failure;
    }
}

static void HOST_TRANSPORT_Record_Event( const HIL_Transport_Event_T* event )
{
    HOST_TRANSPORT_Event_Diagnostic_T diagnostic = {
        .type              = ( uint32_t )event->type,
        .status            = ( uint32_t )event->status,
        .failure           = ( uint32_t )event->failure,
        .required_capacity = event->required_capacity > UINT32_MAX
                                 ? UINT32_MAX
                                 : ( uint32_t )event->required_capacity,
    };
    uint32_t index = g_host_transport_diagnostics.recent_event_write_index;

    HOST_TRANSPORT_Increment( &g_host_transport_diagnostics.transport_event_count );
    if ( ( uint32_t )event->type < HOST_TRANSPORT_EVENT_TYPE_COUNT )
    {
        HOST_TRANSPORT_Increment(
            &g_host_transport_diagnostics.transport_event_counts[( uint32_t )event->type] );
    }

    g_host_transport_diagnostics.most_recent_event    = diagnostic;
    g_host_transport_diagnostics.recent_events[index] = diagnostic;
    index++;
    if ( index >= HOST_TRANSPORT_RECENT_EVENT_DIAGNOSTIC_COUNT )
    {
        index = 0U;
    }
    g_host_transport_diagnostics.recent_event_write_index = index;
}

static bool HOST_TRANSPORT_Drain_Events( HOST_TRANSPORT_Service_Budget_T* budget )
{
    bool drained_any = false;

    while ( budget->event_operations < HOST_TRANSPORT_MAX_EVENT_OPERATIONS_PER_SERVICE )
    {
        HIL_Transport_Event_T  event = { 0 };
        HIL_Transport_Status_T status =
            HIL_TRANSPORT_Read_Event( &s_host_transport.context, &event );

        budget->event_operations++;
        HOST_TRANSPORT_Record_Status( status );
        if ( status == HIL_TRANSPORT_STATUS_NOT_READY )
        {
            return drained_any;
        }
        if ( status != HIL_TRANSPORT_STATUS_OK )
        {
            return drained_any;
        }

        HOST_TRANSPORT_Record_Event( &event );
        drained_any = true;
    }

    return drained_any;
}

static void HOST_TRANSPORT_Try_Submit_Response( void )
{
    HIL_Transport_Status_T status;

    if ( !s_host_transport.response_pending )
    {
        return;
    }

    status = HIL_TRANSPORT_Submit_Application_Data( &s_host_transport.context,
                                                    s_host_transport.pending_response,
                                                    s_host_transport.pending_response_length );
    HOST_TRANSPORT_Record_Status( status );

    if ( status == HIL_TRANSPORT_STATUS_OK )
    {
        s_host_transport.response_pending        = false;
        s_host_transport.pending_response_length = 0U;
        HOST_TRANSPORT_Increment( &g_host_transport_diagnostics.responses_submitted );
        return;
    }

    if ( status == HIL_TRANSPORT_STATUS_NOT_READY
         || status == HIL_TRANSPORT_STATUS_CAPACITY_EXHAUSTED )
    {
        HOST_TRANSPORT_Increment( &g_host_transport_diagnostics.response_submit_retries );
        return;
    }

    /* A locally generated response should never hit a permanent submission error. */
    s_host_transport.response_pending        = false;
    s_host_transport.pending_response_length = 0U;
    HOST_TRANSPORT_Increment( &g_host_transport_diagnostics.response_submit_failures );
}

static bool HOST_TRANSPORT_Service_Output( uint32_t                         now_ms,
                                           HOST_TRANSPORT_Service_Budget_T* budget )
{
    bool committed_any = false;

    while ( budget->output_operations < HOST_TRANSPORT_MAX_OUTPUT_OPERATIONS_PER_SERVICE )
    {
        HIL_Transport_Status_T status;
        size_t                 output_size = 0U;
        uint32_t               output_generation;

        status =
            HIL_TRANSPORT_Peek_Output( &s_host_transport.context, s_host_transport.output_buffer,
                                       sizeof( s_host_transport.output_buffer ), &output_size );
        budget->output_operations++;
        HOST_TRANSPORT_Record_Status( status );

        if ( status == HIL_TRANSPORT_STATUS_NOT_READY )
        {
            s_host_transport.consecutive_usb_busy_iterations = 0U;
            return committed_any;
        }
        if ( status != HIL_TRANSPORT_STATUS_OK )
        {
            return committed_any;
        }

        HOST_TRANSPORT_Increment( &g_host_transport_diagnostics.output_items_peeked );
        output_generation = g_host_transport_diagnostics.link_generation;

        if ( !s_host_transport.link_connected || output_size > UINT16_MAX )
        {
            return committed_any;
        }

        if ( !HW_USB_Transmit( s_host_transport.output_buffer, ( uint16_t )output_size ) )
        {
            HOST_TRANSPORT_Increment( &g_host_transport_diagnostics.usb_tx_busy_retries );
            HOST_TRANSPORT_Increment( &s_host_transport.consecutive_usb_busy_iterations );
            if ( s_host_transport.consecutive_usb_busy_iterations
                 > g_host_transport_diagnostics.maximum_consecutive_usb_busy_iterations )
            {
                g_host_transport_diagnostics.maximum_consecutive_usb_busy_iterations =
                    s_host_transport.consecutive_usb_busy_iterations;
            }
            return committed_any;
        }

        s_host_transport.consecutive_usb_busy_iterations = 0U;
        HOST_TRANSPORT_Increment( &g_host_transport_diagnostics.usb_output_acceptances );
        HOST_TRANSPORT_Add( &g_host_transport_diagnostics.usb_tx_bytes, output_size );

        /* Do not commit bytes that belong to a link generation already observed as stale. */
        if ( !s_host_transport.link_connected
             || output_generation != g_host_transport_diagnostics.link_generation
             || !HW_USB_Is_Connected() )
        {
            return committed_any;
        }

        status = HIL_TRANSPORT_Commit_Output( &s_host_transport.context, now_ms );
        HOST_TRANSPORT_Record_Status( status );
        if ( status == HIL_TRANSPORT_STATUS_OK )
        {
            HOST_TRANSPORT_Increment( &g_host_transport_diagnostics.output_commits );
            committed_any = true;
        }
        else
        {
            HOST_TRANSPORT_Increment( &g_host_transport_diagnostics.output_commit_failures );
            return committed_any;
        }
    }

    return committed_any;
}

static void HOST_TRANSPORT_Consume_Pending_RX( HOST_TRANSPORT_Service_Budget_T* budget,
                                               bool* resume_receive_needed )
{
    while ( budget->receive_operations < HOST_TRANSPORT_MAX_RECEIVE_OPERATIONS_PER_SERVICE
            && s_host_transport.pending_rx_length > 0U )
    {
        size_t                 offered  = s_host_transport.pending_rx_length;
        size_t                 consumed = 0U;
        HIL_Transport_Status_T status;

        status = HIL_TRANSPORT_Receive_Bytes( &s_host_transport.context,
                                              s_host_transport.pending_rx, offered, &consumed );
        budget->receive_operations++;
        HOST_TRANSPORT_Record_Receive_Status( status );
        HOST_TRANSPORT_Add( &g_host_transport_diagnostics.transport_rx_bytes_offered, offered );
        HOST_TRANSPORT_Add( &g_host_transport_diagnostics.transport_rx_bytes_consumed, consumed );

        if ( consumed < offered )
        {
            HOST_TRANSPORT_Increment(
                &g_host_transport_diagnostics.transport_rx_partial_consumption_calls );
            *resume_receive_needed = true;
        }
        if ( consumed == 0U )
        {
            HOST_TRANSPORT_Increment(
                &g_host_transport_diagnostics.transport_rx_zero_progress_calls );
        }

        if ( consumed > s_host_transport.pending_rx_length )
        {
            consumed = s_host_transport.pending_rx_length;
        }

        if ( consumed > 0U )
        {
            s_host_transport.pending_rx_length -= consumed;
            if ( s_host_transport.pending_rx_length > 0U )
            {
                memmove( s_host_transport.pending_rx, &s_host_transport.pending_rx[consumed],
                         s_host_transport.pending_rx_length );
            }
            HOST_TRANSPORT_Update_Pending_RX_Diagnostics();
        }

        if ( status == HIL_TRANSPORT_STATUS_NOT_READY
             || status == HIL_TRANSPORT_STATUS_INTERNAL_ERROR || consumed == 0U )
        {
            return;
        }
    }
}

static void HOST_TRANSPORT_Read_USB_Input( HOST_TRANSPORT_Service_Budget_T* budget,
                                           bool*                            resume_receive_needed )
{
    while ( budget->receive_operations < HOST_TRANSPORT_MAX_RECEIVE_OPERATIONS_PER_SERVICE )
    {
        size_t free_bytes =
            sizeof( s_host_transport.pending_rx ) - s_host_transport.pending_rx_length;
        uint32_t read_limit;
        uint32_t bytes_read;

        HOST_TRANSPORT_Consume_Pending_RX( budget, resume_receive_needed );
        if ( budget->receive_operations >= HOST_TRANSPORT_MAX_RECEIVE_OPERATIONS_PER_SERVICE )
        {
            return;
        }

        free_bytes = sizeof( s_host_transport.pending_rx ) - s_host_transport.pending_rx_length;
        if ( free_bytes == 0U )
        {
            HOST_TRANSPORT_Increment( &g_host_transport_diagnostics.prevented_rx_overflows );
            return;
        }

        read_limit = free_bytes > HOST_TRANSPORT_USB_READ_CHUNK_BYTES
                         ? HOST_TRANSPORT_USB_READ_CHUNK_BYTES
                         : ( uint32_t )free_bytes;
        bytes_read = HW_USB_Receive(
            &s_host_transport.pending_rx[s_host_transport.pending_rx_length], read_limit );
        if ( bytes_read == 0U )
        {
            return;
        }

        if ( bytes_read > free_bytes )
        {
            bytes_read = ( uint32_t )free_bytes;
            HOST_TRANSPORT_Increment( &g_host_transport_diagnostics.prevented_rx_overflows );
        }

        s_host_transport.pending_rx_length += bytes_read;
        HOST_TRANSPORT_Add( &g_host_transport_diagnostics.usb_rx_bytes, bytes_read );
        HOST_TRANSPORT_Update_Pending_RX_Diagnostics();
    }
}

static void HOST_TRANSPORT_Process( uint32_t now_ms, HOST_TRANSPORT_Service_Budget_T* budget )
{
    if ( budget->process_operations >= HOST_TRANSPORT_MAX_PROCESS_OPERATIONS_PER_SERVICE )
    {
        return;
    }

    HIL_Transport_Status_T status = HIL_TRANSPORT_Process( &s_host_transport.context, now_ms,
                                                           HIL_TRANSPORT_OPERATING_MODE_NORMAL );
    budget->process_operations++;
    HOST_TRANSPORT_Record_Status( status );
}

static bool HOST_TRANSPORT_Read_Application_Message( void )
{
    HIL_Transport_Status_T              status;
    PROTOCOL_TEST_HARNESS_Result_T      harness_result;
    PROTOCOL_TEST_HARNESS_Status_Data_T status_data   = { 0 };
    size_t                              message_size  = 0U;
    size_t                              response_size = 0U;

    if ( s_host_transport.response_pending )
    {
        return false;
    }

    status = HIL_TRANSPORT_Read_Application_Data(
        &s_host_transport.context, s_host_transport.application_message,
        sizeof( s_host_transport.application_message ), &message_size );
    HOST_TRANSPORT_Record_Status( status );
    if ( status != HIL_TRANSPORT_STATUS_OK )
    {
        return false;
    }

    HOST_TRANSPORT_Increment( &g_host_transport_diagnostics.application_requests_received );
    HOST_TRANSPORT_Update_Status_Snapshot();

    status_data.link_state            = g_host_transport_diagnostics.link_state;
    status_data.link_generation       = g_host_transport_diagnostics.link_generation;
    status_data.transport_event_count = g_host_transport_diagnostics.transport_event_count;
    status_data.usb_rx_bytes          = g_host_transport_diagnostics.usb_rx_bytes;
    status_data.usb_tx_bytes          = g_host_transport_diagnostics.usb_tx_bytes;
    status_data.application_requests_received =
        g_host_transport_diagnostics.application_requests_received;
    status_data.responses_submitted      = g_host_transport_diagnostics.responses_submitted;
    status_data.usb_tx_busy_retries      = g_host_transport_diagnostics.usb_tx_busy_retries;
    status_data.invalid_harness_messages = g_host_transport_diagnostics.invalid_harness_messages;
    status_data.maximum_service_gap_ms   = g_host_transport_diagnostics.maximum_service_gap_ms;
    status_data.transport_session_state  = g_host_transport_diagnostics.transport_session_state;

    harness_result = PROTOCOL_TEST_HARNESS_Build_Response(
        s_host_transport.application_message, message_size,
        s_host_transport.config.max_application_message_size, &status_data,
        s_host_transport.pending_response, sizeof( s_host_transport.pending_response ),
        &response_size );

    if ( harness_result != PROTOCOL_TEST_HARNESS_RESULT_OK )
    {
        HOST_TRANSPORT_Increment( &g_host_transport_diagnostics.invalid_harness_messages );
        return true;
    }

    s_host_transport.pending_response_length = response_size;
    s_host_transport.response_pending        = true;
    HOST_TRANSPORT_Try_Submit_Response();
    return true;
}

static void HOST_TRANSPORT_Zero_Receive( HOST_TRANSPORT_Service_Budget_T* budget )
{
    size_t                 consumed = 0U;
    HIL_Transport_Status_T status;

    if ( budget->zero_receive_operations >= HOST_TRANSPORT_MAX_ZERO_RECEIVE_OPERATIONS_PER_SERVICE )
    {
        return;
    }

    status = HIL_TRANSPORT_Receive_Bytes( &s_host_transport.context, NULL, 0U, &consumed );
    budget->zero_receive_operations++;
    HOST_TRANSPORT_Increment( &g_host_transport_diagnostics.transport_rx_zero_byte_calls );
    HOST_TRANSPORT_Record_Receive_Status( status );
    if ( consumed == 0U )
    {
        HOST_TRANSPORT_Increment( &g_host_transport_diagnostics.transport_rx_zero_progress_calls );
    }
}

static void
HOST_TRANSPORT_Update_Budget_Diagnostics( const HOST_TRANSPORT_Service_Budget_T* budget )
{
    HIL_Transport_Status_Snapshot_T snapshot = { 0 };
    HIL_Transport_Status_T          status =
        HIL_TRANSPORT_Get_Status( &s_host_transport.context, &snapshot );
    bool exhausted = false;

    HOST_TRANSPORT_Record_Status( status );
    if ( budget->receive_operations >= HOST_TRANSPORT_MAX_RECEIVE_OPERATIONS_PER_SERVICE
         && s_host_transport.pending_rx_length > 0U )
    {
        exhausted = true;
    }
    if ( status == HIL_TRANSPORT_STATUS_OK )
    {
        if ( budget->event_operations >= HOST_TRANSPORT_MAX_EVENT_OPERATIONS_PER_SERVICE
             && snapshot.event_pending != 0U )
        {
            exhausted = true;
        }
        if ( budget->output_operations >= HOST_TRANSPORT_MAX_OUTPUT_OPERATIONS_PER_SERVICE
             && snapshot.output_pending != 0U )
        {
            exhausted = true;
        }
    }

    if ( exhausted )
    {
        HOST_TRANSPORT_Increment( &g_host_transport_diagnostics.operation_budget_exhaustions );
    }
}

static bool HOST_TRANSPORT_Init_Internal( size_t workspace_capacity )
{
    HIL_Transport_Storage_T storage = {
        .workspace      = s_transport_workspace,
        .workspace_size = workspace_capacity,
    };
    size_t                 required_size = 0U;
    HIL_Transport_Status_T status;

    if ( workspace_capacity > sizeof( s_transport_workspace ) )
    {
        workspace_capacity     = sizeof( s_transport_workspace );
        storage.workspace_size = workspace_capacity;
    }

    if ( s_host_transport.initialized )
    {
        return true;
    }

    memset( &s_host_transport, 0, sizeof( s_host_transport ) );
    memset( &g_host_transport_diagnostics, 0, sizeof( g_host_transport_diagnostics ) );
    g_host_transport_diagnostics.initialization_attempted  = 1U;
    g_host_transport_diagnostics.available_workspace_bytes = ( uint32_t )workspace_capacity;
    g_host_transport_diagnostics.workspace_alignment_bytes = HIL_TRANSPORT_WORKSPACE_ALIGNMENT;
    g_host_transport_diagnostics.link_state                = HIL_TRANSPORT_LINK_STATE_DISCONNECTED;

    HIL_TRANSPORT_Default_Config( &s_host_transport.config );
    s_host_transport.config.max_application_message_size =
        HOST_TRANSPORT_MAX_APPLICATION_MESSAGE_SIZE;
    s_host_transport.config.max_encoded_frame_size    = HOST_TRANSPORT_MAX_ENCODED_FRAME_SIZE;
    s_host_transport.config.session_seed              = HOST_TRANSPORT_SESSION_SEED;
    s_host_transport.config.initial_reliable_sequence = HOST_TRANSPORT_INITIAL_RELIABLE_SEQUENCE;
    s_host_transport.config.connection_timeout_ms     = HOST_TRANSPORT_CONNECTION_TIMEOUT_MS;
    s_host_transport.config.retransmit_timeout_ms     = HOST_TRANSPORT_RETRANSMIT_TIMEOUT_MS;
    s_host_transport.config.max_retries               = HOST_TRANSPORT_MAX_RETRIES;

    status = HIL_TRANSPORT_Required_Storage_Size( &s_host_transport.config, &required_size );
    g_host_transport_diagnostics.required_workspace_bytes =
        required_size > UINT32_MAX ? UINT32_MAX : ( uint32_t )required_size;
    g_host_transport_diagnostics.initialization_status = ( uint32_t )status;
    HOST_TRANSPORT_Record_Status( status );
    if ( status != HIL_TRANSPORT_STATUS_OK || required_size > workspace_capacity )
    {
        if ( status == HIL_TRANSPORT_STATUS_OK )
        {
            g_host_transport_diagnostics.initialization_status =
                HIL_TRANSPORT_STATUS_BUFFER_TOO_SMALL;
            HOST_TRANSPORT_Record_Status( HIL_TRANSPORT_STATUS_BUFFER_TOO_SMALL );
        }
        return false;
    }

    if ( ( ( uintptr_t )s_transport_workspace % HIL_TRANSPORT_WORKSPACE_ALIGNMENT ) != 0U )
    {
        g_host_transport_diagnostics.initialization_status = HIL_TRANSPORT_STATUS_INVALID_ARGUMENT;
        HOST_TRANSPORT_Record_Status( HIL_TRANSPORT_STATUS_INVALID_ARGUMENT );
        return false;
    }

    status = HIL_TRANSPORT_Init( &s_host_transport.context, HIL_TRANSPORT_ROLE_RIG,
                                 &s_host_transport.config, &storage );
    g_host_transport_diagnostics.initialization_status = ( uint32_t )status;
    HOST_TRANSPORT_Record_Status( status );
    if ( status != HIL_TRANSPORT_STATUS_OK )
    {
        return false;
    }

    s_host_transport.initialized                           = true;
    g_host_transport_diagnostics.initialization_successful = 1U;
    HOST_TRANSPORT_Update_Status_Snapshot();
    return true;
}

bool HOST_TRANSPORT_Init( void )
{
    return HOST_TRANSPORT_Init_Internal( sizeof( s_transport_workspace ) );
}

void HOST_TRANSPORT_Set_USB_Initialization_Status( bool initialized )
{
    g_host_transport_diagnostics.usb_initialization_successful = initialized ? 1U : 0U;
}

void HOST_TRANSPORT_Set_Link_State( bool connected, uint32_t now_ms )
{
    HIL_Transport_Link_State_T link_state;
    HIL_Transport_Status_T     status;

    if ( !s_host_transport.initialized || connected == s_host_transport.link_connected )
    {
        return;
    }

    HOST_TRANSPORT_Clear_Caller_State();
    HW_USB_Discard_Protocol_Buffers();

    s_host_transport.link_connected = connected;
    link_state =
        connected ? HIL_TRANSPORT_LINK_STATE_CONNECTED : HIL_TRANSPORT_LINK_STATE_DISCONNECTED;

    if ( connected )
    {
        HOST_TRANSPORT_Increment( &g_host_transport_diagnostics.link_generation );
    }

    status = HIL_TRANSPORT_Notify_Link_State( &s_host_transport.context, link_state, now_ms );
    HOST_TRANSPORT_Record_Status( status );
    g_host_transport_diagnostics.link_state = ( uint32_t )link_state;
    HOST_TRANSPORT_Update_Status_Snapshot();
}

void HOST_TRANSPORT_Service( uint32_t now_ms )
{
    HOST_TRANSPORT_Service_Budget_T budget                = { 0 };
    bool                            resume_receive_needed = false;
    bool                            capacity_released     = false;
    bool                            application_read      = false;

    if ( !s_host_transport.initialized )
    {
        return;
    }

    HOST_TRANSPORT_Increment( &g_host_transport_diagnostics.task_service_count );
    if ( s_host_transport.service_time_valid )
    {
        uint32_t service_gap = now_ms - s_host_transport.previous_service_ms;
        g_host_transport_diagnostics.current_service_gap_ms = service_gap;
        if ( service_gap > g_host_transport_diagnostics.maximum_service_gap_ms )
        {
            g_host_transport_diagnostics.maximum_service_gap_ms = service_gap;
        }
    }
    else
    {
        s_host_transport.service_time_valid = true;
    }
    s_host_transport.previous_service_ms = now_ms;

    if ( !s_host_transport.link_connected )
    {
        ( void )HOST_TRANSPORT_Drain_Events( &budget );
        HOST_TRANSPORT_Update_Status_Snapshot();
        HOST_TRANSPORT_Update_Budget_Diagnostics( &budget );
        g_host_transport_diagnostics.usb_rx_dropped_bytes =
            HW_USB_Get_Receive_Stream_Dropped_Bytes();
        g_host_transport_diagnostics.usb_tx_ring_high_water_bytes =
            HW_USB_Get_Transmit_Buffer_High_Water_Bytes();
        return;
    }

    HOST_TRANSPORT_Try_Submit_Response();
    capacity_released = HOST_TRANSPORT_Service_Output( now_ms, &budget );
    HOST_TRANSPORT_Read_USB_Input( &budget, &resume_receive_needed );
    HOST_TRANSPORT_Process( now_ms, &budget );
    capacity_released = HOST_TRANSPORT_Drain_Events( &budget ) || capacity_released;

    if ( !s_host_transport.response_pending )
    {
        application_read      = HOST_TRANSPORT_Read_Application_Message();
        resume_receive_needed = resume_receive_needed || application_read;
    }

    if ( resume_receive_needed || capacity_released )
    {
        HOST_TRANSPORT_Zero_Receive( &budget );
        HOST_TRANSPORT_Process( now_ms, &budget );
        ( void )HOST_TRANSPORT_Drain_Events( &budget );
    }

    ( void )HOST_TRANSPORT_Service_Output( now_ms, &budget );
    HOST_TRANSPORT_Update_Status_Snapshot();
    HOST_TRANSPORT_Update_Budget_Diagnostics( &budget );
    g_host_transport_diagnostics.usb_rx_dropped_bytes = HW_USB_Get_Receive_Stream_Dropped_Bytes();
    g_host_transport_diagnostics.usb_tx_ring_high_water_bytes =
        HW_USB_Get_Transmit_Buffer_High_Water_Bytes();
}

void HOST_TRANSPORT_Set_Task_Stack_High_Water( uint32_t high_water_words )
{
    g_host_transport_diagnostics.task_stack_high_water_words = high_water_words;
}

bool HOST_TRANSPORT_Is_Initialized( void )
{
    return s_host_transport.initialized;
}

const HOST_TRANSPORT_Diagnostics_T* HOST_TRANSPORT_Get_Diagnostics( void )
{
    return &g_host_transport_diagnostics;
}

#ifdef TEST_BUILD
void HOST_TRANSPORT_Test_Reset( void )
{
    memset( &s_host_transport, 0, sizeof( s_host_transport ) );
    memset( &g_host_transport_diagnostics, 0, sizeof( g_host_transport_diagnostics ) );
}

bool HOST_TRANSPORT_Test_Init_With_Workspace_Capacity( uint32_t workspace_capacity )
{
    return HOST_TRANSPORT_Init_Internal( workspace_capacity );
}

bool HOST_TRANSPORT_Test_Seed_Pending_Response( const uint8_t* response, uint32_t length )
{
    if ( response == NULL || length == 0U || length > sizeof( s_host_transport.pending_response ) )
    {
        return false;
    }

    memcpy( s_host_transport.pending_response, response, length );
    s_host_transport.pending_response_length = length;
    s_host_transport.response_pending        = true;
    return true;
}

bool HOST_TRANSPORT_Test_Response_Is_Pending( void )
{
    return s_host_transport.response_pending;
}

bool HOST_TRANSPORT_Test_Try_Read_Application_Message( void )
{
    if ( s_host_transport.response_pending )
    {
        return false;
    }

    return HOST_TRANSPORT_Read_Application_Message();
}

uint32_t HOST_TRANSPORT_Test_Copy_Pending_RX( uint8_t* destination, uint32_t capacity )
{
    size_t copy_size = s_host_transport.pending_rx_length;

    if ( destination == NULL || capacity == 0U )
    {
        return ( uint32_t )copy_size;
    }

    if ( copy_size > capacity )
    {
        copy_size = capacity;
    }
    memcpy( destination, s_host_transport.pending_rx, copy_size );
    return ( uint32_t )copy_size;
}
#endif
