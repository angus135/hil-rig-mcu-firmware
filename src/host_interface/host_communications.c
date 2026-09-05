/******************************************************************************
 *  File:       host_communications.c
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
#include "tests/host_communications_mocks.h"
#else
#include "main.h"
#endif
#include "rtos_config.h"
#include "hw_usb.h"
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

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

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


    
    // =======------- INITIALISE USB INTERFACE 
    // Initialize the Application objects
    size_t usb_recieve_size = 1;
    uint8_t usb_receive_buffer[usb_recieve_size];
    // Initialise the USB wrapper
    if ( !HW_USB_Init() )
    {
        Error_Handler();
    }



    // =======------- INITIALISE APPLICATION LAYER   
    // Zero-initialize the Application objects
    HIL_Application_Context_T a_context {};
    HIL_Application_Message_T a_send_message {};
    HIL_Application_Message_T a_receive_message {};
    HIL_Application_Config_T a_config {};
    size_t a_byte_span_capacity = 1U;
    uint8_t a_send_byte_span[a_byte_span_capacity];
    uint8_t a_receive_byte_span[a_byte_span_capacity];
    size_t a_used_byte_span_capacity = 0;
    size_t a_data_capacity = 1U;
    uint8_t a_send_data[a_data_capacity];
    uint8_t a_receive_data[a_data_capacity];
    size_t a_used_data_capacity = 0;
    HIL_Application_Status_T a_status;

    // Apply default configurations
    a_status = HIL_Application_Default_Config(&a_config);
    if (a_status != HIL_TRANSPORT_STATUS_OK) {
        Error_Handler();
    }

    // Initialise the trasport layer 
    a_status = HIL_APPLICATION_Init(&a_context, &a_config);
    if (t_status != HIL_TRANSPORT_STATUS_OK) {
        Error_Handler();
    }




    // =======------- INITIALISE TRANSPORT LAYER   
    // Zero-initialize the Transport objects
    HIL_Transport_Context_T t_context {};
    HIL_Transport_Config_T t_config {};
    HIL_Transport_Role_T t_role {};
    HIL_Transport_Storage_T t_storage {};
    size_t workspace_size = 1U;
    uint8_t workspace[workspace_size];
    size_t t_req_size = 0;
    size_t t_output_buffer_size = 1U;
    uint8_t t_output_buffer [t_output_buffer_size];
    size_t t_used_output_buffer_size = 0;
    HIL_Transport_Status_T t_status;
    HIL_Transport_Event_T t_event;

    // Call HIL_TRANSPORT_Default_Config() to populate the HIL_Transport_Config_T object
    t_status = HIL_TRANSPORT_Default_Config(&t_config);
    if (t_status != HIL_TRANSPORT_STATUS_OK) {
        Error_Handler();
    }

    // TODO Overwrite HIL_Transport_Context_T with desired values.
    // t_config.max_application_message_size = 1U;
    // t_config.max_encoded_frame_size = 1U;
    t_config.session_seed = (uint64_t)123;
    t_config.initial_reliable_sequence = 123U;
    // t_config.connection_timeout_ms = 123U;
    // t_config.retransmit_timeout_ms = 123U;
    // t_config.max_retries = 123U;

    // Validate /determine HIL_TRANSPORT_Required_Storage_Size
    t_status = HIL_TRANSPORT_Required_Storage_Size(&t_config, &t_req_size);
    if (t_req_size == 0 || t_status != HIL_TRANSPORT_STATUS_OK) {
        Error_Handler();
    }

    // Allocate/retain zeroed context and aligned workspace
    t_storage.workspace = &workspace;
    t_storage.workspace_size = workspace_size;

    // Initialise the trasport layer 
    t_status = HIL_TRANSPORT_Init(&t_context, t_role, &t_config, &t_storage);
    if (t_status != HIL_TRANSPORT_STATUS_OK) {
        Error_Handler();
    }



    // ======------ Host Loop
    TickType_t initial_ticks = xTaskGetTickCount();
    while ( true )
    {
        // =======------- SERVICE USB
        HW_USB_Monitor_Process();

        // Check if the external link has changed
        uint8_t link_changed = 0; // 0 means CONNECTED, 1 means DISCONNECTED
        if (link_changed == 1) {
            HIL_TRANSPORT_Notify_Link_State()
        }


        
        // =======------- SERVICE TRANSPORT
        HIL_TRANSPORT_Process()



        // =======------- READ USB BYTES
        while (HW_USB_Get_Receive_Stream_Used_Bytes() > 0) {

            uint32_t bytes_read =
                HW_USB_Receive(
                    usb_receive_buffer,
                    usb_recieve_size
                )

            if (bytes_read == 0U) {
                break;
            }

            uint32_t offset = 0U;
            size_t bytes_consumed = 0;

            while (offset < bytes_read){
                t_status = HIL_TRANSPORT_Receive_Bytes(
                    &t_context,
                    &(usb_receive_buffer[offset]),
                    bytes_read - offset,
                    &bytes_consumed
                );
                if (t_status != HIL_TRANSPORT_STATUS_OK || bytes_consumed == 0) {
                    Error_Handler();
                    break;
                }

                offset += bytes_consumed;
            }



            // =======------- PROCESS RECIEVED BYTES
            a_status = HIL_TRANSPORT_Read_Application_Data(
                &t_context,
                &a_recieve_byte_span,
                a_byte_span_capacity,
                &a_used_capacity
            )

            if (a_status == HIL_APPLICATION_STATUS_OK) {
                // =======------- CHECK DECODED DATA SIZE
                size_t required_decode_capacity = 0;
                a_status = HIL_APPLICATION_Decode_Storage_Size( a_context, &a_recieve_byte_span, a_used_capacity, &required_decode_capacity)
                if (a_status != HIL_APPLICATION_STATUS_OK || required_decode_capacity > a_data_capacity) {
                    Error_Handler();
                    break;
                }
                // =======------- DECODE APPLICATION MESSAGE
                a_status = HIL_APPLICATION_Decode_Message( &a_context,
                    a_receive_byte_span,
                    a_used_byte_span_capacity,
                    &a_receive_message,
                    a_receive_data,
                    a_data_capacity,
                    &a_used_data_capacity );

                if (a_status != HIL_APPLICATION_STATUS_OK) {
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
            } else {
                Error_Handler();
                break;
            }


            // =======------- HANDLE TRANSPORT EVENTS
            t_status = HIL_TRANSPORT_Read_Event(
                &t_context,
                &t_event
            );
            if (t_status != HIL_TRANSPORT_STATUS_OK) {
                Error_Handler();
                break;
            }

            // Handle Event

        }
        // =======------- TRANSMIT TRANSPORT OUTPUT

        // if output_pending
        uint8_t output_pending = 0; // 0 means pending, 1 means none
        if (output_pending == 0) {

            //     determine required output size

            t_status = HIL_TRANSPORT_Peek_Output(
                &t_context,
                &t_output_buffer,
                t_output_buffer_size,
                &t_used_output_buffer_size
            )
            if (t_status == HIL_TRANSPORT_STATUS_OK) {
                if (HW_USB_Transmit( &t_output_buffer, t_used_output_buffer_size ) == true) {
                    HIL_TRANSPORT_Commit_Output( &transport_context, now);
                    output_pending = 1;
                }
            } else {
                Error_Handler();
                break;
            }
        }


            
        // =======------- MISC

        // delay / block / wait for notification

        vTaskDelayUntil( &initial_ticks, pdMS_TO_TICKS( HOST_INTERFACE_PERIOD ) );
    }
}
