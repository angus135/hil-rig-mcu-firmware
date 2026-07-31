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
typedef enum FrequencyMode_T
{
    FREQUENCY_100HZ,
    FREQUENCY_1KHZ,
    FREQUENCY_10KHZ,
} FrequencyMode_T;

typedef struct
{
    FrequencyMode_T frequency_mode;
    uint32_t        tick_count;
} ExecutionManagerConfig_T;

typedef enum
{
    EXECUTION_MANAGER_STATE_STOPPED,
    EXECUTION_MANAGER_STATE_START_PENDING,
    EXECUTION_MANAGER_STATE_RUNNING,
    EXECUTION_MANAGER_STATE_COMPLETE,
    EXECUTION_MANAGER_STATE_FAILED,
    EXECUTION_MANAGER_STATE_ABORTED,
} ExecutionManagerState_T;

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

typedef struct
{
    ExecutionManagerState_T   state;
    ExecutionManagerFailure_T failure;
    uint32_t                  ticks_completed;
} ExecutionManagerStatus_T;

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

/**
 * @brief Starts an execution run.
 *
 * @param config Execution frequency and number of ticks to run.
 * @return true when the configuration was accepted and the timer was started.
 */
bool EXECUTION_MANAGER_Start( const ExecutionManagerConfig_T* config );

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
