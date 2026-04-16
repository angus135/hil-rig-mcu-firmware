/******************************************************************************
 *  File:       hw_uart_console.h
 *  Author:     Callum Rafferty
 *  Created:    15-04-26
 *
 *  Description:
 *      Public interface for the low-level console UART driver.
 *
 *  Notes:
 *      - RX is interrupt-driven and buffered internally by the low-level driver.
 *      - TX is provided as a blocking primitive intended for console task
 *        context only.
 ******************************************************************************/

#ifndef HW_UART_CONSOLE_H
#define HW_UART_CONSOLE_H

#ifdef __cplusplus
extern "C"
{
#endif

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include <stdint.h>
#include <stdbool.h>

/**-----------------------------------------------------------------------------
 *  Public Defines / Macros
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Public Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

/**
 * @brief Initialise the low-level console UART driver.
 *
 * Configures the console UART peripheral and starts interrupt-driven reception.
 *
 * @param baud_rate  UART baud rate for console operation.
 *
 * @returns true if initialisation succeeds, otherwise false.
 */
bool HW_UART_CONSOLE_Init( uint32_t baud_rate );

/**
 * @brief Read unread bytes from the low-level console RX buffer.
 *
 * Copies up to @p dest_size unread bytes into @p dest and reports the number
 * of bytes copied through @p bytes_read.
 *
 * @param dest        Destination buffer for copied RX bytes.
 * @param dest_size   Maximum number of bytes to copy.
 * @param bytes_read  Output pointer written with the number of bytes copied.
 *
 * @returns true if the read operation succeeds, otherwise false.
 */
bool HW_UART_CONSOLE_Read( uint8_t* dest, uint32_t dest_size, uint32_t* bytes_read );

/**
 * @brief Transmit a block of console bytes using a blocking UART call.
 *
 * @param data        Pointer to bytes to transmit.
 * @param length      Number of bytes to transmit.
 * @param timeout_ms  UART transmit timeout in milliseconds.
 *
 * @returns true if transmission succeeds, otherwise false.
 *
 * @note Intended for use from console task context.
 */
bool HW_UART_CONSOLE_Write_Blocking( const uint8_t* data, uint32_t length, uint32_t timeout_ms );

#ifdef __cplusplus
}
#endif

#endif /* HW_UART_CONSOLE_H */
