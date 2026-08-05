/******************************************************************************
 *  File:       execution_manager.h
 *  Author:     Angus Corr
 *  Created:    20-Dec-2025
 *
 *  Description:
 *      Public interface for timer-driven test execution.
 *
 *  Notes:
 *      The execution path runs in the execution timer ISR. Future integration
 *      must use the Flash Manager's FromISR result-record API and must not call
 *      NAND, take an RTOS mutex, or use task-context FreeRTOS functions.
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
 *
 * For each measurement produced inside EXECUTION_MANAGER_Process_From_ISR():
 *
 * 1. Reserve the maximum payload size required by the selected execution
 *    driver with FLASH_MANAGER_ReserveResultRecordFromISR().
 * 2. Pass lease.payload and lease.payload_capacity_bytes to the execution
 *    driver. The driver copies its result structure directly into that memory
 *    and returns the actual byte count.
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
 * future execution policy must report this condition and stop or fault the run;
 * it must not silently discard a required result. Commit errors must likewise
 * be propagated to the Run State Manager.
 */

/**
 * @brief Starts the Test Scheduler
 *
 */
void EXECUTION_MANAGER_Start( void );

/**
 * @brief Stops the Test Scheduler
 *
 */
void EXECUTION_MANAGER_Stop( void );

/**
 * @brief Sets the frequency mode of the test scheduler
 *
 * @param mode - the selected frequency mode
 *
 * Note: currently only supports 100Hz, 1kHz or 10kHz
 *
 */
void EXECUTION_MANAGER_Set_Frequency_Mode( FrequencyMode_T mode );

/**
 * @brief Test Scheduler Initialization
 *
 * Initialises the test schedular based on the selected frequency mode.
 */
void EXECUTION_MANAGER_Init( void );

/**
 * @brief Processes scheduler work from interrupt context.
 *
 * Intended to be called directly from an ISR to perform the minimal
 * execution-manager processing required for the current scheduler tick.
 * This API is expected to remain ISR-safe and execute quickly.
 *
 * @note Future Flash Manager integration will either add a
 *       BaseType_t* higher_priority_task_woken parameter or return equivalent
 *       wake information to the outer timer IRQ handler. The outermost ISR
 *       performs the single portYIELD_FROM_ISR() call after processing.
 */
void EXECUTION_MANAGER_Process_From_ISR( void );

#ifdef __cplusplus
}
#endif

#endif /* EXECUTION_MANAGER_H */
