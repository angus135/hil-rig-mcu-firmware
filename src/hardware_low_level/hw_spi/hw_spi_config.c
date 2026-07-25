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

SPIPeripheralState_T channel_state_array[SPI_NUM_CHANNELS];

static SPI_HandleTypeDef* const SPI_HAL_HANDLE_ARRAY[SPI_NUM_CHANNELS] = {
    [SPI_CHANNEL_0] = &SPI_CHANNEL_0_HANDLE,
    [SPI_CHANNEL_1] = &SPI_CHANNEL_1_HANDLE,
    [SPI_DAC]       = &SPI_DAC_HANDLE,
};

static SPI_TypeDef* const SPI_INSTANCE_ARRAY[SPI_NUM_CHANNELS] = {
    [SPI_CHANNEL_0] = SPI_CHANNEL_0_INSTANCE,
    [SPI_CHANNEL_1] = SPI_CHANNEL_1_INSTANCE,
    [SPI_DAC]       = SPI_DAC_INSTANCE,
};

static DMA_TypeDef* const SPI_RX_DMA_ARRAY[SPI_NUM_CHANNELS] = {
    [SPI_CHANNEL_0] = SPI_CHANNEL_0_RX_DMA,
    [SPI_CHANNEL_1] = SPI_CHANNEL_1_RX_DMA,
    [SPI_DAC]       = NULL,
};

static const uint32_t SPI_RX_DMA_STREAM_ARRAY[SPI_NUM_CHANNELS] = {
    [SPI_CHANNEL_0] = SPI_CHANNEL_0_RX_DMA_STREAM,
    [SPI_CHANNEL_1] = SPI_CHANNEL_1_RX_DMA_STREAM,
    [SPI_DAC]       = 0U,
};

static DMA_TypeDef* const SPI_TX_DMA_ARRAY[SPI_NUM_CHANNELS] = {
    [SPI_CHANNEL_0] = SPI_CHANNEL_0_TX_DMA,
    [SPI_CHANNEL_1] = SPI_CHANNEL_1_TX_DMA,
    [SPI_DAC]       = SPI_DAC_TX_DMA,
};

static const uint32_t SPI_TX_DMA_STREAM_ARRAY[SPI_NUM_CHANNELS] = {
    [SPI_CHANNEL_0] = SPI_CHANNEL_0_TX_DMA_STREAM,
    [SPI_CHANNEL_1] = SPI_CHANNEL_1_TX_DMA_STREAM,
    [SPI_DAC]       = SPI_DAC_TX_DMA_STREAM,
};

static const IRQn_Type SPI_TX_DMA_IRQN_ARRAY[SPI_NUM_CHANNELS] = {
    [SPI_CHANNEL_0] = SPI_CHANNEL_0_TX_DMA_IRQN,
    [SPI_CHANNEL_1] = SPI_CHANNEL_1_TX_DMA_IRQN,
    [SPI_DAC]       = SPI_DAC_TX_DMA_IRQN,
};

static const Timer_T SPI_FINAL__DRAIN_TIMER_ARRAY[SPI_NUM_CHANNELS] = {
    [SPI_CHANNEL_0] = SPI_CHANNEL_0_TIMER,
    [SPI_CHANNEL_1] = SPI_CHANNEL_1_TIMER,
    [SPI_DAC]       = SPI_DAC_TIMER,
};

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

static bool HW_SPI_Config_Is_Valid_NSS( SPIChannel_T         peripheral,
                                        const HWSPIConfig_T* configuration )
{
    if ( configuration == NULL || HW_GPIO_Is_Valid_Pin( configuration->nss_pin ) == false )
    {
        return false;
    }

    if ( configuration->spi_mode == SPI_MASTER_MODE )
    {
        // Any mapped configurable pin may be used as software CS in master mode.
        return true;
    }

    if ( configuration->spi_mode != SPI_SLAVE_MODE )
    {
        return false;
    }

    // Hardware-NSS input is fixed by the MCU alternate-function routing.
    switch ( peripheral )
    {
        case SPI_CHANNEL_0:
            return configuration->nss_pin == GPIO_SPI1_NSS;
        case SPI_CHANNEL_1:
            return configuration->nss_pin == GPIO_SPI2_NSS;
        case SPI_DAC:
            return configuration->nss_pin == GPIO_SPI4_NSS;
        case SPI_NUM_CHANNELS:
        default:
            return false;
    }
}

static bool HW_SPI_Config_Build_HAL_Init( const HWSPIConfig_T* configuration,
                                          SPI_InitTypeDef*     requested_init )
{
    if ( configuration == NULL || requested_init == NULL )
    {
        return false;
    }

    memset( requested_init, 0, sizeof( *requested_init ) );

    switch ( configuration->spi_mode )
    {
        case SPI_MASTER_MODE:
            requested_init->Mode = SPI_MODE_MASTER;
            requested_init->NSS  = SPI_NSS_SOFT;
            break;
        case SPI_SLAVE_MODE:
            requested_init->Mode = SPI_MODE_SLAVE;
            requested_init->NSS  = SPI_NSS_HARD_INPUT;
            break;
        default:
            return false;
    }

    requested_init->Direction = SPI_DIRECTION_2LINES;

    switch ( configuration->data_size )
    {
        case SPI_SIZE_8_BIT:
            requested_init->DataSize = SPI_DATASIZE_8BIT;
            break;
        case SPI_SIZE_16_BIT:
            requested_init->DataSize = SPI_DATASIZE_16BIT;
            break;
        default:
            return false;
    }

    switch ( configuration->cpol )
    {
        case SPI_CPOL_LOW:
            requested_init->CLKPolarity = SPI_POLARITY_LOW;
            break;
        case SPI_CPOL_HIGH:
            requested_init->CLKPolarity = SPI_POLARITY_HIGH;
            break;
        default:
            return false;
    }

    switch ( configuration->cpha )
    {
        case SPI_CPHA_1_EDGE:
            requested_init->CLKPhase = SPI_PHASE_1EDGE;
            break;
        case SPI_CPHA_2_EDGE:
            requested_init->CLKPhase = SPI_PHASE_2EDGE;
            break;
        default:
            return false;
    }

    switch ( configuration->baud_rate )
    {
        case SPI_BAUD_45MBIT:
            requested_init->BaudRatePrescaler = SPI_BAUDRATEPRESCALER_2;
            break;
        case SPI_BAUD_22M5BIT:
            requested_init->BaudRatePrescaler = SPI_BAUDRATEPRESCALER_4;
            break;
        case SPI_BAUD_11M25BIT:
            requested_init->BaudRatePrescaler = SPI_BAUDRATEPRESCALER_8;
            break;
        case SPI_BAUD_5M625BIT:
            requested_init->BaudRatePrescaler = SPI_BAUDRATEPRESCALER_16;
            break;
        case SPI_BAUD_2M813BIT:
            requested_init->BaudRatePrescaler = SPI_BAUDRATEPRESCALER_32;
            break;
        case SPI_BAUD_1M406BIT:
            requested_init->BaudRatePrescaler = SPI_BAUDRATEPRESCALER_64;
            break;
        case SPI_BAUD_703KBIT:
            requested_init->BaudRatePrescaler = SPI_BAUDRATEPRESCALER_128;
            break;
        case SPI_BAUD_352KBIT:
            requested_init->BaudRatePrescaler = SPI_BAUDRATEPRESCALER_256;
            break;
        default:
            return false;
    }

    switch ( configuration->first_bit )
    {
        case SPI_FIRST_MSB:
            requested_init->FirstBit = SPI_FIRSTBIT_MSB;
            break;
        case SPI_FIRST_LSB:
            requested_init->FirstBit = SPI_FIRSTBIT_LSB;
            break;
        default:
            return false;
    }

    requested_init->TIMode         = SPI_TIMODE_DISABLE;
    requested_init->CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    requested_init->CRCPolynomial  = 10U;
    return true;
}

static bool HW_SPI_Config_Channel_Is_Stopped( const SPIPeripheralState_T* peripheral_state )
{
    if ( peripheral_state->is_configured == false )
    {
        return true;
    }

    return peripheral_state->is_started == false && peripheral_state->cs_asserted == false
           && peripheral_state->tx_num_bytes_pending == 0U
           && peripheral_state->tx_num_bytes_in_transmission == 0U
           && peripheral_state->tx_num_packets_pending == 0U
           && peripheral_state->tx_transaction_state == HW_SPI_TX_TRANSACTION_IDLE;
}

static bool HW_SPI_Config_Apply_GPIO( const HWSPIConfig_T* configuration )
{
    if ( configuration->spi_mode == SPI_MASTER_MODE )
    {
        return HW_GPIO_Configure_Pin_As_Output( configuration->nss_pin, true );
    }

    return HW_GPIO_Configure_Pin_As_Alternate_Function( configuration->nss_pin );
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
    peripheral_state->tx_final_drain_timer = SPI_FINAL__DRAIN_TIMER_ARRAY[( uint32_t )peripheral];

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
    if ( HW_SPI_Is_Valid_Channel( peripheral ) == false )
    {
        return NULL;
    }

    return &( channel_state_array[( uint32_t )peripheral] );
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
    SPI_HandleTypeDef*    hspi;
    SPIPeripheralState_T* peripheral_state;
    SPI_InitTypeDef       requested_init;
    SPI_InitTypeDef       previous_init;
    SPI_TypeDef*          previous_instance;
    bool                  had_previous_configuration;

    if ( HW_SPI_Is_Valid_Channel( peripheral ) == false )
    {
        return false;
    }

    peripheral_state = HW_SPI_Get_State_Fast( peripheral );

    // Validate the complete request without touching the HAL handle, GPIO
    // ownership, or stored channel state.
    if ( HW_SPI_Config_Is_Valid_NSS( peripheral, &configuration ) == false
         || HW_SPI_Config_Build_HAL_Init( &configuration, &requested_init ) == false
         || HW_SPI_Config_Channel_Is_Stopped( peripheral_state ) == false )
    {
        return false;
    }

    hspi                       = SPI_HAL_HANDLE_ARRAY[( uint32_t )peripheral];
    previous_init              = hspi->Init;
    previous_instance          = hspi->Instance;
    had_previous_configuration = peripheral_state->is_configured;

    // A stopped master must already be inactive, but explicitly release the old
    // line before its ownership or the selected CS changes.
    if ( had_previous_configuration && peripheral_state->is_master
         && ( configuration.spi_mode != SPI_MASTER_MODE
              || configuration.nss_pin != peripheral_state->nss_pin ) )
    {
        HW_GPIO_Set_Pin( peripheral_state->nss_pin );
    }

    hspi->Instance = SPI_INSTANCE_ARRAY[( uint32_t )peripheral];
    hspi->Init     = requested_init;

    if ( HAL_SPI_Init( hspi ) != HAL_OK )
    {
        hspi->Instance = previous_instance;
        hspi->Init     = previous_init;
        return false;
    }

    // HAL_SPI_Init() is not assumed to rerun MSP setup. Take explicit ownership
    // of NSS after the peripheral policy has been applied.
    if ( HW_SPI_Config_Apply_GPIO( &configuration ) == false )
    {
        hspi->Instance = previous_instance;
        hspi->Init     = previous_init;
        ( void )HAL_SPI_Init( hspi );

        if ( had_previous_configuration )
        {
            ( void )HW_SPI_Config_Apply_GPIO( &peripheral_state->config );
        }
        return false;
    }

    // Commit driver-visible state only after both SPI and GPIO configuration
    // have succeeded.
    memcpy( &( peripheral_state->config ), &configuration, sizeof( HWSPIConfig_T ) );
    peripheral_state->nss_pin       = configuration.nss_pin;
    peripheral_state->is_configured = true;
    peripheral_state->is_started    = false;
    peripheral_state->cs_asserted   = false;

    peripheral_state->rx_dma         = SPI_RX_DMA_ARRAY[( uint32_t )peripheral];
    peripheral_state->rx_dma_stream  = SPI_RX_DMA_STREAM_ARRAY[( uint32_t )peripheral];
    peripheral_state->tx_dma         = SPI_TX_DMA_ARRAY[( uint32_t )peripheral];
    peripheral_state->tx_dma_stream  = SPI_TX_DMA_STREAM_ARRAY[( uint32_t )peripheral];
    peripheral_state->spi_peripheral = SPI_INSTANCE_ARRAY[( uint32_t )peripheral];
    peripheral_state->tx_dma_irqn    = SPI_TX_DMA_IRQN_ARRAY[( uint32_t )peripheral];

    HW_SPI_Config_Precompute_Hot_Fields( peripheral_state, peripheral, configuration );
    HW_SPI_TX_Configure_Timer( peripheral_state );
    HW_SPI_TX_Reset_State( peripheral_state );

    // Ensure DMA memory/peripheral data widths match the configured SPI frame size.
    HW_SPI_Configure_DMA_Data_Widths( peripheral_state );

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
bool HW_SPI_Stop_Channel( SPIChannel_T peripheral )
{
    SPIPeripheralState_T* peripheral_state;
    HAL_StatusTypeDef     stop_status;

    if ( HW_SPI_Is_Valid_Channel( peripheral ) == false )
    {
        return false;
    }

    peripheral_state = HW_SPI_Get_State_Fast( peripheral );
    if ( peripheral_state->is_configured == false )
    {
        return false;
    }

    if ( peripheral_state->is_master
         && peripheral_state->tx_transaction_state == HW_SPI_TX_TRANSACTION_WAIT_FINAL_DRAIN )
    {
        HW_TIMER_Stop_Timer( peripheral_state->tx_final_drain_timer );
    }

    stop_status = HAL_SPI_DMAStop( SPI_HAL_HANDLE_ARRAY[( uint32_t )peripheral] );

    // CS safety is independent of whether HAL could fully stop the DMA path.
    if ( peripheral_state->is_master )
    {
        HW_SPI_TX_Master_CS_Deassert( peripheral_state );
    }

    if ( stop_status != HAL_OK )
    {
        return false;
    }

    HW_SPI_TX_Reset_State( peripheral_state );
    peripheral_state->is_started = false;
    return true;
}
