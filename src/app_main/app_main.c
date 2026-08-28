/******************************************************************************
 *  File:       app_main.c
 *  Author:     Angus Corr
 *  Created:    6-12-2025
 *
 *  Description:
 *      Runs the MCU application
 *
 *  Notes:
 *      None
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */
#include <stdbool.h>
#include "global_config.h"
#include "background.h"
#include "rtos_config.h"
#include "app_main.h"
#include "console.h"
#include "host_communications.h"
#include "run_state_manager.h"

#ifndef TEST_BUILD
#include "quadspi.h"
#endif

#include "external_flash.h"
#include "flash_manager.h"
#include "hw_qspi.h"

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */

#define APP_MAIN_QSPI_TIMEOUT_MS ( 1000U )

/**-----------------------------------------------------------------------------
 *  Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Public (global) and Extern Variables
 *------------------------------------------------------------------------------
 */
extern TaskHandle_t* ConsoleTaskHandle;        // NOLINT(readability-identifier-naming)
extern TaskHandle_t* BackgroundTaskHandle;     // NOLINT(readability-identifier-naming)
extern TaskHandle_t* HostInterfaceTaskHandle;  // NOLINT(readability-identifier-naming)

/**-----------------------------------------------------------------------------
 *  Private (static) Variables
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Private (static) Function Prototypes
 *------------------------------------------------------------------------------
 */

static bool APP_MAIN_Initialise_Storage( void );

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

static bool APP_MAIN_Initialise_Storage( void )
{
#ifndef TEST_BUILD
    if ( HW_QSPI_AdoptHandle( &hqspi, APP_MAIN_QSPI_TIMEOUT_MS ) != HW_QSPI_STATUS_OK )
    {
        return false;
    }
#endif

    if ( EXTERNAL_FLASH_Init() != EXTERNAL_FLASH_STATUS_OK )
    {
        return false;
    }

    if ( !FLASH_MANAGER_Init() )
    {
        return false;
    }

    return true;
}

/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */

/**
 * @brief Entry point for MCU application
 */
void APP_MAIN_Application( void )
{

    if ( !APP_MAIN_Initialise_Storage() )
    {
        return;
    }

    if ( CREATE_TASK( FLASH_MANAGER_Task, "Flash Manager Task", FLASH_MANAGER_TASK_MEMORY,
                      FLASH_MANAGER_TASK_PRIORITY, NULL )
         != pdPASS )
    {
        return;
    }

    if ( CREATE_TASK( RUN_STATE_MANAGER_Task, "Run State Manager Task",
                      RUN_STATE_MANAGER_TASK_MEMORY, RUN_STATE_MANAGER_TASK_PRIORITY, NULL )
         != pdPASS )
    {
        return;
    }

#if GLOBAL_CONFIG__CONSOLE_ENABLED
    CREATE_TASK( CONSOLE_Task, "Console Task", CONSOLE_TASK_MEMORY, CONSOLE_TASK_PRIORITY,
                 ConsoleTaskHandle );
#endif
    CREATE_TASK( BACKGROUND_Task, "Background Task", BACKGROUND_TASK_MEMORY,
                 BACKGROUND_TASK_PRIORITY, BackgroundTaskHandle );

    CREATE_TASK( HOST_INTERFACE_Task, "Host Interface Task", HOST_INTERFACE_TASK_MEMORY,
                 HOST_INTERFACE_TASK_PRIORITY, HostInterfaceTaskHandle );

    vTaskStartScheduler();
}
