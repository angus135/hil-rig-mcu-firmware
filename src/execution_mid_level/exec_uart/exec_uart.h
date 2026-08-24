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
 *      - UART peripheral and DMA ownership remain in the low-level driver.
 *      - External electrical-interface selection is applied here through the
 *        logic expander.
 *      - RX data returned through this interface is copied from low-level
 *        driver owned DMA buffers into caller-provided storage.
 *      - TX operations are sequenced through the low-level driver TX ring buffer
 *        and DMA pump.
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

#include "hw_uart_dut.h"
/**-----------------------------------------------------------------------------
 *  Public Defines / Macros
 *------------------------------------------------------------------------------
 */

/*
 * Maximum single payload size accepted by the exec UART transmit API.
 * Larger logical UART outputs must be split by higher layers before calling
 * EXEC_UART_Transmit().
 */
#define EXEC_UART_MAX_CHUNK_SIZE HW_UART_TX_BUFFER_SIZE

/**-----------------------------------------------------------------------------
 *  Public Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

/**
 * @brief  Identifies the available UART channels supported by the hardware.
 *
 * @note   Each channel corresponds to a distinct DUT-facing UART interface with
 *         independent configuration and buffering.
 */
typedef enum
{
    EXEC_UART_CHANNEL_1 = 0U,
    EXEC_UART_CHANNEL_2,
    EXEC_UART_CHANNEL_COUNT
} ExecUartChannel_T;

/**
 * @brief  Defines the physical interface mode and voltage behaviour of the UART channel.
 *
 * @note   This controls external hardware selection (e.g. TTL voltage level or RS232
 *         line driver) and must be configured before enabling UART operation.
 */
typedef enum
{
    EXEC_UART_MODE_DISABLED = 0U,
    EXEC_UART_MODE_TTL_3V3,
    EXEC_UART_MODE_TTL_5V0,
    EXEC_UART_MODE_RS232
} ExecUartInterfaceMode_T;

typedef struct
{
    ExecUartInterfaceMode_T interface_mode;

    uint32_t           baud_rate;
    HwUartWordLength_T word_length;
    HwUartStopBits_T   stop_bits;
    HwUartParity_T     parity;

    bool rx_enabled;
    bool tx_enabled;
    bool is_enabled;
} ExecUartConfig_T;

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

/**
 * @brief Configure or disable a UART channel without starting it.
 *
 * Set @p config->is_enabled false to stop, deconfigure, and apply the safe
 * external-interface state.
 */
bool EXEC_UART_Configure_Channel( ExecUartChannel_T channel, const ExecUartConfig_T* config );

/** @brief Start a configured UART channel. */
bool EXEC_UART_Start_Channel( ExecUartChannel_T channel );

/**
 * @brief Stop a started UART channel while retaining its configuration.
 * @return false if the channel is invalid, not started, TX is incomplete, or RX stop fails.
 */
bool EXEC_UART_Stop_Channel( ExecUartChannel_T channel );

/**
 * @brief  Queues a UART TX payload and starts the low-level TX DMA pump if required.
 *
 * @param  channel      UART channel to transmit on.
 * @param  data         Pointer to the payload bytes to transmit.
 * @param  length_bytes Number of payload bytes to transmit.
 *
 * @return true if the payload was successfully queued and the TX DMA pump is running,
 *         already running, or did not require action.
 * @return false if the low-level driver rejects the payload, usually because the full
 *         payload cannot fit in the TX ring buffer, or if low-level trigger fails.
 *
 * @note   This function performs a combined transmit operation by:
 *         1. queueing the complete payload into the low-level TX ring buffer,
 *         2. triggering the low-level DMA pump.
 *
 * @note   Payload queueing is all-or-nothing. If the full payload cannot fit in the
 *         low-level TX ring buffer, no bytes are queued and this function returns false.
 *
 * @note   New TX data may be queued while a previous TX DMA transfer is still active,
 *         provided sufficient free space remains in the low-level TX ring buffer.
 */
bool EXEC_UART_Transmit( ExecUartChannel_T channel, const uint8_t* data, uint32_t length_bytes );

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
bool EXEC_UART_Read( ExecUartChannel_T channel, uint8_t* dest, uint32_t dest_size,
                     uint32_t* bytes_read );

/**
 * @brief Reports whether UART TX is fully complete.
 *
 * TX is complete when the low-level TX queue is empty, no TX DMA transfer is
 * active, and the UART has shifted out the final stop bit.
 *
 * @param channel UART channel to inspect.
 *
 * @return true if TX is fully complete.
 * @return false otherwise.
 */
bool EXEC_UART_Is_Tx_Complete( ExecUartChannel_T channel );

#ifdef __cplusplus
}
#endif

#endif /* EXEC_UART_H */
