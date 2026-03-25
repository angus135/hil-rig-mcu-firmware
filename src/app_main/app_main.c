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
extern TaskHandle_t* ConsoleTaskHandle;     // NOLINT(readability-identifier-naming)
extern TaskHandle_t* BackgroundTaskHandle;  // NOLINT(readability-identifier-naming)

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

    vTaskStartScheduler();
}
