/******************************************************************************
 *  File:       stub_task.c
 *  Author:     Angus Corr
 *  Created:    6-Dec-2025
 *
 *  Description:
 *      Stubs that correspond to task.c in FreeRTOS
 *
 *  Notes:
 *     None
 ******************************************************************************/
#ifdef TEST_BUILD
/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */
#include "rtos_config.h"
#include <stdint.h>
#include <stdbool.h>

// NOLINTBEGIN

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Public (global) and Extern Variables
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Private (static) Variables
 *------------------------------------------------------------------------------
 */
static TickType_t current_tick = 0;

static uint32_t s_xTaskNotifyWait_called;
static uint32_t s_xTaskNotify_called;
static uint32_t s_xTaskNotifyFromISR_called;

static uint32_t   s_xTaskNotifyWait_ulBitsToClearOnEntry;
static uint32_t   s_xTaskNotifyWait_ulBitsToClearOnExit;
static uint32_t*  s_xTaskNotifyWait_pulNotificationValue;
static TickType_t s_xTaskNotifyWait_xTicksToWait;

static TaskHandle_t  s_xTaskNotify_xTaskToNotify;
static uint32_t      s_xTaskNotify_ulValue;
static eNotifyAction s_xTaskNotify_eAction;

static TaskHandle_t  s_xTaskNotifyFromISR_xTaskToNotify;
static uint32_t      s_xTaskNotifyFromISR_ulValue;
static eNotifyAction s_xTaskNotifyFromISR_eAction;
static BaseType_t*   s_xTaskNotifyFromISR_pxHigherPriorityTaskWoken;

// Configurable return values (default to success)
static BaseType_t s_xTaskNotifyWait_return    = pdPASS;
static BaseType_t s_xTaskNotify_return        = pdPASS;
static BaseType_t s_xTaskNotifyFromISR_return = pdPASS;

// Optional output behaviour
static uint32_t   s_xTaskNotifyWait_write_value;
static BaseType_t s_xTaskNotifyWait_should_write_value;

static BaseType_t s_xTaskNotifyFromISR_write_hptw;
static BaseType_t s_xTaskNotifyFromISR_should_write_hptw;

/**-----------------------------------------------------------------------------
 *  Private (static) Function Prototypes
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */

/**
 * @brief stub implementing FreeRTOS vTaskStartScheduler
 */
void vTaskStartScheduler( void )
{
}
/**
 * @brief stub implementing FreeRTOS vTaskDelayUntil
 */
void vTaskDelayUntil( TickType_t* pxPreviousWakeTime, const TickType_t xTimeIncrement )
{
    TickType_t time_to_wake = *pxPreviousWakeTime + xTimeIncrement;

    if ( time_to_wake > current_tick )
    {
        current_tick = time_to_wake;
    }

    *pxPreviousWakeTime = time_to_wake;
}

/**
 * @brief stub implementing FreeRTOS vTaskDelay
 */
void vTaskDelay( const TickType_t xTicksToDelay )
{
    current_tick += xTicksToDelay;
}

/**
 * @brief stub implementing FreeRTOS xTaskGetTickCount
 */
volatile TickType_t xTaskGetTickCount( void )
{
    return current_tick++;
}

/**
 * @brief stub implementing FreeRTOS xTaskNotifyWait
 */
BaseType_t xTaskNotifyWait( uint32_t ulBitsToClearOnEntry, uint32_t ulBitsToClearOnExit,
                            uint32_t* pulNotificationValue, TickType_t xTicksToWait )
{
    s_xTaskNotifyWait_called++;

    s_xTaskNotifyWait_ulBitsToClearOnEntry = ulBitsToClearOnEntry;
    s_xTaskNotifyWait_ulBitsToClearOnExit  = ulBitsToClearOnExit;
    s_xTaskNotifyWait_pulNotificationValue = pulNotificationValue;
    s_xTaskNotifyWait_xTicksToWait         = xTicksToWait;

    if ( ( pulNotificationValue != NULL ) && ( s_xTaskNotifyWait_should_write_value != pdFALSE ) )
    {
        *pulNotificationValue = s_xTaskNotifyWait_write_value;
    }

    return s_xTaskNotifyWait_return;
}

/**
 * @brief stub implementing FreeRTOS xTaskNotify
 */
BaseType_t xTaskNotify( TaskHandle_t xTaskToNotify, uint32_t ulValue, eNotifyAction eAction )
{
    s_xTaskNotify_called++;

    s_xTaskNotify_xTaskToNotify = xTaskToNotify;
    s_xTaskNotify_ulValue       = ulValue;
    s_xTaskNotify_eAction       = eAction;

    return s_xTaskNotify_return;
}

/**
 * @brief stub implementing FreeRTOS xTaskNotifyFromISR
 */
BaseType_t xTaskNotifyFromISR( TaskHandle_t xTaskToNotify, uint32_t ulValue, eNotifyAction eAction,
                               BaseType_t* pxHigherPriorityTaskWoken )
{
    s_xTaskNotifyFromISR_called++;

    s_xTaskNotifyFromISR_xTaskToNotify             = xTaskToNotify;
    s_xTaskNotifyFromISR_ulValue                   = ulValue;
    s_xTaskNotifyFromISR_eAction                   = eAction;
    s_xTaskNotifyFromISR_pxHigherPriorityTaskWoken = pxHigherPriorityTaskWoken;

    if ( ( pxHigherPriorityTaskWoken != NULL )
         && ( s_xTaskNotifyFromISR_should_write_hptw != pdFALSE ) )
    {
        *pxHigherPriorityTaskWoken = s_xTaskNotifyFromISR_write_hptw;
    }

    return s_xTaskNotifyFromISR_return;
}

#endif
// NOLINTEND
