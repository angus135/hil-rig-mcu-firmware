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
#include "rtos_config.h"
#include "run_state_manager.h"
#include "hil_rig_protocol/transport/transport_types.h"

/**-----------------------------------------------------------------------------
 *  Public Defines / Macros
 *------------------------------------------------------------------------------
 */

#define HOST_INTERFACE_TASK_MEMORY 256
#define HOST_INTERFACE_TASK_PRIORITY 3

#define HOST_INTERFACE_NOTIFY_PACKAGE_RECEIVE ( 1UL << 0U )
#define HOST_INTERFACE_NOTIFY_CONFIGURATION ( 1UL << 1U )
#define HOST_INTERFACE_NOTIFY_ARMED ( 1UL << 2U )
#define HOST_INTERFACE_NOTIFY_EXECUTION_COMPLETE ( 1UL << 3U )
#define HOST_INTERFACE_NOTIFY_RESULT_TRANSFER ( 1UL << 4U )
#define HOST_INTERFACE_NOTIFY_RESULT_TRANSFER_COMPLETE ( 1UL << 5U )
#define HOST_INTERFACE_NOTIFY_FAULT ( 1UL << 6U )
#define HOST_INTERFACE_NOTIFY_RESET ( 1UL << 7U )

/**-----------------------------------------------------------------------------
 *  Public Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

typedef struct
{
    bool                  is_initialized;
    bool                  usb_connected;
    bool                  can_consume_incoming;
    bool                  outgoing_message_pending;
    bool                  is_overflowing;
    bool                  is_faulted;
    RunStateFaultReason_T last_fault_reason;
    uint32_t              response_blocked_count;
    uint8_t               last_blocked_message_type;
    uint32_t              expected_tick_count;
    uint32_t              carry_on_notifications;

    /* Traffic & Activity Counters */
    uint32_t rx_message_count;
    uint32_t tx_message_count;
    uint8_t  last_rx_message_type;
    uint32_t last_rx_tick;

    /* Instruction Validation & Rejection Diagnostics */
    uint32_t rejected_instruction_count;
    uint32_t last_rejected_tick;
    uint32_t last_rejected_reason;

    /* Transport Layer Live Status */
    HIL_Transport_Session_State_T transport_session_state;
    bool                          transport_reliable_pending;
    HIL_Transport_Failure_T       transport_last_failure;
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
