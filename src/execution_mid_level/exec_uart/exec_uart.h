/******************************************************************************
 *  File:       exec_uart.h
 *  Author:     Callum Rafferty
 *  Created:    25-Mar-2025
 *
 *  Description:
 *      Public interface for the mid-level UART driver used by the execution
 *      and configuration layers of the HIL-RIG.
 *
 *      This module exposes:
 *      - UART channel configuration and deconfiguration sequencing,
 *      - execution-facing transmit operations,
 *      - execution-facing receive operations that copy unread low-level RX data
 *        into caller-provided storage.
 *
 *  Notes:
 *      - This layer does not directly access UART hardware registers or DMA
 *        peripherals.
 *      - Hardware control, DMA ownership, buffer ownership, and electrical
 *        interface selection remain the responsibility of the low-level
 *        hw_uart driver.
 *      - RX data returned through this interface is copied from low-level
 *        driver owned DMA buffers into caller-provided storage.
 *      - TX operations are sequenced through the low-level driver staging and
 *        trigger path.
 ******************************************************************************/

#ifndef EXEC_UART_H
#define EXEC_UART_H

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

#include "hw_uart.h"
/**-----------------------------------------------------------------------------
 *  Public Defines / Macros
 *------------------------------------------------------------------------------
 */

#define EXEC_UART_MAX_CHUNK_SIZE HW_UART_TX_BUFFER_SIZE

/**-----------------------------------------------------------------------------
 *  Public Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

/**
 * @brief  Applies a UART channel configuration through the exec layer.
 *
 * @param  channel UART channel to configure.
 * @param  config  Pointer to the UART configuration to apply.
 *
 * @return true if the configuration was successfully applied.
 * @return false if the channel is invalid, the configuration pointer is null,
 *         low-level configuration fails, RX stop fails during reconfiguration,
 *         or RX start fails when requested by the supplied configuration.
 *
 * @note   This function sequences configuration-related low-level operations.
 *
 * @note   If RX is currently running on the channel, it is stopped before the
 *         new configuration is applied.
 *
 * @note   If the supplied configuration enables RX, reception is started after
 *         successful low-level configuration.
 */
bool EXEC_UART_Apply_Configuration( HwUartChannel_T channel, const HwUartConfig_T* config );

/**
 * @brief  Deconfigures a UART channel through the exec layer.
 *
 * @param  channel UART channel to deconfigure.
 *
 * @return true if the channel was successfully deconfigured.
 * @return false if the channel is invalid, RX stop fails, or the disabled
 *         configuration cannot be applied through the low-level driver.
 *
 * @note   This function stops active RX if required, then applies a canonical
 *         disabled UART configuration through the low-level driver.
 */
bool EXEC_UART_Deconfigure( HwUartChannel_T channel );

/**
 * @brief  Transmits a UART payload through the exec layer.
 *
 * @param  channel      UART channel to transmit on.
 * @param  data         Pointer to the payload bytes to transmit.
 * @param  length_bytes Number of payload bytes to transmit.
 *
 * @return true if the payload was successfully staged and transmission was
 *         successfully launched.
 * @return false if the channel is already in an exec-level staged lock state,
 *         low-level staging fails, or low-level trigger fails.
 *
 * @note   This function performs a combined transmit operation by:
 *         1. staging the payload into the low-level TX buffer,
 *         2. triggering low-level DMA-based transmission.
 *
 * @note   If low-level trigger fails after low-level staging succeeds, the
 *         channel remains in an exec-level staged lock state for higher-level
 *         recovery.
 */
bool EXEC_UART_Transmit( HwUartChannel_T channel, const uint8_t* data, uint32_t length_bytes );

/**
 * @brief  Copies unread UART RX data into caller-provided storage.
 *
 * @param  channel     UART channel to read from.
 * @param  dest        Destination buffer provided by the caller.
 * @param  dest_size   Maximum number of bytes that may be written to @p dest.
 * @param  bytes_read  Output pointer receiving the number of bytes copied.
 *
 * @return true if the read operation completed successfully.
 * @return false if @p dest or @p bytes_read is null.
 *
 * @note   This function retrieves unread RX data from the low-level driver
 *         using the low-level span interface, copies up to @p dest_size bytes
 *         into caller-provided storage, and consumes exactly the number of
 *         bytes copied.
 *
 * @note   If no unread data is available, this function returns true and sets
 *         @p *bytes_read to 0.
 *
 * @note   If @p dest_size is 0, this function returns true and performs no
 *         copy or consume operation.
 */
bool EXEC_UART_Read( HwUartChannel_T channel, uint8_t* dest, uint32_t dest_size,
                     uint32_t* bytes_read );

/**
 * @brief  Reports whether the UART TX path is currently busy.
 *
 * @param  channel UART channel to query
 *
 * @return true if TX is currently in progress or data is staged
 * @return false if the channel is ready to accept a new transmit
 *
 * @note   This reflects both mid-level staged state and low-level TX activity.
 */
bool EXEC_UART_Is_Tx_Busy( HwUartChannel_T channel );

#ifdef __cplusplus
}
#endif

#endif /* EXEC_UART_H */
