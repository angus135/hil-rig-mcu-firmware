/******************************************************************************
 *  File:       exec_spi.h
 *  Author:     Angus Corr
 *  Created:    25-Apr-2026
 *
 *  Description:
 *      Execution-level SPI wrapper used by the HIL-RIG execution manager.
 *
 *      This module provides a lightweight interface between the execution
 *      manager and the low-level hardware SPI driver. It does not implement SPI
 *      protocol framing, transaction validation, chip-select policy, message
 *      scheduling, or higher-level semantic checks. Those responsibilities are
 *      handled by the execution validation and scheduling layers above this
 *      module.
 *
 *      The purpose of this module is to provide a small execution-facing API
 *      for configuring SPI channels, submitting raw TX bytes, copying available
 *      RX bytes, and checking whether the low-level transmit path has fully
 *      completed.
 *
 *  Notes:
 *      - This module is intentionally lightweight because it may be used by
 *        timing-sensitive execution-manager code.
 *      - The caller is responsible for validating peripheral selection,
 *        requested transfer sizes, configured SPI mode, frame alignment, buffer
 *        sizes, and protocol-level correctness before calling this module.
 *      - RX data is copied from the low-level driver's DMA-backed circular RX
 *        stream into caller-owned storage.
 *      - TX data is copied into the low-level driver's internal TX queue and
 *        then transmission is triggered.
 *      - In 16-bit SPI mode, callers must ensure TX and RX byte counts are
 *        aligned to complete SPI frames.
 ******************************************************************************/

#ifndef EXEC_SPI_H
#define EXEC_SPI_H

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
#include "hw_spi.h"

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
 * @brief Configure and start an execution SPI channel.
 *
 * Applies the requested low-level SPI configuration to the selected channel and
 * starts the low-level SPI runtime path.
 *
 * If the channel is already active, this function first stops the low-level SPI
 * channel before applying the new configuration. This allows the execution
 * layer to reconfigure a SPI channel between test phases without requiring the
 * caller to explicitly stop it first.
 *
 * This function is intended for setup/configuration time rather than the
 * 10 kHz execution hot path. It performs only minimal state handling and relies
 * on the validation subsystem above this layer to ensure that the requested
 * peripheral and configuration are valid for the test being executed.
 *
 * This function does not validate SPI mode semantics, message framing,
 * transfer sizes, chip-select policy, or 8-bit versus 16-bit data alignment.
 *
 * @param peripheral
 *     The SPI peripheral/channel to configure.
 *
 * @param configuration
 *     The low-level SPI configuration to apply.
 *
 * @return
 *     true if the low-level channel was configured and started successfully.
 *     false if the channel state could not be resolved, the current state was
 *     invalid, or the low-level driver failed to configure the channel.
 */
bool EXEC_SPI_Configure_Channel( SPIChannel_T peripheral, HWSPIConfig_T configuration );

/**
 * @brief Queue one or more SPI packets for transmission and trigger the TX path.
 *
 * Copies packetised source data into the low-level SPI driver's internal TX
 * queue, then triggers the low-level TX engine once after all packets have been
 * queued.
 *
 * The source data is provided as one contiguous byte array. Packet boundaries
 * are provided separately through @p packet_sizes_bytes. Each entry in
 * @p packet_sizes_bytes describes the size of one SPI packet inside
 * @p data_src.
 *
 * For master-mode SPI, each low-level HW_SPI_Load_Tx_Buffer() call becomes one
 * software-chip-select-framed SPI transaction. This function therefore calls
 * HW_SPI_Load_Tx_Buffer() once per packet, then calls HW_SPI_Tx_Trigger() only
 * once after all packet loads have completed.
 *
 * Example:
 * @code
 * const uint8_t data[] = {
 *     0x01, 0x02,        // packet 0, 2 bytes
 *     0xAA, 0xBB, 0xCC,  // packet 1, 3 bytes
 *     0x10               // packet 2, 1 byte
 * };
 *
 * const uint32_t packet_sizes[] = { 2U, 3U, 1U };
 *
 * EXEC_SPI_Transmit( SPI_CHANNEL_0, data, packet_sizes, 3U );
 * @endcode
 *
 * This function is intentionally a thin wrapper around the low-level
 * load/trigger sequence. It does not check whether the channel is configured,
 * whether the requested transfer is valid for the configured SPI mode, whether
 * each packet is frame-aligned in 16-bit mode, whether the total packet size is
 * correct for the caller's data buffer, or whether the operation is valid for
 * the current test schedule. Those checks are expected to be handled before this
 * function is called.
 *
 * The source data is copied into the low-level driver's internal TX queue, so
 * the caller does not need to keep the source buffer alive after this function
 * returns.
 *
 * @param peripheral
 *     The SPI peripheral/channel whose TX path should be used.
 *
 * @param data_src
 *     Pointer to the contiguous source bytes containing all packets back-to-back.
 *
 * @param packet_sizes_bytes
 *     Pointer to an array of packet sizes. Each entry gives the size, in bytes,
 *     of the corresponding SPI packet in @p data_src.
 *
 * @param num_packets
 *     Number of entries in @p packet_sizes_bytes.
 *
 * @return
 *     true if all packets were accepted by the low-level TX queue and
 *     transmission was triggered.
 *     false if any packet could not be accepted by the low-level TX queue.
 */
bool EXEC_SPI_Transmit( SPIChannel_T peripheral, const uint8_t* data_src,
                        const uint32_t* packet_sizes_bytes, uint32_t num_packets );

/**
 * @brief Copy all currently unread RX bytes from a SPI channel.
 *
 * Copies the unread RX byte stream currently exposed by the low-level SPI
 * driver into caller-owned storage, then consumes the copied bytes from the
 * low-level RX buffer.
 *
 * The low-level RX buffer may wrap around the end of its circular DMA storage,
 * so this function copies from up to two spans returned by HW_SPI_Rx_Peek().
 * After both spans have been copied, the same total byte count is passed to
 * HW_SPI_Rx_Consume() so that the low-level driver advances its software
 * consume position.
 *
 * The value pointed to by @p size_bytes is used as the destination buffer
 * capacity on entry. If the unread RX byte count is larger than this capacity,
 * no bytes are copied, no RX bytes are consumed, and false is returned.
 *
 * On success, @p size_bytes is updated to the number of bytes copied into
 * @p data_dst.
 *
 * This function does not define message boundaries or validate protocol-level
 * framing. It simply copies the raw unread RX bytes that are currently available
 * from the low-level SPI RX stream.
 *
 * @param peripheral
 *     The SPI peripheral/channel whose RX data should be copied.
 *
 * @param data_dst
 *     Pointer to caller-owned storage where unread RX bytes will be copied.
 *
 * @param size_bytes
 *     On entry, the capacity of @p data_dst in bytes.
 *     On success, updated to the number of bytes copied.
 *
 * @return
 *     true if all currently unread RX bytes fit in @p data_dst and were copied
 *     and consumed successfully.
 *     false if the unread RX byte count exceeds the provided destination
 *     capacity.
 */
bool EXEC_SPI_Receive( SPIChannel_T peripheral, uint8_t* data_dst, uint32_t* size_bytes );

/**
 * @brief Check whether the SPI transmit path has completed.
 *
 * Returns whether the selected SPI channel has no bytes waiting in the low-level
 * TX software queue and no bytes currently owned by an active TX DMA transfer.
 *
 * This function is a lightweight execution-level wrapper around the low-level
 * TX empty check. It does not inspect RX data, infer transaction completion,
 * apply protocol semantics, or update any execution-level transaction state.
 *
 * @param peripheral
 *     The SPI peripheral/channel whose TX completion state should be checked.
 *
 * @return
 *     true if the low-level TX path is empty and no transmission is in progress.
 *     false if bytes are still queued or currently being transmitted.
 */
bool EXEC_SPI_Is_Transmission_Complete( SPIChannel_T peripheral );

#ifdef __cplusplus
}
#endif

#endif /* EXEC_SPI_H */
