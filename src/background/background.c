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
#include "run_state_manager.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

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
static void BACKGROUND_Write_LED( GPIOOutput_T led, bool is_on );
static void BACKGROUND_Write_State_LEDs( RunState_T state );

/**-----------------------------------------------------------------------------
 *  Private (static) Variables
 *------------------------------------------------------------------------------
 */
static uint16_t   background_led_cycles_remaining   = 0U;
static bool       background_state_leds_initialised = false;
static RunState_T background_displayed_state        = RUN_STATE_IDLE;
static bool       background_fault_active           = false;

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

static void BACKGROUND_Write_LED( GPIOOutput_T led, bool is_on )
{
    if ( is_on )
    {
        HW_GPIO_Set_Single_Pin( led );
    }
    else
    {
        HW_GPIO_Reset_Single_Pin( led );
    }
}

static void BACKGROUND_Write_State_LEDs( RunState_T state )
{
    uint8_t state_code = ( uint8_t )state + 1U;

    for ( uint8_t bit = 0U; bit < 4U; bit++ )
    {
        BACKGROUND_Write_LED( ( GPIOOutput_T )( USER_LED_BLUE_5 - bit ),
                              ( state_code & ( uint8_t )( 1U << bit ) ) != 0U );
    }
}

static void BACKGROUND_Process_Status_LED( void )
{
    RunStateManagerStatus_T status;
    ( void )memset( &status, 0, sizeof( status ) );
    RUN_STATE_MANAGER_GetStatus( &status );

    if ( !background_state_leds_initialised || ( status.state != background_displayed_state ) )
    {
        BACKGROUND_Write_State_LEDs( status.state );
        background_displayed_state        = status.state;
        background_state_leds_initialised = true;
    }

    if ( !status.execution_timer_running )
    {
        HW_GPIO_Reset_Single_Pin( USER_LED_BLUE_1 );
    }

    bool fault_active = status.state == RUN_STATE_FAULT;
    if ( fault_active && !background_fault_active )
    {
        for ( uint32_t led = ( uint32_t )USER_LED_RED_0; led <= ( uint32_t )USER_LED_RED_5; led++ )
        {
            HW_GPIO_Set_Single_Pin( ( GPIOOutput_T )led );
        }
        background_led_cycles_remaining = BACKGROUND_LED_PERIOD_CYCLES;
    }
    else if ( !fault_active && background_fault_active )
    {
        for ( uint32_t led = ( uint32_t )USER_LED_RED_0; led <= ( uint32_t )USER_LED_RED_5; led++ )
        {
            HW_GPIO_Reset_Single_Pin( ( GPIOOutput_T )led );
        }
    }
    background_fault_active = fault_active;

    if ( background_led_cycles_remaining == 0U )
    {
        HW_GPIO_Toggle_Output( USER_LED_BLUE_0 );

        if ( fault_active )
        {
            for ( uint32_t led = ( uint32_t )USER_LED_RED_0; led <= ( uint32_t )USER_LED_RED_5;
                  led++ )
            {
                HW_GPIO_Toggle_Output( ( GPIOOutput_T )led );
            }
        }

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
