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

/* Provide a unique "Instance" value for USART3 */
#define USART3 ( ( void* )0x40004800u )

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

extern UART_HandleTypeDef huart3;

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
