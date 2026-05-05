/******************************************************************************
 *  File:       external_flash.h
 *  Author:     Callum Rafferty
 *  Created:    25-Mar-2026
 *
 *  Description:
 *      Public interface for the external flash storage service.
 *
 *  Notes:
 *      This module owns storage policy above the physical NAND driver. It
 *      stores execution results as opaque bytes, reads instruction bytes from a
 *      fixed instruction partition, and reads stored result bytes back for host
 *      transfer.
 *
 *      Intended HIL-RIG use:
 *      - test_package_recieve receives a test package from the host and is
 *        responsible for arranging/programming the instruction partition. This
 *        first driver version only reads instructions; instruction write support
 *        should be added when the package-upload path is implemented.
 *      - buffer_manager owns the RAM instruction/result buffers. It supplies
 *        opaque result byte spans to EXTERNAL_FLASH_WriteResults and receives
 *        opaque instruction byte spans from EXTERNAL_FLASH_ReadInstructions.
 *      - execution_manager consumes instruction bytes and produces result bytes
 *        through buffer_manager-owned buffers. It must not call external_flash.
 *      - result_transfer_manager reads committed result bytes with
 *        EXTERNAL_FLASH_ReadResults and passes them to the host interface.
 *
 *      Design decisions:
 *      - The instruction and result regions are fixed compile-time partitions.
 *      - Results are volatile for now. They are not recovered after reset.
 *      - Result bytes are stored exactly as supplied; this layer does not parse
 *        result records or add per-page metadata yet.
 *      - Bad blocks are scanned at init and skipped by logical byte addressing.
 *      - The result partition is erased at the start of a session to keep
 *        execution-time writes deterministic.
 ******************************************************************************/

#ifndef EXTERNAL_FLASH_H
#define EXTERNAL_FLASH_H

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

/** First physical NAND block reserved for instruction storage. */
#define EXTERNAL_FLASH_INSTRUCTION_START_BLOCK ( 0U )

/** Number of physical NAND blocks reserved for instruction storage. */
#define EXTERNAL_FLASH_INSTRUCTION_BLOCK_COUNT ( 2048U )

/** First physical NAND block reserved for result storage. */
#define EXTERNAL_FLASH_RESULT_START_BLOCK \
    ( EXTERNAL_FLASH_INSTRUCTION_START_BLOCK + EXTERNAL_FLASH_INSTRUCTION_BLOCK_COUNT )

/** Number of physical NAND blocks reserved for result storage. */
#define EXTERNAL_FLASH_RESULT_BLOCK_COUNT ( 2048U )

/**-----------------------------------------------------------------------------
 *  Public Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

typedef enum
{
    EXTERNAL_FLASH_STATUS_OK = 0,
    EXTERNAL_FLASH_STATUS_ERROR,
    EXTERNAL_FLASH_STATUS_BUSY,
    EXTERNAL_FLASH_STATUS_TIMEOUT,
    EXTERNAL_FLASH_STATUS_INVALID_ARG,
    EXTERNAL_FLASH_STATUS_NOT_INITIALISED,
    EXTERNAL_FLASH_STATUS_STORAGE_FULL,
    EXTERNAL_FLASH_STATUS_ECC_ERROR,
    EXTERNAL_FLASH_STATUS_PROGRAM_FAIL,
    EXTERNAL_FLASH_STATUS_ERASE_FAIL
} ExternalFlashStatus_T;

typedef struct
{
    /** Usable instruction capacity after bad-block removal. */
    uint32_t instruction_capacity_bytes;

    /** Usable result capacity after bad-block removal. */
    uint32_t result_capacity_bytes;

    /** Result bytes committed to NAND in the active volatile result session. */
    uint32_t result_length_bytes;

    /** Factory and runtime bad blocks found in the configured partitions. */
    uint32_t bad_block_count;
} ExternalFlashInfo_T;

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

/**
 * @brief Initialises the external flash storage service and scans bad blocks.
 *
 * @return EXTERNAL_FLASH_STATUS_OK on success, otherwise an error status.
 *
 * @note Call this once before buffer_manager, result_transfer_manager, or
 *       test_package_recieve attempts to access the NAND-backed storage.
 */
ExternalFlashStatus_T EXTERNAL_FLASH_Init( void );

/**
 * @brief Returns partition capacity and current result-session state.
 *
 * @param info Destination for capacity and session information.
 *
 * @return EXTERNAL_FLASH_STATUS_OK on success, or EXTERNAL_FLASH_STATUS_INVALID_ARG.
 */
ExternalFlashStatus_T EXTERNAL_FLASH_GetInfo( ExternalFlashInfo_T* info );

/**
 * @brief Erases the result partition and starts a new volatile result session.
 *
 * @return EXTERNAL_FLASH_STATUS_OK on success, otherwise an error status.
 *
 * @note Existing result bytes are discarded. Results are not recovered after reset.
 * @note The flash manager should call this after the host has uploaded the test
 *       package and before execution_manager starts consuming instructions.
 */
ExternalFlashStatus_T EXTERNAL_FLASH_StartSession( void );

/**
 * @brief Appends opaque execution-result bytes to the result partition.
 *
 * @param data   Result bytes supplied by the buffer manager.
 * @param length Number of bytes to append.
 *
 * @return EXTERNAL_FLASH_STATUS_OK on success, otherwise an error status.
 *
 * @note The byte format is owned by execution_manager/buffer_manager. This
 *       function only preserves byte order and appends data to NAND.
 */
ExternalFlashStatus_T EXTERNAL_FLASH_WriteResults( const uint8_t* data, uint32_t length );

/**
 * @brief Flushes any partial result page to NAND.
 *
 * @return EXTERNAL_FLASH_STATUS_OK on success, otherwise an error status.
 *
 * @note Call this when execution ends or before result_transfer_manager starts
 *       returning data to the host.
 */
ExternalFlashStatus_T EXTERNAL_FLASH_FlushResults( void );

/**
 * @brief Reads instruction bytes by logical byte offset from the instruction partition.
 *
 * @param offset Logical instruction byte offset.
 * @param data   Destination buffer.
 * @param length Number of bytes to read.
 *
 * @return EXTERNAL_FLASH_STATUS_OK on success, otherwise an error status.
 *
 * @note buffer_manager should use this to refill instruction buffers for the
 *       execution_manager. The instruction byte format is not interpreted here.
 */
ExternalFlashStatus_T EXTERNAL_FLASH_ReadInstructions( uint32_t offset, uint8_t* data,
                                                       uint32_t length );

/**
 * @brief Reads stored result bytes by logical byte offset from the current session.
 *
 * @param offset Logical result byte offset.
 * @param data   Destination buffer.
 * @param length Number of bytes to read.
 *
 * @return EXTERNAL_FLASH_STATUS_OK on success, otherwise an error status.
 *
 * @note result_transfer_manager should use this to stream committed result bytes
 *       to the host. Unflushed staged bytes are intentionally not readable.
 */
ExternalFlashStatus_T EXTERNAL_FLASH_ReadResults( uint32_t offset, uint8_t* data,
                                                  uint32_t length );

/**
 * @brief Returns whether the external flash service is currently initialised.
 *
 * @return true once EXTERNAL_FLASH_Init() has completed successfully.
 */
bool EXTERNAL_FLASH_IsInitialised( void );

#ifdef __cplusplus
}
#endif

#endif /* EXTERNAL_FLASH_H */
