/******************************************************************************
 *  File:       hw_uart.c
 *  Author:     Angus Corr
 *  Created:    16-Dec-2025
 *
 *  Description:
 *      <Short description of the module's purpose and responsibilities>
 *
 *  Notes:
 *      <Any design notes, dependencies, or assumptions go here>
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#ifdef TEST_BUILD
#include "tests/hw_uart_mocks.h"
#else
#include "usart.h"
#include "stm32f446xx.h"
#endif
#include "hw_uart.h"
#include "rtos_config.h"
#include <stdint.h>
#include <stdbool.h>

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */
#define MAXIMUM_WAIT_MS                                                                            \
    10000  // Maximum time UART will block waiting for sending or receiving to be successful

/**-----------------------------------------------------------------------------
 *  Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Public (global) and Extern Variables
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Private (static) Variables
 *------------------------------------------------------------------------------
 */
// Note UARTPort_T enum value corresponds to the boolean status
static volatile bool uart_port_rx_dma_status[UART_PORT_NUMBER] = { 0 };
static volatile bool uart_port_tx_dma_status[UART_PORT_NUMBER] = { 0 };

/**-----------------------------------------------------------------------------
 *  Private (static) Function Prototypes
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */

// ISRs

void HAL_UART_TxCpltCallback( UART_HandleTypeDef* huart )
{
    if ( huart->Instance == USART3 )
    {
        uart_port_tx_dma_status[UART_CONSOLE] = true;
    }
}

void HAL_UART_RxCpltCallback( UART_HandleTypeDef* huart )
{
    if ( huart->Instance == USART3 )
    {
        uart_port_rx_dma_status[UART_CONSOLE] = true;
    }
}

UARTStatus_T HW_UART_Read_Byte( UARTPort_T port, uint8_t* byte )
{
    HAL_StatusTypeDef status      = HAL_ERROR;
    uart_port_rx_dma_status[port] = false;
    switch ( port )
    {
        case UART_CONSOLE:

            status = HAL_UART_Receive_DMA( &huart3, byte, 1 );
            break;

        default:
            return UART_ERROR;
            break;
    }
    if ( status != HAL_OK )
    {
        switch ( status )
        {
            case HAL_ERROR:
                return UART_ERROR;
                break;

            case HAL_BUSY:
                return UART_BUSY;
                break;

            case HAL_TIMEOUT:
                return UART_TIMEOUT;
                break;

            default:
                return UART_ERROR;
                break;
        }
    }
    uint32_t wait_iterations = 0;
    while ( !uart_port_rx_dma_status[port] && wait_iterations < MAXIMUM_WAIT_MS )
    {
        vTaskDelay( pdMS_TO_TICKS( 1 ) );
        wait_iterations++;
    }
    if ( wait_iterations >= MAXIMUM_WAIT_MS )
    {
        return UART_TIMEOUT;
    }
    else
    {
        return UART_SUCCESS;
    }
}

UARTStatus_T HW_UART_Write_Byte( UARTPort_T port, uint8_t byte )
{
    HAL_StatusTypeDef status      = HAL_ERROR;
    uart_port_tx_dma_status[port] = false;
    switch ( port )
    {
        case UART_CONSOLE:

            status = HAL_UART_Transmit_DMA( &huart3, &byte, 1 );
            break;
        default:
            return UART_ERROR;
            break;
    }
    if ( status != HAL_OK )
    {
        switch ( status )
        {
            case HAL_ERROR:
                return UART_ERROR;
                break;

            case HAL_BUSY:
                return UART_BUSY;
                break;

            case HAL_TIMEOUT:
                return UART_TIMEOUT;
                break;

            default:
                return UART_ERROR;
                break;
        }
    }
    uint32_t wait_iterations = 0;
    while ( !uart_port_tx_dma_status[port] && wait_iterations < MAXIMUM_WAIT_MS )
    {
        vTaskDelay( pdMS_TO_TICKS( 1 ) );
        wait_iterations++;
    }
    if ( wait_iterations >= MAXIMUM_WAIT_MS )
    {
        return UART_TIMEOUT;
    }
    else
    {
        return UART_SUCCESS;
    }
}
