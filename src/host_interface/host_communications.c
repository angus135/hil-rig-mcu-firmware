/******************************************************************************
 *  File:       host_communications.c
 *  Author:     Tim Vogelsang
 *  Created:    6-Sep-2026
 *
 *  Description:
 *
 *  Notes:
 *     None
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
 * The FreeRTOS task that runs all the host interface related logic
 */
void HOST_INTERFACE_Task( void* task_parameters )
{
    ( void )task_parameters;

    // =======------- INITIALISE USB INTERFACE 
    if ( !HW_USB_Init() )
    {
        Error_Handler();
    }

    // =======------- INITIALISE  TRANSPORT LAYER   
    // Zero-initialize the Transport context object (HIL_Transport_Context_T)

    // Call HIL_TRANSPORT_Default_Config() to populate the HIL_Transport_Context_T object

    // Overwrite HIL_Transport_Context_T with desired values.
        // e.g. configure:
        //     transport role
        //     max application message size
        //     max encoded frame size
        //     session seed
        //     initial sequence
        //     retransmission timeout
        //     max retries

    // Validate /determine HIL_TRANSPORT_Required_Storage_Size

    // Allocate/retain zeroed context and aligned workspace

    // Initialise the trasport layer with HIL_TRANSPORT_Init()

    // =======------- START USB INTERFACE 
    // initialise/start STM32 USB CDC

    // inform transport of link state using HIL_TRANSPORT_Notify_Link_State

    TickType_t initial_ticks = xTaskGetTickCount();
    while ( true )
    {
        // =======------- SERVICE USB
        // service USB using HW_USB_Monitor_Process()

        // Check if the external link has changed
        uint8_t link_changed = 0; // 0 means CONNECTED, 1 means DISCONNECTED
        if (link_changed == 1) {
            // Call HIL_TRANSPORT_Notify_Link_State 
        }


        
        // =======------- SERVICE TRANSPORT

        // service transport using HIL_TRANSPORT_Process()



        // =======------- READ USB BYTES

        // while HW_USB_Get_Receive_Stream_Used_Bytes() > 0

        //     bytes_read =
        //         HW_USB_Receive(
        //             usb_receive_buffer,
        //             sizeof(usb_receive_buffer)
        //         )

        //     if bytes_read == 0
        //         break

        //     offset = 0

        //     while offset < bytes_read

        //         HIL_TRANSPORT_Receive_Bytes(
        //             &transport_context,
        //             &usb_receive_buffer[offset],
        //             bytes_read - offset,
        //             &bytes_consumed
        //         )

        //         offset += bytes_consumed

        //         if Transport returned
        //            CAPACITY_EXHAUSTED

        //             break/retry later



        // =======------- PROCESS RECIEVED BYTES

        // while application_message_pending

        //     determine message size

        //     HIL_TRANSPORT_Read_Application_Data(
        //         &transport_context,
        //         application_receive_buffer,
        //         sizeof(application_receive_buffer),
        //         &application_message_size
        //     )

        //     if successful

        //         ------------------------------------------------
        //         Application Decode
        //         ------------------------------------------------

        //         Application_Decode(
        //             application_receive_buffer,
        //             application_message_size,
        //             &application_message
        //         )

        //         if decode failed

        //             handle Application decode error

        //             continue

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



        // =======------- HANDLE TRANSPORT EVENTS

        // while event_pending

        //     HIL_TRANSPORT_Read_Event(
        //         &transport_context,
        //         &event
        //     )

        //     handle event



        // =======------- TRANSMIT TRANSPORT OUTPUT

        // if output_pending

        //     determine required output size

        //     HIL_TRANSPORT_Peek_Output(
        //         &transport_context,
        //         transport_output_buffer,
        //         sizeof(transport_output_buffer),
        //         &transport_output_size
        //     )

        //     if successful

        //         if HW_USB_Transmit(
        //             transport_output_buffer,
        //             transport_output_size
        //         )

        //             HIL_TRANSPORT_Commit_Output(
        //                 &transport_context,
        //                 now
        //             )

        //         else

        //             do not commit
        //             retry later


            
        // =======------- MISC

        // delay / block / wait for notification

        HW_USB_Transmit( const uint8_t* data, uint16_t size_bytes );

        HW_USB_Monitor_Process();

        vTaskDelayUntil( &initial_ticks, pdMS_TO_TICKS( HOST_INTERFACE_PERIOD ) );
    }
}
