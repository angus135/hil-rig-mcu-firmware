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

static inline void HW_SPI_TX_IRQ_Handler( SPIPeripheral_T peripheral );

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

static inline void HW_SPI_TX_IRQ_Handler( SPIPeripheral_T peripheral )
{
    SPIPeripheralState_T* peripheral_state = NULL;
    switch ( peripheral )
    {
        case SPI_CHANNEL_0:
            peripheral_state = channel_0_state;
            break;
        case SPI_CHANNEL_1:
            peripheral_state = channel_1_state;
            break;
        case SPI_DAC:
            peripheral_state = dac_state;
            break;
        default:
            return;
    }
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

    LL_SPI_EnableDMAReq_TX( peripheral_state->spi_peripheral );
    LL_DMA_EnableIT_TC( peripheral_state->tx_dma, peripheral_state->tx_dma_stream );
    LL_DMA_EnableIT_TE( peripheral_state->tx_dma, peripheral_state->tx_dma_stream );
    LL_DMA_EnableStream( peripheral_state->tx_dma, peripheral_state->tx_dma_stream );

    peripheral_state->tx_num_in_transmission = bytes_remaining;
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
    HW_SPI_TX_IRQ_Handler( SPI_CHANNEL_0 );
}

void SPI_CHANNEL_1_TX_DMA_IRQ( void )
{
    HW_SPI_TX_IRQ_Handler( SPI_CHANNEL_1 );
}

// Note: Not implemented yet as Channel 1 and DAC are on the same port right now.
// void SPI_DAC_TX_DMA_IRQ( void )
// {
//     HW_SPI_TX_IRQ_Handler( SPI_DAC );

// }

/**
 * @brief Configure a hardware SPI channel.
 *
 * Applies the provided configuration to the selected SPI peripheral and prepares
 * any associated low-level resources required by the driver, such as peripheral
 * registers, DMA streams, internal software state, and internal RX/TX buffers.
 *
 * This function configures the channel role and operating parameters, including
 * settings such as master/slave mode, SPI mode, bitrate, data width, and any
 * other hardware-level options contained within @p configuration.
 *
 * This function does not start runtime operation of the channel. After a channel
 * has been successfully configured, HW_SPI_Start_Channel() must be called before
 * the peripheral is used.
 *
 * If the channel is already running, behaviour is implementation-defined. The
 * intended usage is that configuration occurs while the channel is stopped.
 *
 * @param peripheral
 *     The SPI peripheral/channel to configure.
 *
 * @param configuration
 *     The hardware SPI configuration to apply to the selected channel.
 *
 * @return
 *     true if the channel was configured successfully.
 *     false if the configuration was invalid, unsupported, or the hardware
 *     resources required for the channel could not be configured.
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
 * @brief Start operation of a configured SPI channel.
 *
 * Enables runtime operation of the selected SPI channel using the configuration
 * previously applied by HW_SPI_Configure_Channel().
 *
 * For a slave channel, this function shall start the background hardware state
 * required for slave operation, including any continuous DMA-based reception
 * mechanism and any associated internal state used by the driver.
 *
 * For a master channel, this function shall place the channel into its ready
 * state such that master write/read transactions may later be initiated using
 * HW_SPI_Master_Write_Read(). Master transfers are not started automatically by
 * this function.
 *
 * This function does not perform any higher-level framing, protocol parsing, or
 * timestamp handling. Those responsibilities belong to higher software layers.
 *
 * The channel should only be started after successful configuration.
 *
 * @param peripheral
 *     The SPI peripheral/channel to start.
 */
void HW_SPI_Start_Channel( SPIPeripheral_T peripheral )
{
    HWSPIConfig_T*     peripheral_to_start = NULL;
    SPI_HandleTypeDef* hspi                = NULL;
    uint8_t*           rx_buffer           = NULL;
    switch ( peripheral )
    {
        case SPI_CHANNEL_0:
            peripheral_to_start = &( channel_0_state->config );
            hspi                = &SPI_CHANNEL_0_HANDLE;
            rx_buffer           = ( channel_0_state->rx_buffer );
            break;
        case SPI_CHANNEL_1:
            peripheral_to_start = &( channel_1_state->config );
            hspi                = &SPI_CHANNEL_1_HANDLE;
            rx_buffer           = ( channel_1_state->rx_buffer );
            break;
        case SPI_DAC:
            peripheral_to_start = &( dac_state->config );
            hspi                = &SPI_DAC_HANDLE;
            break;
        default:
            return;
    }

    switch ( peripheral_to_start->spi_mode )
    {
        case SPI_MASTER_MODE:
            break;
        case SPI_SLAVE_MODE:
            HAL_SPI_Receive_DMA( hspi, rx_buffer, RX_BUFFER_SIZE_BYTES );
            break;
        default:
            return;
    }
}

/**
 * @brief Stop operation of a configured SPI channel.
 *
 * Stops runtime operation of the selected SPI channel and disables any active
 * hardware mechanisms used by the low-level driver for that channel.
 *
 * For a slave channel, this shall stop any background RX/TX activity managed by
 * the driver, including any DMA streams or internal tracking state associated
 * with continuous reception.
 *
 * For a master channel, this shall stop the channel and prevent further
 * transactions from being initiated until the channel is started again. If a
 * transfer is active when this function is called, behaviour is
 * implementation-defined unless otherwise specified by the driver design.
 *
 * This function does not clear the higher-level protocol state owned by the
 * mid-level driver.
 *
 * @param peripheral
 *     The SPI peripheral/channel to stop.
 */
void HW_SPI_Stop_Channel( SPIPeripheral_T peripheral )
{
    HWSPIConfig_T*     peripheral_to_stop = NULL;
    SPI_HandleTypeDef* hspi               = NULL;
    switch ( peripheral )
    {
        case SPI_CHANNEL_0:
            peripheral_to_stop = &( channel_0_state->config );
            hspi               = &SPI_CHANNEL_0_HANDLE;
            break;
        case SPI_CHANNEL_1:
            peripheral_to_stop = &( channel_1_state->config );
            hspi               = &SPI_CHANNEL_1_HANDLE;
            break;
        case SPI_DAC:
            peripheral_to_stop = &( dac_state->config );
            hspi               = &SPI_DAC_HANDLE;
            break;
        default:
            return;
    }

    switch ( peripheral_to_stop->spi_mode )
    {
        case SPI_MASTER_MODE:
            break;
        case SPI_SLAVE_MODE:
            HAL_SPI_DMAStop( hspi );
            break;
        default:
            return;
    }
}

/**
 * @brief  Return the unread slave RX data as one or two spans into the internal DMA buffer.
 *
 * Provides a read-only view of the unread portion of the selected SPI slave RX
 * DMA buffer without consuming any data.
 *
 * Because the RX DMA buffer is circular, unread data may either be contiguous or
 * split across the end and beginning of the buffer. This function returns up to
 * two spans describing that unread data.
 *
 * The returned pointers refer to memory owned by the low-level driver. The
 * caller must not modify this memory and must copy it if persistence is
 * required.
 *
 * This function does not advance the RX consume position. The mid-level driver
 * must call HW_SPI_Slave_Rx_Consume() after it has processed the required
 * number of bytes.
 *
 * @param peripheral
 *     The SPI peripheral/channel to inspect. This channel must be configured in
 *     slave mode.
 *
 * @return
 *     A structure describing the unread slave RX data as one or two spans.
 */
HWSPIRxSpans_T HW_SPI_Slave_Rx_Peek( SPIPeripheral_T peripheral )
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
 * @brief Mark previously received slave RX bytes as consumed.
 *
 * Advances the internal unread-data consume position for the selected slave
 * channel by the specified number of bytes.
 *
 * This function provides the "consume" stage of the slave RX peek/consume model.
 * It is intended to be called by the mid-level driver after it has inspected or
 * parsed data returned by HW_SPI_Slave_Rx_Peek() and determined that a number of
 * bytes can be discarded from the unread portion of the internal RX buffer.
 *
 * The driver shall only consume bytes that are currently unread. If
 * @p bytes_to_consume exceeds the number of unread bytes currently tracked by
 * the driver, behaviour is implementation-defined unless explicitly handled by
 * the implementation.
 *
 * This function is intended for use only on channels configured in slave mode.
 * Calling it on a master channel is invalid.
 *
 * @param peripheral
 *     The SPI peripheral/channel whose unread slave RX data should be advanced.
 *
 * @param bytes_to_consume
 *     The number of unread RX bytes to mark as consumed.
 *
 * @note
 *     This function does not copy any data. It only updates the internal consume
 *     position maintained by the low-level driver.
 */
void HW_SPI_Slave_Rx_Consume( SPIPeripheral_T peripheral, uint32_t bytes_to_consume )
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
 * @brief Load data into the slave transmit buffer for a channel.
 *
 * Copies the supplied data into the selected slave channel's internal transmit
 * buffer so that the channel can present this data on future slave-side SPI
 * transmissions when clocked by an external master.
 *
 * The provided data is copied into internal driver-owned storage. The caller
 * therefore retains ownership of @p data and may modify or discard the source
 * buffer after this function returns.
 *
 * This function does not define higher-level frame semantics. It only loads the
 * low-level bytes to be shifted out by the slave peripheral. The mid-level
 * driver is responsible for determining what data should be made available to
 * the external master and when it should be updated.
 *
 * If the transmit buffer cannot accept the requested data, or the request is not
 * valid for the current state of the channel, the function shall return false.
 *
 * This function is intended for use only on channels configured in slave mode.
 * Calling it on a master channel is invalid.
 *
 * @param peripheral
 *     The SPI peripheral/channel whose slave TX buffer is to be updated.
 *
 * @param data
 *     Pointer to the source data to copy into the internal slave TX buffer.
 *
 * @param size
 *     Number of bytes to copy from @p data into the internal slave TX buffer.
 *
 * @return
 *     true if the data was accepted and loaded successfully.
 *     false if the buffer could not be loaded, the size was invalid, or the
 *     operation was not valid for the current channel state.
 */
bool HW_SPI_Slave_Load_Tx_Buffer( SPIPeripheral_T peripheral, const uint8_t* data, uint32_t size )
{
    // Pointer to variable to update
    uint32_t* spi_tx_write_position = NULL;
    // Pointer to start of buffer
    uint8_t* spi_tx_buffer = NULL;
    // IRQ number for DMA to disable interrupts for preventing race condition.
    IRQn_Type dma_irq = OTG_HS_IRQn;  // Default interrupt that we don't use in project
    switch ( peripheral )
    {
        case SPI_CHANNEL_0:
            spi_tx_write_position = &( channel_0_state->tx_write_position );
            spi_tx_buffer         = ( channel_0_state->tx_buffer );
            dma_irq               = channel_0_state->tx_dma_irqn;
            break;
        case SPI_CHANNEL_1:
            spi_tx_write_position = &( channel_1_state->tx_write_position );
            spi_tx_buffer         = ( channel_1_state->tx_buffer );
            dma_irq               = channel_1_state->tx_dma_irqn;
            break;
        case SPI_DAC:
            spi_tx_write_position = &( dac_state->tx_write_position );
            spi_tx_buffer         = ( dac_state->tx_buffer );
            dma_irq               = dac_state->tx_dma_irqn;
        default:
            return false;
    }
    // Exit if trying to load a message that exceeds buffer size.
    // Note: this may also occur if there are a bunch of messages in the queue that haven't been
    // cleared yet, in which case should also fail.
    if ( *spi_tx_write_position + size > TX_BUFFER_SIZE_BYTES )
    {
        return false;
    }
    // Copying data into buffer and updating write pointer.
    NVIC_DisableIRQ( dma_irq );  //  Need to disable IRQ to prevent race condition with ISR
    memcpy( &( spi_tx_buffer[*spi_tx_write_position] ), data, size );
    *spi_tx_write_position = *spi_tx_write_position + size;
    NVIC_EnableIRQ( dma_irq );
    return true;
}

/**
 * @brief Trigger transmission of queued slave TX data for a channel.
 *
 * Starts the slave transmit DMA for the selected SPI channel if queued transmit
 * data is available and no transmit DMA transfer is currently in progress.
 *
 * This function provides the "trigger" stage of the slave TX queue model. It is
 * intended to be called by the mid-level driver after one or more messages have
 * been loaded into the internal slave TX buffer using the corresponding load
 * function.
 *
 * If a transmit DMA transfer is already active, this function does not restart,
 * interrupt, or modify the current transfer. In this case, the function simply
 * leaves the existing transmission in progress.
 *
 * If no queued transmit data is available, or the selected channel is not valid
 * for slave TX operation, the function shall do nothing.
 *
 * This function only starts transmission of data already stored in the
 * low-level driver's internal slave TX buffer. It does not copy any new data
 * into the transmit queue and does not define higher-level frame semantics. The
 * mid-level driver is responsible for determining what data should be queued and
 * when transmission should be triggered.
 *
 * This function is intended for use only on channels configured in slave mode.
 * Calling it on a master channel is invalid.
 *
 * @param peripheral
 *     The SPI peripheral/channel whose queued slave TX data should be
 *     transmitted.
 */
void HW_SPI_Slave_Tx_Trigger( SPIPeripheral_T peripheral )
{
    SPIPeripheralState_T* peripheral_state = NULL;
    switch ( peripheral )
    {
        case SPI_CHANNEL_0:
            peripheral_state = channel_0_state;
            break;
        case SPI_CHANNEL_1:
            peripheral_state = channel_1_state;
            break;
        case SPI_DAC:
            peripheral_state = dac_state;
            break;
        default:
            return;
    }

    // If DMA is already active or there is nothing to send then return
    if ( peripheral_state->tx_num_in_transmission > 0 || peripheral_state->tx_write_position == 0 )
    {
        return;
    }

    // Disable DMA stream initially
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
    LL_DMA_SetDataLength(
        peripheral_state->tx_dma, peripheral_state->tx_dma_stream,
        peripheral_state->tx_write_position );  // TODO: Adjust based on element size

    LL_SPI_EnableDMAReq_TX( peripheral_state->spi_peripheral );
    LL_DMA_EnableIT_TC( peripheral_state->tx_dma, peripheral_state->tx_dma_stream );
    LL_DMA_EnableIT_TE( peripheral_state->tx_dma, peripheral_state->tx_dma_stream );
    LL_DMA_EnableStream( peripheral_state->tx_dma, peripheral_state->tx_dma_stream );

    peripheral_state->tx_num_in_transmission = peripheral_state->tx_write_position;
}

/**
 * @brief Start a master-mode SPI write/read transaction.
 *
 * Initiates a master-mode SPI transfer on the selected channel using the
 * provided write buffer and read buffer.
 *
 * At the hardware level, SPI master transfers are full duplex. This function
 * therefore represents the fundamental low-level master transaction primitive:
 * bytes are shifted out from @p write_data while bytes are simultaneously
 * captured into @p read_data.
 *
 * Higher software layers may use this primitive to implement:
 * - write-only operations, where received data is ignored,
 * - read operations, where @p write_data contains dummy bytes,
 * - write-then-read style transactions, where @p write_data contains command
 *   bytes followed by any required dummy bytes.
 *
 * This function is intended to be non-blocking. A successful call indicates that
 * the transfer has been accepted and started by the hardware driver. Completion
 * shall later be observed using HW_SPI_Master_Is_Busy(),
 * HW_SPI_Master_Transfer_Complete(), and
 * HW_SPI_Master_Get_Last_Transfer_Size().
 *
 * The caller must ensure that the memory referenced by @p write_data and
 * @p read_data remains valid and unmodified as required until the transfer has
 * completed, unless the implementation explicitly documents different ownership
 * semantics.
 *
 * If the channel is busy or the transfer cannot be started, the function shall
 * return false and no new transfer shall be initiated.
 *
 * This function is intended for use only on channels configured in master mode.
 * Calling it on a slave channel is invalid.
 *
 * @param peripheral
 *     The SPI peripheral/channel on which to start the master transaction.
 *
 * @param write_data
 *     Pointer to the data to be transmitted by the master.
 *
 * @param read_data
 *     Pointer to the destination buffer into which received bytes will be
 *     written by the transfer.
 *
 * @param size
 *     Number of bytes to transmit and receive as part of the transaction.
 *
 * @return
 *     true if the transaction was successfully started.
 *     false if the channel was busy, the arguments were invalid, or the
 *     transaction could not be started.
 */
bool HW_SPI_Master_Write_Read( SPIPeripheral_T peripheral, const uint8_t* write_data,
                               uint8_t* read_data, uint32_t size )
{
}

/**
 * @brief Determine whether a master transfer is currently active.
 *
 * Returns whether the selected SPI channel is currently busy performing a
 * master-mode transaction previously started by HW_SPI_Master_Write_Read().
 *
 * This function is intended for use by the mid-level driver or execution logic
 * to determine whether a new master transaction may be started.
 *
 * This function is intended for use only on channels configured in master mode.
 *
 * @param peripheral
 *     The SPI peripheral/channel to query.
 *
 * @return
 *     true if a master transfer is currently in progress on the channel.
 *     false if no master transfer is currently active.
 */
bool HW_SPI_Master_Is_Busy( SPIPeripheral_T peripheral )
{
}

/**
 * @brief Determine whether the most recent master transfer has completed.
 *
 * Returns whether the currently active or most recently started master transfer
 * on the selected channel has completed.
 *
 * The exact persistence semantics of the completion state are
 * implementation-defined unless explicitly documented by the driver. A common
 * implementation is that this function returns true after transfer completion
 * until a new transfer is started.
 *
 * This function is intended for use by higher software layers to determine when
 * a previously started master transaction has finished and its RX data may be
 * safely interpreted.
 *
 * This function is intended for use only on channels configured in master mode.
 *
 * @param peripheral
 *     The SPI peripheral/channel to query.
 *
 * @return
 *     true if the most recent master transfer has completed.
 *     false if the transfer is still in progress or no completed transfer is
 *     available according to the implementation's completion-state rules.
 */
bool HW_SPI_Master_Transfer_Complete( SPIPeripheral_T peripheral )
{
}

/**
 * @brief Get the size of the most recently completed master transfer.
 *
 * Returns the number of bytes associated with the most recently completed
 * master-mode write/read transaction on the selected channel.
 *
 * This function is intended to allow higher software layers to confirm how many
 * bytes were transferred in the last completed master transaction.
 *
 * The returned value corresponds to the most recent completed transfer tracked
 * by the low-level driver. If no completed transfer is available, the returned
 * value is implementation-defined unless explicitly documented otherwise.
 *
 * This function is intended for use only on channels configured in master mode.
 *
 * @param peripheral
 *     The SPI peripheral/channel to query.
 *
 * @return
 *     The size, in bytes, of the most recently completed master transfer.
 */
uint32_t HW_SPI_Master_Get_Last_Transfer_Size( SPIPeripheral_T peripheral )
{
}
