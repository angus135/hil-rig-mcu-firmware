/******************************************************************************
 *  File:       hw_uart_mocks.h
 *  Author:     Angus Corr
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
    uint32_t NDTR;
} DMA_Stream_TypeDef;

/**-----------------------------------------------------------------------------
 *  Mock Peripheral Instances
 *------------------------------------------------------------------------------
 */

// USART mocks
static USART_TypeDef USART1_mock = { 0U };
static USART_TypeDef USART2_mock = { 0U };

#define USART1 ( &USART1_mock )
#define USART2 ( &USART2_mock )

// DMA stream mocks
static DMA_Stream_TypeDef DMA2_Stream2_mock = { 0U };
static DMA_Stream_TypeDef DMA2_Stream7_mock = { 0U };
static DMA_Stream_TypeDef DMA1_Stream5_mock = { 0U };
static DMA_Stream_TypeDef DMA1_Stream6_mock = { 0U };

#define DMA2_Stream2 ( &DMA2_Stream2_mock )
#define DMA2_Stream7 ( &DMA2_Stream7_mock )
#define DMA1_Stream5 ( &DMA1_Stream5_mock )
#define DMA1_Stream6 ( &DMA1_Stream6_mock )

// hUart handle mocks
static UART_HandleTypeDef huart1 = { .Instance = USART1 };
static UART_HandleTypeDef huart2 = { .Instance = USART2 };
/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

HAL_StatusTypeDef HAL_UART_Init( UART_HandleTypeDef* huart );
HAL_StatusTypeDef HAL_UART_Transmit_DMA( UART_HandleTypeDef* huart, uint8_t* pData, uint16_t Size );
HAL_StatusTypeDef HAL_UART_Receive_DMA( UART_HandleTypeDef* huart, uint8_t* pData, uint16_t Size );

// NOLINTEND

#ifdef __cplusplus
}
#endif

#endif /* HW_UART_MOCKS_H */
