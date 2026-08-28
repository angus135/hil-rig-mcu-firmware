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
 *      and execution clock. Named console events currently stand in for
 *      future Host Interface and Execution Manager event sources.
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
    RUN_STATE_ARMED,
    RUN_STATE_EXECUTION,
    RUN_STATE_RESULT_FINALISATION,
    RUN_STATE_RESULTS_READY,
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

/** First cause that forced the global lifecycle into FAULT. */
typedef enum
{
    RUN_STATE_FAULT_NONE = 0,
    RUN_STATE_FAULT_EXTERNAL_REQUEST,
    RUN_STATE_FAULT_INVALID_TRANSITION,
    RUN_STATE_FAULT_LOGIC_EXPANDER_NOT_READY,
    RUN_STATE_FAULT_CONFIGURATION_UNAVAILABLE,
    RUN_STATE_FAULT_DRIVER_CONFIGURATION,
    RUN_STATE_FAULT_DRIVER_CONFIGURATION_TIMEOUT,
    RUN_STATE_FAULT_DRIVER_START,
    RUN_STATE_FAULT_DRIVER_STOP,
    RUN_STATE_FAULT_EXECUTION_TIMER,
    RUN_STATE_FAULT_FLASH_EXECUTION_PREPARATION,
    RUN_STATE_FAULT_FLASH_EXECUTION_PREPARATION_TIMEOUT,
    RUN_STATE_FAULT_FLASH_RESULT_FINALISATION,
    RUN_STATE_FAULT_FLASH_RESULT_FINALISATION_TIMEOUT,
    RUN_STATE_FAULT_FLASH_RESULT_TRANSFER,
    RUN_STATE_FAULT_FLASH_RESULT_DISPOSITION,
    RUN_STATE_FAULT_FLASH_MANAGER,
    RUN_STATE_FAULT_INTERNAL
} RunStateFaultReason_T;

/** External event most recently evaluated by the Run State Manager task. */
typedef enum
{
    RUN_STATE_REQUEST_NONE = 0,
    RUN_STATE_REQUEST_PACKAGE_RECEIVE,
    RUN_STATE_REQUEST_CONFIGURATION_READY,
    RUN_STATE_REQUEST_EXECUTION,
    RUN_STATE_REQUEST_EXECUTION_COMPLETE,
    RUN_STATE_REQUEST_RESULT_TRANSFER,
    RUN_STATE_REQUEST_RESULT_TRANSFER_COMPLETE,
    RUN_STATE_REQUEST_REPEAT,
    RUN_STATE_REQUEST_DISCARD_RESULTS,
    RUN_STATE_REQUEST_FAULT,
    RUN_STATE_REQUEST_RESET,
    RUN_STATE_REQUEST_DIAGNOSTIC_TIMER_START,
    RUN_STATE_REQUEST_DIAGNOSTIC_TIMER_STOP
} RunStateRequest_T;

/** Result of validating and processing an external lifecycle event. */
typedef enum
{
    RUN_STATE_REQUEST_RESULT_NONE = 0,
    RUN_STATE_REQUEST_RESULT_ACCEPTED,
    RUN_STATE_REQUEST_RESULT_REJECTED_STATE,
    RUN_STATE_REQUEST_RESULT_REJECTED_PENDING,
    RUN_STATE_REQUEST_RESULT_REJECTED_SUBSYSTEM_STATE,
    RUN_STATE_REQUEST_RESULT_FAILED
} RunStateRequestResult_T;

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
 * Fault recovery:
 *
 * After stopping the execution clock and DUT drivers, fault entry requests
 * FLASH_MANAGER_RequestAbortSession(). Flash cleanup is asynchronous. Reset is
 * rejected until FLASH_MANAGER_STATE_IDLE confirms that runtime buffer
 * ownership has been released safely.
 *
 * Every FlashManagerRequestStatus_T value must be handled. In particular,
 * TASK_NOT_READY means startup integration is incomplete, INVALID_STATE means
 * the lifecycle sequence is wrong, and NOTIFY_FAILED leaves the Flash Manager
 * in FAULT.
 *
 * Completed results may be transferred normally, deliberately discarded to
 * return to IDLE, or discarded for a repeat run that retains the active
 * configuration and uploaded instructions before returning to ARMED.
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
 * @brief Requests entry into test-package reception from IDLE.
 *
 * @returns true if the request was delivered to the task, otherwise false.
 */
bool RUN_STATE_MANAGER_RequestPackageReceive( void );

/** Requests application of the committed configuration. */
bool RUN_STATE_MANAGER_RequestConfiguration( void );

/** Requests execution preparation from ARMED. */
bool RUN_STATE_MANAGER_RequestExecution( void );

/**
 * @brief Reports normal execution completion in task context.
 *
 * This is the future Execution Manager integration seam. No Execution Manager
 * dependency is introduced by this interface.
 */
bool RUN_STATE_MANAGER_RequestExecutionComplete( void );

/** Requests entry into result transfer from RESULTS_READY. */
bool RUN_STATE_MANAGER_RequestResultTransfer( void );

/** Reports successful result-transfer completion in task context. */
bool RUN_STATE_MANAGER_RequestResultTransferComplete( void );

/** Discards completed results and returns to ARMED with configuration retained. */
bool RUN_STATE_MANAGER_RequestRepeat( void );

/** Discards completed results and returns to IDLE with configuration cleared. */
bool RUN_STATE_MANAGER_RequestDiscardResults( void );

/**
 * @brief Requests a transition to the fault state from task context.
 *
 * The first non-NONE reason is retained until a successful reset to IDLE.
 * A future ISR integration requires a separate ISR-safe reporting API.
 *
 * @param reason Fault cause reported by the requesting subsystem.
 *
 * @returns true if the request was delivered to the task, otherwise false.
 */
bool RUN_STATE_MANAGER_RequestFault( RunStateFaultReason_T reason );

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

/** @brief Reports whether DUT driver execution has been started. */
bool RUN_STATE_MANAGER_IsExecutionActive( void );

/** @brief Reports whether the RSM-owned execution timer is running. */
bool RUN_STATE_MANAGER_IsExecutionTimerRunning( void );

/** @brief Gets the first fault cause recorded since initialization or reset. */
RunStateFaultReason_T RUN_STATE_MANAGER_GetFaultReason( void );

/** @brief Gets the external event most recently evaluated by the RSM task. */
RunStateRequest_T RUN_STATE_MANAGER_GetLastRequest( void );

/** @brief Gets the validation result for the most recently evaluated event. */
RunStateRequestResult_T RUN_STATE_MANAGER_GetLastRequestResult( void );

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
