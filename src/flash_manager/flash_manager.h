/******************************************************************************
 *  File:       flash_manager.h
 *  Author:     Callum Rafferty
 *  Created:    25-Mar-2026
 *
 *  Description:
 *      Public lifecycle types and ISR-facing result-record interface for the
 *      Flash Manager module.
 *
 *  Notes:
 *      The Flash Manager owns result-buffer memory. The execution timer ISR
 *      obtains temporary write leases, while the Flash Manager task drains
 *      completed NAND pages asynchronously after the ISR returns. Result
 *      retrieval and instruction-buffer lifecycle support remain future work.
 ******************************************************************************/

#ifndef FLASH_MANAGER_H
#define FLASH_MANAGER_H

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
#include "rtos_config.h"

/**-----------------------------------------------------------------------------
 *  Public Defines / Macros
 *------------------------------------------------------------------------------
 */

/**
 * @brief Flash Manager task stack allocation, in FreeRTOS stack words.
 */
#define FLASH_MANAGER_TASK_MEMORY ( 512U )

/**
 * @brief Flash Manager task scheduling priority.
 *
 * The execution timer ISR always preempts this task. This priority must still
 * allow completed pages to drain before the result buffer becomes full and
 * should be validated against NAND latency and the other application tasks.
 */
#define FLASH_MANAGER_TASK_PRIORITY ( 3U )

/**-----------------------------------------------------------------------------
 *  Public Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

/*
 * Implemented result lifecycle:
 *
 * IDLE
 *   -> RequestExecutionPreparation()
 * PREPARING_EXECUTION
 *   -> Flash Manager task starts the NAND session and resets the result buffer
 * EXECUTING
 *   -> Run State Manager stops the timer and waits for the ISR to return
 *   -> RequestResultFinalisation()
 * FINALISING_RESULTS
 *   -> Flash Manager task publishes and drains the final partial page
 * RESULTS_READY
 *   -> Future host-transfer request
 * TRANSFERRING_RESULTS
 */
typedef enum
{
    /** Module startup has not completed. */
    FLASH_MANAGER_STATE_UNINITIALISED = 0,

    /** No upload, execution, finalisation, or transfer is active. */
    FLASH_MANAGER_STATE_IDLE,

    /** An instruction package is being written to NAND. */
    FLASH_MANAGER_STATE_INSTRUCTION_UPLOAD,

    /** NAND and RAM buffers are being prepared for a new execution session. */
    FLASH_MANAGER_STATE_PREPARING_EXECUTION,

    /**
     * The execution ISR may consume instructions and reserve, populate, and
     * commit result records.
     */
    FLASH_MANAGER_STATE_EXECUTING,

    /** Production has stopped and remaining result pages are being drained. */
    FLASH_MANAGER_STATE_FINALISING_RESULTS,

    /** All result pages have been drained and the buffer is ready for host transfer. */
    FLASH_MANAGER_STATE_RESULTS_READY,

    /** Stored results are being read and transferred to the host. */
    FLASH_MANAGER_STATE_TRANSFERRING_RESULTS,

    /** An unrecoverable manager, buffer, or NAND operation has failed. */
    FLASH_MANAGER_STATE_FAULT

} FlashManagerState_T;

typedef enum
{
    /** The asynchronous lifecycle request was accepted. */
    FLASH_MANAGER_REQUEST_OK = 0,

    /** The request is not permitted from the current lifecycle state. */
    FLASH_MANAGER_REQUEST_INVALID_STATE,

    /** FLASH_MANAGER_Init() has not completed successfully. */
    FLASH_MANAGER_REQUEST_NOT_INITIALISED,

    /** The Flash Manager task has not registered its notification handle. */
    FLASH_MANAGER_REQUEST_TASK_NOT_READY,

    /** The task notification failed and the manager entered FAULT. */
    FLASH_MANAGER_REQUEST_NOTIFY_FAILED
} FlashManagerRequestStatus_T;

typedef enum
{
    /** The record was committed and any required drain was signalled. */
    FLASH_MANAGER_RESULT_COMMIT_OK = 0,

    /** Result records are not accepted in the current lifecycle state. */
    FLASH_MANAGER_RESULT_COMMIT_INVALID_STATE,

    /** The supplied lease is null, modified, inactive, or stale. */
    FLASH_MANAGER_RESULT_COMMIT_INVALID_LEASE,

    /** The actual payload length exceeds the reserved capacity. */
    FLASH_MANAGER_RESULT_COMMIT_OVERFLOW,

    /** The record could not be safely scheduled for persistence. */
    FLASH_MANAGER_RESULT_COMMIT_INTERNAL_ERROR
} FlashManagerResultCommitStatus_T;

/**
 * @brief Fixed header stored immediately before every packed result payload.
 *
 * The current on-NAND format stores the MCU representation of this structure.
 * The result-buffer implementation asserts that its size is exactly eight
 * bytes. A future portable storage format must define byte order explicitly.
 */
typedef struct
{
    /** Execution timestamp assigned before the result is committed. */
    uint32_t timestamp;

    /** Number of payload bytes stored immediately after this header. */
    uint16_t payload_length_bytes;

    /** Peripheral family that produced the payload. */
    uint8_t peripheral_type;

    /** Instance or channel within the selected peripheral family. */
    uint8_t channel;
} FlashManagerResultHeader_T;

/**
 * Temporary driver write access to flash-manager-owned result storage.
 *
 * The execution path may write at most payload_capacity_bytes bytes through payload,
 * then must commit or cancel the lease. The complete lease must be returned
 * unchanged so the result buffer can reject stale or modified leases.
 */
typedef struct
{
    /**
     * Writable storage owned by the flash manager.
     */
    uint8_t* payload;

    /**
     * Opaque ID used to validate commit or cancellation and reject stale leases.
     */
    uint32_t lease_id;

    /**
     * Maximum number of bytes that may be written through payload.
     */
    uint16_t payload_capacity_bytes;
} FlashManagerResultWriteLease_T;

/**
 * @brief Temporary host read access to flash-manager-owned result storage.
 *
 * Reserved for the future NAND-to-host result retrieval path.
 */
typedef struct
{
    /**
     * Read-only result bytes owned by the flash manager.
     */
    const uint8_t* result_data;

    /**
     * Number of valid result bytes available.
     */
    uint32_t valid_length_bytes;

    /**
     * Logical byte offset of this data within the NAND result stream.
     */
    uint32_t result_offset_bytes;

    /**
     * Opaque identifier used to reject stale releases.
     */
    uint32_t lease_id;
} FlashManagerResultReadLease_T;

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

/**
 * @brief Runs the Flash Manager RTOS task.
 *
 * The task processes asynchronous flash operations such as draining completed
 * result pages to NAND and finalising result storage.
 *
 * @param parameters Optional FreeRTOS task parameter. Currently unused.
 *
 * @note This function does not return.
 * @note FLASH_MANAGER_Init() must complete successfully before this task is
 *       allowed to run.
 */
void FLASH_MANAGER_Task( void* parameters );

/**
 * @brief Initialises flash-manager synchronization and internal buffers.
 *
 * @return true on success; otherwise false.
 *
 * @note EXTERNAL_FLASH_Init() must succeed before this function is called.
 * @note Call once during startup before the scheduler exposes flash-manager
 *       APIs to other tasks.
 */
bool FLASH_MANAGER_Init( void );

/**
 * @brief Reserves temporary payload storage for one result record.
 *
 * @param payload_capacity_bytes Maximum payload bytes the execution driver may
 *                               write before commit.
 * @param lease Destination for the write lease. It is cleared before any
 *              failure is returned.
 *
 * @return true when storage was reserved; otherwise false.
 *
 * @note Call only from the execution timer ISR while the manager is EXECUTING.
 * @note This function never blocks and does not access NAND.
 * @note Return the complete lease unchanged to commit or cancel it.
 */
bool FLASH_MANAGER_ReserveResultRecordFromISR( uint16_t payload_capacity_bytes,
                                               FlashManagerResultWriteLease_T* lease );

/**
 * @brief Cancels the active result-record reservation.
 *
 * @param lease Lease returned by FLASH_MANAGER_ReserveResultRecordFromISR().
 *
 * @return true when the active lease was cancelled; otherwise false.
 *
 * @note Call only from the execution timer ISR while the manager is EXECUTING.
 * @note Cancellation does not consume packed-buffer capacity. Bytes already
 *       written through the lease become invalid and may be overwritten.
 */
bool FLASH_MANAGER_CancelResultRecordFromISR( const FlashManagerResultWriteLease_T* lease );

/**
 * @brief Commits one result record and signals the drain task when necessary.
 *
 * The fixed header and actual payload bytes become part of the packed result
 * stream. Unused reserved capacity is reclaimed. If the commit completes a
 * NAND page, the Flash Manager task is notified with xTaskNotifyFromISR().
 *
 * @param lease Active write lease returned by the reserve function.
 * @param timestamp Execution timestamp stored in the record header.
 * @param peripheral_type Peripheral family stored in the record header.
 * @param channel Instance or channel stored in the record header.
 * @param actual_payload_length_bytes Number of payload bytes actually written.
 * @param higher_priority_task_woken Optional FreeRTOS ISR wake flag. When
 *        supplied, the outer ISR must initialise it to pdFALSE, preserve it
 *        across all ISR operations, and pass it to portYIELD_FROM_ISR() only
 *        after the complete execution sequence has finished.
 *
 * @return Result commit status.
 *
 * @note Call only from the execution timer ISR while the manager is EXECUTING.
 * @note The function never performs NAND access or switches task context while
 *       the ISR is running. A requested yield occurs only after ISR return.
 * @note If drain notification fails, the record remains committed in RAM and
 *       the Flash Manager enters FAULT.
 */
FlashManagerResultCommitStatus_T FLASH_MANAGER_CommitResultRecordFromISR(
    const FlashManagerResultWriteLease_T* lease, uint32_t timestamp, uint8_t peripheral_type,
    uint8_t channel, uint16_t actual_payload_length_bytes, BaseType_t* higher_priority_task_woken );

/**
 * @brief Requests asynchronous preparation of a new execution result session.
 *
 * The request changes IDLE to PREPARING_EXECUTION and notifies the Flash
 * Manager task. That task starts a new external-flash result session, resets
 * the RAM result buffer, and enters EXECUTING on success or FAULT on failure.
 *
 * @return Request acceptance status.
 *
 * @note Call from task context only.
 * @note The Run State Manager must wait until FLASH_MANAGER_GetState() reports
 *       EXECUTING before starting the execution timer.
 */
FlashManagerRequestStatus_T FLASH_MANAGER_RequestExecutionPreparation( void );

/**
 * @brief Requests asynchronous publication and draining of final results.
 *
 * The request changes EXECUTING to FINALISING_RESULTS and notifies the Flash
 * Manager task. The task publishes any final partial page, drains all remaining
 * pages to NAND, and enters RESULTS_READY on success or FAULT on failure.
 *
 * @return Request acceptance status.
 *
 * @note Call from task context only.
 * @note Before calling, the Run State Manager must stop the execution timer and
 *       ensure the active execution ISR has returned. No result write lease may
 *       remain active.
 */
FlashManagerRequestStatus_T FLASH_MANAGER_RequestResultFinalisation( void );

/**
 * @brief Reads the current Flash Manager lifecycle state.
 *
 * @param state Destination for the current state.
 *
 * @return true when the state was read; false for a null destination or when
 *         the manager synchronization object is unavailable.
 *
 * @note Call from task context only. This function may block on the manager
 *       mutex and must never be called from an ISR.
 */
bool FLASH_MANAGER_GetState( FlashManagerState_T* state );

#ifdef __cplusplus
}
#endif

#endif /* FLASH_MANAGER_H */
