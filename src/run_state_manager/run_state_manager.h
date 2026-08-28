/******************************************************************************
 *  File:       run_state_manager.h
 *  Author:     Angus Corr
 *  Created:    25-Mar-2026
 *
 *  Description:
 *      Public interface for coordinating the HIL-RIG runtime lifecycle.
 *
 *  Notes:
 *      The Run State Manager is an RTOS task that owns the system lifecycle
 *      and execution clock. Console controls currently provide manual state
 *      progression while subsystem-driven transitions are developed.
 ******************************************************************************/

#ifndef RUN_STATE_MANAGER_H
#define RUN_STATE_MANAGER_H

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

/** Run State Manager task stack allocation, in FreeRTOS stack words. */
#define RUN_STATE_MANAGER_TASK_MEMORY ( 256U )

/** Run State Manager task scheduling priority. */
#define RUN_STATE_MANAGER_TASK_PRIORITY ( 3U )

/**-----------------------------------------------------------------------------
 *  Public Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */
typedef enum
{
    RUN_STATE_IDLE = 0,
    RUN_STATE_TEST_PACKAGE_RECEIVE,
    RUN_STATE_CONFIGURATION,
    RUN_STATE_EXECUTION,
    RUN_STATE_RESULT_TRANSFER,
    RUN_STATE_FAULT
} RunState_T;

/** Supported execution-clock frequencies. */
typedef enum
{
    RUN_STATE_FREQUENCY_100HZ = 0,
    RUN_STATE_FREQUENCY_1KHZ,
    RUN_STATE_FREQUENCY_10KHZ
} RunStateFrequencyMode_T;

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

/*
 * Execution and Flash Manager integration reference
 * -------------------------------------------------
 *
 * The Run State Manager owns the global HIL-RIG state. It must not directly
 * assign Flash Manager internal state, access result_buffer, or call
 * external_flash for normal execution result handling.
 *
 * Required startup order before accepting a run request:
 *
 * 1. Adopt and initialise the QSPI/external-flash stack.
 * 2. Call FLASH_MANAGER_Init().
 * 3. Create and start the Flash Manager task so its task handle is registered.
 * 4. Start the Run State Manager task and accept lifecycle requests.
 *
 * Start-of-run handshake:
 *
 * 1. Call FLASH_MANAGER_RequestExecutionPreparation().
 * 2. Wait asynchronously until FLASH_MANAGER_GetState() reports
 *    FLASH_MANAGER_STATE_EXECUTING. Treat FLASH_MANAGER_STATE_FAULT as a failed
 *    run preparation.
 * 3. Only then may the Run State Manager start the execution clock. TIM4
 *    continues to dispatch each tick to EXECUTION_MANAGER_Process_From_ISR().
 *
 * End-of-run handshake:
 *
 * 1. Stop the execution clock first. The execution ISR must be completely
 *    finished, with every result lease committed or cancelled, before
 *    finalisation is requested.
 * 2. Call FLASH_MANAGER_RequestResultFinalisation().
 * 3. Wait asynchronously for FLASH_MANAGER_STATE_RESULTS_READY or
 *    FLASH_MANAGER_STATE_FAULT. RESULTS_READY guarantees that every committed
 *    result byte, including a final partial NAND page, has been drained.
 *
 * Execution-overrun/infeasibility handling:
 *
 * 1. The Execution Manager reports a head instruction whose timestamp is less
 *    than the current tick through an ISR-safe fault handoff.
 * 2. Stop the execution timer and wait for the active ISR to return. The late
 *    instruction is not consumed.
 * 3. Record the test outcome as infeasible. Future pre-execution feasibility
 *    validation should reject this workload before the timer starts.
 * 4. Decide whether committed diagnostic results should be preserved through
 *    FLASH_MANAGER_RequestResultFinalisation(). The current Flash Manager has
 *    no discard/abort-session API; normal finalisation reaches RESULTS_READY
 *    without changing the global test outcome from infeasible.
 *
 * Every FlashManagerRequestStatus_T value must be handled. In particular,
 * TASK_NOT_READY means startup integration is incomplete, INVALID_STATE means
 * the lifecycle sequence is wrong, and NOTIFY_FAILED leaves the Flash Manager
 * in FAULT.
 *
 * Result retrieval will later own the RESULTS_READY -> TRANSFERRING_RESULTS
 * transition and the eventual return to IDLE. Until that path exists, a
 * completed result session must not be overwritten by starting another run.
 *
 * The Run State Manager owns execution-clock configuration, start, and stop.
 * The Execution Manager owns the work performed for each generated tick.
 */

/**
 * @brief Sets the execution-clock frequency mode.
 *
 * @param mode - the selected frequency mode
 *
 * An updated mode takes effect the next time the execution clock is started.
 */
void RUN_STATE_MANAGER_Set_Execution_Frequency( RunStateFrequencyMode_T mode );

/**
 * @brief Gets the configured execution-clock frequency mode.
 *
 * @returns The currently configured frequency mode.
 */
RunStateFrequencyMode_T RUN_STATE_MANAGER_Get_Execution_Frequency( void );

/**
 * @brief Initialises the Run State Manager.
 *
 * Initialises only resources owned by the Run State Manager. It sets the
 * lifecycle to idle, selects the default execution-clock frequency, and
 * ensures the execution clock is stopped. Peripheral and driver initialisation
 * remains the responsibility of the application startup sequence.
 */
void RUN_STATE_MANAGER_Init( void );

/**
 * @brief Requests one manual lifecycle step.
 *
 * @returns true if the request was delivered to the task, otherwise false.
 */
bool RUN_STATE_MANAGER_RequestStep( void );

/**
 * @brief Requests a transition to the fault state.
 *
 * @returns true if the request was delivered to the task, otherwise false.
 */
bool RUN_STATE_MANAGER_RequestFault( void );

/**
 * @brief Requests a reset from fault to idle.
 *
 * @returns true if the request was delivered to the task, otherwise false.
 */
bool RUN_STATE_MANAGER_RequestReset( void );

/**
 * @brief Requests diagnostic execution-timer start through the manager task.
 *
 * This bring-up API intentionally bypasses normal Flash preparation and DUT
 * driver start sequencing. Production test execution must use lifecycle
 * transitions instead.
 *
 * @returns true if the request was delivered to the task, otherwise false.
 */
bool RUN_STATE_MANAGER_RequestExecutionTimerStart( void );

/**
 * @brief Requests diagnostic execution-timer stop through the manager task.
 *
 * If normal execution is active, DUT-facing drivers are also stopped safely.
 *
 * @returns true if the request was delivered to the task, otherwise false.
 */
bool RUN_STATE_MANAGER_RequestExecutionTimerStop( void );

/**
 * @brief Gets the current lifecycle state.
 *
 * @returns The current lifecycle state.
 */
RunState_T RUN_STATE_MANAGER_GetState( void );

/**
 * @brief Reports whether an asynchronous lifecycle transition is in progress.
 *
 * @returns true while the manager is waiting for a subordinate manager,
 *          otherwise false.
 */
bool RUN_STATE_MANAGER_IsTransitionPending( void );

/**
 * @brief Run State Manager FreeRTOS task entry point.
 *
 * @param task_parameters Unused task parameter reserved for future use.
 */
void RUN_STATE_MANAGER_Task( void* task_parameters );

#ifdef __cplusplus
}
#endif

#endif /* RUN_STATE_MANAGER_H */
