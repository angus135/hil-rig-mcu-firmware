/******************************************************************************
 *  File:       host_communications.c
 *  Author:     Angus Corr
 *  Created:    20-Dec-2025
 *
 *  Description:
 *      Owns periodic host-interface servicing. On the DEV-138 hardware-test
 *      branch this task is the sole owner of the shared Transport context.
 ******************************************************************************/

#ifdef TEST_BUILD
#include "tests/host_communications_mocks.h"
#else
#include "main.h"
#endif

#include "host_communications.h"
#include "host_transport.h"
#include "hw_usb.h"
#include "protocol_test_config.h"
#include "rtos_config.h"

#include <stdbool.h>
#include <stdint.h>

#define HOST_INTERFACE_STACK_SAMPLE_PERIOD_MS 1000U

TaskHandle_t* HostInterfaceTaskHandle = NULL;  // NOLINT(readability-identifier-naming)

static uint32_t HOST_INTERFACE_Get_Time_Ms( void )
{
    TickType_t ticks = xTaskGetTickCount();

#ifdef TEST_BUILD
    /* The host RTOS stub defines one tick per millisecond. */
    return ( uint32_t )ticks;
#else
    /* Unsigned conversion remains well-defined when the resulting uint32_t wraps. */
    return ( uint32_t )( ( ( uint64_t )ticks * 1000ULL ) / ( uint64_t )configTICK_RATE_HZ );
#endif
}

/**
 * @brief Host Interface Task
 *
 * The FreeRTOS task that runs all host-interface and Transport servicing. No
 * Transport API is called from USB callbacks or interrupt context.
 */
void HOST_INTERFACE_Task( void* task_parameters )
{
    bool       usb_initialized;
    bool       transport_initialized;
    TickType_t initial_ticks;
    uint32_t   last_stack_sample_ms = 0U;

    ( void )task_parameters;

    usb_initialized       = HW_USB_Init();
    transport_initialized = HOST_TRANSPORT_Init();
    HOST_TRANSPORT_Set_USB_Initialization_Status( usb_initialized );

    initial_ticks = xTaskGetTickCount();
    while ( true )
    {
        uint32_t now_ms = HOST_INTERFACE_Get_Time_Ms();

        if ( usb_initialized )
        {
            HW_USB_Monitor_Process();
        }

        if ( transport_initialized )
        {
            bool connected = usb_initialized && HW_USB_Is_Connected();
            HOST_TRANSPORT_Set_Link_State( connected, now_ms );
            HOST_TRANSPORT_Service( now_ms );
        }

#ifndef TEST_BUILD
        if ( ( uint32_t )( now_ms - last_stack_sample_ms )
             >= HOST_INTERFACE_STACK_SAMPLE_PERIOD_MS )
        {
            HOST_TRANSPORT_Set_Task_Stack_High_Water(
                ( uint32_t )uxTaskGetStackHighWaterMark( NULL ) );
            last_stack_sample_ms = now_ms;
        }
#else
        ( void )last_stack_sample_ms;
#endif

        vTaskDelayUntil( &initial_ticks, pdMS_TO_TICKS( HOST_TRANSPORT_SERVICE_PERIOD_MS ) );
    }
}
