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
 *      - Master packets are required to be contiguous in tx_buffer so a later
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

static inline bool
HW_SPI_TX_Packet_Queue_Has_Free_Slot( const SPIPeripheralState_T* peripheral_state );
static uint32_t
HW_SPI_TX_Get_Contiguous_Free_Bytes_From_Index( const SPIPeripheralState_T* peripheral_state,
                                                uint32_t                    write_index );

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

static inline bool
HW_SPI_TX_Packet_Queue_Has_Free_Slot( const SPIPeripheralState_T* peripheral_state )
{
    return peripheral_state->tx_num_packets_pending < TX_PACKET_QUEUE_DEPTH;
}

static uint32_t
HW_SPI_TX_Get_Contiguous_Free_Bytes_From_Index( const SPIPeripheralState_T* peripheral_state,
                                                uint32_t                    write_index )
{
    if ( HW_SPI_TX_Get_Used_Space( peripheral_state ) == TX_BUFFER_SIZE_BYTES )
    {
        return 0U;
    }

    /*
     * If the candidate write index is behind the read position, the contiguous
     * free region ends at the read position. This prevents queued data or
     * in-flight DMA-owned data from being overwritten after wrapping.
     */
    if ( write_index < peripheral_state->tx_read_position )
    {
        return peripheral_state->tx_read_position - write_index;
    }

    /*
     * Otherwise, the contiguous free region runs to the end of tx_buffer. If a
     * master packet does not fit here, the load path may wrap the whole packet
     * to index 0 and intentionally leave the tail bytes unused.
     */
    return TX_BUFFER_SIZE_BYTES - write_index;
}

/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */

/**
 * @brief Check whether the master packet queue has pending packet descriptors.
 *
 * In master mode, DMA is deliberately armed packet-by-packet instead of sending
 * all available contiguous TX bytes. This preserves transaction boundaries for
 * a future software chip-select layer without inserting delimiter bytes into
 * the transmitted SPI stream.
 *
 * @param peripheral_state
 *     SPI channel state containing the master packet descriptor queue.
 *
 * @return
 *     true if at least one master packet descriptor is pending; false otherwise.
 */
bool HW_SPI_TX_Master_Has_Pending( const SPIPeripheralState_T* peripheral_state )
{
    return peripheral_state->tx_num_packets_pending > 0U;
}

/**
 * @brief Load one contiguous master-mode TX packet into the software TX buffer.
 *
 * Each public load call is treated as one logical master packet. Packets are not
 * allowed to wrap inside tx_buffer so one packet can later be transmitted by one
 * DMA transfer.
 */
bool HW_SPI_TX_Load_Master_Packet( SPIPeripheralState_T* peripheral_state, const uint8_t* data,
                                   uint32_t size )
{
    uint32_t packet_start    = 0U;
    uint32_t contiguous_free = 0U;

    if ( HW_SPI_Is_Frame_Aligned_Size( peripheral_state, size ) == false )
    {
        return false;
    }

    if ( HW_SPI_TX_Packet_Queue_Has_Free_Slot( peripheral_state ) == false )
    {
        return false;
    }

    if ( size > HW_SPI_TX_Get_Free_Space( peripheral_state ) )
    {
        return false;
    }

    contiguous_free = HW_SPI_TX_Get_Contiguous_Free_Bytes_From_Index(
        peripheral_state, peripheral_state->tx_write_position );

    if ( size > contiguous_free )
    {
        /*
         * Preserve the contiguous-packet rule by wrapping the entire packet to
         * the start of the raw TX buffer. If the write pointer is already behind
         * the read pointer, wrapping to zero could overwrite queued data.
         */
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
        ( peripheral_state->tx_packet_write_position + 1U ) % TX_PACKET_QUEUE_DEPTH;
    peripheral_state->tx_num_packets_pending++;

    peripheral_state->tx_write_position =
        ( peripheral_state->tx_write_position + size ) % TX_BUFFER_SIZE_BYTES;
    peripheral_state->tx_num_bytes_pending = peripheral_state->tx_num_bytes_pending + size;

    return true;
}

/**
 * @brief Start a master-mode TX DMA transfer for exactly one queued packet.
 *
 * Master TX progresses from the packet descriptor queue. Each DMA transfer is
 * therefore one logical packet, preserving transaction boundaries for the later
 * software chip-select layer.
 */
bool HW_SPI_TX_Start_Master_Packet_DMA( SPIPeripheralState_T* peripheral_state )
{
    SPITxPacketDescriptor_T* packet            = NULL;
    uint8_t*                 tx_ptr            = NULL;
    uint32_t                 packet_size_bytes = 0U;

    if ( peripheral_state->tx_num_bytes_in_transmission > 0U )
    {
        return false;
    }

    if ( HW_SPI_TX_Master_Has_Pending( peripheral_state ) == false )
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

    /*
     * Move exactly this packet out of the pending software state and into the
     * in-flight DMA state. The descriptor is consumed before DMA is armed so the
     * IRQ can immediately start the next queued packet if required.
     */
    peripheral_state->tx_packet_read_position =
        ( peripheral_state->tx_packet_read_position + 1U ) % TX_PACKET_QUEUE_DEPTH;
    peripheral_state->tx_num_packets_pending--;

    peripheral_state->tx_num_bytes_pending =
        peripheral_state->tx_num_bytes_pending - packet_size_bytes;
    peripheral_state->tx_num_bytes_in_transmission = packet_size_bytes;

    peripheral_state->tx_read_position =
        ( packet->start_index + packet_size_bytes ) % TX_BUFFER_SIZE_BYTES;

    /*
     * Descriptor clearing is for debug/readability only. Descriptor ownership is
     * controlled by tx_packet_read_position and tx_num_packets_pending.
     */
    packet->start_index = 0U;
    packet->size_bytes  = 0U;

    return HW_SPI_TX_Program_DMA( peripheral_state, tx_ptr, packet_size_bytes );
}
