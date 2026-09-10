/******************************************************************************
 *  File:       execution_manager.h
 *
 *  Description:
 *      Task-context preparation and status interface for deterministic test
 *      execution.
 ******************************************************************************/

#ifndef EXECUTION_MANAGER_H
#define EXECUTION_MANAGER_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdbool.h>
#include <stdint.h>
#include "execution_measurement_adapters.h"
#include "rtos_config.h"

typedef enum
{
    EXECUTION_MANAGER_FAILURE_NONE = 0,
    EXECUTION_MANAGER_FAILURE_NOT_PREPARED,
    EXECUTION_MANAGER_FAILURE_INSTRUCTION_UNDERRUN,
    EXECUTION_MANAGER_FAILURE_INSTRUCTION_CORRUPT,
    EXECUTION_MANAGER_FAILURE_INSTRUCTION_LATE,
    EXECUTION_MANAGER_FAILURE_OPERATION_REJECTED,
    EXECUTION_MANAGER_FAILURE_INSTRUCTION_CONSUME,
    EXECUTION_MANAGER_FAILURE_MEASUREMENT_REJECTED
} ExecutionManagerFailure_T;

typedef enum
{
    EXECUTION_MANAGER_TICK_CONTINUE = 0,
    EXECUTION_MANAGER_TICK_COMPLETE,
    EXECUTION_MANAGER_TICK_FAILED
} ExecutionManagerTickResult_T;

typedef void ( *ExecutionManagerTerminalCallback_T )( ExecutionManagerTickResult_T result,
                                                      ExecutionManagerFailure_T    failure,
                                                      BaseType_t* higher_priority_task_woken );

/**
 * @brief Prepares run-local state before the execution timer is started.
 *
 * The Run State Manager owns execution-timer configuration and control. The
 * timer must be stopped while this function executes, and Flash Manager must
 * already be prepared for execution.
 *
 * Tick zero is the configured initial condition and is not processed by a
 * timer interrupt. The first interrupt processes tick one. A run of N ticks
 * therefore processes boundaries 1 through N and completes after boundary N.
 * Output instruction timestamps must be in that range.
 *
 * @param tick_count Final boundary tick and number of timer periods in the run.
 * @return true when the run was accepted; otherwise, false.
 */
bool EXECUTION_MANAGER_Prepare( uint32_t tick_count );

/** Builds the active per-tick measurement list from validated session configuration. */
void EXECUTION_MANAGER_ConfigureMeasurements(
    const ExecutionMeasurementConfiguration_T* configuration );

/** Arms per-opcode cycle profiling for the next prepared execution only. */
void EXECUTION_MANAGER_RequestOperationTiming( void );

/**
 * @brief Registers the lifecycle owner's terminal ISR notification callback.
 *
 * The callback runs inside the execution timer ISR. It must use only ISR-safe
 * operations and accumulate any requested task wake through
 * higher_priority_task_woken. The outer timer ISR performs the single yield
 * after all execution-boundary work has finished.
 */
void EXECUTION_MANAGER_SetTerminalCallback( ExecutionManagerTerminalCallback_T callback );

/**
 * @brief Deactivates the current run after the execution timer has stopped.
 *
 * The last processed tick and first failure remain available for diagnostics.
 * EXECUTION_MANAGER_Prepare() resets all run-local state before the next run.
 */
void EXECUTION_MANAGER_Abort( void );

/**
 * Returns zero before the first interrupt, then the boundary currently or
 * most recently processed.
 */
uint32_t EXECUTION_MANAGER_GetCurrentTick( void );

/** Returns the first failure latched during the current run. */
ExecutionManagerFailure_T EXECUTION_MANAGER_GetFailure( void );

#ifdef __cplusplus
}
#endif

#endif /* EXECUTION_MANAGER_H */
