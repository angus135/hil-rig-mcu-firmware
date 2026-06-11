/******************************************************************************
 *  File:       hw_qspi.h
 *  Author:     Callum Rafferty
 *  Created:    5-May-2026
 *
 *  Description:
 *      Low level STM32 QSPI peripheral wrapper.
 *
 *      This module exposes generic QSPI command, read, and write operations.
 *      It does not know about NAND flash, NOR flash, pages, blocks, ECC,
 *      bad blocks, result storage, or execution timing.
 *
 *      Higher level memory drivers should use this module to issue bus
 *      transactions to an external memory device.
 *
 *  Notes:
 *      HW_QSPI_Init() must be called before any other HW_QSPI function.
 *
 *      GPIO, QSPI peripheral clock, and optional DMA MSP setup are expected
 *      to be provided by the STM32 HAL / CubeMX generated MSP code.
 *
 *      Initial bringup should use blocking transfers. DMA read and write are
 *      provided for bulk transfers once the blocking path is validated.
 *      Memory mapped mode is intentionally omitted from the first implementation.
 ******************************************************************************/

#ifndef HW_QSPI_H
#define HW_QSPI_H

#ifdef __cplusplus
extern "C"
{
#endif

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#ifdef TEST_BUILD
#include "tests/hw_qspi_mocks.h"
#else
#include "stm32f4xx_hal.h"
#endif

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
    HW_QSPI_STATUS_OK = 0,
    HW_QSPI_STATUS_ERROR,
    HW_QSPI_STATUS_BUSY,
    HW_QSPI_STATUS_TIMEOUT,
    HW_QSPI_STATUS_INVALID_ARG,
    HW_QSPI_STATUS_NOT_INITIALISED
} HW_QSPI_Status_T;

typedef enum
{
    HW_QSPI_LINES_NONE = 0,
    HW_QSPI_LINES_1,
    HW_QSPI_LINES_2,
    HW_QSPI_LINES_4
} HW_QSPI_Lines_T;

typedef enum
{
    HW_QSPI_ADDR_NONE = 0,
    HW_QSPI_ADDR_8_BITS,
    HW_QSPI_ADDR_16_BITS,
    HW_QSPI_ADDR_24_BITS,
    HW_QSPI_ADDR_32_BITS
} HW_QSPI_AddressSize_T;

typedef enum
{
    HW_QSPI_ALT_BYTES_NONE = 0,
    HW_QSPI_ALT_BYTES_8_BITS,
    HW_QSPI_ALT_BYTES_16_BITS,
    HW_QSPI_ALT_BYTES_24_BITS,
    HW_QSPI_ALT_BYTES_32_BITS
} HW_QSPI_AlternateBytesSize_T;

typedef struct
{
    uint8_t         instruction;
    HW_QSPI_Lines_T instruction_lines;

    uint32_t              address;
    HW_QSPI_AddressSize_T address_size;
    HW_QSPI_Lines_T       address_lines;

    uint32_t                     alternate_bytes;
    HW_QSPI_AlternateBytesSize_T alternate_bytes_size;
    HW_QSPI_Lines_T              alternate_bytes_lines;

    uint32_t dummy_cycles;

    HW_QSPI_Lines_T data_lines;

    uint32_t timeout_ms;
} HW_QSPI_Command_T;

typedef struct
{
    QSPI_HandleTypeDef* hal_handle;

    uint32_t clock_prescaler;
    uint32_t fifo_threshold;
    uint32_t sample_shifting;

    /*
     * HAL encoded flash size.
     *
     * For STM32 HAL QSPI, this is usually:
     *     FlashSize = log2(addressable_bytes) - 1
     *
     * Example:
     *     128 MB = 2^27 bytes, flash_size = 26
     *     512 MB = 2^29 bytes, flash_size = 28
     */
    uint32_t flash_size;

    uint32_t chip_select_high_time;
    uint32_t clock_mode;

    uint32_t default_timeout_ms;
} HW_QSPI_Config_T;

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

/**
 * @brief Initialises the QSPI peripheral wrapper.
 *
 * @param config Pointer to the QSPI configuration and HAL handle to use.
 *
 * @return HW_QSPI_STATUS_OK if the peripheral is initialised, otherwise an error status.
 *
 * @note This function must be called before any other HW_QSPI function.
 * @note GPIO, clocks, and DMA MSP setup are provided by CubeMX/HAL MSP code.
 */
HW_QSPI_Status_T HW_QSPI_Init( const HW_QSPI_Config_T* config );

/**
 * @brief Issues a QSPI command that has no data phase.
 *
 * @param command Pointer to the command phase description.
 *
 * @return HW_QSPI_STATUS_OK if the command is accepted by the HAL, otherwise an error status.
 *
 * @note Use this for opcode-only transactions or transactions with instruction/address/dummy
 *       phases but no transmit or receive data buffer.
 */
HW_QSPI_Status_T HW_QSPI_Command( const HW_QSPI_Command_T* command );

/**
 * @brief Issues a QSPI command followed by a blocking transmit data phase.
 *
 * @param command Pointer to the command phase description.
 * @param data    Pointer to the bytes to transmit.
 * @param length  Number of bytes to transmit.
 *
 * @return HW_QSPI_STATUS_OK if the complete transaction succeeds, otherwise an error status.
 *
 * @note The command's data_lines field selects the bus width for the data phase.
 * @note The buffer must remain valid until this blocking call returns.
 */
HW_QSPI_Status_T HW_QSPI_WriteBlocking( const HW_QSPI_Command_T* command, const uint8_t* data,
                                        uint32_t length );

/**
 * @brief Issues a QSPI command followed by a blocking receive data phase.
 *
 * @param command Pointer to the command phase description.
 * @param data    Pointer to the destination buffer.
 * @param length  Number of bytes to receive.
 *
 * @return HW_QSPI_STATUS_OK if the complete transaction succeeds, otherwise an error status.
 *
 * @note The command's data_lines field selects the bus width for the data phase.
 * @note The destination buffer must have capacity for at least length bytes.
 */
HW_QSPI_Status_T HW_QSPI_ReadBlocking( const HW_QSPI_Command_T* command, uint8_t* data,
                                       uint32_t length );

/**
 * @brief Issues a QSPI command followed by a DMA transmit data phase.
 *
 * @param command Pointer to the command phase description.
 * @param data    Pointer to the bytes to transmit.
 * @param length  Number of bytes to transmit.
 *
 * @return HW_QSPI_STATUS_OK if the DMA transfer is started, otherwise an error status.
 *
 * @note The source buffer must remain valid until HW_QSPI_IsTransferComplete() reports true or the
 *       transfer is aborted.
 */
HW_QSPI_Status_T HW_QSPI_WriteDma( const HW_QSPI_Command_T* command, const uint8_t* data,
                                   uint32_t length );

/**
 * @brief Issues a QSPI command followed by a DMA receive data phase.
 *
 * @param command Pointer to the command phase description.
 * @param data    Pointer to the destination buffer.
 * @param length  Number of bytes to receive.
 *
 * @return HW_QSPI_STATUS_OK if the DMA transfer is started, otherwise an error status.
 *
 * @note The destination buffer must remain valid until HW_QSPI_IsTransferComplete() reports true or
 *       the transfer is aborted.
 */
HW_QSPI_Status_T HW_QSPI_ReadDma( const HW_QSPI_Command_T* command, uint8_t* data,
                                  uint32_t length );

/**
 * @brief Checks whether the most recent asynchronous QSPI transfer has completed.
 *
 * @return true if the current asynchronous transfer is complete, otherwise false.
 */
bool HW_QSPI_IsTransferComplete( void );

/**
 * @brief Checks whether the QSPI peripheral or wrapper has an operation in progress.
 *
 * @return true if QSPI is busy, otherwise false.
 */
bool HW_QSPI_IsBusy( void );

/**
 * @brief Aborts the current QSPI operation.
 *
 * @return HW_QSPI_STATUS_OK if the abort succeeds, otherwise an error status.
 */
HW_QSPI_Status_T HW_QSPI_Abort( void );
#ifdef __cplusplus
}
#endif

#endif /* HW_QSPI_H */
