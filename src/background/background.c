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
#include "logic_expander.h"
#include <stddef.h>
#include <stdint.h>

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */
#define BACKGROUND_TASK_PERIOD_MS ( 10U )
#define BACKGROUND_LED_PERIOD_MS ( 1000U )
#define BACKGROUND_LED_PERIOD_CYCLES ( BACKGROUND_LED_PERIOD_MS / BACKGROUND_TASK_PERIOD_MS )
/**-----------------------------------------------------------------------------
 *  Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */
typedef void ( *BackgroundProcess_T )( void );

/**-----------------------------------------------------------------------------
 *  Public (global) and Extern Variables
 *------------------------------------------------------------------------------
 */

TaskHandle_t* BackgroundTaskHandle = NULL;  // NOLINT(readability-identifier-naming)

/**-----------------------------------------------------------------------------
 *  Private (static) Function Prototypes
 *------------------------------------------------------------------------------
 */
static void BACKGROUND_Process_Logic_Expander( void );
static void BACKGROUND_Process_Status_LED( void );

/**-----------------------------------------------------------------------------
 *  Private (static) Variables
 *------------------------------------------------------------------------------
 */
static uint16_t background_led_cycles_remaining = 0U;

static const BackgroundProcess_T background_processes[] = {
    BACKGROUND_Process_Logic_Expander,
    BACKGROUND_Process_Status_LED,
};

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */
static void BACKGROUND_Process_Logic_Expander( void )
{
    ( void )LOGIC_EXPANDER_Process();
}

static void BACKGROUND_Process_Status_LED( void )
{
    if ( background_led_cycles_remaining == 0U )
    {
        HW_GPIO_Toggle_Output( USER_LED_BLUE_0 );
        HW_GPIO_Toggle_Output( USER_LED_BLUE_1 );
        HW_GPIO_Toggle_Output( USER_LED_BLUE_2 );
        HW_GPIO_Toggle_Output( USER_LED_BLUE_3 );
        HW_GPIO_Toggle_Output( USER_LED_BLUE_4 );
        HW_GPIO_Toggle_Output( USER_LED_BLUE_5 );

        HW_GPIO_Toggle_Output( USER_LED_RED_0 );
        HW_GPIO_Toggle_Output( USER_LED_RED_1 );
        HW_GPIO_Toggle_Output( USER_LED_RED_2 );
        HW_GPIO_Toggle_Output( USER_LED_RED_3 );
        HW_GPIO_Toggle_Output( USER_LED_RED_4 );
        HW_GPIO_Toggle_Output( USER_LED_RED_5 );

        background_led_cycles_remaining = BACKGROUND_LED_PERIOD_CYCLES;
    }

    background_led_cycles_remaining--;
}

static void BACKGROUND_Process( void )
{
    for ( size_t process_index = 0U;
          process_index < ( sizeof( background_processes ) / sizeof( background_processes[0] ) );
          process_index++ )
    {
        background_processes[process_index]();
    }
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
        vTaskDelayUntil( &initial_ticks, pdMS_TO_TICKS( BACKGROUND_TASK_PERIOD_MS ) );
    }
}
