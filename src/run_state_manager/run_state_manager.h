/******************************************************************************
 *  File:       run_state_manager.h
 *  Author:     Angus Corr
 *  Created:    25-Mar-2026
 *
 *  Description:
 *      Public interface for coordinating the HIL-RIG runtime lifecycle.
 *
 *  Notes:
 *      The current implementation still contains temporary timer-controller
 *      behavior. It is intended to become an RTOS task that owns system policy
 *      and coordinates the Execution Manager and Flash Manager through their
 *      public APIs.
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
 * 3. Only then call EXECUTION_MANAGER_Start() to enable the execution timer.
 *
 * End-of-run handshake:
 *
 * 1. Call EXECUTION_MANAGER_Stop() first. The execution ISR must be completely
 *    finished, with every result lease committed or cancelled, before
 *    finalisation is requested.
 * 2. Call FLASH_MANAGER_RequestResultFinalisation().
 * 3. Wait asynchronously for FLASH_MANAGER_STATE_RESULTS_READY or
 *    FLASH_MANAGER_STATE_FAULT. RESULTS_READY guarantees that every committed
 *    result byte, including a final partial NAND page, has been drained.
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
 * The future Run State Manager should call Execution Manager timer/frequency
 * APIs rather than duplicating timer constants or defining its own
 * FrequencyMode_T.
 */

/**
 * @brief Starts the Test Scheduler
 *
 */
void RUN_STATE_MANAGER_Start( void );

/**
 * @brief Stops the Test Scheduler
 *
 */
void RUN_STATE_MANAGER_Stop( void );

/**
 * @brief Sets the frequency mode of the test scheduler
 *
 * @param mode - the selected frequency mode
 *
 * Note: currently only supports 100Hz, 1kHz or 10kHz
 *
 */
void RUN_STATE_MANAGER_Set_Frequency_Mode( FrequencyMode_T mode );

/**
 * @brief Test Scheduler Initialization
 *
 * Initialises the test schedular based on the selected frequency mode.
 */
void RUN_STATE_MANAGER_Init( void );

#ifdef __cplusplus
}
#endif

#endif /* RUN_STATE_MANAGER_H */
