/******************************************************************************
 *  File:       background.h
 *  Author:     Angus Corr
 *  Created:    20-Dec-2025
 *
 *  Description:
 *      Public interface for the Background module.
 *
 *  Notes:
 *      None
 ******************************************************************************/

#ifndef BACKGROUND_H
#define BACKGROUND_H

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
#define BACKGROUND_TASK_MEMORY 256
#define BACKGROUND_TASK_PRIORITY 3

/**-----------------------------------------------------------------------------
 *  Public Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

/**
 * @brief Console Task
 *
 * The FreeRTOS task that runs all the background related logic
 */
void BACKGROUND_Task(void* task_parameters);

#ifdef __cplusplus
}
#endif

#endif /* BACKGROUND_H */
