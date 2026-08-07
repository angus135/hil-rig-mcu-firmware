/******************************************************************************
 *  File:       hw_nand.h
 *  Author:     Callum Rafferty
 *  Created:    5-May-2026
 *
 *  Description:
 *      Low level SPI NAND flash device driver interface.
 *
 *      This module owns NAND-specific command sequencing, status polling,
 *      feature registers, page/cache operations, program operations, erase
 *      operations, and device geometry.
 *
 *      It uses hw_qspi for bus transactions and exposes NAND-level operations
 *      to higher level storage drivers.
 *
 *  Notes:
 *      HW_NAND_Init() must complete before device transactions are used.
 *      HW_NAND_GetGeometry() and HW_NAND_GetLastEccStatus() do not access the
 *      device and may be called before initialisation. Before the first checked
 *      page read, the last ECC status is HW_NAND_ECC_STATUS_UNKNOWN.
 *
 *      This module should not expose STM32 HAL or QSPI HAL types. Application
 *      code should use external_flash rather than calling hw_nand directly.
 *      Device operations are synchronous and intended for task context, not
 *      interrupt context.
 ******************************************************************************/

#ifndef HW_NAND_H
#define HW_NAND_H

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

typedef enum
{
    HW_NAND_STATUS_OK = 0,
    HW_NAND_STATUS_ERROR,
    HW_NAND_STATUS_BUSY,
    HW_NAND_STATUS_TIMEOUT,
    HW_NAND_STATUS_INVALID_ARG,
    HW_NAND_STATUS_NOT_INITIALISED,
    HW_NAND_STATUS_UNSUPPORTED_DEVICE,
    HW_NAND_STATUS_ECC_ERROR,
    HW_NAND_STATUS_PROGRAM_FAIL,
    HW_NAND_STATUS_ERASE_FAIL
} HW_NAND_Status_T;

typedef enum
{
    HW_NAND_ECC_STATUS_NO_BIT_FLIPS = 0,
    HW_NAND_ECC_STATUS_CORRECTED_1_TO_7,
    HW_NAND_ECC_STATUS_CORRECTED_8,
    HW_NAND_ECC_STATUS_UNCORRECTABLE,
    HW_NAND_ECC_STATUS_UNKNOWN
} HW_NAND_EccStatus_T;

typedef struct
{
    uint32_t page_size_bytes;
    uint32_t spare_size_bytes;
    uint32_t pages_per_block;
    uint32_t block_count;
} HW_NAND_Geometry_T;

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

/**
 * @brief Initialises the SPI NAND device.
 *
 * @return HW_NAND_STATUS_OK when reset, ID verification, block unlock, and ECC configuration
 * succeed.
 *
 * @note This driver is currently configured for the GD5F1GM7UEYIGR device ID.
 */
HW_NAND_Status_T HW_NAND_Init( void );

/**
 * @brief Returns the compiled NAND geometry.
 *
 * @param geometry Destination for page, spare, block, and block-count values.
 *
 * @return HW_NAND_STATUS_OK on success, or HW_NAND_STATUS_INVALID_ARG.
 *
 * @note This accessor does not communicate with the NAND device and may be
 *       called before HW_NAND_Init().
 */
HW_NAND_Status_T HW_NAND_GetGeometry( HW_NAND_Geometry_T* geometry );

/**
 * @brief Returns the ECC result captured by the most recent checked page read.
 *
 * @param ecc_status Destination for the decoded ECC state.
 *
 * @return HW_NAND_STATUS_OK on success, or HW_NAND_STATUS_INVALID_ARG.
 *
 * @note This accessor may be called before HW_NAND_Init(); it reports
 *       HW_NAND_ECC_STATUS_UNKNOWN until a checked page read records a result.
 */
HW_NAND_Status_T HW_NAND_GetLastEccStatus( HW_NAND_EccStatus_T* ecc_status );

/**
 * @brief Reads bytes from a physical NAND page using blocking QSPI.
 *
 * @param page   Physical page row address.
 * @param column First cache column to read.
 * @param data   Destination buffer.
 * @param length Number of bytes to read.
 *
 * @return HW_NAND_STATUS_OK after the complete page operation succeeds,
 *         otherwise an error status.
 */
HW_NAND_Status_T HW_NAND_ReadPageBlocking( uint32_t page, uint16_t column, uint8_t* data,
                                           uint32_t length );

/**
 * @brief Reads a physical NAND page using QSPI DMA.
 *
 * @param page   Physical page row address.
 * @param column First cache column to read.
 * @param data   Destination buffer.
 * @param length Number of bytes to read.
 *
 * @return HW_NAND_STATUS_OK after the page read and DMA transfer complete,
 *         otherwise an error status.
 *
 * @note This call is synchronous from the caller's perspective but uses DMA
 *       for the bulk cache transfer. The destination buffer may be reused when
 *       the function returns.
 * @note While DMA is active, the calling task blocks on the QSPI completion
 *       semaphore so other ready RTOS tasks may execute.
 * @note This function is task-context only.
 */
HW_NAND_Status_T HW_NAND_ReadPageDma( uint32_t page, uint16_t column, uint8_t* data,
                                      uint32_t length );

/**
 * @brief Programs bytes into a physical NAND page using blocking QSPI.
 *
 * @param page   Physical page row address.
 * @param column First cache column to program.
 * @param data   Source buffer.
 * @param length Number of bytes to program.
 *
 * @return HW_NAND_STATUS_OK after program-execute succeeds, otherwise an
 *         error status.
 */
HW_NAND_Status_T HW_NAND_ProgramPageBlocking( uint32_t page, uint16_t column, const uint8_t* data,
                                              uint32_t length );

/**
 * @brief Programs a physical NAND page using QSPI DMA.
 *
 * @param page   Physical page row address.
 * @param column First cache column to program.
 * @param data   Source buffer.
 * @param length Number of bytes to program.
 *
 * @return HW_NAND_STATUS_OK after DMA program-load and NAND program-execute
 *         complete, otherwise an error status.
 *
 * @note This call is synchronous from the caller's perspective but uses DMA
 *       for the bulk cache transfer. The source buffer may be reused when the
 *       function returns.
 * @note While DMA is active, the calling task blocks on the QSPI completion
 *       semaphore so other ready RTOS tasks may execute.
 * @note This function is task-context only.
 */
HW_NAND_Status_T HW_NAND_ProgramPageDma( uint32_t page, uint16_t column, const uint8_t* data,
                                         uint32_t length );

/**
 * @brief Erases a physical block and waits for completion.
 *
 * @param block Physical block index.
 *
 * @return HW_NAND_STATUS_OK after erase completion, otherwise an error status.
 *
 * @note The calling task delays between long erase-status polls so other ready
 *       RTOS tasks may execute.
 * @note This function is task-context only.
 */
HW_NAND_Status_T HW_NAND_BlockErase( uint32_t block );

/**
 * @brief Checks the factory bad-block marker for a physical block.
 *
 * @param block  Physical block index.
 * @param is_bad Destination set true when the marker is programmed.
 *
 * @return HW_NAND_STATUS_OK after the marker is read, otherwise an error status.
 *
 * @note For GD5F1GM7UEYIGR, datasheet Rev. 1.3 section 12.4 defines the marker
 *       as non-0xFF data at byte 2048 on the first page of the block. It does
 *       not require checking the second or last page.
 */
HW_NAND_Status_T HW_NAND_IsBlockBad( uint32_t block, bool* is_bad );

/**
 * @brief Programs a bad-block marker into the first page spare area of a block.
 *
 * @param block Physical block index.
 *
 * @return HW_NAND_STATUS_OK after the marker is programmed, otherwise an
 *         error status.
 */
HW_NAND_Status_T HW_NAND_MarkBlockBad( uint32_t block );

#ifdef __cplusplus
}
#endif

#endif /* HW_NAND_H */
