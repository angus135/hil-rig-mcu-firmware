/******************************************************************************
 *  File:       host_interface.c
 *  Author:     Tim Vogelsang
 *  Created:    6-Sep-2026
 *
 *  Description:
 *      Runs host transport processing from task context.
 *
 *  Notes:
 *      Instruction upload must use the Flash Manager public lifecycle. The
 *      current periodic task owns Transport and Application codec plumbing;
 *      application semantics are dispatched separately by this task.
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "hil_rig_protocol/application/application.h"
#include "hil_rig_protocol/transport/transport.h"
#include "hw_usb.h"
#include "host_interface.h"
#include "rtos_config.h"

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */

#define HOST_INTERFACE_PERIOD_MS ( 1U )
#define HOST_INTERFACE_USB_RECEIVE_CAPACITY ( 512U )
#define HOST_INTERFACE_APPLICATION_MESSAGE_CAPACITY ( HIL_APPLICATION_DEFAULT_MAX_MESSAGE_SIZE )
#define HOST_INTERFACE_APPLICATION_DECODE_CAPACITY                                                 \
    ( HIL_APPLICATION_ABSOLUTE_MAX_VARIABLE_DATA_SIZE )
#define HOST_INTERFACE_TRANSPORT_WORKSPACE_CAPACITY ( 4096U )
#define HOST_INTERFACE_TRANSPORT_OUTPUT_CAPACITY ( HIL_TRANSPORT_DEFAULT_MAX_ENCODED_FRAME_SIZE )
#define HOST_INTERFACE_TRANSPORT_RETRANSMIT_TIMEOUT_MS ( 100U )
#define HOST_INTERFACE_TRANSPORT_MAX_RETRIES ( 5U )

#ifndef TEST_BUILD
#include "main.h"
#define HOST_INTERFACE_Error_Handler() Error_Handler()
#else
#define HOST_INTERFACE_Error_Handler()
#endif

/**-----------------------------------------------------------------------------
 *  Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

typedef struct
{
    uint32_t receive_count;
    uint32_t receive_offset;
    uint8_t  receive_buffer[HOST_INTERFACE_USB_RECEIVE_CAPACITY];
} HOST_INTERFACE_USB_State_T;

typedef struct
{
    HIL_Application_Context_T context;
    HIL_Application_Config_T  config;
    uint8_t                   send_byte_span[HOST_INTERFACE_APPLICATION_MESSAGE_CAPACITY];
    size_t                    used_send_byte_span_size;
    uint8_t                   receive_byte_span[HOST_INTERFACE_APPLICATION_MESSAGE_CAPACITY];
    size_t                    used_receive_byte_span_size;
    _Alignas( HIL_APPLICATION_DECODE_STORAGE_ALIGNMENT ) uint8_t
        receive_data[HOST_INTERFACE_APPLICATION_DECODE_CAPACITY];
    size_t                   used_receive_data_size;
    HIL_Application_Status_T status;
} HOST_INTERFACE_Application_State_T;

typedef struct
{
    HIL_Transport_Context_T context;
    HIL_Transport_Config_T  config;
    HIL_Transport_Role_T    role;
    HIL_Transport_Storage_T storage;
    _Alignas( HIL_TRANSPORT_WORKSPACE_ALIGNMENT )
        uint8_t workspace[HOST_INTERFACE_TRANSPORT_WORKSPACE_CAPACITY];
    size_t                 required_workspace_size;
    uint8_t                output_buffer[HOST_INTERFACE_TRANSPORT_OUTPUT_CAPACITY];
    size_t                 used_output_buffer_size;
    HIL_Transport_Status_T status;
    HIL_Transport_Event_T  event;
} HOST_INTERFACE_Transport_State_T;

typedef struct
{
    HOST_INTERFACE_USB_State_T         usb;
    HOST_INTERFACE_Application_State_T application;
    HOST_INTERFACE_Transport_State_T   transport;
    TickType_t                         initial_ticks;
    HIL_Transport_Link_State_T         observed_link_state;
    bool                               link_state_observed;
} HOST_INTERFACE_Protocol_State_T;

/**-----------------------------------------------------------------------------
 *  Public (global) and Extern Variables
 *------------------------------------------------------------------------------
 */

TaskHandle_t* HostInterfaceTaskHandle = NULL;  // NOLINT(readability-identifier-naming)

/**-----------------------------------------------------------------------------
 *  Private (static) Function Prototypes
 *------------------------------------------------------------------------------
 */

static void HOST_INTERFACE_Protocol_Process( HOST_INTERFACE_Protocol_State_T* protocol_state,
                                             const HIL_Application_Message_T* outgoing_message,
                                             bool*                      outgoing_message_accepted,
                                             HIL_Application_Message_T* incoming_message,
                                             bool* incoming_message_available );
static void HOST_INTERFACE_Protocol_Init( HOST_INTERFACE_Protocol_State_T* protocol_state );
static void
HOST_INTERFACE_Protocol_Update_Link_State( HOST_INTERFACE_Protocol_State_T* protocol_state,
                                           HIL_Transport_Link_State_T       observed_link_state,
                                           uint32_t                         now );

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

static void
HOST_INTERFACE_Protocol_Update_Link_State( HOST_INTERFACE_Protocol_State_T* const protocol_state,
                                           const HIL_Transport_Link_State_T observed_link_state,
                                           const uint32_t                   now )
{
    if ( ( protocol_state->link_state_observed == true )
         && ( protocol_state->observed_link_state == observed_link_state ) )
    {
        return;
    }

    if ( observed_link_state == HIL_TRANSPORT_LINK_STATE_DISCONNECTED )
    {
        // Do not offer bytes retained from an abandoned physical link to a new session.
        protocol_state->usb.receive_count  = 0U;
        protocol_state->usb.receive_offset = 0U;
        HW_USB_Discard_Transmit_Data();

        while ( HW_USB_Get_Receive_Stream_Used_Bytes() > 0U )
        {
            if ( HW_USB_Receive( protocol_state->usb.receive_buffer,
                                 sizeof( protocol_state->usb.receive_buffer ) )
                 == 0U )
            {
                break;
            }
        }
    }

    protocol_state->transport.status = HIL_TRANSPORT_Notify_Link_State(
        &protocol_state->transport.context, observed_link_state, now );
    protocol_state->observed_link_state = observed_link_state;
    protocol_state->link_state_observed = true;

    if ( protocol_state->transport.status == HIL_TRANSPORT_STATUS_INTERNAL_ERROR
         || protocol_state->transport.status == HIL_TRANSPORT_STATUS_INVALID_ARGUMENT )
    {
        HOST_INTERFACE_Error_Handler();
    }
}

static void HOST_INTERFACE_Protocol_Process(
    HOST_INTERFACE_Protocol_State_T* const protocol_state,
    const HIL_Application_Message_T* const outgoing_message, bool* const outgoing_message_accepted,
    HIL_Application_Message_T* const incoming_message, bool* const incoming_message_available )
{
    const uint32_t             now = ( uint32_t )xTaskGetTickCount();
    HIL_Transport_Link_State_T observed_link_state;

    *outgoing_message_accepted  = false;
    *incoming_message_available = false;
    *incoming_message           = ( HIL_Application_Message_T ){ 0 };

    // The Application decoder uses state-owned storage for any variable spans.
    protocol_state->application.used_receive_data_size = 0U;

    // =======------- SERVICE USB AND LINK STATE
    observed_link_state = ( HW_USB_Get_Link_State() == HW_USB_LINK_STATE_CONNECTED )
                              ? HIL_TRANSPORT_LINK_STATE_CONNECTED
                              : HIL_TRANSPORT_LINK_STATE_DISCONNECTED;

    HOST_INTERFACE_Protocol_Update_Link_State( protocol_state, observed_link_state, now );
    HW_USB_Monitor_Process();

    // =======------- DRAIN TRANSPORT EVENTS
    while ( true )
    {
        protocol_state->transport.status = HIL_TRANSPORT_Read_Event(
            &protocol_state->transport.context, &protocol_state->transport.event );
        if ( protocol_state->transport.status == HIL_TRANSPORT_STATUS_NOT_READY )
        {
            break;
        }
        if ( protocol_state->transport.status != HIL_TRANSPORT_STATUS_OK )
        {
            HOST_INTERFACE_Error_Handler();
        }
    }

    // =======------- ADVANCE TRANSPORT
    protocol_state->transport.status = HIL_TRANSPORT_Process(
        &protocol_state->transport.context, now, HIL_TRANSPORT_OPERATING_MODE_NORMAL );
    if ( protocol_state->transport.status == HIL_TRANSPORT_STATUS_INTERNAL_ERROR
         || protocol_state->transport.status == HIL_TRANSPORT_STATUS_INVALID_ARGUMENT )
    {
        HOST_INTERFACE_Error_Handler();
    }

    // =======------- RECEIVE USB BYTE STREAM
    if ( observed_link_state == HIL_TRANSPORT_LINK_STATE_CONNECTED )
    {
        if ( protocol_state->usb.receive_offset == protocol_state->usb.receive_count )
        {
            protocol_state->usb.receive_count = HW_USB_Receive(
                protocol_state->usb.receive_buffer, sizeof( protocol_state->usb.receive_buffer ) );
            protocol_state->usb.receive_offset = 0U;
        }

        if ( protocol_state->usb.receive_offset < protocol_state->usb.receive_count )
        {
            const size_t bytes_available = ( size_t )( protocol_state->usb.receive_count
                                                       - protocol_state->usb.receive_offset );
            size_t       bytes_consumed  = 0U;

            protocol_state->transport.status = HIL_TRANSPORT_Receive_Bytes(
                &protocol_state->transport.context,
                &protocol_state->usb.receive_buffer[protocol_state->usb.receive_offset],
                bytes_available, &bytes_consumed );

            if ( bytes_consumed > bytes_available
                 || ( ( protocol_state->transport.status == HIL_TRANSPORT_STATUS_OK )
                      && ( bytes_consumed == 0U ) ) )
            {
                HOST_INTERFACE_Error_Handler();
            }

            protocol_state->usb.receive_offset += ( uint32_t )bytes_consumed;
            if ( protocol_state->usb.receive_offset == protocol_state->usb.receive_count )
            {
                protocol_state->usb.receive_count  = 0U;
                protocol_state->usb.receive_offset = 0U;
            }

            if ( protocol_state->transport.status == HIL_TRANSPORT_STATUS_INTERNAL_ERROR
                 || protocol_state->transport.status == HIL_TRANSPORT_STATUS_INVALID_ARGUMENT )
            {
                HOST_INTERFACE_Error_Handler();
            }
        }
    }

    // Retry retained parser work after event draining and byte delivery.
    protocol_state->transport.status = HIL_TRANSPORT_Process(
        &protocol_state->transport.context, now, HIL_TRANSPORT_OPERATING_MODE_NORMAL );
    if ( protocol_state->transport.status == HIL_TRANSPORT_STATUS_INTERNAL_ERROR
         || protocol_state->transport.status == HIL_TRANSPORT_STATUS_INVALID_ARGUMENT )
    {
        HOST_INTERFACE_Error_Handler();
    }

    // =======------- RECEIVE ONE APPLICATION MESSAGE
    protocol_state->application.used_receive_byte_span_size = 0U;
    protocol_state->transport.status                        = HIL_TRANSPORT_Read_Application_Data(
        &protocol_state->transport.context, protocol_state->application.receive_byte_span,
        sizeof( protocol_state->application.receive_byte_span ),
        &protocol_state->application.used_receive_byte_span_size );

    if ( protocol_state->transport.status == HIL_TRANSPORT_STATUS_OK )
    {
        size_t required_decode_capacity = 0U;

        protocol_state->application.status = HIL_APPLICATION_Decode_Storage_Size(
            &protocol_state->application.context, protocol_state->application.receive_byte_span,
            protocol_state->application.used_receive_byte_span_size, &required_decode_capacity );

        if ( protocol_state->application.status == HIL_APPLICATION_STATUS_OK
             && required_decode_capacity <= sizeof( protocol_state->application.receive_data ) )
        {
            protocol_state->application.status = HIL_APPLICATION_Decode_Message(
                &protocol_state->application.context, protocol_state->application.receive_byte_span,
                protocol_state->application.used_receive_byte_span_size, incoming_message,
                protocol_state->application.receive_data,
                sizeof( protocol_state->application.receive_data ),
                &protocol_state->application.used_receive_data_size );

            if ( protocol_state->application.status == HIL_APPLICATION_STATUS_OK )
            {
                *incoming_message_available = true;
            }
        }

        if ( protocol_state->application.status == HIL_APPLICATION_STATUS_INTERNAL_ERROR
             || ( protocol_state->application.status == HIL_APPLICATION_STATUS_OK
                  && required_decode_capacity
                         > sizeof( protocol_state->application.receive_data ) ) )
        {
            HOST_INTERFACE_Error_Handler();
        }
    }
    else if ( protocol_state->transport.status != HIL_TRANSPORT_STATUS_NOT_READY )
    {
        HOST_INTERFACE_Error_Handler();
    }

    // =======------- SUBMIT ONE OUTGOING APPLICATION MESSAGE
    if ( outgoing_message != NULL )
    {
        protocol_state->application.used_send_byte_span_size = 0U;
        protocol_state->application.status =
            HIL_APPLICATION_Encode_Message( &protocol_state->application.context, outgoing_message,
                                            protocol_state->application.send_byte_span,
                                            sizeof( protocol_state->application.send_byte_span ),
                                            &protocol_state->application.used_send_byte_span_size );

        if ( protocol_state->application.status == HIL_APPLICATION_STATUS_OK )
        {
            protocol_state->transport.status = HIL_TRANSPORT_Submit_Application_Data(
                &protocol_state->transport.context, protocol_state->application.send_byte_span,
                protocol_state->application.used_send_byte_span_size );

            if ( protocol_state->transport.status == HIL_TRANSPORT_STATUS_OK )
            {
                *outgoing_message_accepted = true;
            }
            else if ( protocol_state->transport.status != HIL_TRANSPORT_STATUS_NOT_READY
                      && protocol_state->transport.status
                             != HIL_TRANSPORT_STATUS_CAPACITY_EXHAUSTED )
            {
                HOST_INTERFACE_Error_Handler();
            }
        }
        else if ( protocol_state->application.status == HIL_APPLICATION_STATUS_INTERNAL_ERROR
                  || protocol_state->application.status == HIL_APPLICATION_STATUS_BUFFER_TOO_SMALL )
        {
            HOST_INTERFACE_Error_Handler();
        }
    }

    // =======------- TRANSMIT TRANSPORT OUTPUT
    while ( true )
    {
        protocol_state->transport.used_output_buffer_size = 0U;
        protocol_state->transport.status                  = HIL_TRANSPORT_Peek_Output(
            &protocol_state->transport.context, protocol_state->transport.output_buffer,
            sizeof( protocol_state->transport.output_buffer ),
            &protocol_state->transport.used_output_buffer_size );

        if ( protocol_state->transport.status == HIL_TRANSPORT_STATUS_NOT_READY )
        {
            break;
        }
        if ( protocol_state->transport.status != HIL_TRANSPORT_STATUS_OK
             || protocol_state->transport.used_output_buffer_size > UINT16_MAX )
        {
            HOST_INTERFACE_Error_Handler();
        }

        if ( !HW_USB_Transmit( protocol_state->transport.output_buffer,
                               ( uint16_t )protocol_state->transport.used_output_buffer_size ) )
        {
            break;
        }

        if ( HW_USB_Get_Link_State() != HW_USB_LINK_STATE_CONNECTED )
        {
            HOST_INTERFACE_Protocol_Update_Link_State( protocol_state,
                                                       HIL_TRANSPORT_LINK_STATE_DISCONNECTED, now );
            break;
        }

        protocol_state->transport.status =
            HIL_TRANSPORT_Commit_Output( &protocol_state->transport.context, now );
        if ( protocol_state->transport.status != HIL_TRANSPORT_STATUS_OK )
        {
            HOST_INTERFACE_Error_Handler();
        }
    }
}

static void HOST_INTERFACE_Protocol_Init( HOST_INTERFACE_Protocol_State_T* const protocol_state )
{
    *protocol_state = ( HOST_INTERFACE_Protocol_State_T ){ 0 };

    // =======------- INITIALISE USB INTERFACE
    if ( !HW_USB_Init() )
    {
        HOST_INTERFACE_Error_Handler();
    }

    // =======------- INITIALISE APPLICATION LAYER
    protocol_state->application.status =
        HIL_APPLICATION_Default_Config( &protocol_state->application.config );
    if ( protocol_state->application.status != HIL_APPLICATION_STATUS_OK )
    {
        HOST_INTERFACE_Error_Handler();
    }

    protocol_state->application.status = HIL_APPLICATION_Init(
        &protocol_state->application.context, &protocol_state->application.config );
    if ( protocol_state->application.status != HIL_APPLICATION_STATUS_OK )
    {
        HOST_INTERFACE_Error_Handler();
    }

    // =======------- INITIALISE TRANSPORT LAYER
    HIL_TRANSPORT_Default_Config( &protocol_state->transport.config );
    protocol_state->transport.config.retransmit_timeout_ms =
        HOST_INTERFACE_TRANSPORT_RETRANSMIT_TIMEOUT_MS;
    protocol_state->transport.config.max_retries = HOST_INTERFACE_TRANSPORT_MAX_RETRIES;
    protocol_state->transport.role               = HIL_TRANSPORT_ROLE_RIG;

    protocol_state->transport.status = HIL_TRANSPORT_Required_Storage_Size(
        &protocol_state->transport.config, &protocol_state->transport.required_workspace_size );
    if ( protocol_state->transport.status != HIL_TRANSPORT_STATUS_OK
         || protocol_state->transport.required_workspace_size == 0U
         || protocol_state->transport.required_workspace_size
                > sizeof( protocol_state->transport.workspace ) )
    {
        HOST_INTERFACE_Error_Handler();
    }

    protocol_state->transport.storage.workspace = protocol_state->transport.workspace;
    protocol_state->transport.storage.workspace_size =
        sizeof( protocol_state->transport.workspace );

    protocol_state->transport.status =
        HIL_TRANSPORT_Init( &protocol_state->transport.context, protocol_state->transport.role,
                            &protocol_state->transport.config, &protocol_state->transport.storage );
    if ( protocol_state->transport.status != HIL_TRANSPORT_STATUS_OK )
    {
        HOST_INTERFACE_Error_Handler();
    }

    protocol_state->initial_ticks = xTaskGetTickCount();
}

/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */

/**
 * @brief Host Interface Task
 *
 * The FreeRTOS task that runs host transport and future Flash Manager upload
 * and result-transfer workflows.
 */
void HOST_INTERFACE_Task( void* task_parameters )
{
    static HOST_INTERFACE_Protocol_State_T protocol_state           = { 0 };
    static HIL_Application_Message_T       outgoing_message         = { 0 };
    static HIL_Application_Message_T       incoming_message         = { 0 };
    bool                                   outgoing_message_pending = false;

    ( void )task_parameters;
    HOST_INTERFACE_Protocol_Init( &protocol_state );

    while ( true )
    {
        bool outgoing_message_accepted  = false;
        bool incoming_message_available = false;

        HOST_INTERFACE_Protocol_Process(
            &protocol_state, outgoing_message_pending ? &outgoing_message : NULL,
            &outgoing_message_accepted, &incoming_message, &incoming_message_available );

        if ( outgoing_message_accepted )
        {
            outgoing_message         = ( HIL_Application_Message_T ){ 0 };
            outgoing_message_pending = false;
        }

        if ( incoming_message_available )
        {
            // Application handlers consume incoming_message and may create the next
            // outgoing_message.
        }

        vTaskDelayUntil( &protocol_state.initial_ticks, pdMS_TO_TICKS( HOST_INTERFACE_PERIOD_MS ) );
    }
}
