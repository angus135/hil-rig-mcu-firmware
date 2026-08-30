/******************************************************************************
 *  File:       host_transport.h
 *  Author:     OpenAI
 *  Created:    30-Aug-2026
 *
 *  Description:
 *      Firmware integration boundary between USB CDC and the shared Transport.
 ******************************************************************************/

#ifndef HOST_TRANSPORT_H
#define HOST_TRANSPORT_H

#ifdef __cplusplus
extern "C"
{
#endif

#include "hil_rig_protocol/transport/transport_types.h"

#include <stdbool.h>
#include <stdint.h>

#define HOST_TRANSPORT_STATUS_COUNT ( ( uint32_t )HIL_TRANSPORT_STATUS_INTERNAL_ERROR + 1U )
#define HOST_TRANSPORT_EVENT_TYPE_COUNT ( ( uint32_t )HIL_TRANSPORT_EVENT_LINK_STATE_CHANGED + 1U )
#define HOST_TRANSPORT_RECENT_EVENT_DIAGNOSTIC_COUNT 8U

typedef struct
{
    uint32_t type;
    uint32_t status;
    uint32_t failure;
    uint32_t required_capacity;
} HOST_TRANSPORT_Event_Diagnostic_T;

typedef struct
{
    uint32_t initialization_attempted;
    uint32_t initialization_successful;
    uint32_t usb_initialization_successful;
    uint32_t initialization_status;
    uint32_t required_workspace_bytes;
    uint32_t available_workspace_bytes;
    uint32_t workspace_alignment_bytes;

    uint32_t link_state;
    uint32_t link_generation;
    uint32_t current_transport_status;
    uint32_t transport_session_state;
    uint32_t transport_last_failure;

    uint32_t task_service_count;
    uint32_t current_service_gap_ms;
    uint32_t maximum_service_gap_ms;
    uint32_t task_stack_high_water_words;

    uint32_t usb_rx_bytes;
    uint32_t usb_rx_dropped_bytes;
    uint32_t usb_tx_bytes;
    uint32_t usb_tx_ring_high_water_bytes;
    uint32_t usb_tx_busy_retries;
    uint32_t maximum_consecutive_usb_busy_iterations;

    uint32_t pending_rx_length;
    uint32_t pending_rx_high_water;
    uint32_t transport_rx_bytes_offered;
    uint32_t transport_rx_bytes_consumed;
    uint32_t transport_rx_partial_consumption_calls;
    uint32_t transport_rx_zero_progress_calls;
    uint32_t transport_rx_zero_byte_calls;
    uint32_t prevented_rx_overflows;
    uint32_t receive_status_counts[HOST_TRANSPORT_STATUS_COUNT];

    uint32_t output_items_peeked;
    uint32_t usb_output_acceptances;
    uint32_t output_commits;
    uint32_t output_commit_failures;

    uint32_t application_requests_received;
    uint32_t responses_submitted;
    uint32_t response_submit_retries;
    uint32_t response_submit_failures;
    uint32_t invalid_harness_messages;

    uint32_t                          transport_event_count;
    uint32_t                          transport_event_counts[HOST_TRANSPORT_EVENT_TYPE_COUNT];
    HOST_TRANSPORT_Event_Diagnostic_T most_recent_event;
    HOST_TRANSPORT_Event_Diagnostic_T recent_events[HOST_TRANSPORT_RECENT_EVENT_DIAGNOSTIC_COUNT];
    uint32_t                          recent_event_write_index;

    uint32_t operation_budget_exhaustions;
} HOST_TRANSPORT_Diagnostics_T;

/** Debugger-visible diagnostics. Counters saturate at UINT32_MAX. */
extern HOST_TRANSPORT_Diagnostics_T g_host_transport_diagnostics;

/** Initialize one statically backed RIG-role Transport context. */
bool HOST_TRANSPORT_Init( void );

/** Record whether the USB wrapper initialized successfully. */
void HOST_TRANSPORT_Set_USB_Initialization_Status( bool initialized );

/** Apply one observed USB configured/deconfigured transition. */
void HOST_TRANSPORT_Set_Link_State( bool connected, uint32_t now_ms );

/** Run one bounded service iteration from the sole owner task. */
void HOST_TRANSPORT_Service( uint32_t now_ms );

/** Record the owner task's FreeRTOS stack high-water value. */
void HOST_TRANSPORT_Set_Task_Stack_High_Water( uint32_t high_water_words );

/** @return true after Transport initialized successfully. */
bool HOST_TRANSPORT_Is_Initialized( void );

/** @return debugger-visible diagnostics snapshot storage. */
const HOST_TRANSPORT_Diagnostics_T* HOST_TRANSPORT_Get_Diagnostics( void );

#ifdef TEST_BUILD
/** Test-only reset for link-time unit tests. Not present in the MCU build. */
void HOST_TRANSPORT_Test_Reset( void );

/** Exercise the production initialization path with an intentionally smaller workspace. */
bool HOST_TRANSPORT_Test_Init_With_Workspace_Capacity( uint32_t workspace_capacity );

/** Test-only helper used to exercise response backpressure without a production seam. */
bool HOST_TRANSPORT_Test_Seed_Pending_Response( const uint8_t* response, uint32_t length );

/** Test-only observation of the one-slot response backpressure state. */
bool HOST_TRANSPORT_Test_Response_Is_Pending( void );

/** Test-only entry point used to verify unread-message backpressure. */
bool HOST_TRANSPORT_Test_Try_Read_Application_Message( void );

/** Copy caller-owned retained RX bytes for exact-suffix verification. */
uint32_t HOST_TRANSPORT_Test_Copy_Pending_RX( uint8_t* destination, uint32_t capacity );
#endif

#ifdef __cplusplus
}
#endif

#endif /* HOST_TRANSPORT_H */
