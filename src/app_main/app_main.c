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
#include "flash_manager.h"
#include "host_communications.h"

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

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */

/**
 * @brief Entry point for MCU application
 */
void APP_MAIN_Application( void )
{
#if GLOBAL_CONFIG__CONSOLE_ENABLED
    CREATE_TASK( CONSOLE_Task, "Console Task", CONSOLE_TASK_MEMORY, CONSOLE_TASK_PRIORITY,
                 ConsoleTaskHandle );
#endif
    CREATE_TASK( BACKGROUND_Task, "Background Task", BACKGROUND_TASK_MEMORY,
                 BACKGROUND_TASK_PRIORITY, BackgroundTaskHandle );

    CREATE_TASK( HOST_INTERFACE_Task, "Host Interface Task", HOST_INTERFACE_TASK_MEMORY,
                 HOST_INTERFACE_TASK_PRIORITY, HostInterfaceTaskHandle );

    /*
     * Enable after external-flash and Flash Manager initialization have been
     * added to the startup sequence.
     */
    // CREATE_TASK( FLASH_MANAGER_Task, "Flash Manager Task", FLASH_MANAGER_TASK_MEMORY,
    //              FLASH_MANAGER_TASK_PRIORITY, NULL );

    vTaskStartScheduler();
}
