/******************************************************************************
 *  File:       hw_uart_dut.h
 *  Author:     Callum Rafferty
 *  Created:    16 Dec 2025
 *
 *  Description:
 *      Public API for the low level UART driver used by DUT facing UART channels.
 *
 *      This module provides:
 *      1. Configuration of UART channels and interface modes.
 *      2. DMA backed continuous RX operation.
 *      3. Efficient access to received data through zero copy RX span views.
 *      4. DMA source TX ring buffering.
 *      5. Normal mode DMA TX pumping over contiguous TX buffer spans.
 *
 *      The low level driver owns the RX DMA circular buffer, the TX DMA source
 *      ring buffer, and all associated buffer management state.
 *
 *      RX data is exposed to higher layers through transient span views. Higher
 *      layers are responsible for copying received data into stable storage if it
 *      must persist beyond the current processing step. After processing, higher
 *      layers must explicitly report RX consumption to advance the internal read
 *      index.
 *
 *      TX data is copied directly into a driver owned TX ring buffer. This ring
 *      buffer is also the DMA source buffer. The DMA TX stream is operated in
 *      normal mode and only transmits one contiguous buffer span at a time. If
 *      queued TX data wraps around the end of the TX buffer, the low level driver
 *      transmits it using multiple normal mode DMA launches.
 *
 *  Execution path API contract:
 *      Unless stated otherwise, execution path functions assume valid input.
 *      The caller must provide a valid channel, must ensure the channel has been
 *      configured for the requested direction, and must respect buffer ownership
 *      rules. These functions avoid defensive parameter checks to minimise
 *      execution path overhead.
 *
 *  Notes:
 *      1. RX data is provided as a transient view into a DMA backed circular buffer.
 *      2. Returned RX pointers must not be retained beyond the valid processing window.
 *      3. TX payloads are queued atomically at the payload level. If a full payload
 *         cannot fit in the available TX buffer space, no bytes are copied and the
 *         load operation fails.
 *      4. TX data may be queued while a DMA TX transfer is already active.
 *      5. The active DMA TX transfer is not modified by later queueing operations.
 *      6. This module does not define result storage or execution semantics.
 *
 *  Typical RX usage:
 *      1. Configure channel using HW_UART_Configure_Channel().
 *      2. Start RX using HW_UART_Rx_Start().
 *      3. Call HW_UART_Rx_Peek() to inspect unread data.
 *      4. Copy data if persistence is required.
 *      5. Call HW_UART_Rx_Consume() after processing to advance the read index.
 *
 *  Typical TX usage:
 *      1. Configure channel using HW_UART_Configure_Channel().
 *      2. Queue TX data using HW_UART_Tx_Load_Buffer().
 *      3. Call HW_UART_Tx_Trigger() to start the DMA pump if it is idle.
 *      4. Continue queueing additional TX data while buffer space remains.
 *      5. Treat a false return from HW_UART_Tx_Load_Buffer() as TX buffer capacity
 *         exhaustion or scheduling failure.
 ******************************************************************************/

#ifndef HW_UART_DUT_H
#define HW_UART_DUT_H

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

#define HW_UART_TX_BUFFER_SIZE 256U

/* Number of UART channels supported by the hardware */
#define HW_UART_CHANNEL_COUNT 2U

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
 *         HW_UART_Configure_Channel() must be called before starting RX or TX.
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
 * @brief  Exposes a transient zero copy view of the current unread RX data.
 *
 * @param  channel The UART channel to inspect.
 *
 * @return A structure containing up to two readable spans and the total unread
 *         byte count.
 *
 * @note   Execution path function. Assumes valid input.
 *
 * @note   Contract:
 *         The caller must provide a valid UART channel.
 *         The channel must already be configured and RX DMA must be running.
 *
 * @note   This function does not copy data. It provides a read only view into
 *         the low level driver owned RX DMA circular buffer.
 *
 * @note   The returned spans are intended for immediate higher level processing.
 *         Higher layers must copy the data into stable storage if persistence is
 *         required beyond the current processing step.
 *
 * @note   Higher layers must not directly manage the DMA buffer, modify the
 *         returned memory, or retain returned pointers beyond the valid processing
 *         window. Once processing is complete, the caller shall report consumption
 *         through HW_UART_Rx_Consume().
 */
HwUartRxSpans_T HW_UART_Rx_Peek( HwUartChannel_T channel );

/**
 * @brief  Marks unread RX data as consumed by advancing the driver managed read index.
 *
 * @param  channel The UART channel to update.
 * @param  bytes_to_consume Number of bytes previously obtained through
 *         HW_UART_Rx_Peek() that have been processed by higher layers.
 *
 * @note   Execution path function. Assumes valid input.
 *
 * @note   Contract:
 *         The caller must provide a valid UART channel.
 *         bytes_to_consume must not exceed the unread byte count previously
 *         reported by HW_UART_Rx_Peek().
 *
 * @note   The low level driver retains ownership of the RX DMA buffer. This
 *         function only updates the read index and does not copy or modify buffer
 *         contents.
 */
void HW_UART_Rx_Consume( HwUartChannel_T channel, uint32_t bytes_to_consume );

/**
 * @brief  Copies a complete transmit payload into the TX DMA source ring buffer.
 *
 * @param  channel The UART channel whose TX ring buffer is to be loaded.
 * @param  data Pointer to the source payload to copy into the TX ring buffer.
 * @param  length_bytes Number of payload bytes to queue for transmission.
 *
 * @return true if the full payload was successfully queued.
 * @return false if insufficient free TX buffer space is available.
 *
 * @note   Execution path function. Assumes valid input.
 *
 * @note   Contract:
 *         The caller must provide a valid UART channel.
 *         data must point to at least length_bytes bytes.
 *         length_bytes must be greater than zero.
 *         The channel must already be configured for TX.
 *         The execution layer is the sole producer for each UART channel.
 *
 * @note   The TX buffer is both the driver owned queue and the DMA source buffer.
 *         DMA transfers read directly from this buffer.
 *
 * @note   Data may be queued while a DMA TX transfer is already active. The active
 *         DMA transfer is not modified. Newly queued data will be transmitted by
 *         a later DMA launch.
 *
 * @note   This function is atomic at the payload level. If the full payload cannot
 *         fit in the available TX buffer space, no bytes are copied.
 *
 * @note   This function does not start transmission by itself. After a successful
 *         load, the caller should call HW_UART_Tx_Trigger() to start the TX DMA
 *         pump if it is currently idle.
 */
bool HW_UART_Tx_Load_Buffer( HwUartChannel_T channel, const uint8_t* data, uint32_t length_bytes );

/**
 * @brief  Starts the TX DMA pump for the next contiguous TX buffer span.
 *
 * @param  channel The UART channel whose queued TX data should be transmitted.
 *
 * @return true if the TX pump is already active, no queued data exists, or a DMA
 *         transfer was started.
 *
 * @note   Execution path function. Assumes valid input.
 *
 * @note   Contract:
 *         The caller must provide a valid UART channel.
 *         The channel must already be configured for TX.
 *
 * @note   This function does not copy payload data. Payload data must first be
 *         queued into the low level driver owned TX DMA source ring buffer using
 *         HW_UART_Tx_Load_Buffer().
 *
 * @note   The TX buffer is managed as a ring buffer. DMA is operated in normal
 *         mode and is only programmed with one contiguous span from the current
 *         TX tail position.
 *
 * @note   If queued TX data wraps around the end of the TX buffer, only the first
 *         contiguous span is launched. The wrapped span is launched by the DMA
 *         completion handler after the first span completes.
 *
 * @note   If a TX DMA transfer is already active, this function leaves the active
 *         transfer untouched and returns true.
 *
 * @note   If no TX data is queued, this function performs no hardware action and
 *         returns true.
 *
 * @note   This implementation does not use HAL for TX transfer setup. It directly
 *         controls the DMA stream and UART DMA request using LL functions and
 *         register access.
 */
bool HW_UART_Tx_Trigger( HwUartChannel_T channel );

/**
 * @brief  Reports whether the low level driver still owns any TX bytes.
 *
 * @param  channel The UART channel to inspect.
 *
 * @return true if TX bytes remain queued or in flight.
 * @return false if the low level driver owns no TX bytes for this channel.
 *
 * @note   Execution path function. Assumes valid input.
 *
 * @note   Contract:
 *         The caller must provide a valid UART channel.
 *         The channel must already be configured for TX.
 *
 * @note   This function reflects low level TX ownership state only. A true return
 *         means bytes remain in the TX DMA source ring buffer or a DMA transfer
 *         is active.
 *
 * @note   This function does not prove that the final UART stop bit has left the
 *         wire.
 *
 * @note   New TX data may still be queued while this function returns true,
 *         provided sufficient free space remains in the TX buffer.
 */
bool HW_UART_Is_Tx_Busy( HwUartChannel_T channel );

/**
 * @brief  Stops UART reception for the specified channel and halts DMA-based RX.
 *
 * @param  channel The UART channel to stop reception on.
 *
 * @return true if RX was successfully stopped.
 * @return false if the channel is invalid, not configured, RX is not running,
 *         or the underlying HAL stop operation fails.
 *
 * @note   This function is intended for non-hot-path lifecycle control.
 *
 * @note   The channel configuration remains valid after RX is stopped. Reception
 *         may be started again later with HW_UART_Rx_Start().
 */
bool HW_UART_Rx_Stop( HwUartChannel_T channel );

/**
 * @brief  Reports whether UART RX is currently active on the specified channel.
 *
 * @param  channel The UART channel to inspect.
 *
 * @return true if the channel is configured and RX is currently running.
 * @return false if the channel is invalid, not configured, or RX is not active.
 *
 * @note   This function is intended as a lightweight lifecycle query for
 *         higher-level sequencing logic.
 */
bool HW_UART_Rx_Is_Running( HwUartChannel_T channel );

#ifdef __cplusplus
}
#endif

#endif /* HW_UART_DUT_H */
