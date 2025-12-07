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
#include <stddef.h>

#ifndef TEST_BUILD
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "timers.h"
#include "queue.h"
#include "stream_buffer.h"

static inline BaseType_t CREATE_TASK(TaskFunction_t task_function, const char* task_name,
                              configSTACK_DEPTH_TYPE task_memory, UBaseType_t task_priority,
                              TaskHandle_t* task_handle)
{
    return xTaskCreate(task_function, // Task function (void (*)(void *))
                       task_name,     // Task name
                       task_memory,   // Stack depth in words
                       NULL,          // Task argument
                       task_priority, // Priority
                       task_handle);  // Task handle
}
#else
// NOLINTBEGIN(readability-identifier-naming)
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

#define pdMS_TO_TICKS(x) (x)

/*------------------------------------------------------------------------------
 *  Public Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */
typedef void (*TaskFunction_t)(void*);
typedef int32_t BaseType_t;
typedef int32_t UBaseType_t;
typedef void    TaskHandle_t;
#define configSTACK_DEPTH_TYPE uint16_t
/*------------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

BaseType_t xTaskCreate(TaskFunction_t pvTaskCode, const char* const pcName,
                       const configSTACK_DEPTH_TYPE uxStackDepth, void* pvParameters,
                       UBaseType_t uxPriority, TaskHandle_t* pxCreatedTask);

static inline BaseType_t CREATE_TASK(TaskFunction_t task_function, const char* task_name,
                              configSTACK_DEPTH_TYPE task_memory, UBaseType_t task_priority,
                              TaskHandle_t* task_handle)
{
    return xTaskCreate(task_function, // Task function (void (*)(void *))
                       task_name,     // Task name
                       task_memory,   // Stack depth in words
                       NULL,          // Task argument
                       task_priority, // Priority
                       task_handle);  // Task handle
}

/**
 * @brief stub implementing FreeRTOS vTaskStartScheduler
 */
void vTaskStartScheduler(void);
/**
 * @brief stub implementing FreeRTOS vTaskDelayUntil
 */
void vTaskDelayUntil(TickType_t* pxPreviousWakeTime, const TickType_t xTimeIncrement);
/**
 * @brief stub implementing FreeRTOS xTaskGetTickCount
 */
volatile TickType_t xTaskGetTickCount(void);
// NOLINTEND(readability-identifier-naming)
#endif
#ifdef __cplusplus
}
#endif

#endif /* RTOS_H */
