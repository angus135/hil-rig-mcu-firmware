/******************************************************************************
 *  File:       background.c
 *  Author:     Angus Corr
 *  Created:    20-Dec-2025
 *
 *  Description:
 *      Background module implementation.
 *
 *      This module provides:
 *        - A task that calls a set of background functions that run with low priority
 *
 *  Notes:
 *     None
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */
#include "rtos_config.h"
#include "background.h"
#include "hw_gpio.h"
#include <stdint.h>
#include <stdbool.h>

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */
#define BACKGROUND_TASK_PERIOD 1000  // 1Hz
/**-----------------------------------------------------------------------------
 *  Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Public (global) and Extern Variables
 *------------------------------------------------------------------------------
 */

TaskHandle_t* BackgroundTaskHandle = NULL;  // NOLINT(readability-identifier-naming)

/**-----------------------------------------------------------------------------
 *  Private (static) Variables
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Private (static) Function Prototypes
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

static void BACKGROUND_Process( void )
{
    HW_GPIO_Toggle( GPIO_GREEN_LED_INDICATOR );
}

/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */

/**
 * @brief Console Task
 *
 * The FreeRTOS task that runs all the background related logic
 */
void BACKGROUND_Task( void* task_parameters )
{
    ( void )task_parameters;

    TickType_t initial_ticks = xTaskGetTickCount();
    while ( true )
    {
        BACKGROUND_Process();
        vTaskDelayUntil( &initial_ticks, pdMS_TO_TICKS( BACKGROUND_TASK_PERIOD ) );
    }
}
