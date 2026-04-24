/******************************************************************************
 *  File:       hw_i2c.c
 *  Author:     Calllum Rafferty
 *  Created:    25-Mar-2026
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

#ifndef TEST_BUILD
#include "stm32f446xx.h"
#include "stm32f4xx_ll_bus.h"
#include "stm32f4xx_ll_dma.h"
#include "stm32f4xx_ll_i2c.h"
#include "stm32f4xx_ll_fmpi2c.h"
#include "stm32f4xx_it.h"
#endif

#include "hw_i2c.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define HW_I2C_Channel_T HWI2CChannel_T
#define HW_I2C_Mode_T HWI2CMode_T
#define HW_I2C_Speed_T HWI2CSpeed_T
#define HW_I2C_TransferPath_T HWI2CTransferPath_T
#define HW_I2C_Status_T HWI2CStatus_T
#define HW_I2C_ChannelConfig_T HWI2CChannelConfig_T
#define HW_I2C_Span_T HWI2CSpan_T
#define HW_I2C_RxPeek_T HWI2CRxPeek_T
#define HW_I2C_TransferKind_T HWI2CTransferKind_T
#define HW_I2C_ChannelState_T HWI2CChannelState_T
#define HW_I2C_CHANNEL_2_DMA_RX_STREAM DMA1_Stream2
#define HW_I2C_CHANNEL_2_DMA_TX_STREAM DMA1_Stream7

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

typedef enum HWI2CTransferKind_T
{
    HW_I2C_TRANSFER_KIND_IDLE,
    HW_I2C_TRANSFER_KIND_MASTER_TX,
    HW_I2C_TRANSFER_KIND_MASTER_RX,
    HW_I2C_TRANSFER_KIND_SLAVE_TX,
    HW_I2C_TRANSFER_KIND_SLAVE_RX,
} HWI2CTransferKind_T;

typedef struct HWI2CChannelState_T
{
    bool                   configured;
    HW_I2C_ChannelConfig_T config;

    volatile bool            transfer_in_progress;
    volatile HW_I2C_Status_T last_error;
    HW_I2C_TransferKind_T    transfer_kind;

    uint16_t target_address_7bit;
    uint16_t rx_expected_length;

    uint8_t        tx_stage_buffer[HW_I2C_TX_STAGE_SIZE];
    uint16_t       tx_stage_length;
    const uint8_t* tx_ptr;
    uint16_t       tx_remaining;
    volatile bool  dma_tx_transfer_complete;

    uint8_t  dma_rx_linear_buffer[HW_I2C_RX_BUFFER_SIZE];
    uint16_t dma_rx_expected_length;

    uint8_t           rx_ring_buffer[HW_I2C_RX_BUFFER_SIZE];
    volatile uint16_t rx_head;
    volatile uint16_t rx_tail;
} HWI2CChannelState_T;

/**-----------------------------------------------------------------------------
 *  Public (global) and Extern Variables
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Private (static) Variables
 *------------------------------------------------------------------------------
 */

static HW_I2C_ChannelState_T hw_i2c_channel_state[HW_I2C_CHANNEL_COUNT] = { 0 };

#ifndef TEST_BUILD
static const uint32_t HW_I2C_APB1_HZ = 45000000UL;
#endif

/**-----------------------------------------------------------------------------
 *  Private (static) Function Prototypes
 *------------------------------------------------------------------------------
 */

static inline bool     hw_i2c_channel_is_valid( HW_I2C_Channel_T channel );
static inline bool     hw_i2c_is_external_channel( HW_I2C_Channel_T channel );
static inline bool     hw_i2c_config_is_valid( HW_I2C_Channel_T              channel,
                                               const HW_I2C_ChannelConfig_T* config );
static inline uint16_t hw_i2c_ring_count( const HW_I2C_ChannelState_T* state );
static inline uint16_t hw_i2c_ring_free( const HW_I2C_ChannelState_T* state );
static inline void     hw_i2c_ring_push_byte( HW_I2C_ChannelState_T* state, uint8_t data_byte );
static inline HW_I2C_Status_T hw_i2c_validate_active_channel( HW_I2C_Channel_T        channel,
                                                              HW_I2C_ChannelState_T** out_state );

#ifndef TEST_BUILD
static inline I2C_TypeDef*        hw_i2c_channel_to_instance( HW_I2C_Channel_T channel );
static inline DMA_Stream_TypeDef* hw_i2c_channel_to_dma_rx_stream( HW_I2C_Channel_T channel );
static inline DMA_Stream_TypeDef* hw_i2c_channel_to_dma_tx_stream( HW_I2C_Channel_T channel );
static inline uint32_t            hw_i2c_channel_to_dma_channel_bits( HW_I2C_Channel_T channel );
static inline uint32_t            hw_i2c_speed_to_hz( HW_I2C_Speed_T speed );
static inline void                hw_i2c_enable_clock_for_channel( HW_I2C_Channel_T channel );
static inline void                hw_i2c_disable_all_runtime_irq_bits( I2C_TypeDef* i2c_instance );
static inline void hw_i2c_set_speed_and_address( I2C_TypeDef* i2c_instance, HW_I2C_Speed_T speed,
                                                 uint16_t own_address_7bit );
static inline void hw_i2c_prepare_interrupt_path( I2C_TypeDef* i2c_instance );
static inline void hw_i2c_prepare_dma_path( I2C_TypeDef* i2c_instance );
static inline void hw_i2c_start_master_transfer( I2C_TypeDef*          i2c_instance,
                                                 HW_I2C_TransferKind_T transfer_kind,
                                                 bool                  use_dma );
static inline void hw_i2c_finish_transfer( HW_I2C_Channel_T channel, I2C_TypeDef* i2c_instance );
static inline void hw_i2c_abort_transfer( HW_I2C_Channel_T channel, I2C_TypeDef* i2c_instance,
                                          HW_I2C_Status_T error_status );
static void        hw_i2c_configure_dma_stream( DMA_Stream_TypeDef* stream, uint32_t channel_bits,
                                                bool memory_to_peripheral, uint32_t peripheral_address,
                                                uint32_t memory_address, uint16_t length );
static inline bool hw_i2c_dma_stream_has_tc( DMA_Stream_TypeDef* stream );
static inline bool hw_i2c_dma_stream_has_te( DMA_Stream_TypeDef* stream );
static inline void hw_i2c_dma_stream_clear_flags( DMA_Stream_TypeDef* stream );
static inline void hw_i2c_service_event_external( HW_I2C_Channel_T channel,
                                                  I2C_TypeDef*     i2c_instance );
static inline void hw_i2c_service_event_fmpi2c1( HW_I2C_ChannelState_T* state );
#endif

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

static inline bool hw_i2c_channel_is_valid( HW_I2C_Channel_T channel )
{
    return ( channel >= HW_I2C_CHANNEL_1 ) && ( channel < HW_I2C_CHANNEL_COUNT );
}

static inline bool hw_i2c_is_external_channel( HW_I2C_Channel_T channel )
{
    return ( channel == HW_I2C_CHANNEL_1 ) || ( channel == HW_I2C_CHANNEL_2 );
}

static inline bool hw_i2c_config_is_valid( HW_I2C_Channel_T              channel,
                                           const HW_I2C_ChannelConfig_T* config )
{
    if ( config == NULL )
    {
        return false;
    }

    if ( ( config->mode != HW_I2C_MODE_MASTER ) && ( config->mode != HW_I2C_MODE_SLAVE ) )
    {
        return false;
    }

    if ( ( config->speed != HW_I2C_SPEED_100KHZ ) && ( config->speed != HW_I2C_SPEED_400KHZ ) )
    {
        return false;
    }

    if ( ( config->tx_transfer_path != HW_I2C_TRANSFER_INTERRUPT )
         && ( config->tx_transfer_path != HW_I2C_TRANSFER_DMA ) )
    {
        return false;
    }

    if ( ( config->rx_transfer_path != HW_I2C_TRANSFER_INTERRUPT )
         && ( config->rx_transfer_path != HW_I2C_TRANSFER_DMA ) )
    {
        return false;
    }

    if ( config->own_address_7bit > 0x7FU )
    {
        return false;
    }

    if ( channel == HW_I2C_CHANNEL_1 )
    {
        if ( ( config->tx_transfer_path == HW_I2C_TRANSFER_DMA )
             || ( config->rx_transfer_path == HW_I2C_TRANSFER_DMA ) )
        {
            return false;
        }
    }

    return true;
}

static inline uint16_t hw_i2c_ring_count( const HW_I2C_ChannelState_T* state )
{
    if ( state->rx_head >= state->rx_tail )
    {
        return ( uint16_t )( state->rx_head - state->rx_tail );
    }

    return ( uint16_t )( HW_I2C_RX_BUFFER_SIZE - ( state->rx_tail - state->rx_head ) );
}

static inline uint16_t hw_i2c_ring_free( const HW_I2C_ChannelState_T* state )
{
    return ( uint16_t )( ( HW_I2C_RX_BUFFER_SIZE - 1U ) - hw_i2c_ring_count( state ) );
}

static inline void hw_i2c_ring_push_byte( HW_I2C_ChannelState_T* state, uint8_t data_byte )
{
    if ( hw_i2c_ring_free( state ) == 0U )
    {
        state->last_error = HW_I2C_STATUS_OVERFLOW;
        return;
    }

    state->rx_ring_buffer[state->rx_head] = data_byte;
    state->rx_head = ( uint16_t )( ( state->rx_head + 1U ) % HW_I2C_RX_BUFFER_SIZE );
}

static inline HW_I2C_Status_T hw_i2c_validate_active_channel( HW_I2C_Channel_T        channel,
                                                              HW_I2C_ChannelState_T** out_state )
{
    if ( !hw_i2c_channel_is_valid( channel ) || ( out_state == NULL ) )
    {
        return HW_I2C_STATUS_INVALID_PARAM;
    }

    HW_I2C_ChannelState_T* state = &hw_i2c_channel_state[channel];
    if ( !state->configured )
    {
        return HW_I2C_STATUS_NOT_CONFIGURED;
    }

    *out_state = state;
    return HW_I2C_STATUS_OK;
}

#ifndef TEST_BUILD
static inline I2C_TypeDef* hw_i2c_channel_to_instance( HW_I2C_Channel_T channel )
{
    switch ( channel )
    {
        case HW_I2C_CHANNEL_1:
            return I2C1;
        case HW_I2C_CHANNEL_2:
            return I2C2;
        default:
            return NULL;
    }
}

static inline DMA_Stream_TypeDef* hw_i2c_channel_to_dma_rx_stream( HW_I2C_Channel_T channel )
{
    switch ( channel )
    {
        case HW_I2C_CHANNEL_2:
            return HW_I2C_CHANNEL_2_DMA_RX_STREAM;
        default:
            return NULL;
    }
}

static inline DMA_Stream_TypeDef* hw_i2c_channel_to_dma_tx_stream( HW_I2C_Channel_T channel )
{
    switch ( channel )
    {
        case HW_I2C_CHANNEL_2:
            return HW_I2C_CHANNEL_2_DMA_TX_STREAM;
        default:
            return NULL;
    }
}

static inline uint32_t hw_i2c_channel_to_dma_channel_bits( HW_I2C_Channel_T channel )
{
    switch ( channel )
    {
        case HW_I2C_CHANNEL_2:
            return ( 7UL << DMA_SxCR_CHSEL_Pos );
        default:
            return 0UL;
    }
}

static inline uint32_t hw_i2c_speed_to_hz( HW_I2C_Speed_T speed )
{
    return ( speed == HW_I2C_SPEED_400KHZ ) ? 400000UL : 100000UL;
}

static inline void hw_i2c_enable_clock_for_channel( HW_I2C_Channel_T channel )
{
    switch ( channel )
    {
        case HW_I2C_CHANNEL_1:
            LL_APB1_GRP1_EnableClock( LL_APB1_GRP1_PERIPH_I2C1 );
            break;
        case HW_I2C_CHANNEL_2:
            LL_APB1_GRP1_EnableClock( LL_APB1_GRP1_PERIPH_I2C2 );
            break;
        case HW_I2C_CHANNEL_FMPI2C1:
            LL_APB1_GRP1_EnableClock( LL_APB1_GRP1_PERIPH_FMPI2C1 );
            break;
        default:
            break;
    }
}

static inline void hw_i2c_disable_all_runtime_irq_bits( I2C_TypeDef* i2c_instance )
{
    i2c_instance->CR2 &= ~( I2C_CR2_ITERREN | I2C_CR2_ITEVTEN | I2C_CR2_ITBUFEN );
}

static inline void hw_i2c_set_speed_and_address( I2C_TypeDef* i2c_instance, HW_I2C_Speed_T speed,
                                                 uint16_t own_address_7bit )
{
    const uint32_t pclk_mhz       = HW_I2C_APB1_HZ / 1000000UL;
    const uint32_t speed_hz       = hw_i2c_speed_to_hz( speed );
    uint32_t       ccr_register   = 0UL;
    uint32_t       trise_register = 0UL;

    i2c_instance->CR1 &= ~I2C_CR1_PE;

    i2c_instance->CR2 &= ~I2C_CR2_FREQ;
    i2c_instance->CR2 |= ( pclk_mhz & I2C_CR2_FREQ );

    if ( speed == HW_I2C_SPEED_400KHZ )
    {
        uint32_t ccr_value = HW_I2C_APB1_HZ / ( speed_hz * 3UL );
        if ( ccr_value < 1UL )
        {
            ccr_value = 1UL;
        }
        ccr_register   = I2C_CCR_FS | ( ccr_value & I2C_CCR_CCR );
        trise_register = ( ( pclk_mhz * 300UL ) / 1000UL ) + 1UL;
    }
    else
    {
        uint32_t ccr_value = HW_I2C_APB1_HZ / ( speed_hz * 2UL );
        if ( ccr_value < 4UL )
        {
            ccr_value = 4UL;
        }
        ccr_register   = ( ccr_value & I2C_CCR_CCR );
        trise_register = pclk_mhz + 1UL;
    }

    i2c_instance->CCR   = ccr_register;
    i2c_instance->TRISE = ( trise_register & I2C_TRISE_TRISE );
    i2c_instance->OAR1  = I2C_OAR1_ADDMODE | ( ( uint32_t )own_address_7bit << 1U );

    i2c_instance->CR1 |= I2C_CR1_PE;
}

static inline void hw_i2c_prepare_interrupt_path( I2C_TypeDef* i2c_instance )
{
    i2c_instance->CR2 &= ~I2C_CR2_DMAEN;
    i2c_instance->CR2 |= ( I2C_CR2_ITERREN | I2C_CR2_ITEVTEN | I2C_CR2_ITBUFEN );
}

static inline void hw_i2c_prepare_dma_path( I2C_TypeDef* i2c_instance )
{
    i2c_instance->CR2 |= ( I2C_CR2_ITERREN | I2C_CR2_ITEVTEN );
    i2c_instance->CR2 &= ~I2C_CR2_ITBUFEN;
    i2c_instance->CR2 |= I2C_CR2_DMAEN;
}

static inline void hw_i2c_start_master_transfer( I2C_TypeDef*          i2c_instance,
                                                 HW_I2C_TransferKind_T transfer_kind, bool use_dma )
{
    if ( transfer_kind == HW_I2C_TRANSFER_KIND_MASTER_RX )
    {
        i2c_instance->CR1 |= I2C_CR1_ACK;
    }

    if ( use_dma )
    {
        hw_i2c_prepare_dma_path( i2c_instance );
    }
    else
    {
        hw_i2c_prepare_interrupt_path( i2c_instance );
    }

    i2c_instance->CR1 |= I2C_CR1_START;
}

static inline void hw_i2c_finish_transfer( HW_I2C_Channel_T channel, I2C_TypeDef* i2c_instance )
{
    HW_I2C_ChannelState_T* state = &hw_i2c_channel_state[channel];

    if ( state->transfer_kind == HW_I2C_TRANSFER_KIND_MASTER_TX
         || state->transfer_kind == HW_I2C_TRANSFER_KIND_MASTER_RX )
    {
        i2c_instance->CR1 |= I2C_CR1_STOP;
    }

    if ( state->config.tx_transfer_path == HW_I2C_TRANSFER_DMA
         || state->config.rx_transfer_path == HW_I2C_TRANSFER_DMA )
    {
        DMA_Stream_TypeDef* rx_stream = hw_i2c_channel_to_dma_rx_stream( channel );
        DMA_Stream_TypeDef* tx_stream = hw_i2c_channel_to_dma_tx_stream( channel );
        if ( rx_stream != NULL )
        {
            rx_stream->CR &= ~DMA_SxCR_EN;
        }
        if ( tx_stream != NULL )
        {
            tx_stream->CR &= ~DMA_SxCR_EN;
        }
        i2c_instance->CR2 &= ~I2C_CR2_DMAEN;
    }

    hw_i2c_disable_all_runtime_irq_bits( i2c_instance );
    state->transfer_in_progress     = false;
    state->transfer_kind            = HW_I2C_TRANSFER_KIND_IDLE;
    state->tx_remaining             = 0U;
    state->dma_tx_transfer_complete = false;
    state->rx_expected_length       = 0U;
}

static inline void hw_i2c_abort_transfer( HW_I2C_Channel_T channel, I2C_TypeDef* i2c_instance,
                                          HW_I2C_Status_T error_status )
{
    hw_i2c_channel_state[channel].last_error = error_status;
    hw_i2c_finish_transfer( channel, i2c_instance );
}

static void hw_i2c_configure_dma_stream( DMA_Stream_TypeDef* stream, uint32_t channel_bits,
                                         bool memory_to_peripheral, uint32_t peripheral_address,
                                         uint32_t memory_address, uint16_t length )
{
    stream->CR &= ~DMA_SxCR_EN;
    while ( ( stream->CR & DMA_SxCR_EN ) != 0U )
    {
    }

    stream->CR   = 0U;
    stream->FCR  = 0U;
    stream->PAR  = peripheral_address;
    stream->M0AR = memory_address;
    stream->NDTR = ( uint32_t )length;

    uint32_t direction_bits = memory_to_peripheral ? DMA_SxCR_DIR_0 : 0U;
    stream->CR = channel_bits | direction_bits | DMA_SxCR_MINC | DMA_SxCR_TCIE | DMA_SxCR_TEIE;
    stream->CR |= DMA_SxCR_EN;
}

static inline bool hw_i2c_dma_stream_has_tc( DMA_Stream_TypeDef* stream )
{
    if ( stream == HW_I2C_CHANNEL_2_DMA_RX_STREAM )
    {
        return ( ( DMA1->LISR & DMA_LISR_TCIF2 ) != 0U );
    }
    if ( stream == HW_I2C_CHANNEL_2_DMA_TX_STREAM )
    {
        return ( ( DMA1->HISR & DMA_HISR_TCIF7 ) != 0U );
    }
    return false;
}

static inline bool hw_i2c_dma_stream_has_te( DMA_Stream_TypeDef* stream )
{
    if ( stream == HW_I2C_CHANNEL_2_DMA_RX_STREAM )
    {
        return ( ( DMA1->LISR & DMA_LISR_TEIF2 ) != 0U );
    }
    if ( stream == HW_I2C_CHANNEL_2_DMA_TX_STREAM )
    {
        return ( ( DMA1->HISR & DMA_HISR_TEIF7 ) != 0U );
    }
    return false;
}

static inline void hw_i2c_dma_stream_clear_flags( DMA_Stream_TypeDef* stream )
{
    if ( stream == HW_I2C_CHANNEL_2_DMA_RX_STREAM )
    {
        DMA1->LIFCR = DMA_LIFCR_CTCIF2 | DMA_LIFCR_CTEIF2 | DMA_LIFCR_CDMEIF2 | DMA_LIFCR_CFEIF2;
    }
    else if ( stream == HW_I2C_CHANNEL_2_DMA_TX_STREAM )
    {
        DMA1->HIFCR = DMA_HIFCR_CTCIF7 | DMA_HIFCR_CTEIF7 | DMA_HIFCR_CDMEIF7 | DMA_HIFCR_CFEIF7;
    }
}

static inline void hw_i2c_service_event_external( HW_I2C_Channel_T channel,
                                                  I2C_TypeDef*     i2c_instance )
{
    HW_I2C_ChannelState_T* state = &hw_i2c_channel_state[channel];
    uint32_t               sr1   = i2c_instance->SR1;

    if ( ( sr1 & I2C_SR1_SB ) != 0U )
    {
        const uint8_t direction_bit =
            ( state->transfer_kind == HW_I2C_TRANSFER_KIND_MASTER_RX ) ? 1U : 0U;
        i2c_instance->DR =
            ( uint32_t )( ( uint8_t )( ( state->target_address_7bit << 1U ) | direction_bit ) );
        return;
    }

    if ( ( sr1 & I2C_SR1_ADDR ) != 0U )
    {
        volatile uint32_t clear_addr = i2c_instance->SR1;
        clear_addr                   = i2c_instance->SR2;
        ( void )clear_addr;

        if ( state->transfer_kind == HW_I2C_TRANSFER_KIND_MASTER_RX )
        {
            if ( state->rx_expected_length <= 1U )
            {
                i2c_instance->CR1 &= ~I2C_CR1_ACK;
            }
        }
        return;
    }

    if ( state->transfer_kind == HW_I2C_TRANSFER_KIND_MASTER_TX
         || state->transfer_kind == HW_I2C_TRANSFER_KIND_SLAVE_TX )
    {
        bool tx_dma_mode = ( state->config.tx_transfer_path == HW_I2C_TRANSFER_DMA );

        if ( !tx_dma_mode && ( ( sr1 & I2C_SR1_TXE ) != 0U ) )
        {
            if ( state->tx_remaining > 0U )
            {
                i2c_instance->DR = ( uint32_t )( *state->tx_ptr );
                state->tx_ptr++;
                state->tx_remaining--;
            }
            else if ( state->transfer_kind == HW_I2C_TRANSFER_KIND_SLAVE_TX )
            {
                i2c_instance->DR = 0xFFU;
            }
        }

        if ( ( state->transfer_kind == HW_I2C_TRANSFER_KIND_MASTER_TX )
             && ( state->tx_remaining == 0U )
             && ( ( !tx_dma_mode ) || state->dma_tx_transfer_complete )
             && ( ( sr1 & I2C_SR1_BTF ) != 0U ) )
        {
            hw_i2c_finish_transfer( channel, i2c_instance );
            return;
        }
    }

    if ( state->transfer_kind == HW_I2C_TRANSFER_KIND_MASTER_RX
         || state->transfer_kind == HW_I2C_TRANSFER_KIND_SLAVE_RX )
    {
        if ( ( sr1 & I2C_SR1_RXNE ) != 0U )
        {
            uint8_t data_byte = ( uint8_t )i2c_instance->DR;
            hw_i2c_ring_push_byte( state, data_byte );

            if ( state->rx_expected_length > 0U )
            {
                state->rx_expected_length--;
            }

            if ( ( state->transfer_kind == HW_I2C_TRANSFER_KIND_MASTER_RX )
                 && ( state->rx_expected_length <= 1U ) )
            {
                i2c_instance->CR1 &= ~I2C_CR1_ACK;
                i2c_instance->CR1 |= I2C_CR1_STOP;
            }

            if ( state->rx_expected_length == 0U )
            {
                hw_i2c_finish_transfer( channel, i2c_instance );
                return;
            }
        }
    }

    if ( ( sr1 & I2C_SR1_STOPF ) != 0U )
    {
        volatile uint32_t clear_stop = i2c_instance->SR1;
        i2c_instance->CR1 |= I2C_CR1_PE;
        ( void )clear_stop;
        hw_i2c_finish_transfer( channel, i2c_instance );
    }
}

static inline void hw_i2c_service_event_fmpi2c1( HW_I2C_ChannelState_T* state )
{
    uint32_t isr = FMPI2C1->ISR;

    if ( ( isr & FMPI2C_ISR_TXIS ) != 0U )
    {
        if ( state->tx_remaining > 0U )
        {
            FMPI2C1->TXDR = *state->tx_ptr;
            state->tx_ptr++;
            state->tx_remaining--;
        }
    }

    if ( ( isr & FMPI2C_ISR_RXNE ) != 0U )
    {
        uint8_t data_byte = ( uint8_t )FMPI2C1->RXDR;
        hw_i2c_ring_push_byte( state, data_byte );
        if ( state->rx_expected_length > 0U )
        {
            state->rx_expected_length--;
        }
    }

    if ( ( isr & FMPI2C_ISR_STOPF ) != 0U )
    {
        FMPI2C1->ICR                = FMPI2C_ICR_STOPCF;
        state->transfer_in_progress = false;
        state->transfer_kind        = HW_I2C_TRANSFER_KIND_IDLE;
        return;
    }

    if ( ( isr & FMPI2C_ISR_TC ) != 0U )
    {
        FMPI2C1->CR2 |= FMPI2C_CR2_STOP;
    }

    if ( ( isr & FMPI2C_ISR_NACKF ) != 0U )
    {
        FMPI2C1->ICR                = FMPI2C_ICR_NACKCF;
        state->last_error           = HW_I2C_STATUS_ERROR;
        state->transfer_in_progress = false;
        state->transfer_kind        = HW_I2C_TRANSFER_KIND_IDLE;
    }

    if ( ( isr & ( FMPI2C_ISR_BERR | FMPI2C_ISR_ARLO | FMPI2C_ISR_OVR | FMPI2C_ISR_TIMEOUT ) )
         != 0U )
    {
        FMPI2C1->ICR =
            FMPI2C_ICR_BERRCF | FMPI2C_ICR_ARLOCF | FMPI2C_ICR_OVRCF | FMPI2C_ICR_TIMOUTCF;
        state->last_error           = HW_I2C_STATUS_ERROR;
        state->transfer_in_progress = false;
        state->transfer_kind        = HW_I2C_TRANSFER_KIND_IDLE;
    }
}
#endif

/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */

HW_I2C_Status_T HW_I2C_Configure_Channel( HW_I2C_Channel_T              channel,
                                          const HW_I2C_ChannelConfig_T* config )
{
    if ( !hw_i2c_is_external_channel( channel ) || !hw_i2c_config_is_valid( channel, config ) )
    {
        return HW_I2C_STATUS_INVALID_PARAM;
    }

    HW_I2C_ChannelState_T* state    = &hw_i2c_channel_state[channel];
    state->config                   = *config;
    state->configured               = true;
    state->transfer_in_progress     = false;
    state->last_error               = HW_I2C_STATUS_OK;
    state->transfer_kind            = HW_I2C_TRANSFER_KIND_IDLE;
    state->tx_stage_length          = 0U;
    state->tx_remaining             = 0U;
    state->dma_tx_transfer_complete = false;
    state->rx_expected_length       = 0U;
    state->dma_rx_expected_length   = 0U;
    state->rx_head                  = 0U;
    state->rx_tail                  = 0U;

#ifndef TEST_BUILD
    I2C_TypeDef* i2c_instance = hw_i2c_channel_to_instance( channel );
    if ( i2c_instance == NULL )
    {
        return HW_I2C_STATUS_INVALID_PARAM;
    }

    hw_i2c_enable_clock_for_channel( channel );
    hw_i2c_set_speed_and_address( i2c_instance, config->speed, config->own_address_7bit );

    if ( config->mode == HW_I2C_MODE_SLAVE )
    {
        i2c_instance->CR1 |= I2C_CR1_ACK;
    }
    else
    {
        i2c_instance->CR1 &= ~I2C_CR1_ACK;
    }

    if ( config->tx_transfer_path == HW_I2C_TRANSFER_DMA
         || config->rx_transfer_path == HW_I2C_TRANSFER_DMA )
    {
        i2c_instance->CR2 |= I2C_CR2_DMAEN;
    }
    else
    {
        i2c_instance->CR2 &= ~I2C_CR2_DMAEN;
    }

    hw_i2c_disable_all_runtime_irq_bits( i2c_instance );
#endif

    return HW_I2C_STATUS_OK;
}

HW_I2C_Status_T HW_I2C_Configure_Internal_FMPI2C1( uint16_t own_address_7bit )
{
    if ( own_address_7bit > 0x7FU )
    {
        return HW_I2C_STATUS_INVALID_PARAM;
    }

    HW_I2C_ChannelState_T* state    = &hw_i2c_channel_state[HW_I2C_CHANNEL_FMPI2C1];
    state->configured               = true;
    state->transfer_in_progress     = false;
    state->last_error               = HW_I2C_STATUS_OK;
    state->transfer_kind            = HW_I2C_TRANSFER_KIND_IDLE;
    state->tx_stage_length          = 0U;
    state->tx_remaining             = 0U;
    state->dma_tx_transfer_complete = false;
    state->rx_expected_length       = 0U;
    state->dma_rx_expected_length   = 0U;
    state->rx_head                  = 0U;
    state->rx_tail                  = 0U;
    state->config.mode              = HW_I2C_MODE_MASTER;
    state->config.speed             = HW_I2C_SPEED_100KHZ;
    state->config.tx_transfer_path  = HW_I2C_TRANSFER_INTERRUPT;
    state->config.rx_transfer_path  = HW_I2C_TRANSFER_INTERRUPT;
    state->config.own_address_7bit  = own_address_7bit;

#ifndef TEST_BUILD
    hw_i2c_enable_clock_for_channel( HW_I2C_CHANNEL_FMPI2C1 );

    FMPI2C1->CR1 &= ~FMPI2C_CR1_PE;
    FMPI2C1->TIMINGR = 0xC0000E12U;
    FMPI2C1->OAR1    = ( ( uint32_t )own_address_7bit << 1U ) | FMPI2C_OAR1_OA1EN;
    FMPI2C1->CR1     = FMPI2C_CR1_PE | FMPI2C_CR1_TXIE | FMPI2C_CR1_RXIE | FMPI2C_CR1_TCIE
                   | FMPI2C_CR1_STOPIE | FMPI2C_CR1_ERRIE;
#endif

    return HW_I2C_STATUS_OK;
}

HW_I2C_Status_T HW_I2C_Load_Stage_Buffer( HW_I2C_Channel_T channel, const uint8_t* data,
                                          uint16_t length )
{
    HW_I2C_ChannelState_T* state  = NULL;
    HW_I2C_Status_T        status = hw_i2c_validate_active_channel( channel, &state );
    if ( status != HW_I2C_STATUS_OK )
    {
        return status;
    }

    if ( ( data == NULL ) || ( length == 0U ) || ( length > HW_I2C_TX_STAGE_SIZE ) )
    {
        return HW_I2C_STATUS_INVALID_PARAM;
    }

    if ( state->transfer_in_progress )
    {
        return HW_I2C_STATUS_BUSY;
    }

    for ( uint16_t idx = 0U; idx < length; ++idx )
    {
        state->tx_stage_buffer[idx] = data[idx];
    }

    state->tx_stage_length = length;
    return HW_I2C_STATUS_OK;
}

HW_I2C_Status_T HW_I2C_Trigger_Master_Transmit( HW_I2C_Channel_T channel,
                                                uint16_t         device_address_7bit )
{
    HW_I2C_ChannelState_T* state  = NULL;
    HW_I2C_Status_T        status = hw_i2c_validate_active_channel( channel, &state );
    if ( status != HW_I2C_STATUS_OK )
    {
        return status;
    }

    if ( ( state->config.mode != HW_I2C_MODE_MASTER ) || ( device_address_7bit > 0x7FU )
         || ( state->tx_stage_length == 0U ) )
    {
        return HW_I2C_STATUS_INVALID_PARAM;
    }

    if ( state->transfer_in_progress )
    {
        return HW_I2C_STATUS_BUSY;
    }

    state->target_address_7bit      = device_address_7bit;
    state->transfer_kind            = HW_I2C_TRANSFER_KIND_MASTER_TX;
    state->transfer_in_progress     = true;
    state->last_error               = HW_I2C_STATUS_OK;
    state->tx_ptr                   = state->tx_stage_buffer;
    state->tx_remaining             = state->tx_stage_length;
    state->dma_tx_transfer_complete = false;
    state->rx_expected_length       = 0U;

#ifndef TEST_BUILD
    if ( hw_i2c_is_external_channel( channel ) )
    {
        I2C_TypeDef* i2c_instance = hw_i2c_channel_to_instance( channel );
        bool         use_dma      = ( state->config.tx_transfer_path == HW_I2C_TRANSFER_DMA );

        if ( use_dma )
        {
            DMA_Stream_TypeDef* tx_stream = hw_i2c_channel_to_dma_tx_stream( channel );
            if ( tx_stream == NULL )
            {
                return HW_I2C_STATUS_ERROR;
            }

            hw_i2c_dma_stream_clear_flags( tx_stream );
            hw_i2c_configure_dma_stream( tx_stream, hw_i2c_channel_to_dma_channel_bits( channel ),
                                         true, ( uint32_t )&i2c_instance->DR,
                                         ( uint32_t )state->tx_stage_buffer,
                                         state->tx_stage_length );
        }

        hw_i2c_start_master_transfer( i2c_instance, state->transfer_kind, use_dma );
    }
    else
    {
        FMPI2C1->CR2 = ( ( uint32_t )device_address_7bit << 1U )
                       | ( ( uint32_t )state->tx_stage_length << FMPI2C_CR2_NBYTES_Pos )
                       | FMPI2C_CR2_START | FMPI2C_CR2_AUTOEND;
    }
#else
    state->transfer_in_progress = false;
    state->transfer_kind        = HW_I2C_TRANSFER_KIND_IDLE;
    state->tx_remaining         = 0U;
#endif

    return HW_I2C_STATUS_OK;
}

HW_I2C_Status_T HW_I2C_Trigger_Master_Receive( HW_I2C_Channel_T channel,
                                               uint16_t         device_address_7bit,
                                               uint16_t         expected_length )
{
    HW_I2C_ChannelState_T* state  = NULL;
    HW_I2C_Status_T        status = hw_i2c_validate_active_channel( channel, &state );
    if ( status != HW_I2C_STATUS_OK )
    {
        return status;
    }

    if ( ( state->config.mode != HW_I2C_MODE_MASTER ) || ( device_address_7bit > 0x7FU )
         || ( expected_length == 0U ) || ( expected_length > HW_I2C_RX_BUFFER_SIZE ) )
    {
        return HW_I2C_STATUS_INVALID_PARAM;
    }

    if ( state->transfer_in_progress )
    {
        return HW_I2C_STATUS_BUSY;
    }

    state->target_address_7bit    = device_address_7bit;
    state->transfer_kind          = HW_I2C_TRANSFER_KIND_MASTER_RX;
    state->transfer_in_progress   = true;
    state->last_error             = HW_I2C_STATUS_OK;
    state->rx_expected_length     = expected_length;
    state->dma_rx_expected_length = expected_length;

#ifndef TEST_BUILD
    if ( hw_i2c_is_external_channel( channel ) )
    {
        I2C_TypeDef* i2c_instance = hw_i2c_channel_to_instance( channel );
        bool         use_dma      = ( state->config.rx_transfer_path == HW_I2C_TRANSFER_DMA );

        if ( use_dma )
        {
            DMA_Stream_TypeDef* rx_stream = hw_i2c_channel_to_dma_rx_stream( channel );
            if ( rx_stream == NULL )
            {
                return HW_I2C_STATUS_ERROR;
            }

            hw_i2c_dma_stream_clear_flags( rx_stream );
            hw_i2c_configure_dma_stream( rx_stream, hw_i2c_channel_to_dma_channel_bits( channel ),
                                         false, ( uint32_t )&i2c_instance->DR,
                                         ( uint32_t )state->dma_rx_linear_buffer, expected_length );
        }

        hw_i2c_start_master_transfer( i2c_instance, state->transfer_kind, use_dma );
    }
    else
    {
        FMPI2C1->CR2 = ( ( uint32_t )device_address_7bit << 1U ) | FMPI2C_CR2_RD_WRN
                       | ( ( uint32_t )expected_length << FMPI2C_CR2_NBYTES_Pos ) | FMPI2C_CR2_START
                       | FMPI2C_CR2_AUTOEND;
    }
#else
    for ( uint16_t idx = 0U; idx < expected_length; ++idx )
    {
        hw_i2c_ring_push_byte( state, 0x00U );
    }
    state->transfer_in_progress = false;
    state->transfer_kind        = HW_I2C_TRANSFER_KIND_IDLE;
    state->rx_expected_length   = 0U;
#endif

    return HW_I2C_STATUS_OK;
}

HW_I2C_Status_T HW_I2C_Trigger_Slave_Transmit( HW_I2C_Channel_T channel )
{
    HW_I2C_ChannelState_T* state  = NULL;
    HW_I2C_Status_T        status = hw_i2c_validate_active_channel( channel, &state );
    if ( status != HW_I2C_STATUS_OK )
    {
        return status;
    }

    if ( ( state->config.mode != HW_I2C_MODE_SLAVE ) || ( state->tx_stage_length == 0U ) )
    {
        return HW_I2C_STATUS_INVALID_PARAM;
    }

    if ( state->transfer_in_progress )
    {
        return HW_I2C_STATUS_BUSY;
    }

    state->transfer_kind            = HW_I2C_TRANSFER_KIND_SLAVE_TX;
    state->transfer_in_progress     = true;
    state->last_error               = HW_I2C_STATUS_OK;
    state->tx_ptr                   = state->tx_stage_buffer;
    state->tx_remaining             = state->tx_stage_length;
    state->dma_tx_transfer_complete = false;

#ifndef TEST_BUILD
    if ( hw_i2c_is_external_channel( channel ) )
    {
        I2C_TypeDef* i2c_instance = hw_i2c_channel_to_instance( channel );

        if ( state->config.tx_transfer_path == HW_I2C_TRANSFER_DMA )
        {
            DMA_Stream_TypeDef* tx_stream = hw_i2c_channel_to_dma_tx_stream( channel );
            if ( tx_stream == NULL )
            {
                return HW_I2C_STATUS_ERROR;
            }

            hw_i2c_dma_stream_clear_flags( tx_stream );
            hw_i2c_configure_dma_stream( tx_stream, hw_i2c_channel_to_dma_channel_bits( channel ),
                                         true, ( uint32_t )&i2c_instance->DR,
                                         ( uint32_t )state->tx_stage_buffer,
                                         state->tx_stage_length );
            hw_i2c_prepare_dma_path( i2c_instance );
        }
        else
        {
            hw_i2c_prepare_interrupt_path( i2c_instance );
        }

        i2c_instance->CR1 |= I2C_CR1_ACK;
    }
#else
    state->transfer_in_progress = false;
    state->transfer_kind        = HW_I2C_TRANSFER_KIND_IDLE;
    state->tx_remaining         = 0U;
#endif

    return HW_I2C_STATUS_OK;
}

HW_I2C_Status_T HW_I2C_Trigger_Slave_Receive( HW_I2C_Channel_T channel, uint16_t expected_length )
{
    HW_I2C_ChannelState_T* state  = NULL;
    HW_I2C_Status_T        status = hw_i2c_validate_active_channel( channel, &state );
    if ( status != HW_I2C_STATUS_OK )
    {
        return status;
    }

    if ( ( state->config.mode != HW_I2C_MODE_SLAVE ) || ( expected_length == 0U )
         || ( expected_length > HW_I2C_RX_BUFFER_SIZE ) )
    {
        return HW_I2C_STATUS_INVALID_PARAM;
    }

    if ( state->transfer_in_progress )
    {
        return HW_I2C_STATUS_BUSY;
    }

    state->transfer_kind          = HW_I2C_TRANSFER_KIND_SLAVE_RX;
    state->transfer_in_progress   = true;
    state->last_error             = HW_I2C_STATUS_OK;
    state->rx_expected_length     = expected_length;
    state->dma_rx_expected_length = expected_length;

#ifndef TEST_BUILD
    if ( hw_i2c_is_external_channel( channel ) )
    {
        I2C_TypeDef* i2c_instance = hw_i2c_channel_to_instance( channel );

        if ( state->config.rx_transfer_path == HW_I2C_TRANSFER_DMA )
        {
            DMA_Stream_TypeDef* rx_stream = hw_i2c_channel_to_dma_rx_stream( channel );
            if ( rx_stream == NULL )
            {
                return HW_I2C_STATUS_ERROR;
            }

            hw_i2c_dma_stream_clear_flags( rx_stream );
            hw_i2c_configure_dma_stream( rx_stream, hw_i2c_channel_to_dma_channel_bits( channel ),
                                         false, ( uint32_t )&i2c_instance->DR,
                                         ( uint32_t )state->dma_rx_linear_buffer, expected_length );
            hw_i2c_prepare_dma_path( i2c_instance );
        }
        else
        {
            hw_i2c_prepare_interrupt_path( i2c_instance );
        }

        i2c_instance->CR1 |= I2C_CR1_ACK;
    }
#else
    for ( uint16_t idx = 0U; idx < expected_length; ++idx )
    {
        hw_i2c_ring_push_byte( state, 0x00U );
    }
    state->transfer_in_progress = false;
    state->transfer_kind        = HW_I2C_TRANSFER_KIND_IDLE;
    state->rx_expected_length   = 0U;
#endif

    return HW_I2C_STATUS_OK;
}

HW_I2C_Status_T HW_I2C_Peek_Received( HW_I2C_Channel_T channel, HW_I2C_RxPeek_T* peek )
{
    HW_I2C_ChannelState_T* state  = NULL;
    HW_I2C_Status_T        status = hw_i2c_validate_active_channel( channel, &state );
    if ( status != HW_I2C_STATUS_OK )
    {
        return status;
    }

    if ( peek == NULL )
    {
        return HW_I2C_STATUS_INVALID_PARAM;
    }

    const uint16_t count = hw_i2c_ring_count( state );
    peek->total_length   = count;

    if ( count == 0U )
    {
        peek->first.data    = NULL;
        peek->first.length  = 0U;
        peek->second.data   = NULL;
        peek->second.length = 0U;
        return HW_I2C_STATUS_OK;
    }

    if ( state->rx_head > state->rx_tail )
    {
        peek->first.data    = &state->rx_ring_buffer[state->rx_tail];
        peek->first.length  = count;
        peek->second.data   = NULL;
        peek->second.length = 0U;
    }
    else
    {
        const uint16_t first_length  = ( uint16_t )( HW_I2C_RX_BUFFER_SIZE - state->rx_tail );
        const uint16_t second_length = ( uint16_t )( count - first_length );

        peek->first.data    = &state->rx_ring_buffer[state->rx_tail];
        peek->first.length  = first_length;
        peek->second.data   = &state->rx_ring_buffer[0];
        peek->second.length = second_length;
    }

    return HW_I2C_STATUS_OK;
}

HW_I2C_Status_T HW_I2C_Consume_Received( HW_I2C_Channel_T channel, uint16_t bytes_to_consume )
{
    HW_I2C_ChannelState_T* state  = NULL;
    HW_I2C_Status_T        status = hw_i2c_validate_active_channel( channel, &state );
    if ( status != HW_I2C_STATUS_OK )
    {
        return status;
    }

    const uint16_t count = hw_i2c_ring_count( state );
    if ( bytes_to_consume > count )
    {
        return HW_I2C_STATUS_INVALID_PARAM;
    }

    state->rx_tail = ( uint16_t )( ( state->rx_tail + bytes_to_consume ) % HW_I2C_RX_BUFFER_SIZE );
    return HW_I2C_STATUS_OK;
}

void HW_I2C_Service_Event_IRQ( HW_I2C_Channel_T channel )
{
    if ( !hw_i2c_channel_is_valid( channel ) )
    {
        return;
    }

    HW_I2C_ChannelState_T* state = &hw_i2c_channel_state[channel];
    if ( !state->configured )
    {
        return;
    }

#ifndef TEST_BUILD
    if ( channel == HW_I2C_CHANNEL_FMPI2C1 )
    {
        hw_i2c_service_event_fmpi2c1( state );
        return;
    }

    I2C_TypeDef* i2c_instance = hw_i2c_channel_to_instance( channel );
    if ( i2c_instance == NULL )
    {
        return;
    }

    hw_i2c_service_event_external( channel, i2c_instance );
#else
    ( void )state;
#endif
}

void HW_I2C_Service_Error_IRQ( HW_I2C_Channel_T channel )
{
    if ( !hw_i2c_channel_is_valid( channel ) )
    {
        return;
    }

#ifndef TEST_BUILD
    if ( channel == HW_I2C_CHANNEL_FMPI2C1 )
    {
        hw_i2c_channel_state[channel].last_error           = HW_I2C_STATUS_ERROR;
        hw_i2c_channel_state[channel].transfer_in_progress = false;
        hw_i2c_channel_state[channel].transfer_kind        = HW_I2C_TRANSFER_KIND_IDLE;
        FMPI2C1->ICR =
            FMPI2C_ICR_BERRCF | FMPI2C_ICR_ARLOCF | FMPI2C_ICR_OVRCF | FMPI2C_ICR_TIMOUTCF;
        return;
    }

    I2C_TypeDef* i2c_instance = hw_i2c_channel_to_instance( channel );
    if ( i2c_instance == NULL )
    {
        return;
    }

    if ( ( i2c_instance->SR1 & ( I2C_SR1_BERR | I2C_SR1_ARLO | I2C_SR1_AF | I2C_SR1_OVR ) ) != 0U )
    {
        i2c_instance->SR1 &= ~( I2C_SR1_BERR | I2C_SR1_ARLO | I2C_SR1_AF | I2C_SR1_OVR );
        hw_i2c_abort_transfer( channel, i2c_instance, HW_I2C_STATUS_ERROR );
    }
#endif
}

void HW_I2C_Service_DMA_Rx_IRQ( HW_I2C_Channel_T channel )
{
    if ( !hw_i2c_is_external_channel( channel ) )
    {
        return;
    }

    HW_I2C_ChannelState_T* state = &hw_i2c_channel_state[channel];
    if ( !state->configured )
    {
        return;
    }

#ifndef TEST_BUILD
    DMA_Stream_TypeDef* rx_stream    = hw_i2c_channel_to_dma_rx_stream( channel );
    I2C_TypeDef*        i2c_instance = hw_i2c_channel_to_instance( channel );
    if ( ( rx_stream == NULL ) || ( i2c_instance == NULL ) )
    {
        return;
    }

    if ( hw_i2c_dma_stream_has_te( rx_stream ) )
    {
        hw_i2c_dma_stream_clear_flags( rx_stream );
        hw_i2c_abort_transfer( channel, i2c_instance, HW_I2C_STATUS_ERROR );
        return;
    }

    if ( hw_i2c_dma_stream_has_tc( rx_stream ) )
    {
        hw_i2c_dma_stream_clear_flags( rx_stream );

        for ( uint16_t idx = 0U; idx < state->dma_rx_expected_length; ++idx )
        {
            hw_i2c_ring_push_byte( state, state->dma_rx_linear_buffer[idx] );
        }

        state->dma_rx_expected_length = 0U;
        hw_i2c_finish_transfer( channel, i2c_instance );
    }
#endif
}

void HW_I2C_Service_DMA_Tx_IRQ( HW_I2C_Channel_T channel )
{
    if ( !hw_i2c_is_external_channel( channel ) )
    {
        return;
    }

    HW_I2C_ChannelState_T* state = &hw_i2c_channel_state[channel];
    if ( !state->configured )
    {
        return;
    }

#ifndef TEST_BUILD
    DMA_Stream_TypeDef* tx_stream    = hw_i2c_channel_to_dma_tx_stream( channel );
    I2C_TypeDef*        i2c_instance = hw_i2c_channel_to_instance( channel );
    if ( ( tx_stream == NULL ) || ( i2c_instance == NULL ) )
    {
        return;
    }

    if ( hw_i2c_dma_stream_has_te( tx_stream ) )
    {
        hw_i2c_dma_stream_clear_flags( tx_stream );
        hw_i2c_abort_transfer( channel, i2c_instance, HW_I2C_STATUS_ERROR );
        return;
    }

    if ( hw_i2c_dma_stream_has_tc( tx_stream ) )
    {
        hw_i2c_dma_stream_clear_flags( tx_stream );
        state->tx_remaining = 0U;

        if ( state->transfer_kind == HW_I2C_TRANSFER_KIND_MASTER_TX )
        {
            state->dma_tx_transfer_complete = true;
            if ( ( i2c_instance->SR1 & I2C_SR1_BTF ) != 0U )
            {
                hw_i2c_finish_transfer( channel, i2c_instance );
            }
        }
        else if ( state->transfer_kind == HW_I2C_TRANSFER_KIND_SLAVE_TX )
        {
            state->dma_tx_transfer_complete = true;
        }
        else
        {
            hw_i2c_finish_transfer( channel, i2c_instance );
        }
    }
#endif
}

HW_I2C_Status_T HW_I2C_Get_Last_Error( HW_I2C_Channel_T channel )
{
    if ( !hw_i2c_channel_is_valid( channel ) )
    {
        return HW_I2C_STATUS_INVALID_PARAM;
    }

    return hw_i2c_channel_state[channel].last_error;
}

/**
 * @brief This function handles I2C1 event interrupt.
 */
void I2C1_EV_IRQHandler( void )
{
    /* USER CODE BEGIN I2C1_EV_IRQn 0 */
    HW_I2C_Service_Event_IRQ( HW_I2C_CHANNEL_1 );
    /* USER CODE END I2C1_EV_IRQn 0 */

    /* USER CODE BEGIN I2C1_EV_IRQn 1 */

    /* USER CODE END I2C1_EV_IRQn 1 */
}

/**
 * @brief This function handles I2C2 event interrupt.
 */
void I2C2_EV_IRQHandler( void )
{
    /* USER CODE BEGIN I2C2_EV_IRQn 0 */
    HW_I2C_Service_Event_IRQ( HW_I2C_CHANNEL_2 );
    /* USER CODE END I2C2_EV_IRQn 0 */

    /* USER CODE BEGIN I2C2_EV_IRQn 1 */

    /* USER CODE END I2C2_EV_IRQn 1 */
}

/**
 * @brief This function handles FMPI2C1 event interrupt.
 */
void FMPI2C1_EV_IRQHandler( void )
{
    /* USER CODE BEGIN FMPI2C1_EV_IRQn 0 */
    HW_I2C_Service_Event_IRQ( HW_I2C_CHANNEL_FMPI2C1 );
    /* USER CODE END FMPI2C1_EV_IRQn 0 */

    /* USER CODE BEGIN FMPI2C1_EV_IRQn 1 */

    /* USER CODE END FMPI2C1_EV_IRQn 1 */
}

/**
 * @brief This function handles DMA1 stream2 global interrupt.
 */
void DMA1_Stream2_IRQHandler( void )
{
    /* USER CODE BEGIN DMA1_Stream2_IRQn 0 */

    /* USER CODE END DMA1_Stream2_IRQn 0 */
    HW_I2C_Service_DMA_Rx_IRQ( HW_I2C_CHANNEL_2 );
    /* USER CODE BEGIN DMA1_Stream2_IRQn 1 */

    /* USER CODE END DMA1_Stream2_IRQn 1 */
}

/**
 * @brief This function handles DMA1 stream7 global interrupt.
 */
void DMA1_Stream7_IRQHandler( void )
{
    /* USER CODE BEGIN DMA1_Stream7_IRQn 0 */

    /* USER CODE END DMA1_Stream7_IRQn 0 */
    HW_I2C_Service_DMA_Tx_IRQ( HW_I2C_CHANNEL_2 );
    /* USER CODE BEGIN DMA1_Stream7_IRQn 1 */

    /* USER CODE END DMA1_Stream7_IRQn 1 */
}
