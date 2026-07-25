/******************************************************************************
 *  File:       host_communications.h
 *  Author:     Callum Rafferty
 *  Created:    25-Mar-2026
 *
 *  Description:
 *      Public interface for the Background module.
 *
 *  Notes:
 *      None
 ******************************************************************************/

#ifndef HOST_COMMUNICATIONS_H
#define HOST_COMMUNICATIONS_H

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

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

/**
 * @brief Host Interface Task
 *
 * The FreeRTOS task that runs all the host interface related logic
 */
void HOST_INTERFACE_Task( void* task_parameters );

#ifdef __cplusplus
}
#endif

#endif /* HOST_COMMUNICATIONS_H */
