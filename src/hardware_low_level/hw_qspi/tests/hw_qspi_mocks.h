/******************************************************************************
 *  File:       hw_qspi_mocks.h
 *  Author:     Callum Rafferty
 *  Created:    25-Mar-2026
 *
 *  Description:
 *      Mock definitions of HAL types and functions for unit testing hw_qspi module.
 *
 *  Notes:
 *
 ******************************************************************************/

#ifndef HW_QSPI_MOCKS_H
#define HW_QSPI_MOCKS_H

#ifdef __cplusplus
extern "C"
{
#endif
// NOLINTBEGIN

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include <stdint.h>

/**-----------------------------------------------------------------------------
 *  Public Defines / Macros
 *------------------------------------------------------------------------------
 */

/* HAL QSPI mode constants used by hw_qspi.c. Values only need to be stable in tests. */
#define QSPI_ADDRESS_8_BITS ( 0x00000000U )
#define QSPI_ADDRESS_16_BITS ( 0x00000001U )
#define QSPI_ADDRESS_24_BITS ( 0x00000002U )
#define QSPI_ADDRESS_32_BITS ( 0x00000003U )

#define QSPI_ALTERNATE_BYTES_8_BITS ( 0x00000000U )
#define QSPI_ALTERNATE_BYTES_16_BITS ( 0x00000010U )
#define QSPI_ALTERNATE_BYTES_24_BITS ( 0x00000020U )
#define QSPI_ALTERNATE_BYTES_32_BITS ( 0x00000030U )

#define QSPI_INSTRUCTION_NONE ( 0x00000000U )
#define QSPI_INSTRUCTION_1_LINE ( 0x00000100U )
#define QSPI_INSTRUCTION_2_LINES ( 0x00000200U )
#define QSPI_INSTRUCTION_4_LINES ( 0x00000300U )

#define QSPI_ADDRESS_NONE ( 0x00000000U )
#define QSPI_ADDRESS_1_LINE ( 0x00001000U )
#define QSPI_ADDRESS_2_LINES ( 0x00002000U )
#define QSPI_ADDRESS_4_LINES ( 0x00003000U )

#define QSPI_ALTERNATE_BYTES_NONE ( 0x00000000U )
#define QSPI_ALTERNATE_BYTES_1_LINE ( 0x00010000U )
#define QSPI_ALTERNATE_BYTES_2_LINES ( 0x00020000U )
#define QSPI_ALTERNATE_BYTES_4_LINES ( 0x00030000U )

#define QSPI_DATA_NONE ( 0x00000000U )
#define QSPI_DATA_1_LINE ( 0x00100000U )
#define QSPI_DATA_2_LINES ( 0x00200000U )
#define QSPI_DATA_4_LINES ( 0x00300000U )

#define QSPI_DDR_MODE_DISABLE ( 0x00000000U )
#define QSPI_DDR_HHC_ANALOG_DELAY ( 0x00000000U )
#define QSPI_SIOO_INST_EVERY_CMD ( 0x00000000U )

#define QSPI_FLASH_ID_1 ( 0x00000000U )
#define QSPI_DUALFLASH_DISABLE ( 0x00000000U )

/**-----------------------------------------------------------------------------
 *  Public Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

typedef enum
{
    HAL_OK      = 0x00U,
    HAL_ERROR   = 0x01U,
    HAL_BUSY    = 0x02U,
    HAL_TIMEOUT = 0x03U
} HAL_StatusTypeDef;

typedef enum
{
    HAL_QSPI_STATE_RESET             = 0x00U,
    HAL_QSPI_STATE_READY             = 0x01U,
    HAL_QSPI_STATE_BUSY              = 0x02U,
    HAL_QSPI_STATE_BUSY_INDIRECT_TX  = 0x12U,
    HAL_QSPI_STATE_BUSY_INDIRECT_RX  = 0x22U,
    HAL_QSPI_STATE_BUSY_AUTO_POLLING = 0x42U,
    HAL_QSPI_STATE_BUSY_MEM_MAPPED   = 0x82U,
    HAL_QSPI_STATE_ABORT             = 0x08U,
    HAL_QSPI_STATE_ERROR             = 0x04U
} HAL_QSPI_StateTypeDef;

typedef struct
{
    uint32_t ClockPrescaler;
    uint32_t FifoThreshold;
    uint32_t SampleShifting;
    uint32_t FlashSize;
    uint32_t ChipSelectHighTime;
    uint32_t ClockMode;
    uint32_t FlashID;
    uint32_t DualFlash;
} QSPI_InitTypeDef;

typedef struct
{
    QSPI_InitTypeDef      Init;
    HAL_QSPI_StateTypeDef State;
    uint32_t              ErrorCode;
} QSPI_HandleTypeDef;

typedef struct
{
    uint32_t Instruction;
    uint32_t Address;
    uint32_t AlternateBytes;
    uint32_t AddressSize;
    uint32_t AlternateBytesSize;
    uint32_t DummyCycles;
    uint32_t InstructionMode;
    uint32_t AddressMode;
    uint32_t AlternateByteMode;
    uint32_t DataMode;
    uint32_t NbData;
    uint32_t DdrMode;
    uint32_t DdrHoldHalfCycle;
    uint32_t SIOOMode;
} QSPI_CommandTypeDef;

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

HAL_StatusTypeDef HAL_QSPI_Init( QSPI_HandleTypeDef* hqspi );
HAL_StatusTypeDef HAL_QSPI_Command( QSPI_HandleTypeDef* hqspi, QSPI_CommandTypeDef* cmd,
                                    uint32_t timeout );
HAL_StatusTypeDef HAL_QSPI_Transmit( QSPI_HandleTypeDef* hqspi, uint8_t* p_data, uint32_t timeout );
HAL_StatusTypeDef HAL_QSPI_Receive( QSPI_HandleTypeDef* hqspi, uint8_t* p_data, uint32_t timeout );
HAL_StatusTypeDef HAL_QSPI_Transmit_DMA( QSPI_HandleTypeDef* hqspi, uint8_t* p_data );
HAL_StatusTypeDef HAL_QSPI_Receive_DMA( QSPI_HandleTypeDef* hqspi, uint8_t* p_data );
HAL_QSPI_StateTypeDef HAL_QSPI_GetState( const QSPI_HandleTypeDef* hqspi );
HAL_StatusTypeDef     HAL_QSPI_Abort( QSPI_HandleTypeDef* hqspi );

void HAL_QSPI_TxCpltCallback( QSPI_HandleTypeDef* hqspi );
void HAL_QSPI_RxCpltCallback( QSPI_HandleTypeDef* hqspi );
void HAL_QSPI_ErrorCallback( QSPI_HandleTypeDef* hqspi );
void HAL_QSPI_AbortCpltCallback( QSPI_HandleTypeDef* hqspi );

// NOLINTEND

#ifdef __cplusplus
}
#endif

#endif /* HW_QSPI_MOCKS_H */
