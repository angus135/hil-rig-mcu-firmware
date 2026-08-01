/******************************************************************************
 *  File:       execution_manager.c
 *  Author:     Angus Corr
 *  Created:    20-Dec-2025
 *
 *  Description:
 *
 *  Notes:
 *     None
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#ifdef TEST_BUILD
#include "tests/host_communications_mocks.h"
#else
#include "main.h"
#endif
#include "rtos_config.h"
#include "hw_usb.h"
#include <stdint.h>
#include <stdbool.h>

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */
#define HOST_INTERFACE_PERIOD 1000  // 1Hz

/**-----------------------------------------------------------------------------
 *  Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Public (global) and Extern Variables
 *------------------------------------------------------------------------------
 */

TaskHandle_t* HostInterfaceTaskHandle = NULL;  // NOLINT(readability-identifier-naming)

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

/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */

/**
 * @brief Host Interface Task
 *
 * The FreeRTOS task that runs all the host interface related logic
 */
void HOST_INTERFACE_Task( void* task_parameters )
{
    ( void )task_parameters;

    if ( !HW_USB_Init() )
    {
        Error_Handler();
    }

    TickType_t initial_ticks = xTaskGetTickCount();
    while ( true )
    {
        // TODO: Implement host interface

        HW_USB_Monitor_Process();

        vTaskDelayUntil( &initial_ticks, pdMS_TO_TICKS( HOST_INTERFACE_PERIOD ) );
    }
}
