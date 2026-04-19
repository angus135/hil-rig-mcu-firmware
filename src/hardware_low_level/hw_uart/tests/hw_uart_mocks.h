/******************************************************************************
 *  File:       hw_uart_mocks.h
 *  Author:     Callum Rafferty
 *  Created:    21-Dec-2025
 *
 *  Description:
 *      Mock definitions of HAL types and functions for unit testing hw_uart module.
 *
 *  Notes:
 *
 ******************************************************************************/

#ifndef HW_UART_MOCKS_H
#define HW_UART_MOCKS_H

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

#define GPIO_PIN_0 ( ( uint16_t )0x0001 )
#define GPIO_PIN_1 ( ( uint16_t )0x0002 )
#define GPIO_PIN_2 ( ( uint16_t )0x0004 )
#define GPIO_PIN_3 ( ( uint16_t )0x0008 )
#define GPIO_PIN_4 ( ( uint16_t )0x0010 )
#define GPIO_PIN_5 ( ( uint16_t )0x0020 )
#define GPIO_PIN_6 ( ( uint16_t )0x0040 )
#define GPIO_PIN_7 ( ( uint16_t )0x0080 )

#define USART_CR1_RXNEIE ( 1U << 5 )
#define USART_CR1_TXEIE ( 1U << 7 )

#define USART_SR_RXNE ( 1U << 5 )
#define USART_SR_TXE ( 1U << 7 )

#define UART_IT_RXNE USART_CR1_RXNEIE
#define UART_IT_TXE USART_CR1_TXEIE

/**-----------------------------------------------------------------------------
 *  Public Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */
typedef struct
{
    volatile uint32_t SR;   /*!< USART Status register,                Address offset: 0x00 */
    volatile uint32_t DR;   /*!< USART Data register,                  Address offset: 0x04 */
    volatile uint32_t BRR;  /*!< USART Baud rate register,             Address offset: 0x08 */
    volatile uint32_t CR1;  /*!< USART Control register 1,             Address offset: 0x0C */
    volatile uint32_t CR2;  /*!< USART Control register 2,             Address offset: 0x10 */
    volatile uint32_t CR3;  /*!< USART Control register 3,             Address offset: 0x14 */
    volatile uint32_t GTPR; /*!< USART Guard time and prescaler reg,   Address offset: 0x18 */
} USART_TypeDef;

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
    USART_TypeDef*   Instance;
    UART_InitTypeDef Init;
} UART_HandleTypeDef;

typedef enum
{
    HAL_OK = 0,
    HAL_ERROR,
    HAL_BUSY,
    HAL_TIMEOUT
} HAL_StatusTypeDef;

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

/**-----------------------------------------------------------------------------
 *  Mock Peripheral Instances
 *------------------------------------------------------------------------------
 */

// USART mocks
static USART_TypeDef USART6_mock = { 0U, 0U, 0U, 0U, 0U, 0U, 0U };
static USART_TypeDef USART2_mock = { 0U, 0U, 0U, 0U, 0U, 0U, 0U };
static USART_TypeDef USART3_mock = { 0U, 0U, 0U, 0U, 0U, 0U, 0U };

#define USART6 ( &USART6_mock )
#define USART2 ( &USART2_mock )
// Console
#define USART3 ( &USART3_mock )

// DMA controller mocks
extern DMA_TypeDef fake_dma1;
extern DMA_TypeDef fake_dma2;

#define DMA1 ( &fake_dma1 )
#define DMA2 ( &fake_dma2 )

// DMA stream mocks
static DMA_Stream_TypeDef DMA2_Stream1_mock = { 0U, 0U, 0U, 0U, 0U };
static DMA_Stream_TypeDef DMA2_Stream6_mock = { 0U, 0U, 0U, 0U, 0U };
static DMA_Stream_TypeDef DMA1_Stream5_mock = { 0U, 0U, 0U, 0U, 0U };
static DMA_Stream_TypeDef DMA1_Stream6_mock = { 0U, 0U, 0U, 0U, 0U };
static DMA_Stream_TypeDef DMA1_Stream1_mock = { 0U, 0U, 0U, 0U, 0U };
static DMA_Stream_TypeDef DMA1_Stream3_mock = { 0U, 0U, 0U, 0U, 0U };

#define DMA2_Stream1 ( &DMA2_Stream1_mock )
#define DMA2_Stream6 ( &DMA2_Stream6_mock )
#define DMA1_Stream5 ( &DMA1_Stream5_mock )
#define DMA1_Stream6 ( &DMA1_Stream6_mock )
// Console
#define DMA1_Stream1 ( &DMA1_Stream1_mock )
#define DMA1_Stream3 ( &DMA1_Stream3_mock )

// LL stream constants
#define LL_DMA_STREAM_1 1U
#define LL_DMA_STREAM_3 3U
#define LL_DMA_STREAM_5 5U
#define LL_DMA_STREAM_6 6U

// hUart handle mocks
static UART_HandleTypeDef huart6 = { 0 };
static UART_HandleTypeDef huart2 = { 0 };
static UART_HandleTypeDef huart3 = { 0 };

// IFCR bit mask mocks
#define DMA_HIFCR_CTCIF6 ( 1U << 0 )
#define DMA_HIFCR_CTEIF6 ( 1U << 1 )
#define DMA_HIFCR_CFEIF6 ( 1U << 2 )
#define DMA_HIFCR_CDMEIF6 ( 1U << 3 )
#define DMA_HIFCR_CHTIF6 ( 1U << 4 )

#define DMA_LIFCR_CTCIF3 ( 1U << 5 )
#define DMA_LIFCR_CTEIF3 ( 1U << 6 )
#define DMA_LIFCR_CFEIF3 ( 1U << 7 )
#define DMA_LIFCR_CDMEIF3 ( 1U << 8 )
#define DMA_LIFCR_CHTIF3 ( 1U << 9 )

#define DMA_LISR_TCIF3 ( 1U << 0 )
#define DMA_LISR_TEIF3 ( 1U << 1 )
#define DMA_HISR_TCIF6 ( 1U << 2 )
#define DMA_HISR_TEIF6 ( 1U << 3 )

// LL register helper macros

#define DMA_SxCR_EN ( 1U << 0 )
#define DMA_SxCR_HTIE ( 1U << 2 )
#define DMA_SxCR_TCIE ( 1U << 4 )
#define DMA_SxCR_TEIE ( 1U << 5 )
#define USART_CR3_DMAT ( 1U << 7 )

#define SET_BIT( REG, BIT ) ( ( REG ) |= ( BIT ) )
#define CLEAR_BIT( REG, BIT ) ( ( REG ) &= ~( BIT ) )

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

HAL_StatusTypeDef HAL_UART_Init( UART_HandleTypeDef* huart );
HAL_StatusTypeDef HAL_UART_Transmit_DMA( UART_HandleTypeDef* huart, uint8_t* pData, uint16_t Size );
HAL_StatusTypeDef HAL_UART_Receive_DMA( UART_HandleTypeDef* huart, uint8_t* pData, uint16_t Size );
HAL_StatusTypeDef HAL_UART_DMAStop( UART_HandleTypeDef* huart );

// LL function prototypes
void     LL_DMA_DisableStream( DMA_TypeDef* dma, uint32_t stream );
void     LL_DMA_EnableStream( DMA_TypeDef* dma, uint32_t stream );
uint32_t LL_DMA_IsEnabledStream( DMA_TypeDef* dma, uint32_t stream );
void     LL_USART_EnableDMAReq_TX( USART_TypeDef* usart );
void     LL_USART_DisableDMAReq_TX( USART_TypeDef* usart );

uint32_t LL_DMA_IsActiveFlag_TC3( DMA_TypeDef* dma );
uint32_t LL_DMA_IsActiveFlag_TE3( DMA_TypeDef* dma );
uint32_t LL_DMA_IsActiveFlag_TC6( DMA_TypeDef* dma );
uint32_t LL_DMA_IsActiveFlag_TE6( DMA_TypeDef* dma );

int  HAL_UART_Receive_IT( UART_HandleTypeDef* huart, uint8_t* data, uint16_t size );
int  HAL_UART_Transmit( UART_HandleTypeDef* huart, uint8_t* data, uint16_t size, uint32_t timeout );
void HAL_UART_IRQHandler( UART_HandleTypeDef* huart );

void LL_DMA_SetMemoryAddress( DMA_TypeDef* dma, uint32_t stream, uint32_t address );
void LL_DMA_SetPeriphAddress( DMA_TypeDef* dma, uint32_t stream, uint32_t address );
void LL_DMA_SetDataLength( DMA_TypeDef* dma, uint32_t stream, uint32_t length );

void LL_DMA_DisableIT_HT( DMA_TypeDef* dma, uint32_t stream );
void LL_DMA_EnableIT_TC( DMA_TypeDef* dma, uint32_t stream );
void LL_DMA_EnableIT_TE( DMA_TypeDef* dma, uint32_t stream );

// NOLINTEND

#ifdef __cplusplus
}
#endif

#endif /* HW_UART_MOCKS_H */
