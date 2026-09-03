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
 *      Result logging/finalisation, instruction preload/refill, streamed
 *      instruction upload, and copied result retrieval are implemented. Host
 *      task APIs never retain caller buffers after returning.
 *
 *      The fixed three-page instruction and result buffers provide transport
 *      storage, not execution admission control. Before TIM4 starts, the
 *      feasibility analyser must prove that each timestamp's instruction and
 *      result burst fits in the available RAM and execution budget, and that
 *      sustained consumption/production is supportable by measured NAND and
 *      scheduler worst cases. Increasing buffer depth only increases burst
 *      tolerance; it cannot make an unsustainable stream feasible.
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
 * Keep this above non-critical application tasks so instruction refill and
 * result drain work cannot be delayed by console, host-interface or background
 * processing. Interrupt priority is configured independently by the NVIC; the
 * execution timer and enabled peripheral ISRs can still preempt this task.
 *
 * @todo Review and document the priority of every application task once the
 *       complete runtime task set is integrated.
 */
#define FLASH_MANAGER_TASK_PRIORITY ( 4U )

/**-----------------------------------------------------------------------------
 *  Public Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

/* Lifecycle control. */

/*
 * Implemented instruction-upload, execution, and result-transfer lifecycles:
 *
 * IDLE
 *   -> RequestInstructionUploadStart()
 * PREPARING_INSTRUCTION_UPLOAD
 *   -> Flash Manager task prepares instruction storage
 * INSTRUCTION_UPLOAD
 *   -> Host Interface submits canonical instruction chunks
 *   -> RequestInstructionUploadFinish()
 * FINALISING_INSTRUCTION_UPLOAD
 *   -> Flash Manager task commits the final partial page
 *   -> IDLE
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
 *   -> RequestResultTransferStart()
 * TRANSFERRING_RESULTS
 *   -> Host Interface calls ReadResultBytes() until END_OF_STREAM
 *   -> FinishResultTransfer()
 * IDLE
 */
typedef enum
{
    /** Module startup has not completed. */
    FLASH_MANAGER_STATE_UNINITIALISED = 0,

    /** No upload, execution, finalisation, or transfer is active. */
    FLASH_MANAGER_STATE_IDLE,

    /** Instruction storage is being prepared by the Flash Manager task. */
    FLASH_MANAGER_STATE_PREPARING_INSTRUCTION_UPLOAD,

    /** Canonical instruction chunks may be submitted by the Host Interface. */
    FLASH_MANAGER_STATE_INSTRUCTION_UPLOAD,

    /** The final partial instruction page is being committed and the upload closed. */
    FLASH_MANAGER_STATE_FINALISING_INSTRUCTION_UPLOAD,

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

    /** Stored results are being prefetched and copied to the Host Interface. */
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

/* Execution result logging. */

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
 *
 * Peripheral DMA remains confined to driver-owned storage. A selected driver
 * synchronously copies a stable measurement into payload from the execution
 * ISR; neither the driver nor DMA may retain this pointer after commit,
 * cancellation, or ISR return.
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

/* Execution instruction serving. */

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
} FlashManagerInstructionView_T;

/** @brief Availability and validation status for the next instruction view. */
typedef enum
{
    /** A complete instruction is buffered and available. */
    FLASH_MANAGER_INSTRUCTION_AVAILABLE = 0,

    /**
     * More instruction bytes exist but the next record is not fully buffered.
     * This is an execution underrun and latches FLASH_MANAGER_STATE_FAULT.
     */
    FLASH_MANAGER_INSTRUCTION_NOT_BUFFERED,

    /**
     * The stored instruction stream is exhausted. This does not end the test or
     * change FLASH_MANAGER_STATE_EXECUTING.
     */
    FLASH_MANAGER_INSTRUCTION_END_OF_STREAM,

    /** The stored record is invalid and FLASH_MANAGER_STATE_FAULT was latched. */
    FLASH_MANAGER_INSTRUCTION_CORRUPT
} FlashManagerInstructionReadStatus_T;

/* Host Interface instruction upload. */

/** @brief Result of submitting an asynchronous instruction-upload operation. */
typedef enum
{
    /** The operation was accepted for asynchronous processing. */
    FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_ACCEPTED = 0,

    /** The operation is not permitted from the current lifecycle state. */
    FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_INVALID_STATE,

    /** A pointer, chunk length, or expected upload length was invalid. */
    FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_INVALID_ARGUMENT,

    /** RAM cannot currently accept the complete operation; retry unchanged. */
    FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_BUSY,

    /** FLASH_MANAGER_Init() has not completed successfully. */
    FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_NOT_INITIALISED,

    /** The Flash Manager task has not registered its notification handle. */
    FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_TASK_NOT_READY,

    /** Task notification failed and the manager entered FAULT. */
    FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_NOTIFY_FAILED
} FlashManagerInstructionUploadRequestStatus_T;

/* Host Interface result retrieval. */

/** @brief Result of a Host Interface result-transfer operation. */
typedef enum
{
    /** The operation completed successfully. */
    FLASH_MANAGER_RESULT_TRANSFER_OK = 0,

    /** The operation is not permitted in the current lifecycle state. */
    FLASH_MANAGER_RESULT_TRANSFER_INVALID_STATE,

    /** A destination pointer or requested capacity was invalid. */
    FLASH_MANAGER_RESULT_TRANSFER_INVALID_ARGUMENT,

    /** No bytes are currently buffered; retry after the Flash Manager task runs. */
    FLASH_MANAGER_RESULT_TRANSFER_BUSY,

    /** Every stored result byte has been returned to the Host Interface. */
    FLASH_MANAGER_RESULT_TRANSFER_END_OF_STREAM,

    /** FLASH_MANAGER_Init() has not completed successfully. */
    FLASH_MANAGER_RESULT_TRANSFER_NOT_INITIALISED,

    /** The Flash Manager task has not registered its notification handle. */
    FLASH_MANAGER_RESULT_TRANSFER_TASK_NOT_READY,

    /** Task notification failed and the manager entered FAULT. */
    FLASH_MANAGER_RESULT_TRANSFER_NOTIFY_FAILED,

    /** Result-buffer or external-flash state was inconsistent and FAULT was entered. */
    FLASH_MANAGER_RESULT_TRANSFER_INTERNAL_ERROR

} FlashManagerResultTransferStatus_T;

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

/* Task entry, module initialisation, and state inspection. */

/**
 * @brief Runs the Flash Manager RTOS task.
 *
 * The task processes lifecycle notifications and performs NAND operations.
 * It prepares, fills, and drains instruction storage; drains and refills result
 * storage; and processes execution, upload, and result-transfer requests.
 *
 * @param parameters Optional FreeRTOS task parameter. Currently unused.
 *
 * @note This function does not return.
 * @note FLASH_MANAGER_Init() must complete successfully before this task is
 *       allowed to run.
 */
void FLASH_MANAGER_Task( void* parameters );

/**
 * @brief Initialises Flash Manager synchronisation and both owned buffers.
 *
 * @retval true
 *      Task-context synchronisation and both owned buffers were initialised.
 * @retval false
 *      The module was already initialised, mutex creation failed, or either
 *      owned buffer rejected the external-flash geometry.
 *
 * @note EXTERNAL_FLASH_Init() must succeed before this function is called.
 * @note Call once during startup before the scheduler exposes Flash Manager
 *       APIs to other tasks.
 */
bool FLASH_MANAGER_Init( void );

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

/* Execution ISR instruction serving and result logging. */

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
 * @warning A record crossing the physical end of the result ring currently
 *          incurs a payload-length-dependent wrap copy. Target timing tests
 *          must cover this case until records are made page-contiguous.
 * @note If drain notification fails, the record remains committed in RAM and
 *       the Flash Manager enters FAULT.
 */
FlashManagerResultCommitStatus_T FLASH_MANAGER_CommitResultRecordFromISR(
    const FlashManagerResultWriteLease_T* lease, uint32_t timestamp, uint8_t peripheral_type,
    uint8_t channel, uint16_t actual_payload_length_bytes, BaseType_t* higher_priority_task_woken );

/**
 * @brief Returns a read-only view of the next buffered instruction.
 *
 * The consumer position is not advanced. Repeated calls before consumption
 * return the same cached view, allowing a future-timestamped instruction to be
 * inspected again on a later execution tick without reparsing it.
 *
 * @param[out] instruction
 *      Destination for a pointer to the prepared instruction view. Its value is
 *      changed only when FLASH_MANAGER_INSTRUCTION_AVAILABLE is returned.
 *
 * @return Current instruction-read status.
 *
 * @pre Call only from the execution ISR while the manager is EXECUTING.
 * @pre instruction must be non-null.
 * @note This function never blocks, accesses NAND or uses a mutex.
 * @note END_OF_STREAM describes instruction storage only. Measurements and
 *       result logging may continue until another subsystem ends the test.
 * @note Timestamp policy belongs to the Execution Manager: a future timestamp
 *       retains this cached view, an equal timestamp is executed and consumed,
 *       and a past timestamp is an execution-overrun fault and is not consumed.
 */
FlashManagerInstructionReadStatus_T
FLASH_MANAGER_PeekNextInstructionFromISR( const FlashManagerInstructionView_T** instruction );

/**
 * @brief Consumes the instruction returned by the current successful peek.
 *
 * @param[in,out] higher_priority_task_woken
 *      Optional accumulated FreeRTOS ISR wake flag. May be NULL when the caller
 *      does not require notification of a newly ready task.
 *
 * @return true when the current instruction was consumed; otherwise false.
 *
 * @pre At least one successful peek must precede exactly one consume call.
 * @pre Call only from the execution ISR while the manager is EXECUTING.
 * @note Consuming an instruction may release page storage and notify the Flash
 *       Manager task to preload another NAND page.
 * @note The view becomes stale after successful consumption.
 * @note The outer timer ISR may yield only after its entire execution sequence
 *       has finished.
 */
bool FLASH_MANAGER_ConsumeInstructionFromISR( BaseType_t* higher_priority_task_woken );

/* Host Interface instruction upload. */

/**
 * @brief Requests preparation of NAND storage for a canonical instruction image.
 *
 * @param[in] expected_length_bytes Total canonical instruction bytes that the
 *        Host Interface will submit during this upload.
 *
 * @return Instruction-upload request status.
 *
 * @note This request is accepted only from FLASH_MANAGER_STATE_IDLE.
 * @note Acceptance changes the state to
 *       FLASH_MANAGER_STATE_PREPARING_INSTRUCTION_UPLOAD and notifies the Flash
 *       Manager task. The caller must wait for
 *       FLASH_MANAGER_STATE_INSTRUCTION_UPLOAD before submitting data.
 * @note This function is task-context only and does not access NAND directly.
 */
FlashManagerInstructionUploadRequestStatus_T
FLASH_MANAGER_RequestInstructionUploadStart( uint32_t expected_length_bytes );

/**
 * @brief Copies one canonical instruction-stream chunk for asynchronous persistence.
 *
 * @param[in] data   Canonical instruction bytes to append in stream order.
 * @param[in] length Number of bytes supplied through data.
 *
 * @return Instruction-upload request status.
 *
 * @note This operation is accepted only in
 *       FLASH_MANAGER_STATE_INSTRUCTION_UPLOAD.
 * @note Accepted bytes are copied into Flash Manager-owned storage before this
 *       function returns, so the caller may immediately reuse its source
 *       buffer.
 * @note Each call is all-or-nothing and may contain at most one NAND page. If
 *       the three-page upload ring cannot accept the complete chunk, this
 *       function returns FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_BUSY and the
 *       caller may retry the identical pointer contents and length.
 * @note Completing a page asynchronously wakes the Flash Manager task. NAND
 *       persistence proceeds concurrently with later host submissions into
 *       other available ring pages.
 * @note The Host Interface application layer must supply the canonical packed
 *       [header][payload] instruction representation with nondecreasing
 *       timestamps, valid routing metadata and payload schemas, and records no
 *       larger than one NAND page. The Flash Manager preserves the byte stream
 *       but does not perform semantic validation.
 * @note This function is task-context only and does not access NAND directly.
 */
FlashManagerInstructionUploadRequestStatus_T
FLASH_MANAGER_SubmitInstructionUploadBytes( const uint8_t* data, uint32_t length );

/**
 * @brief Requests completion of the active instruction upload.
 *
 * @return Instruction-upload request status.
 *
 * @note This request is accepted only in
 *       FLASH_MANAGER_STATE_INSTRUCTION_UPLOAD after every expected byte has
 *       been accepted. A currently active NAND page write returns BUSY so the
 *       caller can retry this request unchanged.
 * @note Acceptance changes the state to
 *       FLASH_MANAGER_STATE_FINALISING_INSTRUCTION_UPLOAD and notifies the
 *       Flash Manager task. Successful finalisation returns the manager to
 *       FLASH_MANAGER_STATE_IDLE; storage failure enters
 *       FLASH_MANAGER_STATE_FAULT.
 * @note This function is task-context only and does not access NAND directly.
 */
FlashManagerInstructionUploadRequestStatus_T FLASH_MANAGER_RequestInstructionUploadFinish( void );

/* Run State Manager lifecycle requests. */

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
 * pages to NAND, closes instruction retrieval regardless of stream position,
 * and enters RESULTS_READY on success or FAULT on failure.
 *
 * @return Request acceptance status.
 *
 * @note Call from task context only.
 * @note Before calling, the Run State Manager must stop the execution timer and
 *       ensure the active execution ISR has returned. No result write lease may
 *       remain active.
 */
FlashManagerRequestStatus_T FLASH_MANAGER_RequestResultFinalisation( void );

/* Host Interface result retrieval. */

/**
 * @brief Starts asynchronous retrieval of the completed result stream.
 *
 * The request changes RESULTS_READY to TRANSFERRING_RESULTS and notifies the
 * Flash Manager task to begin loading result pages from NAND.
 *
 * @return Result-transfer status.
 *
 * @note Call from Host Interface task context only.
 * @note The caller may begin calling FLASH_MANAGER_ReadResultBytes() after this
 *       request succeeds. BUSY is expected until the first NAND page is loaded.
 */
FlashManagerResultTransferStatus_T FLASH_MANAGER_RequestResultTransferStart( void );

/**
 * @brief Copies the next available stored result bytes into caller-owned memory.
 *
 * @param[out] destination
 *      Host Interface-owned destination buffer.
 * @param[in] destination_capacity_bytes
 *      Maximum number of bytes that may be copied into destination.
 * @param[out] bytes_read
 *      Number of bytes copied. Cleared before any failure is returned.
 *
 * @retval FLASH_MANAGER_RESULT_TRANSFER_OK
 *      One or more bytes were copied.
 * @retval FLASH_MANAGER_RESULT_TRANSFER_BUSY
 *      Unread results remain in NAND, but none are currently buffered.
 * @retval FLASH_MANAGER_RESULT_TRANSFER_END_OF_STREAM
 *      Every stored result byte has already been returned.
 *
 * @note Call from Host Interface task context only.
 * @note The returned bytes form part of the raw packed
 *       [result header][payload] stream. Calls and NAND pages are not aligned
 *       with result-record boundaries.
 * @note The bytes are copied before this function returns. The caller therefore
 *       owns destination and may retain or transmit it asynchronously.
 */
FlashManagerResultTransferStatus_T
FLASH_MANAGER_ReadResultBytes( uint8_t* destination, uint32_t destination_capacity_bytes,
                               uint32_t* bytes_read );

/**
 * @brief Completes a fully consumed result transfer.
 *
 * @return Result-transfer status.
 *
 * @note This succeeds only after every stored result byte has been returned.
 *       FLASH_MANAGER_ReadResultBytes() reports END_OF_STREAM once this
 *       condition has been reached.
 * @note Successful completion changes TRANSFERRING_RESULTS to IDLE, allowing a
 *       subsequent instruction upload or execution session.
 * @note Call from Host Interface task context only.
 */
FlashManagerResultTransferStatus_T FLASH_MANAGER_FinishResultTransfer( void );

#ifdef __cplusplus
}
#endif

#endif /* FLASH_MANAGER_H */
