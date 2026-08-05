/******************************************************************************
 *  File:       result_buffer.h
 *  Author:     Callum Rafferty
 *  Created:    25-Mar-2026
 *
 *  Description:
 *      Internal interface for the flash-manager result logging buffer.
 *
 *  Notes:
 *      The execution manager accesses this functionality through the public
 *      flash_manager API. Result records are packed as a fixed header followed
 *      by the committed payload bytes.
 ******************************************************************************/

#ifndef RESULT_BUFFER_H
#define RESULT_BUFFER_H

#ifdef __cplusplus
extern "C"
{
#endif

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include "flash_manager.h"
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

typedef enum
{
    /** The record was committed without completing a NAND page. */
    RESULT_BUFFER_RECORD_COMMIT_OK = 0,

    /** The record was committed and completed one NAND page. */
    RESULT_BUFFER_RECORD_COMMIT_PAGE_READY_TO_DRAIN,

    /** The supplied write lease did not match the active record reservation. */
    RESULT_BUFFER_RECORD_COMMIT_INVALID_LEASE,

    /** The actual payload length exceeded the reserved payload capacity. */
    RESULT_BUFFER_RECORD_COMMIT_OVERFLOW
} ResultBufferRecordCommitStatus_T;

/**
 * Temporary ownership of one committed result page being written to NAND.
 *
 * This lease is internal to the flash manager. It keeps the leased page
 * immutable until the NAND write is reported as successful or failed.
 */
typedef struct
{
    /** Page-aligned result data owned by the result buffer. */
    const uint8_t* page_data;

    /** Number of valid logical result bytes in the page. */
    uint32_t valid_length_bytes;

    /** Opaque identifier used to reject stale completions. */
    uint32_t lease_id;
} ResultBufferDrainLease_T;

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

/**
 * @brief Initialises the result buffer using the active NAND geometry.
 *
 * External flash must be initialised before this function is called.
 *
 * @retval true
 *      External flash reported a supported non-zero page size and the result
 *      buffer was reset using that geometry.
 *
 * @retval false
 *      External flash information was unavailable or its page size could not
 *      be represented by the statically allocated result buffers.
 */
bool RESULT_BUFFER_Init( void );

/**
 * @brief Resets all result-buffer ownership and cursor state.
 *
 * Any outstanding lease becomes invalid. Stored bytes are not cleared because
 * they are inaccessible until overwritten and committed again.
 *
 * @note Initialised geometry is preserved across reset.
 */
void RESULT_BUFFER_Reset( void );

/**
 * @brief Reserves contiguous writable storage for one result payload.
 *
 * Reserves capacity for a fixed result header followed by the requested
 * payload. The returned lease provides a contiguous payload pointer regardless
 * of whether the logical record crosses a NAND page boundary.
 *
 * If the record would cross the physical end of the circular result buffer,
 * the returned pointer refers to temporary wrap-scratch storage. Commit later
 * copies that record into the end and beginning of the circular buffer. This
 * adds one extra copy during commit when the circular buffer wraps. Normal
 * reservations write directly into the result ring.
 *
 * Reservation does not advance the committed-data cursor or make any bytes
 * available for NAND writing. The lease must be committed or cancelled before
 * another reservation can succeed.
 *
 * @param[in] requested_payload_capacity_bytes
 *      Maximum number of payload bytes the caller intends to write. This must
 *      be greater than zero, and the header plus payload capacity must fit
 *      within one configured NAND page.
 *
 * @param[out] lease
 *      Destination for the resulting lease. On success, payload points to
 *      writable flash-manager-owned storage and payload_capacity_bytes equals the
 *      requested capacity. On failure, a non-NULL lease is cleared.
 *
 * @retval true
 *      The reservation succeeded and lease contains valid writable storage.
 *
 * @retval false
 *      The buffer is not initialized, lease is NULL, another reservation is
 *      active, the requested capacity is invalid, or insufficient free buffer
 *      capacity exists.
 *
 * @note Only one result write lease may be active at a time.
 * @note The caller must not write more than lease->payload_capacity_bytes bytes.
 * @note The payload pointer remains valid only until commit, cancellation, or
 *       result-buffer reset.
 * @note The driver must not retain the payload pointer after completing its
 *       measurement copy.
 */
bool RESULT_BUFFER_ReserveRecord( uint16_t                        requested_payload_capacity_bytes,
                                  FlashManagerResultWriteLease_T* lease );

/**
 * @brief Cancels the currently active record reservation.
 *
 * Validates that the supplied write lease matches the active record
 * reservation, then invalidates the reservation without committing any result
 * bytes.
 *
 * Cancellation does not modify the producer cursor, pending-NAND byte count,
 * page states, or underlying buffer contents. Any bytes written through the
 * lease become inaccessible and may be overwritten by a future reservation.
 *
 * @param[in] lease
 *      Lease returned by RESULT_BUFFER_ReserveRecord(). The lease ID, payload
 *      pointer, and payload capacity must all match the active record
 *      reservation.
 *
 * @retval true
 *      The lease matched the active record reservation and was cancelled.
 *
 * @retval false
 *      lease is NULL, no record reservation is active, or the supplied lease
 *      does not match the active record reservation.
 *
 * @note A successfully cancelled lease is stale and must not be reused.
 * @note The caller-owned lease structure is not cleared because it is supplied
 *       through a const pointer.
 */
bool RESULT_BUFFER_CancelRecord( const FlashManagerResultWriteLease_T* lease );

/**
 * @brief Commits the active result write lease into the packed result stream.
 *
 * Validates the lease and actual payload length, writes the fixed result
 * header, and advances the circular buffer by the header plus actual payload
 * length. Unused reserved payload capacity does not consume buffer space.
 *
 * For normal reservations, the driver payload is already in its final result
 * ring location and only the header is copied. For a reservation that crossed
 * the physical end of the ring, the header and payload are copied from wrap
 * scratch into the end and beginning of the ring.
 *
 * @param[in] lease
 *      Lease returned by RESULT_BUFFER_ReserveRecord(). Its ID, pointer, and capacity
 *      must match the active record reservation.
 * @param[in] timestamp
 *      Execution timestamp stored in the result header.
 * @param[in] peripheral_type
 *      Logical peripheral type stored in the result header.
 * @param[in] channel
 *      Logical peripheral channel stored in the result header.
 * @param[in] actual_payload_length_bytes
 *      Number of payload bytes written by the execution driver. This may be
 *      smaller than the reserved capacity but must not exceed it.
 *
 * @retval RESULT_BUFFER_RECORD_COMMIT_OK
 *      The record was committed without completing a NAND page.
 * @retval RESULT_BUFFER_RECORD_COMMIT_PAGE_READY_TO_DRAIN
 *      The record was committed and completed one NAND page.
 * @retval RESULT_BUFFER_RECORD_COMMIT_INVALID_LEASE
 *      No reservation was active or the supplied lease did not match it.
 * @retval RESULT_BUFFER_RECORD_COMMIT_OVERFLOW
 *      actual_payload_length_bytes exceeded the active record reservation capacity.
 *      The reservation remains active so it may be cancelled or retried.
 *
 * @note A successful commit invalidates the lease.
 * @note A zero-length payload is stored as a header-only record.
 * @note This function changes RAM ownership only; it does not write NAND or
 *       notify the flash-manager task.
 */
ResultBufferRecordCommitStatus_T
RESULT_BUFFER_CommitRecord( const FlashManagerResultWriteLease_T* lease, uint32_t timestamp,
                            uint8_t peripheral_type, uint8_t channel,
                            uint16_t actual_payload_length_bytes );

/**
 * @brief Acquires the oldest committed page that is ready to write to NAND.
 *
 * The acquired page changes from READY_TO_DRAIN to DRAINING and remains
 * occupied and immutable until RESULT_BUFFER_CompleteDrain() processes this
 * lease.
 * Only the oldest page may be acquired so NAND result byte ordering is
 * preserved.
 *
 * @param[out] lease
 *      Destination for the drain lease. On success, page_data points to
 *      page-aligned result-buffer storage and valid_length_bytes contains the
 *      logical byte count. On failure, a non-NULL lease is cleared.
 *
 * @retval true
 *      The oldest page was ready and is now owned by this lease.
 * @retval false
 *      The buffer is not initialised, lease is NULL, another drain lease is
 *      active, or the oldest page is not ready.
 *
 * @note A full execution-time page has valid_length_bytes equal to the NAND page
 *       size. A shorter length is possible only after finalisation.
 */
bool RESULT_BUFFER_AcquireDrainPage( ResultBufferDrainLease_T* lease );

/**
 * @brief Completes the NAND write associated with an active drain lease.
 *
 * A successful NAND write releases the page and advances the oldest-page
 * cursor. A failed NAND write returns the page to READY_TO_DRAIN without
 * releasing its bytes, allowing the flash-manager task to retry it.
 *
 * @param[in] lease
 *      Lease returned by RESULT_BUFFER_AcquireDrainPage(). Its ID, pointer, and
 *      valid length must match the active drain lease.
 * @param[in] write_succeeded
 *      true after EXTERNAL_FLASH_WriteResultPage() succeeds; false after it
 *      fails.
 *
 * @retval true
 *      The lease was valid and the success or failure transition was applied.
 * @retval false
 *      No drain lease was active or the supplied lease did not match it.
 *
 * @note The supplied lease becomes stale after any successful completion call,
 *       including when write_succeeded is false.
 */
bool RESULT_BUFFER_CompleteDrain( const ResultBufferDrainLease_T* lease, bool write_succeeded );

/**
 * @brief Stops result production and publishes the final partial NAND page.
 *
 * Once finalised, no further record reservations are accepted. If the
 * current page contains committed bytes, it changes from FILLING to
 * READY_TO_DRAIN and may be acquired with its actual valid length.
 * Exact-page-aligned and empty streams require no additional page.
 *
 * @retval true
 *      The buffer is finalised, or had already been finalised.
 * @retval false
 *      The buffer is not initialised or a record reservation is active.
 *
 * @note This function changes RAM ownership only and does not write NAND.
 */
bool RESULT_BUFFER_Finalise( void );

#ifdef __cplusplus
}
#endif

#endif /* RESULT_BUFFER_H */
