/******************************************************************************
 *  File:       hw_spi.h
 *  Author:     Angus Corr
 *  Created:    10-Apr-2026
 *
 *  Description:
 *      Public interface for the low-level SPI driver used by the HIL-RIG
 *      firmware.
 *
 *      This module exposes configuration and runtime control functions for the
 *      supported SPI peripherals, along with a generic RX peek/consume API and
 *      TX load/trigger API. The interface is designed to present SPI traffic as
 *      a raw byte stream and to hide the underlying DMA and peripheral control
 *      details from higher-level software.
 *
 *      The driver supports both 8-bit and 16-bit SPI data sizes while keeping
 *      the public RX and TX interfaces byte-based. Higher-level software is
 *      responsible for protocol framing, message construction/parsing,
 *      scheduling decisions, and correct semantic use of the configured SPI
 *      channel.
 *
 *  Notes:
 *      - This is a low-level transport-style driver, not a protocol driver.
 *      - RX data is exposed as unread spans into an internal DMA-backed buffer.
 *      - TX data is copied into an internal queue before being transmitted.
 *      - The caller does not retain ownership of returned RX span storage and
 *        must not modify it.
 *      - In 16-bit SPI mode, TX buffer load sizes and RX consume sizes must be
 *        multiples of 2 bytes.
 *      - RX span lengths are always reported in bytes, including in 16-bit
 *        mode.
 *      - The driver does not define packet/message boundaries.
 *      - The driver does not perform byte swapping or data repacking for
 *        16-bit mode; higher-level software must provide data in the intended
 *        in-memory order.
 *      - A channel must be configured before it is started or used.
 ******************************************************************************/

#ifndef HW_SPI_H
#define HW_SPI_H

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
// Add any needed standard or project-specific includes here

/**-----------------------------------------------------------------------------
 *  Public Defines / Macros
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Public Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */
typedef enum SPIBaudRate_T
{
    SPI_BAUD_45MBIT,
    SPI_BAUD_22M5BIT,
    SPI_BAUD_11M25BIT,
    SPI_BAUD_5M625BIT,
    SPI_BAUD_2M813BIT,
    SPI_BAUD_1M406BIT,
    SPI_BAUD_703KBIT,
    SPI_BAUD_352KBIT,
} SPIBaudRate_T;

typedef enum SPICPOL_T
{
    SPI_CPOL_LOW,
    SPI_CPOL_HIGH,
} SPICPOL_T;

typedef enum SPICPHA_T
{
    SPI_CPHA_1_EDGE,
    SPI_CPHA_2_EDGE,
} SPICPHA_T;

typedef enum SPIDataSize_T
{
    SPI_SIZE_8_BIT,
    SPI_SIZE_16_BIT,
} SPIDataSize_T;

typedef enum SPIFirstBit_T
{
    SPI_FIRST_MSB,
    SPI_FIRST_LSB,
} SPIFirstBit_T;

typedef enum SPIMode_T
{
    SPI_MASTER_MODE,
    SPI_SLAVE_MODE,
} SPIMode_T;

typedef enum SPIPeripheral_T
{
    SPI_CHANNEL_0,
    SPI_CHANNEL_1,
    SPI_DAC,
} SPIPeripheral_T;

typedef struct HWSPIConfig_T
{
    SPIMode_T     spi_mode;
    SPIDataSize_T data_size;
    SPIFirstBit_T first_bit;
    SPIBaudRate_T baud_rate;
    SPICPOL_T     cpol;
    SPICPHA_T     cpha;
} HWSPIConfig_T;

typedef struct
{
    const uint8_t* data;          // Pointer to the start of the unread data span
    uint32_t       length_bytes;  // Length of the unread data span in bytes
} HWSPIRxSpan_T;

typedef struct
{
    HWSPIRxSpan_T first_span;   // First contiguous unread span
    HWSPIRxSpan_T second_span;  // Second contiguous unread span, non-zero only if wrapping occurs
    uint32_t      total_length_bytes;  // Total unread bytes across both spans
} HWSPIRxSpans_T;

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

/**
 * @brief Configure a hardware SPI channel and initialise its low-level driver state.
 *
 * Applies the provided configuration to the selected SPI peripheral and stores
 * the configuration and hardware resource mappings required by the low-level
 * SPI driver.
 *
 * This function is responsible for:
 * - selecting the SPI hardware instance associated with the requested channel,
 * - storing the requested SPI configuration in the channel state,
 * - storing the DMA resources associated with the channel,
 * - configuring the STM32 HAL SPI handle,
 * - and initialising the peripheral using HAL_SPI_Init().
 *
 * This function prepares the channel for later runtime use but does not start
 * continuous RX DMA or begin any TX activity. After successful configuration,
 * HW_SPI_Start_Channel() must be called before the channel is used.
 *
 * This low-level driver does not enforce higher-level protocol semantics. In
 * particular, although the configuration includes master/slave mode, the
 * generic TX queue and RX stream APIs exposed by this module are intentionally
 * mode-agnostic at the public interface level. Higher-level software is
 * responsible for ensuring correct use of the channel according to the
 * configured mode.
 *
 * @param peripheral
 *     The SPI peripheral/channel to configure.
 *
 * @param configuration
 *     The SPI configuration to apply to the selected channel.
 *
 * @return
 *     true if configuration and hardware initialisation completed successfully.
 *     false if the peripheral selection was invalid, the configuration was not
 *     supported, or HAL initialisation failed.
 */
bool HW_SPI_Configure_Channel( SPIPeripheral_T peripheral, HWSPIConfig_T configuration );

/**
 * @brief Start runtime operation of a configured SPI channel.
 *
 * Enables the runtime behaviour required by this low-level SPI driver for the
 * selected channel.
 *
 * In the current driver design, starting a channel places its RX path into the
 * driver's continuous receive mode by starting DMA reception into the internal
 * RX buffer. This allows higher-level software to later inspect the received
 * byte stream using HW_SPI_Rx_Peek() and HW_SPI_Rx_Consume().
 *
 * This function does not start any TX transfer. Transmit activity is initiated
 * separately using HW_SPI_Load_Tx_Buffer() and HW_SPI_Tx_Trigger().
 *
 * This function does not impose message framing, protocol parsing, chip-select
 * policy, or higher-level scheduling semantics. Those responsibilities belong
 * to the software layer above this driver.
 *
 * When the channel is configured for 16-bit SPI operation, @p bytes_to_consume
 * must be a multiple of 2 bytes so that the software consume position remains
 * aligned to SPI frames.
 *
 * The channel should only be started after successful configuration.
 *
 * @param peripheral
 *     The SPI peripheral/channel to start.
 */
void HW_SPI_Start_Channel( SPIPeripheral_T peripheral );

/**
 * @brief Stop runtime operation of a configured SPI channel.
 *
 * Stops the active low-level runtime mechanisms used by the selected SPI
 * channel, such as DMA-based reception and any other ongoing SPI/DMA activity
 * managed by this driver.
 *
 * This function is intended to place the channel into a stopped state in which
 * its continuous RX path is no longer active and no further low-level activity
 * is expected until HW_SPI_Start_Channel() is called again.
 *
 * This function does not clear higher-level protocol state, message assembly
 * state, or any interpretation of queued/transferred data. Those concerns are
 * owned by higher-level software.
 *
 * @param peripheral
 *     The SPI peripheral/channel to stop.
 */
void HW_SPI_Stop_Channel( SPIPeripheral_T peripheral );

/**
 * @brief Return the unread received data as one or two spans into the internal RX buffer.
 *
 * Provides a read-only view of the unread portion of the selected channel's
 * internal RX DMA buffer without consuming any data.
 *
 * The RX buffer is treated as a circular DMA-backed byte stream. Because the
 * unread data may wrap around the end of the circular buffer, this function
 * returns up to two spans describing the unread region.
 *
 * The returned pointers refer to memory owned by the low-level driver. The
 * caller must not modify this memory and must copy it if persistence is
 * required beyond the immediate processing window.
 *
 * This function does not advance the RX consume position. After higher-level
 * software has processed some or all of the unread bytes, it must call
 * HW_SPI_Rx_Consume() to mark them as consumed.
 *
 * This function does not define message boundaries or protocol framing. It
 * exposes only the raw unread byte stream currently captured by the channel's
 * RX path.
 *
 * The returned spans are always expressed in bytes, even when the SPI channel is
 * configured for 16-bit operation. In 16-bit mode, the unread spans remain
 * frame-aligned.
 *
 * @param peripheral
 *     The SPI peripheral/channel to inspect.
 *
 * @return
 *     A structure describing the unread RX data as one or two spans.
 */
HWSPIRxSpans_T HW_SPI_Rx_Peek( SPIPeripheral_T peripheral );

/**
 * @brief Mark previously received RX bytes as consumed.
 *
 * Advances the internal unread-data consume position for the selected channel
 * by the specified number of bytes.
 *
 * This function provides the "consume" stage of the RX peek/consume model. It
 * is intended to be called after higher-level software has inspected or parsed
 * data returned by HW_SPI_Rx_Peek() and determined that some number of unread
 * bytes can be discarded from the unread region.
 *
 * This function only updates the driver's software consume position. It does
 * not copy data and does not modify the DMA hardware write position.
 *
 * This function does not impose message boundaries or protocol semantics. It
 * only advances the low-level driver's view of which RX bytes remain unread.
 *
 * If @p bytes_to_consume exceeds the actual unread byte count, behaviour is
 * implementation-defined unless explicitly guarded by higher-level software.
 *
 * @param peripheral
 *     The SPI peripheral/channel whose unread RX data should be advanced.
 *
 * @param bytes_to_consume
 *     The number of unread RX bytes to mark as consumed.
 */
void HW_SPI_Rx_Consume( SPIPeripheral_T peripheral, uint32_t bytes_to_consume );

/**
 * @brief Load data into the channel's internal transmit queue.
 *
 * Copies the supplied data into the selected channel's internal TX buffer so
 * that it may later be transmitted when HW_SPI_Tx_Trigger() is called.
 *
 * The provided data is copied into internal driver-owned storage. The caller
 * therefore retains ownership of @p data and may modify or discard the source
 * buffer after this function returns.
 *
 * This function does not immediately start SPI transmission. It only appends
 * bytes to the low-level driver's internal TX queue.
 *
 * The TX buffer used by this driver is a linear software queue rather than a
 * circular queue. Buffered data remains in the queue until transmitted by the
 * TX engine and the queue is reset back to empty when all queued data has been
 * sent.
 *
 * This function does not define message framing or protocol semantics. It only
 * stores raw bytes to be shifted out by the SPI peripheral. Higher-level
 * software is responsible for deciding what those bytes mean and when queued
 * transmission should be triggered.
 *
 * @param peripheral
 *     The SPI peripheral/channel whose TX queue is to be updated.
 *
 * @param data
 *     Pointer to the source data to copy into the internal TX buffer.
 *
 * @param size
 *     Number of bytes to copy from @p data into the internal TX buffer.
 *
 * @return
 *     true if the data was accepted and loaded successfully.
 *     false if the buffer could not accept the requested data or the channel
 *     selection was invalid.
 */
bool HW_SPI_Load_Tx_Buffer( SPIPeripheral_T peripheral, const uint8_t* data, uint32_t size );

/**
 * @brief Trigger transmission of queued TX data for a channel.
 *
 * Starts the transmit DMA for the selected SPI channel if queued TX data is
 * available and no transmit DMA transfer is currently in progress.
 *
 * This function provides the "trigger" stage of the driver's TX queue model.
 * It is intended to be called by higher-level software after one or more calls
 * to HW_SPI_Load_Tx_Buffer().
 *
 * If a transmit DMA transfer is already active, this function does not restart,
 * interrupt, or modify the current transfer. In that case, the existing TX
 * activity is left in progress.
 *
 * If no queued transmit data is available, this function does nothing.
 *
 * Once started, transmit progression is managed by the TX DMA completion IRQ
 * handler. If additional bytes have been appended to the TX queue while a
 * transfer is in progress, the IRQ handler may re-arm the DMA to continue
 * transmitting the remaining queued bytes.
 *
 * This function only starts transmission of data already stored in the
 * internal TX queue. It does not define message boundaries, chip-select
 * policy, or higher-level framing semantics.
 *
 * @param peripheral
 *     The SPI peripheral/channel whose queued TX data should be transmitted.
 */
void HW_SPI_Tx_Trigger( SPIPeripheral_T peripheral );

#ifdef __cplusplus
}
#endif

#endif /* HW_SPI_H */
