/******************************************************************************
 *  File:       hw_spi_config.c
 *  Author:     Angus Corr
 *  Created:    10-Apr-2026
 *
 *  Description:
 *      Configuration and shared-state implementation for the low-level SPI
 *      driver used by the HIL-RIG firmware.
 *
 *      This file owns the private per-peripheral state structures and the
 *      shared helper functions used by the RX and TX implementation files. It
 *      also contains public configuration/stop functions that are not specific
 *      to RX or TX buffering behaviour.
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

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

SPIPeripheralState_T* HW_SPI_Get_State( SPIPeripheral_T peripheral )
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

uint32_t HW_SPI_Get_Frame_Size_Bytes( const SPIPeripheralState_T* peripheral_state )
{
    if ( peripheral_state->config.data_size == SPI_SIZE_16_BIT )
    {
        return 2U;
    }

    return 1U;
}

uint16_t HW_SPI_Bytes_To_DMA_Elements( const SPIPeripheralState_T* peripheral_state,
                                       uint32_t                    size_bytes )
{
    return size_bytes / HW_SPI_Get_Frame_Size_Bytes( peripheral_state );
}

uint32_t HW_SPI_DMA_Elements_To_Bytes( const SPIPeripheralState_T* peripheral_state,
                                       uint32_t                    num_elements )
{
    return num_elements * HW_SPI_Get_Frame_Size_Bytes( peripheral_state );
}

bool HW_SPI_Is_Frame_Aligned_Size( const SPIPeripheralState_T* peripheral_state,
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
    }

    if ( peripheral_state->tx_dma != NULL )
    {
        LL_DMA_SetMemorySize( peripheral_state->tx_dma, peripheral_state->tx_dma_stream,
                              memory_width );
        LL_DMA_SetPeriphSize( peripheral_state->tx_dma, peripheral_state->tx_dma_stream,
                              peripheral_width );
    }
}

/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */

bool HW_SPI_Configure_Channel( SPIPeripheral_T peripheral, HWSPIConfig_T configuration )
{
    SPI_HandleTypeDef*    hspi             = NULL;
    SPIPeripheralState_T* peripheral_state = NULL;
    // Select the HAL handle, SPI instance, DMA streams, and state block for the
    // requested logical SPI channel. The rest of the function can then configure
    // the selected HAL handle generically.
    switch ( peripheral )
    {
        case SPI_CHANNEL_0:
            hspi             = &SPI_CHANNEL_0_HANDLE;
            peripheral_state = channel_0_state;
            hspi->Instance   = SPI_CHANNEL_0_INSTANCE;
            memcpy( &( channel_0_state->config ), &configuration, sizeof( HWSPIConfig_T ) );
            channel_0_state->rx_dma         = SPI_CHANNEL_0_RX_DMA;
            channel_0_state->rx_dma_stream  = SPI_CHANNEL_0_RX_DMA_STREAM;
            channel_0_state->tx_dma         = SPI_CHANNEL_0_TX_DMA;
            channel_0_state->tx_dma_stream  = SPI_CHANNEL_0_TX_DMA_STREAM;
            channel_0_state->spi_peripheral = SPI_CHANNEL_0_INSTANCE;
            channel_0_state->tx_dma_irqn    = SPI_CHANNEL_0_TX_DMA_IRQN;
            break;
        case SPI_CHANNEL_1:
            hspi             = &SPI_CHANNEL_1_HANDLE;
            peripheral_state = channel_1_state;
            hspi->Instance   = SPI_CHANNEL_1_INSTANCE;
            memcpy( &( channel_1_state->config ), &configuration, sizeof( HWSPIConfig_T ) );
            channel_1_state->rx_dma         = SPI_CHANNEL_1_RX_DMA;
            channel_1_state->rx_dma_stream  = SPI_CHANNEL_1_RX_DMA_STREAM;
            channel_1_state->tx_dma         = SPI_CHANNEL_1_TX_DMA;
            channel_1_state->tx_dma_stream  = SPI_CHANNEL_1_TX_DMA_STREAM;
            channel_1_state->spi_peripheral = SPI_CHANNEL_1_INSTANCE;
            channel_1_state->tx_dma_irqn    = SPI_CHANNEL_1_TX_DMA_IRQN;
            break;
        case SPI_DAC:
            hspi             = &SPI_DAC_HANDLE;
            peripheral_state = dac_state;
            hspi->Instance   = SPI_DAC_INSTANCE;
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
            /*
             * Master-mode chip-select is always software controlled by this
             * driver around each DMA-backed master packet. The SPI peripheral
             * must therefore not drive hardware NSS as the transaction framing
             * signal.
             */
            hspi->Init.NSS = SPI_NSS_SOFT;
            break;
        case SPI_SLAVE_MODE:
            hspi->Init.Mode = SPI_MODE_SLAVE;
            hspi->Init.NSS  = SPI_NSS_HARD_INPUT;
            break;
        default:
            return false;
    }

    HW_SPI_TX_Configure_Operations( peripheral_state );
    HW_SPI_TX_Reset_State( peripheral_state );

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
