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
 *      - test_package_recieve receives a test package from the host and
 *        programs the instruction partition through the instruction upload API.
 *      - flash_manager owns the RAM instruction and result buffers used by
 *        execution_manager.
 *      - flash_manager is the only task that calls external_flash during normal
 *        execution. It refills page sized instruction buffers with
 *        EXTERNAL_FLASH_ReadInstructionPage and drains page sized result buffers
 *        with EXTERNAL_FLASH_WriteResultPage.
 *      - execution_manager consumes instruction bytes and produces result bytes
 *        through flash_manager owned buffers. It must not call external_flash.
 *      - result_transfer_manager reads committed result bytes with
 *        EXTERNAL_FLASH_ReadResults and passes them to the host interface.
 *
 *      Design decisions:
 *      - The instruction and result regions are fixed compile time partitions.
 *      - Instructions and results are volatile for now. They are not recovered
 *        after reset.
 *      - Repeated instruction uploads and result sessions use volatile
 *        erase-count-aware block allocation so short images do not always
 *        program the first good blocks in each partition.
 *      - Result bytes are stored exactly as supplied. This layer does not parse
 *        result records or add per page metadata yet.
 *      - Bad blocks are scanned at init and skipped by logical byte addressing.
 *      - Result storage is prepared at the start of a session to keep execution
 *        time writes deterministic.
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
#define EXTERNAL_FLASH_RESULT_START_BLOCK                                                          \
    ( EXTERNAL_FLASH_INSTRUCTION_START_BLOCK + EXTERNAL_FLASH_INSTRUCTION_BLOCK_COUNT )

/** Number of physical NAND blocks reserved for result storage. */
#define EXTERNAL_FLASH_RESULT_BLOCK_COUNT ( 2032U )

/** First physical NAND block reserved for external_flash metadata. */
#define EXTERNAL_FLASH_METADATA_START_BLOCK                                                   \
    ( EXTERNAL_FLASH_RESULT_START_BLOCK + EXTERNAL_FLASH_RESULT_BLOCK_COUNT )

/** Number of physical NAND blocks reserved for wear and allocation metadata. */
#define EXTERNAL_FLASH_METADATA_BLOCK_COUNT ( 16U )

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
    /** Usable instruction capacity after bad block removal. */
    uint32_t instruction_capacity_bytes;

    /** Usable result capacity after bad block removal. */
    uint32_t result_capacity_bytes;

    /** Instruction bytes committed to NAND by the current volatile upload. */
    uint32_t instruction_length_bytes;

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
 * @note Call this once before flash_manager, result_transfer_manager, or
 *       test_package_recieve attempts to access the NAND backed storage.
 */
ExternalFlashStatus_T EXTERNAL_FLASH_Init( void );

/**
 * @brief Returns partition capacity and current result session state.
 *
 * @param info Destination for capacity and session information.
 *
 * @return EXTERNAL_FLASH_STATUS_OK on success, or EXTERNAL_FLASH_STATUS_INVALID_ARG.
 */
ExternalFlashStatus_T EXTERNAL_FLASH_GetInfo( ExternalFlashInfo_T* info );

/**
 * @brief Prepares result storage and starts a new volatile result session.
 *
 * @return EXTERNAL_FLASH_STATUS_OK on success, otherwise an error status.
 *
 * @note Existing result bytes are discarded. Results are not recovered after reset.
 * @note The flash manager should call this after the host has uploaded the test
 *       package and before execution_manager starts consuming instructions.
 */
ExternalFlashStatus_T EXTERNAL_FLASH_StartSession( void );

/**
 * @brief Prepares instruction storage and starts a new instruction upload.
 *
 * @param expected_length Total instruction byte count expected from the host package.
 *
 * @return EXTERNAL_FLASH_STATUS_OK on success, otherwise an error status.
 *
 * @note Existing instruction bytes are discarded. Instruction metadata is RAM
 *       only in this first version, so instructions are not recovered after reset.
 * @note test_package_recieve should call this before streaming package
 *       instruction bytes through EXTERNAL_FLASH_WriteInstructionBytes or
 *       EXTERNAL_FLASH_WriteInstructionPage.
 */
ExternalFlashStatus_T EXTERNAL_FLASH_StartInstructionUpload( uint32_t expected_length );

/**
 * @brief Appends opaque instruction bytes to the instruction partition upload.
 *
 * @param data   Instruction bytes received from the host package.
 * @param length Number of bytes to append.
 *
 * @return EXTERNAL_FLASH_STATUS_OK on success, otherwise an error status.
 *
 * @note This is the preferred package upload API. It accepts arbitrary host
 *       transfer chunk sizes and internally stages partial NAND pages.
 * @note The instruction byte format is owned by the package and execution data
 *       model. This function only preserves byte order and appends data to NAND.
 */
ExternalFlashStatus_T EXTERNAL_FLASH_WriteInstructionBytes( const uint8_t* data,
                                                            uint32_t length );

/**
 * @brief Writes one logical instruction page during an active upload.
 *
 * @param data         Instruction page data supplied by test_package_recieve.
 * @param valid_length Number of valid instruction bytes in this page.
 *
 * @return EXTERNAL_FLASH_STATUS_OK on success, otherwise an error status.
 *
 * @note Full page writes are programmed directly from the caller supplied
 *       buffer. Partial page writes are padded internally with 0xFF.
 * @note This API is optional. It is useful when the package receiver already
 *       has page sized instruction chunks.
 * @note This API must not be mixed with a partially staged
 *       EXTERNAL_FLASH_WriteInstructionBytes call.
 */
ExternalFlashStatus_T EXTERNAL_FLASH_WriteInstructionPage( const uint8_t* data,
                                                           uint32_t valid_length );

/**
 * @brief Finishes an instruction upload and commits any final partial page.
 *
 * @return EXTERNAL_FLASH_STATUS_OK if the expected instruction length has been
 *         committed to NAND, otherwise an error status.
 *
 * @note Call this after all host package instruction bytes have been supplied.
 *       Execution should not start until this function succeeds.
 */
ExternalFlashStatus_T EXTERNAL_FLASH_FinishInstructionUpload( void );

/**
 * @brief Writes one logical result page to the result partition.
 *
 * @param data         Result page data supplied by the flash manager.
 * @param valid_length Number of valid logical result bytes in this page.
 *
 * @return EXTERNAL_FLASH_STATUS_OK on success, otherwise an error status.
 *
 * @note If valid_length equals the NAND page size, data is programmed directly
 *       from the caller supplied buffer. The caller must keep this buffer valid
 *       and unchanged until the function returns.
 * @note If valid_length is less than the NAND page size, this is treated as the
 *       final partial page and padded internally with 0xFF before programming.
 * @note After a partial page write succeeds, no further result page writes
 *       should be appended in the same session.
 * @note This is the only result write API. Results should be supplied as full
 *       pages during execution and as one final partial page after execution.
 */
ExternalFlashStatus_T EXTERNAL_FLASH_WriteResultPage( const uint8_t* data, uint32_t valid_length );

/**
 * @brief Reads one instruction page or partial instruction page using DMA internally.
 *
 * @param offset Logical instruction byte offset. Must be page aligned.
 * @param data   Destination buffer supplied by the flash manager.
 * @param length Number of bytes to read. Must be greater than zero and no larger
 *               than the NAND page size.
 *
 * @return EXTERNAL_FLASH_STATUS_OK on success, otherwise an error status.
 *
 * @note This API is intended for flash manager instruction queue refills.
 * @note Reads are limited to the instruction bytes committed by the most recent
 *       successful instruction upload.
 * @note The caller must keep data valid and writable until the function returns.
 * @note This API is synchronous from the caller perspective. Internally it uses
 *       the NAND DMA read path and waits for DMA completion before returning.
 */
ExternalFlashStatus_T EXTERNAL_FLASH_ReadInstructionPage( uint32_t offset, uint8_t* data,
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
 *       to the host.
 */
ExternalFlashStatus_T EXTERNAL_FLASH_ReadResults( uint32_t offset, uint8_t* data, uint32_t length );

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
