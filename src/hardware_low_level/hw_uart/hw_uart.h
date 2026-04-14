/******************************************************************************
 *  File:       hw_uart.h
 *  Author:     Callum Rafferty
 *  Created:    16-Dec-2025
 *
 *  Description:
 *      Public API for the low-level UART driver used by DUT-facing UART channels.
 *
 *      This module provides:
 *      - configuration of UART channels and interface modes,
 *      - DMA-backed continuous RX operation,
 *      - efficient access to received data via zero-copy span views.
 *
 *      The low-level driver owns the DMA circular buffer and all associated
 *      buffer management. Higher layers may inspect unread data through span
 *      views and are responsible for copying data into stable storage if it must
 *      persist beyond the current processing step.
 *
 *      After processing, higher layers must explicitly report consumption of
 *      data to advance the internal read index.
 *
 *  Notes:
 *      - RX data is provided as a transient view into a DMA-backed circular buffer.
 *      - Returned pointers must not be retained beyond the valid processing window.
 *      - This module does not define result storage or execution semantics.
 *
 *  Typical usage:
 *      1. Configure channel using HW_UART_Configure_Channel()
 *      2. Start RX using HW_UART_Rx_Start()
 *      3. Call HW_UART_Rx_Peek() to inspect unread data
 *      4. Copy data if persistence is required
 *      5. Call HW_UART_Rx_Consume() after processing to advance the read index
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
#define HW_UART_TX_MAX_CHUNK_SIZE 256U
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
    HW_UART_CHANNEL_1 = 0,  // First DUT facing UART channel
    HW_UART_CHANNEL_2 = 1,  // Second DUT facing UART channel
    HW_UART_CHANNEL_3 = 2,  // Temporary Console UART channel
} HwUartChannel_T;

/**
 * @brief  Defines the physical interface mode and voltage behaviour of the UART channel.
 *
 * @note   This controls external hardware selection (e.g. TTL voltage level or RS232
 *         line driver) and must be configured before enabling UART operation.
 */
typedef enum
{
    HW_UART_MODE_DISABLED = 0,  // Default state, no UART functionality
    HW_UART_MODE_TTL_3V3,       // Standard TTL logic levels (0V for LOW, 3.3V for HIGH)
    HW_UART_MODE_TTL_5V0,       // Standard TTL logic levels (0V for LOW, 5V for HIGH)
    HW_UART_MODE_RS232          // RS-232 line driver interface
} HwUartInterfaceMode_T;

/**
 * @brief  Specifies the UART parity configuration.
 *
 * @note   This value is applied during UART peripheral initialisation.
 */
typedef enum
{
    HW_UART_PARITY_NONE = 0,  // No parity bit is used, all bits are data bits
    HW_UART_PARITY_ODD  = 1,  // An odd parity bit is added
    HW_UART_PARITY_EVEN = 2   // An even parity bit is added
} HwUartParity_T;

/**
 * @brief  Specifies the UART word length in bits.
 *
 * @note   Determines the number of data bits per frame and affects buffer
 *         interpretation at higher layers.
 */
typedef enum
{
    HW_UART_WORD_LENGTH_8_BITS = 8,  // Standard 8 data bits per frame
    HW_UART_WORD_LENGTH_9_BITS = 9   // Extended 9 data bits per frame, may be used for specific
                                     // protocols or addressing schemes
} HwUartWordLength_T;

/**
 * @brief  Specifies the number of stop bits in each UART frame.
 *
 * @note   This is applied directly to the UART peripheral configuration.
 */
typedef enum
{
    HW_UART_STOP_BITS_1 = 1,  // Standard 1 stop bit per frame
    HW_UART_STOP_BITS_2 = 2   // Extended 2 stop bits per frame
} HwUartStopBits_T;

/**
 * @brief  Configuration structure for a UART channel.
 *
 * @note   This structure defines both the electrical interface mode and UART
 *         framing parameters. It is validated and stored by the low-level driver
 *         during configuration and later applied when RX or TX is started.
 *
 * @note   This structure does not initiate hardware activity by itself.
 *         HW_UART_CONFIGURE_CHANNEL() must be called before starting RX or TX.
 */
typedef struct
{
    HwUartInterfaceMode_T interface_mode;  // Determines the Uart interface type and voltage levels

    uint32_t           baud_rate;    // Communication speed in bits per second (bps)
    HwUartWordLength_T word_length;  // Number of data bits per UART frame (e.g. 8 or 9)
    HwUartStopBits_T   stop_bits;    // Number of stop bits per UART frame (e.g. 1 or 2)
    HwUartParity_T     parity;       // Parity configuration for error detection (none, even, odd)

    bool rx_enabled;  // Enable reception functionality
    bool tx_enabled;  // Enable transmission functionality
} HwUartConfig_T;

/**
 * @brief  Represents a contiguous region of unread data within the DMA RX buffer.
 *
 * @note   The data pointer refers to memory owned by the low-level driver and is
 *         valid only within the current processing window.
 */
typedef struct
{
    const uint8_t* data;  // Pointer to the start of the unread data span within the DMA buffer
    uint32_t       length_bytes;  // Length of the unread data span in bytes
} HwUartRxSpan_T;

/**
 * @brief  Represents the unread data available in the DMA RX buffer as one or two spans.
 *
 * @note   Due to circular buffer wrapping, unread data may be split into two
 *         contiguous regions. This structure provides access to both regions
 *         without requiring copying.
 *
 * @note   The spans form a transient, read-only view into the DMA buffer.
 *         Higher layers must copy data if persistence is required.
 */
typedef struct
{
    HwUartRxSpan_T first_span;  // First contiguous span of unread data
    HwUartRxSpan_T
        second_span;  // Second contiguous span of unread data (non-zero only if wrapping occurs)
    uint32_t total_length_bytes;  // Total number of unread bytes across both spans for convenience
} HwUartRxSpans_T;

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */
/**
 * @brief  Configures a UART channel with the specified settings and applies
 *         the associated static hardware configuration.
 *
 * @param  channel The UART channel to configure.
 * @param  config  Pointer to the configuration structure describing UART
 *                 parameters and interface mode.
 *
 * @return true if the channel was successfully configured.
 * @return false if the channel index or configuration is invalid, or if
 *         hardware selection fails.
 *
 * @note   This function performs validation of the provided configuration
 *         before applying any changes to the hardware.
 *
 * @note   The configuration is stored within the low-level driver and used
 *         later during start operations. This function does not enable RX or
 *         TX operation.
 *
 * @note   Static hardware selection (e.g. interface mode and voltage levels)
 *         is applied as part of configuration to ensure the physical interface
 *         is in a safe and defined state prior to enabling UART activity.
 *
 * @note   Runtime state is reset as part of configuration, including read index
 *         and fault flags.
 *
 * @note   This function must be called successfully before invoking
 *         HW_UART_Rx_Start() or any TX-related operations.
 */
bool HW_UART_Configure_Channel( HwUartChannel_T channel, const HwUartConfig_T* config );

/**
 * @brief  Starts UART reception for the specified channel using DMA into the
 *         LL driver owned circular RX buffer.
 *
 * @param  channel The UART channel to start reception on.
 *
 * @return true if RX was successfully started.
 * @return false if the channel is invalid, not configured, RX is disabled, or
 *         hardware initialisation fails.
 *
 * @note   This function applies the stored configuration to the underlying UART
 *         peripheral via HAL and initiates DMA-based reception into the internal
 *         circular buffer owned by the low-level driver.
 *
 * @note   The RX buffer and read index are reset prior to enabling reception to
 *         ensure a clean starting state.
 *
 * @note   This function does not expose or transfer ownership of received data.
 *         Data is made available to higher layers via HW_UART_Rx_Peek().
 *
 * @note   The DMA stream is expected to be configured in circular mode so that
 *         reception continues indefinitely without software intervention.
 *
 * @note   This function must only be called after successful configuration via
 *         HW_UART_Configure_Channel().
 *
 * @note   RX operation is considered active once this function returns true, and
 *         can be queried via the runtime state.
 */
bool HW_UART_Rx_Start( HwUartChannel_T channel );

/**
 * @brief  Exposes a transient zero-copy view of the current unread RX data for the specified UART
 *         channel as one or two contiguous spans into the LL driver owned DMA circular buffer.
 *
 * @param channel The UART channel to inspect.
 *
 * @return HwUartRxSpans_T
 *         A structure containing up to two readable spans and the total unread byte count.
 *
 * @note   This function does not copy data. It provides a read-only view into the low-level driver
 *         owned DMA buffer. Buffer allocation, DMA write ownership, wrap handling, and consume
 *         semantics remain the responsibility of the low-level driver.
 *
 * @note   The returned spans are intended for immediate higher-level processing. Higher layers may
 *         copy the data into execution-owned result storage if persistence is required beyond the
 *         current processing step.
 *
 * @note   Higher layers must not directly manage the DMA buffer, modify the returned memory, or
 *         retain the returned pointers beyond the valid processing window. Once the required copy
 *         or processing is complete, the caller shall report consumption through
 * HW_UART_Rx_Consume().
 *
 * @note   This interface preserves a clean ownership boundary:
 *         - the low-level driver owns the DMA circular buffer and its management,
 *         - the mid-level driver owns adaptation from raw UART bytes to execution-level data,
 *         - the execution manager owns result storage and tick association.
 */
HwUartRxSpans_T HW_UART_Rx_Peek( HwUartChannel_T channel );

/**
 * @brief  Marks unread RX data as consumed by advancing the LL driver managed read index.
 *
 * @param channel The UART channel to update.
 * @param bytes_to_consume Number of bytes previously obtained via HW_UART_Rx_Peek() that have been
 *                         processed by higher layers.
 *
 * @note   The LL driver retains ownership of the DMA buffer. This function only updates the read
 *         index and does not copy or modify buffer contents.
 *
 * @note   The caller shall only consume data that has already been processed or copied into
 *         stable storage. Consuming data allows the DMA buffer region to be reused.
 */
void HW_UART_Rx_Consume( HwUartChannel_T channel, uint32_t bytes_to_consume );

/**
 * @brief  Copies a transmit payload into the low-level driver owned TX staging buffer for the
 *         specified UART channel.
 *
 * @param  channel       The UART channel whose TX staging buffer is to be loaded.
 * @param  data          Pointer to the source payload to copy into the staging buffer.
 * @param  length_bytes  Number of payload bytes to stage for transmission.
 *
 * @return true if the payload was successfully copied into the staging buffer.
 * @return false if the channel is invalid, the payload pointer is null, the payload length is
 *         zero or exceeds the staging buffer capacity, the channel is not ready for TX, or
 *         staged or in-flight TX data already owns the buffer.
 *
 * @note   This function copies data into low-level driver owned memory. The caller retains
 *         ownership of the source buffer and may reuse or discard it after this function returns.
 *
 * @note   Staged data must not be overwritten before it is transmitted or otherwise released.
 *         This function therefore rejects loads while TX data is already staged or while a TX
 *         transfer is currently in progress.
 *
 * @note   This function stages data only. It does not begin transmission. Transmission begins
 *         only when HW_UART_Tx_Trigger() is called.
 */
bool HW_UART_Tx_Load_Buffer( HwUartChannel_T channel, const uint8_t* data, uint32_t length_bytes );

/**
 * @brief  Starts transmission of the currently staged TX payload for the specified UART channel
 *         using a low-level (LL) DMA launch.
 *
 * @param  channel The UART channel to transmit on.
 *
 * @return true if DMA transmission was successfully started.
 * @return false if the channel is invalid, the channel is not ready for TX, no payload has been
 *         staged, a TX transfer is already running, or the staged length is invalid.
 *
 * @note   This function does not copy payload data. It launches transmission of data that has
 *         already been staged in the low-level driver owned TX buffer via HW_UART_Tx_Load_Buffer().
 *
 * @note   This implementation does not use HAL. Instead, it directly controls the DMA stream and
 *         UART DMA request using LL functions and register access.
 *
 * @note   Prior to launching the transfer, the DMA stream is explicitly disabled and all pending
 *         interrupt flags are cleared. This ensures that the DMA registers (M0AR, PAR, NDTR) can be
 *         safely reprogrammed for the new transfer and that no stale flags interfere with
 *         operation.
 *
 * @note   The DMA is configured in normal mode. Once enabled, the DMA controller autonomously
 *         transfers bytes from the staged TX buffer (memory) to the UART data register (DR). No CPU
 *         intervention is required during the transfer.
 *
 * @note   TX buffer ownership is retained by the low-level driver until the DMA transfer completes.
 *         Completion is detected via the DMA transfer complete interrupt, at which point the driver
 *         must clear tx_running, tx_loaded, and tx_length_bytes.
 *
 * @note   The UART DMA transmit request (CR3.DMAT) is enabled before the DMA stream is started.
 *         This allows the UART peripheral to generate DMA requests as it becomes ready to accept
 * new data.
 *
 * @note   This function assumes that the DMA stream configuration (channel selection, direction,
 *         increment modes, data width, and priority) has already been performed during channel
 *         initialization. Only the transfer-specific parameters (addresses and length) are updated
 *         here.
 *
 * @note   This function is designed for deterministic execution. All channel-specific hardware
 *         information (DMA controller, stream, and flag registers) is precomputed in the hardware
 *         map, avoiding switch statements in the hot path.
 */
bool HW_UART_Tx_Trigger( HwUartChannel_T channel );

bool HW_UART_Tx_Is_Busy( HwUartChannel_T channel );

bool HW_UART_Rx_Stop( HwUartChannel_T channel );
bool HW_UART_Rx_Is_Running( HwUartChannel_T channel );

#ifdef __cplusplus
}
#endif

#endif /* HW_UART_H */
