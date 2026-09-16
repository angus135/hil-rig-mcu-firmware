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
