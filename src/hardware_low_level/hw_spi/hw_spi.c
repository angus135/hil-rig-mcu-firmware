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
 *      appended to an internal ring buffer and transmitted using DMA when
 *      triggered. Each DMA transfer is armed for the next contiguous region of
 *      queued TX data. If the queued data wraps around the end of the ring, TX
 *      progression is continued by the DMA completion IRQ handler.
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
 *      - TX uses a circular queue buffer. Once all queued data has been sent, the
 *        read and write positions are left at their current ring positions.
 *      - Each TX DMA transfer can only transmit one contiguous span of the TX
 *        ring. Wrapped queued data is transmitted by re-arming DMA from the TX
 *        completion IRQ.
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
    uint8_t       rx_buffer[RX_BUFFER_SIZE_BYTES] __attribute__( ( aligned( 2 ) ) );
    uint32_t      rx_position;
    uint8_t       tx_buffer[TX_BUFFER_SIZE_BYTES] __attribute__( ( aligned( 2 ) ) );
    uint32_t      tx_write_position;
    uint32_t      tx_read_position;
    uint32_t      tx_num_bytes_queued;
    uint32_t tx_num_bytes_in_transmission;  // Note: stored in bytes so for 16 bit, divide by 2 for
                                            // elements
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
static void            HW_SPI_Configure_DMA_Data_Widths( SPIPeripheralState_T* peripheral_state );
static bool HW_SPI_TX_Start_DMA_From_Current_Read_Position( SPIPeripheralState_T* peripheral_state );
static void            HW_SPI_TX_Reset_State( SPIPeripheralState_T* peripheral_state );
static inline uint32_t HW_SPI_TX_Get_Free_Space( const SPIPeripheralState_T* peripheral_state );
static inline uint32_t HW_SPI_TX_Get_Contiguous_Read_Bytes( const SPIPeripheralState_T* peripheral_state );

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

    /*
     * Stop further TX DMA activity for this channel.
     */
    LL_DMA_DisableIT_TC( peripheral_state->tx_dma, peripheral_state->tx_dma_stream );
    LL_DMA_DisableIT_TE( peripheral_state->tx_dma, peripheral_state->tx_dma_stream );
    LL_SPI_DisableDMAReq_TX( peripheral_state->spi_peripheral );
    LL_DMA_DisableStream( peripheral_state->tx_dma, peripheral_state->tx_dma_stream );

    while ( LL_DMA_IsEnabledStream( peripheral_state->tx_dma, peripheral_state->tx_dma_stream )
            != 0U )
    {
    }

    /*
     * Drop knowledge of the currently active transfer. The queued buffer
     * positions are left unchanged so a higher layer may inspect or recover.
     */
    peripheral_state->tx_num_bytes_in_transmission = 0U;

    /*
     * TODO:
     * Add fault logging, error counters, or escalation here if desired.
     */
}

static inline void HW_SPI_TX_IRQ_Handler( SPIPeripheral_T peripheral )
{
    SPIPeripheralState_T* peripheral_state = HW_SPI_Get_State( peripheral );

    uint32_t completed_bytes = peripheral_state->tx_num_bytes_in_transmission;

    if ( completed_bytes == 0U )
    {
        return;
    }

    peripheral_state->tx_read_position =
        ( peripheral_state->tx_read_position + completed_bytes ) % TX_BUFFER_SIZE_BYTES;

    if ( completed_bytes >= peripheral_state->tx_num_bytes_queued )
    {
        peripheral_state->tx_num_bytes_queued = 0U;
    }
    else
    {
        peripheral_state->tx_num_bytes_queued =
            peripheral_state->tx_num_bytes_queued - completed_bytes;
    }

    peripheral_state->tx_num_bytes_in_transmission = 0U;

    /*
     * If the ring still contains queued bytes, re-arm DMA for the next
     * contiguous span. This is what allows wrapped ring data to be transmitted
     * as two back-to-back DMA transfers.
     */
    if ( peripheral_state->tx_num_bytes_queued == 0U )
    {
        return;
    }

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

static void HW_SPI_TX_Reset_State( SPIPeripheralState_T* peripheral_state )
{
    if ( peripheral_state == NULL )
    {
        return;
    }

    peripheral_state->tx_write_position            = 0U;
    peripheral_state->tx_read_position             = 0U;
    peripheral_state->tx_num_bytes_queued          = 0U;
    peripheral_state->tx_num_bytes_in_transmission = 0U;
}

static inline uint32_t HW_SPI_TX_Get_Free_Space( const SPIPeripheralState_T* peripheral_state )
{
    return TX_BUFFER_SIZE_BYTES - peripheral_state->tx_num_bytes_queued;
}

/*
 * DMA can only transmit one contiguous memory span at a time. If the queued TX
 * data wraps around the end of tx_buffer, this returns only the bytes from the
 * current read position to the end of the buffer. The IRQ handler will later
 * re-arm DMA for the wrapped span at tx_buffer[0].
 */
static inline uint32_t HW_SPI_TX_Get_Contiguous_Read_Bytes( const SPIPeripheralState_T* peripheral_state )
{
    uint32_t bytes_until_end = 0U;

    if ( peripheral_state->tx_num_bytes_queued == 0U )
    {
        return 0U;
    }

    bytes_until_end = TX_BUFFER_SIZE_BYTES - peripheral_state->tx_read_position;

    if ( peripheral_state->tx_num_bytes_queued < bytes_until_end )
    {
        return peripheral_state->tx_num_bytes_queued;
    }

    return bytes_until_end;
}

/**
 * @brief Start a TX DMA transfer from the current software read position.
 *
 * Software state remains byte-oriented. DMA length is configured in DMA elements,
 * which are 1 byte for 8-bit SPI or 2 bytes for 16-bit SPI.
 */
static bool HW_SPI_TX_Start_DMA_From_Current_Read_Position( SPIPeripheralState_T* peripheral_state )
{
    uint32_t bytes_to_send = 0;
    uint32_t dma_elements  = 0;

    bytes_to_send = HW_SPI_TX_Get_Contiguous_Read_Bytes( peripheral_state );

    if ( bytes_to_send == 0U )
    {
        return false;
    }

    if ( HW_SPI_Is_Frame_Aligned_Size( peripheral_state, bytes_to_send ) == false )
    {
        return false;
    }

    dma_elements = HW_SPI_Bytes_To_DMA_Elements( peripheral_state, bytes_to_send );

    LL_DMA_DisableStream( peripheral_state->tx_dma, peripheral_state->tx_dma_stream );
    while ( LL_DMA_IsEnabledStream( peripheral_state->tx_dma, peripheral_state->tx_dma_stream )
            != 0U )
    {
    }

    uint8_t* tx_ptr = &( peripheral_state->tx_buffer[peripheral_state->tx_read_position] );

    LL_DMA_SetMemoryAddress( peripheral_state->tx_dma, peripheral_state->tx_dma_stream,
                             ( uintptr_t )tx_ptr );

    LL_DMA_SetPeriphAddress( peripheral_state->tx_dma, peripheral_state->tx_dma_stream,
                             LL_SPI_DMA_GetRegAddr( peripheral_state->spi_peripheral ) );

    LL_DMA_SetDataLength( peripheral_state->tx_dma, peripheral_state->tx_dma_stream, dma_elements );

    /*
     * Keep software bookkeeping in bytes so the rest of the queue logic stays
     * byte-oriented and consistent with the public API.
     */
    peripheral_state->tx_num_bytes_in_transmission = bytes_to_send;

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
    /*
     * Handle transfer error first. If both TE and TC are somehow latched, error
     * handling wins and no normal completion processing is performed.
     */
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
    // SPI Channel
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
            HW_SPI_TX_Reset_State( channel_0_state );
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
            HW_SPI_TX_Reset_State( channel_1_state );
            break;
        case SPI_DAC:
            hspi           = &SPI_DAC_HANDLE;
            hspi->Instance = SPI_DAC_INSTANCE;
            memcpy( &( dac_state->config ), &configuration, sizeof( HWSPIConfig_T ) );
            dac_state->tx_dma         = SPI_DAC_TX_DMA;
            dac_state->tx_dma_stream  = SPI_DAC_TX_DMA_STREAM;
            dac_state->spi_peripheral = SPI_DAC_INSTANCE;
            dac_state->tx_dma_irqn    = SPI_DAC_TX_DMA_IRQN;
            HW_SPI_TX_Reset_State( dac_state );
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
void HW_SPI_Start_Channel( SPIPeripheral_T peripheral )
{
    SPI_HandleTypeDef*    hspi               = NULL;
    SPIPeripheralState_T* peripheral_state   = NULL;
    uint8_t*              rx_buffer          = NULL;
    uint16_t              rx_length_elements = 0U;

    switch ( peripheral )
    {
        case SPI_CHANNEL_0:
            hspi             = &SPI_CHANNEL_0_HANDLE;
            peripheral_state = channel_0_state;
            rx_buffer        = channel_0_state->rx_buffer;
            break;

        case SPI_CHANNEL_1:
            hspi             = &SPI_CHANNEL_1_HANDLE;
            peripheral_state = channel_1_state;
            rx_buffer        = channel_1_state->rx_buffer;
            break;

        case SPI_DAC:  // SPI will not be receiving anything
        default:
            return;
    }
    /*
     * Public software API remains byte-oriented, but HAL/DMA needs the transfer
     * length in SPI frames, not bytes.
     */
    rx_length_elements = HW_SPI_Bytes_To_DMA_Elements( peripheral_state, RX_BUFFER_SIZE_BYTES );

    peripheral_state->rx_position = 0U;

    // Disable Rx interrupts
    NVIC_DisableIRQ( SPI_CHANNEL_0_RX_DMA_IRQN );
    NVIC_DisableIRQ( SPI_CHANNEL_1_RX_DMA_IRQN );

    HAL_SPI_Receive_DMA( hspi, rx_buffer, rx_length_elements );
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

    /*
     * DMA NDTR is expressed in DMA elements, not bytes. Convert back to bytes so
     * the software RX stream remains byte-oriented.
     */
    dma_remaining_bytes = HW_SPI_DMA_Elements_To_Bytes( peripheral_state, dma_remaining_elements );

    dma_write_index = RX_BUFFER_SIZE_BYTES - dma_remaining_bytes;

    if ( dma_write_index == RX_BUFFER_SIZE_BYTES )
    {
        dma_write_index = 0U;
    }

    unread_bytes = ( dma_write_index + RX_BUFFER_SIZE_BYTES - read_index ) % RX_BUFFER_SIZE_BYTES;

    if ( unread_bytes == 0U )
    {
        return ( HWSPIRxSpans_T ){ .first_span  = { .data = &rx_buffer[0], .length_bytes = 0U },
                                   .second_span = { .data = &rx_buffer[0], .length_bytes = 0U },
                                   .total_length_bytes = 0U };
    }

    if ( dma_write_index > read_index )
    {
        return ( HWSPIRxSpans_T ){
            .first_span         = { .data = &rx_buffer[read_index], .length_bytes = unread_bytes },
            .second_span        = { .data = &rx_buffer[0], .length_bytes = 0U },
            .total_length_bytes = unread_bytes };
    }

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
    if ( peripheral_state == NULL )
    {
        return;
    }

    /*
     * In 16-bit mode, keep the software consume pointer aligned to SPI frames.
     * Public lengths remain byte-based, but they must still be frame-aligned.
     */
    if ( HW_SPI_Is_Frame_Aligned_Size( peripheral_state, bytes_to_consume ) == false )
    {
        return;
    }

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
 * The TX buffer used by this driver is a circular software queue. Buffered data
 * remains in the queue until transmitted by the TX engine. If queued data wraps
 * around the end of the internal buffer, it is transmitted as multiple
 * contiguous DMA transfers.
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
    /*
     * The public API stays byte-oriented, but in 16-bit mode queued TX data must
     * still be an integer number of SPI frames.
     */
    if ( HW_SPI_Is_Frame_Aligned_Size( peripheral_state, size ) == false )
    {
        return false;
    }

    NVIC_DisableIRQ(
        peripheral_state->tx_dma_irqn );  //  Need to disable IRQ to prevent race condition with ISR

    /*
     * RING BUFFER CHANGE:
     * Check available ring capacity instead of checking whether write + size
     * reaches the physical end of the array. The write pointer may legitimately
     * wrap back to index 0.
     */
    if ( size > HW_SPI_TX_Get_Free_Space( peripheral_state ) )
    {
        NVIC_EnableIRQ( peripheral_state->tx_dma_irqn );
        return false;
    }

    /*
     * RING BUFFER CHANGE:
     * Copy in one or two chunks. The first chunk runs from the current write
     * position to either the end of the TX buffer or the end of the source data.
     * The optional second chunk wraps to tx_buffer[0].
     */
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
        memcpy( &( peripheral_state->tx_buffer[0] ), &( data[first_copy_size] ),
                second_copy_size );
    }

    peripheral_state->tx_write_position =
        ( peripheral_state->tx_write_position + size ) % TX_BUFFER_SIZE_BYTES;

    peripheral_state->tx_num_bytes_queued = peripheral_state->tx_num_bytes_queued + size;

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

    /*
     * Protect against a race with the TX DMA IRQ handler. We only disable the
     * specific DMA IRQ for this channel, not global interrupts.
     */
    NVIC_DisableIRQ( peripheral_state->tx_dma_irqn );

    /*
     * RING BUFFER CHANGE:
     * Do not use tx_write_position == 0 to detect an empty TX queue. In a ring
     * buffer, write position 0 is a valid wrapped write location. Use the
     * queued-byte count instead.
     */
    if ( ( peripheral_state->tx_num_bytes_in_transmission > 0U )
         || ( peripheral_state->tx_num_bytes_queued == 0U ) )
    {
        NVIC_EnableIRQ( peripheral_state->tx_dma_irqn );
        return;
    }

    /*
     * Clear any stale terminal flags before starting a fresh transfer.
     * These are stream-specific and should be adjusted if the stream mapping
     * changes.
     */
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

    if ( HW_SPI_TX_Start_DMA_From_Current_Read_Position( peripheral_state ) == false )
    {
        HW_SPI_TX_Error_Handler( peripheral );
    }

    NVIC_EnableIRQ( peripheral_state->tx_dma_irqn );
}
