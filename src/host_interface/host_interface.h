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

/**-----------------------------------------------------------------------------
 *  Public Defines / Macros
 *------------------------------------------------------------------------------
 */

#define HOST_INTERFACE_TASK_MEMORY 256
#define HOST_INTERFACE_TASK_PRIORITY 3

/**-----------------------------------------------------------------------------
 *  Public Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

typedef struct
{
    bool     is_initialized;
    bool     usb_connected;
    bool     can_consume_incoming;
    bool     outgoing_message_pending;
    bool     is_overflowing;
    uint32_t expected_tick_count;
    uint32_t carry_on_notifications;
} HostInterfaceStatus_T;

extern TaskHandle_t host_interface_task_handle;

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

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
