/******************************************************************************
 *  File:       hw_spi_config.c
 *  Author:     Angus Corr
 *  Created:    10-Apr-2026
 *
 *  Description:
 *      Configuration and shared-state implementation for the low-level SPI
 *      driver used by the HIL-RIG firmware.
 *
 *      This file owns the per-peripheral state structures, logical peripheral
 *      lookup, frame-size conversions, DMA data-width configuration, HAL SPI
 *      initialisation, and channel stop behaviour. It also contains public
 *      configuration/stop functions that are not specific to RX or TX buffering
 *      behaviour.
 *  Notes:
 *      Runtime TX/RX paths intentionally keep validation minimal. Configuration
 *      functions perform setup-time checks; ISR and hot-path functions assume
 *      the selected peripheral has already been configured correctly.
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#define HW_SPI_INTERNAL
#include "hw_spi.h"

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

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

static uint16_t HW_SPI_Config_Get_Cycles_Per_SCK( SPIBaudRate_T baud_rate )
{
    switch ( baud_rate )
    {
        case SPI_BAUD_45MBIT:
            return 4U;
        case SPI_BAUD_22M5BIT:
            return 8U;
        case SPI_BAUD_11M25BIT:
            return 16U;
        case SPI_BAUD_5M625BIT:
            return 32U;
        case SPI_BAUD_2M813BIT:
            return 64U;
        case SPI_BAUD_1M406BIT:
            return 128U;
        case SPI_BAUD_703KBIT:
            return 256U;
        case SPI_BAUD_352KBIT:
        default:
            return 512U;
    }
}

static bool HW_SPI_Config_Uses_Final_Drain_Timer( SPIBaudRate_T baud_rate )
{
    return baud_rate >= SPI_BAUD_2M813BIT;
}

static void HW_SPI_Config_Precompute_Hot_Fields( SPIPeripheralState_T* peripheral_state,
                                                 SPIChannel_T          peripheral,
                                                 HWSPIConfig_T         configuration )
{
    uint16_t frame_bits = 8U;
    // Setting basic channel configurations
    peripheral_state->logical_peripheral = peripheral;
    peripheral_state->is_master          = configuration.spi_mode == SPI_MASTER_MODE;
    peripheral_state->frame_size_bytes   = ( configuration.data_size == SPI_SIZE_16_BIT ) ? 2U : 1U;
    peripheral_state->frame_shift        = ( configuration.data_size == SPI_SIZE_16_BIT ) ? 1U : 0U;
    // Based on the baud rate determines whether to use a timer to wait after dma complete for tx
    peripheral_state->tx_uses_final_drain_timer =
        HW_SPI_Config_Uses_Final_Drain_Timer( configuration.baud_rate );
    switch ( peripheral )
    {
        case SPI_CHANNEL_0:
            peripheral_state->tx_final_drain_timer = SPI_CHANNEL_0_TIMER;
            break;
        case SPI_CHANNEL_1:
            peripheral_state->tx_final_drain_timer = SPI_CHANNEL_1_TIMER;
            break;
        case SPI_DAC:
            peripheral_state->tx_final_drain_timer = SPI_DAC_TIMER;
            break;
        default:
            return;  // TODO: need to return false here since false
    }

    if ( configuration.data_size == SPI_SIZE_16_BIT )
    {
        frame_bits = 16U;
    }

    // Determines number of cycles to check
    peripheral_state->tx_final_drain_cycles =
        ( uint16_t )( ( frame_bits * HW_SPI_Config_Get_Cycles_Per_SCK( configuration.baud_rate ) )
                      + SPI_FINAL_DRAIN_GUARD_CYCLES );
}

/**
 * @brief Return the private state block for a logical SPI peripheral.
 *
 * @param peripheral
 *     Logical SPI channel requested by the caller.
 *
 * @return
 *     Pointer to the matching private state block, or NULL for an invalid
 *     peripheral enum.
 */
SPIPeripheralState_T* HW_SPI_Get_State( SPIChannel_T peripheral )
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
 * @brief Return the configured SPI frame size in bytes.
 *
 * @param peripheral_state
 *     Configured SPI channel state. Runtime callers are expected to pass a
 *     valid state pointer that was initialised by HW_SPI_Configure_Channel().
 *
 * @return
 *     1 for 8-bit SPI frames; 2 for 16-bit SPI frames.
 */
uint32_t HW_SPI_Get_Frame_Size_Bytes( const SPIPeripheralState_T* peripheral_state )
{
    return HW_SPI_Get_Frame_Size_Bytes_Fast( peripheral_state );
}

/**
 * @brief Convert a byte count into a DMA element count for the configured frame size.
 *
 * @param peripheral_state
 *     Configured SPI channel state.
 *
 * @param size_bytes
 *     Number of software-visible bytes to transfer.
 *
 * @return
 *     DMA NDTR element count corresponding to @p size_bytes.
 */
uint16_t HW_SPI_Bytes_To_DMA_Elements( const SPIPeripheralState_T* peripheral_state,
                                       uint32_t                    size_bytes )
{
    return HW_SPI_Bytes_To_DMA_Elements_Fast( peripheral_state, size_bytes );
}

/**
 * @brief Convert a byte count into a DMA element count for the configured frame size.
 *
 * @param peripheral_state
 *     Configured SPI channel state.
 *
 * @param size_bytes
 *     Number of software-visible bytes to transfer.
 *
 * @return
 *     DMA NDTR element count corresponding to @p size_bytes.
 */
uint32_t HW_SPI_DMA_Elements_To_Bytes( const SPIPeripheralState_T* peripheral_state,
                                       uint32_t                    num_elements )
{
    return HW_SPI_DMA_Elements_To_Bytes_Fast( peripheral_state, num_elements );
}

/**
 * @brief Check whether a byte count is aligned to the configured SPI frame size.
 *
 * @param peripheral_state
 *     Configured SPI channel state.
 *
 * @param size_bytes
 *     Byte count supplied by the caller.
 *
 * @return
 *     true if @p size_bytes is legal for the configured frame size; false
 *     otherwise.
 */
bool HW_SPI_Is_Frame_Aligned_Size( const SPIPeripheralState_T* peripheral_state,
                                   uint32_t                    size_bytes )
{
    return HW_SPI_Is_Frame_Aligned_Size_Fast( peripheral_state, size_bytes );
}
/**
 * @brief Configure DMA memory/peripheral data widths to match the SPI frame size.
 *
 * This must match the selected SPI data size. For 8-bit SPI, DMA transfers bytes.
 * For 16-bit SPI, DMA transfers halfwords. This is here to guarantee configuration of DMA for size
 * and alignment that HAL may not do.
 */
void HW_SPI_Configure_DMA_Data_Widths( SPIPeripheralState_T* peripheral_state )
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
        LL_DMA_SetPeriphAddress( peripheral_state->rx_dma, peripheral_state->rx_dma_stream,
                                 LL_SPI_DMA_GetRegAddr( peripheral_state->spi_peripheral ) );
    }

    if ( peripheral_state->tx_dma != NULL )
    {
        LL_DMA_SetMemorySize( peripheral_state->tx_dma, peripheral_state->tx_dma_stream,
                              memory_width );
        LL_DMA_SetPeriphSize( peripheral_state->tx_dma, peripheral_state->tx_dma_stream,
                              peripheral_width );
        LL_DMA_SetPeriphAddress( peripheral_state->tx_dma, peripheral_state->tx_dma_stream,
                                 LL_SPI_DMA_GetRegAddr( peripheral_state->spi_peripheral ) );
        LL_DMA_EnableIT_TC( peripheral_state->tx_dma, peripheral_state->tx_dma_stream );
        LL_DMA_EnableIT_TE( peripheral_state->tx_dma, peripheral_state->tx_dma_stream );
    }
}

/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */

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
bool HW_SPI_Configure_Channel( SPIChannel_T peripheral, HWSPIConfig_T configuration )
{
    SPI_HandleTypeDef*    hspi             = NULL;
    SPIPeripheralState_T* peripheral_state = NULL;
    // Configuring and making sure HAL is configured correctly
    switch ( peripheral )
    {
        case SPI_CHANNEL_0:
            hspi           = &SPI_CHANNEL_0_HANDLE;
            hspi->Instance = SPI_CHANNEL_0_INSTANCE;
            break;
        case SPI_CHANNEL_1:
            hspi           = &SPI_CHANNEL_1_HANDLE;
            hspi->Instance = SPI_CHANNEL_1_INSTANCE;
            break;
        case SPI_DAC:
            hspi           = &SPI_DAC_HANDLE;
            hspi->Instance = SPI_DAC_INSTANCE;
            break;
        default:
            return false;
    }

    // Mode + chip select logic
    switch ( configuration.spi_mode )
    {
        case SPI_MASTER_MODE:
            hspi->Init.Mode = SPI_MODE_MASTER;
            // Master-mode chip-select is always software controlled by this
            // driver around each DMA-backed master packet. The SPI peripheral
            // must therefore not drive hardware NSS as the transaction framing
            // signal.
            hspi->Init.NSS = SPI_NSS_SOFT;
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

    // Select the SPI instance, DMA streams, and state block for the
    // requested logical SPI channel. The rest of the function can then configure state
    switch ( peripheral )
    {
        case SPI_CHANNEL_0:
            peripheral_state = channel_0_state;
            memcpy( &( channel_0_state->config ), &configuration, sizeof( HWSPIConfig_T ) );
            channel_0_state->rx_dma         = SPI_CHANNEL_0_RX_DMA;
            channel_0_state->rx_dma_stream  = SPI_CHANNEL_0_RX_DMA_STREAM;
            channel_0_state->tx_dma         = SPI_CHANNEL_0_TX_DMA;
            channel_0_state->tx_dma_stream  = SPI_CHANNEL_0_TX_DMA_STREAM;
            channel_0_state->spi_peripheral = SPI_CHANNEL_0_INSTANCE;
            channel_0_state->tx_dma_irqn    = SPI_CHANNEL_0_TX_DMA_IRQN;
            break;
        case SPI_CHANNEL_1:
            peripheral_state = channel_1_state;
            memcpy( &( channel_1_state->config ), &configuration, sizeof( HWSPIConfig_T ) );
            channel_1_state->rx_dma         = SPI_CHANNEL_1_RX_DMA;
            channel_1_state->rx_dma_stream  = SPI_CHANNEL_1_RX_DMA_STREAM;
            channel_1_state->tx_dma         = SPI_CHANNEL_1_TX_DMA;
            channel_1_state->tx_dma_stream  = SPI_CHANNEL_1_TX_DMA_STREAM;
            channel_1_state->spi_peripheral = SPI_CHANNEL_1_INSTANCE;
            channel_1_state->tx_dma_irqn    = SPI_CHANNEL_1_TX_DMA_IRQN;
            break;
        case SPI_DAC:
            peripheral_state = dac_state;
            memcpy( &( dac_state->config ), &configuration, sizeof( HWSPIConfig_T ) );
            dac_state->tx_dma         = SPI_DAC_TX_DMA;
            dac_state->tx_dma_stream  = SPI_DAC_TX_DMA_STREAM;
            dac_state->spi_peripheral = SPI_DAC_INSTANCE;
            dac_state->tx_dma_irqn    = SPI_DAC_TX_DMA_IRQN;
            break;
        default:
            return false;
    }

    HW_SPI_Config_Precompute_Hot_Fields( peripheral_state, peripheral, configuration );

    HW_SPI_TX_Configure_Operations( peripheral_state );
    HW_SPI_TX_Reset_State( peripheral_state );

    // Ensure DMA memory/peripheral data widths match the configured SPI frame size.
    // This is essential when switching between 8-bit and 16-bit operation.
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
void HW_SPI_Stop_Channel( SPIChannel_T peripheral )
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
