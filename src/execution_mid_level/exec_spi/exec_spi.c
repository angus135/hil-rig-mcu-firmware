/******************************************************************************
 *  File:       exec_spi.c
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

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include "exec_spi.h"
#include "hw_spi.h"

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

/**
 * @brief Execution-level state of a SPI channel.
 *
 * Tracks whether the execution SPI layer considers a channel configured and
 * active.
 *
 * This state is intentionally minimal. It is not a full transaction state
 * machine and does not attempt to track protocol-level transfer progress.
 */
typedef enum EXECSPIChannelState_T
{
    EXEC_SPI_STATE_UNCONFIGURED,
    EXEC_SPI_STATE_ACTIVE,
} EXECSPIChannelState_T;

/**
 * @brief Execution-level state container for one SPI channel.
 *
 * Stores the most recently applied low-level SPI configuration and the current
 * execution-level channel state.
 *
 * The stored configuration is not used for validation in the hot path. It is
 * retained so that the execution SPI layer has a local record of the active
 * channel configuration if required for later diagnostics, inspection, or
 * future lightweight behaviour.
 */
typedef struct EXECSPIState_T
{
    HWSPIConfig_T         configuration;
    EXECSPIChannelState_T state;
} EXECSPIState_T;

/**-----------------------------------------------------------------------------
 *  Public (global) and Extern Variables
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Private (static) Variables
 *------------------------------------------------------------------------------
 */

static EXECSPIState_T spi_channel_0_state = { 0 };
static EXECSPIState_T spi_channel_1_state = { 0 };
static EXECSPIState_T spi_dac_state       = { 0 };

/**-----------------------------------------------------------------------------
 *  Private (static) Function Prototypes
 *------------------------------------------------------------------------------
 */

/**
 * @brief Get the execution-level state object for a SPI peripheral.
 *
 * Maps a public SPI peripheral identifier to the private execution-level state
 * structure used by this module.
 *
 * This helper does not access the low-level hardware driver. It only resolves
 * the software state object associated with the requested SPI channel.
 *
 * @param peripheral
 *     The SPI peripheral/channel whose execution-level state should be
 *     returned.
 *
 * @return
 *     Pointer to the execution-level state structure for the requested channel.
 *     NULL if the peripheral identifier is not recognised.
 */
static inline EXECSPIState_T* EXEC_SPI_Get_State( SPIPeripheral_T peripheral );

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

static inline EXECSPIState_T* EXEC_SPI_Get_State( SPIPeripheral_T peripheral )
{
    switch ( peripheral )
    {
        case SPI_CHANNEL_0:
            return &spi_channel_0_state;

        case SPI_CHANNEL_1:
            return &spi_channel_1_state;

        case SPI_DAC:
            return &spi_dac_state;

        default:
            // Configuration is not a hot-path operation, so keep this guard for
            // invalid peripheral IDs rather than dereferencing an invalid state.
            return NULL;
    }
}

/**-----------------------------------------------------------------------------
 *  Public Function Definitions
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
bool EXEC_SPI_Configure_Channel( SPIPeripheral_T peripheral, HWSPIConfig_T configuration )
{
    EXECSPIState_T* state = EXEC_SPI_Get_State( peripheral );
    if ( state == NULL )
    {
        return false;
    }
    switch ( state->state )
    {
        case EXEC_SPI_STATE_UNCONFIGURED:
            // Nothing is currently active, so the channel can be configured directly.
            break;
        case EXEC_SPI_STATE_ACTIVE:
            // Reconfiguration is treated as stop-then-configure-then-start.
            HW_SPI_Stop_Channel( peripheral );
            break;
        default:
            return false;
    }

    if ( !HW_SPI_Configure_Channel( peripheral, configuration ) )
    {
        state->state = EXEC_SPI_STATE_UNCONFIGURED;
        return false;
    }

    // Start arms the low-level runtime path, including passive RX DMA capture.
    HW_SPI_Start_Channel( peripheral );
    state->state         = EXEC_SPI_STATE_ACTIVE;
    state->configuration = configuration;
    return true;
}

/**
 * @brief Queue raw bytes for SPI transmission and trigger the TX path.
 *
 * Copies the supplied bytes into the low-level SPI driver's internal TX queue
 * and then triggers the low-level TX DMA path.
 *
 * This function is intentionally a thin wrapper around the low-level
 * load/trigger sequence. It does not check whether the channel is configured,
 * whether the requested transfer is valid for the configured SPI mode, whether
 * the transfer is frame-aligned in 16-bit mode, or whether the operation is
 * valid for the current test schedule. Those checks are expected to be handled
 * before this function is called.
 *
 * The source data is copied into the low-level driver's internal TX queue, so
 * the caller does not need to keep the source buffer alive after this function
 * returns.
 *
 * @param peripheral
 *     The SPI peripheral/channel whose TX path should be used.
 *
 * @param data_src
 *     Pointer to the source bytes to queue for transmission.
 *
 * @param size_bytes
 *     Number of bytes to queue for transmission.
 *
 * @return
 *     true if the data was accepted by the low-level TX queue and transmission
 *     was triggered.
 *     false if the low-level TX queue could not accept the requested data.
 */
bool EXEC_SPI_Transmit( SPIPeripheral_T peripheral, const uint8_t* data_src, uint32_t size_bytes )
{
    if ( !HW_SPI_Load_Tx_Buffer( peripheral, data_src, size_bytes ) )
    {
        return false;
    }

    // Trigger is separate from loading so the low-level driver can avoid
    // restarting DMA if a transfer is already active.
    HW_SPI_Tx_Trigger( peripheral );
    return true;
}

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
bool EXEC_SPI_Receive( SPIPeripheral_T peripheral, uint8_t* data_dst, uint32_t* size_bytes )
{
    HWSPIRxSpans_T data_spans = HW_SPI_Rx_Peek( peripheral );
    if ( data_spans.total_length_bytes > *size_bytes )
    {
        // Do not partially copy or consume RX data if the caller's destination
        // buffer cannot hold the full unread RX stream.
        return false;
    }

    memcpy( data_dst, data_spans.first_span.data, data_spans.first_span.length_bytes );

    memcpy( data_dst + data_spans.first_span.length_bytes, data_spans.second_span.data,
            data_spans.second_span.length_bytes );

    *size_bytes = data_spans.total_length_bytes;

    // Consume exactly the bytes copied so the low-level RX stream and caller's
    // copied data remain consistent.
    HW_SPI_Rx_Consume( peripheral, data_spans.total_length_bytes );
    return true;
}

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
bool EXEC_SPI_Is_Transmission_Complete( SPIPeripheral_T peripheral )
{
    return HW_SPI_Tx_Buffer_Empty( peripheral );
}
