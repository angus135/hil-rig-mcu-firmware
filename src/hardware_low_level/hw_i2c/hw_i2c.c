/******************************************************************************
 *  File:       hw_i2c.c
 *  Author:     Coen Pasitchnyj
 *  Created:    20-Apr-2026
 *
 *  Description:
 *      Low-level hardware I2C driver implementation. Manages I2C peripheral
 *      configuration, state machines for master/slave operations, and interrupt/DMA
 *      service routines. Queues complete master transactions and publishes
 *      complete received messages.
 *
 *  Notes:
 *      - Requires STM32F4xx HAL/LL driver libraries
 *      - FMPI2C1 operates at 100 kHz with fixed timing register value
 *      - I2C3 interrupt-only; I2C2 supports DMA; FMPI2C1 interrupt-only
 *      - Receive storage is 512 bytes; maximum transmit message is 256 bytes
 *      - Queue state is protected against the channel's I2C/DMA interrupts
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

#define HW_I2C_EV_IRQ_CHANNEL_1 I2C3_EV_IRQHandler
#define HW_I2C_EV_IRQ_CHANNEL_2 I2C2_EV_IRQHandler
#define HW_I2C_EV_IRQ_FMPI2C1 FMPI2C1_EV_IRQHandler
#define HW_I2C_ER_IRQ_CHANNEL_1 I2C3_ER_IRQHandler
#define HW_I2C_ER_IRQ_CHANNEL_2 I2C2_ER_IRQHandler
#define HW_I2C_ER_IRQ_FMPI2C1 FMPI2C1_ER_IRQHandler
#define HW_I2C_DMA_RX_IRQ_CHANNEL_2 DMA1_Stream2_IRQHandler
#define HW_I2C_DMA_TX_IRQ_CHANNEL_2 DMA1_Stream7_IRQHandler

void HW_I2C_EV_IRQ_CHANNEL_1( void );
void HW_I2C_EV_IRQ_CHANNEL_2( void );
void HW_I2C_EV_IRQ_FMPI2C1( void );
void HW_I2C_ER_IRQ_CHANNEL_1( void );
void HW_I2C_ER_IRQ_CHANNEL_2( void );
void HW_I2C_ER_IRQ_FMPI2C1( void );
void HW_I2C_DMA_RX_IRQ_CHANNEL_2( void );
void HW_I2C_DMA_TX_IRQ_CHANNEL_2( void );

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

typedef struct HWI2CMasterTransaction_T
{
    HWI2CTransferKind_T transfer_kind;
    uint16_t            target_address_7bit;
    uint16_t            length;
    uint8_t             tx_payload[HW_I2C_TX_MAX_MESSAGE_SIZE];
} HWI2CMasterTransaction_T;

typedef struct HWI2CIrqState_T
{
    uint32_t event_irq_enabled;
    uint32_t error_irq_enabled;
    uint32_t dma_rx_irq_enabled;
    uint32_t dma_tx_irq_enabled;
} HWI2CIrqState_T;

typedef struct HWI2CChannelState_T
{
    /* Configuration state */
    bool                 is_configured; /* True if channel has been configured */
    HWI2CChannelConfig_T config;        /* Runtime configuration (mode, speed, transfer paths) */
    bool                 is_started;    /* True if channel has been started */

    /* Transfer control and state */
    volatile bool       transfer_in_progress;
    HWI2CTransferKind_T transfer_kind;
    volatile bool       master_queue_active;
    volatile bool       completion_condition_seen;
    volatile bool       restart_pending;
    bool                active_uses_dma;

    /* Complete master transactions. Count includes the active queue head. */
    HWI2CMasterTransaction_T master_queue[HW_I2C_MASTER_TRANSACTION_QUEUE_DEPTH];
    volatile uint8_t         master_queue_head;
    volatile uint8_t         master_queue_tail;
    volatile uint8_t         master_queue_count;

    /* Master-mode addressing */
    uint16_t target_address_7bit; /* 7-bit slave address for master transfers */
    uint16_t rx_expected_length;  /* Remaining interrupt-driven receive count */

    /* Non-queued slave TX compatibility storage plus active TX pointers. */
    uint8_t        slave_tx_stage_buffer[HW_I2C_TX_STAGE_SIZE];
    uint16_t       slave_tx_stage_length;
    const uint8_t* tx_ptr;
    uint16_t       tx_remaining;
    volatile bool  dma_tx_transfer_complete;

    /* One private linear staging area is shared by DMA and IRQ receive paths. */
    uint8_t       rx_staging_buffer[HW_I2C_RX_BUFFER_SIZE];
    uint16_t      rx_transfer_length;
    uint16_t      rx_received_length;
    uint16_t      dma_rx_expected_length;
    volatile bool dma_rx_transfer_complete;

    /* Complete received messages: byte storage plus parallel descriptors. */
    uint8_t                    rx_ring_buffer[HW_I2C_RX_BUFFER_SIZE];
    volatile uint16_t          rx_head;
    volatile uint16_t          rx_tail;
    volatile uint16_t          rx_count;
    HWI2CRxMessageDescriptor_T rx_message_queue[HW_I2C_RX_MESSAGE_QUEUE_DEPTH];
    volatile uint8_t           rx_message_head;
    volatile uint8_t           rx_message_tail;
    volatile uint8_t           rx_message_count;

    /* Error tracking */
    volatile bool          overflow_occurred;
    volatile HWI2CStatus_T transfer_result;
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
    { .instance = I2C3, .dma_rx = NULL, .dma_tx = NULL, .dma_channel_bits = 0UL },
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
static inline bool     HW_I2C_Channel_Is_Valid( HWI2CChannel_T channel );
static inline bool     HW_I2C_Address_Is_Valid( uint16_t device_address_7bit );
static inline bool     HW_I2C_Config_Is_Valid( HWI2CChannel_T              channel,
                                               const HWI2CChannelConfig_T* config );
static inline uint16_t HW_I2C_Ring_Count( const HWI2CChannelState_T* state );
static inline uint16_t HW_I2C_Ring_Free( const HWI2CChannelState_T* state );
static inline bool     HW_I2C_Peripheral_Is_Busy( HWI2CChannel_T channel );
static HWI2CIrqState_T HW_I2C_Channel_Irqs_Disable( HWI2CChannel_T channel );
static void HW_I2C_Channel_Irqs_Restore( HWI2CChannel_T channel, HWI2CIrqState_T irq_state );
static bool HW_I2C_Publish_Received_Message( HWI2CChannelState_T* state );

static inline uint32_t HWI2CSpeed_To_Hz( HWI2CSpeed_T speed );
static inline void     HW_I2C_Enable_Clock_For_Channel( HWI2CChannel_T channel );
static inline void     HW_I2C_Enable_Error_IRQ_For_Channel( HWI2CChannel_T channel );
static inline void     HW_I2C_Disable_All_Runtime_Irq_Bits( I2C_TypeDef* i2c_instance );
static inline void     HW_I2C_Disable_DMA_Request( I2C_TypeDef* i2c_instance );
static inline void     HW_I2C_Set_Speed_And_Address( I2C_TypeDef* i2c_instance, HWI2CSpeed_T speed,
                                                     uint16_t own_address_7bit );
static inline void     HW_I2C_Prepare_Interrupt_Path( I2C_TypeDef* i2c_instance );
static inline void     HW_I2C_Prepare_DMA_Path( I2C_TypeDef*        i2c_instance,
                                                HWI2CTransferKind_T transfer_kind );
static inline void     HW_I2C_Start_Master_Transfer( I2C_TypeDef*        i2c_instance,
                                                     HWI2CTransferKind_T transfer_kind, bool use_dma );
static void            HW_I2C_Cleanup_Active_Transfer( HWI2CChannel_T channel );
static void        HW_I2C_Latch_Transfer_Result( HWI2CChannelState_T* state, HWI2CStatus_T result );
static void        HW_I2C_Abort_Transfer( HWI2CChannel_T channel, HWI2CStatus_T result );
static void        HW_I2C_Request_Master_Stop( HWI2CChannel_T channel );
static bool        HW_I2C_Pump_Master_Queue( HWI2CChannel_T channel );
static void        HW_I2C_Complete_Master_Queue_Head( HWI2CChannel_T channel );
static void        HW_I2C_Configure_DMA_Stream( DMA_Stream_TypeDef* stream, uint32_t channel_bits,
                                                bool memory_to_peripheral, uint32_t peripheral_address,
                                                uint32_t memory_address, uint16_t length );
static inline bool HW_I2C_DMA_Stream_Has_TC( DMA_Stream_TypeDef* stream );
static inline bool HW_I2C_DMA_Stream_Has_TE( DMA_Stream_TypeDef* stream );
static inline void HW_I2C_DMA_Stream_Clear_Flags( DMA_Stream_TypeDef* stream );
static inline void HW_I2C_Service_Event_External( HWI2CChannel_T channel,
                                                  I2C_TypeDef*   i2c_instance );
static bool        HW_I2C_Read_External_Byte( HWI2CChannel_T channel, I2C_TypeDef* i2c_instance );
static inline void HW_I2C_Service_Event_FMPI2C1( HWI2CChannel_T channel );

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

static inline bool HW_I2C_Is_External_Channel( HWI2CChannel_T channel )
{
    return ( channel == HW_I2C_CHANNEL_1 ) || ( channel == HW_I2C_CHANNEL_2 );
}

static inline bool HW_I2C_Channel_Is_Valid( HWI2CChannel_T channel )
{
    return ( channel >= HW_I2C_CHANNEL_1 ) && ( channel < HW_I2C_CHANNEL_COUNT );
}

static inline bool HW_I2C_Address_Is_Valid( uint16_t device_address_7bit )
{
    return device_address_7bit <= 0x7FU;
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

    /* I2C3 does not support DMA */
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
    return state->rx_count;
}

static inline uint16_t HW_I2C_Ring_Free( const HWI2CChannelState_T* state )
{
    return ( uint16_t )( HW_I2C_RX_BUFFER_SIZE - HW_I2C_Ring_Count( state ) );
}

static inline bool HW_I2C_Peripheral_Is_Busy( HWI2CChannel_T channel )
{
    if ( channel == HW_I2C_CHANNEL_FMPI2C1 )
    {
        return ( FMPI2C1->ISR & FMPI2C_ISR_BUSY ) != 0U;
    }

    return ( HW_I2C_MAP[channel].instance->SR2 & I2C_SR2_BUSY ) != 0U;
}

static HWI2CIrqState_T HW_I2C_Channel_Irqs_Disable( HWI2CChannel_T channel )
{
    HWI2CIrqState_T irq_state = { 0 };

    switch ( channel )
    {
        case HW_I2C_CHANNEL_1:
            irq_state.event_irq_enabled = NVIC_GetEnableIRQ( I2C3_EV_IRQn );
            irq_state.error_irq_enabled = NVIC_GetEnableIRQ( I2C3_ER_IRQn );
            NVIC_DisableIRQ( I2C3_EV_IRQn );
            NVIC_DisableIRQ( I2C3_ER_IRQn );
            break;
        case HW_I2C_CHANNEL_2:
            irq_state.event_irq_enabled  = NVIC_GetEnableIRQ( I2C2_EV_IRQn );
            irq_state.error_irq_enabled  = NVIC_GetEnableIRQ( I2C2_ER_IRQn );
            irq_state.dma_rx_irq_enabled = NVIC_GetEnableIRQ( DMA1_Stream2_IRQn );
            irq_state.dma_tx_irq_enabled = NVIC_GetEnableIRQ( DMA1_Stream7_IRQn );
            NVIC_DisableIRQ( I2C2_EV_IRQn );
            NVIC_DisableIRQ( I2C2_ER_IRQn );
            NVIC_DisableIRQ( DMA1_Stream2_IRQn );
            NVIC_DisableIRQ( DMA1_Stream7_IRQn );
            break;
        case HW_I2C_CHANNEL_FMPI2C1:
            irq_state.event_irq_enabled = NVIC_GetEnableIRQ( FMPI2C1_EV_IRQn );
            irq_state.error_irq_enabled = NVIC_GetEnableIRQ( FMPI2C1_ER_IRQn );
            NVIC_DisableIRQ( FMPI2C1_EV_IRQn );
            NVIC_DisableIRQ( FMPI2C1_ER_IRQn );
            break;
        case HW_I2C_CHANNEL_COUNT:
        default:
            break;
    }

    return irq_state;
}

static void HW_I2C_Channel_Irqs_Restore( HWI2CChannel_T channel, HWI2CIrqState_T irq_state )
{
    switch ( channel )
    {
        case HW_I2C_CHANNEL_1:
            if ( irq_state.event_irq_enabled != 0U )
            {
                NVIC_EnableIRQ( I2C3_EV_IRQn );
            }
            if ( irq_state.error_irq_enabled != 0U )
            {
                NVIC_EnableIRQ( I2C3_ER_IRQn );
            }
            break;
        case HW_I2C_CHANNEL_2:
            if ( irq_state.event_irq_enabled != 0U )
            {
                NVIC_EnableIRQ( I2C2_EV_IRQn );
            }
            if ( irq_state.error_irq_enabled != 0U )
            {
                NVIC_EnableIRQ( I2C2_ER_IRQn );
            }
            if ( irq_state.dma_rx_irq_enabled != 0U )
            {
                NVIC_EnableIRQ( DMA1_Stream2_IRQn );
            }
            if ( irq_state.dma_tx_irq_enabled != 0U )
            {
                NVIC_EnableIRQ( DMA1_Stream7_IRQn );
            }
            break;
        case HW_I2C_CHANNEL_FMPI2C1:
            if ( irq_state.event_irq_enabled != 0U )
            {
                NVIC_EnableIRQ( FMPI2C1_EV_IRQn );
            }
            if ( irq_state.error_irq_enabled != 0U )
            {
                NVIC_EnableIRQ( FMPI2C1_ER_IRQn );
            }
            break;
        case HW_I2C_CHANNEL_COUNT:
        default:
            break;
    }
}

static bool HW_I2C_Publish_Received_Message( HWI2CChannelState_T* state )
{
    const uint16_t message_length = state->rx_received_length;

    if ( ( message_length > HW_I2C_Ring_Free( state ) )
         || ( state->rx_message_count >= HW_I2C_RX_MESSAGE_QUEUE_DEPTH ) )
    {
        state->overflow_occurred = true;
        HW_I2C_Latch_Transfer_Result( state, HW_I2C_STATUS_OVERFLOW );
        return false;
    }

    uint16_t new_head = state->rx_head;
    for ( uint16_t index = 0U; index < message_length; ++index )
    {
        state->rx_ring_buffer[new_head] = state->rx_staging_buffer[index];
        new_head                        = ( uint16_t )( ( new_head + 1U ) % HW_I2C_RX_BUFFER_SIZE );
    }

    HWI2CRxMessageDescriptor_T* descriptor = &state->rx_message_queue[state->rx_message_head];
    descriptor->transfer_kind              = state->transfer_kind;
    descriptor->target_address_7bit = ( state->transfer_kind == HW_I2C_TRANSFER_KIND_MASTER_RX )
                                          ? state->target_address_7bit
                                          : 0U;
    descriptor->length              = message_length;
    descriptor->status              = HW_I2C_STATUS_OK;

    state->rx_head  = new_head;
    state->rx_count = ( uint16_t )( state->rx_count + message_length );
    state->rx_message_head =
        ( uint8_t )( ( state->rx_message_head + 1U ) % HW_I2C_RX_MESSAGE_QUEUE_DEPTH );
    state->rx_message_count++;

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
            LL_APB1_GRP1_EnableClock( LL_APB1_GRP1_PERIPH_I2C3 );
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

static inline void HW_I2C_Enable_Error_IRQ_For_Channel( HWI2CChannel_T channel )
{
    switch ( channel )
    {
        case HW_I2C_CHANNEL_1:
            NVIC_EnableIRQ( I2C3_ER_IRQn );
            break;
        case HW_I2C_CHANNEL_2:
            NVIC_EnableIRQ( I2C2_ER_IRQn );
            break;
        case HW_I2C_CHANNEL_FMPI2C1:
            NVIC_EnableIRQ( FMPI2C1_ER_IRQn );
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
        if ( transfer_kind == HW_I2C_TRANSFER_KIND_MASTER_RX )
        {
            i2c_instance->CR2 |= I2C_CR2_LAST;
        }
        LL_I2C_EnableDMAReq_RX( i2c_instance );
    }
    else
    {
        i2c_instance->CR2 &= ~I2C_CR2_LAST;
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
    i2c_instance->CR1 &= ~I2C_CR1_POS;
    i2c_instance->CR2 &= ~I2C_CR2_LAST;

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

static void HW_I2C_Cleanup_Active_Transfer( HWI2CChannel_T channel )
{
    HWI2CChannelState_T* state = &hw_i2c_channel_state[channel];

    if ( HW_I2C_Is_External_Channel( channel ) )
    {
        I2C_TypeDef* i2c_instance = HW_I2C_MAP[channel].instance;

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
        HW_I2C_Disable_All_Runtime_Irq_Bits( i2c_instance );
        i2c_instance->CR1 &= ~I2C_CR1_POS;
        i2c_instance->CR2 &= ~I2C_CR2_LAST;
        LL_I2C_AcknowledgeNextData(
            i2c_instance, ( state->config.mode == HW_I2C_MODE_SLAVE ) ? LL_I2C_ACK : LL_I2C_NACK );
    }

    state->transfer_in_progress     = false;
    state->transfer_kind            = HW_I2C_TRANSFER_KIND_IDLE;
    state->master_queue_active      = false;
    state->active_uses_dma          = false;
    state->tx_remaining             = 0U;
    state->dma_tx_transfer_complete = false;
    state->rx_expected_length       = 0U;
    state->rx_transfer_length       = 0U;
    state->rx_received_length       = 0U;
    state->dma_rx_expected_length   = 0U;
    state->dma_rx_transfer_complete = false;
}

static void HW_I2C_Latch_Transfer_Result( HWI2CChannelState_T* state, HWI2CStatus_T result )
{
    if ( ( result != HW_I2C_STATUS_OK ) && ( state->transfer_result == HW_I2C_STATUS_OK ) )
    {
        state->transfer_result = result;
    }
}

static void HW_I2C_Abort_Transfer( HWI2CChannel_T channel, HWI2CStatus_T result )
{
    HWI2CIrqState_T      irq_state = HW_I2C_Channel_Irqs_Disable( channel );
    HWI2CChannelState_T* state     = &hw_i2c_channel_state[channel];

    if ( state->master_queue_active )
    {
        if ( channel == HW_I2C_CHANNEL_FMPI2C1 )
        {
            LL_FMPI2C_GenerateStopCondition( FMPI2C1 );
        }
        else
        {
            LL_I2C_GenerateStopCondition( HW_I2C_MAP[channel].instance );
        }
    }

    HW_I2C_Cleanup_Active_Transfer( channel );
    state->master_queue_head         = 0U;
    state->master_queue_tail         = 0U;
    state->master_queue_count        = 0U;
    state->restart_pending           = true;
    state->completion_condition_seen = false;
    if ( result == HW_I2C_STATUS_OVERFLOW )
    {
        state->overflow_occurred = true;
    }
    HW_I2C_Latch_Transfer_Result( state, result );

    if ( channel == HW_I2C_CHANNEL_FMPI2C1 )
    {
        LL_FMPI2C_Disable( FMPI2C1 );
        FMPI2C1->CR2 = 0U;
        LL_FMPI2C_Enable( FMPI2C1 );
    }
    else
    {
        I2C_TypeDef* i2c_instance = HW_I2C_MAP[channel].instance;
        LL_I2C_Disable( i2c_instance );
        LL_I2C_Enable( i2c_instance );
        LL_I2C_AcknowledgeNextData(
            i2c_instance, ( state->config.mode == HW_I2C_MODE_SLAVE ) ? LL_I2C_ACK : LL_I2C_NACK );
    }

    HW_I2C_Channel_Irqs_Restore( channel, irq_state );
}

static void HW_I2C_Request_Master_Stop( HWI2CChannel_T channel )
{
    HWI2CChannelState_T* state = &hw_i2c_channel_state[channel];

    if ( !state->master_queue_active )
    {
        return;
    }

    if ( channel == HW_I2C_CHANNEL_FMPI2C1 )
    {
        LL_FMPI2C_GenerateStopCondition( FMPI2C1 );
        return;
    }

    if ( !state->completion_condition_seen )
    {
        I2C_TypeDef* i2c_instance = HW_I2C_MAP[channel].instance;
        LL_I2C_GenerateStopCondition( i2c_instance );
        LL_I2C_DisableIT_EVT( i2c_instance );
        LL_I2C_DisableIT_BUF( i2c_instance );
        state->completion_condition_seen = true;
        state->restart_pending           = true;
    }
}

static bool HW_I2C_Pump_Master_Queue( HWI2CChannel_T channel )
{
    HWI2CChannelState_T* state = &hw_i2c_channel_state[channel];

    if ( state->master_queue_active || ( state->master_queue_count == 0U ) )
    {
        return true;
    }

    if ( HW_I2C_Peripheral_Is_Busy( channel ) )
    {
        state->restart_pending = true;
        return true;
    }

    HWI2CMasterTransaction_T* transaction = &state->master_queue[state->master_queue_head];

    state->target_address_7bit       = transaction->target_address_7bit;
    state->transfer_kind             = transaction->transfer_kind;
    state->transfer_in_progress      = true;
    state->master_queue_active       = true;
    state->completion_condition_seen = false;
    state->restart_pending           = false;
    state->dma_tx_transfer_complete  = false;
    state->dma_rx_transfer_complete  = false;
    state->rx_transfer_length        = 0U;
    state->rx_received_length        = 0U;
    state->rx_expected_length        = 0U;
    state->dma_rx_expected_length    = 0U;

    if ( transaction->transfer_kind == HW_I2C_TRANSFER_KIND_MASTER_TX )
    {
        state->tx_ptr       = transaction->tx_payload;
        state->tx_remaining = transaction->length;
    }
    else
    {
        state->tx_ptr                 = NULL;
        state->tx_remaining           = 0U;
        state->rx_transfer_length     = transaction->length;
        state->rx_expected_length     = transaction->length;
        state->dma_rx_expected_length = transaction->length;
    }

    if ( channel == HW_I2C_CHANNEL_FMPI2C1 )
    {
        uint32_t cr2 = ( ( uint32_t )transaction->target_address_7bit << 1U )
                       | ( ( uint32_t )transaction->length << FMPI2C_CR2_NBYTES_Pos )
                       | FMPI2C_CR2_START | FMPI2C_CR2_AUTOEND;
        if ( transaction->transfer_kind == HW_I2C_TRANSFER_KIND_MASTER_RX )
        {
            cr2 |= FMPI2C_CR2_RD_WRN;
        }
        FMPI2C1->CR2           = cr2;
        state->active_uses_dma = false;
        return true;
    }

    I2C_TypeDef* i2c_instance = HW_I2C_MAP[channel].instance;
    const bool   use_dma      = ( transaction->transfer_kind == HW_I2C_TRANSFER_KIND_MASTER_TX )
                                    ? ( ( state->config.tx_transfer_path == HW_I2C_TRANSFER_DMA )
                                 && ( transaction->length > 0U ) )
                                    : ( state->config.rx_transfer_path == HW_I2C_TRANSFER_DMA );
    state->active_uses_dma    = use_dma;

    if ( use_dma )
    {
        DMA_Stream_TypeDef* stream =
            ( transaction->transfer_kind == HW_I2C_TRANSFER_KIND_MASTER_TX )
                ? HW_I2C_MAP[channel].dma_tx
                : HW_I2C_MAP[channel].dma_rx;
        const bool memory_to_peripheral =
            transaction->transfer_kind == HW_I2C_TRANSFER_KIND_MASTER_TX;
        const uint8_t* memory =
            memory_to_peripheral ? transaction->tx_payload : state->rx_staging_buffer;

        HW_I2C_DMA_Stream_Clear_Flags( stream );
        HW_I2C_Configure_DMA_Stream( stream, HW_I2C_MAP[channel].dma_channel_bits,
                                     memory_to_peripheral,
                                     ( uint32_t )( uintptr_t )&i2c_instance->DR,
                                     ( uint32_t )( uintptr_t )memory, transaction->length );
    }

    HW_I2C_Start_Master_Transfer( i2c_instance, transaction->transfer_kind, use_dma );
    return true;
}

static void HW_I2C_Complete_Master_Queue_Head( HWI2CChannel_T channel )
{
    HWI2CChannelState_T* state = &hw_i2c_channel_state[channel];

    if ( !state->master_queue_active || ( state->master_queue_count == 0U ) )
    {
        return;
    }

    if ( ( state->transfer_kind == HW_I2C_TRANSFER_KIND_MASTER_RX )
         && !HW_I2C_Publish_Received_Message( state ) )
    {
        HW_I2C_Abort_Transfer( channel, HW_I2C_STATUS_OVERFLOW );
        return;
    }

    HW_I2C_Cleanup_Active_Transfer( channel );
    state->master_queue_head =
        ( uint8_t )( ( state->master_queue_head + 1U ) % HW_I2C_MASTER_TRANSACTION_QUEUE_DEPTH );
    state->master_queue_count--;

    if ( state->master_queue_count == 0U )
    {
        state->restart_pending           = false;
        state->completion_condition_seen = true;
    }
    else
    {
        state->restart_pending           = true;
        state->completion_condition_seen = false;
    }
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
    HWI2CChannelState_T* state = &hw_i2c_channel_state[channel];
    uint32_t             sr1   = i2c_instance->SR1;

    if ( ( sr1 & ( I2C_SR1_ARLO | I2C_SR1_BERR | I2C_SR1_OVR | I2C_SR1_TIMEOUT ) ) != 0U )
    {
        if ( ( sr1 & I2C_SR1_AF ) != 0U )
        {
            LL_I2C_ClearFlag_AF( i2c_instance );
        }
        if ( ( sr1 & I2C_SR1_ARLO ) != 0U )
        {
            LL_I2C_ClearFlag_ARLO( i2c_instance );
        }
        if ( ( sr1 & I2C_SR1_BERR ) != 0U )
        {
            LL_I2C_ClearFlag_BERR( i2c_instance );
        }
        if ( ( sr1 & I2C_SR1_OVR ) != 0U )
        {
            LL_I2C_ClearFlag_OVR( i2c_instance );
        }
        if ( ( sr1 & I2C_SR1_TIMEOUT ) != 0U )
        {
            LL_I2C_ClearSMBusFlag_TIMEOUT( i2c_instance );
        }
        HW_I2C_Abort_Transfer( channel, HW_I2C_STATUS_ERROR );
        return;
    }

    if ( ( sr1 & I2C_SR1_AF ) != 0U )
    {
        LL_I2C_ClearFlag_AF( i2c_instance );

        /* A master ends a slave-transmit transaction by NACKing the final byte.
         * Keep the slave transfer active until STOP so a subsequent response cannot
         * overwrite it before the bus transaction has physically completed. */
        if ( state->transfer_kind == HW_I2C_TRANSFER_KIND_SLAVE_TX )
        {
            return;
        }

        HW_I2C_Abort_Transfer( channel, HW_I2C_STATUS_ERROR );
        return;
    }

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
        if ( state->transfer_kind == HW_I2C_TRANSFER_KIND_MASTER_RX )
        {
            i2c_instance->CR1 &= ~I2C_CR1_POS;

            if ( state->active_uses_dma )
            {
                LL_I2C_AcknowledgeNextData( i2c_instance, LL_I2C_ACK );
                i2c_instance->CR2 |= I2C_CR2_LAST;
                LL_I2C_ClearFlag_ADDR( i2c_instance );
            }
            else if ( state->rx_expected_length == 1U )
            {
                LL_I2C_AcknowledgeNextData( i2c_instance, LL_I2C_NACK );
                LL_I2C_ClearFlag_ADDR( i2c_instance );
                LL_I2C_GenerateStopCondition( i2c_instance );
            }
            else if ( state->rx_expected_length == 2U )
            {
                LL_I2C_AcknowledgeNextData( i2c_instance, LL_I2C_NACK );
                i2c_instance->CR1 |= I2C_CR1_POS;
                LL_I2C_ClearFlag_ADDR( i2c_instance );
            }
            else
            {
                LL_I2C_AcknowledgeNextData( i2c_instance, LL_I2C_ACK );
                LL_I2C_ClearFlag_ADDR( i2c_instance );
            }
        }
        else
        {
            LL_I2C_ClearFlag_ADDR( i2c_instance );
        }
        return;
    }

    if ( state->transfer_kind == HW_I2C_TRANSFER_KIND_MASTER_TX
         || state->transfer_kind == HW_I2C_TRANSFER_KIND_SLAVE_TX )
    {
        const bool tx_dma_mode = state->active_uses_dma;

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
            HW_I2C_Request_Master_Stop( channel );
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

    if ( ( state->transfer_kind == HW_I2C_TRANSFER_KIND_MASTER_RX ) && !state->active_uses_dma )
    {
        if ( ( sr1 & I2C_SR1_BTF ) != 0U )
        {
            if ( state->rx_expected_length == 3U )
            {
                LL_I2C_DisableIT_BUF( i2c_instance );
                LL_I2C_AcknowledgeNextData( i2c_instance, LL_I2C_NACK );
                if ( !HW_I2C_Read_External_Byte( channel, i2c_instance ) )
                {
                    return;
                }
                return;
            }
            if ( state->rx_expected_length == 2U )
            {
                LL_I2C_GenerateStopCondition( i2c_instance );
                if ( !HW_I2C_Read_External_Byte( channel, i2c_instance )
                     || !HW_I2C_Read_External_Byte( channel, i2c_instance ) )
                {
                    return;
                }
                i2c_instance->CR1 &= ~I2C_CR1_POS;
                HW_I2C_Request_Master_Stop( channel );
                return;
            }
            if ( state->rx_expected_length > 3U )
            {
                if ( !HW_I2C_Read_External_Byte( channel, i2c_instance ) )
                {
                    return;
                }
                if ( state->rx_expected_length == 3U )
                {
                    LL_I2C_DisableIT_BUF( i2c_instance );
                }
                return;
            }
        }

        if ( ( sr1 & I2C_SR1_RXNE ) != 0U )
        {
            if ( state->rx_expected_length > 3U )
            {
                if ( !HW_I2C_Read_External_Byte( channel, i2c_instance ) )
                {
                    return;
                }
                if ( state->rx_expected_length == 3U )
                {
                    LL_I2C_DisableIT_BUF( i2c_instance );
                }
                return;
            }
            if ( state->rx_expected_length == 1U )
            {
                if ( !HW_I2C_Read_External_Byte( channel, i2c_instance ) )
                {
                    return;
                }
                i2c_instance->CR1 &= ~( I2C_CR1_ACK | I2C_CR1_POS );
                i2c_instance->CR2 &= ~I2C_CR2_LAST;
                HW_I2C_Request_Master_Stop( channel );
                return;
            }

            LL_I2C_DisableIT_BUF( i2c_instance );
        }
    }
    else if ( ( state->transfer_kind == HW_I2C_TRANSFER_KIND_SLAVE_RX ) && !state->active_uses_dma
              && ( ( sr1 & I2C_SR1_RXNE ) != 0U ) )
    {
        if ( !HW_I2C_Read_External_Byte( channel, i2c_instance ) )
        {
            return;
        }
    }

    if ( ( sr1 & I2C_SR1_STOPF ) != 0U )
    {
        LL_I2C_ClearFlag_STOP( i2c_instance );

        if ( state->master_queue_active )
        {
            state->completion_condition_seen = true;
            state->restart_pending           = true;
            return;
        }

        if ( state->transfer_kind == HW_I2C_TRANSFER_KIND_SLAVE_RX )
        {
            if ( state->active_uses_dma )
            {
                const uint16_t remaining = ( uint16_t )HW_I2C_MAP[channel].dma_rx->NDTR;
                state->rx_received_length =
                    ( remaining <= state->rx_transfer_length )
                        ? ( uint16_t )( state->rx_transfer_length - remaining )
                        : 0U;
            }

            if ( !HW_I2C_Publish_Received_Message( state ) )
            {
                HW_I2C_Abort_Transfer( channel, HW_I2C_STATUS_OVERFLOW );
                return;
            }
        }

        HW_I2C_Cleanup_Active_Transfer( channel );
        state->completion_condition_seen = !HW_I2C_Peripheral_Is_Busy( channel );
    }
}

static bool HW_I2C_Read_External_Byte( HWI2CChannel_T channel, I2C_TypeDef* i2c_instance )
{
    HWI2CChannelState_T* state     = &hw_i2c_channel_state[channel];
    const uint8_t        data_byte = ( uint8_t )i2c_instance->DR;

    if ( state->rx_received_length >= state->rx_transfer_length )
    {
        HW_I2C_Abort_Transfer( channel, HW_I2C_STATUS_OVERFLOW );
        return false;
    }

    state->rx_staging_buffer[state->rx_received_length] = data_byte;
    state->rx_received_length++;
    if ( state->rx_expected_length > 0U )
    {
        state->rx_expected_length--;
    }

    return true;
}

static inline void HW_I2C_Service_Event_FMPI2C1( HWI2CChannel_T channel )
{
    HWI2CChannelState_T* state = &hw_i2c_channel_state[channel];
    uint32_t             isr   = FMPI2C1->ISR;

    if ( ( isr & FMPI2C_ISR_NACKF ) != 0U )
    {
        LL_FMPI2C_ClearFlag_NACK( FMPI2C1 );
        HW_I2C_Abort_Transfer( channel, HW_I2C_STATUS_ERROR );
        return;
    }

    if ( ( isr & ( FMPI2C_ISR_BERR | FMPI2C_ISR_ARLO | FMPI2C_ISR_OVR | FMPI2C_ISR_TIMEOUT ) )
         != 0U )
    {
        LL_FMPI2C_ClearFlag_BERR( FMPI2C1 );
        LL_FMPI2C_ClearFlag_ARLO( FMPI2C1 );
        LL_FMPI2C_ClearFlag_OVR( FMPI2C1 );
        LL_FMPI2C_ClearSMBusFlag_TIMEOUT( FMPI2C1 );
        HW_I2C_Abort_Transfer( channel, HW_I2C_STATUS_ERROR );
        return;
    }

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

    if ( ( isr & FMPI2C_ISR_RXNE ) != 0U )
    {
        uint8_t data_byte = ( uint8_t )FMPI2C1->RXDR;
        if ( state->rx_received_length >= state->rx_transfer_length )
        {
            HW_I2C_Abort_Transfer( channel, HW_I2C_STATUS_OVERFLOW );
            return;
        }
        state->rx_staging_buffer[state->rx_received_length] = data_byte;
        state->rx_received_length++;
        if ( state->rx_expected_length > 0U )
        {
            state->rx_expected_length--;
        }
    }

    if ( ( isr & FMPI2C_ISR_STOPF ) != 0U )
    {
        LL_FMPI2C_ClearFlag_STOP( FMPI2C1 );
        state->completion_condition_seen = true;
        state->restart_pending           = true;
        return;
    }

    /* Transfer complete: generate STOP to terminate transfer. */
    if ( ( isr & FMPI2C_ISR_TC ) != 0U )
    {
        LL_FMPI2C_GenerateStopCondition( FMPI2C1 );
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
    /* FMPI2C1 has a different register interface; route to dedicated handler. */
    if ( channel == HW_I2C_CHANNEL_FMPI2C1 )
    {
        HW_I2C_Service_Event_FMPI2C1( channel );
        return;
    }

    /* External channels (I2C3, I2C2) route to their dedicated handler. */
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

    DMA_Stream_TypeDef* rx_stream = HW_I2C_MAP[channel].dma_rx;

    /* Check for DMA transfer error (e.g., FIFO error). */
    if ( HW_I2C_DMA_Stream_Has_TE( rx_stream ) )
    {
        HW_I2C_DMA_Stream_Clear_Flags( rx_stream );
        HW_I2C_Abort_Transfer( channel, HW_I2C_STATUS_ERROR );
        return;
    }

    /* Check for DMA transfer completion. */
    if ( HW_I2C_DMA_Stream_Has_TC( rx_stream ) )
    {
        HW_I2C_DMA_Stream_Clear_Flags( rx_stream );

        rx_stream->CR &= ~DMA_SxCR_EN;
        state->rx_received_length       = state->dma_rx_expected_length;
        state->dma_rx_transfer_complete = true;

        if ( state->transfer_kind == HW_I2C_TRANSFER_KIND_MASTER_RX )
        {
            I2C_TypeDef* i2c_instance = HW_I2C_MAP[channel].instance;
            HW_I2C_Disable_DMA_Request( i2c_instance );
            HW_I2C_Request_Master_Stop( channel );
            LL_I2C_AcknowledgeNextData( i2c_instance, LL_I2C_NACK );
            i2c_instance->CR1 &= ~I2C_CR1_POS;
            i2c_instance->CR2 &= ~I2C_CR2_LAST;
        }
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
        HW_I2C_Abort_Transfer( channel, HW_I2C_STATUS_ERROR );
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
                HW_I2C_Request_Master_Stop( channel );
            }
        }
        /* For slave transmit, mark DMA complete but don't finish yet; wait for
           the bus transaction to complete naturally. */
        else if ( state->transfer_kind == HW_I2C_TRANSFER_KIND_SLAVE_TX )
        {
            state->dma_tx_transfer_complete = true;
        }
    }
}

/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */

/**
 * @brief Configure an external I2C channel (I2C3 or I2C2).
 *
 * Applies mode, speed, address, and transfer-path configuration while leaving
 * the peripheral stopped. HW_I2C_Start_Channel() must be called before use.
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

    HWI2CChannelState_T* state        = &hw_i2c_channel_state[channel];
    I2C_TypeDef*         i2c_instance = HW_I2C_MAP[channel].instance;

    if ( i2c_instance == NULL )
    {
        return HW_I2C_STATUS_INVALID_PARAM;
    }

    if ( state->is_started || state->transfer_in_progress || state->master_queue_active
         || ( state->master_queue_count > 0U ) )
    {
        return HW_I2C_STATUS_BUSY;
    }

    memset( state, 0, sizeof( *state ) );

    state->config                    = *config;
    state->transfer_kind             = HW_I2C_TRANSFER_KIND_IDLE;
    state->completion_condition_seen = true;
    state->transfer_result           = HW_I2C_STATUS_OK;

    HW_I2C_Enable_Clock_For_Channel( channel );
    HW_I2C_Enable_Error_IRQ_For_Channel( channel );

    /*
     * This helper temporarily enables the peripheral after applying the timing
     * and address registers. Configuration finishes by disabling it below.
     */
    HW_I2C_Set_Speed_And_Address( i2c_instance, config->speed, config->own_address_7bit );

    if ( config->mode == HW_I2C_MODE_SLAVE )
    {
        LL_I2C_AcknowledgeNextData( i2c_instance, LL_I2C_ACK );
    }
    else
    {
        LL_I2C_AcknowledgeNextData( i2c_instance, LL_I2C_NACK );
    }

    HW_I2C_Disable_DMA_Request( i2c_instance );
    HW_I2C_Disable_All_Runtime_Irq_Bits( i2c_instance );
    LL_I2C_Disable( i2c_instance );

    state->is_configured = true;
    state->is_started    = false;

    return HW_I2C_STATUS_OK;
}

/**
 * @brief Configure the internal FMPI2C1 channel.
 *
 * Initializes and starts the internal FMPI2C1 channel. This infrastructure
 * channel is not controlled by the external-channel lifecycle API.
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

    HWI2CChannelState_T* state = &hw_i2c_channel_state[HW_I2C_CHANNEL_FMPI2C1];
    memset( state, 0, sizeof( *state ) );
    state->transfer_kind             = HW_I2C_TRANSFER_KIND_IDLE;
    state->completion_condition_seen = true;
    state->transfer_result           = HW_I2C_STATUS_OK;
    state->config.mode               = HW_I2C_MODE_MASTER;
    state->config.speed              = HW_I2C_SPEED_100KHZ;
    state->config.tx_transfer_path   = HW_I2C_TRANSFER_INTERRUPT;
    state->config.rx_transfer_path   = HW_I2C_TRANSFER_INTERRUPT;
    state->config.own_address_7bit   = own_address_7bit;
    HW_I2C_Enable_Clock_For_Channel( HW_I2C_CHANNEL_FMPI2C1 );
    HW_I2C_Enable_Error_IRQ_For_Channel( HW_I2C_CHANNEL_FMPI2C1 );

    LL_FMPI2C_Disable( FMPI2C1 );
    LL_FMPI2C_SetTiming( FMPI2C1, FMPI2C1_TIMINGR );
    LL_FMPI2C_SetOwnAddress1( FMPI2C1, ( uint32_t )own_address_7bit << 1U,
                              LL_FMPI2C_OWNADDRESS1_7BIT );
    LL_FMPI2C_EnableOwnAddress1( FMPI2C1 );
    LL_FMPI2C_Enable( FMPI2C1 );
    LL_FMPI2C_EnableIT_TX( FMPI2C1 );
    LL_FMPI2C_EnableIT_RX( FMPI2C1 );
    LL_FMPI2C_EnableIT_NACK( FMPI2C1 );
    LL_FMPI2C_EnableIT_TC( FMPI2C1 );
    LL_FMPI2C_EnableIT_STOP( FMPI2C1 );
    LL_FMPI2C_EnableIT_ERR( FMPI2C1 );

    state->is_configured = true;
    state->is_started    = true;
    return HW_I2C_STATUS_OK;
}

HWI2CStatus_T HW_I2C_Enqueue_Master_Transmit( HWI2CChannel_T channel, uint16_t device_address_7bit,
                                              const uint8_t* payload, uint16_t payload_length )
{
    if ( !HW_I2C_Channel_Is_Valid( channel ) || !HW_I2C_Address_Is_Valid( device_address_7bit )
         || ( payload_length == 0U ) || ( ( payload == NULL ) && ( payload_length > 0U ) )
         || ( payload_length > HW_I2C_TX_MAX_MESSAGE_SIZE )
         || ( ( channel == HW_I2C_CHANNEL_FMPI2C1 ) && ( payload_length > 255U ) ) )
    {
        return HW_I2C_STATUS_INVALID_PARAM;
    }

    HWI2CChannelState_T* state = &hw_i2c_channel_state[channel];
    if ( !state->is_configured || ( state->config.mode != HW_I2C_MODE_MASTER ) )
    {
        return HW_I2C_STATUS_NOT_CONFIGURED;
    }

    HWI2CIrqState_T irq_state = HW_I2C_Channel_Irqs_Disable( channel );
    if ( state->transfer_result != HW_I2C_STATUS_OK )
    {
        HWI2CStatus_T result = state->transfer_result;
        HW_I2C_Channel_Irqs_Restore( channel, irq_state );
        return result;
    }

    if ( state->master_queue_count >= HW_I2C_MASTER_TRANSACTION_QUEUE_DEPTH )
    {
        HW_I2C_Channel_Irqs_Restore( channel, irq_state );
        return HW_I2C_STATUS_BUSY;
    }

    HWI2CMasterTransaction_T* transaction = &state->master_queue[state->master_queue_tail];
    transaction->transfer_kind            = HW_I2C_TRANSFER_KIND_MASTER_TX;
    transaction->target_address_7bit      = device_address_7bit;
    transaction->length                   = payload_length;
    if ( payload_length > 0U )
    {
        memcpy( transaction->tx_payload, payload, ( size_t )payload_length );
    }

    state->master_queue_tail =
        ( uint8_t )( ( state->master_queue_tail + 1U ) % HW_I2C_MASTER_TRANSACTION_QUEUE_DEPTH );
    state->master_queue_count++;
    state->completion_condition_seen = false;
    ( void )HW_I2C_Pump_Master_Queue( channel );
    HW_I2C_Channel_Irqs_Restore( channel, irq_state );

    return HW_I2C_STATUS_OK;
}

HWI2CStatus_T HW_I2C_Start_Channel( HWI2CChannel_T channel )
{
    if ( !HW_I2C_Is_External_Channel( channel ) )
    {
        return HW_I2C_STATUS_INVALID_PARAM;
    }

    HWI2CChannelState_T* state = &hw_i2c_channel_state[channel];

    if ( !state->is_configured )
    {
        return HW_I2C_STATUS_NOT_CONFIGURED;
    }

    if ( state->is_started )
    {
        return HW_I2C_STATUS_BUSY;
    }

    I2C_TypeDef* i2c_instance = HW_I2C_MAP[channel].instance;

    if ( i2c_instance == NULL )
    {
        return HW_I2C_STATUS_INVALID_PARAM;
    }

    /*
     * Transfer-specific DMA requests and interrupt sources are enabled by the
     * existing transfer paths when a transaction is started.
     */
    HW_I2C_Disable_DMA_Request( i2c_instance );
    HW_I2C_Disable_All_Runtime_Irq_Bits( i2c_instance );

    LL_I2C_Enable( i2c_instance );

    state->is_started = true;

    return HW_I2C_STATUS_OK;
}

HWI2CStatus_T HW_I2C_Stop_Channel( HWI2CChannel_T channel )
{
    if ( !HW_I2C_Is_External_Channel( channel ) )
    {
        return HW_I2C_STATUS_INVALID_PARAM;
    }

    HWI2CChannelState_T* state = &hw_i2c_channel_state[channel];

    if ( !state->is_configured )
    {
        return HW_I2C_STATUS_NOT_CONFIGURED;
    }

    if ( !state->is_started )
    {
        return HW_I2C_STATUS_BUSY;
    }

    I2C_TypeDef* i2c_instance = HW_I2C_MAP[channel].instance;

    if ( i2c_instance == NULL )
    {
        return HW_I2C_STATUS_INVALID_PARAM;
    }

    HWI2CIrqState_T irq_state = HW_I2C_Channel_Irqs_Disable( channel );

    const bool has_pending_work = state->transfer_in_progress || state->master_queue_active
                                  || ( state->master_queue_count > 0U )
                                  || HW_I2C_Peripheral_Is_Busy( channel );

    if ( has_pending_work )
    {
        HW_I2C_Channel_Irqs_Restore( channel, irq_state );
        return HW_I2C_STATUS_BUSY;
    }

    /*
     * The channel is idle, so cleanup only normalizes the inactive transfer
     * state and disables any remaining DMA requests or runtime interrupts.
     * Completed receive messages and staged slave transmit data are retained.
     */
    HW_I2C_Cleanup_Active_Transfer( channel );
    LL_I2C_Disable( i2c_instance );

    state->is_started = false;

    HW_I2C_Channel_Irqs_Restore( channel, irq_state );

    return HW_I2C_STATUS_OK;
}

bool HW_I2C_Is_Channel_Configured( HWI2CChannel_T channel )
{
    if ( !HW_I2C_Is_External_Channel( channel ) )
    {
        return false;
    }

    return hw_i2c_channel_state[channel].is_configured;
}

bool HW_I2C_Is_Channel_Started( HWI2CChannel_T channel )
{
    if ( !HW_I2C_Is_External_Channel( channel ) )
    {
        return false;
    }

    return hw_i2c_channel_state[channel].is_started;
}

HWI2CStatus_T HW_I2C_Enqueue_Master_Receive( HWI2CChannel_T channel, uint16_t device_address_7bit,
                                             uint16_t expected_length )
{
    if ( !HW_I2C_Channel_Is_Valid( channel ) || !HW_I2C_Address_Is_Valid( device_address_7bit )
         || ( expected_length == 0U ) || ( expected_length > HW_I2C_RX_BUFFER_SIZE )
         || ( ( channel == HW_I2C_CHANNEL_FMPI2C1 ) && ( expected_length > 255U ) ) )
    {
        return HW_I2C_STATUS_INVALID_PARAM;
    }

    HWI2CChannelState_T* state = &hw_i2c_channel_state[channel];
    if ( !state->is_configured || ( state->config.mode != HW_I2C_MODE_MASTER ) )
    {
        return HW_I2C_STATUS_NOT_CONFIGURED;
    }

    HWI2CIrqState_T irq_state = HW_I2C_Channel_Irqs_Disable( channel );
    if ( state->transfer_result != HW_I2C_STATUS_OK )
    {
        HWI2CStatus_T result = state->transfer_result;
        HW_I2C_Channel_Irqs_Restore( channel, irq_state );
        return result;
    }

    if ( state->master_queue_count >= HW_I2C_MASTER_TRANSACTION_QUEUE_DEPTH )
    {
        HW_I2C_Channel_Irqs_Restore( channel, irq_state );
        return HW_I2C_STATUS_BUSY;
    }

    uint32_t reserved_rx_bytes       = state->rx_count;
    uint32_t reserved_rx_descriptors = state->rx_message_count;
    uint8_t  queue_index             = state->master_queue_head;
    for ( uint8_t index = 0U; index < state->master_queue_count; ++index )
    {
        const HWI2CMasterTransaction_T* queued = &state->master_queue[queue_index];
        if ( queued->transfer_kind == HW_I2C_TRANSFER_KIND_MASTER_RX )
        {
            reserved_rx_bytes += queued->length;
            reserved_rx_descriptors++;
        }
        queue_index = ( uint8_t )( ( queue_index + 1U ) % HW_I2C_MASTER_TRANSACTION_QUEUE_DEPTH );
    }

    reserved_rx_bytes += expected_length;
    reserved_rx_descriptors++;
    if ( ( reserved_rx_bytes > HW_I2C_RX_BUFFER_SIZE )
         || ( reserved_rx_descriptors > HW_I2C_RX_MESSAGE_QUEUE_DEPTH ) )
    {
        HW_I2C_Channel_Irqs_Restore( channel, irq_state );
        return HW_I2C_STATUS_BUSY;
    }

    HWI2CMasterTransaction_T* transaction = &state->master_queue[state->master_queue_tail];
    transaction->transfer_kind            = HW_I2C_TRANSFER_KIND_MASTER_RX;
    transaction->target_address_7bit      = device_address_7bit;
    transaction->length                   = expected_length;

    state->master_queue_tail =
        ( uint8_t )( ( state->master_queue_tail + 1U ) % HW_I2C_MASTER_TRANSACTION_QUEUE_DEPTH );
    state->master_queue_count++;
    state->completion_condition_seen = false;
    ( void )HW_I2C_Pump_Master_Queue( channel );
    HW_I2C_Channel_Irqs_Restore( channel, irq_state );

    return HW_I2C_STATUS_OK;
}

void HW_I2C_Service_Transaction_Queue( HWI2CChannel_T channel )
{
    if ( !HW_I2C_Channel_Is_Valid( channel ) )
    {
        return;
    }

    HWI2CIrqState_T      irq_state = HW_I2C_Channel_Irqs_Disable( channel );
    HWI2CChannelState_T* state     = &hw_i2c_channel_state[channel];
    const bool           bus_idle  = !HW_I2C_Peripheral_Is_Busy( channel );

    if ( state->master_queue_active && state->completion_condition_seen && bus_idle )
    {
        HW_I2C_Complete_Master_Queue_Head( channel );
    }

    if ( !state->master_queue_active && ( state->master_queue_count > 0U )
         && !HW_I2C_Peripheral_Is_Busy( channel ) )
    {
        ( void )HW_I2C_Pump_Master_Queue( channel );
    }

    if ( ( state->master_queue_count == 0U ) && !state->transfer_in_progress
         && !HW_I2C_Peripheral_Is_Busy( channel ) )
    {
        state->restart_pending           = false;
        state->completion_condition_seen = true;
    }

    HW_I2C_Channel_Irqs_Restore( channel, irq_state );
}

bool HW_I2C_Is_Transaction_Queue_Complete( HWI2CChannel_T channel )
{
    if ( !HW_I2C_Channel_Is_Valid( channel ) )
    {
        return false;
    }

    HW_I2C_Service_Transaction_Queue( channel );
    HWI2CIrqState_T      irq_state   = HW_I2C_Channel_Irqs_Disable( channel );
    HWI2CChannelState_T* state       = &hw_i2c_channel_state[channel];
    const bool           is_complete = state->is_configured && ( state->master_queue_count == 0U )
                             && !state->master_queue_active && !state->transfer_in_progress
                             && !HW_I2C_Peripheral_Is_Busy( channel )
                             && state->completion_condition_seen;
    HW_I2C_Channel_Irqs_Restore( channel, irq_state );
    return is_complete;
}

HWI2CStatus_T HW_I2C_Get_And_Clear_Transfer_Result( HWI2CChannel_T channel )
{
    if ( !HW_I2C_Channel_Is_Valid( channel ) )
    {
        return HW_I2C_STATUS_INVALID_PARAM;
    }

    HWI2CIrqState_T      irq_state = HW_I2C_Channel_Irqs_Disable( channel );
    HWI2CChannelState_T* state     = &hw_i2c_channel_state[channel];
    HWI2CStatus_T        result    = state->transfer_result;
    state->transfer_result         = HW_I2C_STATUS_OK;
    HW_I2C_Channel_Irqs_Restore( channel, irq_state );
    return result;
}

HWI2CStatus_T HW_I2C_Recover_Channel( HWI2CChannel_T channel )
{
    if ( !HW_I2C_Channel_Is_Valid( channel ) )
    {
        return HW_I2C_STATUS_INVALID_PARAM;
    }

    HWI2CChannelState_T* state = &hw_i2c_channel_state[channel];

    if ( !state->is_configured )
    {
        return HW_I2C_STATUS_NOT_CONFIGURED;
    }

    const bool was_started = state->is_started;

    HWI2CIrqState_T irq_state = HW_I2C_Channel_Irqs_Disable( channel );

    if ( state->transfer_in_progress || state->master_queue_active )
    {
        if ( channel == HW_I2C_CHANNEL_FMPI2C1 )
        {
            LL_FMPI2C_GenerateStopCondition( FMPI2C1 );
        }
        else
        {
            LL_I2C_GenerateStopCondition( HW_I2C_MAP[channel].instance );
        }
    }

    HW_I2C_Cleanup_Active_Transfer( channel );
    memset( state->master_queue, 0, sizeof( state->master_queue ) );
    state->master_queue_head         = 0U;
    state->master_queue_tail         = 0U;
    state->master_queue_count        = 0U;
    state->target_address_7bit       = 0U;
    state->tx_ptr                    = NULL;
    state->restart_pending           = false;
    state->completion_condition_seen = true;

    memset( state->rx_staging_buffer, 0, sizeof( state->rx_staging_buffer ) );
    memset( state->rx_ring_buffer, 0, sizeof( state->rx_ring_buffer ) );
    memset( state->rx_message_queue, 0, sizeof( state->rx_message_queue ) );
    state->rx_head           = 0U;
    state->rx_tail           = 0U;
    state->rx_count          = 0U;
    state->rx_message_head   = 0U;
    state->rx_message_tail   = 0U;
    state->rx_message_count  = 0U;
    state->overflow_occurred = false;
    state->transfer_result   = HW_I2C_STATUS_ERROR;

    if ( channel == HW_I2C_CHANNEL_FMPI2C1 )
    {
        LL_FMPI2C_Disable( FMPI2C1 );
        FMPI2C1->CR1 = 0U;
        FMPI2C1->CR2 = 0U;
        LL_FMPI2C_SetTiming( FMPI2C1, FMPI2C1_TIMINGR );
        LL_FMPI2C_SetOwnAddress1( FMPI2C1, ( uint32_t )state->config.own_address_7bit << 1U,
                                  LL_FMPI2C_OWNADDRESS1_7BIT );
        LL_FMPI2C_EnableOwnAddress1( FMPI2C1 );
        LL_FMPI2C_Enable( FMPI2C1 );
        LL_FMPI2C_EnableIT_TX( FMPI2C1 );
        LL_FMPI2C_EnableIT_RX( FMPI2C1 );
        LL_FMPI2C_EnableIT_NACK( FMPI2C1 );
        LL_FMPI2C_EnableIT_TC( FMPI2C1 );
        LL_FMPI2C_EnableIT_STOP( FMPI2C1 );
        LL_FMPI2C_EnableIT_ERR( FMPI2C1 );
    }
    else
    {
        I2C_TypeDef* i2c_instance = HW_I2C_MAP[channel].instance;
        LL_I2C_Disable( i2c_instance );
        i2c_instance->CR1 &= ~( I2C_CR1_START | I2C_CR1_STOP | I2C_CR1_ACK );
        HW_I2C_Set_Speed_And_Address( i2c_instance, state->config.speed,
                                      state->config.own_address_7bit );
        LL_I2C_AcknowledgeNextData(
            i2c_instance, ( state->config.mode == HW_I2C_MODE_SLAVE ) ? LL_I2C_ACK : LL_I2C_NACK );
        HW_I2C_Disable_DMA_Request( i2c_instance );
        HW_I2C_Disable_All_Runtime_Irq_Bits( i2c_instance );

        if ( HW_I2C_MAP[channel].dma_rx != NULL )
        {
            HW_I2C_DMA_Stream_Clear_Flags( HW_I2C_MAP[channel].dma_rx );
        }
        if ( HW_I2C_MAP[channel].dma_tx != NULL )
        {
            HW_I2C_DMA_Stream_Clear_Flags( HW_I2C_MAP[channel].dma_tx );
        }

        if ( !was_started )
        {
            LL_I2C_Disable( i2c_instance );
        }
    }

    HW_I2C_Channel_Irqs_Restore( channel, irq_state );
    return HW_I2C_STATUS_ERROR;
}

inline bool HW_I2C_Load_Stage_Buffer( HWI2CChannel_T channel, const uint8_t* data, uint16_t length )
{
    if ( !HW_I2C_Channel_Is_Valid( channel ) || ( length > HW_I2C_TX_STAGE_SIZE )
         || ( ( data == NULL ) && ( length > 0U ) ) )
    {
        return false;
    }

    HWI2CIrqState_T      irq_state = HW_I2C_Channel_Irqs_Disable( channel );
    HWI2CChannelState_T* state     = &hw_i2c_channel_state[channel];
    if ( state->transfer_in_progress )
    {
        HW_I2C_Channel_Irqs_Restore( channel, irq_state );
        return false;
    }

    if ( length > 0U )
    {
        memcpy( state->slave_tx_stage_buffer, data, ( size_t )length );
    }
    state->slave_tx_stage_length = length;
    HW_I2C_Channel_Irqs_Restore( channel, irq_state );
    return true;
}

bool HW_I2C_Trigger_Master_Transmit_External( HWI2CChannel_T channel, uint16_t device_address_7bit )
{
    if ( !HW_I2C_Is_External_Channel( channel ) )
    {
        return false;
    }

    HWI2CChannelState_T* state = &hw_i2c_channel_state[channel];
    return HW_I2C_Enqueue_Master_Transmit( channel, device_address_7bit,
                                           state->slave_tx_stage_buffer,
                                           state->slave_tx_stage_length )
           == HW_I2C_STATUS_OK;
}

inline bool HW_I2C_Trigger_Master_Transmit_Internal( uint16_t device_address_7bit )
{
    HWI2CChannelState_T* state = &hw_i2c_channel_state[HW_I2C_CHANNEL_FMPI2C1];
    return HW_I2C_Enqueue_Master_Transmit( HW_I2C_CHANNEL_FMPI2C1, device_address_7bit,
                                           state->slave_tx_stage_buffer,
                                           state->slave_tx_stage_length )
           == HW_I2C_STATUS_OK;
}

bool HW_I2C_Trigger_Master_Receive_External( HWI2CChannel_T channel, uint16_t device_address_7bit,
                                             uint16_t expected_length )
{
    if ( !HW_I2C_Is_External_Channel( channel ) )
    {
        return false;
    }
    return HW_I2C_Enqueue_Master_Receive( channel, device_address_7bit, expected_length )
           == HW_I2C_STATUS_OK;
}

inline bool HW_I2C_Trigger_Master_Receive_Internal( uint16_t device_address_7bit,
                                                    uint16_t expected_length )
{
    return HW_I2C_Enqueue_Master_Receive( HW_I2C_CHANNEL_FMPI2C1, device_address_7bit,
                                          expected_length )
           == HW_I2C_STATUS_OK;
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
    if ( !HW_I2C_Is_External_Channel( channel ) )
    {
        return false;
    }

    HWI2CIrqState_T      irq_state = HW_I2C_Channel_Irqs_Disable( channel );
    HWI2CChannelState_T* state     = &hw_i2c_channel_state[channel];
    if ( !state->is_configured || ( state->config.mode != HW_I2C_MODE_SLAVE )
         || state->transfer_in_progress || ( state->master_queue_count > 0U ) )
    {
        HW_I2C_Channel_Irqs_Restore( channel, irq_state );
        return false;
    }

    state->transfer_kind            = HW_I2C_TRANSFER_KIND_SLAVE_TX;
    state->transfer_in_progress     = true;
    state->tx_ptr                   = state->slave_tx_stage_buffer;
    state->tx_remaining             = state->slave_tx_stage_length;
    state->dma_tx_transfer_complete = false;
    state->active_uses_dma          = ( state->config.tx_transfer_path == HW_I2C_TRANSFER_DMA )
                             && ( state->slave_tx_stage_length > 0U );

    I2C_TypeDef* i2c_instance = HW_I2C_MAP[channel].instance;

    if ( state->active_uses_dma )
    {
        DMA_Stream_TypeDef* tx_stream = HW_I2C_MAP[channel].dma_tx;

        HW_I2C_DMA_Stream_Clear_Flags( tx_stream );
        HW_I2C_Configure_DMA_Stream( tx_stream, HW_I2C_MAP[channel].dma_channel_bits, true,
                                     ( uint32_t )( uintptr_t )&i2c_instance->DR,
                                     ( uint32_t )( uintptr_t )state->slave_tx_stage_buffer,
                                     state->slave_tx_stage_length );
        HW_I2C_Prepare_DMA_Path( i2c_instance, state->transfer_kind );
    }
    else
    {
        HW_I2C_Prepare_Interrupt_Path( i2c_instance );
    }

    LL_I2C_AcknowledgeNextData( i2c_instance, LL_I2C_ACK );
    HW_I2C_Channel_Irqs_Restore( channel, irq_state );

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
    if ( !HW_I2C_Is_External_Channel( channel ) || ( expected_length == 0U )
         || ( expected_length > HW_I2C_RX_BUFFER_SIZE ) )
    {
        return false;
    }

    HWI2CIrqState_T      irq_state = HW_I2C_Channel_Irqs_Disable( channel );
    HWI2CChannelState_T* state     = &hw_i2c_channel_state[channel];
    if ( !state->is_configured || ( state->config.mode != HW_I2C_MODE_SLAVE )
         || state->transfer_in_progress || ( state->master_queue_count > 0U ) )
    {
        HW_I2C_Channel_Irqs_Restore( channel, irq_state );
        return false;
    }

    state->transfer_kind            = HW_I2C_TRANSFER_KIND_SLAVE_RX;
    state->transfer_in_progress     = true;
    state->rx_transfer_length       = expected_length;
    state->rx_received_length       = 0U;
    state->rx_expected_length       = expected_length;
    state->dma_rx_expected_length   = expected_length;
    state->dma_rx_transfer_complete = false;
    state->overflow_occurred        = false;
    state->active_uses_dma          = state->config.rx_transfer_path == HW_I2C_TRANSFER_DMA;

    I2C_TypeDef* i2c_instance = HW_I2C_MAP[channel].instance;

    if ( state->active_uses_dma )
    {
        DMA_Stream_TypeDef* rx_stream = HW_I2C_MAP[channel].dma_rx;

        HW_I2C_DMA_Stream_Clear_Flags( rx_stream );
        HW_I2C_Configure_DMA_Stream( rx_stream, HW_I2C_MAP[channel].dma_channel_bits, false,
                                     ( uint32_t )( uintptr_t )&i2c_instance->DR,
                                     ( uint32_t )( uintptr_t )state->rx_staging_buffer,
                                     expected_length );
        HW_I2C_Prepare_DMA_Path( i2c_instance, state->transfer_kind );
    }
    else
    {
        HW_I2C_Prepare_Interrupt_Path( i2c_instance );
    }

    LL_I2C_AcknowledgeNextData( i2c_instance, LL_I2C_ACK );
    HW_I2C_Channel_Irqs_Restore( channel, irq_state );

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
bool HW_I2C_Peek_Received_Message( HWI2CChannel_T channel, HWI2CRxMessagePeek_T* message )
{
    if ( !HW_I2C_Channel_Is_Valid( channel ) || ( message == NULL ) )
    {
        return false;
    }

    HWI2CIrqState_T      irq_state = HW_I2C_Channel_Irqs_Disable( channel );
    HWI2CChannelState_T* state     = &hw_i2c_channel_state[channel];

    if ( state->rx_message_count == 0U )
    {
        memset( message, 0, sizeof( *message ) );
        message->descriptor.transfer_kind = HW_I2C_TRANSFER_KIND_IDLE;
        message->descriptor.status        = HW_I2C_STATUS_OK;
        HW_I2C_Channel_Irqs_Restore( channel, irq_state );
        return true;
    }

    message->descriptor           = state->rx_message_queue[state->rx_message_tail];
    const uint16_t message_length = message->descriptor.length;
    const uint16_t first_length =
        ( message_length < ( uint16_t )( HW_I2C_RX_BUFFER_SIZE - state->rx_tail ) )
            ? message_length
            : ( uint16_t )( HW_I2C_RX_BUFFER_SIZE - state->rx_tail );
    const uint16_t second_length = ( uint16_t )( message_length - first_length );

    if ( first_length > 0U )
    {
        message->first.data   = &state->rx_ring_buffer[state->rx_tail];
        message->first.length = first_length;
    }
    else
    {
        message->first.data   = NULL;
        message->first.length = 0U;
    }

    if ( second_length > 0U )
    {
        message->second.data   = &state->rx_ring_buffer[0];
        message->second.length = second_length;
    }
    else
    {
        message->second.data   = NULL;
        message->second.length = 0U;
    }

    HW_I2C_Channel_Irqs_Restore( channel, irq_state );
    return true;
}

bool HW_I2C_Consume_Received_Message( HWI2CChannel_T channel )
{
    if ( !HW_I2C_Channel_Is_Valid( channel ) )
    {
        return false;
    }

    HWI2CIrqState_T      irq_state = HW_I2C_Channel_Irqs_Disable( channel );
    HWI2CChannelState_T* state     = &hw_i2c_channel_state[channel];

    if ( state->rx_message_count == 0U )
    {
        HW_I2C_Channel_Irqs_Restore( channel, irq_state );
        return false;
    }

    const uint16_t message_length = state->rx_message_queue[state->rx_message_tail].length;
    state->rx_tail  = ( uint16_t )( ( state->rx_tail + message_length ) % HW_I2C_RX_BUFFER_SIZE );
    state->rx_count = ( uint16_t )( state->rx_count - message_length );
    state->rx_message_tail =
        ( uint8_t )( ( state->rx_message_tail + 1U ) % HW_I2C_RX_MESSAGE_QUEUE_DEPTH );
    state->rx_message_count--;

    HW_I2C_Channel_Irqs_Restore( channel, irq_state );
    return true;
}

inline bool HW_I2C_Peek_Received( HWI2CChannel_T channel, HWI2CRxPeek_T* peek )
{
    if ( peek == NULL )
    {
        return false;
    }

    HWI2CRxMessagePeek_T message;
    memset( &message, 0, sizeof( message ) );
    if ( !HW_I2C_Peek_Received_Message( channel, &message ) )
    {
        return false;
    }

    peek->first        = message.first;
    peek->second       = message.second;
    peek->total_length = message.descriptor.length;
    return true;
}

inline bool HW_I2C_Consume_Received( HWI2CChannel_T channel, uint16_t bytes_to_consume )
{
    if ( !HW_I2C_Channel_Is_Valid( channel ) )
    {
        return false;
    }

    HWI2CIrqState_T      irq_state = HW_I2C_Channel_Irqs_Disable( channel );
    HWI2CChannelState_T* state     = &hw_i2c_channel_state[channel];
    if ( state->rx_message_count == 0U )
    {
        HW_I2C_Channel_Irqs_Restore( channel, irq_state );
        return bytes_to_consume == 0U;
    }

    const bool exact_message =
        bytes_to_consume == state->rx_message_queue[state->rx_message_tail].length;
    HW_I2C_Channel_Irqs_Restore( channel, irq_state );
    return exact_message ? HW_I2C_Consume_Received_Message( channel ) : false;
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
    if ( !HW_I2C_Channel_Is_Valid( channel ) )
    {
        return false;
    }

    HWI2CIrqState_T      irq_state       = HW_I2C_Channel_Irqs_Disable( channel );
    HWI2CChannelState_T* state           = &hw_i2c_channel_state[channel];
    bool                 overflow_status = state->overflow_occurred;
    state->overflow_occurred             = false;
    if ( overflow_status && ( state->transfer_result == HW_I2C_STATUS_OVERFLOW ) )
    {
        state->transfer_result = HW_I2C_STATUS_OK;
    }
    HW_I2C_Channel_Irqs_Restore( channel, irq_state );
    return overflow_status;
}

/**
 * @brief This function handles I2C3 event interrupt.
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

void HW_I2C_ER_IRQ_CHANNEL_1( void )
{
    HW_I2C_Service_Event_IRQ( HW_I2C_CHANNEL_1 );
}

void HW_I2C_ER_IRQ_CHANNEL_2( void )
{
    HW_I2C_Service_Event_IRQ( HW_I2C_CHANNEL_2 );
}

void HW_I2C_ER_IRQ_FMPI2C1( void )
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
