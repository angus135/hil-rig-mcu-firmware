/******************************************************************************
 *  File:       hw_uart.h
 *  Author:     Angus Corr
 *  Created:    16-Dec-2025
 *
 *  Description:
 *      <Short description of the module, what it exposes, and how it should be used>
 *
 *  Notes:
 *      <Public assumptions, required initialisation order, dependencies, etc.>
 ******************************************************************************/

#ifndef HW_UART_H
#define HW_UART_H

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

typedef enum UARTPort_T
{
    UART_CONSOLE,

    UART_PORT_NUMBER,
} UARTPort_T;

typedef enum UARTStatus_T
{
    UART_SUCCESS,
    UART_BUSY,
    UART_ERROR,
    UART_TIMEOUT,
} UARTStatus_T;

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */
/**
 * @brief Starts the UART receive service for a specified port
 *
 * @param port The UART port to start the receive service for
 * @return UARTStatus_T - the status of the operation
 */
UARTStatus_T HW_UART_Start_Rx_Service( UARTPort_T port );

/**
 * @brief Tries to read a byte from a specified UART port
 *
 * @param port The UART port to read from.
 * @param byte Pointer to the byte to store the received data.
 * @return true if a byte was read successfully, false if no byte was available.
 */
bool HW_UART_Try_Read_Byte( UARTPort_T port, uint8_t* byte );

/**
 * @brief writes a single Byte to a specified UART port
 *
 * @param port   The UART port to write to
 * @param byte   The byte to write
 *
 * @returns UARTStatus_T - the status of the transfer
 *
 * This function wraps the HAL_UART_Transmit_DMA( ... ) function provided by the
 * HAL layer. So this will be done via DMA.
 */
UARTStatus_T HW_UART_Write_Byte( UARTPort_T port, uint8_t byte );

#ifdef __cplusplus
}
#endif

#endif /* HW_UART_H */
