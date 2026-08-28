/******************************************************************************
 *  File:       run_state_manager.c
 *  Author:     Callum Rafferty
 *  Created:    25-Mar-2026
 *
 *  Description:
 *      Owns the HIL-RIG runtime lifecycle and execution clock.
 *
 *  Notes:
 *      Console controls currently step through the lifecycle manually. State
 *      entry hooks are intentionally minimal until subsystem-driven transition
 *      behaviour is designed.
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */
#include "run_state_manager.h"
#include "dut_driver_lifecycle.h"
#include "flash_manager.h"
#include "hw_timer.h"
#include "rtos_config.h"
#include <stdint.h>
#include <stdbool.h>

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */
#define RUN_STATE_MANAGER_NOTIFY_STEP ( 1UL << 0U )
#define RUN_STATE_MANAGER_NOTIFY_FAULT ( 1UL << 1U )
#define RUN_STATE_MANAGER_NOTIFY_RESET ( 1UL << 2U )
#define RUN_STATE_MANAGER_NOTIFY_TIMER_START ( 1UL << 3U )
#define RUN_STATE_MANAGER_NOTIFY_TIMER_STOP ( 1UL << 4U )

#define RUN_STATE_MANAGER_PENDING_POLL_MS ( 10U )

#define PSC_100HZ 14U
#define ARR_100HZ 59999U

#define PSC_1KHZ 1U
#define ARR_1KHZ 44999U

#define PSC_10KHZ 0U
#define ARR_10KHZ 8999U

/**-----------------------------------------------------------------------------
 *  Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

typedef enum
{
    RUN_STATE_PENDING_NONE = 0,
    RUN_STATE_PENDING_EXECUTION_PREPARATION,
    RUN_STATE_PENDING_RESULT_FINALISATION
} RunStatePendingOperation_T;

/**-----------------------------------------------------------------------------
 *  Public (global) and Extern Variables
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Private (static) Variables
 *------------------------------------------------------------------------------
 */
static volatile RunState_T run_state = RUN_STATE_IDLE;

static RunStateFrequencyMode_T frequency_mode = RUN_STATE_FREQUENCY_1KHZ;

static RunStatePendingOperation_T pending_operation = RUN_STATE_PENDING_NONE;

static bool execution_active = false;

static bool execution_timer_running = false;

static TaskHandle_t run_state_manager_task_handle = NULL;

/**-----------------------------------------------------------------------------
 *  Private (static) Function Prototypes
 *------------------------------------------------------------------------------
 */
static bool RUN_STATE_MANAGER_Notify( uint32_t notification );
static bool RUN_STATE_MANAGER_StartExecution( void );
static void RUN_STATE_MANAGER_StopExecution( void );
static void RUN_STATE_MANAGER_BeginExecutionPreparation( void );
static void RUN_STATE_MANAGER_BeginResultFinalisation( void );
static void RUN_STATE_MANAGER_ProcessPendingOperation( void );
static void RUN_STATE_MANAGER_ProcessNotifications( uint32_t notifications );
static void RUN_STATE_MANAGER_TransitionTo( RunState_T next_state );
static void RUN_STATE_MANAGER_Step( void );
static void RUN_STATE_MANAGER_Start_Execution_Timer( void );
static void RUN_STATE_MANAGER_Stop_Execution_Timer( void );

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

/**
 * @brief Sends a notification to the Run State Manager task.
 *
 * @param notification Notification bit to set.
 *
 * @returns true if the notification was delivered, otherwise false.
 */
static bool RUN_STATE_MANAGER_Notify( uint32_t notification )
{
    if ( run_state_manager_task_handle == NULL )
    {
        return false;
    }

    return xTaskNotify( run_state_manager_task_handle, notification, eSetBits ) == pdPASS;
}

/**
 * @brief Starts the DUT-facing drivers followed by the execution timer.
 *
 * @returns true when execution is active, otherwise false.
 */
static bool RUN_STATE_MANAGER_StartExecution( void )
{
    if ( execution_active )
    {
        return true;
    }

    if ( !DUT_DRIVER_LIFECYCLE_Start() )
    {
        return false;
    }

    RUN_STATE_MANAGER_Start_Execution_Timer();
    execution_active = true;

    return true;
}

/**
 * @brief Stops the execution timer followed by all active DUT-facing drivers.
 */
static void RUN_STATE_MANAGER_StopExecution( void )
{
    RUN_STATE_MANAGER_Stop_Execution_Timer();

    if ( execution_active )
    {
        execution_active = false;
        DUT_DRIVER_LIFECYCLE_Stop();
    }
}

/**
 * @brief Requests asynchronous preparation of the Flash Manager for execution.
 */
static void RUN_STATE_MANAGER_BeginExecutionPreparation( void )
{
    FlashManagerRequestStatus_T status = FLASH_MANAGER_RequestExecutionPreparation();

    if ( status == FLASH_MANAGER_REQUEST_OK )
    {
        pending_operation = RUN_STATE_PENDING_EXECUTION_PREPARATION;
        return;
    }

    RUN_STATE_MANAGER_TransitionTo( RUN_STATE_FAULT );
}

/**
 * @brief Stops execution and requests asynchronous result finalisation.
 */
static void RUN_STATE_MANAGER_BeginResultFinalisation( void )
{
    RUN_STATE_MANAGER_StopExecution();

    FlashManagerRequestStatus_T status = FLASH_MANAGER_RequestResultFinalisation();

    if ( status == FLASH_MANAGER_REQUEST_OK )
    {
        pending_operation = RUN_STATE_PENDING_RESULT_FINALISATION;
        return;
    }

    RUN_STATE_MANAGER_TransitionTo( RUN_STATE_FAULT );
}

/**
 * @brief Advances an asynchronous Flash Manager lifecycle operation.
 */
static void RUN_STATE_MANAGER_ProcessPendingOperation( void )
{
    if ( pending_operation == RUN_STATE_PENDING_NONE )
    {
        return;
    }

    FlashManagerState_T flash_state = FLASH_MANAGER_STATE_UNINITIALISED;

    if ( !FLASH_MANAGER_GetState( &flash_state ) )
    {
        RUN_STATE_MANAGER_TransitionTo( RUN_STATE_FAULT );
        return;
    }

    if ( flash_state == FLASH_MANAGER_STATE_FAULT )
    {
        RUN_STATE_MANAGER_TransitionTo( RUN_STATE_FAULT );
        return;
    }

    switch ( pending_operation )
    {
        case RUN_STATE_PENDING_EXECUTION_PREPARATION:
            if ( flash_state == FLASH_MANAGER_STATE_EXECUTING )
            {
                pending_operation = RUN_STATE_PENDING_NONE;
                RUN_STATE_MANAGER_TransitionTo( RUN_STATE_EXECUTION );
            }
            else if ( flash_state != FLASH_MANAGER_STATE_PREPARING_EXECUTION )
            {
                RUN_STATE_MANAGER_TransitionTo( RUN_STATE_FAULT );
            }
            break;

        case RUN_STATE_PENDING_RESULT_FINALISATION:
            if ( flash_state == FLASH_MANAGER_STATE_RESULTS_READY )
            {
                pending_operation = RUN_STATE_PENDING_NONE;
                RUN_STATE_MANAGER_TransitionTo( RUN_STATE_RESULT_TRANSFER );
            }
            else if ( flash_state != FLASH_MANAGER_STATE_FINALISING_RESULTS )
            {
                RUN_STATE_MANAGER_TransitionTo( RUN_STATE_FAULT );
            }
            break;

        case RUN_STATE_PENDING_NONE:
        default:
            RUN_STATE_MANAGER_TransitionTo( RUN_STATE_FAULT );
            break;
    }
}

/**
 * @brief Processes task notifications in fault, reset, then step priority.
 *
 * @param notifications Notification bits received by the task.
 */
static void RUN_STATE_MANAGER_ProcessNotifications( uint32_t notifications )
{
    if ( ( notifications & RUN_STATE_MANAGER_NOTIFY_FAULT ) != 0U )
    {
        RUN_STATE_MANAGER_TransitionTo( RUN_STATE_FAULT );
        return;
    }

    if ( ( notifications & RUN_STATE_MANAGER_NOTIFY_RESET ) != 0U )
    {
        if ( run_state == RUN_STATE_FAULT )
        {
            RUN_STATE_MANAGER_TransitionTo( RUN_STATE_IDLE );
        }
        return;
    }

    if ( ( notifications & RUN_STATE_MANAGER_NOTIFY_TIMER_STOP ) != 0U )
    {
        RUN_STATE_MANAGER_StopExecution();
        return;
    }

    if ( ( notifications & RUN_STATE_MANAGER_NOTIFY_TIMER_START ) != 0U )
    {
        if ( run_state != RUN_STATE_FAULT && pending_operation == RUN_STATE_PENDING_NONE )
        {
            RUN_STATE_MANAGER_Start_Execution_Timer();
        }
        return;
    }

    if ( ( notifications & RUN_STATE_MANAGER_NOTIFY_STEP ) != 0U
         && pending_operation == RUN_STATE_PENDING_NONE )
    {
        RUN_STATE_MANAGER_Step();
    }
}

/**
 * @brief Performs common exit and entry actions for a lifecycle transition.
 *
 * @param next_state State to enter.
 */
static void RUN_STATE_MANAGER_TransitionTo( RunState_T next_state )
{
    if ( run_state == next_state )
    {
        return;
    }

    if ( next_state == RUN_STATE_FAULT )
    {
        pending_operation = RUN_STATE_PENDING_NONE;
        RUN_STATE_MANAGER_StopExecution();
    }
    else if ( next_state != RUN_STATE_EXECUTION )
    {
        RUN_STATE_MANAGER_StopExecution();
    }

    switch ( next_state )
    {
        case RUN_STATE_IDLE:
            DUT_DRIVER_LIFECYCLE_EnterIdle();
            break;
        case RUN_STATE_TEST_PACKAGE_RECEIVE:
            /* TODO: Request test-package reception from the Host Interface. */
            break;
        case RUN_STATE_CONFIGURATION:
            if ( !DUT_DRIVER_LIFECYCLE_Configure() )
            {
                RUN_STATE_MANAGER_TransitionTo( RUN_STATE_FAULT );
                return;
            }
            break;
        case RUN_STATE_EXECUTION:
            if ( !RUN_STATE_MANAGER_StartExecution() )
            {
                RUN_STATE_MANAGER_TransitionTo( RUN_STATE_FAULT );
                return;
            }
            break;
        case RUN_STATE_RESULT_TRANSFER:
            /* TODO: Finalise and request transfer of the test results. */
            break;
        case RUN_STATE_FAULT:
            DUT_DRIVER_LIFECYCLE_EnterFault();
            break;
        default:
            RUN_STATE_MANAGER_TransitionTo( RUN_STATE_FAULT );
            return;
    }

    run_state = next_state;
}

/**
 * @brief Advances the manual lifecycle by one state.
 */
static void RUN_STATE_MANAGER_Step( void )
{
    switch ( run_state )
    {
        case RUN_STATE_IDLE:
            RUN_STATE_MANAGER_TransitionTo( RUN_STATE_TEST_PACKAGE_RECEIVE );
            break;
        case RUN_STATE_TEST_PACKAGE_RECEIVE:
            RUN_STATE_MANAGER_TransitionTo( RUN_STATE_CONFIGURATION );
            break;
        case RUN_STATE_CONFIGURATION:
            RUN_STATE_MANAGER_BeginExecutionPreparation();
            break;
        case RUN_STATE_EXECUTION:
            RUN_STATE_MANAGER_BeginResultFinalisation();
            break;
        case RUN_STATE_RESULT_TRANSFER:
            RUN_STATE_MANAGER_TransitionTo( RUN_STATE_IDLE );
            break;
        case RUN_STATE_FAULT:
        default:
            break;
    }
}

/**
 * @brief Configures and starts the Run State Manager-owned execution timer.
 */
static void RUN_STATE_MANAGER_Start_Execution_Timer( void )
{
    if ( execution_timer_running )
    {
        return;
    }

    switch ( frequency_mode )
    {
        case RUN_STATE_FREQUENCY_100HZ:
            HW_TIMER_Configure_Timer( EXECUTION_MANAGER_TIMER, PSC_100HZ, ARR_100HZ );
            break;
        case RUN_STATE_FREQUENCY_1KHZ:
            HW_TIMER_Configure_Timer( EXECUTION_MANAGER_TIMER, PSC_1KHZ, ARR_1KHZ );
            break;
        case RUN_STATE_FREQUENCY_10KHZ:
            HW_TIMER_Configure_Timer( EXECUTION_MANAGER_TIMER, PSC_10KHZ, ARR_10KHZ );
            break;
        default:
            return;
    }

    HW_TIMER_Start_Timer( EXECUTION_MANAGER_TIMER );
    execution_timer_running = true;
}

/**
 * @brief Stops the Run State Manager-owned execution timer.
 */
static void RUN_STATE_MANAGER_Stop_Execution_Timer( void )
{
    HW_TIMER_Stop_Timer( EXECUTION_MANAGER_TIMER );
    execution_timer_running = false;
}

/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */

void RUN_STATE_MANAGER_Set_Execution_Frequency( RunStateFrequencyMode_T mode )
{
    switch ( mode )
    {
        // Defensive, only accept valid frequency modes.
        case RUN_STATE_FREQUENCY_100HZ:
        case RUN_STATE_FREQUENCY_1KHZ:
        case RUN_STATE_FREQUENCY_10KHZ:
            frequency_mode = mode;
            break;
        default:
            break;
    }
}

RunStateFrequencyMode_T RUN_STATE_MANAGER_Get_Execution_Frequency( void )
{
    return frequency_mode;
}

void RUN_STATE_MANAGER_Init( void )
{
    RUN_STATE_MANAGER_Stop_Execution_Timer();
    frequency_mode    = RUN_STATE_FREQUENCY_1KHZ;
    pending_operation = RUN_STATE_PENDING_NONE;
    execution_active  = false;
    execution_timer_running = false;
    run_state         = RUN_STATE_IDLE;
}

bool RUN_STATE_MANAGER_RequestStep( void )
{
    return RUN_STATE_MANAGER_Notify( RUN_STATE_MANAGER_NOTIFY_STEP );
}

bool RUN_STATE_MANAGER_RequestFault( void )
{
    return RUN_STATE_MANAGER_Notify( RUN_STATE_MANAGER_NOTIFY_FAULT );
}

bool RUN_STATE_MANAGER_RequestReset( void )
{
    return RUN_STATE_MANAGER_Notify( RUN_STATE_MANAGER_NOTIFY_RESET );
}

bool RUN_STATE_MANAGER_RequestExecutionTimerStart( void )
{
    return RUN_STATE_MANAGER_Notify( RUN_STATE_MANAGER_NOTIFY_TIMER_START );
}

bool RUN_STATE_MANAGER_RequestExecutionTimerStop( void )
{
    return RUN_STATE_MANAGER_Notify( RUN_STATE_MANAGER_NOTIFY_TIMER_STOP );
}

RunState_T RUN_STATE_MANAGER_GetState( void )
{
    return run_state;
}

bool RUN_STATE_MANAGER_IsTransitionPending( void )
{
    return pending_operation != RUN_STATE_PENDING_NONE;
}

void RUN_STATE_MANAGER_Task( void* task_parameters )
{
    ( void )task_parameters;

    RUN_STATE_MANAGER_Init();
    run_state_manager_task_handle = xTaskGetCurrentTaskHandle();

    while ( true )
    {
        uint32_t notifications = 0U;
        TickType_t wait_ticks  = portMAX_DELAY;

        if ( pending_operation != RUN_STATE_PENDING_NONE )
        {
            wait_ticks = pdMS_TO_TICKS( RUN_STATE_MANAGER_PENDING_POLL_MS );
        }

        if ( xTaskNotifyWait( 0U, UINT32_MAX, &notifications, wait_ticks ) == pdTRUE )
        {
            RUN_STATE_MANAGER_ProcessNotifications( notifications );
        }

        RUN_STATE_MANAGER_ProcessPendingOperation();
    }
}
