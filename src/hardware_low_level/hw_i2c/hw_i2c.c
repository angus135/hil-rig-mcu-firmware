/******************************************************************************
 *  File:       hw_i2c.c
 *  Author:     Coen Pasitchnyj
 *  Created:    20-Apr-2026
 *
 *  Description:
 *      Low-level hardware I2C driver implementation. Manages I2C peripheral
 *      configuration, state machines for master/slave operations, and interrupt/DMA
 *      service routines. Uses ring-buffered receives and stage-buffered transmits.
 *
 *  Notes:
 *      - Requires STM32F4xx HAL/LL driver libraries
 *      - FMPI2C1 operates at 100 kHz with fixed timing register value
 *      - I2C1 interrupt-only; I2C2 supports DMA; FMPI2C1 interrupt-only
 *      - Receive buffers are 512 bytes; transmit stage is 256 bytes
 *      - Not thread-safe; assumes single-threaded execution or external synchronization
 *      - Interrupt handlers must be called from corresponding ISRs in stm32f4xx_it.c
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#ifdef TEST_BUILD
#include "tests/hw_i2c_mocks.h"
#else
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
#include <string.h>

#define HW_I2C_CHANNEL_2_DMA_RX_STREAM DMA1_Stream2
#define HW_I2C_CHANNEL_2_DMA_TX_STREAM DMA1_Stream7

#define HW_I2C_APB1_HZ 45000000UL
#define FMPI2C1_TIMINGR 0xC0000E12U
#define HW_I2C_CHANNEL_2_DMA_RX_TC_FLAG DMA_LISR_TCIF2
#define HW_I2C_CHANNEL_2_DMA_RX_TE_FLAG DMA_LISR_TEIF2
#define HW_I2C_CHANNEL_2_DMA_TX_TC_FLAG DMA_HISR_TCIF7
#define HW_I2C_CHANNEL_2_DMA_TX_TE_FLAG DMA_HISR_TEIF7
#define HW_I2C_CHANNEL_2_DMA_RX_CLEAR_FLAGS_MASK                                                   \
    ( DMA_LIFCR_CTCIF2 | DMA_LIFCR_CTEIF2 | DMA_LIFCR_CDMEIF2 | DMA_LIFCR_CFEIF2 )
#define HW_I2C_CHANNEL_2_DMA_TX_CLEAR_FLAGS_MASK                                                   \
    ( DMA_HIFCR_CTCIF7 | DMA_HIFCR_CTEIF7 | DMA_HIFCR_CDMEIF7 | DMA_HIFCR_CFEIF7 )

#define HW_I2C_EV_IRQ_CHANNEL_1 I2C1_EV_IRQHandler
#define HW_I2C_EV_IRQ_CHANNEL_2 I2C2_EV_IRQHandler
#define HW_I2C_EV_IRQ_FMPI2C1 FMPI2C1_EV_IRQHandler
#define HW_I2C_DMA_RX_IRQ_CHANNEL_2 DMA1_Stream2_IRQHandler
#define HW_I2C_DMA_TX_IRQ_CHANNEL_2 DMA1_Stream7_IRQHandler

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
    /* Configuration state */
    bool                 configured; /* True if channel has been configured */
    HWI2CChannelConfig_T config;     /* Runtime configuration (mode, speed, transfer paths) */

    /* Transfer control and state */
    volatile bool transfer_in_progress; /* True while a transfer (master or slave) is active */
    HWI2CTransferKind_T transfer_kind; /* Current transfer type (idle, master TX/RX, slave TX/RX) */

    /* Master-mode addressing */
    uint16_t target_address_7bit; /* 7-bit slave address for master transfers */
    uint16_t rx_expected_length;  /* Expected receive count; decremented as bytes arrive */

    /* Transmit path: shadow buffer and pointers */
    uint8_t        tx_stage_buffer[HW_I2C_TX_STAGE_SIZE]; /* Holds data to be transmitted */
    uint16_t       tx_stage_length;                       /* Number of bytes in tx_stage_buffer */
    const uint8_t* tx_ptr;       /* Current position in tx_stage_buffer during transfer */
    uint16_t       tx_remaining; /* Bytes left to transmit */
    volatile bool
        dma_tx_transfer_complete; /* Flag set when DMA TX finishes (for master TX detection) */

    /* Receive path: DMA linear buffer (used by DMA transfers on I2C2) */
    uint8_t  dma_rx_linear_buffer[HW_I2C_RX_BUFFER_SIZE]; /* Linear buffer filled by DMA */
    uint16_t dma_rx_expected_length;                      /* Expected DMA receive count */

    /* Receive path: ring buffer (used by interrupt-driven receives and exported to caller) */
    uint8_t           rx_ring_buffer[HW_I2C_RX_BUFFER_SIZE]; /* Ring buffer for received data */
    volatile uint16_t rx_head; /* Write pointer (advanced by RX ISR/DMA) */
    volatile uint16_t rx_tail; /* Read pointer (advanced by consumer) */

    /* Error tracking */
    volatile bool overflow_occurred; /* True if ring buffer overflow detected during receive */
} HWI2CChannelState_T;

/**-----------------------------------------------------------------------------
 *  Public (global) and Extern Variables
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Private (static) Variables
 *------------------------------------------------------------------------------
 */

static HWI2CChannelState_T hw_i2c_channel_state[HW_I2C_CHANNEL_COUNT] = { 0 };

typedef struct HWI2CMapping_T
{
    I2C_TypeDef*        instance;
    DMA_Stream_TypeDef* dma_rx;
    DMA_Stream_TypeDef* dma_tx;
    uint32_t            dma_channel_bits;
} HWI2CMapping_T;

static const HWI2CMapping_T HW_I2C_MAP[HW_I2C_CHANNEL_COUNT] = {
    { .instance = I2C1, .dma_rx = NULL, .dma_tx = NULL, .dma_channel_bits = 0UL },
    { .instance         = I2C2,
      .dma_rx           = HW_I2C_CHANNEL_2_DMA_RX_STREAM,
      .dma_tx           = HW_I2C_CHANNEL_2_DMA_TX_STREAM,
      .dma_channel_bits = ( 7UL << DMA_SxCR_CHSEL_Pos ) },
    { .instance = NULL, .dma_rx = NULL, .dma_tx = NULL, .dma_channel_bits = 0UL },
};

/**-----------------------------------------------------------------------------
 *  Private (static) Function Prototypes
 *------------------------------------------------------------------------------
 */

static inline bool     HW_I2C_Is_External_Channel( HWI2CChannel_T channel );
static inline bool     HW_I2C_Config_Is_Valid( HWI2CChannel_T              channel,
                                               const HWI2CChannelConfig_T* config );
static inline uint16_t HW_I2C_Ring_Count( const HWI2CChannelState_T* state );
static inline uint16_t HW_I2C_Ring_Free( const HWI2CChannelState_T* state );
static inline bool     HW_I2C_Ring_Push_Byte( HWI2CChannelState_T* state, uint8_t data_byte );

static inline uint32_t HWI2CSpeed_To_Hz( HWI2CSpeed_T speed );
static inline void     HW_I2C_Enable_Clock_For_Channel( HWI2CChannel_T channel );
static inline void     HW_I2C_Disable_All_Runtime_Irq_Bits( I2C_TypeDef* i2c_instance );
static inline void     HW_I2C_Disable_DMA_Request( I2C_TypeDef* i2c_instance );
static inline void     HW_I2C_Set_Speed_And_Address( I2C_TypeDef* i2c_instance, HWI2CSpeed_T speed,
                                                     uint16_t own_address_7bit );
static inline void     HW_I2C_Prepare_Interrupt_Path( I2C_TypeDef* i2c_instance );
static inline void     HW_I2C_Prepare_DMA_Path( I2C_TypeDef*        i2c_instance,
                                                HWI2CTransferKind_T transfer_kind );
static inline void     HW_I2C_Start_Master_Transfer( I2C_TypeDef*        i2c_instance,
                                                     HWI2CTransferKind_T transfer_kind, bool use_dma );
static inline void     HW_I2C_Finish_Transfer( HWI2CChannel_T channel, I2C_TypeDef* i2c_instance );
static inline void     HW_I2C_Abort_Transfer( HWI2CChannel_T channel, I2C_TypeDef* i2c_instance );
static void        HW_I2C_Configure_DMA_Stream( DMA_Stream_TypeDef* stream, uint32_t channel_bits,
                                                bool memory_to_peripheral, uint32_t peripheral_address,
                                                uint32_t memory_address, uint16_t length );
static inline bool HW_I2C_DMA_Stream_Has_TC( DMA_Stream_TypeDef* stream );
static inline bool HW_I2C_DMA_Stream_Has_TE( DMA_Stream_TypeDef* stream );
static inline void HW_I2C_DMA_Stream_Clear_Flags( DMA_Stream_TypeDef* stream );
static inline void HW_I2C_Service_Event_External( HWI2CChannel_T channel,
                                                  I2C_TypeDef*   i2c_instance );
static inline void HW_I2C_Service_Event_FMPI2C1( HWI2CChannelState_T* state );

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

static inline bool HW_I2C_Is_External_Channel( HWI2CChannel_T channel )
{
    return ( channel == HW_I2C_CHANNEL_1 ) || ( channel == HW_I2C_CHANNEL_2 );
}

static inline bool HW_I2C_Config_Is_Valid( HWI2CChannel_T              channel,
                                           const HWI2CChannelConfig_T* config )
{
    if ( config == NULL )
    {
        return false;
    }

    /* Mode must be master or slave */
    if ( ( config->mode != HW_I2C_MODE_MASTER ) && ( config->mode != HW_I2C_MODE_SLAVE ) )
    {
        return false;
    }

    /* Speed must be 100 kHz or 400 kHz */
    if ( ( config->speed != HW_I2C_SPEED_100KHZ ) && ( config->speed != HW_I2C_SPEED_400KHZ ) )
    {
        return false;
    }

    /* Transfer paths must be interrupt or DMA */
    if ( ( config->tx_transfer_path != HW_I2C_TRANSFER_INTERRUPT )
         && ( config->tx_transfer_path != HW_I2C_TRANSFER_DMA ) )
    {
        return false;
    }

    /* Transfer paths must be interrupt or DMA */
    if ( ( config->rx_transfer_path != HW_I2C_TRANSFER_INTERRUPT )
         && ( config->rx_transfer_path != HW_I2C_TRANSFER_DMA ) )
    {
        return false;
    }

    /* Own address must be 7 bits max */
    if ( config->own_address_7bit > 0x7FU )
    {
        return false;
    }

    /* I2C1 does not support DMA */
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

static inline uint16_t HW_I2C_Ring_Count( const HWI2CChannelState_T* state )
{
    /* Return number of bytes currently stored in the ring buffer.
       When head >= tail the bytes are contiguous; when head < tail the
       buffer has wrapped and the count is computed accordingly. */
    if ( state->rx_head >= state->rx_tail )
    {
        return ( uint16_t )( state->rx_head - state->rx_tail );
    }

    /* Wrapped case: available data is buffer size minus the unused gap. */
    return ( uint16_t )( HW_I2C_RX_BUFFER_SIZE - ( state->rx_tail - state->rx_head ) );
}

static inline uint16_t HW_I2C_Ring_Free( const HWI2CChannelState_T* state )
{
    /* Calculate free space in the ring buffer. One slot is intentionally
       left unused to distinguish full from empty, so subtract one. */
    return ( uint16_t )( ( HW_I2C_RX_BUFFER_SIZE - 1U ) - HW_I2C_Ring_Count( state ) );
}

static inline bool HW_I2C_Ring_Push_Byte( HWI2CChannelState_T* state, uint8_t data_byte )
{
    /* Fail if no free space available. */
    if ( HW_I2C_Ring_Free( state ) == 0U )
    {
        return false;
    }

    /* Store byte at head and advance head (wrapping via modulo). */
    state->rx_ring_buffer[state->rx_head] = data_byte;
    state->rx_head = ( uint16_t )( ( state->rx_head + 1U ) % HW_I2C_RX_BUFFER_SIZE );
    return true;
}

static inline uint32_t HWI2CSpeed_To_Hz( HWI2CSpeed_T speed )
{
    return ( speed == HW_I2C_SPEED_400KHZ ) ? 400000UL : 100000UL;
}

static inline void HW_I2C_Enable_Clock_For_Channel( HWI2CChannel_T channel )
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
        case HW_I2C_CHANNEL_COUNT:
        default:
            break;
    }
}

static inline void HW_I2C_Disable_All_Runtime_Irq_Bits( I2C_TypeDef* i2c_instance )
{
    LL_I2C_DisableIT_ERR( i2c_instance );
    LL_I2C_DisableIT_EVT( i2c_instance );
    LL_I2C_DisableIT_BUF( i2c_instance );
}

static inline void HW_I2C_Disable_DMA_Request( I2C_TypeDef* i2c_instance )
{
    LL_I2C_DisableDMAReq_TX( i2c_instance );
    LL_I2C_DisableDMAReq_RX( i2c_instance );
}

static inline void HW_I2C_Set_Speed_And_Address( I2C_TypeDef* i2c_instance, HWI2CSpeed_T speed,
                                                 uint16_t own_address_7bit )
{
    LL_I2C_Disable( i2c_instance );
    LL_I2C_SetPeriphClock( i2c_instance, HW_I2C_APB1_HZ );
    LL_I2C_ConfigSpeed( i2c_instance, HW_I2C_APB1_HZ, HWI2CSpeed_To_Hz( speed ),
                        LL_I2C_DUTYCYCLE_2 );
    LL_I2C_SetOwnAddress1( i2c_instance, ( uint32_t )own_address_7bit << 1U,
                           LL_I2C_OWNADDRESS1_7BIT );
    LL_I2C_Enable( i2c_instance );
}

static inline void HW_I2C_Prepare_Interrupt_Path( I2C_TypeDef* i2c_instance )
{
    HW_I2C_Disable_DMA_Request( i2c_instance );
    LL_I2C_EnableIT_ERR( i2c_instance );
    LL_I2C_EnableIT_EVT( i2c_instance );
    LL_I2C_EnableIT_BUF( i2c_instance );
}

static inline void HW_I2C_Prepare_DMA_Path( I2C_TypeDef*        i2c_instance,
                                            HWI2CTransferKind_T transfer_kind )
{
    LL_I2C_EnableIT_ERR( i2c_instance );
    LL_I2C_EnableIT_EVT( i2c_instance );
    LL_I2C_DisableIT_BUF( i2c_instance );

    if ( ( transfer_kind == HW_I2C_TRANSFER_KIND_MASTER_RX )
         || ( transfer_kind == HW_I2C_TRANSFER_KIND_SLAVE_RX ) )
    {
        LL_I2C_EnableDMAReq_RX( i2c_instance );
    }
    else
    {
        LL_I2C_EnableDMAReq_TX( i2c_instance );
    }
}

/**
 * @brief Prepare and start a master transfer on the given I2C instance.
 *
 * This helper performs the minimal sequence required to begin a master
 * transfer: set acknowledge behaviour for incoming reads, enable the
 * appropriate runtime path (DMA or interrupt), then generate a START.
 *
 * @param i2c_instance  Pointer to the I2C peripheral registers.
 * @param transfer_kind Indicates whether this is a master RX or TX.
 * @param use_dma       True to configure DMA path, false for IRQ path.
 *
 * Notes:
 * - When initiating a master read (MASTER_RX) we must ACK the next
 *   received byte so the peripheral will continue delivering data.
 * - The function only prepares the peripheral and triggers START; it
 *   does not manage transfer state variables (caller must do that).
 */
static inline void HW_I2C_Start_Master_Transfer( I2C_TypeDef*        i2c_instance,
                                                 HWI2CTransferKind_T transfer_kind, bool use_dma )
{
    /* For master receive transfers, acknowledge the next incoming byte so
       the peripheral will keep sending data (NACK would terminate). */
    if ( transfer_kind == HW_I2C_TRANSFER_KIND_MASTER_RX )
    {
        LL_I2C_AcknowledgeNextData( i2c_instance, LL_I2C_ACK );
    }

    /* Configure runtime path: DMA requires different setup than the
       interrupt-driven path. */
    if ( use_dma )
    {
        HW_I2C_Prepare_DMA_Path( i2c_instance, transfer_kind );
    }
    else
    {
        HW_I2C_Prepare_Interrupt_Path( i2c_instance );
    }

    /* Finally, tell the peripheral to issue a START condition on the bus. */
    LL_I2C_GenerateStartCondition( i2c_instance );
}

/**
 * @brief Complete an ongoing transfer and clean up runtime state.
 *
 * This helper performs the canonical end-of-transfer tasks:
 * - issue a STOP condition for master transfers that generated traffic
 * - disable DMA streams if they were used for the channel
 * - disable runtime IRQ/DMA requests and reset transfer state fields
 *
 * Caller expectation: the peripheral/ISR may already have placed the
 * device into a quiescent state; this call ensures all runtime
 * resources are released and the software state reflects idle.
 *
 * @param channel       Channel index for state bookkeeping.
 * @param i2c_instance  Peripheral register base for STOP generation.
 */
static inline void HW_I2C_Finish_Transfer( HWI2CChannel_T channel, I2C_TypeDef* i2c_instance )
{
    HWI2CChannelState_T* state = &hw_i2c_channel_state[channel];

    /* Generate STOP for transfers that actually drove the bus as master. */
    if ( state->transfer_kind == HW_I2C_TRANSFER_KIND_MASTER_TX
         || state->transfer_kind == HW_I2C_TRANSFER_KIND_MASTER_RX )
    {
        LL_I2C_GenerateStopCondition( i2c_instance );
    }

    /* If DMA was configured for either path, ensure DMA streams are
       disabled and clear the peripheral DMA request lines. */
    if ( state->config.tx_transfer_path == HW_I2C_TRANSFER_DMA
         || state->config.rx_transfer_path == HW_I2C_TRANSFER_DMA )
    {
        DMA_Stream_TypeDef* rx_stream = HW_I2C_MAP[channel].dma_rx;
        DMA_Stream_TypeDef* tx_stream = HW_I2C_MAP[channel].dma_tx;
        if ( rx_stream != NULL )
        {
            rx_stream->CR &= ~DMA_SxCR_EN;
        }
        if ( tx_stream != NULL )
        {
            tx_stream->CR &= ~DMA_SxCR_EN;
        }
        HW_I2C_Disable_DMA_Request( i2c_instance );
    }

    /* Disable runtime IRQ bits (ERR/EVT/BUF) to stop further ISR traffic. */
    HW_I2C_Disable_All_Runtime_Irq_Bits( i2c_instance );

    /* Reset channel state so it can be reused for future transfers. */
    state->transfer_in_progress     = false;
    state->transfer_kind            = HW_I2C_TRANSFER_KIND_IDLE;
    state->tx_remaining             = 0U;
    state->dma_tx_transfer_complete = false;
    state->rx_expected_length       = 0U;
}

/**
 * @brief Abort an in-progress transfer and release resources.
 *
 * This is a thin wrapper that performs the same cleanup as
 * HW_I2C_Finish_Transfer. It exists to provide a clear semantic
 * name when error paths need to terminate a transfer prematurely.
 */
static inline void HW_I2C_Abort_Transfer( HWI2CChannel_T channel, I2C_TypeDef* i2c_instance )
{
    /* Delegate to Finish which handles STOP, DMA and state cleanup. */
    HW_I2C_Finish_Transfer( channel, i2c_instance );
}

static void HW_I2C_Configure_DMA_Stream( DMA_Stream_TypeDef* stream, uint32_t channel_bits,
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

static inline bool HW_I2C_DMA_Stream_Has_TC( DMA_Stream_TypeDef* stream )
{
    if ( stream == HW_I2C_CHANNEL_2_DMA_RX_STREAM )
    {
        return ( ( DMA1->LISR & HW_I2C_CHANNEL_2_DMA_RX_TC_FLAG ) != 0U );
    }
    if ( stream == HW_I2C_CHANNEL_2_DMA_TX_STREAM )
    {
        return ( ( DMA1->HISR & HW_I2C_CHANNEL_2_DMA_TX_TC_FLAG ) != 0U );
    }
    return false;
}

static inline bool HW_I2C_DMA_Stream_Has_TE( DMA_Stream_TypeDef* stream )
{
    if ( stream == HW_I2C_CHANNEL_2_DMA_RX_STREAM )
    {
        return ( ( DMA1->LISR & HW_I2C_CHANNEL_2_DMA_RX_TE_FLAG ) != 0U );
    }
    if ( stream == HW_I2C_CHANNEL_2_DMA_TX_STREAM )
    {
        return ( ( DMA1->HISR & HW_I2C_CHANNEL_2_DMA_TX_TE_FLAG ) != 0U );
    }
    return false;
}

static inline void HW_I2C_DMA_Stream_Clear_Flags( DMA_Stream_TypeDef* stream )
{
    if ( stream == HW_I2C_CHANNEL_2_DMA_RX_STREAM )
    {
        DMA1->LIFCR = HW_I2C_CHANNEL_2_DMA_RX_CLEAR_FLAGS_MASK;
    }
    else if ( stream == HW_I2C_CHANNEL_2_DMA_TX_STREAM )
    {
        DMA1->HIFCR = HW_I2C_CHANNEL_2_DMA_TX_CLEAR_FLAGS_MASK;
    }
}

static inline void HW_I2C_Service_Event_External( HWI2CChannel_T channel,
                                                  I2C_TypeDef*   i2c_instance )
{
    /**
     * @brief Service I2C events for an external I2C channel (I2C1/I2C2).
     *
     * This function is intended to be called from the I2C event interrupt
     * handler. It inspects SR1 to determine the event and then updates
     * peripheral registers and the software state machine accordingly.
     *
     * Events handled (non-exhaustive):
     * - SB: Start bit detected, send address+R/W
     * - ADDR: Address matched, adjust ACK behaviour for short reads
     * - TXE/BTF: Transmit data register empty / byte transfer finished
     * - RXNE: Received byte available, push into ring buffer
     * - STOPF: Stop detected, finish transfer and cleanup
     */

    HWI2CChannelState_T* state = &hw_i2c_channel_state[channel];
    uint32_t             sr1   = i2c_instance->SR1;

    /* Start bit - master has sent START; write (addr<<1 | R/W) into DR */
    if ( ( sr1 & I2C_SR1_SB ) != 0U )
    {
        const uint8_t direction_bit =
            ( state->transfer_kind == HW_I2C_TRANSFER_KIND_MASTER_RX ) ? 1U : 0U;
        i2c_instance->DR =
            ( uint32_t )( ( uint8_t )( ( state->target_address_7bit << 1U ) | direction_bit ) );
        return;
    }

    /* Address matched - read SR1/SR2 to clear the flag. For master RX with
       a short expected length, prepare to NACK so the peripheral stops after
       the final byte. */
    if ( ( sr1 & I2C_SR1_ADDR ) != 0U )
    {
        volatile uint32_t clear_addr = i2c_instance->SR1;
        clear_addr                   = i2c_instance->SR2;
        ( void )clear_addr;

        if ( state->transfer_kind == HW_I2C_TRANSFER_KIND_MASTER_RX )
        {
            if ( state->rx_expected_length <= 1U )
            {
                LL_I2C_AcknowledgeNextData( i2c_instance, LL_I2C_NACK );
            }
        }
        return;
    }

    /* Transmit path: if not using DMA, service TXE by writing next byte
       or sending a filler byte for slave TX when data exhausted. When BTF
       indicates completion and no data remains, finish the transfer. */
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
                /* No data to send for slave - send 0xFF as filler. */
                i2c_instance->DR = 0xFFU;
            }
        }

        if ( ( state->transfer_kind == HW_I2C_TRANSFER_KIND_MASTER_TX )
             && ( state->tx_remaining == 0U )
             && ( ( !tx_dma_mode ) || state->dma_tx_transfer_complete )
             && ( ( sr1 & I2C_SR1_BTF ) != 0U ) )
        {
            HW_I2C_Finish_Transfer( channel, i2c_instance );
            return;
        }

        if ( ( state->transfer_kind == HW_I2C_TRANSFER_KIND_SLAVE_TX ) && tx_dma_mode
             && state->dma_tx_transfer_complete && ( ( sr1 & I2C_SR1_BTF ) != 0U ) )
        {
            /* For slave + DMA, when DMA finished and BTF set, send filler. This is required so the
             * line is released properly and doesn't hang.
             */
            i2c_instance->DR = 0xFFU;
        }
    }

    /* Receive path: read DR when RXNE set and push into ring buffer. If the
       transfer expected length reaches 0, finish the transfer. */
    if ( state->transfer_kind == HW_I2C_TRANSFER_KIND_MASTER_RX
         || state->transfer_kind == HW_I2C_TRANSFER_KIND_SLAVE_RX )
    {
        if ( ( sr1 & I2C_SR1_RXNE ) != 0U )
        {
            if ( ( state->transfer_kind == HW_I2C_TRANSFER_KIND_MASTER_RX )
                 && ( state->rx_expected_length <= 1U ) )
            {
                /* For the final byte(s), NACK and generate STOP to end read. */
                LL_I2C_AcknowledgeNextData( i2c_instance, LL_I2C_NACK );
                LL_I2C_GenerateStopCondition( i2c_instance );
            }

            uint8_t data_byte = ( uint8_t )i2c_instance->DR;
            if ( !HW_I2C_Ring_Push_Byte( state, data_byte ) )
            {
                /* Ring buffer overflow - abort transfer and mark error. */
                state->overflow_occurred = true;
                HW_I2C_Abort_Transfer( channel, i2c_instance );
                return;
            }

            if ( state->rx_expected_length > 0U )
            {
                state->rx_expected_length--;
            }

            if ( state->rx_expected_length == 0U )
            {
                HW_I2C_Finish_Transfer( channel, i2c_instance );
                return;
            }
        }
    }

    /* STOP detected by peripheral - clear flag and finish transfer. */
    if ( ( sr1 & I2C_SR1_STOPF ) != 0U )
    {
        LL_I2C_ClearFlag_STOP( i2c_instance );
        HW_I2C_Finish_Transfer( channel, i2c_instance );
    }
}

static inline void HW_I2C_Service_Event_FMPI2C1( HWI2CChannelState_T* state )
{
    /**
     * @brief Service FMPI2C1 events.
     *
     * FMPI2C1 (internal high-speed I2C) uses different register names but
     * the handling pattern is similar: write TXDR on TXIS, read RXDR on
     * RXNE and push to ring buffer, and respond to STOP/TC/NACK/error flags.
     */

    uint32_t isr = FMPI2C1->ISR;

    /* Transmit ready: write next byte if available. */
    if ( ( isr & FMPI2C_ISR_TXIS ) != 0U )
    {
        if ( state->tx_remaining > 0U )
        {
            FMPI2C1->TXDR = *state->tx_ptr;
            state->tx_ptr++;
            state->tx_remaining--;
        }
    }

    /* Receive ready: read byte and push into ring buffer. */
    if ( ( isr & FMPI2C_ISR_RXNE ) != 0U )
    {
        uint8_t data_byte = ( uint8_t )FMPI2C1->RXDR;
        if ( !HW_I2C_Ring_Push_Byte( state, data_byte ) )
        {
            /* Ring buffer overflow - abort transfer and mark error. */
            state->overflow_occurred    = true;
            state->transfer_in_progress = false;
            state->transfer_kind        = HW_I2C_TRANSFER_KIND_IDLE;
            return;
        }
        if ( state->rx_expected_length > 0U )
        {
            state->rx_expected_length--;
        }
    }

    /* STOP flag: clear and mark transfer finished. */
    if ( ( isr & FMPI2C_ISR_STOPF ) != 0U )
    {
        LL_FMPI2C_ClearFlag_STOP( FMPI2C1 );
        state->transfer_in_progress = false;
        state->transfer_kind        = HW_I2C_TRANSFER_KIND_IDLE;
        return;
    }

    /* Transfer complete: generate STOP to terminate transfer. */
    if ( ( isr & FMPI2C_ISR_TC ) != 0U )
    {
        LL_FMPI2C_GenerateStopCondition( FMPI2C1 );
    }

    /* NACK received: clear and abort transfer. */
    if ( ( isr & FMPI2C_ISR_NACKF ) != 0U )
    {
        LL_FMPI2C_ClearFlag_NACK( FMPI2C1 );
        state->transfer_in_progress = false;
        state->transfer_kind        = HW_I2C_TRANSFER_KIND_IDLE;
    }

    /* Bus error conditions: clear peripheral flags and abort. */
    if ( ( isr & ( FMPI2C_ISR_BERR | FMPI2C_ISR_ARLO | FMPI2C_ISR_OVR | FMPI2C_ISR_TIMEOUT ) )
         != 0U )
    {
        LL_FMPI2C_ClearFlag_BERR( FMPI2C1 );
        LL_FMPI2C_ClearFlag_ARLO( FMPI2C1 );
        LL_FMPI2C_ClearFlag_OVR( FMPI2C1 );
        LL_FMPI2C_ClearSMBusFlag_TIMEOUT( FMPI2C1 );
        state->transfer_in_progress = false;
        state->transfer_kind        = HW_I2C_TRANSFER_KIND_IDLE;
    }
}

/**
 * @brief Service I2C event interrupt.
 *
 * Should be called from the I2C event interrupt handler for the channel.
 * Manages state machine for master/slave operations and data transfers.
 *
 * @param[in] channel  I2C channel experiencing the event
 */
static inline void HW_I2C_Service_Event_IRQ( HWI2CChannel_T channel )
{
    HWI2CChannelState_T* state = &hw_i2c_channel_state[channel];

    /* FMPI2C1 has a different register interface; route to dedicated handler. */
    if ( channel == HW_I2C_CHANNEL_FMPI2C1 )
    {
        HW_I2C_Service_Event_FMPI2C1( state );
        return;
    }

    /* External channels (I2C1, I2C2) route to their dedicated handler. */
    I2C_TypeDef* i2c_instance = HW_I2C_MAP[channel].instance;
    if ( i2c_instance == NULL )
    {
        return;
    }

    HW_I2C_Service_Event_External( channel, i2c_instance );
}

/**
 * @brief Service DMA receive interrupt.
 *
 * Should be called from the DMA stream interrupt handler for I2C receive.
 * Transfers DMA-received data into the ring buffer and detects completion.
 *
 * @param[in] channel  I2C channel with pending DMA receive completion
 */
static inline void HW_I2C_Service_DMA_Rx_IRQ( HWI2CChannel_T channel )
{
    HWI2CChannelState_T* state = &hw_i2c_channel_state[channel];

    /* Get the DMA stream and I2C peripheral for this channel. */
    DMA_Stream_TypeDef* rx_stream    = HW_I2C_MAP[channel].dma_rx;
    I2C_TypeDef*        i2c_instance = HW_I2C_MAP[channel].instance;

    /* Check for DMA transfer error (e.g., FIFO error). */
    if ( HW_I2C_DMA_Stream_Has_TE( rx_stream ) )
    {
        HW_I2C_DMA_Stream_Clear_Flags( rx_stream );
        /* Error detected; abort the transfer and cleanup. */
        HW_I2C_Abort_Transfer( channel, i2c_instance );
        return;
    }

    /* Check for DMA transfer completion. */
    if ( HW_I2C_DMA_Stream_Has_TC( rx_stream ) )
    {
        HW_I2C_DMA_Stream_Clear_Flags( rx_stream );

        /* Copy all received bytes from the linear DMA buffer into the ring buffer
           so they can be retrieved via HW_I2C_Peek_Received/Consume_Received. */
        for ( uint16_t idx = 0U; idx < state->dma_rx_expected_length; ++idx )
        {
            if ( !HW_I2C_Ring_Push_Byte( state, state->dma_rx_linear_buffer[idx] ) )
            {
                /* Ring buffer overflow - abort transfer and mark error. */
                state->overflow_occurred = true;
                HW_I2C_Abort_Transfer( channel, i2c_instance );
                return;
            }
        }

        state->dma_rx_expected_length = 0U;
        /* Transfer complete; cleanup and release I2C resources. */
        HW_I2C_Finish_Transfer( channel, i2c_instance );
    }
}

/**
 * @brief Service DMA transmit interrupt.
 *
 * Should be called from the DMA stream interrupt handler for I2C transmit.
 * Detects DMA transmit completion and updates transfer state.
 *
 * @param[in] channel  I2C channel with pending DMA transmit completion
 */
static inline void HW_I2C_Service_DMA_Tx_IRQ( HWI2CChannel_T channel )
{
    HWI2CChannelState_T* state = &hw_i2c_channel_state[channel];

    /* Get the DMA stream and I2C peripheral for this channel. */
    DMA_Stream_TypeDef* tx_stream    = HW_I2C_MAP[channel].dma_tx;
    I2C_TypeDef*        i2c_instance = HW_I2C_MAP[channel].instance;

    /* Check for DMA transfer error (e.g., FIFO error). */
    if ( HW_I2C_DMA_Stream_Has_TE( tx_stream ) )
    {
        HW_I2C_DMA_Stream_Clear_Flags( tx_stream );
        /* Error detected; abort the transfer and cleanup. */
        HW_I2C_Abort_Transfer( channel, i2c_instance );
        return;
    }

    /* Check for DMA transfer completion. */
    if ( HW_I2C_DMA_Stream_Has_TC( tx_stream ) )
    {
        HW_I2C_DMA_Stream_Clear_Flags( tx_stream );
        state->tx_remaining = 0U;

        /* For master transmit, mark DMA complete and check if the I2C peripheral
           has finished (BTF = Byte Transfer Finished). If so, finish the transfer. */
        if ( state->transfer_kind == HW_I2C_TRANSFER_KIND_MASTER_TX )
        {
            state->dma_tx_transfer_complete = true;
            if ( ( i2c_instance->SR1 & I2C_SR1_BTF ) != 0U )
            {
                HW_I2C_Finish_Transfer( channel, i2c_instance );
            }
        }
        /* For slave transmit, mark DMA complete but don't finish yet; wait for
           the bus transaction to complete naturally. */
        else if ( state->transfer_kind == HW_I2C_TRANSFER_KIND_SLAVE_TX )
        {
            state->dma_tx_transfer_complete = true;
        }
        /* For other transfer types (e.g., slave receive), finish immediately. */
        else
        {
            HW_I2C_Finish_Transfer( channel, i2c_instance );
        }
    }
}

/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */

/**
 * @brief Configure an external I2C channel (I2C1 or I2C2).
 *
 * Initializes an I2C channel with the specified configuration including mode
 * (master/slave), speed (100 kHz / 400 kHz), and transfer path (interrupt/DMA).
 * Must be called before any transfers on the channel.
 *
 * @param[in] channel       I2C channel to configure (HW_I2C_CHANNEL_1 or HW_I2C_CHANNEL_2)
 * @param[in] config        Pointer to configuration structure. Must not be NULL.
 *
 * @return HW_I2C_STATUS_OK on success
 * @return HW_I2C_STATUS_INVALID_PARAM if parameters are invalid or channel is not external
 */
HWI2CStatus_T HW_I2C_Configure_Channel( HWI2CChannel_T channel, const HWI2CChannelConfig_T* config )
{
    if ( !HW_I2C_Is_External_Channel( channel ) || !HW_I2C_Config_Is_Valid( channel, config ) )
    {
        return HW_I2C_STATUS_INVALID_PARAM;
    }

    HWI2CChannelState_T* state      = &hw_i2c_channel_state[channel];
    state->config                   = *config;
    state->configured               = true;
    state->transfer_in_progress     = false;
    state->transfer_kind            = HW_I2C_TRANSFER_KIND_IDLE;
    state->tx_stage_length          = 0U;
    state->tx_remaining             = 0U;
    state->dma_tx_transfer_complete = false;
    state->rx_expected_length       = 0U;
    state->dma_rx_expected_length   = 0U;
    state->rx_head                  = 0U;
    state->rx_tail                  = 0U;
    state->overflow_occurred        = false;
    I2C_TypeDef* i2c_instance       = HW_I2C_MAP[channel].instance;
    if ( i2c_instance == NULL )
    {
        return HW_I2C_STATUS_INVALID_PARAM;
    }

    HW_I2C_Enable_Clock_For_Channel( channel );
    HW_I2C_Set_Speed_And_Address( i2c_instance, config->speed, config->own_address_7bit );

    if ( config->mode == HW_I2C_MODE_SLAVE )
    {
        LL_I2C_AcknowledgeNextData( i2c_instance, LL_I2C_ACK );
    }
    else
    {
        LL_I2C_AcknowledgeNextData( i2c_instance, LL_I2C_NACK );
    }

    HW_I2C_Disable_All_Runtime_Irq_Bits( i2c_instance );

    return HW_I2C_STATUS_OK;
}

/**
 * @brief Configure the internal FMPI2C1 channel.
 *
 * Initializes the high-speed internal FMPI2C1 channel with a specified own address.
 * Channel operates in master mode with interrupt-based transfer path.
 *
 * @param[in] own_address_7bit  7-bit own address for the channel (0x00-0x7F)
 *
 * @return HW_I2C_STATUS_OK on success
 * @return HW_I2C_STATUS_INVALID_PARAM if address exceeds 7 bits
 */
HWI2CStatus_T HW_I2C_Configure_Internal_FMPI2C1( uint16_t own_address_7bit )
{
    if ( own_address_7bit > 0x7FU )
    {
        return HW_I2C_STATUS_INVALID_PARAM;
    }

    HWI2CChannelState_T* state      = &hw_i2c_channel_state[HW_I2C_CHANNEL_FMPI2C1];
    state->configured               = true;
    state->transfer_in_progress     = false;
    state->transfer_kind            = HW_I2C_TRANSFER_KIND_IDLE;
    state->tx_stage_length          = 0U;
    state->tx_remaining             = 0U;
    state->dma_tx_transfer_complete = false;
    state->rx_expected_length       = 0U;
    state->dma_rx_expected_length   = 0U;
    state->rx_head                  = 0U;
    state->rx_tail                  = 0U;
    state->overflow_occurred        = false;
    state->config.mode              = HW_I2C_MODE_MASTER;
    state->config.speed             = HW_I2C_SPEED_100KHZ;
    state->config.tx_transfer_path  = HW_I2C_TRANSFER_INTERRUPT;
    state->config.rx_transfer_path  = HW_I2C_TRANSFER_INTERRUPT;
    state->config.own_address_7bit  = own_address_7bit;
    HW_I2C_Enable_Clock_For_Channel( HW_I2C_CHANNEL_FMPI2C1 );

    LL_FMPI2C_Disable( FMPI2C1 );
    LL_FMPI2C_SetTiming( FMPI2C1, FMPI2C1_TIMINGR );
    LL_FMPI2C_SetOwnAddress1( FMPI2C1, ( uint32_t )own_address_7bit << 1U,
                              LL_FMPI2C_OWNADDRESS1_7BIT );
    LL_FMPI2C_EnableOwnAddress1( FMPI2C1 );
    LL_FMPI2C_Enable( FMPI2C1 );
    LL_FMPI2C_EnableIT_TX( FMPI2C1 );
    LL_FMPI2C_EnableIT_RX( FMPI2C1 );
    LL_FMPI2C_EnableIT_TC( FMPI2C1 );
    LL_FMPI2C_EnableIT_STOP( FMPI2C1 );
    LL_FMPI2C_EnableIT_ERR( FMPI2C1 );

    return HW_I2C_STATUS_OK;
}

/**
 * @brief Load data into the transmit stage buffer.
 *
 * Prepares data for transmission. Must be called before triggering a transmit.
 * Cannot be called while a transfer is in progress on the channel.
 *
 * @param[in] channel  I2C channel
 * @param[in] data     Pointer to data to transmit. May be NULL if length is 0.
 * @param[in] length   Number of bytes to transmit (max HW_I2C_TX_STAGE_SIZE)
 *
 * @return true if data was loaded successfully
 * @return false if transfer is in progress or length exceeds buffer size
 */
inline bool HW_I2C_Load_Stage_Buffer( HWI2CChannel_T channel, const uint8_t* data, uint16_t length )
{
    HWI2CChannelState_T* state = &hw_i2c_channel_state[channel];

    if ( length > HW_I2C_TX_STAGE_SIZE )
    {
        return false;
    }

    if ( state->transfer_in_progress )
    {
        return false;
    }

    if ( ( data != NULL ) && ( length > 0U ) )
    {
        memcpy( state->tx_stage_buffer, data, ( size_t )length );
    }

    state->tx_stage_length = length;
    return true;
}

/**
 * @brief Trigger a master transmit operation on an external I2C channel.
 *
 * Initiates an I2C master transmit to the specified device address on an external channel
 * (I2C1 or I2C2) using data previously loaded with HW_I2C_Load_Stage_Buffer().
 * Supports both interrupt and DMA-based transfer paths as configured.
 *
 * @param[in] channel               External I2C channel (HW_I2C_CHANNEL_1 or HW_I2C_CHANNEL_2)
 * @param[in] device_address_7bit   7-bit slave address to transmit to
 *
 * @return true if transfer was initiated successfully
 * @return false if another transfer is already in progress
 */
bool HW_I2C_Trigger_Master_Transmit_External( HWI2CChannel_T channel, uint16_t device_address_7bit )
{
    HWI2CChannelState_T* state = &hw_i2c_channel_state[channel];

    if ( state->transfer_in_progress )
    {
        return false;
    }

    state->target_address_7bit      = device_address_7bit;
    state->transfer_kind            = HW_I2C_TRANSFER_KIND_MASTER_TX;
    state->transfer_in_progress     = true;
    state->tx_ptr                   = state->tx_stage_buffer;
    state->tx_remaining             = state->tx_stage_length;
    state->dma_tx_transfer_complete = false;
    state->rx_expected_length       = 0U;

    I2C_TypeDef* i2c_instance = HW_I2C_MAP[channel].instance;
    bool         use_dma      = ( state->config.tx_transfer_path == HW_I2C_TRANSFER_DMA );

    if ( use_dma )
    {
        DMA_Stream_TypeDef* tx_stream = HW_I2C_MAP[channel].dma_tx;

        HW_I2C_DMA_Stream_Clear_Flags( tx_stream );
        HW_I2C_Configure_DMA_Stream( tx_stream, HW_I2C_MAP[channel].dma_channel_bits, true,
                                     ( uint32_t )( uintptr_t )&i2c_instance->DR,
                                     ( uint32_t )( uintptr_t )state->tx_stage_buffer,
                                     state->tx_stage_length );
    }

    HW_I2C_Start_Master_Transfer( i2c_instance, state->transfer_kind, use_dma );

    return true;
}

/**
 * @brief Trigger a master transmit operation on the internal FMPI2C1 channel.
 *
 * Initiates an I2C master transmit to the specified device address on the internal
 * FMPI2C1 channel using data previously loaded with HW_I2C_Load_Stage_Buffer().
 * The FMPI2C1 channel uses interrupt-based transfer path only.
 *
 * @param[in] device_address_7bit   7-bit slave address to transmit to
 *
 * @return true if transfer was initiated successfully
 * @return false if another transfer is already in progress
 */
inline bool HW_I2C_Trigger_Master_Transmit_Internal( uint16_t device_address_7bit )
{
    HWI2CChannelState_T* state = &hw_i2c_channel_state[HW_I2C_CHANNEL_FMPI2C1];

    if ( state->transfer_in_progress )
    {
        return false;
    }

    state->target_address_7bit      = device_address_7bit;
    state->transfer_kind            = HW_I2C_TRANSFER_KIND_MASTER_TX;
    state->transfer_in_progress     = true;
    state->tx_ptr                   = state->tx_stage_buffer;
    state->tx_remaining             = state->tx_stage_length;
    state->dma_tx_transfer_complete = false;
    state->rx_expected_length       = 0U;

    // LL_FMPI2C_GenerateStartCondition( FMPI2C1 );
    FMPI2C1->CR2 = ( ( uint32_t )device_address_7bit << 1U )
                   | ( ( uint32_t )state->tx_stage_length << FMPI2C_CR2_NBYTES_Pos )
                   | FMPI2C_CR2_START | FMPI2C_CR2_AUTOEND;

    return true;
}

/**
 * @brief Trigger a master receive operation on an external I2C channel.
 *
 * Initiates an I2C master receive from the specified device address on an external channel
 * (I2C1 or I2C2). Received data will be available via HW_I2C_Peek_Received() and consumed
 * with HW_I2C_Consume_Received(). Supports both interrupt and DMA-based transfer paths
 * as configured.
 *
 * The overflow flag is cleared when this new receive transfer is armed so the next
 * overflow report only reflects the current transfer.
 *
 * @param[in] channel               External I2C channel (HW_I2C_CHANNEL_1 or HW_I2C_CHANNEL_2)
 * @param[in] device_address_7bit   7-bit slave address to receive from
 * @param[in] expected_length       Number of bytes expected to receive
 *
 * @return true if transfer was initiated successfully
 * @return false if another transfer is already in progress
 */
bool HW_I2C_Trigger_Master_Receive_External( HWI2CChannel_T channel, uint16_t device_address_7bit,
                                             uint16_t expected_length )
{
    HWI2CChannelState_T* state = &hw_i2c_channel_state[channel];

    if ( state->transfer_in_progress )
    {
        return false;
    }

    state->target_address_7bit    = device_address_7bit;
    state->transfer_kind          = HW_I2C_TRANSFER_KIND_MASTER_RX;
    state->transfer_in_progress   = true;
    state->rx_expected_length     = expected_length;
    state->dma_rx_expected_length = expected_length;
    state->overflow_occurred      = false;

    I2C_TypeDef* i2c_instance = HW_I2C_MAP[channel].instance;
    bool         use_dma      = ( state->config.rx_transfer_path == HW_I2C_TRANSFER_DMA );

    if ( use_dma )
    {
        DMA_Stream_TypeDef* rx_stream = HW_I2C_MAP[channel].dma_rx;

        HW_I2C_DMA_Stream_Clear_Flags( rx_stream );
        HW_I2C_Configure_DMA_Stream( rx_stream, HW_I2C_MAP[channel].dma_channel_bits, false,
                                     ( uint32_t )( uintptr_t )&i2c_instance->DR,
                                     ( uint32_t )( uintptr_t )state->dma_rx_linear_buffer,
                                     expected_length );
    }

    HW_I2C_Start_Master_Transfer( i2c_instance, state->transfer_kind, use_dma );

    return true;
}

/**
 * @brief Trigger a master receive operation on the internal FMPI2C1 channel.
 *
 * Initiates an I2C master receive from the specified device address on the internal
 * FMPI2C1 channel. Received data will be available via HW_I2C_Peek_Received() and consumed
 * with HW_I2C_Consume_Received(). The FMPI2C1 channel uses interrupt-based transfer path only.
 *
 * The overflow flag is cleared when this new receive transfer is armed so the next
 * overflow report only reflects the current transfer.
 *
 * @param[in] device_address_7bit   7-bit slave address to receive from
 * @param[in] expected_length       Number of bytes expected to receive
 *
 * @return true if transfer was initiated successfully
 * @return false if another transfer is already in progress
 */
inline bool HW_I2C_Trigger_Master_Receive_Internal( uint16_t device_address_7bit,
                                                    uint16_t expected_length )
{
    HWI2CChannelState_T* state = &hw_i2c_channel_state[HW_I2C_CHANNEL_FMPI2C1];

    if ( state->transfer_in_progress || expected_length > HW_I2C_RX_BUFFER_SIZE )
    {
        return false;
    }

    state->target_address_7bit    = device_address_7bit;
    state->transfer_kind          = HW_I2C_TRANSFER_KIND_MASTER_RX;
    state->transfer_in_progress   = true;
    state->rx_expected_length     = expected_length;
    state->dma_rx_expected_length = expected_length;
    state->overflow_occurred      = false;

    FMPI2C1->CR2 = ( ( uint32_t )device_address_7bit << 1U ) | FMPI2C_CR2_RD_WRN
                   | ( ( uint32_t )expected_length << FMPI2C_CR2_NBYTES_Pos ) | FMPI2C_CR2_START
                   | FMPI2C_CR2_AUTOEND;

    return true;
}

/**
 * @brief Trigger a slave transmit operation.
 *
 * Prepares the channel to transmit data in slave mode when the master
 * requests it. Data must be pre-loaded with HW_I2C_Load_Stage_Buffer().
 *
 * @param[in] channel  I2C channel
 *
 * @return true if transmit was prepared successfully
 * @return false if another transfer is already in progress
 */
bool HW_I2C_Trigger_Slave_Transmit_External( HWI2CChannel_T channel )
{
    HWI2CChannelState_T* state = &hw_i2c_channel_state[channel];

    state->transfer_kind            = HW_I2C_TRANSFER_KIND_SLAVE_TX;
    state->transfer_in_progress     = true;
    state->tx_ptr                   = state->tx_stage_buffer;
    state->tx_remaining             = state->tx_stage_length;
    state->dma_tx_transfer_complete = false;

    I2C_TypeDef* i2c_instance = HW_I2C_MAP[channel].instance;

    if ( state->config.tx_transfer_path == HW_I2C_TRANSFER_DMA )
    {
        DMA_Stream_TypeDef* tx_stream = HW_I2C_MAP[channel].dma_tx;

        HW_I2C_DMA_Stream_Clear_Flags( tx_stream );
        HW_I2C_Configure_DMA_Stream( tx_stream, HW_I2C_MAP[channel].dma_channel_bits, true,
                                     ( uint32_t )( uintptr_t )&i2c_instance->DR,
                                     ( uint32_t )( uintptr_t )state->tx_stage_buffer,
                                     state->tx_stage_length );
        HW_I2C_Prepare_DMA_Path( i2c_instance, state->transfer_kind );
    }
    else
    {
        HW_I2C_Prepare_Interrupt_Path( i2c_instance );
    }

    LL_I2C_AcknowledgeNextData( i2c_instance, LL_I2C_ACK );

    return true;
}

/**
 * @brief Trigger a slave receive operation.
 *
 * Prepares the channel to receive data in slave mode from a master.
 * Received data will be available via HW_I2C_Peek_Received() and consumed
 * with HW_I2C_Consume_Received().
 *
 * The overflow flag is cleared when this new receive transfer is armed so the next
 * overflow report only reflects the current transfer.
 *
 * @param[in] channel           I2C channel
 * @param[in] expected_length   Number of bytes expected to receive
 *
 * @return true if receive was prepared successfully
 * @return false if another transfer is already in progress
 */
bool HW_I2C_Trigger_Slave_Receive_External( HWI2CChannel_T channel, uint16_t expected_length )
{
    HWI2CChannelState_T* state = &hw_i2c_channel_state[channel];

    if ( expected_length > HW_I2C_RX_BUFFER_SIZE )
    {
        return false;
    }

    state->transfer_kind          = HW_I2C_TRANSFER_KIND_SLAVE_RX;
    state->transfer_in_progress   = true;
    state->rx_expected_length     = expected_length;
    state->dma_rx_expected_length = expected_length;
    state->overflow_occurred      = false;

    I2C_TypeDef* i2c_instance = HW_I2C_MAP[channel].instance;

    if ( state->config.rx_transfer_path == HW_I2C_TRANSFER_DMA )
    {
        DMA_Stream_TypeDef* rx_stream = HW_I2C_MAP[channel].dma_rx;

        HW_I2C_DMA_Stream_Clear_Flags( rx_stream );
        HW_I2C_Configure_DMA_Stream( rx_stream, HW_I2C_MAP[channel].dma_channel_bits, false,
                                     ( uint32_t )( uintptr_t )&i2c_instance->DR,
                                     ( uint32_t )( uintptr_t )state->dma_rx_linear_buffer,
                                     expected_length );
        HW_I2C_Prepare_DMA_Path( i2c_instance, state->transfer_kind );
    }
    else
    {
        HW_I2C_Prepare_Interrupt_Path( i2c_instance );
    }

    LL_I2C_AcknowledgeNextData( i2c_instance, LL_I2C_ACK );

    return true;
}

/**
 * @brief Peek at received data without consuming it.
 *
 * Provides zero-copy access to received data in the ring buffer via two
 * spans (first and second), which may wrap around the buffer.
 *
 * @param[in]  channel  I2C channel
 * @param[out] peek     Pointer to receive peek structure with first and second spans
 *
 * @return true on success
 * @return false on failure
 */
inline bool HW_I2C_Peek_Received( HWI2CChannel_T channel, HWI2CRxPeek_T* peek )
{
    HWI2CChannelState_T* state = &hw_i2c_channel_state[channel];

    const uint16_t count = HW_I2C_Ring_Count( state );
    peek->total_length   = count;

    if ( count == 0U )
    {
        peek->first.data    = NULL;
        peek->first.length  = 0U;
        peek->second.data   = NULL;
        peek->second.length = 0U;
        return true;
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

    return true;
}

/**
 * @brief Consume received bytes from the ring buffer.
 *
 * Advances the tail pointer of the receive ring buffer to discard
 * the specified number of bytes.
 *
 * @param[in] channel           I2C channel
 * @param[in] bytes_to_consume  Number of bytes to consume
 *
 * @return true if bytes were consumed successfully
 * @return false if bytes_to_consume exceeds available data
 */
inline bool HW_I2C_Consume_Received( HWI2CChannel_T channel, uint16_t bytes_to_consume )
{
    HWI2CChannelState_T* state = &hw_i2c_channel_state[channel];

    const uint16_t count = HW_I2C_Ring_Count( state );
    if ( bytes_to_consume > count )
    {
        return false;
    }

    state->rx_tail = ( uint16_t )( ( state->rx_tail + bytes_to_consume ) % HW_I2C_RX_BUFFER_SIZE );
    return true;
}

/**
 * @brief Check if an overflow occurred on the channel.
 *
 * Returns true if the ring buffer overflowed during the last receive transfer.
 * Once read, the flag is cleared.
 *
 * @param[in] channel  I2C channel
 *
 * @return true if overflow was detected
 * @return false if no overflow
 */
bool HW_I2C_Get_Overflow_Status( HWI2CChannel_T channel )
{
    HWI2CChannelState_T* state           = &hw_i2c_channel_state[channel];
    bool                 overflow_status = state->overflow_occurred;
    state->overflow_occurred             = false;
    return overflow_status;
}

/**
 * @brief This function handles I2C1 event interrupt.
 */
void HW_I2C_EV_IRQ_CHANNEL_1( void )
{
    HW_I2C_Service_Event_IRQ( HW_I2C_CHANNEL_1 );
}

/**
 * @brief This function handles I2C2 event interrupt.
 */
void HW_I2C_EV_IRQ_CHANNEL_2( void )
{
    HW_I2C_Service_Event_IRQ( HW_I2C_CHANNEL_2 );
}

/**
 * @brief This function handles FMPI2C1 event interrupt.
 */
void HW_I2C_EV_IRQ_FMPI2C1( void )
{
    HW_I2C_Service_Event_IRQ( HW_I2C_CHANNEL_FMPI2C1 );
}

/**
 * @brief This function handles DMA1 stream2 global interrupt.
 */
void HW_I2C_DMA_RX_IRQ_CHANNEL_2( void )
{
    HW_I2C_Service_DMA_Rx_IRQ( HW_I2C_CHANNEL_2 );
}

/**
 * @brief This function handles DMA1 stream7 global interrupt.
 */
void HW_I2C_DMA_TX_IRQ_CHANNEL_2( void )
{
    HW_I2C_Service_DMA_Tx_IRQ( HW_I2C_CHANNEL_2 );
}
