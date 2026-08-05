/******************************************************************************
 *  File:       flash_manager.h
 *  Author:     Callum Rafferty
 *  Created:    25-Mar-2026
 *
 *  Description:
 *      Placeholder public interface for the Flash Manager module.
 *
 *  Notes:
 *      The runtime flash-manager API has not been implemented yet. The intended
 *      ownership and buffering model is documented in README.md.
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
    UNINITIALISED = 0,
    IDLE,
    INSTRUCTION_UPLOAD,
    PREPARING_EXECUTION,
    EXECUTING,
    FINALISING_RESULTS,
    RESULT_TRANSFER,
    FAULT

} FlashManagerState_T;

typedef struct
{
    /** Execution timestamp assigned before the result is committed. */
    uint32_t timestamp;

    /** Number of payload bytes stored immediately after this header. */
    uint16_t payload_length;

    /** Peripheral family that produced the payload. */
    uint8_t  peripheral_type;

    /** Instance or channel within the selected peripheral family. */
    uint8_t  channel;
} FlashManagerResultHeader_T;

/**
 * Temporary write access to flash-manager-owned result storage.
 *
 * The execution path may write at most payload_capacity bytes through payload,
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
    uint32_t reservation_id;

    /**
     * Maximum number of bytes that may be written through payload.
     */
    uint16_t payload_capacity;
} FlashManagerResultLease_T;

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

#ifdef __cplusplus
}
#endif

#endif /* FLASH_MANAGER_H */
