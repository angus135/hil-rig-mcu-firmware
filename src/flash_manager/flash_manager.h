/******************************************************************************
 *  File:       flash_manager.h
 *  Author:     Callum Rafferty
 *  Created:    25-Mar-2026
 *
 *  Description:
 *      Public lifecycle and execution-facing interface for the Flash Manager.
 *
 *  Notes:
 *      The Flash Manager owns instruction and result RAM. The execution timer
 *      ISR writes results through temporary payload leases and reads prefetched
 *      instructions through immutable views. NAND access remains restricted to
 *      the Flash Manager task and occurs only after the ISR returns.
 *
 *      Result logging/finalisation and instruction preload/refill are
 *      implemented. Result transfer to the host and instruction upload remain
 *      future work.
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

#include "rtos_config.h"

#include <stdbool.h>
#include <stdint.h>

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
 * allow instruction pages to refill before underrun and completed result pages
 * to drain before overflow. Validate it against NAND latency and other tasks.
 */
#define FLASH_MANAGER_TASK_PRIORITY ( 3U )

/**-----------------------------------------------------------------------------
 *  Public Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

/*
 * Implemented execution lifecycle:
 *
 * IDLE
 *   -> RequestExecutionPreparation()
 * PREPARING_EXECUTION
 *   -> Flash Manager task starts the result session and resets result RAM
 *   -> Flash Manager task preloads instruction RAM before EXECUTING
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

    /** Reserved for writing a canonical instruction image to NAND. */
    FLASH_MANAGER_STATE_INSTRUCTION_UPLOAD,

    /** NAND and RAM buffers are being prepared for a new execution session. */
    FLASH_MANAGER_STATE_PREPARING_EXECUTION,

    /**
     * The execution ISR may consume prefetched instructions and reserve,
     * populate, and commit result records.
     */
    FLASH_MANAGER_STATE_EXECUTING,

    /** Production has stopped and remaining result pages are being drained. */
    FLASH_MANAGER_STATE_FINALISING_RESULTS,

    /** All result pages have been drained and the buffer is ready for host transfer. */
    FLASH_MANAGER_STATE_RESULTS_READY,

    /** Reserved for reading stored results and transferring them to the host. */
    FLASH_MANAGER_STATE_TRANSFERRING_RESULTS,

    /** An unrecoverable manager, buffer, or NAND operation has failed. */
    FLASH_MANAGER_STATE_FAULT

} FlashManagerState_T;

/** @brief Result of submitting an asynchronous lifecycle request. */
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

/** @brief Result of committing an ISR-produced result record. */
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
 * @brief Temporary driver write access to Flash Manager-owned result storage.
 *
 * The execution path may write at most payload_capacity_bytes through payload,
 * then must commit or cancel the lease. The complete lease must be returned
 * unchanged so stale or modified ownership can be rejected.
 */
typedef struct
{
    /** Writable payload storage owned by the Flash Manager. */
    uint8_t* payload;

    /** Opaque identifier used to validate commit or cancellation. */
    uint32_t lease_id;

    /** Maximum number of bytes that may be written through payload. */
    uint16_t payload_capacity_bytes;
} FlashManagerResultWriteLease_T;

/**
 * @brief Temporary host read access to Flash Manager-owned result storage.
 *
 * Reserved for the future NAND-to-host result retrieval path.
 */
typedef struct
{
    /** Read-only result bytes owned by the Flash Manager. */
    const uint8_t* result_data;

    /** Number of valid result bytes available. */
    uint32_t valid_length_bytes;

    /** Logical byte offset of this data within the NAND result stream. */
    uint32_t result_offset_bytes;

    /** Opaque identifier used to reject stale releases. */
    uint32_t lease_id;
} FlashManagerResultReadLease_T;

/**
 * @brief Fixed header stored before every canonical instruction payload.
 *
 * The package application layer creates this representation before upload.
 * Stored instructions form a packed [header][payload] stream without padding;
 * the next header immediately follows the preceding payload. Upload processing
 * must validate every record and reject malformed streams before NAND storage.
 * The current storage format uses the MCU structure representation; byte order
 * must be defined explicitly before instruction images become portable.
 */
typedef struct
{
    /** Timestamp at which the instruction becomes due. */
    uint32_t timestamp;

    /** Number of payload bytes following this header. */
    uint16_t payload_length_bytes;

    /** Peripheral family targeted by the instruction. */
    uint8_t peripheral_type;

    /** Peripheral instance or channel targeted by the instruction. */
    uint8_t channel;
} FlashManagerInstructionHeader_T;

/**
 * @brief Read-only view of the next buffered execution instruction.
 *
 * The header and payload together represent one complete logical instruction.
 * The fixed header is copied into this aligned view for safe field access; the
 * variable-length payload remains in Flash Manager-owned buffer storage and is
 * exposed without a copy. An internal mirror keeps records crossing the
 * physical end of circular storage contiguous.
 *
 * The view remains valid until consumed or the instruction buffer is reset.
 * The Execution Manager and peripheral driver must not retain payload after a
 * successful consume operation.
 */
typedef struct
{
    /** Parsed copy of the stored instruction header. */
    FlashManagerInstructionHeader_T header;

    /** Read-only payload belonging to the instruction described by header. */
    const uint8_t* payload;

    /** Opaque identifier used to reject stale consumption attempts. */
    uint32_t view_id;
} FlashManagerInstructionView_T;

/** @brief Availability and validation status for the next instruction view. */
typedef enum
{
    /** A complete instruction is buffered and available. */
    FLASH_MANAGER_INSTRUCTION_AVAILABLE = 0,

    /** More instruction bytes exist but the next record is not fully buffered. */
    FLASH_MANAGER_INSTRUCTION_NOT_BUFFERED,

    /** The complete instruction stream has been consumed. */
    FLASH_MANAGER_INSTRUCTION_END_OF_STREAM,

    /** Instruction retrieval is unavailable in the current manager state. */
    FLASH_MANAGER_INSTRUCTION_INVALID_STATE,

    /** The stored header or record length is invalid. */
    FLASH_MANAGER_INSTRUCTION_CORRUPT,

    /** The supplied output pointer was null. */
    FLASH_MANAGER_INSTRUCTION_INVALID_ARGUMENT
} FlashManagerInstructionReadStatus_T;

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

/**
 * @brief Runs the Flash Manager RTOS task.
 *
 * The task processes lifecycle notifications and performs NAND operations.
 * It prepares and refills instruction storage, drains result storage, and
 * processes execution-session lifecycle requests.
 *
 * @param parameters Optional FreeRTOS task parameter. Currently unused.
 *
 * @note This function does not return.
 * @note FLASH_MANAGER_Init() must complete successfully before this task is
 *       allowed to run.
 */
void FLASH_MANAGER_Task( void* parameters );

/**
 * @brief Initialises Flash Manager synchronisation and implemented buffers.
 *
 * @retval true
 *      Task-context synchronisation and the result buffer were initialised.
 * @retval false
 *      The module was already initialised, mutex creation failed, or result
 *      buffer geometry was invalid.
 *
 * @note EXTERNAL_FLASH_Init() must succeed before this function is called.
 * @note Call once during startup before the scheduler exposes Flash Manager
 *       APIs to other tasks.
 */
bool FLASH_MANAGER_Init( void );

/**
 * @brief Reserves temporary payload storage for one result record.
 *
 * @param[in] payload_capacity_bytes
 *      Maximum payload bytes the execution driver may write before commit.
 * @param[out] lease
 *      Destination for the write lease. It is cleared before any failure is
 *      returned.
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
 * @param[in] lease Lease returned by FLASH_MANAGER_ReserveResultRecordFromISR().
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
 * @param[in] lease Active write lease returned by the reserve function.
 * @param[in] timestamp Execution timestamp stored in the record header.
 * @param[in] peripheral_type Peripheral family stored in the record header.
 * @param[in] channel Instance or channel stored in the record header.
 * @param[in] actual_payload_length_bytes Number of payload bytes actually written.
 * @param[in,out] higher_priority_task_woken Optional FreeRTOS ISR wake flag. When
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
 * @brief Returns a read-only view of the next buffered instruction.
 *
 * The consumer position is not advanced. Repeated calls return the same
 * instruction until it is consumed.
 *
 * @param[out] instruction
 *      Destination for a pointer to the prepared instruction view. Set to null
 *      before returning any status other than
 *      FLASH_MANAGER_INSTRUCTION_AVAILABLE.
 *
 * @return Current instruction-read status.
 *
 * @note Call only from the execution ISR while the manager is EXECUTING.
 * @note This function never blocks, accesses NAND or uses a mutex.
 */
FlashManagerInstructionReadStatus_T
FLASH_MANAGER_PeekNextInstructionFromISR( const FlashManagerInstructionView_T** instruction );

/**
 * @brief Consumes the instruction returned by the current successful peek.
 *
 * @param[in] instruction
 *      Unmodified view returned by FLASH_MANAGER_PeekNextInstructionFromISR().
 * @param[in,out] higher_priority_task_woken
 *      Optional accumulated FreeRTOS ISR wake flag. May be NULL when the caller
 *      does not require notification of a newly ready task.
 *
 * @return true when the matching instruction was consumed; otherwise false.
 *
 * @note Consuming an instruction may release page storage and notify the Flash
 *       Manager task to preload another NAND page.
 * @note The view becomes stale after successful consumption.
 * @note The outer timer ISR may yield only after its entire execution sequence
 *       has finished.
 */
bool FLASH_MANAGER_ConsumeInstructionFromISR( const FlashManagerInstructionView_T* instruction,
                                              BaseType_t* higher_priority_task_woken );

/**
 * @brief Requests asynchronous preparation of a new execution session.
 *
 * The request changes IDLE to PREPARING_EXECUTION and notifies the Flash
 * Manager task. The task starts a new external-flash result session, resets
 * result RAM, preloads every available instruction slot, and enters EXECUTING
 * only after preparation succeeds.
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
 * @param[out] state Destination for the current state.
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
