/******************************************************************************
 *  File:       hw_spi_mocks.h
 *  Author:     Angus Corr
 *  Created:    10-Apr-2026
 *
 *  Description:
 *      Mock definitions of HAL types and functions for unit testing hw_spi module.
 *
 *  Notes:
 *
 ******************************************************************************/

#ifndef HW_SPI_MOCKS_H
#define HW_SPI_MOCKS_H

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

// HAL defined SPI peripherals
#define SPI1 ( void* )1
#define SPI4 ( void* )4

// HAL defined SPI modes
#define SPI_MODE_SLAVE ( 0 )
#define SPI_MODE_MASTER ( 1 )

// HAL defined SPI directions
#define SPI_DIRECTION_2LINES ( 0x00000000U )

// HAL defined SPI data size
#define SPI_DATASIZE_8BIT ( 8 )
#define SPI_DATASIZE_16BIT ( 16 )

// HAL defined SPI Clock Polarity (CPOL)
#define SPI_POLARITY_LOW ( 0 )
#define SPI_POLARITY_HIGH ( 1 )

// HAL defined SPI Clock Polarity (CPOL)
#define SPI_PHASE_1EDGE ( 0 )
#define SPI_PHASE_2EDGE ( 1 )

// HAL defined NSS (chip select management)
#define SPI_NSS_HARD_INPUT ( 0 )
#define SPI_NSS_HARD_OUTPUT ( 1 )

// HAL defined baud rate prescalar
#define SPI_BAUDRATEPRESCALER_2 2
#define SPI_BAUDRATEPRESCALER_4 4
#define SPI_BAUDRATEPRESCALER_8 8
#define SPI_BAUDRATEPRESCALER_16 16
#define SPI_BAUDRATEPRESCALER_32 32
#define SPI_BAUDRATEPRESCALER_64 64
#define SPI_BAUDRATEPRESCALER_128 128
#define SPI_BAUDRATEPRESCALER_256 256

// HAL defined endianness
#define SPI_FIRSTBIT_MSB 0
#define SPI_FIRSTBIT_LSB 1

// Other options to automatically disable
#define SPI_TIMODE_DISABLE ( 0x00000000U )
#define SPI_CRCCALCULATION_DISABLE ( 0x00000000U )

/**-----------------------------------------------------------------------------
 *  Public Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

typedef struct
{
    volatile uint32_t
        CR1; /*!< SPI control register 1 (not used in I2S mode),      Address offset: 0x00 */
    volatile uint32_t
        CR2; /*!< SPI control register 2,                             Address offset: 0x04 */
    volatile uint32_t
        SR; /*!< SPI status register,                                Address offset: 0x08 */
    volatile uint32_t
        DR; /*!< SPI data register,                                  Address offset: 0x0C */
    volatile uint32_t
        CRCPR; /*!< SPI CRC polynomial register (not used in I2S mode), Address offset: 0x10 */
    volatile uint32_t
        RXCRCR; /*!< SPI RX CRC register (not used in I2S mode),         Address offset: 0x14 */
    volatile uint32_t
        TXCRCR; /*!< SPI TX CRC register (not used in I2S mode),         Address offset: 0x18 */
    volatile uint32_t
        I2SCFGR; /*!< SPI_I2S configuration register,                     Address offset: 0x1C */
    volatile uint32_t
        I2SPR; /*!< SPI_I2S prescaler register,                         Address offset: 0x20 */
} SPI_TypeDef;

/**
 * @brief  SPI Configuration Structure definition
 */
typedef struct
{
    uint32_t Mode; /*!< Specifies the SPI operating mode.
                        This parameter can be a value of @ref SPI_Mode */

    uint32_t Direction; /*!< Specifies the SPI bidirectional mode state.
                             This parameter can be a value of @ref SPI_Direction */

    uint32_t DataSize; /*!< Specifies the SPI data size.
                            This parameter can be a value of @ref SPI_Data_Size */

    uint32_t CLKPolarity; /*!< Specifies the serial clock steady state.
                               This parameter can be a value of @ref SPI_Clock_Polarity */

    uint32_t CLKPhase; /*!< Specifies the clock active edge for the bit capture.
                            This parameter can be a value of @ref SPI_Clock_Phase */

    uint32_t NSS; /*!< Specifies whether the NSS signal is managed by
                       hardware (NSS pin) or by software using the SSI bit.
                       This parameter can be a value of @ref SPI_Slave_Select_management */

    uint32_t BaudRatePrescaler; /*!< Specifies the Baud Rate prescaler value which will be
                                     used to configure the transmit and receive SCK clock.
                                     This parameter can be a value of @ref SPI_BaudRate_Prescaler
                                     @note The communication clock is derived from the master
                                     clock. The slave clock does not need to be set. */

    uint32_t FirstBit; /*!< Specifies whether data transfers start from MSB or LSB bit.
                            This parameter can be a value of @ref SPI_MSB_LSB_transmission */

    uint32_t TIMode; /*!< Specifies if the TI mode is enabled or not.
                          This parameter can be a value of @ref SPI_TI_mode */

    uint32_t CRCCalculation; /*!< Specifies if the CRC calculation is enabled or not.
                                  This parameter can be a value of @ref SPI_CRC_Calculation */

    uint32_t CRCPolynomial; /*!< Specifies the polynomial used for the CRC calculation.
                                 This parameter must be an odd number between Min_Data = 1 and
                               Max_Data = 65535 */
} SPI_InitTypeDef;

/**
 * @brief  SPI handle Structure definition
 */
typedef struct __SPI_HandleTypeDef
{
    SPI_TypeDef* Instance; /*!< SPI registers base address               */

    SPI_InitTypeDef Init; /*!< SPI communication parameters             */

} SPI_HandleTypeDef;

typedef enum
{
    HAL_OK = 0,
    HAL_ERROR,
    HAL_BUSY,
    HAL_TIMEOUT
} HAL_StatusTypeDef;

/**-----------------------------------------------------------------------------
 *  Public Variables
 *------------------------------------------------------------------------------
 */

SPI_HandleTypeDef hspi1;
SPI_HandleTypeDef hspi4;

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */
/**
 * @brief  Initialize the SPI according to the specified parameters
 *         in the SPI_InitTypeDef and initialize the associated handle.
 * @param  hspi pointer to a SPI_HandleTypeDef structure that contains
 *               the configuration information for SPI module.
 * @retval HAL status
 */
HAL_StatusTypeDef HAL_SPI_Init( SPI_HandleTypeDef* hspi );

// NOLINTEND

#ifdef __cplusplus
}
#endif

#endif /* HW_QSPI_MOCKS_H */
