/******************************************************************************
 *  File:       result_buffer.h
 *  Author:     Callum Rafferty
 *  Created:    25-Mar-2026
 *
 *  Description:
 *      Public interface for the Result Buffer module.
 *
 *  Notes:
 *      This is currently a placeholder; no result-buffer API is exposed.
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
    RESULT_BUFFER_COMMIT_OK = 0,
    RESULT_BUFFER_COMMIT_PAGE_READY,
    RESULT_BUFFER_COMMIT_INVALID_LEASE,
    RESULT_BUFFER_COMMIT_OVERFLOW
} ResultBufferCommitStatus_T;

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

/**
 * @brief Initialises the result buffer using the active NAND geometry.
 *
 * External flash must be initialised before this function is called.
 *
 * @return true when the reported page geometry is supported.
 */
bool RESULT_BUFFER_Init( void );

/**
 * @brief Resets all result-buffer ownership and cursor state.
 *
 * Any outstanding lease becomes invalid. Stored bytes are not cleared because
 * they are inaccessible until overwritten and committed again.
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
 * copies that record into the end and beginning of the circular buffer.
 * This adds one extra copy during commit when the circular buffer wraps. Normal reservations write
 * directly into the result ring.
 *
 * Reservation does not advance the committed-data cursor or make any bytes
 * available for NAND writing. The lease must be committed or cancelled before
 * another reservation can succeed.
 *
 * @param[in] requested_payload_capacity
 *      Maximum number of payload bytes the caller intends to write. This must
 *      be greater than zero, and the header plus payload capacity must fit
 *      within one configured NAND page.
 *
 * @param[out] lease
 *      Destination for the resulting lease. On success, payload points to
 *      writable flash-manager-owned storage and payload_capacity equals the
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
 * @note Only one result lease may be active at a time.
 * @note The caller must not write more than lease->payload_capacity bytes.
 * @note The payload pointer remains valid only until commit, cancellation, or
 *       result-buffer reset.
 * @note The driver must not retain the payload pointer after completing its
 *       measurement copy.
 */
bool RESULT_BUFFER_Reserve( uint16_t requested_payload_capacity, FlashManagerResultLease_T* lease );

/**
 * @brief Cancels the currently active result reservation.
 *
 * Validates that the supplied lease matches the active reservation, then
 * invalidates the reservation without committing any result bytes.
 *
 * Cancellation does not modify the write cursor, occupied-byte count, page
 * states, or underlying buffer contents. Any bytes written through the lease
 * become inaccessible and may be overwritten by a future reservation.
 *
 * @param[in] lease
 *      Lease returned by RESULT_BUFFER_Reserve(). The reservation ID, payload
 *      pointer, and payload capacity must all match the active reservation.
 *
 * @retval true
 *      The lease matched the active reservation and was cancelled.
 *
 * @retval false
 *      lease is NULL, no reservation is active, or the supplied lease does not
 *      match the active reservation.
 *
 * @note A successfully cancelled lease is stale and must not be reused.
 * @note The caller-owned lease structure is not cleared because it is supplied
 *       through a const pointer.
 */
bool RESULT_BUFFER_Cancel( const FlashManagerResultLease_T* lease );

/**
 * @brief Commits a previously reserved result buffer region, making it available for NAND write.
 *
 * @param lease - The lease to commit.
 * @param timestamp - The timestamp to associate with the committed result record.
 * @param peripheral_type - The peripheral type to associate with the committed result record.
 * @param channel - The channel to associate with the committed result record.
 * @return RESULT_BUFFER_COMMIT_OK if the commit was successful and the reserved region is now
 * available for NAND write.
 * @return RESULT_BUFFER_COMMIT_INVALID_LEASE if the lease was invalid or could not be committed.
 * @return RESULT_BUFFER_COMMIT_OVERFLOW if committing the lease would exceed the buffer's capacity.
 */
ResultBufferCommitStatus_T RESULT_BUFFER_Commit( const FlashManagerResultLease_T* lease,
                                                 uint32_t timestamp, uint8_t peripheral_type,
                                                 uint8_t channel, uint16_t actual_payload_length );

#ifdef __cplusplus
}
#endif

#endif /* RESULT_BUFFER_H */
