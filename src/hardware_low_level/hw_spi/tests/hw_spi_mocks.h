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

// DMA related defines
#define DMA2 ( ( void* )0x40026400U )
#define LL_DMA_STREAM_0 0x00000000U
#define LL_DMA_STREAM_1 0x00000001U
#define LL_DMA_STREAM_2 0x00000002U
#define LL_DMA_STREAM_3 0x00000003U

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

typedef struct
{
    uint32_t LISR;  /*!< DMA low interrupt status register,      Address offset: 0x00 */
    uint32_t HISR;  /*!< DMA high interrupt status register,     Address offset: 0x04 */
    uint32_t LIFCR; /*!< DMA low interrupt flag clear register,  Address offset: 0x08 */
    uint32_t HIFCR; /*!< DMA high interrupt flag clear register, Address offset: 0x0C */
} DMA_TypeDef;

typedef enum
{
    HAL_OK = 0,
    HAL_ERROR,
    HAL_BUSY,
    HAL_TIMEOUT
} HAL_StatusTypeDef;

typedef enum
{
    /******  Cortex-M4 Processor Exceptions Numbers
     ****************************************************************/
    NonMaskableInt_IRQn   = -14, /*!< 2 Non Maskable Interrupt */
    MemoryManagement_IRQn = -12, /*!< 4 Cortex-M4 Memory Management Interrupt */
    BusFault_IRQn   = -11, /*!< 5 Cortex-M4 Bus Fault Interrupt                                   */
    UsageFault_IRQn = -10, /*!< 6 Cortex-M4 Usage Fault Interrupt                                 */
    SVCall_IRQn     = -5,  /*!< 11 Cortex-M4 SV Call Interrupt                                    */
    DebugMonitor_IRQn = -4, /*!< 12 Cortex-M4 Debug Monitor Interrupt */
    PendSV_IRQn  = -2, /*!< 14 Cortex-M4 Pend SV Interrupt                                    */
    SysTick_IRQn = -1, /*!< 15 Cortex-M4 System Tick Interrupt                                */
    /******  STM32 specific Interrupt Numbers
     **********************************************************************/
    WWDG_IRQn       = 0,  /*!< Window WatchDog Interrupt                                         */
    PVD_IRQn        = 1,  /*!< PVD through EXTI Line detection Interrupt                         */
    TAMP_STAMP_IRQn = 2,  /*!< Tamper and TimeStamp interrupts through the EXTI line             */
    RTC_WKUP_IRQn   = 3,  /*!< RTC Wakeup interrupt through the EXTI line                        */
    FLASH_IRQn      = 4,  /*!< FLASH global Interrupt                                            */
    RCC_IRQn        = 5,  /*!< RCC global Interrupt                                              */
    EXTI0_IRQn      = 6,  /*!< EXTI Line0 Interrupt                                              */
    EXTI1_IRQn      = 7,  /*!< EXTI Line1 Interrupt                                              */
    EXTI2_IRQn      = 8,  /*!< EXTI Line2 Interrupt                                              */
    EXTI3_IRQn      = 9,  /*!< EXTI Line3 Interrupt                                              */
    EXTI4_IRQn      = 10, /*!< EXTI Line4 Interrupt                                              */
    DMA1_Stream0_IRQn = 11, /*!< DMA1 Stream 0 global Interrupt */
    DMA1_Stream1_IRQn = 12, /*!< DMA1 Stream 1 global Interrupt */
    DMA1_Stream2_IRQn = 13, /*!< DMA1 Stream 2 global Interrupt */
    DMA1_Stream3_IRQn = 14, /*!< DMA1 Stream 3 global Interrupt */
    DMA1_Stream4_IRQn = 15, /*!< DMA1 Stream 4 global Interrupt */
    DMA1_Stream5_IRQn = 16, /*!< DMA1 Stream 5 global Interrupt */
    DMA1_Stream6_IRQn = 17, /*!< DMA1 Stream 6 global Interrupt */
    ADC_IRQn      = 18, /*!< ADC1, ADC2 and ADC3 global Interrupts                             */
    CAN1_TX_IRQn  = 19, /*!< CAN1 TX Interrupt                                                 */
    CAN1_RX0_IRQn = 20, /*!< CAN1 RX0 Interrupt                                                */
    CAN1_RX1_IRQn = 21, /*!< CAN1 RX1 Interrupt                                                */
    CAN1_SCE_IRQn = 22, /*!< CAN1 SCE Interrupt                                                */
    EXTI9_5_IRQn  = 23, /*!< External Line[9:5] Interrupts                                     */
    TIM1_BRK_TIM9_IRQn = 24, /*!< TIM1 Break interrupt and TIM9 global interrupt */
    TIM1_UP_TIM10_IRQn = 25, /*!< TIM1 Update Interrupt and TIM10 global interrupt */
    TIM1_TRG_COM_TIM11_IRQn =
        26,                /*!< TIM1 Trigger and Commutation Interrupt and TIM11 global interrupt */
    TIM1_CC_IRQn     = 27, /*!< TIM1 Capture Compare Interrupt                                    */
    TIM2_IRQn        = 28, /*!< TIM2 global Interrupt                                             */
    TIM3_IRQn        = 29, /*!< TIM3 global Interrupt                                             */
    TIM4_IRQn        = 30, /*!< TIM4 global Interrupt                                             */
    I2C1_EV_IRQn     = 31, /*!< I2C1 Event Interrupt                                              */
    I2C1_ER_IRQn     = 32, /*!< I2C1 Error Interrupt                                              */
    I2C2_EV_IRQn     = 33, /*!< I2C2 Event Interrupt                                              */
    I2C2_ER_IRQn     = 34, /*!< I2C2 Error Interrupt                                              */
    SPI1_IRQn        = 35, /*!< SPI1 global Interrupt                                             */
    SPI2_IRQn        = 36, /*!< SPI2 global Interrupt                                             */
    USART1_IRQn      = 37, /*!< USART1 global Interrupt                                           */
    USART2_IRQn      = 38, /*!< USART2 global Interrupt                                           */
    USART3_IRQn      = 39, /*!< USART3 global Interrupt                                           */
    EXTI15_10_IRQn   = 40, /*!< External Line[15:10] Interrupts                                   */
    RTC_Alarm_IRQn   = 41, /*!< RTC Alarm (A and B) through EXTI Line Interrupt                   */
    OTG_FS_WKUP_IRQn = 42, /*!< USB OTG FS Wakeup through EXTI line interrupt                     */
    TIM8_BRK_TIM12_IRQn = 43, /*!< TIM8 Break Interrupt and TIM12 global interrupt */
    TIM8_UP_TIM13_IRQn  = 44, /*!< TIM8 Update Interrupt and TIM13 global interrupt */
    TIM8_TRG_COM_TIM14_IRQn =
        45,            /*!< TIM8 Trigger and Commutation Interrupt and TIM14 global interrupt */
    TIM8_CC_IRQn = 46, /*!< TIM8 Capture Compare global interrupt                             */
    DMA1_Stream7_IRQn = 47, /*!< DMA1 Stream7 Interrupt */
    FMC_IRQn      = 48, /*!< FMC global Interrupt                                              */
    SDIO_IRQn     = 49, /*!< SDIO global Interrupt                                             */
    TIM5_IRQn     = 50, /*!< TIM5 global Interrupt                                             */
    SPI3_IRQn     = 51, /*!< SPI3 global Interrupt                                             */
    UART4_IRQn    = 52, /*!< UART4 global Interrupt                                            */
    UART5_IRQn    = 53, /*!< UART5 global Interrupt                                            */
    TIM6_DAC_IRQn = 54, /*!< TIM6 global and DAC1&2 underrun error  interrupts                 */
    TIM7_IRQn     = 55, /*!< TIM7 global interrupt                                             */
    DMA2_Stream0_IRQn = 56, /*!< DMA2 Stream 0 global Interrupt */
    DMA2_Stream1_IRQn = 57, /*!< DMA2 Stream 1 global Interrupt */
    DMA2_Stream2_IRQn = 58, /*!< DMA2 Stream 2 global Interrupt */
    DMA2_Stream3_IRQn = 59, /*!< DMA2 Stream 3 global Interrupt */
    DMA2_Stream4_IRQn = 60, /*!< DMA2 Stream 4 global Interrupt */
    CAN2_TX_IRQn  = 63, /*!< CAN2 TX Interrupt                                                 */
    CAN2_RX0_IRQn = 64, /*!< CAN2 RX0 Interrupt                                                */
    CAN2_RX1_IRQn = 65, /*!< CAN2 RX1 Interrupt                                                */
    CAN2_SCE_IRQn = 66, /*!< CAN2 SCE Interrupt                                                */
    OTG_FS_IRQn   = 67, /*!< USB OTG FS global Interrupt                                       */
    DMA2_Stream5_IRQn = 68, /*!< DMA2 Stream 5 global interrupt */
    DMA2_Stream6_IRQn = 69, /*!< DMA2 Stream 6 global interrupt */
    DMA2_Stream7_IRQn = 70, /*!< DMA2 Stream 7 global interrupt */
    USART6_IRQn  = 71, /*!< USART6 global interrupt                                           */
    I2C3_EV_IRQn = 72, /*!< I2C3 event interrupt                                              */
    I2C3_ER_IRQn = 73, /*!< I2C3 error interrupt                                              */
    OTG_HS_EP1_OUT_IRQn = 74, /*!< USB OTG HS End Point 1 Out global interrupt */
    OTG_HS_EP1_IN_IRQn  = 75, /*!< USB OTG HS End Point 1 In global interrupt */
    OTG_HS_WKUP_IRQn = 76, /*!< USB OTG HS Wakeup through EXTI interrupt                          */
    OTG_HS_IRQn      = 77, /*!< USB OTG HS global interrupt                                       */
    DCMI_IRQn        = 78, /*!< DCMI global interrupt                                             */
    FPU_IRQn         = 81, /*!< FPU global interrupt                                              */
    SPI4_IRQn        = 84, /*!< SPI4 global Interrupt                                             */
    SAI1_IRQn        = 87, /*!< SAI1 global Interrupt                                             */
    SAI2_IRQn        = 91, /*!< SAI2 global Interrupt                                             */
    QUADSPI_IRQn     = 92, /*!< QuadSPI global Interrupt                                          */
    CEC_IRQn         = 93, /*!< CEC global Interrupt                                              */
    SPDIF_RX_IRQn   = 94, /*!< SPDIF-RX global Interrupt                                          */
    FMPI2C1_EV_IRQn = 95, /*!< FMPI2C1 Event Interrupt                                           */
    FMPI2C1_ER_IRQn = 96  /*!< FMPI2C1 Error Interrupt                                           */
} IRQn_Type;

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

/**
 * @brief  Receive an amount of data in non-blocking mode with DMA.
 * @note   In case of MASTER mode and SPI_DIRECTION_2LINES direction, hdmatx shall be defined.
 * @param  hspi pointer to a SPI_HandleTypeDef structure that contains
 *               the configuration information for SPI module.
 * @param  pData pointer to data buffer (u8 or u16 data elements)
 * @note   When the CRC feature is enabled the pData Length must be Size + 1.
 * @param  Size amount of data elements (u8 or u16) to be received
 * @retval HAL status
 */
HAL_StatusTypeDef HAL_SPI_Receive_DMA( SPI_HandleTypeDef* hspi, uint8_t* pData, uint16_t Size );

/**
 * @brief  Stop the DMA Transfer.
 * @param  hspi pointer to a SPI_HandleTypeDef structure that contains
 *               the configuration information for the specified SPI module.
 * @retval HAL status
 */
HAL_StatusTypeDef HAL_SPI_DMAStop( SPI_HandleTypeDef* hspi );

/**
 * @brief Get Number of data to transfer.
 * @rmtoll NDTR          NDT           LL_DMA_GetDataLength
 * @note   Once the stream is enabled, the return value indicate the
 *         remaining bytes to be transmitted.
 * @param  DMAx DMAx Instance
 * @param  Stream This parameter can be one of the following values:
 *         @arg @ref LL_DMA_STREAM_0
 *         @arg @ref LL_DMA_STREAM_1
 *         @arg @ref LL_DMA_STREAM_2
 *         @arg @ref LL_DMA_STREAM_3
 *         @arg @ref LL_DMA_STREAM_4
 *         @arg @ref LL_DMA_STREAM_5
 *         @arg @ref LL_DMA_STREAM_6
 *         @arg @ref LL_DMA_STREAM_7
 * @retval Between 0 to 0xFFFFFFFF
 */
uint32_t LL_DMA_GetDataLength( void* DMAx, uint32_t Stream );

/**
 * @brief Disable DMA stream.
 * @rmtoll CR          EN            LL_DMA_DisableStream
 * @param  DMAx DMAx Instance
 * @param  Stream This parameter can be one of the following values:
 *         @arg @ref LL_DMA_STREAM_0
 *         @arg @ref LL_DMA_STREAM_1
 *         @arg @ref LL_DMA_STREAM_2
 *         @arg @ref LL_DMA_STREAM_3
 *         @arg @ref LL_DMA_STREAM_4
 *         @arg @ref LL_DMA_STREAM_5
 *         @arg @ref LL_DMA_STREAM_6
 *         @arg @ref LL_DMA_STREAM_7
 * @retval None
 */
void LL_DMA_DisableStream( DMA_TypeDef* DMAx, uint32_t Stream );

/**
 * @brief Check if DMA stream is enabled or disabled.
 * @rmtoll CR          EN            LL_DMA_IsEnabledStream
 * @param  DMAx DMAx Instance
 * @param  Stream This parameter can be one of the following values:
 *         @arg @ref LL_DMA_STREAM_0
 *         @arg @ref LL_DMA_STREAM_1
 *         @arg @ref LL_DMA_STREAM_2
 *         @arg @ref LL_DMA_STREAM_3
 *         @arg @ref LL_DMA_STREAM_4
 *         @arg @ref LL_DMA_STREAM_5
 *         @arg @ref LL_DMA_STREAM_6
 *         @arg @ref LL_DMA_STREAM_7
 * @retval State of bit (1 or 0).
 */
uint32_t LL_DMA_IsEnabledStream( DMA_TypeDef* DMAx, uint32_t Stream );

/**
 * @brief  Set the Memory address.
 * @rmtoll M0AR        M0A         LL_DMA_SetMemoryAddress
 * @note   Interface used for direction LL_DMA_DIRECTION_PERIPH_TO_MEMORY or
 * LL_DMA_DIRECTION_MEMORY_TO_PERIPH only.
 * @note   This API must not be called when the DMA channel is enabled.
 * @param  DMAx DMAx Instance
 * @param  Stream This parameter can be one of the following values:
 *         @arg @ref LL_DMA_STREAM_0
 *         @arg @ref LL_DMA_STREAM_1
 *         @arg @ref LL_DMA_STREAM_2
 *         @arg @ref LL_DMA_STREAM_3
 *         @arg @ref LL_DMA_STREAM_4
 *         @arg @ref LL_DMA_STREAM_5
 *         @arg @ref LL_DMA_STREAM_6
 *         @arg @ref LL_DMA_STREAM_7
 * @param  MemoryAddress Between 0 to 0xFFFFFFFF
 * @retval None
 */
void LL_DMA_SetMemoryAddress( DMA_TypeDef* DMAx, uint32_t Stream, uint32_t MemoryAddress );

/**
 * @brief  Set the Peripheral address.
 * @rmtoll PAR        PA         LL_DMA_SetPeriphAddress
 * @note   Interface used for direction LL_DMA_DIRECTION_PERIPH_TO_MEMORY or
 * LL_DMA_DIRECTION_MEMORY_TO_PERIPH only.
 * @note   This API must not be called when the DMA channel is enabled.
 * @param  DMAx DMAx Instance
 * @param  Stream This parameter can be one of the following values:
 *         @arg @ref LL_DMA_STREAM_0
 *         @arg @ref LL_DMA_STREAM_1
 *         @arg @ref LL_DMA_STREAM_2
 *         @arg @ref LL_DMA_STREAM_3
 *         @arg @ref LL_DMA_STREAM_4
 *         @arg @ref LL_DMA_STREAM_5
 *         @arg @ref LL_DMA_STREAM_6
 *         @arg @ref LL_DMA_STREAM_7
 * @param  PeriphAddress Between 0 to 0xFFFFFFFF
 * @retval None
 */
void LL_DMA_SetPeriphAddress( DMA_TypeDef* DMAx, uint32_t Stream, uint32_t PeriphAddress );

/**
 * @brief  Get the data register address used for DMA transfer
 * @rmtoll DR           DR            LL_SPI_DMA_GetRegAddr
 * @param  SPIx SPI Instance
 * @retval Address of data register
 */
uint32_t LL_SPI_DMA_GetRegAddr( const SPI_TypeDef* SPIx );

/**
 * @brief Set Number of data to transfer.
 * @rmtoll NDTR          NDT           LL_DMA_SetDataLength
 * @note   This action has no effect if
 *         stream is enabled.
 * @param  DMAx DMAx Instance
 * @param  Stream This parameter can be one of the following values:
 *         @arg @ref LL_DMA_STREAM_0
 *         @arg @ref LL_DMA_STREAM_1
 *         @arg @ref LL_DMA_STREAM_2
 *         @arg @ref LL_DMA_STREAM_3
 *         @arg @ref LL_DMA_STREAM_4
 *         @arg @ref LL_DMA_STREAM_5
 *         @arg @ref LL_DMA_STREAM_6
 *         @arg @ref LL_DMA_STREAM_7
 * @param  NbData Between 0 to 0xFFFFFFFF
 * @retval None
 */
void LL_DMA_SetDataLength( DMA_TypeDef* DMAx, uint32_t Stream, uint32_t NbData );

/**
 * @brief  Enable DMA Tx
 * @rmtoll CR2          TXDMAEN       LL_SPI_EnableDMAReq_TX
 * @param  SPIx SPI Instance
 * @retval None
 */
void LL_SPI_EnableDMAReq_TX( SPI_TypeDef* SPIx );

/**
 * @brief Enable Transfer complete interrupt.
 * @rmtoll CR        TCIE         LL_DMA_EnableIT_TC
 * @param  DMAx DMAx Instance
 * @param  Stream This parameter can be one of the following values:
 *         @arg @ref LL_DMA_STREAM_0
 *         @arg @ref LL_DMA_STREAM_1
 *         @arg @ref LL_DMA_STREAM_2
 *         @arg @ref LL_DMA_STREAM_3
 *         @arg @ref LL_DMA_STREAM_4
 *         @arg @ref LL_DMA_STREAM_5
 *         @arg @ref LL_DMA_STREAM_6
 *         @arg @ref LL_DMA_STREAM_7
 * @retval None
 */
void LL_DMA_EnableIT_TC( DMA_TypeDef* DMAx, uint32_t Stream );

/**
 * @brief Enable Transfer error interrupt.
 * @rmtoll CR        TEIE         LL_DMA_EnableIT_TE
 * @param  DMAx DMAx Instance
 * @param  Stream This parameter can be one of the following values:
 *         @arg @ref LL_DMA_STREAM_0
 *         @arg @ref LL_DMA_STREAM_1
 *         @arg @ref LL_DMA_STREAM_2
 *         @arg @ref LL_DMA_STREAM_3
 *         @arg @ref LL_DMA_STREAM_4
 *         @arg @ref LL_DMA_STREAM_5
 *         @arg @ref LL_DMA_STREAM_6
 *         @arg @ref LL_DMA_STREAM_7
 * @retval None
 */
void LL_DMA_EnableIT_TE( DMA_TypeDef* DMAx, uint32_t Stream );

/**
 * @brief Enable DMA stream.
 * @rmtoll CR          EN            LL_DMA_EnableStream
 * @param  DMAx DMAx Instance
 * @param  Stream This parameter can be one of the following values:
 *         @arg @ref LL_DMA_STREAM_0
 *         @arg @ref LL_DMA_STREAM_1
 *         @arg @ref LL_DMA_STREAM_2
 *         @arg @ref LL_DMA_STREAM_3
 *         @arg @ref LL_DMA_STREAM_4
 *         @arg @ref LL_DMA_STREAM_5
 *         @arg @ref LL_DMA_STREAM_6
 *         @arg @ref LL_DMA_STREAM_7
 * @retval None
 */
void LL_DMA_EnableStream( DMA_TypeDef* DMAx, uint32_t Stream );

/**
  \brief   Disable Interrupt
  \details Disables a device specific interrupt in the NVIC interrupt controller.
  \param [in]      IRQn  Device specific interrupt number.
  \note    IRQn must not be negative.
 */
void NVIC_DisableIRQ( IRQn_Type IRQn );

/**
  \brief   Enable Interrupt
  \details Enables a device specific interrupt in the NVIC interrupt controller.
  \param [in]      IRQn  Device specific interrupt number.
  \note    IRQn must not be negative.
 */
void NVIC_EnableIRQ( IRQn_Type IRQn );

// DMA IRQ Handlers
void DMA2_Stream0_IRQHandler( void );
void DMA2_Stream1_IRQHandler( void );
void DMA2_Stream2_IRQHandler( void );
void DMA2_Stream3_IRQHandler( void );

// NOLINTEND

#ifdef __cplusplus
}
#endif

#endif /* HW_QSPI_MOCKS_H */
