/******************************************************************************
 *  File:       host_interface.h
 *  Author:     Callum Rafferty
 *  Created:    25-Mar-2026
 *
 *  Description:
 *      Public interface for the Host Interface RTOS task.
 *
 *  Notes:
 *      Instruction upload integration is task-context only. The Host Interface
 *      translates and validates protocol data into the canonical Flash Manager
 *      instruction stream, then uses the asynchronous upload lifecycle
 *      documented in flash_manager.h. It never calls external_flash directly.
 ******************************************************************************/

#ifndef HOST_INTERFACE_H
#define HOST_INTERFACE_H

#ifdef __cplusplus
extern "C"
{
#endif

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "rtos_config.h"
#include "run_state_manager.h"
#include "hw_usb.h"
#include "hil_rig_protocol/application/application_status.h"
#include "hil_rig_protocol/transport/transport_types.h"
#include "result_message_producer.h"
#include "variable_result_message_producer.h"

/**-----------------------------------------------------------------------------
 *  Public Defines / Macros
 *------------------------------------------------------------------------------
 */

#define HOST_INTERFACE_TASK_MEMORY 256
#define HOST_INTERFACE_TASK_PRIORITY 3

#define HOST_INTERFACE_NOTIFY_PACKAGE_RECEIVE ( ( uint32_t )1U << 0U )
#define HOST_INTERFACE_NOTIFY_CONFIGURATION ( ( uint32_t )1U << 1U )
#define HOST_INTERFACE_NOTIFY_ARMED ( ( uint32_t )1U << 2U )
#define HOST_INTERFACE_NOTIFY_EXECUTION_COMPLETE ( ( uint32_t )1U << 3U )
#define HOST_INTERFACE_NOTIFY_RESULT_TRANSFER ( ( uint32_t )1U << 4U )
#define HOST_INTERFACE_NOTIFY_RESULT_TRANSFER_COMPLETE ( ( uint32_t )1U << 5U )
#define HOST_INTERFACE_NOTIFY_FAULT ( ( uint32_t )1U << 6U )
#define HOST_INTERFACE_NOTIFY_RESET ( ( uint32_t )1U << 7U )

/**-----------------------------------------------------------------------------
 *  Public Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

typedef struct
{
    bool                      is_initialized;
    bool                      usb_connected;
    HW_USB_Connection_State_T usb_connection_state;
    uint32_t                  usb_rx_stream_used_bytes;
    bool                      can_consume_incoming;
    bool                      outgoing_message_pending;
    uint8_t                   outgoing_pending_message_type;
    uint32_t                  outgoing_pending_tick;
    bool                      is_overflowing;
    uint8_t                   overflow_message_type;
    uint32_t                  overflow_message_tick;
    uint32_t                  overflow_duration_ms;
    bool                      is_faulted;
    RunStateFaultReason_T     last_fault_reason;
    uint32_t                  response_blocked_count;
    uint8_t                   last_blocked_message_type;
    uint32_t                  last_blocked_message_tick;
    uint32_t                  expected_tick_count;
    uint32_t                  carry_on_notifications;

    /* Traffic & Activity Counters */
    uint32_t rx_message_count;
    uint32_t tx_message_count;
    uint8_t  last_rx_message_type;
    uint32_t last_rx_tick;
    uint8_t  last_tx_message_type;
    uint32_t last_tx_tick;

    /* Codec & Protocol Live Diagnostics */
    HIL_Application_Status_T last_encode_status;
    uint8_t                  last_encode_failed_type;
    size_t                   last_encode_size;
    HIL_Application_Status_T last_decode_status;

    /* Instruction Validation & Rejection Diagnostics */
    uint32_t rejected_instruction_count;
    uint32_t last_rejected_tick;
    uint32_t last_rejected_reason;

    /* Transport Layer Live Status */
    HIL_Transport_Session_State_T transport_session_state;
    bool                          transport_reliable_pending;
    HIL_Transport_Failure_T       transport_last_failure;
    HIL_Transport_Status_T        transport_last_submit_status;

    /* Result Stream Producer Diagnostics */
    Result_Message_Producer_Status_T    result_producer_last_status;
    VariableResultProducerDiagnostics_T var_producer_diags;

    /* Instruction Receive Timing & Statistics */
    bool     instruction_phase_active;
    uint32_t instruction_rx_count;
    uint32_t instruction_start_tick;
    uint32_t instruction_end_tick;
    uint32_t instruction_duration_ms;
    uint32_t instruction_rate_msgs_per_sec;

    /* Result Transfer Timing & Statistics */
    bool     result_phase_active;
    uint32_t result_tx_count;
    uint32_t result_start_tick;
    uint32_t result_end_tick;
    uint32_t result_duration_ms;
    uint32_t result_rate_msgs_per_sec;

    /* Dynamic Scheduling Status */
    uint32_t effective_period_ms;
} HostInterfaceStatus_T;

extern TaskHandle_t host_interface_task_handle;

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

/**
 * @brief Resets Host Interface internal state (clears pending responses and fault latch).
 */
void HOST_INTERFACE_Reset( void );

/**
 * @brief Notify the Host interface to send some message
 *
 */
bool HOST_INTERFACE_Notify( uint32_t notification );

/**
 * @brief Queries the live runtime status of the Host Interface task.
 *
 * @param[out] status Receives a snapshot of the current status.
 */
void HOST_INTERFACE_GetStatus( HostInterfaceStatus_T* status );

/**
 * @brief Host Interface Task
 *
 * The FreeRTOS task that runs host transport, canonical instruction upload,
 * and future result-transfer processing.
 */
void HOST_INTERFACE_Task( void* task_parameters );

#ifdef __cplusplus
}
#endif

#endif /* HOST_INTERFACE_H */
