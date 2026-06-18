/******************************************************************************
 *  File:       hw_spi_tx_slave.c
 *  Author:     Angus Corr
 *  Created:    10-Apr-2026
 *
 *  Description:
 *      Slave-mode TX implementation for the low-level SPI driver used by the
 *      HIL-RIG firmware.
 *
 *      This file owns slave stream loading and slave stream DMA start logic.
 *      Slave-mode TX preserves the original byte-stream behaviour: queued data
 *      may wrap in the TX ring and is transmitted as one or more contiguous DMA
 *      spans.
 *
 *      The shared TX configuration, public TX API wrappers, DMA IRQ entry
 *      points, and common DMA programming helper live in hw_spi_tx_config.c.
 *
 *  Notes:
 *      - In slave mode, TX is treated as a raw byte stream rather than a packet
 *        queue.
 *      - If queued data wraps around the end of tx_buffer, DMA sends the next
 *        contiguous span first and the TX DMA IRQ can re-arm DMA for the wrapped
 *        span starting at index 0.
 *      - Chip-select framing is not owned by this path because an external SPI
 *        master controls NSS/transaction boundaries.
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
 * @brief Return the next contiguous slave TX span length.
 *
 * @details
 *     Slave TX uses the original byte-stream ring-buffer model. DMA can only be
 *     programmed with one linear span, so this helper returns the number of
 *     bytes available from tx_read_position until either pending data ends or
 *     tx_buffer wraps.
 *
 * @param peripheral_state
 *     SPI channel state containing slave TX ring positions and byte counts.
 *
 * @return
 *     Number of contiguous bytes available for the next slave TX DMA transfer.
 */
HW_SPI_ALWAYS_INLINE uint32_t
HW_SPI_TX_Get_Contiguous_Read_Bytes( const SPIPeripheralState_T* peripheral_state );

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

HW_SPI_ALWAYS_INLINE uint32_t
HW_SPI_TX_Get_Contiguous_Read_Bytes( const SPIPeripheralState_T* peripheral_state )
{
    uint32_t bytes_until_end = 0U;

    if ( peripheral_state->tx_num_bytes_pending == 0U )
    {
        return 0U;
    }

    // DMA can only be programmed with a single linear memory span. If the TX
    // ring has wrapped, only the bytes from tx_read_position to the end of the
    // buffer are returned here. The IRQ will start another transfer for the
    // wrapped span.
    bytes_until_end = TX_BUFFER_SIZE_BYTES - peripheral_state->tx_read_position;

    if ( peripheral_state->tx_num_bytes_pending < bytes_until_end )
    {
        return peripheral_state->tx_num_bytes_pending;
    }

    return bytes_until_end;
}

/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */

/**
 * @brief Load slave-mode byte-stream TX data into the software TX buffer.
 *
 * @details
 *     This keeps the original stream behaviour: the source data may wrap inside
 *     the TX ring and later be sent as one or more contiguous DMA spans. Unlike
 *     master mode, this path does not create packet descriptors or software-CS
 *     transaction boundaries.
 *
 * @param peripheral_state
 *     SPI channel state containing the slave TX stream queue.
 *
 * @param data
 *     Caller-owned data to copy into tx_buffer.
 *
 * @param size
 *     Number of bytes to append to the stream. Must be aligned to the configured
 *     SPI frame size.
 *
 * @return
 *     true if the bytes were queued; false if alignment or free-space checks
 *     failed.
 */
bool HW_SPI_TX_Load_Slave_Stream( SPIPeripheralState_T* peripheral_state, const uint8_t* data,
                                  uint32_t size )
{
    uint32_t first_copy_size  = 0U;
    uint32_t second_copy_size = 0U;

    if ( HW_SPI_Is_Frame_Aligned_Size_Fast( peripheral_state, size ) == false )
    {
        return false;
    }

    if ( size > HW_SPI_TX_Get_Free_Space_Fast( peripheral_state ) )
    {
        return false;
    }

    first_copy_size = TX_BUFFER_SIZE_BYTES - peripheral_state->tx_write_position;
    if ( first_copy_size > size )
    {
        first_copy_size = size;
    }

    memcpy( &( peripheral_state->tx_buffer[peripheral_state->tx_write_position] ), data,
            first_copy_size );

    second_copy_size = size - first_copy_size;
    if ( second_copy_size > 0U )
    {
        memcpy( &( peripheral_state->tx_buffer[0] ), &( data[first_copy_size] ), second_copy_size );
    }

    peripheral_state->tx_write_position =
        HW_SPI_Wrap_Tx_Buffer_Index( peripheral_state->tx_write_position + size );
    peripheral_state->tx_num_bytes_pending = peripheral_state->tx_num_bytes_pending + size;

    return true;
}

/**
 * @brief Start a slave-mode TX DMA transfer for the next contiguous stream span.
 *
 * @details
 *     This is the original byte-stream behaviour separated from master packet
 *     TX. The function hands one contiguous span to DMA, updates the TX ring
 *     positions, and leaves any wrapped remainder pending for a later DMA TC
 *     re-arm.
 *
 * @param peripheral_state
 *     SPI channel state containing the slave stream queue and DMA resources.
 *
 * @return
 *     true if a contiguous stream span was handed to DMA; false if no data was
 *     available or a transfer was already active.
 */
bool HW_SPI_TX_Start_Slave_Stream_DMA( SPIPeripheralState_T* peripheral_state )
{
    uint32_t bytes_to_send = 0U;
    uint8_t* tx_ptr        = NULL;

    if ( peripheral_state->tx_num_bytes_in_transmission > 0U )
    {
        return false;
    }

    bytes_to_send = HW_SPI_TX_Get_Contiguous_Read_Bytes( peripheral_state );
    if ( bytes_to_send == 0U )
    {
        return false;
    }

    tx_ptr = &( peripheral_state->tx_buffer[peripheral_state->tx_read_position] );

    peripheral_state->tx_read_position =
        HW_SPI_Wrap_Tx_Buffer_Index( peripheral_state->tx_read_position + bytes_to_send );
    peripheral_state->tx_num_bytes_pending = peripheral_state->tx_num_bytes_pending - bytes_to_send;
    peripheral_state->tx_num_bytes_in_transmission = bytes_to_send;

    return HW_SPI_TX_Program_DMA( peripheral_state, tx_ptr, bytes_to_send );
}
