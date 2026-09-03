/******************************************************************************
 *  File:       instruction_buffer.h
 *  Author:     Callum Rafferty
 *  Created:    25-Mar-2026
 *
 *  Description:
 *      Internal interface for the Flash Manager instruction buffer.
 *
 *  Notes:
 *      This interface separates two mutually exclusive data flows:
 *
 *      - Retrieval fills empty pages from NAND and serves complete timestamped
 *        instructions to the Execution Manager.
 *      - Upload copies canonical Host Interface chunks into empty pages and
 *        exposes completed pages to the Flash Manager task for NAND writes.
 *
 *      This module owns the instruction RAM and its page-level ownership state.
 *      The calling layer must serialise lifecycle operations. This module does
 *      not access NAND directly or use blocking RTOS primitives. Fill completion
 *      owns one short task critical section that publishes prepared page metadata
 *      atomically to the execution ISR.
 *
 *      Instruction records are packed in the NAND image as a fixed-size
 *      FlashManagerInstructionHeader_T followed immediately by the indicated
 *      payload bytes, with the next header immediately following that payload.
 *      Records may cross NAND page boundaries, but an individual record must
 *      not exceed one NAND page.
 *
 *      Upload preprocessing is responsible for validating this canonical
 *      stream before it reaches NAND, including nondecreasing timestamps,
 *      routing metadata, payload schemas, and the one-page record limit.
 *      Runtime detection of an invalid stored length is a session-ending fault
 *      rather than a recoverable parse error.
 *
 *      The implementation owns three circular NAND-page slots plus one
 *      page-sized mirror of slot zero. The mirror is populated in Flash Manager
 *      task context and makes a record crossing the ring end contiguous to the
 *      execution ISR without an ISR-time payload copy.
 *
 *      Three slots do not guarantee that an uploaded stream is executable. A
 *      single TIM4 invocation can consume several records with the same
 *      timestamp, but the Flash Manager task cannot refill a released slot
 *      until the ISR returns. Admission validation must therefore bound both
 *      instruction bytes and worst-case driver time per timestamp, then compare
 *      sustained consumption with measured NAND refill and scheduling latency.
 ******************************************************************************/

#ifndef INSTRUCTION_BUFFER_H
#define INSTRUCTION_BUFFER_H

#ifdef __cplusplus
extern "C"
{
#endif

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include "flash_manager.h"

#include <stdbool.h>
#include <stdint.h>

/**-----------------------------------------------------------------------------
 *  Public Defines / Macros
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Public Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

/* Instruction retrieval types. */

/**
 * @brief Temporary ownership of a page slot being filled from NAND.
 *
 * The Flash Manager task passes page_data to
 * EXTERNAL_FLASH_ReadInstructionPage(). The slot remains unavailable to the
 * instruction consumer until the read is reported as successfully completed.
 */
typedef struct
{
    /** Writable Flash Manager-owned destination for the NAND DMA read. */
    uint8_t* page_data;

    /** Logical byte offset within the NAND instruction image. */
    uint32_t instruction_offset_bytes;

    /** Number of instruction bytes to read into this page. */
    uint32_t read_length_bytes;

    /** Opaque identifier used to reject stale completion calls. */
    uint32_t lease_id;
} InstructionBufferPageFillLease_T;

/**
 * @brief Result of inspecting the next instruction in the buffered stream.
 */
typedef enum
{
    /** A complete instruction is buffered and available. */
    INSTRUCTION_BUFFER_PEEK_AVAILABLE = 0,

    /** The next instruction is not completely buffered yet. */
    INSTRUCTION_BUFFER_PEEK_NOT_BUFFERED,

    /** Every byte in the configured instruction image has been consumed. */
    INSTRUCTION_BUFFER_PEEK_END_OF_STREAM,

    /** The next header or record length is invalid. */
    INSTRUCTION_BUFFER_PEEK_CORRUPT
} InstructionBufferPeekStatus_T;

/**
 * @brief Result of consuming the current instruction view.
 */
typedef enum
{
    /** The instruction was consumed without requiring a NAND refill. */
    INSTRUCTION_BUFFER_CONSUME_OK = 0,

    /** Consuming the instruction released storage while unread NAND data remains. */
    INSTRUCTION_BUFFER_CONSUME_REFILL_REQUIRED
} InstructionBufferConsumeStatus_T;

/* Instruction upload types. */

/** @brief Result of atomically submitting one host instruction chunk. */
typedef enum
{
    /** The complete chunk was copied and no new complete page was produced. */
    INSTRUCTION_BUFFER_UPLOAD_WRITE_ACCEPTED = 0,

    /** The complete chunk was copied and at least one page became drainable. */
    INSTRUCTION_BUFFER_UPLOAD_WRITE_PAGE_READY,

    /** Insufficient currently available RAM; no bytes were copied. */
    INSTRUCTION_BUFFER_UPLOAD_WRITE_BUSY,

    /** The buffer is not in upload mode. */
    INSTRUCTION_BUFFER_UPLOAD_WRITE_INVALID_STATE,

    /** The supplied pointer or length was invalid. */
    INSTRUCTION_BUFFER_UPLOAD_WRITE_INVALID_ARGUMENT
} InstructionBufferUploadWriteStatus_T;

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

/* Shared state management. */

/**
 * @brief Initialises the instruction buffer from active external-flash geometry.
 *
 * @retval true
 *      External flash reported a supported, non-zero NAND page size and
 *      instruction partition capacity.
 * @retval false
 *      External-flash information was unavailable or its geometry could not be
 *      represented by the statically allocated instruction storage.
 *
 * @note EXTERNAL_FLASH_Init() must complete successfully before this function.
 * @note Reinitialisation invalidates all retrieval views, page-fill leases,
 *       upload page ownership, and buffered bytes.
 */
bool INSTRUCTION_BUFFER_Init( void );

/* Instruction retrieval: lifecycle, NAND fill, and execution serving. */

/**
 * @brief Resets the buffer for sequential retrieval of an instruction image.
 *
 * @param[in] instruction_length_bytes
 *      Total number of canonical instruction bytes committed in NAND.
 *
 * @retval true
 *      The retrieval session was reset and is ready for sequential page fills.
 * @retval false
 *      The buffer was not initialised, an upload owned the shared storage, or
 *      the supplied length exceeded the instruction partition capacity.
 *
 * @note A zero length prepares an empty instruction stream.
 * @note This function invalidates all previous page-fill leases and instruction
 *       views without accessing NAND.
 * @note The Flash Manager must ensure that no NAND DMA operation is still using
 *       instruction storage before resetting the session.
 */
bool INSTRUCTION_BUFFER_PrepareRead( uint32_t instruction_length_bytes );

/**
 * @brief Ends the active instruction read session without implying test completion.
 *
 * Invalidates all page-fill leases and instruction views, releases every page
 * slot, and clears the active image cursors while preserving initialised NAND
 * geometry and the monotonically changing page-fill lease identifier.
 *
 * @note The Flash Manager must stop the execution ISR before calling this from
 *       task context.
 * @note This function does nothing while upload owns the shared page storage.
 */
void INSTRUCTION_BUFFER_EndRead( void );

/**
 * @brief Acquires the next free page slot for a sequential NAND read.
 *
 * @param[out] lease
 *      Destination for the fill lease. Cleared before returning false.
 *
 * @retval true
 *      A slot was reserved and the lease identifies the NAND offset and length.
 * @retval false
 *      No slot was free, no unread bytes remained, another fill lease was
 *      active, the buffer was unavailable, or lease was null.
 *
 * @note Only one page-fill lease may be active at a time.
 * @note The final instruction page may have read_length_bytes smaller than the
 *       configured NAND page size.
 * @note A non-null lease is cleared before any failure is returned.
 */
bool INSTRUCTION_BUFFER_AcquireFillPage( InstructionBufferPageFillLease_T* lease );

/**
 * @brief Completes the NAND read associated with a fill lease.
 *
 * A successful read publishes the page and advances the next NAND offset. A
 * failed read releases the page without advancing, allowing the read to retry.
 *
 * @param[in] lease
 *      Unmodified lease returned by INSTRUCTION_BUFFER_AcquireFillPage().
 * @param[in] nand_read_succeeded
 *      Whether EXTERNAL_FLASH_ReadInstructionPage() succeeded.
 *
 * @retval true
 *      The lease matched and the requested success or failure transition was
 *      applied.
 * @retval false
 *      The lease was null, stale, modified or inconsistent with page state.
 *
 * @note Any successful completion call makes the supplied lease stale, even
 *       when nand_read_succeeded is false.
 * @note Invalid completion leaves the active lease and page ownership unchanged
 *       for fault handling or an explicit session reset.
 * @note Completing a successful slot-zero fill copies its valid bytes into the
 *       internal mirror before publishing the page. This bounded page copy is
 *       performed with interrupts enabled in Flash Manager task context. Only
 *       the subsequent metadata publication uses a critical section.
 * @note Call from Flash Manager task context only.
 */
bool INSTRUCTION_BUFFER_CompleteFillPage( const InstructionBufferPageFillLease_T* lease,
                                          bool nand_read_succeeded );

/**
 * @brief Returns a read-only view of the next complete instruction.
 *
 * The returned view contains the instruction's parsed header and a pointer to
 * its complete payload. Together these fields represent one logical stored
 * instruction.
 *
 * The consumer position is not advanced. Repeated calls before consumption
 * return the same cached view without copying or reparsing the header.
 *
 * @param[out] instruction
 *      Destination for a read-only pointer to the prepared instruction view.
 *      Its value is changed only when INSTRUCTION_BUFFER_PEEK_AVAILABLE is
 *      returned.
 *
 * @return Current buffered instruction status.
 *
 * @note This function performs no NAND access and uses no RTOS primitives.
 * @pre A prepared read session is active and instruction is non-null.
 * @note The returned payload is contiguous even if its stored record crosses a
 *       NAND page boundary or the physical end of the circular page storage.
 * @note Peek copies only the fixed-size header; it never copies the payload.
 * @note Timestamp comparison is deliberately outside this buffer. The
 *       Execution Manager retains a future record, consumes a record due on
 *       the current tick, and treats a past record as an overrun fault.
 * @note The view remains valid until consumed, PrepareRead is called again, or
 *       EndRead closes the retrieval session.
 */
InstructionBufferPeekStatus_T
INSTRUCTION_BUFFER_PeekInstruction( const FlashManagerInstructionView_T** instruction );

/**
 * @brief Consumes the instruction returned by the active successful peek.
 *
 * Advances by the stored header and payload length. If the instruction reaches
 * a page boundary, the cold path also releases every exhausted page slot.
 *
 * @return Consumption status, including whether a refill is required.
 *
 * @pre At least one successful peek must precede exactly one consume call.
 * @note This function performs no NAND access and uses no RTOS primitives.
 */
InstructionBufferConsumeStatus_T INSTRUCTION_BUFFER_ConsumeInstruction( void );

/* Instruction upload: host production, NAND drain, and lifecycle. */

/**
 * @brief Resets instruction RAM ownership for a new streamed upload.
 *
 * @param[in] expected_length_bytes
 *      Total number of canonical instruction bytes expected during the upload.
 *
 * @retval true
 *      Upload mode was prepared successfully.
 * @retval false
 *      The buffer was not initialised, shared storage was still owned, the
 *      expected length was zero, or it exceeded the partition capacity.
 *
 * @note This configures only Flash Manager-owned RAM state and does not access
 *       NAND or use RTOS primitives.
 * @note Preparation is refused while retrieval, another upload, or a NAND fill
 *       owns the shared storage.
 * @note The expected length describes the complete stream, not one host chunk.
 * @note The Flash Manager must ensure no NAND operation or execution consumer
 *       still owns instruction storage before calling this function.
 */
bool INSTRUCTION_BUFFER_PrepareUpload( uint32_t expected_length_bytes );

/**
 * @brief Returns the expected length of the prepared instruction upload.
 *
 * @param[out] expected_length_bytes
 *      Destination for the complete expected stream length.
 *
 * @retval true
 *      Upload mode is prepared and the expected length was returned.
 * @retval false
 *      The output pointer was null or no upload is currently prepared.
 *
 * @note This accessor performs no NAND access and uses no RTOS primitives.
 */
bool INSTRUCTION_BUFFER_GetUploadExpectedLength( uint32_t* expected_length_bytes );

/**
 * @brief Atomically appends one canonical host chunk to upload RAM.
 *
 * @param[in] data   Canonical instruction bytes in stream order.
 * @param[in] length Number of bytes to append; at most one NAND page.
 *
 * @return Upload write status.
 *
 * @note The complete chunk is copied or no state is changed. BUSY therefore
 *       permits the caller to retry the identical data and length.
 * @note A chunk may fill the tail of one page and continue into the next page.
 * @note Chunk boundaries are transport boundaries only. They need not align
 *       with instruction-record or NAND-page boundaries.
 * @note Full pages become immutable and ready for NAND immediately. The final
 *       partial page is published by INSTRUCTION_BUFFER_FinaliseUpload().
 * @note This function performs no NAND access and is not internally synchronised.
 */
InstructionBufferUploadWriteStatus_T INSTRUCTION_BUFFER_WriteUploadBytes( const uint8_t* data,
                                                                          uint32_t       length );

/**
 * @brief Acquires the oldest completed upload page for a NAND write.
 *
 * @param[out] page_data Destination for the immutable page pointer. Set to null
 *        before returning false.
 * @param[out] valid_length_bytes Destination for the number of logical bytes
 *        in the page. Set to zero before returning false.
 *
 * @retval true  The oldest ready page changed to WRITING_TO_NAND.
 * @retval false No page was ready, a NAND write already owned the oldest page,
 *        an output was null, upload mode was unavailable, or accounting was
 *        inconsistent.
 *
 * @note The page remains occupied and immutable until CompleteUploadDrain is
 *       called after the NAND write.
 * @note Only the oldest page is acquired, preserving NAND stream order.
 * @note WRITING_TO_NAND together with the drain cursor is the complete ownership
 *       record; no separate lease is required for the single Flash Manager
 *       consumer.
 * @note The caller may release its mutex during the NAND operation because host
 *       writes cannot modify a WRITING_TO_NAND page.
 */
bool INSTRUCTION_BUFFER_AcquireUploadDrainPage( const uint8_t** page_data,
                                                uint32_t*       valid_length_bytes );

/**
 * @brief Completes the NAND write owning the oldest upload page.
 *
 * @param[in] nand_write_succeeded Whether the external-flash write succeeded.
 *
 * @retval true  The active WRITING_TO_NAND page was completed.
 * @retval false No upload page was being written or accounting was inconsistent.
 *
 * @note Success releases the page and advances persisted-byte accounting.
 *       Failure restores READY_FOR_NAND so the page can be reacquired.
 * @note Only the Flash Manager task that performed acquisition may complete the
 *       operation, and it must do so before attempting another acquisition.
 */
bool INSTRUCTION_BUFFER_CompleteUploadDrain( bool nand_write_succeeded );

/**
 * @brief Ends host production and publishes the final partial page, if present.
 *
 * @retval true  Every expected byte was accepted and upload was finalised.
 * @retval false Upload was unavailable, incomplete, already finalised, actively
 *        draining, or internally inconsistent.
 */
bool INSTRUCTION_BUFFER_FinaliseUpload( void );

/**
 * @brief Reports whether every declared host byte has been accepted.
 *
 * @note This does not imply that the bytes have been persisted to NAND.
 */
bool INSTRUCTION_BUFFER_IsUploadInputComplete( void );

/**
 * @brief Reports whether finalised upload data is completely persisted.
 *
 * This requires matching expected, accepted and persisted lengths and every
 * shared page slot to be empty, including no active NAND write.
 */
bool INSTRUCTION_BUFFER_IsUploadPersisted( void );

/**
 * @brief Releases upload state after external-flash finalisation succeeds.
 *
 * @retval true  The fully persisted upload state was released.
 * @retval false Upload data or page ownership remained outstanding.
 *
 * @note The page-fill lease identifier and external-flash geometry are preserved.
 */
bool INSTRUCTION_BUFFER_EndUpload( void );

#ifdef __cplusplus
}
#endif

#endif /* INSTRUCTION_BUFFER_H */
