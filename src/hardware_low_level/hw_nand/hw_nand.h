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
 *      HW_NAND_Init() must be called before any other HW_NAND function.
 *
 *      This module should not expose STM32 HAL or QSPI HAL types. Application
 *      code should use external_flash rather than calling hw_nand directly.
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
    HW_NAND_ECC_STATUS_CORRECTED_1_TO_2,
    HW_NAND_ECC_STATUS_CORRECTED_3_TO_6,
    HW_NAND_ECC_STATUS_UNCORRECTABLE,
    HW_NAND_ECC_STATUS_UNKNOWN
} HW_NAND_EccStatus_T;

typedef struct
{
    uint8_t manufacturer_id;
    uint8_t device_id;
} HW_NAND_Id_T;

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
 * @note This driver is currently configured for the S35ML04G3 device ID.
 */
HW_NAND_Status_T HW_NAND_Init( void );

/**
 * @brief Issues the NAND reset command.
 *
 * @return HW_NAND_STATUS_OK if the reset command is accepted.
 */
HW_NAND_Status_T HW_NAND_Reset( void );

/**
 * @brief Reads the two byte manufacturer/device ID.
 *
 * @param id Destination for the ID bytes.
 *
 * @return HW_NAND_STATUS_OK on success, otherwise an error status.
 */
HW_NAND_Status_T HW_NAND_ReadId( HW_NAND_Id_T* id );

/**
 * @brief Returns the compiled NAND geometry.
 *
 * @param geometry Destination for page, spare, block, and block-count values.
 *
 * @return HW_NAND_STATUS_OK on success, or HW_NAND_STATUS_INVALID_ARG.
 */
HW_NAND_Status_T HW_NAND_GetGeometry( HW_NAND_Geometry_T* geometry );

/**
 * @brief Returns the ECC result captured by the most recent checked page read.
 *
 * @param ecc_status Destination for the decoded ECC state.
 *
 * @return HW_NAND_STATUS_OK on success, or HW_NAND_STATUS_INVALID_ARG.
 */
HW_NAND_Status_T HW_NAND_GetLastEccStatus( HW_NAND_EccStatus_T* ecc_status );

/**
 * @brief Reads a raw feature register.
 *
 * @param feature_address Datasheet feature address, for example A0h, B0h, or C0h.
 * @param value           Destination for the register byte.
 *
 * @return HW_NAND_STATUS_OK on success, otherwise an error status.
 */
HW_NAND_Status_T HW_NAND_GetFeature( uint8_t feature_address, uint8_t* value );

/**
 * @brief Writes a raw feature register.
 *
 * @param feature_address Datasheet feature address, for example A0h or B0h.
 * @param value           Register byte to write.
 *
 * @return HW_NAND_STATUS_OK on success, otherwise an error status.
 */
HW_NAND_Status_T HW_NAND_SetFeature( uint8_t feature_address, uint8_t value );

/**
 * @brief Polls the status register until the device is ready.
 *
 * @param timeout_ms Polling timeout in milliseconds.
 *
 * @return HW_NAND_STATUS_OK when ready, otherwise an error or timeout status.
 *
 * @note This only checks OIP. Use the operation-specific wait functions after page read, program,
 *       or erase operations.
 */
HW_NAND_Status_T HW_NAND_WaitReady( uint32_t timeout_ms );

/**
 * @brief Waits for a page-read-to-cache operation and decodes ECC status.
 *
 * @param timeout_ms Polling timeout in milliseconds.
 *
 * @return HW_NAND_STATUS_OK on success, or HW_NAND_STATUS_ECC_ERROR for uncorrectable reads.
 */
HW_NAND_Status_T HW_NAND_WaitPageReadComplete( uint32_t timeout_ms );

/**
 * @brief Waits for a program-execute operation and checks program-fail status.
 */
HW_NAND_Status_T HW_NAND_WaitProgramComplete( uint32_t timeout_ms );

/**
 * @brief Waits for a block-erase operation and checks erase-fail status.
 */
HW_NAND_Status_T HW_NAND_WaitBlockEraseComplete( uint32_t timeout_ms );

/**
 * @brief Starts a page-read transfer from NAND array into the internal cache.
 *
 * @param page Physical page row address.
 *
 * @return HW_NAND_STATUS_OK if the command is accepted.
 *
 * @note This function does not wait for OIP to clear. Use HW_NAND_WaitPageReadComplete() before
 *       reading cache.
 */
HW_NAND_Status_T HW_NAND_StartPageReadToCache( uint32_t page );

/**
 * @brief Reads a physical NAND page into the internal cache and waits until ready.
 *
 * @param page Physical page row address.
 *
 * @return HW_NAND_STATUS_OK on success, or an ECC/timeout/error status.
 */
HW_NAND_Status_T HW_NAND_ReadPageToCache( uint32_t page );

/**
 * @brief Reads bytes from the NAND cache using blocking QSPI.
 *
 * @param column Cache column address.
 * @param data   Destination buffer.
 * @param length Number of bytes to read.
 *
 * @return HW_NAND_STATUS_OK on success, otherwise an error status.
 */
HW_NAND_Status_T HW_NAND_ReadCacheBlocking( uint16_t column, uint8_t* data, uint32_t length );

/**
 * @brief Starts a DMA read from the NAND cache.
 *
 * @param column Cache column address.
 * @param data   Destination buffer. It must remain valid until the DMA transfer completes.
 * @param length Number of bytes to read.
 *
 * @return HW_NAND_STATUS_OK if the DMA transfer starts.
 */
HW_NAND_Status_T HW_NAND_ReadCacheDma( uint16_t column, uint8_t* data, uint32_t length );

/**
 * @brief Reads a physical page to cache, waits ready, then reads cache with blocking QSPI.
 */
HW_NAND_Status_T HW_NAND_ReadPageBlocking( uint32_t page, uint16_t column, uint8_t* data,
                                           uint32_t length );

/**
 * @brief Reads a physical page to cache, waits ready, then starts a DMA cache read.
 */
HW_NAND_Status_T HW_NAND_ReadPageDma( uint32_t page, uint16_t column, uint8_t* data,
                                      uint32_t length );

/**
 * @brief Performs write-enable and blocking quad program-load into the NAND cache.
 */
HW_NAND_Status_T HW_NAND_ProgramLoadBlocking( uint16_t column, const uint8_t* data,
                                              uint32_t length );

/**
 * @brief Performs write-enable and starts a DMA quad program-load into the NAND cache.
 *
 * @note The caller must wait for HW_NAND_IsTransferComplete() before executing the program.
 */
HW_NAND_Status_T HW_NAND_ProgramLoadDma( uint16_t column, const uint8_t* data, uint32_t length );

/**
 * @brief Starts program execute for the page currently staged in the NAND cache.
 *
 * @param page Physical page row address.
 *
 * @return HW_NAND_STATUS_OK if the command is accepted.
 *
 * @note This does not wait for OIP to clear. Use HW_NAND_WaitProgramComplete() to finish the
 *       operation.
 */
HW_NAND_Status_T HW_NAND_StartProgramExecute( uint32_t page );

/**
 * @brief Starts program execute and waits for completion.
 */
HW_NAND_Status_T HW_NAND_ProgramExecute( uint32_t page );

/**
 * @brief Blocking convenience wrapper for program load followed by program execute.
 */
HW_NAND_Status_T HW_NAND_ProgramPageBlocking( uint32_t page, uint16_t column, const uint8_t* data,
                                              uint32_t length );

/**
 * @brief Starts erasing a physical block.
 *
 * @param block Physical block index.
 *
 * @return HW_NAND_STATUS_OK if the erase command is accepted.
 *
 * @note This does not wait for OIP to clear. Use HW_NAND_WaitBlockEraseComplete() to finish the
 *       operation.
 */
HW_NAND_Status_T HW_NAND_StartBlockErase( uint32_t block );

/**
 * @brief Erases a physical block and waits for completion.
 */
HW_NAND_Status_T HW_NAND_BlockErase( uint32_t block );

/**
 * @brief Checks the factory bad-block markers for a physical block.
 */
HW_NAND_Status_T HW_NAND_IsBlockBad( uint32_t block, bool* is_bad );

/**
 * @brief Programs a bad-block marker into the first page spare area of a block.
 */
HW_NAND_Status_T HW_NAND_MarkBlockBad( uint32_t block );

/**
 * @brief Returns whether the current QSPI DMA transfer is complete.
 */
bool HW_NAND_IsTransferComplete( void );

/**
 * @brief Returns whether the QSPI wrapper is busy.
 */
bool HW_NAND_IsBusy( void );

#ifdef __cplusplus
}
#endif

#endif /* HW_NAND_H */
