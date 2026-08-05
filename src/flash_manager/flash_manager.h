/******************************************************************************
 *  File:       flash_manager.h
 *  Author:     Callum Rafferty
 *  Created:    25-Mar-2026
 *
 *  Description:
 *      Public types and interface for the Flash Manager module.
 *
 *  Notes:
 *      The runtime control API has not been implemented yet. The result write
 *      and read lease types establish ownership across the execution and host
 *      interfaces.
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
    FLASH_MANAGER_STATE_UNINITIALISED = 0,
    FLASH_MANAGER_STATE_IDLE,
    FLASH_MANAGER_STATE_INSTRUCTION_UPLOAD,
    FLASH_MANAGER_STATE_PREPARING_EXECUTION,
    FLASH_MANAGER_STATE_EXECUTING,
    FLASH_MANAGER_STATE_FINALISING_RESULTS,
    FLASH_MANAGER_STATE_TRANSFERRING_RESULTS,
    FLASH_MANAGER_STATE_FAULT

} FlashManagerState_T;

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

/** Temporary host read access to flash-manager-owned result storage. */
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

#ifdef __cplusplus
}
#endif

#endif /* FLASH_MANAGER_H */
