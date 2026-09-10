/******************************************************************************
 *  File:       host_communications.c
 *  Author:     Angus Corr
 *  Created:    20-Dec-2025
 *
 *  Description:
 *      Runs host transport processing from task context.
 *
 *  Notes:
 *      Instruction upload must use the Flash Manager public lifecycle. The
 *      current periodic task is only a transport skeleton; upload state,
 *      retry/backpressure handling, and canonical conversion are not yet wired.
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
 * The FreeRTOS task that runs host transport and future Flash Manager upload
 * and result-transfer workflows.
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
        /*
         * TODO: Add streamed instruction upload in task context:
         * - validate and canonicalise ordered tick instructions and operations;
         * - request upload start and wait for INSTRUCTION_UPLOAD;
         * - submit chunks atomically, retaining an unchanged chunk on BUSY;
         * - request finish after every declared byte is accepted; and
         * - wait for IDLE or FAULT before reporting completion to the host.
         *
         * The current one-second loop is a monitor placeholder, not the final
         * upload retry cadence. Never call external_flash from this task.
         */

        HW_USB_Monitor_Process();

        vTaskDelayUntil( &initial_ticks, pdMS_TO_TICKS( HOST_INTERFACE_PERIOD ) );
    }
}
