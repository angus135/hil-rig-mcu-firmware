/******************************************************************************
 *  File:       instruction_buffer.h
 *  Author:     Callum Rafferty
 *  Created:    25-Mar-2026
 *
 *  Description:
 *      Internal interface for the Flash Manager instruction retrieval buffer.
 *
 *  Notes:
 *      The Flash Manager task fills page slots from NAND. The Execution Manager
 *      consumes complete timestamped instructions through the public interface
 *      declared in flash_manager.h.
 *
 *      This module owns the instruction RAM and its page-level ownership state.
 *      The Flash Manager must serialise state-changing calls; this module does
 *      not contain RTOS synchronisation primitives or access NAND directly.
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

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

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
 * @note Reinitialisation invalidates all existing page-fill leases and future
 *       instruction views.
 */
bool INSTRUCTION_BUFFER_Init( void );

/**
 * @brief Resets the buffer for sequential retrieval of an instruction image.
 *
 * @param[in] instruction_length_bytes
 *      Total number of canonical instruction bytes committed in NAND.
 *
 * @retval true
 *      The retrieval session was reset and is ready for sequential page fills.
 * @retval false
 *      The buffer was not initialised or the supplied length exceeded the
 *      usable instruction partition capacity.
 *
 * @note A zero length prepares an empty instruction stream.
 * @note This function invalidates all previous page-fill leases and instruction
 *       views without accessing NAND.
 */
bool INSTRUCTION_BUFFER_PrepareRead( uint32_t instruction_length_bytes );

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
 */
bool INSTRUCTION_BUFFER_CompleteFillPage( const InstructionBufferPageFillLease_T* lease,
                                          bool nand_read_succeeded );

#ifdef __cplusplus
}
#endif

#endif /* INSTRUCTION_BUFFER_H */
