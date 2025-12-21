/******************************************************************************
 *  File:       rtos_config
 *  Author:     Angus Corr
 *  Created:    6-Dec-2025
 *
 *  Description:
 *      Public interface for accessing FreeRTOS
 *
 *  Notes:
 *      None
 ******************************************************************************/

#ifndef RTOS_CONFIG_H
#define RTOS_CONFIG_H

#ifdef __cplusplus
extern "C"
{
#endif

/**-----------------------------------------------------------------------------
 *  Common Includes
 *------------------------------------------------------------------------------
 */

#include <stddef.h>

#ifndef TEST_BUILD

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "timers.h"
#include "queue.h"
#include "stream_buffer.h"

/**-----------------------------------------------------------------------------
 *  Public Defines / Macros
 *------------------------------------------------------------------------------
 */

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
// NOLINTBEGIN
/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */
#include <stdint.h>
/**-----------------------------------------------------------------------------
 *  Public Defines / Macros
 *------------------------------------------------------------------------------
 */
typedef uint32_t TickType_t;
#define portMAX_DELAY (TickType_t)0xffffffffUL

#define pdMS_TO_TICKS(x) (x)

/**-----------------------------------------------------------------------------
 *  Public Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */
typedef void (*TaskFunction_t)(void*);
typedef int32_t BaseType_t;
#define pdFALSE ((BaseType_t)0)
#define pdTRUE ((BaseType_t)1)
#define pdPASS (pdTRUE)
#define pdFAIL (pdFALSE)
typedef int32_t UBaseType_t;
typedef void*   TaskHandle_t;
typedef void*   QueueHandle_t;
#define configSTACK_DEPTH_TYPE uint16_t
/* Actions that can be performed when vTaskNotify() is called. */
typedef enum
{
    eNoAction = 0,            /* Notify the task without updating its notify value. */
    eSetBits,                 /* Set bits in the task's notification value. */
    eIncrement,               /* Increment the task's notification value. */
    eSetValueWithOverwrite,   /* Set the task's notification value to a specific value even if the
                                 previous value has not yet been read by the task. */
    eSetValueWithoutOverwrite /* Set the task's notification value if the previous value has been
                                 read by the task. */
} eNotifyAction;
/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

// task.c functions

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
 * @brief stub implementing FreeRTOS vTaskDelay
 */
void vTaskDelay(const TickType_t xTicksToDelay);

/**
 * @brief stub implementing FreeRTOS xTaskGetTickCount
 */
volatile TickType_t xTaskGetTickCount(void);

/**
 * @brief stub implementing FreeRTOS xTaskNotifyWait
 */
BaseType_t xTaskNotifyWait(uint32_t ulBitsToClearOnEntry, uint32_t ulBitsToClearOnExit,
                           uint32_t* pulNotificationValue, TickType_t xTicksToWait);
/**
 * @brief stub implementing FreeRTOS xTaskNotify
 */
BaseType_t xTaskNotify(TaskHandle_t xTaskToNotify, uint32_t ulValue, eNotifyAction eAction);
/**
 * @brief stub implementing FreeRTOS xTaskNotifyFromISR
 */
BaseType_t xTaskNotifyFromISR(TaskHandle_t xTaskToNotify, uint32_t ulValue, eNotifyAction eAction,
                              BaseType_t* pxHigherPriorityTaskWoken);

// queue.c functions

/**
 * @brief stub implementing FreeRTOS xQueueCreate
 */
QueueHandle_t xQueueCreate(UBaseType_t uxQueueLength, UBaseType_t uxItemSize);

/**
 * @brief stub implementing FreeRTOS xQueueSend
 */
BaseType_t xQueueSend(QueueHandle_t xQueue, const void* pvItemToQueue, TickType_t xTicksToWait);

/**
 * @brief stub implementing FreeRTOS xQueueSendFromISR
 */
BaseType_t xQueueSendFromISR(QueueHandle_t xQueue, const void* pvItemToQueue,
                             BaseType_t* pxHigherPriorityTaskWoken);

/**
 * @brief stub implementing FreeRTOS xQueueReceive
 */
BaseType_t xQueueReceive(QueueHandle_t xQueue, void* pvBuffer, TickType_t xTicksToWait);

/**
 * @brief stub implementing FreeRTOS xQueueReceiveFromISR
 */
BaseType_t xQueueReceiveFromISR(QueueHandle_t xQueue, void* pvBuffer,
                                BaseType_t* pxHigherPriorityTaskWoken);

/**
 * @brief stub implementing FreeRTOS xQueuePeek
 */
BaseType_t xQueuePeek(QueueHandle_t xQueue, void* pvBuffer, TickType_t xTicksToWait);

/**
 * @brief stub implementing FreeRTOS xQueuePeekFromISR
 */
BaseType_t xQueuePeekFromISR(QueueHandle_t xQueue, void* pvBuffer);

// semphr.c functions

// NOLINTEND

#endif

#ifdef __cplusplus
}
#endif

#endif /* RTOS_CONFIG_H */
