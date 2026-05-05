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
    HW_NAND_STATUS_ECC_ERROR,
    HW_NAND_STATUS_PROGRAM_FAIL,
    HW_NAND_STATUS_ERASE_FAIL
} HW_NAND_Status_T;

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

HW_NAND_Status_T HW_NAND_Init( void );

HW_NAND_Status_T HW_NAND_Reset( void );

HW_NAND_Status_T HW_NAND_ReadId( HW_NAND_Id_T* id );

HW_NAND_Status_T HW_NAND_GetGeometry( HW_NAND_Geometry_T* geometry );

HW_NAND_Status_T HW_NAND_GetFeature( uint8_t feature_address, uint8_t* value );

HW_NAND_Status_T HW_NAND_SetFeature( uint8_t feature_address, uint8_t value );

HW_NAND_Status_T HW_NAND_WaitReady( uint32_t timeout_ms );

HW_NAND_Status_T HW_NAND_ReadPageToCache( uint32_t page );

HW_NAND_Status_T HW_NAND_ReadCacheBlocking( uint16_t column, uint8_t* data, uint32_t length );

HW_NAND_Status_T HW_NAND_ReadCacheDma( uint16_t column, uint8_t* data, uint32_t length );

HW_NAND_Status_T HW_NAND_ProgramLoadBlocking( uint16_t column, const uint8_t* data,
                                              uint32_t length );

HW_NAND_Status_T HW_NAND_ProgramLoadDma( uint16_t column, const uint8_t* data, uint32_t length );

HW_NAND_Status_T HW_NAND_ProgramExecute( uint32_t page );

HW_NAND_Status_T HW_NAND_BlockErase( uint32_t block );

bool HW_NAND_IsTransferComplete( void );

bool HW_NAND_IsBusy( void );

#ifdef __cplusplus
}
#endif

#endif /* HW_NAND_H */
