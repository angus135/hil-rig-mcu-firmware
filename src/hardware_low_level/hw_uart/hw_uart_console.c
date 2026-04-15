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
#define HW_UART_CONSOLE_TX_BUFFER_SIZE 256U

/**-----------------------------------------------------------------------------
 *  Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

typedef struct
{
    uint32_t rx_head;
    uint32_t rx_tail;

    uint32_t tx_head;
    uint32_t tx_tail;

    bool is_initialised;
    bool tx_interrupt_enabled;
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
static uint8_t              uart_console_tx_buffer[HW_UART_CONSOLE_TX_BUFFER_SIZE];

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

    uart_console_state.rx_head              = 0U;
    uart_console_state.rx_tail              = 0U;
    uart_console_state.tx_head              = 0U;
    uart_console_state.tx_tail              = 0U;
    uart_console_state.is_initialised       = true;
    uart_console_state.tx_interrupt_enabled = false;

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

bool HW_UART_CONSOLE_Write( const uint8_t* data, uint32_t length )
{
    if ( data == NULL )
    {
        return false;
    }

    if ( !uart_console_state.is_initialised )
    {
        return false;
    }

    if ( length == 0U )
    {
        return true;
    }

    uint32_t free_space;

    if ( uart_console_state.tx_head >= uart_console_state.tx_tail )
    {
        free_space = ( HW_UART_CONSOLE_TX_BUFFER_SIZE
                       - ( uart_console_state.tx_head - uart_console_state.tx_tail ) )
                     - 1U;
    }
    else
    {
        free_space = ( uart_console_state.tx_tail - uart_console_state.tx_head ) - 1U;
    }

    if ( length > free_space )
    {
        return false;
    }

    for ( uint32_t i = 0U; i < length; i++ )
    {
        uart_console_tx_buffer[uart_console_state.tx_head] = data[i];
        uart_console_state.tx_head =
            ( uart_console_state.tx_head + 1U ) % HW_UART_CONSOLE_TX_BUFFER_SIZE;
    }

    __HAL_UART_ENABLE_IT( HW_UART_CONSOLE_HANDLE, UART_IT_TXE );
    uart_console_state.tx_interrupt_enabled = true;

    return true;
}

bool HW_UART_CONSOLE_Is_Tx_Busy( void )
{
    if ( !uart_console_state.is_initialised )
    {
        return false;
    }

    return ( uart_console_state.tx_head != uart_console_state.tx_tail )
           || uart_console_state.tx_interrupt_enabled;
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
    }

    if ( LL_USART_IsActiveFlag_TXE( HW_UART_CONSOLE_INSTANCE ) )
    {
        if ( uart_console_state.tx_tail != uart_console_state.tx_head )
        {
            LL_USART_TransmitData8( HW_UART_CONSOLE_INSTANCE,
                                    uart_console_tx_buffer[uart_console_state.tx_tail] );

            uart_console_state.tx_tail =
                ( uart_console_state.tx_tail + 1U ) % HW_UART_CONSOLE_TX_BUFFER_SIZE;
        }
        else
        {
            __HAL_UART_DISABLE_IT( HW_UART_CONSOLE_HANDLE, UART_IT_TXE );
            uart_console_state.tx_interrupt_enabled = false;
        }
    }
}

void USART3_IRQHandler( void )
{
    HW_UART_CONSOLE_IRQHandler();
}
