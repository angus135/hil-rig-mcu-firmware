/******************************************************************************
 *  File:       rtos.h
 *  Author:     Angus Corr
 *  Created:    6-Dec-2025
 *
 *  Description:
 *      Public interface for accessing FreeRTOS
 *
 *  Notes:
 *      None
 ******************************************************************************/

#ifndef RTOS_H
#define RTOS_H

#ifdef __cplusplus
extern "C"
{
#endif

#ifndef TEST_BUILD
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "timers.h"
#include "queue.h"
#include "stream_buffer.h"
#else
/*------------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */
#include <stdint.h>
/*------------------------------------------------------------------------------
 *  Public Defines / Macros
 *------------------------------------------------------------------------------
 */
typedef uint32_t TickType_t;
#define portMAX_DELAY (TickType_t)0xffffffffUL

/*------------------------------------------------------------------------------
 *  Public Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

/*------------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

/**
 * @brief stub implementing FreeRTOS vTaskDelayUntil
 */
void vTaskDelayUntil(TickType_t* pxPreviousWakeTime, const TickType_t xTimeIncrement);

#endif
#ifdef __cplusplus
}
#endif

#endif /* RTOS_H */
