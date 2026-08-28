/******************************************************************************
 *  File:       run_state_manager.c
 *  Author:     Callum Rafferty
 *  Created:    25-Mar-2026
 *
 *  Description:
 *      Owns the HIL-RIG runtime lifecycle and execution clock.
 *
 *  Notes:
 *      Named task notifications currently expose lifecycle control to the
 *      console and provide integration seams for future subsystem owners.
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */
#include "run_state_manager.h"
#include "dut_driver_lifecycle.h"
#include "flash_manager.h"
#include "hw_timer.h"
#include "logic_expander.h"
#include "rtos_config.h"
#include "test_configuration.h"
#include <stdint.h>
#include <stdbool.h>

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */
#define RUN_STATE_MANAGER_NOTIFY_PACKAGE_RECEIVE ( 1UL << 0U )
#define RUN_STATE_MANAGER_NOTIFY_CONFIGURATION ( 1UL << 1U )
#define RUN_STATE_MANAGER_NOTIFY_EXECUTION ( 1UL << 2U )
#define RUN_STATE_MANAGER_NOTIFY_EXECUTION_COMPLETE ( 1UL << 3U )
#define RUN_STATE_MANAGER_NOTIFY_RESULT_TRANSFER ( 1UL << 4U )
#define RUN_STATE_MANAGER_NOTIFY_RESULT_TRANSFER_COMPLETE ( 1UL << 5U )
#define RUN_STATE_MANAGER_NOTIFY_FAULT ( 1UL << 6U )
#define RUN_STATE_MANAGER_NOTIFY_RESET ( 1UL << 7U )
#define RUN_STATE_MANAGER_NOTIFY_TIMER_START ( 1UL << 8U )
#define RUN_STATE_MANAGER_NOTIFY_TIMER_STOP ( 1UL << 9U )
#define RUN_STATE_MANAGER_NOTIFY_REPEAT ( 1UL << 10U )
#define RUN_STATE_MANAGER_NOTIFY_DISCARD_RESULTS ( 1UL << 11U )

#define RUN_STATE_MANAGER_PENDING_POLL_MS ( 10U )
#define RUN_STATE_MANAGER_EXECUTION_PREPARATION_TIMEOUT_MS ( 15000U )
#define RUN_STATE_MANAGER_RESULT_FINALISATION_TIMEOUT_MS ( 15000U )

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
static TickType_t pending_operation_started_at = 0U;

static bool execution_active = false;

static bool execution_timer_running = false;

static TaskHandle_t run_state_manager_task_handle = NULL;

static volatile RunStateFaultReason_T fault_reason = RUN_STATE_FAULT_NONE;
static volatile RunStateFaultReason_T requested_fault_reason = RUN_STATE_FAULT_NONE;
static volatile RunStateRequest_T last_request = RUN_STATE_REQUEST_NONE;
static volatile RunStateRequestResult_T last_request_result = RUN_STATE_REQUEST_RESULT_NONE;

/**-----------------------------------------------------------------------------
 *  Private (static) Function Prototypes
 *------------------------------------------------------------------------------
 */
static bool RUN_STATE_MANAGER_Notify( uint32_t notification );
static void RUN_STATE_MANAGER_RecordFault( RunStateFaultReason_T reason );
static void RUN_STATE_MANAGER_EnterFault( RunStateFaultReason_T reason );

static bool RUN_STATE_MANAGER_IsTransitionAllowed( RunState_T current_state,
                                                   RunState_T next_state );

static bool RUN_STATE_MANAGER_EnterTestPackageReceive( void );
static bool RUN_STATE_MANAGER_EnterConfiguration( void );
static bool RUN_STATE_MANAGER_BeginExecutionPreparation( void );
static bool RUN_STATE_MANAGER_EnterExecution( void );
static bool RUN_STATE_MANAGER_StopExecution( void );
static bool RUN_STATE_MANAGER_BeginResultFinalisation( void );
static bool RUN_STATE_MANAGER_BeginResultTransfer( void );
static bool RUN_STATE_MANAGER_CompleteResultTransfer( void );
static bool RUN_STATE_MANAGER_DiscardCompletedResults( RunState_T next_state );
static bool RUN_STATE_MANAGER_FlashIsIdle( void );

static void RUN_STATE_MANAGER_ProcessPendingOperation( void );
static void RUN_STATE_MANAGER_ProcessRequest( RunStateRequest_T request );
static void RUN_STATE_MANAGER_ProcessNotifications( uint32_t notifications );

static bool RUN_STATE_MANAGER_TransitionTo( RunState_T next_state );
static bool RUN_STATE_MANAGER_StartExecutionTimer( void );
static void RUN_STATE_MANAGER_StopExecutionTimer( void );
static void RUN_STATE_MANAGER_StartPendingOperation( RunStatePendingOperation_T operation );
static void RUN_STATE_MANAGER_ClearPendingOperation( void );
static bool RUN_STATE_MANAGER_PendingOperationTimedOut( TickType_t timeout_ticks );

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

static void RUN_STATE_MANAGER_StartPendingOperation( RunStatePendingOperation_T operation )
{
    pending_operation            = operation;
    pending_operation_started_at = xTaskGetTickCount();
}

static void RUN_STATE_MANAGER_ClearPendingOperation( void )
{
    pending_operation            = RUN_STATE_PENDING_NONE;
    pending_operation_started_at = 0U;
}

static bool RUN_STATE_MANAGER_PendingOperationTimedOut( TickType_t timeout_ticks )
{
    const TickType_t elapsed =
        ( TickType_t )( xTaskGetTickCount() - pending_operation_started_at );
    return elapsed >= timeout_ticks;
}

static void RUN_STATE_MANAGER_RecordFault( RunStateFaultReason_T reason )
{
    if ( fault_reason == RUN_STATE_FAULT_NONE && reason != RUN_STATE_FAULT_NONE )
    {
        fault_reason = reason;
    }
}

static void RUN_STATE_MANAGER_EnterFault( RunStateFaultReason_T reason )
{
    RUN_STATE_MANAGER_RecordFault( reason );
    ( void )RUN_STATE_MANAGER_TransitionTo( RUN_STATE_FAULT );
}

/**
 * @brief Determines whether a lifecycle transition is currently supported.
 *
 * Fault is reachable from every state. Re-entering the current state is
 * treated as an idempotent successful transition.
 */
static bool RUN_STATE_MANAGER_IsTransitionAllowed( RunState_T current_state, RunState_T next_state )
{
    if ( current_state == next_state )
    {
        return true;
    }

    if ( next_state == RUN_STATE_FAULT )
    {
        return true;
    }

    switch ( current_state )
    {
        case RUN_STATE_IDLE:
            return next_state == RUN_STATE_TEST_PACKAGE_RECEIVE;

        case RUN_STATE_TEST_PACKAGE_RECEIVE:
            return next_state == RUN_STATE_CONFIGURATION || next_state == RUN_STATE_IDLE;

        case RUN_STATE_CONFIGURATION:
            return next_state == RUN_STATE_ARMED || next_state == RUN_STATE_IDLE;

        case RUN_STATE_ARMED:
            return next_state == RUN_STATE_EXECUTION || next_state == RUN_STATE_CONFIGURATION
                   || next_state == RUN_STATE_IDLE;

        case RUN_STATE_EXECUTION:
            return next_state == RUN_STATE_ARMED || next_state == RUN_STATE_RESULT_FINALISATION;

        case RUN_STATE_RESULT_FINALISATION:
            return next_state == RUN_STATE_RESULTS_READY;

        case RUN_STATE_RESULTS_READY:
            return next_state == RUN_STATE_RESULT_TRANSFER || next_state == RUN_STATE_ARMED
                   || next_state == RUN_STATE_IDLE;

        case RUN_STATE_RESULT_TRANSFER:
            return next_state == RUN_STATE_RESULTS_READY || next_state == RUN_STATE_IDLE;

        case RUN_STATE_FAULT:
            return next_state == RUN_STATE_IDLE;

        default:
            return false;
    }
}

static bool RUN_STATE_MANAGER_EnterTestPackageReceive( void )
{
    /*
     * Future responsibility:
     * Tell Host Interface to accept a new test package.
     *
     * Until that interface exists, entering the state is sufficient.
     */
    return true;
}

/**
 * @brief Applies the active test configuration before execution preparation.
 *
 * @return true when every configuration action succeeds; otherwise false.
 *
 * @note This function runs in Run State Manager task context.
 * @note All configured DUT-facing drivers must remain stopped on return.
 */
static bool RUN_STATE_MANAGER_EnterConfiguration( void )
{
    DutDriverConfiguration_T configuration = { 0 };

    if ( !LOGIC_EXPANDER_Is_Ready() )
    {
        RUN_STATE_MANAGER_RecordFault( RUN_STATE_FAULT_LOGIC_EXPANDER_NOT_READY );
        return false;
    }

    if ( !TEST_CONFIGURATION_GetActive( &configuration ) )
    {
        RUN_STATE_MANAGER_RecordFault( RUN_STATE_FAULT_CONFIGURATION_UNAVAILABLE );
        return false;
    }

    if ( !DUT_DRIVER_LIFECYCLE_Configure( &configuration ) )
    {
        RUN_STATE_MANAGER_RecordFault( RUN_STATE_FAULT_DRIVER_CONFIGURATION );
        return false;
    }

    return true;
}

/**
 * @brief Enters production execution.
 *
 * Starts all configured DUT-facing drivers before starting the execution
 * clock. If timer startup fails, the driver lifecycle is rolled back.
 *
 * @return true when the DUT drivers and execution clock are active;
 *         otherwise false.
 */
static bool RUN_STATE_MANAGER_EnterExecution( void )
{
    if ( execution_active )
    {
        return true;
    }

    if ( !DUT_DRIVER_LIFECYCLE_Start() )
    {
        RUN_STATE_MANAGER_RecordFault( RUN_STATE_FAULT_DRIVER_START );
        return false;
    }

    if ( !RUN_STATE_MANAGER_StartExecutionTimer() )
    {
        ( void )DUT_DRIVER_LIFECYCLE_Stop();
        RUN_STATE_MANAGER_RecordFault( RUN_STATE_FAULT_EXECUTION_TIMER );
        return false;
    }

    execution_active = true;
    return true;
}
/**
 * @brief Stops the execution timer followed by all active DUT-facing drivers.
 */
static bool RUN_STATE_MANAGER_StopExecution( void )
{
    RUN_STATE_MANAGER_StopExecutionTimer();

    if ( execution_active )
    {
        execution_active = false;
        if ( !DUT_DRIVER_LIFECYCLE_Stop() )
        {
            RUN_STATE_MANAGER_RecordFault( RUN_STATE_FAULT_DRIVER_STOP );
            return false;
        }
    }

    return true;
}

/**
 * @brief Requests asynchronous preparation of the Flash Manager for execution.
 */
static bool RUN_STATE_MANAGER_BeginExecutionPreparation( void )
{
    FlashManagerRequestStatus_T status = FLASH_MANAGER_RequestExecutionPreparation();

    if ( status == FLASH_MANAGER_REQUEST_OK )
    {
        RUN_STATE_MANAGER_StartPendingOperation(
            RUN_STATE_PENDING_EXECUTION_PREPARATION );
        return true;
    }

    RUN_STATE_MANAGER_EnterFault( RUN_STATE_FAULT_FLASH_EXECUTION_PREPARATION );
    return false;
}

/**
 * @brief Stops execution and requests asynchronous result finalisation.
 */
static bool RUN_STATE_MANAGER_BeginResultFinalisation( void )
{
    if ( !RUN_STATE_MANAGER_StopExecution() )
    {
        RUN_STATE_MANAGER_EnterFault( RUN_STATE_FAULT_DRIVER_STOP );
        return false;
    }

    FlashManagerRequestStatus_T status = FLASH_MANAGER_RequestResultFinalisation();

    if ( status == FLASH_MANAGER_REQUEST_OK )
    {
        RUN_STATE_MANAGER_StartPendingOperation(
            RUN_STATE_PENDING_RESULT_FINALISATION );
        ( void )RUN_STATE_MANAGER_TransitionTo( RUN_STATE_RESULT_FINALISATION );
        return true;
    }

    RUN_STATE_MANAGER_EnterFault( RUN_STATE_FAULT_FLASH_RESULT_FINALISATION );
    return false;
}

/** Starts Flash Manager result retrieval before entering RESULT_TRANSFER. */
static bool RUN_STATE_MANAGER_BeginResultTransfer( void )
{
    if ( FLASH_MANAGER_RequestResultTransferStart() != FLASH_MANAGER_RESULT_TRANSFER_OK )
    {
        RUN_STATE_MANAGER_EnterFault( RUN_STATE_FAULT_FLASH_RESULT_TRANSFER );
        return false;
    }

    return RUN_STATE_MANAGER_TransitionTo( RUN_STATE_RESULT_TRANSFER );
}

/** Finishes a fully consumed Flash result stream before returning to IDLE. */
static bool RUN_STATE_MANAGER_CompleteResultTransfer( void )
{
    const FlashManagerResultTransferStatus_T status = FLASH_MANAGER_FinishResultTransfer();

    if ( status == FLASH_MANAGER_RESULT_TRANSFER_INVALID_STATE )
    {
        return false;
    }

    if ( status != FLASH_MANAGER_RESULT_TRANSFER_OK )
    {
        RUN_STATE_MANAGER_EnterFault( RUN_STATE_FAULT_FLASH_RESULT_TRANSFER );
        return false;
    }

    return RUN_STATE_MANAGER_TransitionTo( RUN_STATE_IDLE );
}

/**
 * @brief Releases completed Flash results before applying the selected policy.
 */
static bool RUN_STATE_MANAGER_DiscardCompletedResults( RunState_T next_state )
{
    const FlashManagerRequestStatus_T status = FLASH_MANAGER_DiscardResults();
    if ( status != FLASH_MANAGER_REQUEST_OK )
    {
        RUN_STATE_MANAGER_EnterFault( RUN_STATE_FAULT_FLASH_RESULT_DISPOSITION );
        return false;
    }

    if ( next_state == RUN_STATE_IDLE )
    {
        TEST_CONFIGURATION_Clear();
    }

    return RUN_STATE_MANAGER_TransitionTo( next_state );
}

/** Returns true only when Flash is in the reusable IDLE state. */
static bool RUN_STATE_MANAGER_FlashIsIdle( void )
{
    FlashManagerState_T flash_state = FLASH_MANAGER_STATE_UNINITIALISED;
    return FLASH_MANAGER_GetState( &flash_state ) && flash_state == FLASH_MANAGER_STATE_IDLE;
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
        RUN_STATE_MANAGER_EnterFault( RUN_STATE_FAULT_FLASH_MANAGER );
        return;
    }

    if ( flash_state == FLASH_MANAGER_STATE_FAULT )
    {
        RUN_STATE_MANAGER_EnterFault( RUN_STATE_FAULT_FLASH_MANAGER );
        return;
    }

    switch ( pending_operation )
    {
        case RUN_STATE_PENDING_EXECUTION_PREPARATION:
            if ( flash_state == FLASH_MANAGER_STATE_EXECUTING )
            {
                RUN_STATE_MANAGER_ClearPendingOperation();
                ( void )RUN_STATE_MANAGER_TransitionTo( RUN_STATE_EXECUTION );
            }
            else if ( flash_state != FLASH_MANAGER_STATE_PREPARING_EXECUTION )
            {
                RUN_STATE_MANAGER_EnterFault( RUN_STATE_FAULT_FLASH_EXECUTION_PREPARATION );
            }
            else if ( RUN_STATE_MANAGER_PendingOperationTimedOut( pdMS_TO_TICKS(
                          RUN_STATE_MANAGER_EXECUTION_PREPARATION_TIMEOUT_MS ) ) )
            {
                RUN_STATE_MANAGER_EnterFault(
                    RUN_STATE_FAULT_FLASH_EXECUTION_PREPARATION_TIMEOUT );
            }
            break;

        case RUN_STATE_PENDING_RESULT_FINALISATION:
            if ( flash_state == FLASH_MANAGER_STATE_RESULTS_READY )
            {
                RUN_STATE_MANAGER_ClearPendingOperation();
                ( void )RUN_STATE_MANAGER_TransitionTo( RUN_STATE_RESULTS_READY );
            }
            else if ( flash_state != FLASH_MANAGER_STATE_FINALISING_RESULTS )
            {
                RUN_STATE_MANAGER_EnterFault( RUN_STATE_FAULT_FLASH_RESULT_FINALISATION );
            }
            else if ( RUN_STATE_MANAGER_PendingOperationTimedOut( pdMS_TO_TICKS(
                          RUN_STATE_MANAGER_RESULT_FINALISATION_TIMEOUT_MS ) ) )
            {
                RUN_STATE_MANAGER_EnterFault(
                    RUN_STATE_FAULT_FLASH_RESULT_FINALISATION_TIMEOUT );
            }
            break;

        case RUN_STATE_PENDING_NONE:
        default:
            RUN_STATE_MANAGER_EnterFault( RUN_STATE_FAULT_INTERNAL );
            break;
    }
}

/** Validates and processes one externally supplied lifecycle event. */
static void RUN_STATE_MANAGER_ProcessRequest( RunStateRequest_T request )
{
    last_request = request;

    if ( pending_operation != RUN_STATE_PENDING_NONE
         && request != RUN_STATE_REQUEST_FAULT
         && request != RUN_STATE_REQUEST_DIAGNOSTIC_TIMER_STOP )
    {
        last_request_result = RUN_STATE_REQUEST_RESULT_REJECTED_PENDING;
        return;
    }

    bool accepted = false;

    switch ( request )
    {
        case RUN_STATE_REQUEST_PACKAGE_RECEIVE:
            if ( run_state == RUN_STATE_IDLE )
            {
                accepted = RUN_STATE_MANAGER_TransitionTo( RUN_STATE_TEST_PACKAGE_RECEIVE );
            }
            break;

        case RUN_STATE_REQUEST_CONFIGURATION_READY:
            if ( run_state == RUN_STATE_TEST_PACKAGE_RECEIVE )
            {
                accepted = RUN_STATE_MANAGER_TransitionTo( RUN_STATE_CONFIGURATION );
                if ( accepted )
                {
                    accepted = RUN_STATE_MANAGER_TransitionTo( RUN_STATE_ARMED );
                }
            }
            break;

        case RUN_STATE_REQUEST_EXECUTION:
            if ( run_state == RUN_STATE_ARMED )
            {
                accepted = RUN_STATE_MANAGER_BeginExecutionPreparation();
            }
            break;

        case RUN_STATE_REQUEST_EXECUTION_COMPLETE:
            if ( run_state == RUN_STATE_EXECUTION )
            {
                accepted = RUN_STATE_MANAGER_BeginResultFinalisation();
            }
            break;

        case RUN_STATE_REQUEST_RESULT_TRANSFER:
            if ( run_state == RUN_STATE_RESULTS_READY )
            {
                accepted = RUN_STATE_MANAGER_BeginResultTransfer();
            }
            break;

        case RUN_STATE_REQUEST_RESULT_TRANSFER_COMPLETE:
            if ( run_state == RUN_STATE_RESULT_TRANSFER )
            {
                accepted = RUN_STATE_MANAGER_CompleteResultTransfer();
                if ( !accepted && run_state != RUN_STATE_FAULT )
                {
                    last_request_result =
                        RUN_STATE_REQUEST_RESULT_REJECTED_SUBSYSTEM_STATE;
                    return;
                }
            }
            break;

        case RUN_STATE_REQUEST_REPEAT:
            if ( run_state == RUN_STATE_RESULTS_READY )
            {
                accepted = RUN_STATE_MANAGER_DiscardCompletedResults( RUN_STATE_ARMED );
            }
            break;

        case RUN_STATE_REQUEST_DISCARD_RESULTS:
            if ( run_state == RUN_STATE_RESULTS_READY )
            {
                accepted = RUN_STATE_MANAGER_DiscardCompletedResults( RUN_STATE_IDLE );
            }
            break;

        case RUN_STATE_REQUEST_FAULT:
        {
            const RunStateFaultReason_T reason = requested_fault_reason;
            requested_fault_reason             = RUN_STATE_FAULT_NONE;
            RUN_STATE_MANAGER_EnterFault( reason );
            accepted = run_state == RUN_STATE_FAULT;
            break;
        }

        case RUN_STATE_REQUEST_RESET:
            if ( run_state == RUN_STATE_FAULT )
            {
                if ( !RUN_STATE_MANAGER_FlashIsIdle() )
                {
                    last_request_result =
                        RUN_STATE_REQUEST_RESULT_REJECTED_SUBSYSTEM_STATE;
                    return;
                }

                accepted = RUN_STATE_MANAGER_TransitionTo( RUN_STATE_IDLE );
                if ( accepted )
                {
                    fault_reason = RUN_STATE_FAULT_NONE;
                }
            }
            break;

        case RUN_STATE_REQUEST_DIAGNOSTIC_TIMER_START:
            if ( run_state != RUN_STATE_FAULT )
            {
                accepted = RUN_STATE_MANAGER_StartExecutionTimer();
                if ( !accepted )
                {
                    RUN_STATE_MANAGER_EnterFault( RUN_STATE_FAULT_EXECUTION_TIMER );
                }
            }
            break;

        case RUN_STATE_REQUEST_DIAGNOSTIC_TIMER_STOP:
            accepted = RUN_STATE_MANAGER_StopExecution();
            break;

        case RUN_STATE_REQUEST_NONE:
        default:
            break;
    }

    if ( accepted )
    {
        last_request_result = RUN_STATE_REQUEST_RESULT_ACCEPTED;
    }
    else if ( run_state == RUN_STATE_FAULT )
    {
        last_request_result = RUN_STATE_REQUEST_RESULT_FAILED;
    }
    else
    {
        last_request_result = RUN_STATE_REQUEST_RESULT_REJECTED_STATE;
    }
}

/**
 * @brief Processes one notification and preserves any coalesced notifications.
 *
 * Fault, reset, and timer-stop requests have priority. Remaining bits are
 * returned to the task notification word so no named event is silently lost.
 */
static void RUN_STATE_MANAGER_ProcessNotifications( uint32_t notifications )
{
    uint32_t          selected_bit = 0U;
    RunStateRequest_T request      = RUN_STATE_REQUEST_NONE;

    if ( ( notifications & RUN_STATE_MANAGER_NOTIFY_FAULT ) != 0U )
    {
        selected_bit = RUN_STATE_MANAGER_NOTIFY_FAULT;
        request      = RUN_STATE_REQUEST_FAULT;
    }
    else if ( ( notifications & RUN_STATE_MANAGER_NOTIFY_RESET ) != 0U )
    {
        selected_bit = RUN_STATE_MANAGER_NOTIFY_RESET;
        request      = RUN_STATE_REQUEST_RESET;
    }
    else if ( ( notifications & RUN_STATE_MANAGER_NOTIFY_TIMER_STOP ) != 0U )
    {
        selected_bit = RUN_STATE_MANAGER_NOTIFY_TIMER_STOP;
        request      = RUN_STATE_REQUEST_DIAGNOSTIC_TIMER_STOP;
    }
    else if ( ( notifications & RUN_STATE_MANAGER_NOTIFY_PACKAGE_RECEIVE ) != 0U )
    {
        selected_bit = RUN_STATE_MANAGER_NOTIFY_PACKAGE_RECEIVE;
        request      = RUN_STATE_REQUEST_PACKAGE_RECEIVE;
    }
    else if ( ( notifications & RUN_STATE_MANAGER_NOTIFY_CONFIGURATION ) != 0U )
    {
        selected_bit = RUN_STATE_MANAGER_NOTIFY_CONFIGURATION;
        request      = RUN_STATE_REQUEST_CONFIGURATION_READY;
    }
    else if ( ( notifications & RUN_STATE_MANAGER_NOTIFY_EXECUTION ) != 0U )
    {
        selected_bit = RUN_STATE_MANAGER_NOTIFY_EXECUTION;
        request      = RUN_STATE_REQUEST_EXECUTION;
    }
    else if ( ( notifications & RUN_STATE_MANAGER_NOTIFY_EXECUTION_COMPLETE ) != 0U )
    {
        selected_bit = RUN_STATE_MANAGER_NOTIFY_EXECUTION_COMPLETE;
        request      = RUN_STATE_REQUEST_EXECUTION_COMPLETE;
    }
    else if ( ( notifications & RUN_STATE_MANAGER_NOTIFY_RESULT_TRANSFER ) != 0U )
    {
        selected_bit = RUN_STATE_MANAGER_NOTIFY_RESULT_TRANSFER;
        request      = RUN_STATE_REQUEST_RESULT_TRANSFER;
    }
    else if ( ( notifications & RUN_STATE_MANAGER_NOTIFY_RESULT_TRANSFER_COMPLETE ) != 0U )
    {
        selected_bit = RUN_STATE_MANAGER_NOTIFY_RESULT_TRANSFER_COMPLETE;
        request      = RUN_STATE_REQUEST_RESULT_TRANSFER_COMPLETE;
    }
    else if ( ( notifications & RUN_STATE_MANAGER_NOTIFY_TIMER_START ) != 0U )
    {
        selected_bit = RUN_STATE_MANAGER_NOTIFY_TIMER_START;
        request      = RUN_STATE_REQUEST_DIAGNOSTIC_TIMER_START;
    }
    else if ( ( notifications & RUN_STATE_MANAGER_NOTIFY_REPEAT ) != 0U )
    {
        selected_bit = RUN_STATE_MANAGER_NOTIFY_REPEAT;
        request      = RUN_STATE_REQUEST_REPEAT;
    }
    else if ( ( notifications & RUN_STATE_MANAGER_NOTIFY_DISCARD_RESULTS ) != 0U )
    {
        selected_bit = RUN_STATE_MANAGER_NOTIFY_DISCARD_RESULTS;
        request      = RUN_STATE_REQUEST_DISCARD_RESULTS;
    }

    if ( selected_bit == 0U )
    {
        return;
    }

    RUN_STATE_MANAGER_ProcessRequest( request );

    const uint32_t remaining_notifications = notifications & ~selected_bit;
    if ( remaining_notifications != 0U )
    {
        ( void )RUN_STATE_MANAGER_Notify( remaining_notifications );
    }
}

/**
 * @brief Performs common exit and entry actions for a lifecycle transition.
 *
 * @param next_state State to enter.
 */
static bool RUN_STATE_MANAGER_TransitionTo( RunState_T next_state )
{
    if ( run_state == next_state )
    {
        return true;
    }

    if ( !RUN_STATE_MANAGER_IsTransitionAllowed( run_state, next_state ) )
    {
        RUN_STATE_MANAGER_EnterFault( RUN_STATE_FAULT_INVALID_TRANSITION );
        return false;
    }

    if ( next_state == RUN_STATE_FAULT )
    {
        RUN_STATE_MANAGER_ClearPendingOperation();
        ( void )RUN_STATE_MANAGER_StopExecution();
    }
    else if ( next_state != RUN_STATE_EXECUTION )
    {
        if ( !RUN_STATE_MANAGER_StopExecution() )
        {
            RUN_STATE_MANAGER_EnterFault( RUN_STATE_FAULT_DRIVER_STOP );
            return false;
        }
    }

    switch ( next_state )
    {
        case RUN_STATE_IDLE:
            DUT_DRIVER_LIFECYCLE_EnterIdle();
            break;

        case RUN_STATE_TEST_PACKAGE_RECEIVE:
            if ( !RUN_STATE_MANAGER_EnterTestPackageReceive() )
            {
                RUN_STATE_MANAGER_EnterFault( RUN_STATE_FAULT_INTERNAL );
                return false;
            }
            break;

        case RUN_STATE_CONFIGURATION:
            if ( !RUN_STATE_MANAGER_EnterConfiguration() )
            {
                RUN_STATE_MANAGER_EnterFault( RUN_STATE_FAULT_DRIVER_CONFIGURATION );
                return false;
            }
            break;

        case RUN_STATE_ARMED:
            break;

        case RUN_STATE_EXECUTION:
            if ( !RUN_STATE_MANAGER_EnterExecution() )
            {
                RUN_STATE_MANAGER_EnterFault( RUN_STATE_FAULT_INTERNAL );
                return false;
            }
            break;

        case RUN_STATE_RESULT_FINALISATION:
        case RUN_STATE_RESULTS_READY:
            break;

        case RUN_STATE_RESULT_TRANSFER:
            /* Future Host Interface result-transfer entry action. */
            break;

        case RUN_STATE_FAULT:
            DUT_DRIVER_LIFECYCLE_EnterFault();
            break;

        default:
            RUN_STATE_MANAGER_EnterFault( RUN_STATE_FAULT_INTERNAL );
            return false;
    }

    run_state = next_state;
    return true;
}

/**
 * @brief Configures and starts the Run State Manager-owned execution timer.
 */
static bool RUN_STATE_MANAGER_StartExecutionTimer( void )
{
    if ( execution_timer_running )
    {
        return true;
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
            return false;
    }

    HW_TIMER_Start_Timer( EXECUTION_MANAGER_TIMER );
    execution_timer_running = true;

    return true;
}

/**
 * @brief Stops the Run State Manager-owned execution timer.
 */
static void RUN_STATE_MANAGER_StopExecutionTimer( void )
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
    RUN_STATE_MANAGER_StopExecutionTimer();
    frequency_mode          = RUN_STATE_FREQUENCY_1KHZ;
    pending_operation       = RUN_STATE_PENDING_NONE;
    pending_operation_started_at = 0U;
    execution_active        = false;
    execution_timer_running = false;
    fault_reason            = RUN_STATE_FAULT_NONE;
    requested_fault_reason  = RUN_STATE_FAULT_NONE;
    last_request            = RUN_STATE_REQUEST_NONE;
    last_request_result     = RUN_STATE_REQUEST_RESULT_NONE;
    run_state               = RUN_STATE_IDLE;
}

bool RUN_STATE_MANAGER_RequestPackageReceive( void )
{
    return RUN_STATE_MANAGER_Notify( RUN_STATE_MANAGER_NOTIFY_PACKAGE_RECEIVE );
}

bool RUN_STATE_MANAGER_RequestConfiguration( void )
{
    return RUN_STATE_MANAGER_Notify( RUN_STATE_MANAGER_NOTIFY_CONFIGURATION );
}

bool RUN_STATE_MANAGER_RequestExecution( void )
{
    return RUN_STATE_MANAGER_Notify( RUN_STATE_MANAGER_NOTIFY_EXECUTION );
}

bool RUN_STATE_MANAGER_RequestExecutionComplete( void )
{
    return RUN_STATE_MANAGER_Notify( RUN_STATE_MANAGER_NOTIFY_EXECUTION_COMPLETE );
}

bool RUN_STATE_MANAGER_RequestResultTransfer( void )
{
    return RUN_STATE_MANAGER_Notify( RUN_STATE_MANAGER_NOTIFY_RESULT_TRANSFER );
}

bool RUN_STATE_MANAGER_RequestResultTransferComplete( void )
{
    return RUN_STATE_MANAGER_Notify( RUN_STATE_MANAGER_NOTIFY_RESULT_TRANSFER_COMPLETE );
}

bool RUN_STATE_MANAGER_RequestRepeat( void )
{
    return RUN_STATE_MANAGER_Notify( RUN_STATE_MANAGER_NOTIFY_REPEAT );
}

bool RUN_STATE_MANAGER_RequestDiscardResults( void )
{
    return RUN_STATE_MANAGER_Notify( RUN_STATE_MANAGER_NOTIFY_DISCARD_RESULTS );
}

bool RUN_STATE_MANAGER_RequestFault( RunStateFaultReason_T reason )
{
    if ( reason == RUN_STATE_FAULT_NONE )
    {
        return false;
    }

    bool stored_reason = false;

    taskENTER_CRITICAL();
    if ( requested_fault_reason == RUN_STATE_FAULT_NONE )
    {
        requested_fault_reason = reason;
        stored_reason          = true;
    }
    taskEXIT_CRITICAL();

    if ( !stored_reason )
    {
        return true;
    }

    if ( RUN_STATE_MANAGER_Notify( RUN_STATE_MANAGER_NOTIFY_FAULT ) )
    {
        return true;
    }

    taskENTER_CRITICAL();
    if ( requested_fault_reason == reason )
    {
        requested_fault_reason = RUN_STATE_FAULT_NONE;
    }
    taskEXIT_CRITICAL();
    return false;
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

bool RUN_STATE_MANAGER_IsExecutionActive( void )
{
    return execution_active;
}

bool RUN_STATE_MANAGER_IsExecutionTimerRunning( void )
{
    return execution_timer_running;
}

RunStateFaultReason_T RUN_STATE_MANAGER_GetFaultReason( void )
{
    return fault_reason;
}

RunStateRequest_T RUN_STATE_MANAGER_GetLastRequest( void )
{
    return last_request;
}

RunStateRequestResult_T RUN_STATE_MANAGER_GetLastRequestResult( void )
{
    return last_request_result;
}

void RUN_STATE_MANAGER_Task( void* task_parameters )
{
    ( void )task_parameters;

    RUN_STATE_MANAGER_Init();
    run_state_manager_task_handle = xTaskGetCurrentTaskHandle();

    while ( true )
    {
        uint32_t   notifications = 0U;
        TickType_t wait_ticks    = portMAX_DELAY;

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
