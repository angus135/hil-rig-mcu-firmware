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

/**-----------------------------------------------------------------------------
 *  Public Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

typedef struct
{
    void* Instance;
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

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

HAL_StatusTypeDef HAL_UART_Transmit_DMA( UART_HandleTypeDef* huart, uint8_t* pData, uint16_t Size );
HAL_StatusTypeDef HAL_UART_Receive_DMA( UART_HandleTypeDef* huart, uint8_t* pData, uint16_t Size );

// NOLINTEND

#ifdef __cplusplus
}
#endif

#endif /* HW_UART_MOCKS_H */
