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
 *      1. HW_UART_Read_Byte( ... ) is not implemented as it is not needed for the current use case,
 *      but could be added in the future if desired. The HW_UART_Try_Read_Byte( ... ) function
 *provides a non-blocking way to read bytes from the UART receive FIFO.
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
#include <stddef.h>

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */
#define MAXIMUM_WAIT_MS                                                                            \
    10000  // Maximum time UART will block waiting for sending or receiving to be successful
#define UART_RX_FIFO_SIZE 32U  // Size of the uart rx fifo buffer
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
static volatile bool uart_port_tx_dma_status[UART_PORT_NUMBER] = { 0 };

// Simple software FIFO buffers for received bytes, one per UART port
static uint8_t         s_uart_rx_fifo[UART_PORT_NUMBER][UART_RX_FIFO_SIZE];
static volatile size_t s_uart_rx_head[UART_PORT_NUMBER] = { 0U };
static volatile size_t s_uart_rx_tail[UART_PORT_NUMBER] = { 0U };

// Temporary storage for the current byte being received via DMA, one per UART port
static uint8_t       s_uart_rx_dma_byte[UART_PORT_NUMBER] = { 0U };
static volatile bool s_uart_rx_active[UART_PORT_NUMBER]   = { false };
static volatile bool s_uart_rx_overflow[UART_PORT_NUMBER] = { false };

/**-----------------------------------------------------------------------------
 *  Private (static) Function Prototypes
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

/**
 * @brief Get the UART handle for a specific port.
 *
 * @param port The UART port to get the handle for.
 * @return UART_HandleTypeDef* The UART handle for the specified port, or NULL if invalid.
 */
static UART_HandleTypeDef* HW_UART_Get_Handle( UARTPort_T port )
{
    switch ( port )
    {
        case UART_CONSOLE:
            return &huart3;

        default:
            return NULL;
    }
}

/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */

/**
 * @brief Start the UART receive service for a specific port.
 *
 * @param port The UART port to start the receive service for.
 * @return UARTStatus_T The status of the operation.
 */

UARTStatus_T HW_UART_Start_Rx_Service( UARTPort_T port )
{
    UART_HandleTypeDef* handle = HW_UART_Get_Handle( port );
    HAL_StatusTypeDef   status = HAL_ERROR;

    if ( handle == NULL )
    {
        return UART_ERROR;
    }

    s_uart_rx_head[port]     = 0U;
    s_uart_rx_tail[port]     = 0U;
    s_uart_rx_overflow[port] = false;
    s_uart_rx_active[port]   = true;

    status = HAL_UART_Receive_DMA( handle, &s_uart_rx_dma_byte[port], 1U );

    if ( status == HAL_OK )
    {
        return UART_SUCCESS;
    }
    if ( status == HAL_BUSY )
    {
        return UART_BUSY;
    }
    if ( status == HAL_TIMEOUT )
    {
        return UART_TIMEOUT;
    }

    return UART_ERROR;
}

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

    UARTPort_T          port   = UART_PORT_NUMBER;
    UART_HandleTypeDef* handle = NULL;

    // Determine which UART port triggered the callback
    if ( huart->Instance == USART3 )
    {
        port = UART_CONSOLE;
    }
    else
    {
        return;  // Unknown UART instance, ignore
    }

    // Check for overflow condition before writing to the FIFO
    size_t next_head = ( s_uart_rx_head[port] + 1U ) % UART_RX_FIFO_SIZE;
    if ( next_head == s_uart_rx_tail[port] )
    {
        // FIFO is full, set overflow flag and discard the byte
        s_uart_rx_overflow[port] = true;
    }
    else
    {
        // Store the received byte in the FIFO and update head index
        s_uart_rx_fifo[port][s_uart_rx_head[port]] = s_uart_rx_dma_byte[port];
        s_uart_rx_head[port]                       = next_head;
    }

    handle = HW_UART_Get_Handle( port );
    if ( handle != NULL && s_uart_rx_active[port] )
    {
        // Restart the DMA reception for the next byte
        ( void )HAL_UART_Receive_DMA( handle, &s_uart_rx_dma_byte[port], 1U );
    }
}

/**
 * @brief Try to read a byte from the UART receive FIFO.
 *
 * @param port The UART port to read from.
 * @param byte Pointer to the byte to store the received data.
 * @return true if a byte was read, false if no byte was available.
 */
bool HW_UART_Try_Read_Byte( UARTPort_T port, uint8_t* byte )
{
    if ( port >= UART_PORT_NUMBER )
    {
        return false;
    }
    if ( byte == NULL )
    {
        return false;
    }

    if ( s_uart_rx_head[port] == s_uart_rx_tail[port] )
    {
        return false;
    }

    *byte                = s_uart_rx_fifo[port][s_uart_rx_tail[port]];
    s_uart_rx_tail[port] = ( s_uart_rx_tail[port] + 1U ) % UART_RX_FIFO_SIZE;

    return true;
}

UARTStatus_T HW_UART_Write_Byte( UARTPort_T port, uint8_t byte )
{
    UART_HandleTypeDef* handle = HW_UART_Get_Handle( port );
    HAL_StatusTypeDef   status = HAL_ERROR;

    if ( handle == NULL )
    {
        return UART_ERROR;
    }

    uart_port_tx_dma_status[port] = false;
    status                        = HAL_UART_Transmit_DMA( handle, &byte, 1U );

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
