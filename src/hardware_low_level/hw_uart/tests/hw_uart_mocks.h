/******************************************************************************
 *  File:       hw_uart_mocks.h
 *  Author:     Callum Rafferty
 *  Created:    21 Dec 2025
 *
 *  Description:
 *      Mock definitions of HAL, LL, CMSIS, USART, and DMA types and functions
 *      required for unit testing the DUT UART driver and console UART driver.
 *
 *  Notes:
 *      This header provides only the subset of STM32 HAL, LL, CMSIS, USART, and
 *      DMA definitions required by the current unit tests.
 ******************************************************************************/

#ifndef HW_UART_MOCKS_H
#define HW_UART_MOCKS_H

#ifdef __cplusplus
extern "C"
{
#endif

/* NOLINTBEGIN */

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include <stdint.h>

/**-----------------------------------------------------------------------------
 *  Public Defines / Macros
 *------------------------------------------------------------------------------
 */

/* HAL UART configuration values. */
#define UART_WORDLENGTH_8B 8U
#define UART_WORDLENGTH_9B 9U

#define UART_STOPBITS_1 1U
#define UART_STOPBITS_2 2U

#define UART_PARITY_NONE 0U
#define UART_PARITY_EVEN 1U
#define UART_PARITY_ODD 2U

#define UART_MODE_RX 0x01U
#define UART_MODE_TX 0x02U
#define UART_MODE_TX_RX ( UART_MODE_RX | UART_MODE_TX )

/* GPIO pin values used by the DUT UART static hardware selection placeholders. */
#define GPIO_PIN_0 ( ( uint16_t )0x0001 )
#define GPIO_PIN_1 ( ( uint16_t )0x0002 )
#define GPIO_PIN_2 ( ( uint16_t )0x0004 )
#define GPIO_PIN_3 ( ( uint16_t )0x0008 )
#define GPIO_PIN_4 ( ( uint16_t )0x0010 )
#define GPIO_PIN_5 ( ( uint16_t )0x0020 )
#define GPIO_PIN_6 ( ( uint16_t )0x0040 )
#define GPIO_PIN_7 ( ( uint16_t )0x0080 )

/* DMA stream control bits used by DUT DMA tests. */
#define DMA_SxCR_EN ( 1U << 0 )
#define DMA_SxCR_HTIE ( 1U << 2 )
#define DMA_SxCR_TCIE ( 1U << 4 )
#define DMA_SxCR_TEIE ( 1U << 5 )

/* HAL DMA interrupt masks used by DUT RX DMA startup. */
#define DMA_IT_TC 0x00000010U
#define DMA_IT_HT 0x00000008U
#define DMA_IT_TE 0x00000004U

/* USART control bits used by DUT TX DMA tests. */
#define USART_CR3_DMAT ( 1U << 7 )

/* DUT TX DMA stream constants. */
#define LL_DMA_STREAM_6 6U

/* DMA interrupt flag clear masks used by DUT TX DMA handlers. */
#define DMA_HIFCR_CTCIF6 ( 1U << 0 )
#define DMA_HIFCR_CTEIF6 ( 1U << 1 )
#define DMA_HIFCR_CFEIF6 ( 1U << 2 )
#define DMA_HIFCR_CDMEIF6 ( 1U << 3 )
#define DMA_HIFCR_CHTIF6 ( 1U << 4 )

/* DMA interrupt status flags used by DUT TX DMA tests. */
#define DMA_HISR_TCIF6 ( 1U << 2 )
#define DMA_HISR_TEIF6 ( 1U << 3 )

/* DMA interrupt status and clear flags for DMA2 Stream 2. */
#define DMA_LISR_FEIF2 ( 1UL << 16 )
#define DMA_LISR_DMEIF2 ( 1UL << 18 )
#define DMA_LISR_TEIF2 ( 1UL << 19 )
#define DMA_LISR_HTIF2 ( 1UL << 20 )
#define DMA_LISR_TCIF2 ( 1UL << 21 )

#define DMA_LIFCR_CFEIF2 ( 1UL << 16 )
#define DMA_LIFCR_CDMEIF2 ( 1UL << 18 )
#define DMA_LIFCR_CTEIF2 ( 1UL << 19 )
#define DMA_LIFCR_CHTIF2 ( 1UL << 20 )
#define DMA_LIFCR_CTCIF2 ( 1UL << 21 )

/* DMA interrupt status and clear flags for DMA1 Stream 5. */
#define DMA_HISR_FEIF5 ( 1UL << 6 )
#define DMA_HISR_DMEIF5 ( 1UL << 8 )
#define DMA_HISR_TEIF5 ( 1UL << 9 )
#define DMA_HISR_HTIF5 ( 1UL << 10 )
#define DMA_HISR_TCIF5 ( 1UL << 11 )

#define DMA_HIFCR_CFEIF5 ( 1UL << 6 )
#define DMA_HIFCR_CDMEIF5 ( 1UL << 8 )
#define DMA_HIFCR_CTEIF5 ( 1UL << 9 )
#define DMA_HIFCR_CHTIF5 ( 1UL << 10 )
#define DMA_HIFCR_CTCIF5 ( 1UL << 11 )

#define SET_BIT( REG, BIT ) ( ( REG ) |= ( BIT ) )
#define CLEAR_BIT( REG, BIT ) ( ( REG ) &= ~( BIT ) )

/**-----------------------------------------------------------------------------
 *  Public Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

typedef struct
{
    volatile uint32_t SR;
    volatile uint32_t DR;
    volatile uint32_t BRR;
    volatile uint32_t CR1;
    volatile uint32_t CR2;
    volatile uint32_t CR3;
    volatile uint32_t GTPR;
} USART_TypeDef;

typedef struct
{
    uint32_t CR;
    uint32_t NDTR;
    uint32_t PAR;
    uint32_t M0AR;
    uint32_t FCR;
} DMA_Stream_TypeDef;

typedef struct
{
    uint32_t LISR;
    uint32_t HISR;
    uint32_t LIFCR;
    uint32_t HIFCR;
} DMA_TypeDef;

typedef struct
{
    DMA_Stream_TypeDef* Instance;
    uint32_t            disabled_interrupt_mask;
} DMA_HandleTypeDef;

typedef struct
{
    uint32_t BaudRate;
    uint32_t WordLength;
    uint32_t StopBits;
    uint32_t Parity;
    uint32_t Mode;
} UART_InitTypeDef;

typedef struct
{
    USART_TypeDef*     Instance;
    UART_InitTypeDef   Init;
    DMA_HandleTypeDef* hdmarx;
    DMA_HandleTypeDef* hdmatx;
} UART_HandleTypeDef;

typedef enum
{
    HAL_OK = 0,
    HAL_ERROR,
    HAL_BUSY,
    HAL_TIMEOUT
} HAL_StatusTypeDef;

/**-----------------------------------------------------------------------------
 *  HAL Helper Macros
 *------------------------------------------------------------------------------
 */

#define __HAL_DMA_DISABLE_IT( __HANDLE__, __INTERRUPT__ )                                          \
    do                                                                                             \
    {                                                                                              \
        if ( ( __HANDLE__ ) != 0 )                                                                 \
        {                                                                                          \
            ( __HANDLE__ )->disabled_interrupt_mask |= ( __INTERRUPT__ );                          \
        }                                                                                          \
    } while ( 0 )

/**-----------------------------------------------------------------------------
 *  Mock Peripheral Instances
 *------------------------------------------------------------------------------
 */

/* USART mocks.
 *
 * USART6 and USART2 are used by the DUT UART driver.
 * USART3 is used by the console UART driver.
 */
static USART_TypeDef USART6_mock = { 0U, 0U, 0U, 0U, 0U, 0U, 0U };
static USART_TypeDef USART2_mock = { 0U, 0U, 0U, 0U, 0U, 0U, 0U };
static USART_TypeDef USART3_mock = { 0U, 0U, 0U, 0U, 0U, 0U, 0U };

#define USART6 ( &USART6_mock )
#define USART2 ( &USART2_mock )
#define USART3 ( &USART3_mock )

/* DMA controller mocks used by the DUT UART driver. */
extern DMA_TypeDef fake_dma1;
extern DMA_TypeDef fake_dma2;

#define DMA1 ( &fake_dma1 )
#define DMA2 ( &fake_dma2 )

/* DUT UART DMA stream mocks.
 *
 * DMA2 Stream 2 is channel 1 RX.
 * DMA2 Stream 6 is channel 1 TX.
 * DMA1 Stream 5 is channel 2 RX.
 * DMA1 Stream 6 is channel 2 TX.
 */
static DMA_Stream_TypeDef DMA2_Stream2_mock = { 0U, 0U, 0U, 0U, 0U };
static DMA_Stream_TypeDef DMA2_Stream6_mock = { 0U, 0U, 0U, 0U, 0U };
static DMA_Stream_TypeDef DMA1_Stream5_mock = { 0U, 0U, 0U, 0U, 0U };
static DMA_Stream_TypeDef DMA1_Stream6_mock = { 0U, 0U, 0U, 0U, 0U };

#define DMA2_Stream2 ( &DMA2_Stream2_mock )
#define DMA2_Stream6 ( &DMA2_Stream6_mock )
#define DMA1_Stream5 ( &DMA1_Stream5_mock )
#define DMA1_Stream6 ( &DMA1_Stream6_mock )

/* DMA handle mocks used by UART handle RX DMA fields. */
static DMA_HandleTypeDef hdma_usart6_rx = { 0 };
static DMA_HandleTypeDef hdma_usart2_rx = { 0 };
static DMA_HandleTypeDef hdma_usart3_rx = { 0 };

/* UART handle mocks.
 *
 * huart6 and huart2 are used by the DUT UART driver.
 * huart3 is used by the console UART driver.
 */
static UART_HandleTypeDef huart6 = { 0 };
static UART_HandleTypeDef huart2 = { 0 };
static UART_HandleTypeDef huart3 = { 0 };

/**-----------------------------------------------------------------------------
 *  Mock State
 *------------------------------------------------------------------------------
 */

extern uint32_t mock_primask;
extern uint32_t mock_irq_disable_count;
extern uint32_t mock_irq_enable_count;

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

/* HAL UART seams used by DUT and console UART drivers. */
HAL_StatusTypeDef HAL_UART_Init( UART_HandleTypeDef* huart );
HAL_StatusTypeDef HAL_UART_Receive_DMA( UART_HandleTypeDef* huart, uint8_t* pData, uint16_t Size );
HAL_StatusTypeDef HAL_UART_DMAStop( UART_HandleTypeDef* huart );
int               HAL_UART_Receive_IT( UART_HandleTypeDef* huart, uint8_t* data, uint16_t size );
int  HAL_UART_Transmit( UART_HandleTypeDef* huart, uint8_t* data, uint16_t size, uint32_t timeout );
void HAL_UART_IRQHandler( UART_HandleTypeDef* huart );

/* LL DMA and USART seams used by DUT TX DMA path. */
void     LL_DMA_DisableStream( DMA_TypeDef* dma, uint32_t stream );
void     LL_DMA_EnableStream( DMA_TypeDef* dma, uint32_t stream );
uint32_t LL_DMA_IsEnabledStream( DMA_TypeDef* dma, uint32_t stream );

void LL_DMA_SetMemoryAddress( DMA_TypeDef* dma, uint32_t stream, uint32_t address );
void LL_DMA_SetPeriphAddress( DMA_TypeDef* dma, uint32_t stream, uint32_t address );
void LL_DMA_SetDataLength( DMA_TypeDef* dma, uint32_t stream, uint32_t length );

void LL_DMA_DisableIT_HT( DMA_TypeDef* dma, uint32_t stream );
void LL_DMA_EnableIT_TC( DMA_TypeDef* dma, uint32_t stream );
void LL_DMA_EnableIT_TE( DMA_TypeDef* dma, uint32_t stream );

uint32_t LL_DMA_IsActiveFlag_TC6( DMA_TypeDef* dma );
uint32_t LL_DMA_IsActiveFlag_TE6( DMA_TypeDef* dma );

uint32_t LL_DMA_IsActiveFlag_TC2( DMA_TypeDef* dma );
uint32_t LL_DMA_IsActiveFlag_TE2( DMA_TypeDef* dma );
uint32_t LL_DMA_IsActiveFlag_DME2( DMA_TypeDef* dma );
uint32_t LL_DMA_IsActiveFlag_FE2( DMA_TypeDef* dma );
uint32_t LL_DMA_IsActiveFlag_HT2( DMA_TypeDef* dma );

uint32_t LL_DMA_IsActiveFlag_TC5( DMA_TypeDef* dma );
uint32_t LL_DMA_IsActiveFlag_TE5( DMA_TypeDef* dma );
uint32_t LL_DMA_IsActiveFlag_DME5( DMA_TypeDef* dma );
uint32_t LL_DMA_IsActiveFlag_FE5( DMA_TypeDef* dma );
uint32_t LL_DMA_IsActiveFlag_HT5( DMA_TypeDef* dma );

void LL_USART_EnableDMAReq_TX( USART_TypeDef* usart );
void LL_USART_DisableDMAReq_TX( USART_TypeDef* usart );

/* CMSIS IRQ masking seams used by DUT TX ring buffer critical sections. */
uint32_t __get_PRIMASK( void );
void     __disable_irq( void );
void     __enable_irq( void );

/* NOLINTEND */

#ifdef __cplusplus
}
#endif

#endif /* HW_UART_MOCKS_H */
