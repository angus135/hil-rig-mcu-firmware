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
 *      Initial bringup should use blocking transfers. DMA support should be
 *      added later through separate APIs.
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
    uint32_t        data_length;

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

HW_QSPI_Status_T HW_QSPI_Init( const HW_QSPI_Config_T* config );

HW_QSPI_Status_T HW_QSPI_Command( const HW_QSPI_Command_T* command );

HW_QSPI_Status_T HW_QSPI_WriteBlocking( const HW_QSPI_Command_T* command, const uint8_t* data,
                                        uint32_t length );

HW_QSPI_Status_T HW_QSPI_ReadBlocking( const HW_QSPI_Command_T* command, uint8_t* data,
                                       uint32_t length );

bool HW_QSPI_IsBusy( void );

HW_QSPI_Status_T HW_QSPI_Abort( void );

#ifdef __cplusplus
}
#endif

#endif /* HW_QSPI_H */
