/******************************************************************************
 *  File:       console.c
 *  Author:     Angus Corr
 *  Created:    6-Dec-2025
 *
 *  Description:
 *      Console module implementation.
 *
 *      This module provides:
 *        - A simple command line console which can be accessed to interface with the MCU
 *
 *  Notes:
 *     None
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */
#include "rtos_config.h"
#include "console.h"
#include "hw_gpio.h"
#include <stdint.h>
#include <stdbool.h>

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */
#define CONSOLE_TASK_PERIOD 100 // 10Hz
/**-----------------------------------------------------------------------------
 *  Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

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

static void CONSOLE_Process(void)
{
    HW_GPIO_Toggle(GPIO_GREEN_LED_INDICATOR);
}

/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */

/**
 * @brief Console Task
 *
 * The FreeRTOS task that runs all the console related logic
 */
void CONSOLE_Task(void* task_parameters)
{
    (void)task_parameters;

    TickType_t initial_ticks = xTaskGetTickCount();
    while (true)
    {
        CONSOLE_Process();
        vTaskDelayUntil(&initial_ticks, pdMS_TO_TICKS(CONSOLE_TASK_PERIOD));
    }
}
