/******************************************************************************
 *  File:       hw_spi_tx_master.c
 *  Author:     Angus Corr
 *  Created:    10-Apr-2026
 *
 *  Description:
 *      Master-mode TX implementation for the low-level SPI driver used by the
 *      HIL-RIG firmware.
 *
 *      This file owns master packet loading and master packet DMA start logic.
 *      Master-mode TX preserves each public load call as one packet descriptor
 *      so transaction boundaries can later be paired with software chip-select
 *      handling without inserting delimiter bytes into the transmitted SPI
 *      stream.
 *
 *      The shared TX configuration, public TX API wrappers, DMA IRQ entry
 *      points, and common DMA programming helper live in hw_spi_tx_config.c.
 *
 *  Notes:
 *      - In master mode, each call to HW_SPI_Load_Tx_Buffer() is treated as one
 *        logical TX packet.
 *      - Master software chip-select is always automatic: each master packet is
 *        framed by CS and transmitted as exactly one DMA transfer.
 *      - Master packets are required to be contiguous in tx_buffer so the
 *        automatic software-CS path can map one packet to one DMA transfer.
 *      - If a packet does not fit before the end of tx_buffer, the load path may
 *        wrap the whole packet to index 0 and intentionally leave the unused
 *        tail bytes unused.
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#define HW_SPI_INTERNAL
#include "hw_spi.h"

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Public (global) and Extern Variables
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Private (static) Variables
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Private (static) Function Prototypes
 *------------------------------------------------------------------------------
 */

/**
 * @brief Check whether the master packet descriptor queue has room.
 *
 * @param peripheral_state
 *     SPI channel state containing the master packet queue.
 *
 * @return
 *     true when at least one descriptor slot is free; false when the queue is full.
 */
HW_SPI_ALWAYS_INLINE bool
HW_SPI_TX_Packet_Queue_Has_Free_Slot( const SPIPeripheralState_T* peripheral_state );

/**
 * @brief Return the free linear TX-buffer space from a candidate write index.
 *
 * @details
 *     Master packets must be contiguous so each packet can be represented by a
 *     single DMA transfer and a single software-CS transaction. This helper
 *     reports how many bytes can be written without crossing either the end of
 *     tx_buffer or unread/in-flight data.
 *
 * @param peripheral_state
 *     SPI channel state containing TX ring positions and byte counts.
 *
 * @param write_index
 *     Candidate byte index where a new master packet would start.
 *
 * @return
 *     Number of contiguous free bytes starting at @p write_index.
 */
HW_SPI_ALWAYS_INLINE uint32_t HW_SPI_TX_Get_Contiguous_Free_Bytes_From_Index(
    const SPIPeripheralState_T* peripheral_state, uint32_t write_index );

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

HW_SPI_ALWAYS_INLINE bool
HW_SPI_TX_Packet_Queue_Has_Free_Slot( const SPIPeripheralState_T* peripheral_state )
{
    return peripheral_state->tx_num_packets_pending < TX_PACKET_QUEUE_DEPTH;
}

HW_SPI_ALWAYS_INLINE uint32_t HW_SPI_TX_Get_Contiguous_Free_Bytes_From_Index(
    const SPIPeripheralState_T* peripheral_state, uint32_t write_index )
{
    if ( HW_SPI_TX_Get_Used_Space_Fast( peripheral_state ) == TX_BUFFER_SIZE_BYTES )
    {
        return 0U;
    }

    // If the candidate write index is behind the read position, the contiguous
    // free region ends at the read position. This prevents queued data or
    // in-flight DMA-owned data from being overwritten after wrapping.
    if ( write_index < peripheral_state->tx_read_position )
    {
        return peripheral_state->tx_read_position - write_index;
    }

    // Otherwise, the contiguous free region runs to the end of tx_buffer. If a
    // master packet does not fit here, the load path may wrap the whole packet
    // to index 0 and intentionally leave the tail bytes unused.
    return TX_BUFFER_SIZE_BYTES - write_index;
}

/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */

/**
 * @brief Load one contiguous master-mode TX packet into the software TX buffer.
 *
 * @details
 *     Each public load call is treated as one logical master packet. Packets are
 *     not allowed to wrap inside tx_buffer so one packet can later be
 *     transmitted by one DMA transfer and framed by one software-CS pulse. This
 *     function only queues the packet; HW_SPI_Tx_Trigger() or the TX completion
 *     chain starts transmission later.
 *
 * @param peripheral_state
 *     SPI channel state containing the master packet queue.
 *
 * @param data
 *     Caller-owned packet bytes to copy into tx_buffer.
 *
 * @param size
 *     Packet size in bytes. Must be aligned to the configured SPI frame size.
 *
 * @return
 *     true if the packet was queued; false if alignment, descriptor space, or
 *     buffer-space checks failed.
 */
bool HW_SPI_TX_Load_Master_Packet( SPIPeripheralState_T* peripheral_state, const uint8_t* data,
                                   uint32_t size )
{
    uint32_t packet_start    = 0U;
    uint32_t contiguous_free = 0U;

    if ( HW_SPI_Is_Frame_Aligned_Size_Fast( peripheral_state, size ) == false )
    {
        return false;
    }

    if ( HW_SPI_TX_Packet_Queue_Has_Free_Slot( peripheral_state ) == false )
    {
        return false;
    }

    if ( size > HW_SPI_TX_Get_Free_Space_Fast( peripheral_state ) )
    {
        return false;
    }

    contiguous_free = HW_SPI_TX_Get_Contiguous_Free_Bytes_From_Index(
        peripheral_state, peripheral_state->tx_write_position );

    if ( size > contiguous_free )
    {
        // Preserve the contiguous-packet rule by wrapping the entire packet to
        // the start of the raw TX buffer. If the write pointer is already behind
        // the read pointer, wrapping to zero could overwrite queued data.
        if ( peripheral_state->tx_write_position < peripheral_state->tx_read_position )
        {
            return false;
        }

        peripheral_state->tx_write_position = 0U;

        contiguous_free = HW_SPI_TX_Get_Contiguous_Free_Bytes_From_Index(
            peripheral_state, peripheral_state->tx_write_position );

        if ( size > contiguous_free )
        {
            return false;
        }
    }

    packet_start = peripheral_state->tx_write_position;

    memcpy( &( peripheral_state->tx_buffer[packet_start] ), data, size );

    peripheral_state->tx_packet_descriptors[peripheral_state->tx_packet_write_position]
        .start_index = ( uint16_t )packet_start;
    peripheral_state->tx_packet_descriptors[peripheral_state->tx_packet_write_position].size_bytes =
        ( uint16_t )size;

    peripheral_state->tx_packet_write_position =
        HW_SPI_Wrap_Tx_Packet_Index( peripheral_state->tx_packet_write_position + 1U );
    peripheral_state->tx_num_packets_pending++;

    peripheral_state->tx_write_position =
        HW_SPI_Wrap_Tx_Buffer_Index( peripheral_state->tx_write_position + size );
    peripheral_state->tx_num_bytes_pending = peripheral_state->tx_num_bytes_pending + size;

    return true;
}

/**
 * @brief Start a master-mode TX DMA transfer for exactly one queued packet.
 *
 * @details
 *     Master TX progresses from the packet descriptor queue. Starting a packet
 *     consumes one descriptor, moves the packet bytes from pending to in-flight,
 *     asserts software CS, and arms one TX DMA transfer. The DMA completion path
 *     is responsible for final-drain handling and CS release.
 *
 * @param peripheral_state
 *     SPI channel state containing the queued packet and DMA resources.
 *
 * @return
 *     true when one packet was successfully handed to DMA; false if no packet
 *     was available, another transaction was active, or DMA setup failed.
 */
bool HW_SPI_TX_Start_Master_Packet_DMA( SPIPeripheralState_T* peripheral_state )
{
    SPITxPacketDescriptor_T* packet            = NULL;
    uint8_t*                 tx_ptr            = NULL;
    uint32_t                 packet_size_bytes = 0U;

    if ( peripheral_state->tx_num_bytes_in_transmission > 0U
         || peripheral_state->tx_transaction_state != HW_SPI_TX_TRANSACTION_IDLE )
    {
        return false;
    }

    // Check if there are any packets pending
    if ( peripheral_state->tx_num_packets_pending == 0U )
    {
        return false;
    }

    packet =
        &( peripheral_state->tx_packet_descriptors[peripheral_state->tx_packet_read_position] );

    if ( packet->size_bytes == 0U )
    {
        return false;
    }

    packet_size_bytes = packet->size_bytes;
    tx_ptr            = &( peripheral_state->tx_buffer[packet->start_index] );

    // Move exactly this packet out of the pending software state and into the
    // in-flight DMA state. The descriptor is consumed before DMA is armed. The
    // DMA completion IRQ must not start another packet until the automatic-CS
    // completion path has waited for the final SPI frame to drain and released
    // CS for this packet.
    peripheral_state->tx_packet_read_position =
        HW_SPI_Wrap_Tx_Packet_Index( peripheral_state->tx_packet_read_position + 1U );
    peripheral_state->tx_num_packets_pending--;

    peripheral_state->tx_num_bytes_pending =
        peripheral_state->tx_num_bytes_pending - packet_size_bytes;
    peripheral_state->tx_num_bytes_in_transmission = packet_size_bytes;

    peripheral_state->tx_read_position =
        HW_SPI_Wrap_Tx_Buffer_Index( packet->start_index + packet_size_bytes );

    // Descriptor clearing is for debug/readability only. Descriptor ownership is
    // controlled by tx_packet_read_position and tx_num_packets_pending.
    packet->start_index = 0U;
    packet->size_bytes  = 0U;

    peripheral_state->tx_transaction_state = HW_SPI_TX_TRANSACTION_DMA_ACTIVE;

    // Master software CS is asserted immediately before arming the DMA transfer
    // for this packet. The actual GPIO access is intentionally hidden behind
    // HW_SPI_TX_Master_CS_Assert(), which should call the separate GPIO driver.
    HW_SPI_TX_Master_CS_Assert( peripheral_state );

    if ( HW_SPI_TX_Program_DMA( peripheral_state, tx_ptr, packet_size_bytes ) == false )
    {
        HW_SPI_TX_Master_CS_Deassert( peripheral_state );
        peripheral_state->tx_transaction_state         = HW_SPI_TX_TRANSACTION_ERROR;
        peripheral_state->tx_num_bytes_in_transmission = 0U;
        return false;
    }

    return true;
}
