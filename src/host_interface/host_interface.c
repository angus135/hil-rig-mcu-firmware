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
 *      current periodic task is only a transport skeleton; upload state,
 *      retry/backpressure handling, and canonical conversion are not yet wired.
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#ifdef TEST_BUILD
#include "tests/host_interface_mocks.h"
#else
#include "main.h"
#endif
#include "hil_rig_protocol/application/application.h"
#include "hil_rig_protocol/transport/transport.h"
#include "hw_usb.h"
#include "host_interface.h"
#include "rtos_config.h"

#include <stdint.h>
#include <stdbool.h>

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */
#define HOST_INTERFACE_PERIOD 1000  // 1Hz

/**-----------------------------------------------------------------------------
 *  Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

typedef struct
{
    uint32_t receive_size;
    uint8_t  receive_buffer[1U];
} HOST_INTERFACE_USB_State_T;

typedef struct
{
    HIL_Application_Context_T context;
    HIL_Application_Message_T send_message;
    HIL_Application_Message_T receive_message;
    HIL_Application_Config_T  config;
    size_t                    byte_span_capacity;
    uint8_t                   send_byte_span[1U];
    uint8_t                   receive_byte_span[1U];
    size_t                    used_byte_span_capacity;
    size_t                    data_capacity;
    uint8_t                   send_data[1U];
    uint8_t                   receive_data[1U];
    size_t                    used_data_capacity;
    HIL_Application_Status_T  status;
} HOST_INTERFACE_Application_State_T;

typedef struct
{
    HIL_Transport_Context_T context;
    HIL_Transport_Config_T  config;
    HIL_Transport_Role_T    role;
    HIL_Transport_Storage_T storage;
    size_t                  workspace_size;
    uint8_t                 workspace[1U];
    size_t                  req_size;
    size_t                  output_buffer_size;
    uint8_t                 output_buffer[1U];
    size_t                  used_output_buffer_size;
    HIL_Transport_Status_T  status;
    HIL_Transport_Event_T   event;
} HOST_INTERFACE_Transport_State_T;

typedef struct
{
    HOST_INTERFACE_USB_State_T         usb;
    HOST_INTERFACE_Application_State_T application;
    HOST_INTERFACE_Transport_State_T   transport;
    TickType_t                         initial_ticks;
    uint8_t                            link_changed;
    uint8_t                            output_pending;
} HOST_INTERFACE_Protocol_State_T;

/**-----------------------------------------------------------------------------
 *  Public (global) and Extern Variables
 *------------------------------------------------------------------------------
 */

TaskHandle_t* HostInterfaceTaskHandle = NULL;  // NOLINT(readability-identifier-naming)

/**-----------------------------------------------------------------------------
 *  Private (static) Variables
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Private (static) Function Prototypes
 *------------------------------------------------------------------------------
 */

static void HOST_INTERFACE_Protocol_Process( HOST_INTERFACE_Protocol_State_T* protocol_state );
static void HOST_INTERFACE_Protocol_Init( HOST_INTERFACE_Protocol_State_T* protocol_state );

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

static void HOST_INTERFACE_Protocol_Process( HOST_INTERFACE_Protocol_State_T* protocol_state )
{
    uint32_t now = ( uint32_t )xTaskGetTickCount();

    do
    {
        // =======------- SERVICE USB
        HW_USB_Monitor_Process();

        // Check if the external link has changed
        protocol_state->link_changed = 0;  // 0 means CONNECTED, 1 means DISCONNECTED
        if ( protocol_state->link_changed == 1 )
        {
            protocol_state->transport.status = HIL_TRANSPORT_Notify_Link_State(
                &protocol_state->transport.context, HIL_TRANSPORT_LINK_STATE_DISCONNECTED, now );
        }

        // =======------- SERVICE TRANSPORT
        protocol_state->transport.status = HIL_TRANSPORT_Process(
            &protocol_state->transport.context, now, HIL_TRANSPORT_OPERATING_MODE_NORMAL );

        // =======------- READ USB BYTES
        while ( HW_USB_Get_Receive_Stream_Used_Bytes() > 0 )
        {

            uint32_t bytes_read = HW_USB_Receive( protocol_state->usb.receive_buffer,
                                                  protocol_state->usb.receive_size );

            if ( bytes_read == 0U )
            {
                break;
            }

            size_t offset         = 0U;
            size_t bytes_consumed = 0;

            while ( offset < bytes_read )
            {
                protocol_state->transport.status =
                    HIL_TRANSPORT_Receive_Bytes( &protocol_state->transport.context,
                                                 &( protocol_state->usb.receive_buffer[offset] ),
                                                 bytes_read - offset, &bytes_consumed );
                if ( protocol_state->transport.status != HIL_TRANSPORT_STATUS_OK
                     || bytes_consumed == 0 )
                {
                    Error_Handler();
                    break;
                }

                offset += bytes_consumed;
            }

            // =======------- PROCESS RECEIVED BYTES
            protocol_state->transport.status = HIL_TRANSPORT_Read_Application_Data(
                &protocol_state->transport.context, protocol_state->application.receive_byte_span,
                protocol_state->application.byte_span_capacity,
                &protocol_state->application.used_byte_span_capacity );

            if ( protocol_state->transport.status == HIL_TRANSPORT_STATUS_OK )
            {
                // =======------- CHECK DECODED DATA SIZE
                size_t required_decode_capacity    = 0;
                protocol_state->application.status = HIL_APPLICATION_Decode_Storage_Size(
                    &protocol_state->application.context,
                    protocol_state->application.receive_byte_span,
                    protocol_state->application.used_byte_span_capacity,
                    &required_decode_capacity );
                if ( protocol_state->application.status != HIL_APPLICATION_STATUS_OK
                     || required_decode_capacity > protocol_state->application.data_capacity )
                {
                    Error_Handler();
                    break;
                }
                // =======------- DECODE APPLICATION MESSAGE
                protocol_state->application.status = HIL_APPLICATION_Decode_Message(
                    &protocol_state->application.context,
                    protocol_state->application.receive_byte_span,
                    protocol_state->application.used_byte_span_capacity,
                    &protocol_state->application.receive_message,
                    protocol_state->application.receive_data,
                    protocol_state->application.data_capacity,
                    &protocol_state->application.used_data_capacity );

                if ( protocol_state->application.status != HIL_APPLICATION_STATUS_OK )
                {
                    Error_Handler();
                    break;
                }

                //         ------------------------------------------------
                //         Application Processing
                //         ------------------------------------------------

                //         Application_Process(
                //             &application_message,
                //             &application_response
                //         )

                //         ------------------------------------------------
                //         Application Encode
                //         ------------------------------------------------

                //         if response_required

                //             Application_Encode(
                //                 &application_response,
                //                 application_transmit_buffer,
                //                 &application_transmit_size
                //             )

                //             ------------------------------------------------
                //             Submit to Transport
                //             ------------------------------------------------

                //             HIL_TRANSPORT_Submit_Application_Data(
                //                 &transport_context,
                //                 application_transmit_buffer,
                //                 application_transmit_size
                //             )
            }
            else
            {
                Error_Handler();
                break;
            }

            // =======------- HANDLE TRANSPORT EVENTS
            protocol_state->transport.status = HIL_TRANSPORT_Read_Event(
                &protocol_state->transport.context, &protocol_state->transport.event );
            if ( protocol_state->transport.status != HIL_TRANSPORT_STATUS_OK )
            {
                Error_Handler();
                break;
            }

            // Handle Event
        }
        // =======------- TRANSMIT TRANSPORT OUTPUT

        // if output_pending
        protocol_state->output_pending = 0;  // 0 means pending, 1 means none
        if ( protocol_state->output_pending == 0 )
        {

            //     determine required output size

            protocol_state->transport.status = HIL_TRANSPORT_Peek_Output(
                &protocol_state->transport.context, protocol_state->transport.output_buffer,
                protocol_state->transport.output_buffer_size,
                &protocol_state->transport.used_output_buffer_size );
            if ( protocol_state->transport.status == HIL_TRANSPORT_STATUS_OK )
            {
                if ( HW_USB_Transmit( protocol_state->transport.output_buffer,
                                      protocol_state->transport.used_output_buffer_size )
                     == true )
                {
                    HIL_TRANSPORT_Commit_Output( &protocol_state->transport.context, now );
                    protocol_state->output_pending = 1;
                }
            }
            else
            {
                Error_Handler();
                break;
            }
        }
    } while ( false );
}

static void HOST_INTERFACE_Protocol_Init( HOST_INTERFACE_Protocol_State_T* protocol_state )
{
    // =======------- INITIALISE USB INTERFACE
    // Initialize the Application objects
    protocol_state->usb.receive_size = 1U;
    // Initialise the USB wrapper
    if ( !HW_USB_Init() )
    {
        Error_Handler();
    }

    // =======------- INITIALISE APPLICATION LAYER
    // Zero-initialize the Application objects
    protocol_state->application.context                 = ( HIL_Application_Context_T ){ 0 };
    protocol_state->application.send_message            = ( HIL_Application_Message_T ){ 0 };
    protocol_state->application.receive_message         = ( HIL_Application_Message_T ){ 0 };
    protocol_state->application.config                  = ( HIL_Application_Config_T ){ 0 };
    protocol_state->application.byte_span_capacity      = 1U;
    protocol_state->application.used_byte_span_capacity = 0;
    protocol_state->application.data_capacity           = 1U;
    protocol_state->application.used_data_capacity      = 0;

    // Apply default configurations
    protocol_state->application.status =
        HIL_APPLICATION_Default_Config( &protocol_state->application.config );
    if ( protocol_state->application.status != HIL_APPLICATION_STATUS_OK )
    {
        Error_Handler();
    }

    // Initialise the transport layer
    protocol_state->application.status = HIL_APPLICATION_Init(
        &protocol_state->application.context, &protocol_state->application.config );
    if ( protocol_state->application.status != HIL_APPLICATION_STATUS_OK )
    {
        Error_Handler();
    }

    // =======------- INITIALISE TRANSPORT LAYER
    // Zero-initialize the Transport objects
    protocol_state->transport.context                 = ( HIL_Transport_Context_T ){ 0 };
    protocol_state->transport.config                  = ( HIL_Transport_Config_T ){ 0 };
    protocol_state->transport.role                    = ( HIL_Transport_Role_T ){ 0 };
    protocol_state->transport.storage                 = ( HIL_Transport_Storage_T ){ 0 };
    protocol_state->transport.workspace_size          = 1U;
    protocol_state->transport.req_size                = 0;
    protocol_state->transport.output_buffer_size      = 1U;
    protocol_state->transport.used_output_buffer_size = 0;

    // Call HIL_TRANSPORT_Default_Config() to populate the HIL_Transport_Config_T object
    HIL_TRANSPORT_Default_Config( &protocol_state->transport.config );

    // TODO Overwrite HIL_Transport_Context_T with desired values.
    // protocol_state->transport.config.max_application_message_size = 1U;
    // protocol_state->transport.config.max_encoded_frame_size = 1U;
    protocol_state->transport.config.session_seed              = ( uint64_t )123;
    protocol_state->transport.config.initial_reliable_sequence = 123U;
    // protocol_state->transport.config.connection_timeout_ms = 123U;
    // protocol_state->transport.config.retransmit_timeout_ms = 123U;
    // protocol_state->transport.config.max_retries = 123U;

    // Validate /determine HIL_TRANSPORT_Required_Storage_Size
    protocol_state->transport.status = HIL_TRANSPORT_Required_Storage_Size(
        &protocol_state->transport.config, &protocol_state->transport.req_size );
    if ( protocol_state->transport.req_size == 0
         || protocol_state->transport.status != HIL_TRANSPORT_STATUS_OK )
    {
        Error_Handler();
    }

    // Allocate/retain zeroed context and aligned workspace
    protocol_state->transport.storage.workspace      = protocol_state->transport.workspace;
    protocol_state->transport.storage.workspace_size = protocol_state->transport.workspace_size;

    // Initialise the transport layer
    protocol_state->transport.status =
        HIL_TRANSPORT_Init( &protocol_state->transport.context, protocol_state->transport.role,
                            &protocol_state->transport.config, &protocol_state->transport.storage );
    if ( protocol_state->transport.status != HIL_TRANSPORT_STATUS_OK )
    {
        Error_Handler();
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
    ( void )task_parameters;
    HOST_INTERFACE_Protocol_State_T protocol_state = { 0 };
    HOST_INTERFACE_Protocol_Init( &protocol_state );

    while ( true )
    {
        HOST_INTERFACE_Protocol_Process( &protocol_state );
        vTaskDelayUntil( &protocol_state.initial_ticks, pdMS_TO_TICKS( HOST_INTERFACE_PERIOD ) );
    }
}
