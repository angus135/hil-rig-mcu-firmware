/******************************************************************************
 *  File:       hw_spi_tx.c
 *  Author:     Angus Corr
 *  Created:    10-Apr-2026
 *
 *  Description:
 *      TX-side implementation for the low-level SPI driver used by the HIL-RIG
 *      firmware.
 *
 *      This file contains master packet TX, slave stream TX, TX DMA IRQ
 *      handling, and public TX load/trigger/status functions. Shared state and
 *      common helpers are declared through hw_spi.h when HW_SPI_INTERNAL is
 *      enabled.
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

static inline void     HW_SPI_TX_Clear_Channel_0_DMA_Flags( DMA_TypeDef* dma );
static inline void     HW_SPI_TX_Clear_Channel_1_DMA_Flags( DMA_TypeDef* dma );
static inline void     HW_SPI_TX_Clear_DAC_DMA_Flags( DMA_TypeDef* dma );
static inline uint32_t HW_SPI_TX_Get_Free_Space( const SPIPeripheralState_T* peripheral_state );
static inline uint32_t
HW_SPI_TX_Get_Contiguous_Read_Bytes( const SPIPeripheralState_T* peripheral_state );
static inline bool
HW_SPI_TX_Packet_Queue_Has_Free_Slot( const SPIPeripheralState_T* peripheral_state );
static uint32_t
HW_SPI_TX_Get_Contiguous_Free_Bytes_From_Index( const SPIPeripheralState_T* peripheral_state,
                                                uint32_t                    write_index );
static inline bool HW_SPI_TX_Master_Has_Pending( const SPIPeripheralState_T* peripheral_state );
static inline bool HW_SPI_TX_Slave_Has_Pending( const SPIPeripheralState_T* peripheral_state );
static bool        HW_SPI_TX_Load_Master_Packet( SPIPeripheralState_T* peripheral_state,
                                                 const uint8_t* data, uint32_t size );
static bool        HW_SPI_TX_Load_Slave_Stream( SPIPeripheralState_T* peripheral_state,
                                                const uint8_t* data, uint32_t size );
static inline bool HW_SPI_TX_Program_DMA( SPIPeripheralState_T* peripheral_state, uint8_t* tx_ptr,
                                          uint32_t size_bytes );
static bool        HW_SPI_TX_Start_Master_Packet_DMA( SPIPeripheralState_T* peripheral_state );
static bool        HW_SPI_TX_Start_Slave_Stream_DMA( SPIPeripheralState_T* peripheral_state );

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

static inline void HW_SPI_TX_Clear_Channel_0_DMA_Flags( DMA_TypeDef* dma )
{
    SPI_CHANNEL_0_TX_DMA_CLEAR_TC( dma );
    SPI_CHANNEL_0_TX_DMA_CLEAR_TE( dma );
}

static inline void HW_SPI_TX_Clear_Channel_1_DMA_Flags( DMA_TypeDef* dma )
{
    SPI_CHANNEL_1_TX_DMA_CLEAR_TC( dma );
    SPI_CHANNEL_1_TX_DMA_CLEAR_TE( dma );
}

static inline void HW_SPI_TX_Clear_DAC_DMA_Flags( DMA_TypeDef* dma )
{
    SPI_DAC_TX_DMA_CLEAR_TC( dma );
    SPI_DAC_TX_DMA_CLEAR_TE( dma );
}

/**
 * @brief Generic low-level TX DMA error handler.
 *
 * This function is called when a TX DMA transfer error is detected for a SPI
 * channel. It stops the active TX DMA stream, disables the SPI TX DMA request,
 * and clears the driver's in-flight TX tracking so higher-level software can
 * decide how to recover.
 *
 * @param peripheral
 *     The SPI peripheral/channel that encountered the TX DMA error.
 */
void HW_SPI_TX_Error_Handler( SPIPeripheral_T peripheral )
{
    SPIPeripheralState_T* peripheral_state = HW_SPI_Get_State( peripheral );
    if ( peripheral_state == NULL )
    {
        return;
    }

    // Stop further TX DMA activity for this channel. The error path disables
    // the stream and the SPI TX DMA request so the peripheral cannot keep
    // requesting data from a failed transfer.
    LL_DMA_DisableIT_TC( peripheral_state->tx_dma, peripheral_state->tx_dma_stream );
    LL_DMA_DisableIT_TE( peripheral_state->tx_dma, peripheral_state->tx_dma_stream );
    LL_SPI_DisableDMAReq_TX( peripheral_state->spi_peripheral );
    LL_DMA_DisableStream( peripheral_state->tx_dma, peripheral_state->tx_dma_stream );

    while ( LL_DMA_IsEnabledStream( peripheral_state->tx_dma, peripheral_state->tx_dma_stream )
            != 0U )
    {
    }

    // Drop knowledge of the currently active DMA transfer. The pending ring
    // state is left alone so a higher layer can decide whether to flush,
    // rebuild, or retry the transaction.
    peripheral_state->tx_num_bytes_in_transmission = 0U;

    // TODO: Add fault logging, error counters, or escalation here if desired.
}

void HW_SPI_TX_IRQ_Handler( SPIPeripheral_T peripheral )
{
    SPIPeripheralState_T* peripheral_state = HW_SPI_Get_State( peripheral );

    // The completed TX span was already removed from the pending software
    // state when DMA was started. The IRQ only marks the active DMA span as
    // complete, then dispatches to the mode-specific pending/start handlers.
    peripheral_state->tx_num_bytes_in_transmission = 0U;

    if ( peripheral_state->tx_has_pending_function( peripheral_state ) == false )
    {
        return;
    }

    if ( peripheral_state->tx_start_dma_function( peripheral_state ) == false )
    {
        HW_SPI_TX_Error_Handler( peripheral );
    }
}

void HW_SPI_TX_Reset_State( SPIPeripheralState_T* peripheral_state )
{
    if ( peripheral_state == NULL )
    {
        return;
    }

    // Reset the circular TX queue state. Pending bytes are bytes still in the
    // software ring. In-flight bytes are bytes already handed to DMA.
    peripheral_state->tx_write_position            = 0U;
    peripheral_state->tx_read_position             = 0U;
    peripheral_state->tx_num_bytes_pending         = 0U;
    peripheral_state->tx_num_bytes_in_transmission = 0U;

    // Reset packet descriptor queue state. Descriptor contents are cleared only
    // for debug/readability; queue validity is controlled by the explicit
    // packet read/write/count fields rather than by descriptor contents.
    peripheral_state->tx_packet_write_position = 0U;
    peripheral_state->tx_packet_read_position  = 0U;
    peripheral_state->tx_num_packets_pending   = 0U;
    memset( peripheral_state->tx_packet_descriptors, 0,
            sizeof( peripheral_state->tx_packet_descriptors ) );
}

uint32_t HW_SPI_TX_Get_Used_Space( const SPIPeripheralState_T* peripheral_state )
{
    // Both software-pending bytes and DMA-owned bytes occupy storage in
    // tx_buffer, so both must be included when checking whether new data fits.
    return peripheral_state->tx_num_bytes_pending + peripheral_state->tx_num_bytes_in_transmission;
}

static inline uint32_t HW_SPI_TX_Get_Free_Space( const SPIPeripheralState_T* peripheral_state )
{
    // The caller is responsible for not overflowing the TX ring. This helper is
    // intentionally small because it is used on the load fast path.
    return TX_BUFFER_SIZE_BYTES - HW_SPI_TX_Get_Used_Space( peripheral_state );
}

static inline uint32_t
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

/**
 * @brief Start a master-mode TX DMA transfer for exactly one queued packet.
 *
 * In master mode, DMA is deliberately armed packet-by-packet instead of sending
 * all available contiguous TX bytes. This preserves transaction boundaries for
 * a future software chip-select layer without inserting delimiter bytes into
 * the transmitted SPI stream.
 */
static inline bool HW_SPI_TX_Master_Has_Pending( const SPIPeripheralState_T* peripheral_state )
{
    return peripheral_state->tx_num_packets_pending > 0U;
}

static inline bool HW_SPI_TX_Slave_Has_Pending( const SPIPeripheralState_T* peripheral_state )
{
    return peripheral_state->tx_num_bytes_pending > 0U;
}

/**
 * @brief Program the TX DMA stream for one already-selected linear memory span.
 *
 * The caller owns mode-specific queue bookkeeping. This helper only performs
 * the common low-level DMA/SPI programming used by both master packet TX and
 * slave stream TX.
 */
static inline bool HW_SPI_TX_Program_DMA( SPIPeripheralState_T* peripheral_state, uint8_t* tx_ptr,
                                          uint32_t size_bytes )
{
    uint32_t dma_elements = 0U;

    if ( HW_SPI_Is_Frame_Aligned_Size( peripheral_state, size_bytes ) == false )
    {
        return false;
    }

    dma_elements = HW_SPI_Bytes_To_DMA_Elements( peripheral_state, size_bytes );

    LL_DMA_DisableStream( peripheral_state->tx_dma, peripheral_state->tx_dma_stream );
    while ( LL_DMA_IsEnabledStream( peripheral_state->tx_dma, peripheral_state->tx_dma_stream )
            != 0U )
    {
    }

    LL_DMA_SetMemoryAddress( peripheral_state->tx_dma, peripheral_state->tx_dma_stream,
                             ( uintptr_t )tx_ptr );

    LL_DMA_SetPeriphAddress( peripheral_state->tx_dma, peripheral_state->tx_dma_stream,
                             LL_SPI_DMA_GetRegAddr( peripheral_state->spi_peripheral ) );

    LL_DMA_SetDataLength( peripheral_state->tx_dma, peripheral_state->tx_dma_stream, dma_elements );

    LL_DMA_EnableIT_TC( peripheral_state->tx_dma, peripheral_state->tx_dma_stream );
    LL_DMA_EnableIT_TE( peripheral_state->tx_dma, peripheral_state->tx_dma_stream );
    LL_DMA_EnableStream( peripheral_state->tx_dma, peripheral_state->tx_dma_stream );
    LL_SPI_EnableDMAReq_TX( peripheral_state->spi_peripheral );

    return true;
}

/**
 * @brief Start a master-mode TX DMA transfer for exactly one queued packet.
 *
 * Master TX progresses from the packet descriptor queue. Each DMA transfer is
 * therefore one logical packet, preserving transaction boundaries for the later
 * software chip-select layer.
 */
static bool HW_SPI_TX_Start_Master_Packet_DMA( SPIPeripheralState_T* peripheral_state )
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

    // Move exactly this packet out of the pending software state and into the
    // in-flight DMA state. The descriptor is consumed before DMA is armed so the
    // IRQ can immediately start the next queued packet if required.
    peripheral_state->tx_packet_read_position =
        ( peripheral_state->tx_packet_read_position + 1U ) % TX_PACKET_QUEUE_DEPTH;
    peripheral_state->tx_num_packets_pending--;

    peripheral_state->tx_num_bytes_pending =
        peripheral_state->tx_num_bytes_pending - packet_size_bytes;
    peripheral_state->tx_num_bytes_in_transmission = packet_size_bytes;

    peripheral_state->tx_read_position =
        ( packet->start_index + packet_size_bytes ) % TX_BUFFER_SIZE_BYTES;

    // Descriptor clearing is for debug/readability only. Descriptor ownership is
    // controlled by tx_packet_read_position and tx_num_packets_pending.
    packet->start_index = 0U;
    packet->size_bytes  = 0U;

    return HW_SPI_TX_Program_DMA( peripheral_state, tx_ptr, packet_size_bytes );
}

/**
 * @brief Start a slave-mode TX DMA transfer for the next contiguous stream span.
 *
 * This is the original byte-stream behaviour separated from master packet TX.
 */
static bool HW_SPI_TX_Start_Slave_Stream_DMA( SPIPeripheralState_T* peripheral_state )
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
        ( peripheral_state->tx_read_position + bytes_to_send ) % TX_BUFFER_SIZE_BYTES;
    peripheral_state->tx_num_bytes_pending = peripheral_state->tx_num_bytes_pending - bytes_to_send;
    peripheral_state->tx_num_bytes_in_transmission = bytes_to_send;

    return HW_SPI_TX_Program_DMA( peripheral_state, tx_ptr, bytes_to_send );
}

/**
 * @brief Load one contiguous master-mode TX packet into the software TX buffer.
 *
 * Each public load call is treated as one logical master packet. Packets are not
 * allowed to wrap inside tx_buffer so one packet can later be transmitted by one
 * DMA transfer.
 */
static bool HW_SPI_TX_Load_Master_Packet( SPIPeripheralState_T* peripheral_state,
                                          const uint8_t* data, uint32_t size )
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
        ( peripheral_state->tx_packet_write_position + 1U ) % TX_PACKET_QUEUE_DEPTH;
    peripheral_state->tx_num_packets_pending++;

    peripheral_state->tx_write_position =
        ( peripheral_state->tx_write_position + size ) % TX_BUFFER_SIZE_BYTES;
    peripheral_state->tx_num_bytes_pending = peripheral_state->tx_num_bytes_pending + size;

    return true;
}

/**
 * @brief Load slave-mode byte-stream TX data into the software TX buffer.
 *
 * This keeps the original stream behaviour: the source data may wrap inside the
 * TX ring and later be sent as one or more contiguous DMA spans.
 */
static bool HW_SPI_TX_Load_Slave_Stream( SPIPeripheralState_T* peripheral_state,
                                         const uint8_t* data, uint32_t size )
{
    uint32_t first_copy_size  = 0U;
    uint32_t second_copy_size = 0U;

    if ( HW_SPI_Is_Frame_Aligned_Size( peripheral_state, size ) == false )
    {
        return false;
    }

    if ( size > HW_SPI_TX_Get_Free_Space( peripheral_state ) )
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
        ( peripheral_state->tx_write_position + size ) % TX_BUFFER_SIZE_BYTES;
    peripheral_state->tx_num_bytes_pending = peripheral_state->tx_num_bytes_pending + size;

    return true;
}

/**
 * @brief Configure TX operation function pointers for the selected SPI mode.
 *
 * This is called during channel configuration so TX hot paths can dispatch
 * directly through function pointers rather than branching on master/slave mode.
 */
void HW_SPI_TX_Configure_Operations( SPIPeripheralState_T* peripheral_state )
{
    if ( peripheral_state == NULL )
    {
        return;
    }

    switch ( peripheral_state->config.spi_mode )
    {
        case SPI_MASTER_MODE:
            peripheral_state->tx_load_function        = HW_SPI_TX_Load_Master_Packet;
            peripheral_state->tx_start_dma_function   = HW_SPI_TX_Start_Master_Packet_DMA;
            peripheral_state->tx_has_pending_function = HW_SPI_TX_Master_Has_Pending;
            break;

        case SPI_SLAVE_MODE:
            peripheral_state->tx_load_function        = HW_SPI_TX_Load_Slave_Stream;
            peripheral_state->tx_start_dma_function   = HW_SPI_TX_Start_Slave_Stream_DMA;
            peripheral_state->tx_has_pending_function = HW_SPI_TX_Slave_Has_Pending;
            break;

        default:
            peripheral_state->tx_load_function        = NULL;
            peripheral_state->tx_start_dma_function   = NULL;
            peripheral_state->tx_has_pending_function = NULL;
            break;
    }

    if ( peripheral_state == channel_0_state )
    {
        peripheral_state->tx_clear_dma_flags_function = HW_SPI_TX_Clear_Channel_0_DMA_Flags;
    }
    else if ( peripheral_state == channel_1_state )
    {
        peripheral_state->tx_clear_dma_flags_function = HW_SPI_TX_Clear_Channel_1_DMA_Flags;
    }
    else if ( peripheral_state == dac_state )
    {
        peripheral_state->tx_clear_dma_flags_function = HW_SPI_TX_Clear_DAC_DMA_Flags;
    }
}

/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */

void SPI_CHANNEL_0_TX_DMA_IRQ( void )
{
    // Handle transfer error first. If TE and TC are both latched, error
    // handling wins and normal completion processing is skipped.
    if ( SPI_CHANNEL_0_TX_DMA_IS_ACTIVE_TE( SPI_CHANNEL_0_TX_DMA ) != 0U )
    {
        SPI_CHANNEL_0_TX_DMA_CLEAR_TE( SPI_CHANNEL_0_TX_DMA );
        HW_SPI_TX_Error_Handler( SPI_CHANNEL_0 );
        return;
    }

    if ( SPI_CHANNEL_0_TX_DMA_IS_ACTIVE_TC( SPI_CHANNEL_0_TX_DMA ) != 0U )
    {
        SPI_CHANNEL_0_TX_DMA_CLEAR_TC( SPI_CHANNEL_0_TX_DMA );
        HW_SPI_TX_IRQ_Handler( SPI_CHANNEL_0 );
        return;
    }
}

void SPI_CHANNEL_1_TX_DMA_IRQ( void )
{
    // Handle transfer error first. If TE and TC are both latched, error
    // handling wins and normal completion processing is skipped.
    if ( SPI_CHANNEL_1_TX_DMA_IS_ACTIVE_TE( SPI_CHANNEL_1_TX_DMA ) != 0U )
    {
        SPI_CHANNEL_1_TX_DMA_CLEAR_TE( SPI_CHANNEL_1_TX_DMA );
        HW_SPI_TX_Error_Handler( SPI_CHANNEL_1 );
        return;
    }

    if ( SPI_CHANNEL_1_TX_DMA_IS_ACTIVE_TC( SPI_CHANNEL_1_TX_DMA ) != 0U )
    {
        SPI_CHANNEL_1_TX_DMA_CLEAR_TC( SPI_CHANNEL_1_TX_DMA );
        HW_SPI_TX_IRQ_Handler( SPI_CHANNEL_1 );
        return;
    }
}

// Note: Not implemented yet as Channel 1 and DAC are on the same port right now.
// void SPI_DAC_TX_DMA_IRQ( void )
// {
//     if ( LL_DMA_IsActiveFlag_TE1( SPI_DAC_TX_DMA ) != 0U )
//     {
//         LL_DMA_ClearFlag_TE1( SPI_DAC_TX_DMA );
//         HW_SPI_TX_Error_Handler( SPI_DAC );
//         return;
//     }
//
//     if ( LL_DMA_IsActiveFlag_TC1( SPI_DAC_TX_DMA ) != 0U )
//     {
//         LL_DMA_ClearFlag_TC1( SPI_DAC_TX_DMA );
//         HW_SPI_TX_IRQ_Handler( SPI_DAC );
//         return;
//     }
// }

bool HW_SPI_Load_Tx_Buffer( SPIPeripheral_T peripheral, const uint8_t* data, uint32_t size )
{
    SPIPeripheralState_T* peripheral_state = HW_SPI_Get_State( peripheral );
    bool                  accepted         = false;

    // Prevent the TX DMA IRQ from modifying pending/in-flight state while the
    // selected TX load implementation calculates free space and updates the
    // queue state. This disables only the channel's TX DMA IRQ, not global
    // interrupts.
    NVIC_DisableIRQ( peripheral_state->tx_dma_irqn );
    accepted = peripheral_state->tx_load_function( peripheral_state, data, size );
    NVIC_EnableIRQ( peripheral_state->tx_dma_irqn );

    return accepted;
}

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
void HW_SPI_Tx_Trigger( SPIPeripheral_T peripheral )
{
    SPIPeripheralState_T* peripheral_state = HW_SPI_Get_State( peripheral );

    // Protect against a race with the TX DMA IRQ handler. We only disable the
    // specific DMA IRQ for this channel, not global interrupts.
    NVIC_DisableIRQ( peripheral_state->tx_dma_irqn );

    // If DMA is already active or the selected TX mode has no queued work,
    // leave the queue unchanged. The mode-specific pending check is dispatched
    // through the configured function pointer.
    if ( peripheral_state->tx_num_bytes_in_transmission > 0U
         || peripheral_state->tx_has_pending_function( peripheral_state ) == false )
    {
        NVIC_EnableIRQ( peripheral_state->tx_dma_irqn );
        return;
    }

    peripheral_state->tx_clear_dma_flags_function( peripheral_state->tx_dma );

    if ( peripheral_state->tx_start_dma_function( peripheral_state ) == false )
    {
        HW_SPI_TX_Error_Handler( peripheral );
    }

    NVIC_EnableIRQ( peripheral_state->tx_dma_irqn );
}

/**
 * @brief Check whether the TX buffer is empty for a SPI channel.
 *
 * Reports whether the selected channel has no TX data waiting in the software
 * ring and no TX data currently owned by DMA.
 *
 * In the TX ring-buffer model, data can exist in two places:
 * - pending bytes still waiting in the software TX ring, and
 * - in-flight bytes that have already been handed to DMA but have not completed.
 *
 * This function treats the TX path as empty only when both of those counts are
 * zero. It is intended for higher-level code that needs to know whether all
 * previously loaded TX data has been fully transmitted.
 *
 * This function assumes the caller provides a valid SPI peripheral. Invalid
 * peripheral validation is intentionally not performed here because this is a
 * lightweight low-level helper intended for frequent use.
 *
 * @param peripheral
 *     The SPI peripheral/channel to inspect.
 *
 * @return
 *     true if there are no pending or in-flight TX bytes.
 *     false if any TX data is still pending in the ring or currently being
 *     transmitted by DMA.
 */
bool HW_SPI_Tx_Buffer_Empty( SPIPeripheral_T peripheral )
{
    SPIPeripheralState_T* state = HW_SPI_Get_State( peripheral );

    // Empty only means no pending software-ring bytes and no in-flight DMA bytes.
    return HW_SPI_TX_Get_Used_Space( state ) == 0U;
}
