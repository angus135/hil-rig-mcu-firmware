/******************************************************************************
 *  File:       execution_manager.c
 *  Author:     Angus Corr
 *  Created:    20-Dec-2025
 *
 *  Description:
 *      Lifecycle and ISR scaffold for deterministic per-tick execution.
 *
 *  Notes:
 *      Instruction execution and result capture are intentionally not yet
 *      implemented.
 *      All private functions are declared inline because each has only one
 *      caller, reducing the number of instructions required for each call.
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */
#include "execution_manager.h"
#include "execution_manager_isr.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/**-----------------------------------------------------------------------------
 *  Private (static) Variables
 *------------------------------------------------------------------------------
 */
static uint32_t                          execution_tick_count = 0U;
static volatile ExecutionManagerStatus_T execution_status = {
    EXECUTION_MANAGER_STATE_STOPPED,
    EXECUTION_MANAGER_FAILURE_NONE,
    0U,
};
static volatile uint32_t current_tick = 0U;

/**-----------------------------------------------------------------------------
 *  Private (static) Function Prototypes
 *------------------------------------------------------------------------------
 */
static inline bool EXECUTION_MANAGER_Capture_Completed_Interval_From_ISR( void );
static inline bool EXECUTION_MANAGER_Finalise_Result_From_ISR( void );
static inline bool EXECUTION_MANAGER_Advance_Tick_From_ISR( void );
static inline bool EXECUTION_MANAGER_Fetch_Current_Tick_From_ISR( void );
static inline bool EXECUTION_MANAGER_Apply_Current_Tick_From_ISR( void );
static inline bool EXECUTION_MANAGER_Check_Execution_Status_From_ISR( void );
static inline void EXECUTION_MANAGER_Fail_From_ISR( ExecutionManagerFailure_T failure );
static inline void EXECUTION_MANAGER_Complete_From_ISR( void );

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */
/**
 * @brief Captures inputs and completed asynchronous measurements for the interval.
 *
 * @return true if the interval data was captured successfully; otherwise, false.
 */
static inline bool EXECUTION_MANAGER_Capture_Completed_Interval_From_ISR( void )
{
    /* TODO: Capture inputs and drain completed asynchronous measurements. */
    return true;
}

/**
 * @brief Builds and publishes the result for the completed execution tick.
 *
 * @return true if the result was finalised successfully; otherwise, false.
 */
static inline bool EXECUTION_MANAGER_Finalise_Result_From_ISR( void )
{
    /* TODO: Construct and publish the completed tick result. */
    return true;
}

/**
 * @brief Advances the current tick and completed-tick count.
 *
 * @return true if the tick counters were advanced successfully; otherwise, false.
 */
static inline bool EXECUTION_MANAGER_Advance_Tick_From_ISR( void )
{
    current_tick++;
    execution_status.ticks_completed++;
    return true;
}

/**
 * @brief Retrieves the prepared instruction for the current execution tick.
 *
 * @return true if the instruction was retrieved successfully; otherwise, false.
 */
static inline bool EXECUTION_MANAGER_Fetch_Current_Tick_From_ISR( void )
{
    /* TODO: Retrieve the prepared instruction for the authoritative tick. */
    return true;
}

/**
 * @brief Applies the outputs and starts communications scheduled for the current tick.
 *
 * @return true if the instruction was applied successfully; otherwise, false.
 */
static inline bool EXECUTION_MANAGER_Apply_Current_Tick_From_ISR( void )
{
    /* TODO: Apply fixed outputs and initiate scheduled communications. */
    return true;
}

/**
 * @brief Checks for peripheral failures and execution tick overruns.
 *
 * @return true if execution remains healthy; otherwise, false.
 */
static inline bool EXECUTION_MANAGER_Check_Execution_Status_From_ISR( void )
{
    /* TODO: Detect peripheral runtime failures and execution tick overruns. */
    return true;
}

/**
 * @brief Records the supplied failure state.
 *
 * @param failure Failure that caused execution to stop.
 */
static inline void EXECUTION_MANAGER_Fail_From_ISR( ExecutionManagerFailure_T failure )
{
    execution_status.failure = failure;
    execution_status.state   = EXECUTION_MANAGER_STATE_FAILED;
}

/**
 * @brief Marks execution as complete.
 */
static inline void EXECUTION_MANAGER_Complete_From_ISR( void )
{
    execution_status.state = EXECUTION_MANAGER_STATE_COMPLETE;
}

/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */

void EXECUTION_MANAGER_Process_From_ISR( void )
{
    if ( execution_status.state != EXECUTION_MANAGER_STATE_RUNNING )
    {
        return;
    }

    if ( !EXECUTION_MANAGER_Capture_Completed_Interval_From_ISR() )
    {
        EXECUTION_MANAGER_Fail_From_ISR( EXECUTION_MANAGER_FAILURE_MEASUREMENT_INVALID );
        return;
    }

    if ( !EXECUTION_MANAGER_Finalise_Result_From_ISR() )
    {
        EXECUTION_MANAGER_Fail_From_ISR( EXECUTION_MANAGER_FAILURE_RESULT_BUFFER_FULL );
        return;
    }

    if ( !EXECUTION_MANAGER_Advance_Tick_From_ISR() )
    {
        EXECUTION_MANAGER_Fail_From_ISR( EXECUTION_MANAGER_FAILURE_INTERNAL );
        return;
    }

    if ( !EXECUTION_MANAGER_Fetch_Current_Tick_From_ISR() )
    {
        EXECUTION_MANAGER_Fail_From_ISR( EXECUTION_MANAGER_FAILURE_INSTRUCTION_UNDERRUN );
        return;
    }

    if ( !EXECUTION_MANAGER_Apply_Current_Tick_From_ISR() )
    {
        EXECUTION_MANAGER_Fail_From_ISR( EXECUTION_MANAGER_FAILURE_OUTPUT_REJECTED );
        return;
    }

    if ( !EXECUTION_MANAGER_Check_Execution_Status_From_ISR() )
    {
        EXECUTION_MANAGER_Fail_From_ISR( EXECUTION_MANAGER_FAILURE_TICK_OVERRUN );
        return;
    }

    if ( execution_status.ticks_completed >= execution_tick_count )
    {
        EXECUTION_MANAGER_Complete_From_ISR();
    }
}

bool EXECUTION_MANAGER_Start( uint32_t tick_count )
{
    if ( tick_count == 0U )
    {
        return false;
    }

    execution_tick_count             = tick_count;
    current_tick                     = 0U;
    execution_status.failure         = EXECUTION_MANAGER_FAILURE_NONE;
    execution_status.ticks_completed = 0U;
    execution_status.state           = EXECUTION_MANAGER_STATE_RUNNING;
    return true;
}

void EXECUTION_MANAGER_Abort( void )
{
    /* TODO: Put configured peripherals into their safe state. */
    execution_status.failure = EXECUTION_MANAGER_FAILURE_NONE;
    execution_status.state   = EXECUTION_MANAGER_STATE_ABORTED;
}

void EXECUTION_MANAGER_Get_Status( ExecutionManagerStatus_T* status )
{
    ExecutionManagerStatus_T first_snapshot;
    ExecutionManagerStatus_T second_snapshot;

    if ( status == NULL )
    {
        return;
    }

    // Here we're ensuring that the status is accurate by continuously taking snapshots until we get
    // 2 that agree, ensuring that there hasn't been a change while reading the status
    do
    {
        first_snapshot.state           = execution_status.state;
        first_snapshot.failure         = execution_status.failure;
        first_snapshot.ticks_completed = execution_status.ticks_completed;

        second_snapshot.state           = execution_status.state;
        second_snapshot.failure         = execution_status.failure;
        second_snapshot.ticks_completed = execution_status.ticks_completed;
    } while ( ( first_snapshot.state != second_snapshot.state )
              || ( first_snapshot.failure != second_snapshot.failure )
              || ( first_snapshot.ticks_completed != second_snapshot.ticks_completed ) );

    *status = second_snapshot;
}
