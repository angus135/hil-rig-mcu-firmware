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
#include "rtos.h"
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
 *  Private (static) Variables
 *------------------------------------------------------------------------------
 */
static TickType_t current_tick = 0;

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
void vTaskStartScheduler(void)
{
}
/**
 * @brief stub implementing FreeRTOS vTaskDelayUntil
 */
void vTaskDelayUntil(TickType_t* pxPreviousWakeTime, const TickType_t xTimeIncrement)
{
    TickType_t time_to_wake = *pxPreviousWakeTime + xTimeIncrement;

    if (time_to_wake > current_tick)
    {
        current_tick = time_to_wake;
    }

    *pxPreviousWakeTime = time_to_wake;
}
/**
 * @brief stub implementing FreeRTOS xTaskGetTickCount
 */
volatile TickType_t xTaskGetTickCount(void)
{
    return current_tick++;
}
#endif
// NOLINTEND
