/******************************************************************************
 *  File:       hw_uart_console.c
 *  Author:     Callum Rafferty
 *  Created:    15-04-26
 *
 *  Description:
 *      Low-level console UART driver for the HIL-RIG debug console.
 *
 *      This module owns:
 *      - USART3 console UART initialisation,
 *      - interrupt-driven single-byte RX capture,
 *      - software RX ring buffer management,
 *      - blocking TX primitive for console task-driven output flushing.
 *
 *  Notes:
 *      - RX is interrupt-driven so bytes are captured reliably even while the
 *        console task is not running.
 *      - TX is intentionally blocking and is expected to be called only from
 *        console task context.
 *      - This module does not implement console parsing, command handling, or
 *        TX queueing policy. Those responsibilities belong to console.c.
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

/**
 * @brief Stores low-level runtime state for the console UART driver.
 *
 * @note This structure tracks RX ring buffer indices and whether the low-level
 *       console UART driver has been successfully initialised.
 */
typedef struct
{
    uint32_t rx_head;
    uint32_t rx_tail;

    bool is_initialised;
} HwUartConsoleState_T;

/**-----------------------------------------------------------------------------
 *  Public (global) and Extern Variables
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Private (static) Variables
 *------------------------------------------------------------------------------
 */

static HwUartConsoleState_T uart_console_state;  // Driver-owned runtime state
static uint8_t           uart_console_rx_buffer[HW_UART_CONSOLE_RX_BUFFER_SIZE];  // RX ring buffer
static uint8_t           uart_console_rx_byte;      // Single-byte HAL RX staging buffer
static volatile uint32_t s_rx_overflow_count = 0U;  // Count of dropped RX bytes due to overflow

/**-----------------------------------------------------------------------------
 *  Private (static) Function Definitions
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */

/**
 * @brief Initialise the low-level console UART driver.
 *
 * Configures USART3 for console operation, resets driver-owned RX state, and
 * starts interrupt-driven single-byte reception using HAL_UART_Receive_IT().
 *
 * @param baud_rate  UART baud rate to apply to the console peripheral.
 *
 * @returns true if initialisation and initial RX interrupt arming succeed.
 * @returns false if UART initialisation or RX interrupt startup fails.
 */
bool HW_UART_CONSOLE_Init( uint32_t baud_rate )
{
    UART_HandleTypeDef* huart = HW_UART_CONSOLE_HANDLE;

    huart->Instance        = HW_UART_CONSOLE_INSTANCE;
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

    if ( HAL_UART_Receive_IT( huart, &uart_console_rx_byte, 1U ) != HAL_OK )
    {
        uart_console_state.is_initialised = false;
        return false;
    }

    return true;
}

/**
 * @brief Drain available bytes from the console RX ring buffer.
 *
 * Copies up to @p dest_size unread bytes from the driver-owned RX ring buffer
 * into the caller-provided destination buffer and advances the RX tail index
 * accordingly.
 *
 * @param dest        Destination buffer to receive unread console bytes.
 * @param dest_size   Maximum number of bytes to copy into @p dest.
 * @param bytes_read  Output pointer written with the number of bytes copied.
 *
 * @returns true if the read operation completes successfully.
 * @returns false if required pointers are null.
 *
 * @note Returning true with @p *bytes_read equal to zero indicates that no
 *       unread console bytes were available.
 */
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

/**
 * @brief Transmit a block of console bytes using a blocking UART call.
 *
 * Sends the provided byte range directly over USART3 using HAL_UART_Transmit().
 * This function is intentionally blocking and is expected to be called only
 * from console task context.
 *
 * @param data        Pointer to bytes to transmit.
 * @param length      Number of bytes to transmit.
 * @param timeout_ms  HAL transmit timeout in milliseconds.
 *
 * @returns true if the full byte range is transmitted successfully.
 * @returns false if arguments are invalid, the driver is uninitialised, or
 *          HAL_UART_Transmit() fails.
 *
 * @note Blocking TX is intentional. Console output is flushed from task
 *       context so that no UART TX interrupt activity occurs during the
 *       timer-driven execution window.
 */
bool HW_UART_CONSOLE_Write_Blocking( const uint8_t* data, uint32_t length, uint32_t timeout_ms )
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

    if ( timeout_ms == 0U )
    {
        return false;
    }

    return ( HAL_UART_Transmit( HW_UART_CONSOLE_HANDLE, ( uint8_t* )data, ( uint16_t )length,
                                timeout_ms )
             == HAL_OK );
}

/**
 * @brief HAL UART RX-complete callback for the console UART.
 *
 * Stores the newly received console byte into the RX ring buffer if space is
 * available and rearms the next single-byte receive interrupt.
 *
 * @param huart  HAL UART handle that completed a receive operation.
 *
 * @returns void
 *
 * @note If the RX ring buffer is full, the received byte is dropped and the
 *       RX overflow counter is incremented.
 */
void HAL_UART_RxCpltCallback( UART_HandleTypeDef* huart )
{
    if ( huart->Instance == HW_UART_CONSOLE_INSTANCE )
    {
        uint32_t next_head = ( uart_console_state.rx_head + 1U ) % HW_UART_CONSOLE_RX_BUFFER_SIZE;

        if ( next_head != uart_console_state.rx_tail )
        {
            uart_console_rx_buffer[uart_console_state.rx_head] = uart_console_rx_byte;
            uart_console_state.rx_head                         = next_head;
        }
        else
        {
            s_rx_overflow_count++;
        }

        ( void )HAL_UART_Receive_IT( huart, &uart_console_rx_byte, 1U );
    }
}

/**
 * @brief HAL UART error callback for the console UART.
 *
 * Rearms interrupt-driven single-byte reception after a UART error so that
 * subsequent console RX activity can continue.
 *
 * @param huart  HAL UART handle associated with the error event.
 *
 * @returns void
 */
void HAL_UART_ErrorCallback( UART_HandleTypeDef* huart )
{
    if ( huart->Instance == HW_UART_CONSOLE_INSTANCE )
    {
        ( void )HAL_UART_Receive_IT( huart, &uart_console_rx_byte, 1U );
    }
}

/**
 * @brief USART3 interrupt handler for the low-level console UART driver.
 *
 * Forwards USART3 interrupt handling to the HAL UART IRQ dispatcher.
 *
 * @returns void
 */
void USART3_IRQHandler( void )
{
    HAL_UART_IRQHandler( HW_UART_CONSOLE_HANDLE );
}
