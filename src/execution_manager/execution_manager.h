/******************************************************************************
 *  File:       execution_manager.h
 *  Author:     Angus Corr
 *  Created:    20-Dec-2025
 *
 *  Description:
 *      Public interface for timer-driven test execution.
 *
 *  Notes:
 *      The execution path runs in the execution timer ISR. Integration must
 *      use the Flash Manager's FromISR instruction/result APIs and must not
 *      call NAND, take an RTOS mutex, or use task-context FreeRTOS functions.
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

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

/*
 * Flash Manager integration reference
 * -----------------------------------
 *
 * Before the timer starts, the Run State Manager must have requested Flash
 * Manager preparation and observed FLASH_MANAGER_STATE_EXECUTING.
 * Instructions are supplied in strictly increasing timestamp order, with at
 * most one instruction for each output-bearing tick.
 *
 * At each execution tick, peek the head instruction once:
 *
 * - timestamp > current tick: stop this iteration without consuming it. A
 *   later peek returns the same cached view without reparsing the header.
 * - timestamp == current tick: dispatch every packed operation in order, then
 *   consume the complete instruction exactly once.
 * - timestamp < current tick: report an execution-overrun/infeasibility fault,
 *   stop the session, and do not consume the late instruction.
 *
 * End of the stored instruction stream does not itself end the test. A future
 * measurement may still be required; session completion is determined by the
 * configured execution policy outside the Flash Manager.
 *
 * For each measurement produced inside EXECUTION_MANAGER_Process_From_ISR():
 *
 * 1. Reserve the maximum payload size required by the selected execution
 *    driver with FLASH_MANAGER_ReserveResultRecordFromISR().
 * 2. Pass lease.payload and lease.payload_capacity_bytes to the execution
 *    driver. Peripheral DMA populates driver-owned storage asynchronously, but
 *    the selected driver synchronously copies one stable result into the lease
 *    during this ISR and returns the actual byte count. DMA must never target
 *    the lease directly.
 * 3. Assign the execution timestamp and commit the record with
 *    FLASH_MANAGER_CommitResultRecordFromISR(). The header also identifies the
 *    peripheral type and channel.
 * 4. Cancel the lease with FLASH_MANAGER_CancelResultRecordFromISR() whenever
 *    the measurement cannot be committed. A lease must not survive the ISR.
 *
 * Accumulate one BaseType_t higher_priority_task_woken value across the entire
 * execution sequence. Initialise it to pdFALSE before the first commit and
 * call portYIELD_FROM_ISR() only after every operation for the tick has
 * completed. A requested yield cannot run the Flash Manager task until the ISR
 * returns, so the execution sequence remains uninterrupted by RTOS tasks.
 * The execution timer IRQ priority must remain eligible to call FreeRTOS
 * FromISR APIs. TIM4 is currently configured at priority 5, matching
 * configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY.
 *
 * Reservation failure means the RAM result queue has no safe capacity. The
 * execution policy must report this condition and stop/fault the run; it must
 * not silently discard a required result. Peek underrun/corruption, consume
 * failure, commit errors, and timestamp overrun must likewise be propagated to
 * the Run State Manager through an ISR-safe mechanism that remains to be
 * implemented.
 */

/**
 * @brief Processes scheduler work from interrupt context.
 *
 * Intended to be called directly from an ISR to perform the minimal
 * execution-manager processing required for the current scheduler tick.
 * This API is expected to remain ISR-safe and execute quickly.
 *
 * @note Flash Manager integration must accumulate one BaseType_t wake flag
 *       across all instruction consumes and result commits, then make that
 *       value available to the outer timer IRQ handler. The outermost ISR
 *       performs one portYIELD_FROM_ISR() call after the complete tick.
 * @note The overrun/fault reporting mechanism is not implemented yet.
 */
void EXECUTION_MANAGER_Process_From_ISR( void );

#ifdef __cplusplus
}
#endif

#endif /* EXECUTION_MANAGER_H */
