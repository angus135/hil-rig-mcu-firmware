/******************************************************************************
 *  File:       hw_spi.c
 *  Author:     Angus Corr
 *  Created:    10-Apr-2026
 *
 *  Description:
 *      Low-level SPI driver implementation for the HIL-RIG firmware.
 *
 *      This module provides a generic DMA-backed SPI transport layer for the
 *      supported SPI peripherals. It is intentionally kept mode-agnostic at the
 *      public data-path level and focuses only on configuring SPI hardware,
 *      managing continuous RX capture into an internal circular buffer, and
 *      managing queued TX transfers from an internal circular software buffer.
 *
 *      RX is exposed through a peek/consume model over a DMA-backed circular
 *      byte stream. TX is exposed through a load/trigger model where data is
 *      appended to an internal circular software queue and transmitted using
 *      DMA when triggered. TX DMA is armed for one contiguous span of the
 *      circular queue at a time. If queued data wraps around the end of the TX
 *      buffer, the TX DMA completion IRQ re-arms DMA for the next contiguous
 *      span.
 *
 *      The module does not implement higher-level protocol framing, transaction
 *      scheduling, chip-select sequencing policy beyond basic hardware mode
 *      configuration, logging interpretation, or master/slave application
 *      semantics. Those responsibilities are intentionally left to higher-level
 *      software.
 *
 *  Notes:
 *      - Public RX/TX APIs are byte-oriented even when the SPI peripheral is
 *        configured for 16-bit data size.
 *      - In 16-bit mode, queued TX sizes and RX consume sizes must remain
 *        aligned to whole SPI frames (multiples of 2 bytes).
 *      - Software queue positions and RX unread tracking are maintained in
 *        bytes. DMA transfer lengths are configured internally in DMA elements
 *        according to the selected SPI data size.
 *      - RX uses a continuous DMA-backed circular buffer. The caller must
 *        consume data often enough to avoid overwrite of unread data.
 *      - TX uses a circular software queue. Read/write positions wrap modulo
 *        TX_BUFFER_SIZE_BYTES rather than being reset after every transfer.
 *      - TX DMA must be configured in normal mode. RX DMA is circular; TX DMA
 *        is intentionally one-shot so the master only clocks explicitly queued
 *        data.
 *      - Each TX DMA transfer can only cover one contiguous memory span.
 *        Wrapped TX data is sent by re-arming DMA from the TX completion IRQ.
 *      - This module assumes DMA stream/hardware mappings defined in this file
 *        are correct for the target hardware configuration.
 *      - Stream-specific DMA terminal flag handling is currently hard-coded to
 *        the selected streams and must be updated if the DMA stream mappings
 *        change.
 *      - DMA memory/peripheral widths must match the configured SPI data size.
 *      - This layer performs no byte swapping, message framing, or semantic
 *        interpretation of queued or received data.
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#ifdef TEST_BUILD
#include "tests/hw_spi_mocks.h"
#else
#include "spi.h"
#include "stm32f446xx.h"
#include "stm32f4xx_ll_dma.h"
#include "stm32f4xx_ll_spi.h"
#include "stm32f4xx_it.h"
#endif
#include "hw_spi.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */
// TODO: Change this based on true hardware
#define SPI_CHANNEL_0_HANDLE hspi1
#define SPI_CHANNEL_1_HANDLE hspi4
#define SPI_DAC_HANDLE hspi4

#define SPI_CHANNEL_0_INSTANCE SPI1
#define SPI_CHANNEL_1_INSTANCE SPI4
#define SPI_DAC_INSTANCE SPI4

// DMA Definitions
#define SPI_CHANNEL_0_RX_DMA DMA2
#define SPI_CHANNEL_0_RX_DMA_STREAM LL_DMA_STREAM_0
#define SPI_CHANNEL_0_RX_DMA_IRQ DMA2_Stream0_IRQHandler
#define SPI_CHANNEL_0_RX_DMA_IRQN DMA2_Stream0_IRQn
#define SPI_CHANNEL_0_TX_DMA DMA2
#define SPI_CHANNEL_0_TX_DMA_STREAM LL_DMA_STREAM_5
#define SPI_CHANNEL_0_TX_DMA_IRQ DMA2_Stream5_IRQHandler
#define SPI_CHANNEL_0_TX_DMA_IRQN DMA2_Stream5_IRQn
#define SPI_CHANNEL_0_TX_DMA_IS_ACTIVE_TC LL_DMA_IsActiveFlag_TC5
#define SPI_CHANNEL_0_TX_DMA_IS_ACTIVE_TE LL_DMA_IsActiveFlag_TE5
#define SPI_CHANNEL_0_TX_DMA_CLEAR_TC LL_DMA_ClearFlag_TC5
#define SPI_CHANNEL_0_TX_DMA_CLEAR_TE LL_DMA_ClearFlag_TE5
#define SPI_CHANNEL_1_RX_DMA DMA2
#define SPI_CHANNEL_1_RX_DMA_STREAM LL_DMA_STREAM_3
#define SPI_CHANNEL_1_RX_DMA_IRQ DMA2_Stream3_IRQHandler
#define SPI_CHANNEL_1_RX_DMA_IRQN DMA2_Stream3_IRQn
#define SPI_CHANNEL_1_TX_DMA DMA2
#define SPI_CHANNEL_1_TX_DMA_STREAM LL_DMA_STREAM_1
#define SPI_CHANNEL_1_TX_DMA_IRQ DMA2_Stream1_IRQHandler
#define SPI_CHANNEL_1_TX_DMA_IRQN DMA2_Stream1_IRQn
#define SPI_CHANNEL_1_TX_DMA_IS_ACTIVE_TC LL_DMA_IsActiveFlag_TC1
#define SPI_CHANNEL_1_TX_DMA_IS_ACTIVE_TE LL_DMA_IsActiveFlag_TE1
#define SPI_CHANNEL_1_TX_DMA_CLEAR_TC LL_DMA_ClearFlag_TC1
#define SPI_CHANNEL_1_TX_DMA_CLEAR_TE LL_DMA_ClearFlag_TE1
#define SPI_DAC_TX_DMA DMA2
#define SPI_DAC_TX_DMA_STREAM LL_DMA_STREAM_1
#define SPI_DAC_TX_DMA_IRQ DMA2_Stream1_IRQHandler
#define SPI_DAC_TX_DMA_IRQN DMA2_Stream1_IRQn
#define SPI_DAC_TX_DMA_IS_ACTIVE_TC LL_DMA_IsActiveFlag_TC1
#define SPI_DAC_TX_DMA_IS_ACTIVE_TE LL_DMA_IsActiveFlag_TE1
#define SPI_DAC_TX_DMA_CLEAR_TC LL_DMA_ClearFlag_TC1
#define SPI_DAC_TX_DMA_CLEAR_TE LL_DMA_ClearFlag_TE1

#define RX_BUFFER_SIZE_BYTES 1024
#define TX_BUFFER_SIZE_BYTES 1024

#if RX_BUFFER_SIZE_BYTES % 2 != 0
#error "RX Buffer Must be a size that is a multiple of 2"
#endif

#if TX_BUFFER_SIZE_BYTES % 2 != 0
#error "TX Buffer Must be a size that is a multiple of 2"
#endif

/**-----------------------------------------------------------------------------
 *  Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */
typedef struct SPIPeripheralState_T
{
    HWSPIConfig_T config;

    // RX is owned by DMA. rx_position is the software consume index used by
    // HW_SPI_Rx_Peek() and HW_SPI_Rx_Consume(). The current DMA write index is
    // derived from the DMA stream NDTR value instead of being stored here.
    uint8_t  rx_buffer[RX_BUFFER_SIZE_BYTES] __attribute__( ( aligned( 2 ) ) );
    uint32_t rx_position;

    // TX is a software ring buffer. New bytes are copied at tx_write_position.
    // DMA reads from tx_read_position. Both indices wrap modulo
    // TX_BUFFER_SIZE_BYTES.
    uint8_t  tx_buffer[TX_BUFFER_SIZE_BYTES] __attribute__( ( aligned( 2 ) ) );
    uint32_t tx_write_position;
    uint32_t tx_read_position;

    // Bytes waiting in the software TX ring that have not yet been handed to
    // DMA. This does not include the active DMA transfer.
    uint32_t tx_num_bytes_pending;

    // Bytes currently owned by the active TX DMA transfer. This is stored in
    // bytes; for 16-bit SPI it is converted to DMA halfword elements only when
    // programming the DMA stream length.
    uint32_t tx_num_bytes_in_transmission;

    // Hardware information
    DMA_TypeDef* rx_dma;
    uint32_t     rx_dma_stream;
    DMA_TypeDef* tx_dma;
    uint32_t     tx_dma_stream;
    SPI_TypeDef* spi_peripheral;
    IRQn_Type    tx_dma_irqn;
} SPIPeripheralState_T;

/**-----------------------------------------------------------------------------
 *  Public (global) and Extern Variables
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Private (static) Variables
 *------------------------------------------------------------------------------
 */
SPIPeripheralState_T  channel_0_state_struct;
SPIPeripheralState_T  channel_1_state_struct;
SPIPeripheralState_T  dac_state_struct;
SPIPeripheralState_T* channel_0_state = &channel_0_state_struct;
SPIPeripheralState_T* channel_1_state = &channel_1_state_struct;
SPIPeripheralState_T* dac_state       = &dac_state_struct;

/**-----------------------------------------------------------------------------
 *  Private (static) Function Prototypes
 *------------------------------------------------------------------------------
 */

static inline SPIPeripheralState_T* HW_SPI_Get_State( SPIPeripheral_T peripheral );
static void                         HW_SPI_TX_Error_Handler( SPIPeripheral_T peripheral );
static inline void                  HW_SPI_TX_IRQ_Handler( SPIPeripheral_T peripheral );
static inline uint32_t HW_SPI_Get_Frame_Size_Bytes( const SPIPeripheralState_T* peripheral_state );
static inline uint16_t HW_SPI_Bytes_To_DMA_Elements( const SPIPeripheralState_T* peripheral_state,
                                                     uint32_t                    size_bytes );
static inline uint32_t HW_SPI_DMA_Elements_To_Bytes( const SPIPeripheralState_T* peripheral_state,
                                                     uint32_t                    num_elements );
static inline bool     HW_SPI_Is_Frame_Aligned_Size( const SPIPeripheralState_T* peripheral_state,
                                                     uint32_t                    size_bytes );
static void            HW_SPI_TX_Reset_State( SPIPeripheralState_T* peripheral_state );
static inline uint32_t HW_SPI_TX_Get_Used_Space( const SPIPeripheralState_T* peripheral_state );
static inline uint32_t HW_SPI_TX_Get_Free_Space( const SPIPeripheralState_T* peripheral_state );
static inline uint32_t
            HW_SPI_TX_Get_Contiguous_Read_Bytes( const SPIPeripheralState_T* peripheral_state );
static void HW_SPI_Configure_DMA_Data_Widths( SPIPeripheralState_T* peripheral_state );
static bool HW_SPI_RX_Start_Passive_DMA( SPIPeripheralState_T* peripheral_state );
static bool
HW_SPI_TX_Start_DMA_From_Current_Read_Position( SPIPeripheralState_T* peripheral_state );

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

static inline SPIPeripheralState_T* HW_SPI_Get_State( SPIPeripheral_T peripheral )
{
    switch ( peripheral )
    {
        case SPI_CHANNEL_0:
            return channel_0_state;

        case SPI_CHANNEL_1:
            return channel_1_state;

        case SPI_DAC:
            return dac_state;

        default:
            return NULL;
    }
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
static void HW_SPI_TX_Error_Handler( SPIPeripheral_T peripheral )
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

static inline void HW_SPI_TX_IRQ_Handler( SPIPeripheral_T peripheral )
{
    SPIPeripheralState_T* peripheral_state = HW_SPI_Get_State( peripheral );

    // The completed TX span was already removed from the pending software
    // ring when the DMA transfer was started. The IRQ does not advance the
    // ring read pointer; it only marks the active DMA span as complete.
    peripheral_state->tx_num_bytes_in_transmission = 0U;

    // If no more bytes were queued while DMA was active, TX is now idle. The
    // read and write indices are intentionally left at their current ring
    // positions rather than being reset to zero.
    if ( peripheral_state->tx_num_bytes_pending == 0U )
    {
        return;
    }

    // More data is pending, so start the next contiguous TX ring span. If the
    // queue wrapped, this second transfer will normally begin at index 0.
    if ( HW_SPI_TX_Start_DMA_From_Current_Read_Position( peripheral_state ) == false )
    {
        HW_SPI_TX_Error_Handler( peripheral );
    }
}

static inline uint32_t HW_SPI_Get_Frame_Size_Bytes( const SPIPeripheralState_T* peripheral_state )
{
    if ( peripheral_state->config.data_size == SPI_SIZE_16_BIT )
    {
        return 2U;
    }

    return 1U;
}

static inline uint16_t HW_SPI_Bytes_To_DMA_Elements( const SPIPeripheralState_T* peripheral_state,
                                                     uint32_t                    size_bytes )
{
    return size_bytes / HW_SPI_Get_Frame_Size_Bytes( peripheral_state );
}

static inline uint32_t HW_SPI_DMA_Elements_To_Bytes( const SPIPeripheralState_T* peripheral_state,
                                                     uint32_t                    num_elements )
{
    return num_elements * HW_SPI_Get_Frame_Size_Bytes( peripheral_state );
}

static inline bool HW_SPI_Is_Frame_Aligned_Size( const SPIPeripheralState_T* peripheral_state,
                                                 uint32_t                    size_bytes )
{
    return ( size_bytes % HW_SPI_Get_Frame_Size_Bytes( peripheral_state ) ) == 0U;
}

static void HW_SPI_TX_Reset_State( SPIPeripheralState_T* peripheral_state )
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
}

static inline uint32_t HW_SPI_TX_Get_Used_Space( const SPIPeripheralState_T* peripheral_state )
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

/**
 * @brief Configure DMA memory/peripheral data widths to match the SPI frame size.
 *
 * This must match the selected SPI data size. For 8-bit SPI, DMA transfers bytes.
 * For 16-bit SPI, DMA transfers halfwords. This is here to guarantee configuration of DMA for size
 * and alignment that HAL may not do.
 */
static void HW_SPI_Configure_DMA_Data_Widths( SPIPeripheralState_T* peripheral_state )
{
    uint32_t memory_width     = 0;
    uint32_t peripheral_width = 0;

    // Public APIs use byte counts, but the DMA stream width must match the SPI
    // frame width. In 16-bit mode, the DMA memory and peripheral accesses are
    // halfwords rather than bytes.
    if ( peripheral_state->config.data_size == SPI_SIZE_16_BIT )
    {
        memory_width     = LL_DMA_MDATAALIGN_HALFWORD;
        peripheral_width = LL_DMA_PDATAALIGN_HALFWORD;
    }
    else
    {
        memory_width     = LL_DMA_MDATAALIGN_BYTE;
        peripheral_width = LL_DMA_PDATAALIGN_BYTE;
    }

    if ( peripheral_state->rx_dma
         != NULL )  // Checks can be done here since configuration is not in critical path
    {
        LL_DMA_SetMemorySize( peripheral_state->rx_dma, peripheral_state->rx_dma_stream,
                              memory_width );
        LL_DMA_SetPeriphSize( peripheral_state->rx_dma, peripheral_state->rx_dma_stream,
                              peripheral_width );
    }

    if ( peripheral_state->tx_dma != NULL )
    {
        LL_DMA_SetMemorySize( peripheral_state->tx_dma, peripheral_state->tx_dma_stream,
                              memory_width );
        LL_DMA_SetPeriphSize( peripheral_state->tx_dma, peripheral_state->tx_dma_stream,
                              peripheral_width );
    }
}

/**
 * @brief Arm RX DMA without causing master-mode dummy clock generation.
 *
 * PASSIVE RX DMA CHANGE:
 * HAL_SPI_Receive_DMA() is not suitable for this driver model when the SPI
 * peripheral is configured as a 2-line master. STM32 HAL implements that case
 * as a transmit/receive operation so the master can generate SCK, which can
 * clock a full RX buffer worth of dummy traffic.
 *
 * This helper arms RX DMA directly. It lets RX DMA drain SPI->DR whenever
 * frames arrive, but it does not generate clocks by itself. Clocks are only
 * generated by explicit TX activity.
 */
static bool HW_SPI_RX_Start_Passive_DMA( SPIPeripheralState_T* peripheral_state )
{
    uint16_t rx_length_elements = 0U;

    if ( peripheral_state == NULL )
    {
        return false;
    }

    if ( peripheral_state->rx_dma == NULL )
    {
        return false;
    }

    // The software buffer size is in bytes, but the DMA NDTR length is in DMA
    // elements. In 8-bit mode bytes == elements; in 16-bit mode two bytes are
    // one DMA element.
    rx_length_elements = HW_SPI_Bytes_To_DMA_Elements( peripheral_state, RX_BUFFER_SIZE_BYTES );

    // Reset the software consume index. The DMA write index is calculated later
    // from NDTR in HW_SPI_Rx_Peek().
    peripheral_state->rx_position = 0U;

    // Reprogram the RX stream from a known disabled state. The stream is
    // circular, so once enabled it continuously drains SPI->DR into rx_buffer.
    LL_DMA_DisableStream( peripheral_state->rx_dma, peripheral_state->rx_dma_stream );
    while ( LL_DMA_IsEnabledStream( peripheral_state->rx_dma, peripheral_state->rx_dma_stream )
            != 0U )
    {
    }

    // Memory address is the start of the circular RX buffer. The stream itself
    // wraps in hardware when it reaches the programmed length.
    LL_DMA_SetMemoryAddress( peripheral_state->rx_dma, peripheral_state->rx_dma_stream,
                             ( uintptr_t )peripheral_state->rx_buffer );

    // Peripheral address is the SPI data register. RX DMA copies from SPI->DR
    // into rx_buffer whenever the SPI peripheral receives a frame.
    LL_DMA_SetPeriphAddress( peripheral_state->rx_dma, peripheral_state->rx_dma_stream,
                             LL_SPI_DMA_GetRegAddr( peripheral_state->spi_peripheral ) );

    // Program the full circular buffer length. LL_DMA_GetDataLength() later
    // exposes the remaining element count, which HW_SPI_Rx_Peek() converts back
    // into a software write index.
    LL_DMA_SetDataLength( peripheral_state->rx_dma, peripheral_state->rx_dma_stream,
                          rx_length_elements );

    // Enable the DMA stream before enabling the SPI RX DMA request so that an
    // incoming byte cannot set RXNE without somewhere to be drained.
    LL_DMA_EnableStream( peripheral_state->rx_dma, peripheral_state->rx_dma_stream );
    LL_SPI_EnableDMAReq_RX( peripheral_state->spi_peripheral );

    // Enabling SPI here arms the peripheral. For a master, this does not clock
    // anything by itself; clocks are generated only by explicit TX activity.
    LL_SPI_Enable( peripheral_state->spi_peripheral );

    return true;
}

/**
 * @brief Start a TX DMA transfer from the current software read position.
 *
 * Software state remains byte-oriented. DMA length is configured in DMA elements,
 * which are 1 byte for 8-bit SPI or 2 bytes for 16-bit SPI.
 */
static bool HW_SPI_TX_Start_DMA_From_Current_Read_Position( SPIPeripheralState_T* peripheral_state )
{
    uint32_t bytes_to_send = 0U;
    uint32_t dma_elements  = 0U;
    uint8_t* tx_ptr        = NULL;

    if ( peripheral_state->tx_num_bytes_in_transmission > 0U )
    {
        return false;
    }

    // Select only the next contiguous region of the ring. If pending data wraps
    // around the end of tx_buffer, only the tail region is sent by this DMA
    // transaction and the IRQ will schedule the head region afterwards.
    bytes_to_send = HW_SPI_TX_Get_Contiguous_Read_Bytes( peripheral_state );
    if ( bytes_to_send == 0U )
    {
        return false;
    }

    if ( HW_SPI_Is_Frame_Aligned_Size( peripheral_state, bytes_to_send ) == false )
    {
        return false;
    }

    // DMA length is configured in elements, not bytes. For 16-bit SPI this is
    // half the byte count.
    dma_elements = HW_SPI_Bytes_To_DMA_Elements( peripheral_state, bytes_to_send );

    // Reconfigure TX DMA from a known disabled state. TX DMA must be normal
    // mode, not circular mode, otherwise the master will repeatedly clock the
    // same memory span.
    LL_DMA_DisableStream( peripheral_state->tx_dma, peripheral_state->tx_dma_stream );
    while ( LL_DMA_IsEnabledStream( peripheral_state->tx_dma, peripheral_state->tx_dma_stream )
            != 0U )
    {
    }

    // Capture the DMA source pointer before advancing tx_read_position. From
    // this point onward, the selected span is considered owned by DMA.
    tx_ptr = &( peripheral_state->tx_buffer[peripheral_state->tx_read_position] );

    // Move the selected span out of the pending software ring and into the
    // in-flight DMA state. This keeps pending bytes as only the data not yet
    // handed to DMA.
    peripheral_state->tx_read_position =
        ( peripheral_state->tx_read_position + bytes_to_send ) % TX_BUFFER_SIZE_BYTES;
    peripheral_state->tx_num_bytes_pending = peripheral_state->tx_num_bytes_pending - bytes_to_send;
    peripheral_state->tx_num_bytes_in_transmission = bytes_to_send;

    // DMA source is the selected contiguous TX ring span.
    LL_DMA_SetMemoryAddress( peripheral_state->tx_dma, peripheral_state->tx_dma_stream,
                             ( uintptr_t )tx_ptr );

    // DMA destination is the SPI data register.
    LL_DMA_SetPeriphAddress( peripheral_state->tx_dma, peripheral_state->tx_dma_stream,
                             LL_SPI_DMA_GetRegAddr( peripheral_state->spi_peripheral ) );

    // Start exactly this span. If more bytes are pending after this span, the
    // TX DMA completion IRQ will arm another span.
    LL_DMA_SetDataLength( peripheral_state->tx_dma, peripheral_state->tx_dma_stream, dma_elements );

    // Enable the SPI request and the DMA stream last, after all addresses and
    // lengths have been written.
    LL_SPI_EnableDMAReq_TX( peripheral_state->spi_peripheral );
    LL_DMA_EnableIT_TC( peripheral_state->tx_dma, peripheral_state->tx_dma_stream );
    LL_DMA_EnableIT_TE( peripheral_state->tx_dma, peripheral_state->tx_dma_stream );
    LL_DMA_EnableStream( peripheral_state->tx_dma, peripheral_state->tx_dma_stream );

    return true;
}

/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */
void SPI_CHANNEL_0_RX_DMA_IRQ( void )
{
}

void SPI_CHANNEL_1_RX_DMA_IRQ( void )
{
}

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
bool HW_SPI_Configure_Channel( SPIPeripheral_T peripheral, HWSPIConfig_T configuration )
{
    SPI_HandleTypeDef* hspi = NULL;
    // Select the HAL handle, SPI instance, DMA streams, and state block for the
    // requested logical SPI channel. The rest of the function can then configure
    // the selected HAL handle generically.
    switch ( peripheral )
    {
        case SPI_CHANNEL_0:
            hspi           = &SPI_CHANNEL_0_HANDLE;
            hspi->Instance = SPI_CHANNEL_0_INSTANCE;
            memcpy( &( channel_0_state->config ), &configuration, sizeof( HWSPIConfig_T ) );
            channel_0_state->rx_dma         = SPI_CHANNEL_0_RX_DMA;
            channel_0_state->rx_dma_stream  = SPI_CHANNEL_0_RX_DMA_STREAM;
            channel_0_state->tx_dma         = SPI_CHANNEL_0_TX_DMA;
            channel_0_state->tx_dma_stream  = SPI_CHANNEL_0_TX_DMA_STREAM;
            channel_0_state->spi_peripheral = SPI_CHANNEL_0_INSTANCE;
            channel_0_state->tx_dma_irqn    = SPI_CHANNEL_0_TX_DMA_IRQN;
            break;
        case SPI_CHANNEL_1:
            hspi           = &SPI_CHANNEL_1_HANDLE;
            hspi->Instance = SPI_CHANNEL_1_INSTANCE;
            memcpy( &( channel_1_state->config ), &configuration, sizeof( HWSPIConfig_T ) );
            channel_1_state->rx_dma         = SPI_CHANNEL_1_RX_DMA;
            channel_1_state->rx_dma_stream  = SPI_CHANNEL_1_RX_DMA_STREAM;
            channel_1_state->tx_dma         = SPI_CHANNEL_1_TX_DMA;
            channel_1_state->tx_dma_stream  = SPI_CHANNEL_1_TX_DMA_STREAM;
            channel_1_state->spi_peripheral = SPI_CHANNEL_1_INSTANCE;
            channel_1_state->tx_dma_irqn    = SPI_CHANNEL_1_TX_DMA_IRQN;
            break;
        case SPI_DAC:
            hspi           = &SPI_DAC_HANDLE;
            hspi->Instance = SPI_DAC_INSTANCE;
            memcpy( &( dac_state->config ), &configuration, sizeof( HWSPIConfig_T ) );
            dac_state->tx_dma         = SPI_DAC_TX_DMA;
            dac_state->tx_dma_stream  = SPI_DAC_TX_DMA_STREAM;
            dac_state->spi_peripheral = SPI_DAC_INSTANCE;
            dac_state->tx_dma_irqn    = SPI_DAC_TX_DMA_IRQN;
            break;
        default:
            return false;
    }

    // Mode + chip select logic
    switch ( configuration.spi_mode )
    {
        case SPI_MASTER_MODE:
            hspi->Init.Mode = SPI_MODE_MASTER;
            hspi->Init.NSS  = SPI_NSS_HARD_OUTPUT;
            break;
        case SPI_SLAVE_MODE:
            hspi->Init.Mode = SPI_MODE_SLAVE;
            hspi->Init.NSS  = SPI_NSS_HARD_INPUT;
            break;
        default:
            return false;
    }

    // Direction (always 2 lines (MISO and MOSI))
    hspi->Init.Direction = SPI_DIRECTION_2LINES;
    // Datasize (8 bit or 16 bit)
    switch ( configuration.data_size )
    {
        case SPI_SIZE_8_BIT:
            hspi->Init.DataSize = SPI_DATASIZE_8BIT;
            break;
        case SPI_SIZE_16_BIT:
            hspi->Init.DataSize = SPI_DATASIZE_16BIT;
            break;
        default:
            return false;
    }

    // Clock Polarity (CPOL)
    switch ( configuration.cpol )
    {
        case SPI_CPOL_HIGH:
            hspi->Init.CLKPolarity = SPI_POLARITY_HIGH;
            break;
        case SPI_CPOL_LOW:
            hspi->Init.CLKPolarity = SPI_POLARITY_LOW;
            break;
        default:
            return false;
    }

    // Clock Phase (CPHA)
    switch ( configuration.cpha )
    {
        case SPI_CPHA_1_EDGE:
            hspi->Init.CLKPhase = SPI_PHASE_1EDGE;
            break;
        case SPI_CPHA_2_EDGE:
            hspi->Init.CLKPhase = SPI_PHASE_2EDGE;
            break;
        default:
            return false;
    }

    // Baud rate
    switch ( configuration.baud_rate )
    {
        case SPI_BAUD_45MBIT:
            hspi->Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_2;
            break;
        case SPI_BAUD_22M5BIT:
            hspi->Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_4;
            break;
        case SPI_BAUD_11M25BIT:
            hspi->Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_8;
            break;
        case SPI_BAUD_5M625BIT:
            hspi->Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_16;
            break;
        case SPI_BAUD_2M813BIT:
            hspi->Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_32;
            break;
        case SPI_BAUD_1M406BIT:
            hspi->Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_64;
            break;
        case SPI_BAUD_703KBIT:
            hspi->Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_128;
            break;
        case SPI_BAUD_352KBIT:
            hspi->Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_256;
            break;
        default:
            return false;
    }

    // MSB vs LSB
    switch ( configuration.first_bit )
    {
        case SPI_FIRST_LSB:
            hspi->Init.FirstBit = SPI_FIRSTBIT_LSB;
            break;
        case SPI_FIRST_MSB:
            hspi->Init.FirstBit = SPI_FIRSTBIT_MSB;
            break;
        default:
            return false;
    }

    // Default values sourced from HAL
    hspi->Init.TIMode         = SPI_TIMODE_DISABLE;
    hspi->Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    hspi->Init.CRCPolynomial  = 10;
    if ( HAL_SPI_Init( hspi ) != HAL_OK )
    {
        return false;
    }

    /*
     * Ensure DMA memory/peripheral data widths match the configured SPI frame size.
     * This is essential when switching between 8-bit and 16-bit operation.
     */
    switch ( peripheral )
    {
        case SPI_CHANNEL_0:
            HW_SPI_Configure_DMA_Data_Widths( channel_0_state );
            break;

        case SPI_CHANNEL_1:
            HW_SPI_Configure_DMA_Data_Widths( channel_1_state );
            break;

        case SPI_DAC:
            HW_SPI_Configure_DMA_Data_Widths( dac_state );
            break;

        default:
            return false;
    }

    return true;
}

/**
 * @brief Start runtime operation of a configured SPI channel.
 *
 * Enables the runtime behaviour required by this low-level SPI driver for the
 * selected channel.
 *
 * In the current driver design, starting a channel places its RX path into the
 * driver's continuous receive mode by directly arming the RX DMA stream against
 * the internal RX buffer. This allows higher-level software to later inspect
 * the received byte stream using HW_SPI_Rx_Peek() and HW_SPI_Rx_Consume().
 *
 * This function does not start any TX transfer. Transmit activity is initiated
 * separately using HW_SPI_Load_Tx_Buffer() and HW_SPI_Tx_Trigger().
 *
 * This function does not impose message framing, protocol parsing, chip-select
 * policy, or higher-level scheduling semantics. Those responsibilities belong
 * to the software layer above this driver.
 *
 * The RX DMA path is armed passively. In master mode this function must not
 * generate clocks; clocks are generated only by explicit TX activity.
 *
 * The channel should only be started after successful configuration.
 *
 * @param peripheral
 *     The SPI peripheral/channel to start.
 */
void HW_SPI_Start_Channel( SPIPeripheral_T peripheral )
{
    SPIPeripheralState_T* peripheral_state = NULL;

    // Start only the RX side here. TX is deliberately separate and is started by
    // HW_SPI_Load_Tx_Buffer() plus HW_SPI_Tx_Trigger().
    switch ( peripheral )
    {
        case SPI_CHANNEL_0:
            peripheral_state = channel_0_state;
            break;

        case SPI_CHANNEL_1:
            peripheral_state = channel_1_state;
            break;

        case SPI_DAC:  // SPI will not be receiving anything
        default:
            return;
    }

    // Arm RX DMA directly instead of using HAL_SPI_Receive_DMA(). This keeps
    // master-mode RX passive: no clocks are generated until the caller starts a
    // TX transfer.
    ( void )HW_SPI_RX_Start_Passive_DMA( peripheral_state );
}

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
void HW_SPI_Stop_Channel( SPIPeripheral_T peripheral )
{
    SPI_HandleTypeDef* hspi = NULL;
    switch ( peripheral )
    {
        case SPI_CHANNEL_0:
            hspi = &SPI_CHANNEL_0_HANDLE;
            break;
        case SPI_CHANNEL_1:
            hspi = &SPI_CHANNEL_1_HANDLE;
            break;
        case SPI_DAC:
            hspi = &SPI_DAC_HANDLE;
            return;
            break;
        default:
            return;
    }

    HAL_SPI_DMAStop( hspi );
}

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
HWSPIRxSpans_T HW_SPI_Rx_Peek( SPIPeripheral_T peripheral )
{
    SPIPeripheralState_T* peripheral_state       = NULL;
    uint8_t*              rx_buffer              = NULL;
    uint32_t              read_index             = 0U;
    uint32_t              dma_remaining_elements = 0U;
    uint32_t              dma_remaining_bytes    = 0U;
    uint32_t              dma_write_index        = 0U;
    uint32_t              unread_bytes           = 0U;

    switch ( peripheral )
    {
        case SPI_CHANNEL_0:
            peripheral_state = channel_0_state;
            rx_buffer        = channel_0_state->rx_buffer;
            read_index       = channel_0_state->rx_position;
            dma_remaining_elements =
                LL_DMA_GetDataLength( SPI_CHANNEL_0_RX_DMA, SPI_CHANNEL_0_RX_DMA_STREAM );
            break;

        case SPI_CHANNEL_1:
            peripheral_state = channel_1_state;
            rx_buffer        = channel_1_state->rx_buffer;
            read_index       = channel_1_state->rx_position;
            dma_remaining_elements =
                LL_DMA_GetDataLength( SPI_CHANNEL_1_RX_DMA, SPI_CHANNEL_1_RX_DMA_STREAM );
            break;

        case SPI_DAC:
        default:
            return ( HWSPIRxSpans_T ){ .first_span         = { .data = NULL, .length_bytes = 0U },
                                       .second_span        = { .data = NULL, .length_bytes = 0U },
                                       .total_length_bytes = 0U };
    }

    // DMA NDTR is expressed in DMA elements, not bytes. Convert back to bytes
    // so the software RX stream remains byte-oriented.
    dma_remaining_bytes = HW_SPI_DMA_Elements_To_Bytes( peripheral_state, dma_remaining_elements );

    // In circular RX mode, NDTR counts down from the full buffer length to 0
    // and then reloads. Subtracting the remaining count gives the current DMA
    // write index in byte units.
    dma_write_index = RX_BUFFER_SIZE_BYTES - dma_remaining_bytes;

    if ( dma_write_index == RX_BUFFER_SIZE_BYTES )
    {
        dma_write_index = 0U;
    }

    // The modulo subtraction handles both non-wrapped and wrapped unread RX
    // regions.
    unread_bytes = ( dma_write_index + RX_BUFFER_SIZE_BYTES - read_index ) % RX_BUFFER_SIZE_BYTES;

    if ( unread_bytes == 0U )
    {
        return ( HWSPIRxSpans_T ){ .first_span  = { .data = &rx_buffer[0], .length_bytes = 0U },
                                   .second_span = { .data = &rx_buffer[0], .length_bytes = 0U },
                                   .total_length_bytes = 0U };
    }

    // If the DMA write index is ahead of the software read index, all unread
    // data is one contiguous span.
    if ( dma_write_index > read_index )
    {
        return ( HWSPIRxSpans_T ){
            .first_span         = { .data = &rx_buffer[read_index], .length_bytes = unread_bytes },
            .second_span        = { .data = &rx_buffer[0], .length_bytes = 0U },
            .total_length_bytes = unread_bytes };
    }

    // Otherwise the unread data wraps around the end of rx_buffer. Return two
    // spans so the caller can process both without copying inside the driver.
    uint32_t first_span_length  = RX_BUFFER_SIZE_BYTES - read_index;
    uint32_t second_span_length = unread_bytes - first_span_length;

    return ( HWSPIRxSpans_T ){
        .first_span         = { .data = &rx_buffer[read_index], .length_bytes = first_span_length },
        .second_span        = { .data = &rx_buffer[0], .length_bytes = second_span_length },
        .total_length_bytes = unread_bytes };
}

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
void HW_SPI_Rx_Consume( SPIPeripheral_T peripheral, uint32_t bytes_to_consume )
{
    SPIPeripheralState_T* peripheral_state = HW_SPI_Get_State( peripheral );

    // Advance only the software consume index. The DMA write index is hardware
    // controlled and is derived from NDTR when peeking.
    peripheral_state->rx_position =
        ( peripheral_state->rx_position + bytes_to_consume ) % RX_BUFFER_SIZE_BYTES;
}

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
 * The TX buffer used by this driver is a circular software queue. Buffered
 * data remains pending until a contiguous span is handed to DMA. Once a span is
 * handed to DMA it is tracked as in-flight rather than pending.
 *
 * This function does not define message framing or protocol semantics. It only
 * stores raw bytes to be shifted out by the SPI peripheral. Higher-level
 * software is responsible for deciding what those bytes mean and when queued
 * transmission should be triggered.
 *
 * When the channel is configured for 16-bit SPI operation, @p size must be a
 * multiple of 2 bytes so that the queued TX data remains aligned to SPI frames.
 * The driver remains byte-oriented at the public API boundary, but misaligned
 * byte counts are rejected in 16-bit mode.
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
bool HW_SPI_Load_Tx_Buffer( SPIPeripheral_T peripheral, const uint8_t* data, uint32_t size )
{
    SPIPeripheralState_T* peripheral_state = HW_SPI_Get_State( peripheral );
    uint32_t              first_copy_size  = 0U;
    uint32_t              second_copy_size = 0U;

    // The public API stays byte-oriented, but in 16-bit mode queued TX data
    // must still be an integer number of SPI frames. This is one of the few
    // checks kept in the low-level driver because misalignment breaks DMA frame
    // sizing.
    if ( HW_SPI_Is_Frame_Aligned_Size( peripheral_state, size ) == false )
    {
        return false;
    }

    // Prevent the TX DMA IRQ from modifying pending/in-flight state while this
    // function is calculating free space and updating the write pointer. This
    // disables only the channel's TX DMA IRQ, not global interrupts.
    NVIC_DisableIRQ( peripheral_state->tx_dma_irqn );

    // Free space accounts for both bytes still pending in the software ring and
    // bytes currently owned by DMA, because both occupy storage in tx_buffer.
    if ( size > HW_SPI_TX_Get_Free_Space( peripheral_state ) )
    {
        NVIC_EnableIRQ( peripheral_state->tx_dma_irqn );
        return false;
    }

    // Copy into the circular TX buffer. If the write pointer is near the end of
    // the buffer, the copy is split into an end span and a wrapped span at
    // index 0.
    first_copy_size = TX_BUFFER_SIZE_BYTES - peripheral_state->tx_write_position;
    if ( first_copy_size > size )
    {
        first_copy_size = size;
    }

    memcpy( &( peripheral_state->tx_buffer[peripheral_state->tx_write_position] ), data,
            first_copy_size );

    // If the message did not fit before the end of the buffer, copy the
    // remaining bytes to the start of the ring.
    second_copy_size = size - first_copy_size;
    if ( second_copy_size > 0U )
    {
        memcpy( &( peripheral_state->tx_buffer[0] ), &( data[first_copy_size] ), second_copy_size );
    }

    // Advance the write position modulo the ring size and record that these
    // bytes are pending. They are not removed from pending until a DMA transfer
    // is armed for them.
    peripheral_state->tx_write_position =
        ( peripheral_state->tx_write_position + size ) % TX_BUFFER_SIZE_BYTES;
    peripheral_state->tx_num_bytes_pending = peripheral_state->tx_num_bytes_pending + size;

    NVIC_EnableIRQ( peripheral_state->tx_dma_irqn );
    return true;
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

    // If DMA is already active or there is nothing to send, leave the queued
    // state unchanged. Higher-level code may call trigger again after loading
    // more data.
    if ( peripheral_state->tx_num_bytes_in_transmission > 0
         || peripheral_state->tx_write_position == 0 )
    {
        NVIC_EnableIRQ( peripheral_state->tx_dma_irqn );
        return;
    }

    // Clear stale terminal flags before starting a fresh transfer. These are
    // stream-specific because the STM32F4 LL flag helpers are stream-numbered.
    switch ( peripheral )
    {
        case SPI_CHANNEL_0:
            SPI_CHANNEL_0_TX_DMA_CLEAR_TC( peripheral_state->tx_dma );
            SPI_CHANNEL_0_TX_DMA_CLEAR_TE( peripheral_state->tx_dma );
            break;

        case SPI_CHANNEL_1:
            SPI_CHANNEL_1_TX_DMA_CLEAR_TC( peripheral_state->tx_dma );
            SPI_CHANNEL_1_TX_DMA_CLEAR_TE( peripheral_state->tx_dma );
            break;

        case SPI_DAC:
            SPI_DAC_TX_DMA_CLEAR_TC( peripheral_state->tx_dma );
            SPI_DAC_TX_DMA_CLEAR_TE( peripheral_state->tx_dma );
            break;

        default:
            NVIC_EnableIRQ( peripheral_state->tx_dma_irqn );
            return;
    }

    // Arm the first contiguous pending span. Any wrapped or later-appended span
    // is handled by the TX completion IRQ.
    if ( HW_SPI_TX_Start_DMA_From_Current_Read_Position( peripheral_state ) == false )
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
