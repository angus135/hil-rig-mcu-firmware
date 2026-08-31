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
#include "logic_expander.h"

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
    EXEC_SPI_STATE_DISABLED = 0U,
    EXEC_SPI_STATE_CONFIGURED,
    EXEC_SPI_STATE_STARTED,
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

typedef struct ExecSPIHardwareMap_T
{
    LogicExpanderIndex_T expander;
    LogicExpanderPort_T  port;
    uint8_t              enable_bit;
    uint8_t              master_nslave_bit;
} ExecSPIHardwareMap_T;

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

static const ExecSPIHardwareMap_T exec_spi_hardware_map[2U] = {
    [SPI_CHANNEL_0] =
        {
            .expander          = LOGIC_EXPANDER_PWM_SPI,
            .port              = LOGIC_EXPANDER_PORT_B,
            .enable_bit        = 6U,
            .master_nslave_bit = 7U,
        },
    [SPI_CHANNEL_1] =
        {
            .expander          = LOGIC_EXPANDER_PWM_SPI,
            .port              = LOGIC_EXPANDER_PORT_B,
            .enable_bit        = 4U,
            .master_nslave_bit = 5U,
        },
};

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
static inline EXECSPIState_T* EXEC_SPI_Get_State( SPIChannel_T peripheral );

static bool EXEC_SPI_Apply_Interface_Control( SPIChannel_T peripheral, bool is_enabled,
                                              SPIMode_T spi_mode );

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

static inline EXECSPIState_T* EXEC_SPI_Get_State( SPIChannel_T peripheral )
{
    switch ( peripheral )
    {
        case SPI_CHANNEL_0:
            return &spi_channel_0_state;

        case SPI_CHANNEL_1:
            return &spi_channel_1_state;

        default:
            // Configuration is not a hot-path operation, so keep this guard for
            // invalid peripheral IDs rather than dereferencing an invalid state.
            return NULL;
    }
}

static bool EXEC_SPI_Apply_Interface_Control( SPIChannel_T peripheral, bool is_enabled,
                                              SPIMode_T spi_mode )
{
    const ExecSPIHardwareMap_T* hardware;
    bool                        master_nslave;

    if ( peripheral != SPI_CHANNEL_0 && peripheral != SPI_CHANNEL_1 )
    {
        return false;
    }

    if ( spi_mode == SPI_MASTER_MODE )
    {
        master_nslave = true;
    }
    else if ( spi_mode == SPI_SLAVE_MODE )
    {
        master_nslave = false;
    }
    else
    {
        return false;
    }

    hardware = &exec_spi_hardware_map[( uint32_t )peripheral];

    if ( LOGIC_EXPANDER_Load_Control_Bit( hardware->expander, hardware->port,
                                          hardware->master_nslave_bit, master_nslave )
         != LOGIC_EXPANDER_STATUS_OK )
    {
        return false;
    }

    if ( LOGIC_EXPANDER_Load_Control_Bit( hardware->expander, hardware->port, hardware->enable_bit,
                                          is_enabled )
         != LOGIC_EXPANDER_STATUS_OK )
    {
        return false;
    }

    /*
     * Remove this direct send when Logic Expander changes are committed by the
     * global configuration operation.
     */
    return LOGIC_EXPANDER_Send_Control_Bits() == LOGIC_EXPANDER_STATUS_OK;
}

/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */

/**
 * @brief Configure or disable an execution SPI channel.
 *
 * Enabled configuration applies the external master/slave selection while
 * keeping SPI_EN low, then configures HW SPI in its stopped state. Disabled
 * configuration applies the safe external state and stops a started channel.
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
 * @param config
 *     Execution enable request and low-level SPI configuration.
 *
 * @return
 *     true if configuration or the safe disabled state was applied.
 *     false if validation or a Logic Expander/HW SPI operation failed.
 */
bool EXEC_SPI_Configure_Channel( SPIChannel_T peripheral, const ExecSPIConfig_T* config )
{
    EXECSPIState_T* state = EXEC_SPI_Get_State( peripheral );

    if ( state == NULL || config == NULL )
    {
        return false;
    }

    if ( !config->is_enabled )
    {
        if ( state->state == EXEC_SPI_STATE_STARTED )
        {
            if ( !EXEC_SPI_Stop_Channel( peripheral ) )
            {
                return false;
            }
        }

        /*
         * Disabled safe state:
         * - SPI_EN = 0
         * - SPI_EN_MASTER_NSLAVE = 0 (slave)
         */
        if ( !EXEC_SPI_Apply_Interface_Control( peripheral, false, SPI_SLAVE_MODE ) )
        {
            return false;
        }

        /*
         * HW SPI currently has no deconfigure operation. Its previous
         * configuration is retained, but the external interface is disabled.
         */
        state->state = EXEC_SPI_STATE_DISABLED;

        return true;
    }

    /*
     * Reconfiguration while running is rejected. The caller must explicitly
     * stop the channel first.
     */
    if ( state->state == EXEC_SPI_STATE_STARTED )
    {
        return false;
    }

    /*
     * Select master/slave while keeping the external interface disabled.
     * SPI_EN is asserted only by EXEC_SPI_Start_Channel().
     */
    if ( !EXEC_SPI_Apply_Interface_Control( peripheral, false, config->hardware.spi_mode ) )
    {
        return false;
    }

    if ( !HW_SPI_Configure_Channel( peripheral, config->hardware ) )
    {
        /*
         * The external interface has already been placed in its disabled state.
         * Do not continue reporting the previous configuration as available.
         */
        state->state = EXEC_SPI_STATE_DISABLED;
        return false;
    }

    state->configuration = config->hardware;
    state->state         = EXEC_SPI_STATE_CONFIGURED;

    return true;
}

bool EXEC_SPI_Start_Channel( SPIChannel_T peripheral )
{
    EXECSPIState_T* state = EXEC_SPI_Get_State( peripheral );

    if ( state == NULL )
    {
        return false;
    }

    if ( state->state != EXEC_SPI_STATE_CONFIGURED )
    {
        return false;
    }

    /*
     * Start the MCU peripheral before connecting the external SPI interface.
     */
    if ( !HW_SPI_Start_Channel( peripheral ) )
    {
        return false;
    }

    if ( !EXEC_SPI_Apply_Interface_Control( peripheral, true, state->configuration.spi_mode ) )
    {
        /*
         * Best-effort rollback to the safe external state.
         */
        ( void )EXEC_SPI_Apply_Interface_Control( peripheral, false,
                                                  state->configuration.spi_mode );

        if ( !HW_SPI_Stop_Channel( peripheral ) )
        {
            /*
             * The HW channel could not be stopped, so preserve STARTED to
             * represent the uncertain active hardware state and allow Stop()
             * to be retried.
             */
            state->state = EXEC_SPI_STATE_STARTED;
        }

        return false;
    }

    state->state = EXEC_SPI_STATE_STARTED;

    return true;
}

bool EXEC_SPI_Stop_Channel( SPIChannel_T peripheral )
{
    EXECSPIState_T* state = EXEC_SPI_Get_State( peripheral );

    if ( state == NULL )
    {
        return false;
    }

    if ( state->state != EXEC_SPI_STATE_STARTED )
    {
        return false;
    }

    /*
     * Preserve valid queued or active transmission data. The caller may retry
     * after electrical completion.
     *
     * A faulted TX path cannot complete normally, so permit the terminating HW
     * stop to recover it.
     */
    if ( !HW_SPI_Tx_Is_Complete( peripheral ) && !HW_SPI_Tx_Is_Faulted( peripheral ) )
    {
        return false;
    }

    /*
     * Disconnect the external interface before stopping the MCU peripheral.
     * Preserve the configured master/slave selection for a later restart.
     */
    if ( !EXEC_SPI_Apply_Interface_Control( peripheral, false, state->configuration.spi_mode ) )
    {
        return false;
    }

    if ( !HW_SPI_Stop_Channel( peripheral ) )
    {
        /*
         * The external interface is disabled, but the low-level hardware state
         * is uncertain. Preserve STARTED so Stop() can be retried.
         */
        return false;
    }

    state->state = EXEC_SPI_STATE_CONFIGURED;

    return true;
}

bool EXEC_SPI_Is_Configured( SPIChannel_T peripheral )
{
    EXECSPIState_T* state = EXEC_SPI_Get_State( peripheral );

    if ( state == NULL )
    {
        return false;
    }

    return state->state == EXEC_SPI_STATE_CONFIGURED || state->state == EXEC_SPI_STATE_STARTED;
}

bool EXEC_SPI_Is_Started( SPIChannel_T peripheral )
{
    EXECSPIState_T* state = EXEC_SPI_Get_State( peripheral );

    if ( state == NULL )
    {
        return false;
    }

    return state->state == EXEC_SPI_STATE_STARTED;
}

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
                        const uint32_t* packet_sizes_bytes, uint32_t num_packets )
{
    /*
     * TODO: Submit variable-length packets atomically, preflighting descriptor,
     * storage and alignment limits under one IRQ critical section. A later
     * load failure currently leaves the accepted prefix queued (or transmitting);
     * retrying the whole operation can duplicate it. Each load also masks IRQs.
     */
    uint32_t data_offset_bytes = 0U;

    for ( uint32_t packet_index = 0U; packet_index < num_packets; packet_index++ )
    {
        const uint32_t packet_size_bytes = packet_sizes_bytes[packet_index];

        if ( !HW_SPI_Load_Tx_Buffer( peripheral, &data_src[data_offset_bytes], packet_size_bytes ) )
        {
            return false;
        }

        data_offset_bytes += packet_size_bytes;
    }

    /*
     * Trigger once after all packets have been queued. The low-level master TX
     * path will then drain the packet queue automatically, framing each loaded
     * packet with software CS. Calling trigger once also avoids repeated
     * IRQ-disable/enable overhead in the 100 us execution tick.
     */
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
bool EXEC_SPI_Receive( SPIChannel_T peripheral, uint8_t* data_dst, uint32_t* size_bytes )
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
bool EXEC_SPI_Is_Transmission_Complete( SPIChannel_T peripheral )
{
    return HW_SPI_Tx_Is_Complete( peripheral );
}
