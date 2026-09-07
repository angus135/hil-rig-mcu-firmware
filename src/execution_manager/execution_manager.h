/******************************************************************************
 *  File:       execution_manager.h
 *  Author:     Angus Corr
 *  Created:    20-Dec-2025
 *
 *  Description:
 *      Task-context lifecycle interface for the Execution Manager module.
 *
 *  Notes:
 *      None
 ******************************************************************************/

#ifndef EXECUTION_MANAGER_H
#define EXECUTION_MANAGER_H

#ifdef __cplusplus
extern "C"
{
#endif

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include <stdint.h>
#include <stdbool.h>

/**-----------------------------------------------------------------------------
 *  Public Defines / Macros
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Public Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */
// Different high level states of the execution manager
typedef enum
{
    EXECUTION_MANAGER_STATE_STOPPED,
    EXECUTION_MANAGER_STATE_RUNNING,
    EXECUTION_MANAGER_STATE_COMPLETE,
    EXECUTION_MANAGER_STATE_FAILED,
    EXECUTION_MANAGER_STATE_ABORTED,
} ExecutionManagerState_T;

// Different execution manager failure modes to be reported back to host
typedef enum
{
    EXECUTION_MANAGER_FAILURE_NONE,
    EXECUTION_MANAGER_FAILURE_INSTRUCTION_UNDERRUN,
    EXECUTION_MANAGER_FAILURE_RESULT_BUFFER_FULL,
    EXECUTION_MANAGER_FAILURE_OUTPUT_REJECTED,
    EXECUTION_MANAGER_FAILURE_MEASUREMENT_INVALID,
    EXECUTION_MANAGER_FAILURE_TICK_OVERRUN,
    EXECUTION_MANAGER_FAILURE_INTERNAL,
} ExecutionManagerFailure_T;

// Status structure containing all the present information about the operation of the execution
// manager
typedef struct
{
    ExecutionManagerState_T   state;            // What state, eg: running, stopped etc.
    ExecutionManagerFailure_T failure;          // If there is a failure what failure mode
    uint32_t                  ticks_completed;  // Current number of ticks that have been completed.
} ExecutionManagerStatus_T;

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

/**
 * @brief Initialises the Execution Manager for a run.
 *
 * Timer configuration and control remain the responsibility of the Run State
 * Manager. The execution timer must be stopped when this function is called.
 *
 * @param tick_count Number of execution ticks in the run.
 * @return true when the run was accepted; otherwise, false.
 */
bool EXECUTION_MANAGER_Start( uint32_t tick_count );

/**
 * @brief Aborts the current execution run.
 *
 */
void EXECUTION_MANAGER_Abort( void );

/**
 * @brief Copies the current lifecycle status into caller-owned storage.
 *
 * @param status Destination for the status snapshot. NULL is ignored.
 */
void EXECUTION_MANAGER_Get_Status( ExecutionManagerStatus_T* status );

#ifdef __cplusplus
}
#endif

#endif /* EXECUTION_MANAGER_H */
