/******************************************************************************
 *  File:       hw_spi.c
 *  Author:     Angus Corr
 *  Created:    10-Apr-2026
 *
 *  Description:
 *      <Short description of the module's purpose and responsibilities>
 *
 *  Notes:
 *      <Any design notes, dependencies, or assumptions go here>
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
#define SPI_CHANNEL_0_RX_DMA_STREAM LL_DMA_STREAM_2
#define SPI_CHANNEL_0_RX_DMA_IRQ DMA2_Stream2_IRQHandler
#define SPI_CHANNEL_0_RX_DMA_IRQN DMA2_Stream2_IRQn
#define SPI_CHANNEL_0_TX_DMA DMA2
#define SPI_CHANNEL_0_TX_DMA_STREAM LL_DMA_STREAM_3
#define SPI_CHANNEL_0_TX_DMA_IRQ DMA2_Stream3_IRQHandler
#define SPI_CHANNEL_0_TX_DMA_IRQN DMA2_Stream3_IRQn
#define SPI_CHANNEL_1_RX_DMA DMA2
#define SPI_CHANNEL_1_RX_DMA_STREAM LL_DMA_STREAM_0
#define SPI_CHANNEL_1_RX_DMA_IRQ DMA2_Stream0_IRQHandler
#define SPI_CHANNEL_1_RX_DMA_IRQN DMA2_Stream0_IRQn
#define SPI_CHANNEL_1_TX_DMA DMA2
#define SPI_CHANNEL_1_TX_DMA_STREAM LL_DMA_STREAM_1
#define SPI_CHANNEL_1_TX_DMA_IRQ DMA2_Stream1_IRQHandler
#define SPI_CHANNEL_1_TX_DMA_IRQN DMA2_Stream1_IRQn
#define SPI_DAC_TX_DMA DMA2
#define SPI_DAC_TX_DMA_STREAM LL_DMA_STREAM_1
#define SPI_DAC_TX_DMA_IRQ DMA2_Stream1_IRQHandler
#define SPI_DAC_TX_DMA_IRQN DMA2_Stream1_IRQn

#define RX_BUFFER_SIZE_BYTES 1024
#define TX_BUFFER_SIZE_BYTES 1024

/**-----------------------------------------------------------------------------
 *  Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */
typedef struct SPIPeripheralState_T
{
    HWSPIConfig_T config;
    uint8_t       rx_buffer[RX_BUFFER_SIZE_BYTES];
    uint32_t      rx_position;
    uint8_t       tx_buffer[TX_BUFFER_SIZE_BYTES];
    uint32_t      tx_write_position;
    uint32_t      tx_read_position;
    uint32_t      tx_num_in_transmission;
    DMA_TypeDef*  rx_dma;
    uint32_t      rx_dma_stream;
    DMA_TypeDef*  tx_dma;
    uint32_t      tx_dma_stream;
    SPI_TypeDef*  spi_peripheral;
    IRQn_Type     tx_dma_irqn;
} SPIPeripheralState_T;

/**-----------------------------------------------------------------------------
 *  Public (global) and Extern Variables
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Private (static) Variables
 *------------------------------------------------------------------------------
 */
SPIPeripheralState_T  channel_0_state_struct = { 0 };
SPIPeripheralState_T  channel_1_state_struct = { 0 };
SPIPeripheralState_T  dac_state_struct       = { 0 };
SPIPeripheralState_T* channel_0_state        = &channel_0_state_struct;
SPIPeripheralState_T* channel_1_state        = &channel_1_state_struct;
SPIPeripheralState_T* dac_state              = &dac_state_struct;

/**-----------------------------------------------------------------------------
 *  Private (static) Function Prototypes
 *------------------------------------------------------------------------------
 */

static inline SPIPeripheralState_T* HW_SPI_Get_State( SPIPeripheral_T peripheral );
static void                         HW_SPI_TX_Error_Handler( SPIPeripheral_T peripheral );
static inline void                  HW_SPI_TX_IRQ_Handler( SPIPeripheral_T peripheral );

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
    peripheral_state->tx_num_in_transmission = 0U;

    /*
     * TODO:
     * Add fault logging, error counters, or escalation here if desired.
     */
}

static inline void HW_SPI_TX_IRQ_Handler( SPIPeripheral_T peripheral )
{
    SPIPeripheralState_T* peripheral_state = HW_SPI_Get_State( peripheral );

    /*
     * Advance the software read position past the bytes that have just finished
     * transmitting.
     */
    peripheral_state->tx_read_position =
        peripheral_state->tx_read_position + peripheral_state->tx_num_in_transmission;

    /*
     * The just-finished DMA transfer is no longer active.
     */
    peripheral_state->tx_num_in_transmission = 0U;

    /*
     * If all queued bytes have now been transmitted, reset the linear buffer
     * state back to empty so the next transmission session starts from index 0.
     */
    if ( peripheral_state->tx_read_position >= peripheral_state->tx_write_position )
    {
        peripheral_state->tx_read_position  = 0U;
        peripheral_state->tx_write_position = 0U;
        return;
    }

    /*
     * More data was appended while the previous DMA transfer was active.
     * Re-arm DMA for the remaining unsent portion of the TX buffer.
     */
    uint32_t bytes_remaining =
        peripheral_state->tx_write_position - peripheral_state->tx_read_position;

    LL_DMA_DisableStream( peripheral_state->tx_dma, peripheral_state->tx_dma_stream );
    while ( LL_DMA_IsEnabledStream( peripheral_state->tx_dma, peripheral_state->tx_dma_stream )
            != 0U )
    {
    }

    LL_DMA_SetMemoryAddress(
        peripheral_state->tx_dma, peripheral_state->tx_dma_stream,
        ( uint32_t ) & ( peripheral_state->tx_buffer[peripheral_state->tx_read_position] ) );

    LL_DMA_SetPeriphAddress( peripheral_state->tx_dma, peripheral_state->tx_dma_stream,
                             LL_SPI_DMA_GetRegAddr( peripheral_state->spi_peripheral ) );

    LL_DMA_SetDataLength( peripheral_state->tx_dma, peripheral_state->tx_dma_stream,
                          bytes_remaining );  // TODO: Adjust based on element size

    /*
     * Mark the transfer as active before enabling the stream so software state
     * already reflects "busy" if anything else inspects it immediately.
     */
    peripheral_state->tx_num_in_transmission = bytes_remaining;

    LL_SPI_EnableDMAReq_TX( peripheral_state->spi_peripheral );
    LL_DMA_EnableIT_TC( peripheral_state->tx_dma, peripheral_state->tx_dma_stream );
    LL_DMA_EnableIT_TE( peripheral_state->tx_dma, peripheral_state->tx_dma_stream );
    LL_DMA_EnableStream( peripheral_state->tx_dma, peripheral_state->tx_dma_stream );
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

void SPI_CHANNEL_0_TX_DMA_IRQ(
    void )  // TODO: If the DMA stream/channel changes, will need to change TE3/TC3 etc.
{
    /*
     * Handle transfer error first. If both TE and TC are somehow latched, error
     * handling wins and no normal completion processing is performed.
     */
    if ( LL_DMA_IsActiveFlag_TE3( SPI_CHANNEL_0_TX_DMA ) != 0U )
    {
        LL_DMA_ClearFlag_TE3( SPI_CHANNEL_0_TX_DMA );
        HW_SPI_TX_Error_Handler( SPI_CHANNEL_0 );
        return;
    }

    if ( LL_DMA_IsActiveFlag_TC3( SPI_CHANNEL_0_TX_DMA ) != 0U )
    {
        LL_DMA_ClearFlag_TC3( SPI_CHANNEL_0_TX_DMA );
        HW_SPI_TX_IRQ_Handler( SPI_CHANNEL_0 );
        return;
    }
}

void SPI_CHANNEL_1_TX_DMA_IRQ(
    void )  // TODO: If the DMA stream/channel changes, will need to change TE3/TC3 etc.
{
    if ( LL_DMA_IsActiveFlag_TE1( SPI_CHANNEL_1_TX_DMA ) != 0U )
    {
        LL_DMA_ClearFlag_TE1( SPI_CHANNEL_1_TX_DMA );
        HW_SPI_TX_Error_Handler( SPI_CHANNEL_1 );
        return;
    }

    if ( LL_DMA_IsActiveFlag_TC1( SPI_CHANNEL_1_TX_DMA ) != 0U )
    {
        LL_DMA_ClearFlag_TC1( SPI_CHANNEL_1_TX_DMA );
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
 * The channel should only be started after successful configuration.
 *
 * @param peripheral
 *     The SPI peripheral/channel to start.
 */
void HW_SPI_Start_Channel( SPIPeripheral_T peripheral )
{
    SPI_HandleTypeDef* hspi      = NULL;
    uint8_t*           rx_buffer = NULL;
    switch ( peripheral )
    {
        case SPI_CHANNEL_0:
            hspi      = &SPI_CHANNEL_0_HANDLE;
            rx_buffer = ( channel_0_state->rx_buffer );
            break;
        case SPI_CHANNEL_1:
            hspi      = &SPI_CHANNEL_1_HANDLE;
            rx_buffer = ( channel_1_state->rx_buffer );
            break;
        case SPI_DAC:
            hspi = &SPI_DAC_HANDLE;
            return;
            break;
        default:
            return;
    }

    HAL_SPI_Receive_DMA( hspi, rx_buffer, RX_BUFFER_SIZE_BYTES );
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
 * @param peripheral
 *     The SPI peripheral/channel to inspect.
 *
 * @return
 *     A structure describing the unread RX data as one or two spans.
 */
HWSPIRxSpans_T HW_SPI_Rx_Peek( SPIPeripheral_T peripheral )
{
    uint8_t* rx_buffer       = NULL;
    uint32_t read_index      = 0U;
    uint32_t dma_remaining   = 0U;
    uint32_t dma_write_index = 0U;
    uint32_t unread_bytes    = 0U;

    // TODO: fix this based on state
    switch ( peripheral )
    {
        case SPI_CHANNEL_0:
            rx_buffer  = ( channel_0_state->rx_buffer );
            read_index = ( channel_0_state->rx_position );
            dma_remaining =
                LL_DMA_GetDataLength( SPI_CHANNEL_0_RX_DMA, SPI_CHANNEL_0_RX_DMA_STREAM );
            break;

        case SPI_CHANNEL_1:
            rx_buffer  = ( channel_1_state->rx_buffer );
            read_index = ( channel_1_state->rx_position );
            dma_remaining =
                LL_DMA_GetDataLength( SPI_CHANNEL_1_RX_DMA, SPI_CHANNEL_1_RX_DMA_STREAM );
            break;

        case SPI_DAC:
        default:
            return ( HWSPIRxSpans_T ){ .first_span         = { .data = NULL, .length_bytes = 0U },
                                       .second_span        = { .data = NULL, .length_bytes = 0U },
                                       .total_length_bytes = 0U };
    }

    dma_write_index =
        RX_BUFFER_SIZE_BYTES - dma_remaining;  // TODO: Adjust based on size of DMA element

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
    switch ( peripheral )
    {
        case SPI_CHANNEL_0:
            ( channel_0_state->rx_position ) =
                ( ( channel_0_state->rx_position ) + bytes_to_consume ) % RX_BUFFER_SIZE_BYTES;
            break;
        case SPI_CHANNEL_1:
            ( channel_1_state->rx_position ) =
                ( ( channel_1_state->rx_position ) + bytes_to_consume ) % RX_BUFFER_SIZE_BYTES;
            break;
        case SPI_DAC:
        default:
            return;
    }
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
bool HW_SPI_Load_Tx_Buffer( SPIPeripheral_T peripheral, const uint8_t* data, uint32_t size )
{
    SPIPeripheralState_T* peripheral_state = HW_SPI_Get_State( peripheral );
    if ( peripheral_state == NULL )
    {
        return false;
    }

    NVIC_DisableIRQ(
        peripheral_state->tx_dma_irqn );  //  Need to disable IRQ to prevent race condition with ISR

    // Exit if trying to load a message that exceeds buffer size.
    // Note: this may also occur if there are a bunch of messages in the queue that haven't been
    // cleared yet, in which case should also fail.
    if ( peripheral_state->tx_write_position + size > TX_BUFFER_SIZE_BYTES )
    {
        NVIC_EnableIRQ( peripheral_state->tx_dma_irqn );
        return false;
    }
    // Copying data into buffer and updating write pointer.
    memcpy( &( peripheral_state->tx_buffer[peripheral_state->tx_write_position] ), data, size );
    peripheral_state->tx_write_position = peripheral_state->tx_write_position + size;
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

    // If DMA is already active or there is nothing to send then return
    if ( peripheral_state->tx_num_in_transmission > 0 || peripheral_state->tx_write_position == 0 )
    {
        NVIC_EnableIRQ( peripheral_state->tx_dma_irqn );
        return;
    }

    // Disable DMA stream initially
    LL_DMA_DisableStream( peripheral_state->tx_dma, peripheral_state->tx_dma_stream );
    while ( LL_DMA_IsEnabledStream( peripheral_state->tx_dma, peripheral_state->tx_dma_stream )
            != 0U )
    {
    }

    /*
     * Clear any stale terminal flags before starting a fresh transfer.
     * These are stream-specific and should be adjusted if the stream mapping
     * changes.
     */
    // TODO: If the DMA stream/channel changes, will need to change TE3/TC3 etc.
    switch ( peripheral )
    {
        case SPI_CHANNEL_0:
            LL_DMA_ClearFlag_TC3( peripheral_state->tx_dma );
            LL_DMA_ClearFlag_TE3( peripheral_state->tx_dma );
            break;

        case SPI_CHANNEL_1:
            LL_DMA_ClearFlag_TC1( peripheral_state->tx_dma );
            LL_DMA_ClearFlag_TE1( peripheral_state->tx_dma );
            break;

        case SPI_DAC:
            LL_DMA_ClearFlag_TC1( peripheral_state->tx_dma );
            LL_DMA_ClearFlag_TE1( peripheral_state->tx_dma );
            break;

        default:
            NVIC_EnableIRQ( peripheral_state->tx_dma_irqn );
            return;
    }

    LL_DMA_SetMemoryAddress(
        peripheral_state->tx_dma, peripheral_state->tx_dma_stream,
        ( uint32_t ) & ( peripheral_state->tx_buffer[peripheral_state->tx_read_position] ) );
    LL_DMA_SetPeriphAddress( peripheral_state->tx_dma, peripheral_state->tx_dma_stream,
                             LL_SPI_DMA_GetRegAddr( peripheral_state->spi_peripheral ) );
    LL_DMA_SetDataLength(
        peripheral_state->tx_dma, peripheral_state->tx_dma_stream,
        peripheral_state->tx_write_position
            - peripheral_state->tx_read_position );  // TODO: Adjust based on element size

    LL_SPI_EnableDMAReq_TX( peripheral_state->spi_peripheral );
    LL_DMA_EnableIT_TC( peripheral_state->tx_dma, peripheral_state->tx_dma_stream );
    LL_DMA_EnableIT_TE( peripheral_state->tx_dma, peripheral_state->tx_dma_stream );
    LL_DMA_EnableStream( peripheral_state->tx_dma, peripheral_state->tx_dma_stream );

    peripheral_state->tx_num_in_transmission =
        peripheral_state->tx_write_position - peripheral_state->tx_read_position;

    NVIC_EnableIRQ( peripheral_state->tx_dma_irqn );
}
