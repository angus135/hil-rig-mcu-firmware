/******************************************************************************
 *  File:       hw_uart_console.c
 *  Author:     Callum Rafferty
 *  Created:    15-04-26
 *
 *  Description:
 *
 *
 *  Notes:
 *
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
#include "stm32f4xx_ll_usart.h"
#endif

#include "hw_uart_console.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */
#define HW_UART_CONSOLE_HANDLE ( &huart3 )
#define HW_UART_CONSOLE_INSTANCE USART3

#define HW_UART_CONSOLE_RX_BUFFER_SIZE 128U

/**-----------------------------------------------------------------------------
 *  Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

typedef struct
{
    uint32_t rx_head;
    uint32_t rx_tail;
    bool     is_initialised;
} HwUartConsoleState_T;

/**-----------------------------------------------------------------------------
 *  Public (global) and Extern Variables
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Private (static) Variables
 *------------------------------------------------------------------------------
 */

static HwUartConsoleState_T uart_console_state;
static uint8_t              uart_console_rx_buffer[HW_UART_CONSOLE_RX_BUFFER_SIZE];

/**-----------------------------------------------------------------------------
 *  Private (static) Function Definitions
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */

bool HW_UART_CONSOLE_Init( uint32_t baud_rate )
{
    UART_HandleTypeDef* huart = HW_UART_CONSOLE_HANDLE;

    huart->Init.BaudRate   = baud_rate;
    huart->Init.WordLength = UART_WORDLENGTH_8B;
    huart->Init.StopBits   = UART_STOPBITS_1;
    huart->Init.Parity     = UART_PARITY_NONE;
    huart->Init.Mode       = UART_MODE_TX_RX;

    if ( HAL_UART_Init( huart ) != HAL_OK )
    {
        return false;
    }

    uart_console_state.rx_head        = 0U;
    uart_console_state.rx_tail        = 0U;
    uart_console_state.is_initialised = true;

    __HAL_UART_ENABLE_IT( huart, UART_IT_RXNE );

    return true;
}

bool HW_UART_CONSOLE_Read( uint8_t* dest, uint32_t dest_size, uint32_t* bytes_read )
{
    if ( ( dest == NULL ) || ( bytes_read == NULL ) )
    {
        return false;
    }

    *bytes_read = 0U;

    if ( !uart_console_state.is_initialised || ( dest_size == 0U ) )
    {
        return true;
    }

    while ( ( *bytes_read < dest_size )
            && ( uart_console_state.rx_tail != uart_console_state.rx_head ) )
    {
        dest[*bytes_read] = uart_console_rx_buffer[uart_console_state.rx_tail];

        uart_console_state.rx_tail =
            ( uart_console_state.rx_tail + 1U ) % HW_UART_CONSOLE_RX_BUFFER_SIZE;

        ( *bytes_read )++;
    }

    return true;
}

void HW_UART_CONSOLE_IRQHandler( void )
{
    if ( !uart_console_state.is_initialised )
    {
        return;
    }

    if ( LL_USART_IsActiveFlag_RXNE( HW_UART_CONSOLE_INSTANCE ) )
    {
        uint8_t byte = ( uint8_t )LL_USART_ReceiveData8( HW_UART_CONSOLE_INSTANCE );

        uint32_t next_head = ( uart_console_state.rx_head + 1U ) % HW_UART_CONSOLE_RX_BUFFER_SIZE;

        if ( next_head != uart_console_state.rx_tail )
        {
            uart_console_rx_buffer[uart_console_state.rx_head] = byte;
            uart_console_state.rx_head                         = next_head;
        }
        else
        {
            /* Optional later: latch RX overflow */
        }
    }
}

void USART3_IRQHandler( void )
{
    HW_UART_CONSOLE_IRQHandler();
}
