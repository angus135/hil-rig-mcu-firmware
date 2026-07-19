/******************************************************************************
 *  File:       hw_can.c
 *  Author:     HIL-RIG Firmware Team
 *  Created:    4-Apr-2026
 *
 *  Description:
 *      Low-level driver for the two STM32F446 bxCAN peripherals.
 *
 *      Configuration uses the STM32 HAL because it runs outside the execution
 *      hot path. Runtime transmit, receive and error interrupt paths use direct
 *      register access so their work is fixed, allocation-free and bounded by
 *      the three bxCAN transmit mailboxes or three-entry receive FIFO.
 *
 *  Notes:
 *      Cube-generated MSP code remains responsible for clocks, pins, generated
 *      HAL handles and NVIC setup. This file owns bit timing, filters, queues,
 *      runtime notification masks and the custom interrupt-vector bodies.
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include "hw_can.h"

#ifndef TEST_BUILD
#include "can.h"
#include "stm32f4xx_hal_can.h"
#include "stm32f446xx.h"
#else
#include "tests/hw_can_mocks.h"
#endif

#include <stddef.h>
#include <string.h>

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */

#define HW_CAN_TX_QUEUE_INDEX_MASK ( HW_CAN_TX_QUEUE_DEPTH_FRAMES - 1U )
#define HW_CAN_RX_QUEUE_INDEX_MASK ( HW_CAN_RX_QUEUE_DEPTH_FRAMES - 1U )

#define HW_CAN_TX_MAILBOX_COUNT 3U
#define HW_CAN_RX_FIFO0_DEPTH 3U
#define HW_CAN_ABORT_TIMEOUT_ITERATIONS 1000U

#define HW_CAN_FILTER_BANK_FIRST_CAN1 0U
#define HW_CAN_FILTER_BANK_FIRST_CAN2 HW_CAN_FILTER_BANK_SPLIT

#define HW_CAN_CH1_TX_IRQ_HANDLER CAN1_TX_IRQHandler
#define HW_CAN_CH1_RX_IRQ_HANDLER CAN1_RX0_IRQHandler
#define HW_CAN_CH1_SCE_IRQ_HANDLER CAN1_SCE_IRQHandler
#define HW_CAN_CH2_TX_IRQ_HANDLER CAN2_TX_IRQHandler
#define HW_CAN_CH2_RX_IRQ_HANDLER CAN2_RX0_IRQHandler
#define HW_CAN_CH2_SCE_IRQ_HANDLER CAN2_SCE_IRQHandler

#define HW_CAN_RUNTIME_NOTIFICATION_MASK                                                           \
    ( CAN_IT_RX_FIFO0_MSG_PENDING | CAN_IT_RX_FIFO0_FULL | CAN_IT_RX_FIFO0_OVERRUN                 \
      | CAN_IT_ERROR_WARNING | CAN_IT_ERROR_PASSIVE | CAN_IT_BUSOFF | CAN_IT_LAST_ERROR_CODE       \
      | CAN_IT_ERROR )

#define HW_CAN_ALL_NOTIFICATION_MASK ( HW_CAN_RUNTIME_NOTIFICATION_MASK | CAN_IT_TX_MAILBOX_EMPTY )

#if defined( __GNUC__ )
#define HW_CAN_ALWAYS_INLINE static inline __attribute__( ( always_inline ) )
#else
#define HW_CAN_ALWAYS_INLINE static inline
#endif

#ifdef __cplusplus
#define HW_CAN_STATIC_ASSERT( condition, message ) static_assert( condition, message )
#else
#define HW_CAN_STATIC_ASSERT( condition, message ) _Static_assert( condition, message )
#endif

HW_CAN_STATIC_ASSERT( ( HW_CAN_TX_QUEUE_DEPTH_FRAMES & ( HW_CAN_TX_QUEUE_DEPTH_FRAMES - 1U ) )
                          == 0U,
                      "CAN TX queue depth must be a power of two" );
HW_CAN_STATIC_ASSERT( ( HW_CAN_RX_QUEUE_DEPTH_FRAMES & ( HW_CAN_RX_QUEUE_DEPTH_FRAMES - 1U ) )
                          == 0U,
                      "CAN RX queue depth must be a power of two" );
HW_CAN_STATIC_ASSERT( HW_CAN_FILTER_BANK_SPLIT == HW_CAN_MAX_FILTERS_PER_CHANNEL,
                      "Fixed CAN filter partition and per-channel maximum must agree" );

#ifndef TEST_BUILD
#define HW_CAN_WRITE_TSR( instance, value ) ( ( instance )->TSR = ( value ) )
#define HW_CAN_WRITE_RF0R( instance, value ) ( ( instance )->RF0R = ( value ) )
#define HW_CAN_WRITE_MSR( instance, value ) ( ( instance )->MSR = ( value ) )
#else
#define HW_CAN_WRITE_TSR( instance, value ) HW_CAN_Mock_Write_TSR( ( instance ), ( value ) )
#define HW_CAN_WRITE_RF0R( instance, value ) HW_CAN_Mock_Write_RF0R( ( instance ), ( value ) )
#define HW_CAN_WRITE_MSR( instance, value ) HW_CAN_Mock_Write_MSR( ( instance ), ( value ) )
#endif

/**-----------------------------------------------------------------------------
 *  Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

/**
 * @brief Validated bxCAN timing values produced during configuration.
 */
typedef struct HwCanBitTiming_T
{
    uint32_t actual_bitrate;
    uint16_t sample_point_permill;
    uint16_t bitrate_error_ppm;
    uint16_t prescaler;
    uint8_t  bit_segment_1_tq;
    uint8_t  bit_segment_2_tq;
    uint8_t  sync_jump_width_tq;
} HwCanBitTiming_T;

/**
 * @brief Pre-encoded transmit-mailbox values stored in the TX software queue.
 *
 * Frame validation, identifier shifting and payload packing occur in foreground
 * code. The ISR can therefore transfer one slot into a hardware mailbox using
 * four fixed register writes and then set TXRQ.
 */
typedef struct HwCanTxQueueSlot_T
{
    uint32_t tir_without_tx_request;
    uint32_t tdtr;
    uint32_t tdlr;
    uint32_t tdhr;
} HwCanTxQueueSlot_T;

/**
 * @brief Monotonic counters and latched fault state for one channel.
 */
typedef struct HwCanDiagnostics_T
{
    volatile HwCanFaultFlags_T latched_faults;
    volatile HwCanLastError_T  last_error;

    volatile uint32_t tx_frames_queued;
    volatile uint32_t tx_frames_rejected;
    volatile uint32_t tx_frames_submitted;
    volatile uint32_t tx_frames_succeeded;
    volatile uint32_t tx_arbitration_losses;
    volatile uint32_t tx_errors;
    volatile uint32_t tx_aborted;

    volatile uint32_t rx_frames_received;
    volatile uint32_t rx_software_drops;
    volatile uint32_t rx_fifo_full_events;
    volatile uint32_t rx_fifo_overruns;

    volatile uint32_t error_warning_events;
    volatile uint32_t error_passive_events;
    volatile uint32_t bus_off_events;
} HwCanDiagnostics_T;

/**
 * @brief All mutable software state owned by one CAN channel.
 *
 * TX has one execution-context producer and one CAN-TX-ISR consumer. RX has one
 * CAN-RX-ISR producer and one execution-context consumer. Monotonic counters
 * allow all queue slots to be used while power-of-two masks make indexing cheap.
 */
typedef struct HwCanChannelState_T
{
    volatile HwCanState_T state;

    // HAL start/stop completion is tracked separately from API usability. A
    // failed rollback can leave bxCAN physically listening while the public
    // lifecycle state is FAULT; retaining this fact makes the next cold-path
    // stop, recovery or deconfiguration retry the hardware shutdown.
    bool hardware_started;

    // Recovery is permitted only after a complete timing/filter configuration
    // has been stored. This prevents a failed first configuration from using
    // zero-valued timing fields as array indices during a later recovery call.
    bool configuration_valid;

    HwCanConfig_T    configuration;
    HwCanFilter_T    filters[HW_CAN_MAX_FILTERS_PER_CHANNEL];
    HwCanBitTiming_T timing;

    HwCanTxQueueSlot_T tx_queue[HW_CAN_TX_QUEUE_DEPTH_FRAMES];
    volatile uint32_t  tx_head;
    volatile uint32_t  tx_tail;
    volatile uint8_t   tx_in_flight_mask;

    HwCanRxFrame_T    rx_queue[HW_CAN_RX_QUEUE_DEPTH_FRAMES];
    volatile uint32_t rx_head;
    volatile uint32_t rx_tail;

    HwCanDiagnostics_T diagnostics;
} HwCanChannelState_T;

/**
 * @brief Fixed Cube-generated resource mapping for one logical channel.
 */
typedef struct HwCanHardwareMap_T
{
    CAN_HandleTypeDef* handle;
    IRQn_Type          tx_irqn;
    IRQn_Type          rx_irqn;
    IRQn_Type          sce_irqn;
    uint8_t            first_filter_bank;
} HwCanHardwareMap_T;

/**-----------------------------------------------------------------------------
 *  Private Constant Data
 *------------------------------------------------------------------------------
 */

static const uint32_t HW_CAN_TME_MASKS[HW_CAN_TX_MAILBOX_COUNT] = {
    CAN_TSR_TME0,
    CAN_TSR_TME1,
    CAN_TSR_TME2,
};

static const uint32_t HW_CAN_RQCP_MASKS[HW_CAN_TX_MAILBOX_COUNT] = {
    CAN_TSR_RQCP0,
    CAN_TSR_RQCP1,
    CAN_TSR_RQCP2,
};

static const uint32_t HW_CAN_TXOK_MASKS[HW_CAN_TX_MAILBOX_COUNT] = {
    CAN_TSR_TXOK0,
    CAN_TSR_TXOK1,
    CAN_TSR_TXOK2,
};

static const uint32_t HW_CAN_ALST_MASKS[HW_CAN_TX_MAILBOX_COUNT] = {
    CAN_TSR_ALST0,
    CAN_TSR_ALST1,
    CAN_TSR_ALST2,
};

static const uint32_t HW_CAN_TERR_MASKS[HW_CAN_TX_MAILBOX_COUNT] = {
    CAN_TSR_TERR0,
    CAN_TSR_TERR1,
    CAN_TSR_TERR2,
};

static const uint32_t HW_CAN_ABRQ_MASKS[HW_CAN_TX_MAILBOX_COUNT] = {
    CAN_TSR_ABRQ0,
    CAN_TSR_ABRQ1,
    CAN_TSR_ABRQ2,
};

static const uint32_t HW_CAN_BS1_HAL_VALUES[16] = {
    CAN_BS1_1TQ,  CAN_BS1_2TQ,  CAN_BS1_3TQ,  CAN_BS1_4TQ,  CAN_BS1_5TQ,  CAN_BS1_6TQ,
    CAN_BS1_7TQ,  CAN_BS1_8TQ,  CAN_BS1_9TQ,  CAN_BS1_10TQ, CAN_BS1_11TQ, CAN_BS1_12TQ,
    CAN_BS1_13TQ, CAN_BS1_14TQ, CAN_BS1_15TQ, CAN_BS1_16TQ,
};

static const uint32_t HW_CAN_BS2_HAL_VALUES[8] = {
    CAN_BS2_1TQ, CAN_BS2_2TQ, CAN_BS2_3TQ, CAN_BS2_4TQ,
    CAN_BS2_5TQ, CAN_BS2_6TQ, CAN_BS2_7TQ, CAN_BS2_8TQ,
};

static const uint32_t HW_CAN_SJW_HAL_VALUES[4] = {
    CAN_SJW_1TQ,
    CAN_SJW_2TQ,
    CAN_SJW_3TQ,
    CAN_SJW_4TQ,
};

static const HwCanHardwareMap_T HW_CAN_HARDWARE_MAP[HW_CAN_CHANNEL_COUNT] = {
    {
        .handle            = &hcan1,
        .tx_irqn           = CAN1_TX_IRQn,
        .rx_irqn           = CAN1_RX0_IRQn,
        .sce_irqn          = CAN1_SCE_IRQn,
        .first_filter_bank = HW_CAN_FILTER_BANK_FIRST_CAN1,
    },
    {
        .handle            = &hcan2,
        .tx_irqn           = CAN2_TX_IRQn,
        .rx_irqn           = CAN2_RX0_IRQn,
        .sce_irqn          = CAN2_SCE_IRQn,
        .first_filter_bank = HW_CAN_FILTER_BANK_FIRST_CAN2,
    },
};

/**-----------------------------------------------------------------------------
 *  Private Variables
 *------------------------------------------------------------------------------
 */

static HwCanChannelState_T hw_can_channel_states[HW_CAN_CHANNEL_COUNT];

/**-----------------------------------------------------------------------------
 *  Interrupt-Vector Declarations
 *------------------------------------------------------------------------------
 */

void HW_CAN_CH1_TX_IRQ_HANDLER( void );
void HW_CAN_CH1_RX_IRQ_HANDLER( void );
void HW_CAN_CH1_SCE_IRQ_HANDLER( void );
void HW_CAN_CH2_TX_IRQ_HANDLER( void );
void HW_CAN_CH2_RX_IRQ_HANDLER( void );
void HW_CAN_CH2_SCE_IRQ_HANDLER( void );

/**-----------------------------------------------------------------------------
 *  Private Function Prototypes
 *------------------------------------------------------------------------------
 */

HW_CAN_ALWAYS_INLINE bool HW_CAN_Is_Valid_Channel( HwCanChannel_T channel );
HW_CAN_ALWAYS_INLINE bool HW_CAN_Is_Valid_Frame( const HwCanFrame_T* frame );
static bool               HW_CAN_Is_Valid_Filter_Value( const HwCanFilterValue_T* value );
static bool               HW_CAN_Is_Valid_Filter( const HwCanFilter_T* filter );
static bool               HW_CAN_Is_Valid_Configuration( const HwCanConfig_T* config );

static bool HW_CAN_Compute_Bit_Timing( uint32_t peripheral_clock_hz, const HwCanConfig_T* config,
                                       HwCanBitTiming_T* timing );
static bool HW_CAN_Map_Mode( HwCanMode_T mode, uint32_t* hal_mode );
static bool HW_CAN_Apply_HAL_Configuration( const HwCanHardwareMap_T* hardware,
                                            const HwCanConfig_T*      config,
                                            const HwCanBitTiming_T*   timing );

static uint32_t HW_CAN_Encode_Filter_Value( const HwCanFilterValue_T* value );
static uint32_t HW_CAN_Encode_Filter_Mask( const HwCanFilter_T* filter );
static bool HW_CAN_Apply_Filters( const HwCanHardwareMap_T* hardware, const HwCanConfig_T* config );
static bool HW_CAN_Deactivate_Filter_Banks( const HwCanHardwareMap_T* hardware );
static void HW_CAN_Reset_Hardware_Runtime_State( const HwCanHardwareMap_T* hardware );

static void HW_CAN_Reset_Queues( HwCanChannelState_T* state );
static void HW_CAN_Reset_Diagnostics( HwCanChannelState_T* state );
static void HW_CAN_Store_Configuration( HwCanChannelState_T* state, const HwCanConfig_T* config,
                                        const HwCanBitTiming_T* timing );

static HwCanTxQueueSlot_T HW_CAN_Encode_Tx_Frame( const HwCanFrame_T* frame );
static HwCanResult_T      HW_CAN_Stop_Internal( HwCanChannelState_T*      state,
                                                const HwCanHardwareMap_T* hardware,
                                                bool                      preserve_software_queues );

HW_CAN_ALWAYS_INLINE uint8_t HW_CAN_Count_In_Flight( uint8_t mask );
HW_CAN_ALWAYS_INLINE void    HW_CAN_Collect_Tx_Completions_From_ISR( HwCanChannelState_T* state,
                                                                     CAN_TypeDef*         instance,
                                                                     uint32_t tsr_snapshot );
HW_CAN_ALWAYS_INLINE void    HW_CAN_Fill_Tx_Mailboxes_From_ISR( HwCanChannelState_T* state,
                                                                CAN_TypeDef*         instance );
HW_CAN_ALWAYS_INLINE void    HW_CAN_Tx_Service_From_ISR( HwCanChannelState_T* state,
                                                         CAN_TypeDef*         instance );
HW_CAN_ALWAYS_INLINE void    HW_CAN_Rx_Service_From_ISR( HwCanChannelState_T* state,
                                                         CAN_TypeDef*         instance );
HW_CAN_ALWAYS_INLINE void    HW_CAN_Error_Service_From_ISR( HwCanChannelState_T* state,
                                                            CAN_TypeDef*         instance );

/**-----------------------------------------------------------------------------
 *  Private Validation Helpers
 *------------------------------------------------------------------------------
 */

HW_CAN_ALWAYS_INLINE bool HW_CAN_Is_Valid_Channel( HwCanChannel_T channel )
{
    return ( uint32_t )channel < HW_CAN_CHANNEL_COUNT;
}

HW_CAN_ALWAYS_INLINE bool HW_CAN_Is_Valid_Frame( const HwCanFrame_T* frame )
{
    if ( frame == NULL || frame->dlc > HW_CAN_CLASSIC_MAX_DATA_BYTES )
    {
        return false;
    }

    if ( frame->is_extended_id )
    {
        return frame->identifier <= HW_CAN_EXTENDED_IDENTIFIER_MAX;
    }

    return frame->identifier <= HW_CAN_STANDARD_IDENTIFIER_MAX;
}

static bool HW_CAN_Is_Valid_Filter_Value( const HwCanFilterValue_T* value )
{
    if ( value == NULL )
    {
        return false;
    }

    if ( value->is_extended_id )
    {
        return value->identifier <= HW_CAN_EXTENDED_IDENTIFIER_MAX;
    }

    return value->identifier <= HW_CAN_STANDARD_IDENTIFIER_MAX;
}

static bool HW_CAN_Is_Valid_Filter( const HwCanFilter_T* filter )
{
    if ( filter == NULL || !HW_CAN_Is_Valid_Filter_Value( &filter->first ) )
    {
        return false;
    }

    switch ( filter->mode )
    {
        case HW_CAN_FILTER_MODE_MASK: {
            uint32_t identifier_max = filter->first.is_extended_id ? HW_CAN_EXTENDED_IDENTIFIER_MAX
                                                                   : HW_CAN_STANDARD_IDENTIFIER_MAX;
            return filter->identifier_mask <= identifier_max;
        }

        case HW_CAN_FILTER_MODE_LIST:
            return HW_CAN_Is_Valid_Filter_Value( &filter->second );

        default:
            return false;
    }
}

static bool HW_CAN_Is_Valid_Configuration( const HwCanConfig_T* config )
{
    if ( config == NULL || config->bitrate == 0U || config->bitrate > 1000000U )
    {
        return false;
    }

    if ( config->sample_point_permill < 500U || config->sample_point_permill > 950U )
    {
        return false;
    }

    if ( config->sync_jump_width_tq < 1U || config->sync_jump_width_tq > 4U )
    {
        return false;
    }

    switch ( config->mode )
    {
        case HW_CAN_MODE_NORMAL:
        case HW_CAN_MODE_LOOPBACK:
        case HW_CAN_MODE_SILENT:
        case HW_CAN_MODE_SILENT_LOOPBACK:
            break;

        default:
            return false;
    }

    switch ( config->tx_priority )
    {
        case HW_CAN_TX_PRIORITY_REQUEST_ORDER:
        case HW_CAN_TX_PRIORITY_IDENTIFIER:
            break;

        default:
            return false;
    }

    switch ( config->filter_policy )
    {
        case HW_CAN_FILTER_ACCEPT_NONE:
        case HW_CAN_FILTER_ACCEPT_ALL:
            return config->filter_count == 0U;

        case HW_CAN_FILTER_CONFIGURED:
            if ( config->filters == NULL || config->filter_count == 0U
                 || config->filter_count > HW_CAN_MAX_FILTERS_PER_CHANNEL )
            {
                return false;
            }

            for ( uint32_t i = 0U; i < config->filter_count; ++i )
            {
                if ( !HW_CAN_Is_Valid_Filter( &config->filters[i] ) )
                {
                    return false;
                }
            }
            return true;

        default:
            return false;
    }
}

/**-----------------------------------------------------------------------------
 *  Private Configuration Helpers
 *------------------------------------------------------------------------------
 */

/**
 * @brief Search every legal bxCAN time-segment combination for the best timing.
 *
 * The search is deliberately performed only during configuration. Each
 * candidate uses a rounded prescaler, then scores exact bitrate error first and
 * sample-point error second. Using uint64_t avoids overflow when converting the
 * error ratio to parts per million.
 */
static bool HW_CAN_Compute_Bit_Timing( uint32_t peripheral_clock_hz, const HwCanConfig_T* config,
                                       HwCanBitTiming_T* timing )
{
    bool             found                  = false;
    uint64_t         best_bitrate_error_ppm = UINT64_MAX;
    uint32_t         best_sample_error      = UINT32_MAX;
    uint32_t         best_total_tq          = 0U;
    HwCanBitTiming_T best                   = { 0 };

    if ( peripheral_clock_hz == 0U || config == NULL || timing == NULL )
    {
        return false;
    }

    for ( uint32_t bs1 = 1U; bs1 <= 16U; ++bs1 )
    {
        for ( uint32_t bs2 = 1U; bs2 <= 8U; ++bs2 )
        {
            if ( config->sync_jump_width_tq > bs2 )
            {
                continue;
            }

            uint32_t total_tq          = 1U + bs1 + bs2;
            uint64_t requested_divisor = ( uint64_t )config->bitrate * total_tq;
            uint64_t prescaler = ( ( uint64_t )peripheral_clock_hz + ( requested_divisor / 2U ) )
                                 / requested_divisor;

            if ( prescaler < 1U || prescaler > 1024U )
            {
                continue;
            }

            uint64_t actual_divisor  = prescaler * total_tq;
            uint64_t requested_clock = ( uint64_t )config->bitrate * actual_divisor;
            uint64_t bitrate_error   = ( peripheral_clock_hz >= requested_clock )
                                           ? ( ( uint64_t )peripheral_clock_hz - requested_clock )
                                           : ( requested_clock - ( uint64_t )peripheral_clock_hz );
            uint64_t bitrate_error_ppm =
                ( ( bitrate_error * 1000000U ) + ( requested_clock / 2U ) ) / requested_clock;

            uint32_t actual_sample_point =
                ( ( ( 1U + bs1 ) * 1000U ) + ( total_tq / 2U ) ) / total_tq;
            uint32_t sample_error = ( actual_sample_point >= config->sample_point_permill )
                                        ? ( actual_sample_point - config->sample_point_permill )
                                        : ( config->sample_point_permill - actual_sample_point );

            bool is_better =
                !found || bitrate_error_ppm < best_bitrate_error_ppm
                || ( bitrate_error_ppm == best_bitrate_error_ppm
                     && sample_error < best_sample_error )
                || ( bitrate_error_ppm == best_bitrate_error_ppm
                     && sample_error == best_sample_error && total_tq > best_total_tq );

            if ( is_better )
            {
                found                  = true;
                best_bitrate_error_ppm = bitrate_error_ppm;
                best_sample_error      = sample_error;
                best_total_tq          = total_tq;
                best.actual_bitrate =
                    ( uint32_t )( ( ( uint64_t )peripheral_clock_hz + ( actual_divisor / 2U ) )
                                  / actual_divisor );
                best.sample_point_permill = ( uint16_t )actual_sample_point;
                best.bitrate_error_ppm    = ( uint16_t )bitrate_error_ppm;
                best.prescaler            = ( uint16_t )prescaler;
                best.bit_segment_1_tq     = ( uint8_t )bs1;
                best.bit_segment_2_tq     = ( uint8_t )bs2;
                best.sync_jump_width_tq   = config->sync_jump_width_tq;
            }
        }
    }

    if ( !found || best_bitrate_error_ppm > HW_CAN_MAX_BITRATE_ERROR_PPM )
    {
        return false;
    }

    *timing = best;
    return true;
}

static bool HW_CAN_Map_Mode( HwCanMode_T mode, uint32_t* hal_mode )
{
    if ( hal_mode == NULL )
    {
        return false;
    }

    switch ( mode )
    {
        case HW_CAN_MODE_NORMAL:
            *hal_mode = CAN_MODE_NORMAL;
            return true;

        case HW_CAN_MODE_LOOPBACK:
            *hal_mode = CAN_MODE_LOOPBACK;
            return true;

        case HW_CAN_MODE_SILENT:
            *hal_mode = CAN_MODE_SILENT;
            return true;

        case HW_CAN_MODE_SILENT_LOOPBACK:
            *hal_mode = CAN_MODE_SILENT_LOOPBACK;
            return true;

        default:
            return false;
    }
}

static bool HW_CAN_Apply_HAL_Configuration( const HwCanHardwareMap_T* hardware,
                                            const HwCanConfig_T*      config,
                                            const HwCanBitTiming_T*   timing )
{
    uint32_t hal_mode = 0U;

    if ( hardware == NULL || hardware->handle == NULL || hardware->handle->Instance == NULL
         || config == NULL || timing == NULL || timing->prescaler < 1U || timing->prescaler > 1024U
         || timing->bit_segment_1_tq < 1U || timing->bit_segment_1_tq > 16U
         || timing->bit_segment_2_tq < 1U || timing->bit_segment_2_tq > 8U
         || timing->sync_jump_width_tq < 1U || timing->sync_jump_width_tq > 4U
         || !HW_CAN_Map_Mode( config->mode, &hal_mode ) )
    {
        return false;
    }

    CAN_HandleTypeDef* handle = hardware->handle;

    handle->Init.Prescaler     = timing->prescaler;
    handle->Init.Mode          = hal_mode;
    handle->Init.SyncJumpWidth = HW_CAN_SJW_HAL_VALUES[timing->sync_jump_width_tq - 1U];
    handle->Init.TimeSeg1      = HW_CAN_BS1_HAL_VALUES[timing->bit_segment_1_tq - 1U];
    handle->Init.TimeSeg2      = HW_CAN_BS2_HAL_VALUES[timing->bit_segment_2_tq - 1U];

    handle->Init.TimeTriggeredMode  = DISABLE;
    handle->Init.AutoBusOff         = config->automatic_bus_off_recovery ? ENABLE : DISABLE;
    handle->Init.AutoWakeUp         = config->automatic_wake_up ? ENABLE : DISABLE;
    handle->Init.AutoRetransmission = config->automatic_retransmission ? ENABLE : DISABLE;
    handle->Init.ReceiveFifoLocked  = config->receive_fifo_locked ? ENABLE : DISABLE;
    handle->Init.TransmitFifoPriority =
        config->tx_priority == HW_CAN_TX_PRIORITY_REQUEST_ORDER ? ENABLE : DISABLE;

    return HAL_CAN_Init( handle ) == HAL_OK;
}

static uint32_t HW_CAN_Encode_Filter_Value( const HwCanFilterValue_T* value )
{
    uint32_t encoded = value->is_extended_id ? ( value->identifier << CAN_TI0R_EXID_Pos )
                                             : ( value->identifier << CAN_TI0R_STID_Pos );

    if ( value->is_extended_id )
    {
        encoded |= CAN_TI0R_IDE;
    }

    if ( value->is_remote_frame )
    {
        encoded |= CAN_TI0R_RTR;
    }

    return encoded;
}

static uint32_t HW_CAN_Encode_Filter_Mask( const HwCanFilter_T* filter )
{
    uint32_t mask = filter->first.is_extended_id ? ( filter->identifier_mask << CAN_TI0R_EXID_Pos )
                                                 : ( filter->identifier_mask << CAN_TI0R_STID_Pos );

    // Configured mask filters always distinguish 11-bit and 29-bit identifiers.
    mask |= CAN_TI0R_IDE;

    if ( filter->match_remote_frame )
    {
        mask |= CAN_TI0R_RTR;
    }

    return mask;
}

/**
 * @brief Program every filter bank in a channel's fixed partition.
 *
 * Inactive banks are explicitly deactivated so a prior configuration can never
 * remain live. All filters route to FIFO0 because only RX0 has a bounded service
 * path and configured interrupt vector.
 */
static bool HW_CAN_Apply_Filters( const HwCanHardwareMap_T* hardware, const HwCanConfig_T* config )
{
    if ( hardware == NULL || config == NULL )
    {
        return false;
    }

    for ( uint32_t local_bank = 0U; local_bank < HW_CAN_MAX_FILTERS_PER_CHANNEL; ++local_bank )
    {
        CAN_FilterTypeDef hal_filter = { 0 };
        bool              active     = false;

        hal_filter.FilterBank           = hardware->first_filter_bank + local_bank;
        hal_filter.FilterScale          = CAN_FILTERSCALE_32BIT;
        hal_filter.FilterFIFOAssignment = CAN_FILTER_FIFO0;
        hal_filter.SlaveStartFilterBank = HW_CAN_FILTER_BANK_SPLIT;
        hal_filter.FilterActivation     = DISABLE;
        hal_filter.FilterMode           = CAN_FILTERMODE_IDMASK;

        if ( config->filter_policy == HW_CAN_FILTER_ACCEPT_ALL && local_bank == 0U )
        {
            // An all-zero mask explicitly accepts every standard/extended frame.
            active = true;
        }
        else if ( config->filter_policy == HW_CAN_FILTER_CONFIGURED
                  && local_bank < config->filter_count )
        {
            const HwCanFilter_T* filter      = &config->filters[local_bank];
            uint32_t             first_word  = HW_CAN_Encode_Filter_Value( &filter->first );
            uint32_t             second_word = filter->mode == HW_CAN_FILTER_MODE_LIST
                                                   ? HW_CAN_Encode_Filter_Value( &filter->second )
                                                   : HW_CAN_Encode_Filter_Mask( filter );

            hal_filter.FilterMode = filter->mode == HW_CAN_FILTER_MODE_LIST ? CAN_FILTERMODE_IDLIST
                                                                            : CAN_FILTERMODE_IDMASK;
            hal_filter.FilterIdHigh     = ( first_word >> 16U ) & 0xFFFFU;
            hal_filter.FilterIdLow      = first_word & 0xFFFFU;
            hal_filter.FilterMaskIdHigh = ( second_word >> 16U ) & 0xFFFFU;
            hal_filter.FilterMaskIdLow  = second_word & 0xFFFFU;
            active                      = true;
        }

        hal_filter.FilterActivation = active ? ENABLE : DISABLE;

        if ( HAL_CAN_ConfigFilter( hardware->handle, &hal_filter ) != HAL_OK )
        {
            return false;
        }
    }

    return true;
}

static bool HW_CAN_Deactivate_Filter_Banks( const HwCanHardwareMap_T* hardware )
{
    bool success = true;

    if ( hardware == NULL )
    {
        return false;
    }

    for ( uint32_t local_bank = 0U; local_bank < HW_CAN_MAX_FILTERS_PER_CHANNEL; ++local_bank )
    {
        CAN_FilterTypeDef hal_filter = { 0 };

        hal_filter.FilterBank           = hardware->first_filter_bank + local_bank;
        hal_filter.FilterMode           = CAN_FILTERMODE_IDMASK;
        hal_filter.FilterScale          = CAN_FILTERSCALE_32BIT;
        hal_filter.FilterFIFOAssignment = CAN_FILTER_FIFO0;
        hal_filter.FilterActivation     = DISABLE;
        hal_filter.SlaveStartFilterBank = HW_CAN_FILTER_BANK_SPLIT;

        if ( HAL_CAN_ConfigFilter( hardware->handle, &hal_filter ) != HAL_OK )
        {
            success = false;
        }
    }

    return success;
}

/**
 * @brief Discard stale peripheral runtime state while bxCAN is stopped.
 *
 * Entering bxCAN initialization mode does not guarantee that FIFO0 contents or
 * already-pended NVIC requests disappear. Releasing at most the three hardware
 * FIFO entries prevents a frame accepted under an old filter configuration
 * from being published after restart. Peripheral causes are cleared before the
 * NVIC pending bits so they cannot immediately repend the vectors.
 */
static void HW_CAN_Reset_Hardware_Runtime_State( const HwCanHardwareMap_T* hardware )
{
    CAN_TypeDef* instance       = hardware->handle->Instance;
    uint32_t     pending_frames = instance->RF0R & CAN_RF0R_FMP0;

    if ( pending_frames > HW_CAN_RX_FIFO0_DEPTH )
    {
        pending_frames = HW_CAN_RX_FIFO0_DEPTH;
    }

    for ( uint32_t frame = 0U; frame < pending_frames; ++frame )
    {
        HW_CAN_WRITE_RF0R( instance, CAN_RF0R_RFOM0 );
    }

    CLEAR_BIT( instance->IER, ( uint32_t )HW_CAN_ALL_NOTIFICATION_MASK );
    HW_CAN_WRITE_TSR( instance, CAN_TSR_RQCP0 | CAN_TSR_RQCP1 | CAN_TSR_RQCP2 );
    HW_CAN_WRITE_RF0R( instance, CAN_RF0R_FULL0 | CAN_RF0R_FOVR0 );

    if ( ( instance->MSR & CAN_MSR_ERRI ) != 0U )
    {
        HW_CAN_WRITE_MSR( instance, CAN_MSR_ERRI );
    }

    CLEAR_BIT( instance->ESR, ( uint32_t )CAN_ESR_LEC );

    NVIC_ClearPendingIRQ( hardware->tx_irqn );
    NVIC_ClearPendingIRQ( hardware->rx_irqn );
    NVIC_ClearPendingIRQ( hardware->sce_irqn );
}

static void HW_CAN_Reset_Queues( HwCanChannelState_T* state )
{
    state->tx_head           = 0U;
    state->tx_tail           = 0U;
    state->tx_in_flight_mask = 0U;
    state->rx_head           = 0U;
    state->rx_tail           = 0U;

    // Queue clearing is cold-path work and makes debug/status inspection deterministic.
    memset( state->tx_queue, 0, sizeof( state->tx_queue ) );
    memset( state->rx_queue, 0, sizeof( state->rx_queue ) );
}

static void HW_CAN_Reset_Diagnostics( HwCanChannelState_T* state )
{
    memset( &state->diagnostics, 0, sizeof( state->diagnostics ) );
    state->diagnostics.last_error = HW_CAN_LAST_ERROR_NONE;
}

static void HW_CAN_Store_Configuration( HwCanChannelState_T* state, const HwCanConfig_T* config,
                                        const HwCanBitTiming_T* timing )
{
    state->configuration = *config;
    state->timing        = *timing;

    if ( config->filter_policy == HW_CAN_FILTER_CONFIGURED )
    {
        for ( uint32_t i = 0U; i < config->filter_count; ++i )
        {
            state->filters[i] = config->filters[i];
        }

        // Point the stored configuration at driver-owned persistent filter memory.
        state->configuration.filters = state->filters;
    }
    else
    {
        state->configuration.filters = NULL;
    }

    state->configuration_valid = true;
}

/**-----------------------------------------------------------------------------
 *  Private TX Helpers
 *------------------------------------------------------------------------------
 */

static HwCanTxQueueSlot_T HW_CAN_Encode_Tx_Frame( const HwCanFrame_T* frame )
{
    HwCanTxQueueSlot_T slot = { 0 };

    slot.tir_without_tx_request = frame->is_extended_id
                                      ? ( frame->identifier << CAN_TI0R_EXID_Pos ) | CAN_TI0R_IDE
                                      : ( frame->identifier << CAN_TI0R_STID_Pos );

    if ( frame->is_remote_frame )
    {
        slot.tir_without_tx_request |= CAN_TI0R_RTR;
    }

    slot.tdtr = frame->dlc;

    // Remote frames carry a requested length but do not carry payload bytes.
    if ( !frame->is_remote_frame )
    {
        for ( uint32_t i = 0U; i < frame->dlc; ++i )
        {
            if ( i < 4U )
            {
                slot.tdlr |= ( uint32_t )frame->data[i] << ( i * 8U );
            }
            else
            {
                slot.tdhr |= ( uint32_t )frame->data[i] << ( ( i - 4U ) * 8U );
            }
        }
    }

    return slot;
}

HW_CAN_ALWAYS_INLINE uint8_t HW_CAN_Count_In_Flight( uint8_t mask )
{
    uint8_t count = 0U;

    for ( uint32_t mailbox = 0U; mailbox < HW_CAN_TX_MAILBOX_COUNT; ++mailbox )
    {
        if ( ( mask & ( uint8_t )( 1U << mailbox ) ) != 0U )
        {
            ++count;
        }
    }

    return count;
}

/**
 * @brief Classify all completion flags from one immutable TSR snapshot.
 *
 * RQCPx is write-one-to-clear and clearing it also clears TXOKx, ALSTx and
 * TERRx. Outcomes must therefore be classified before one direct combined write
 * acknowledges the completion flags.
 */
HW_CAN_ALWAYS_INLINE void HW_CAN_Collect_Tx_Completions_From_ISR( HwCanChannelState_T* state,
                                                                  CAN_TypeDef*         instance,
                                                                  uint32_t tsr_snapshot )
{
    uint32_t clear_mask = 0U;

    for ( uint32_t mailbox = 0U; mailbox < HW_CAN_TX_MAILBOX_COUNT; ++mailbox )
    {
        if ( ( tsr_snapshot & HW_CAN_RQCP_MASKS[mailbox] ) == 0U )
        {
            continue;
        }

        clear_mask |= HW_CAN_RQCP_MASKS[mailbox];

        uint8_t mailbox_bit = ( uint8_t )( 1U << mailbox );
        if ( ( state->tx_in_flight_mask & mailbox_bit ) == 0U )
        {
            // The driver did not own this completion, but it still must be acknowledged.
            continue;
        }

        if ( ( tsr_snapshot & HW_CAN_TXOK_MASKS[mailbox] ) != 0U )
        {
            ++state->diagnostics.tx_frames_succeeded;
        }
        else if ( ( tsr_snapshot & HW_CAN_ALST_MASKS[mailbox] ) != 0U )
        {
            ++state->diagnostics.tx_arbitration_losses;
            state->diagnostics.latched_faults |= HW_CAN_FAULT_TX_ARBITRATION_LOST;
        }
        else if ( ( tsr_snapshot & HW_CAN_TERR_MASKS[mailbox] ) != 0U )
        {
            ++state->diagnostics.tx_errors;
            state->diagnostics.latched_faults |= HW_CAN_FAULT_TX_ERROR;
        }
        else
        {
            ++state->diagnostics.tx_aborted;
            state->diagnostics.latched_faults |= HW_CAN_FAULT_TX_ABORTED;
        }

        state->tx_in_flight_mask &= ( uint8_t )~mailbox_bit;
    }

    if ( clear_mask != 0U )
    {
        HW_CAN_WRITE_TSR( instance, clear_mask );
    }
}

/**
 * @brief Copy queued pre-encoded slots into currently empty hardware mailboxes.
 *
 * The loop is bounded by the three bxCAN mailboxes. A queue entry is consumed
 * only after all mailbox registers have been written and TXRQ has transferred
 * ownership of the frame to hardware.
 */
HW_CAN_ALWAYS_INLINE void HW_CAN_Fill_Tx_Mailboxes_From_ISR( HwCanChannelState_T* state,
                                                             CAN_TypeDef*         instance )
{
    uint32_t tsr_snapshot   = instance->TSR;
    uint32_t published_head = state->tx_head;

    // Pair once with the producer's publication barrier. The entry snapshot
    // also prevents newly queued work from extending this interrupt service.
    __DMB();

    for ( uint32_t mailbox = 0U; mailbox < HW_CAN_TX_MAILBOX_COUNT; ++mailbox )
    {
        if ( state->tx_tail == published_head )
        {
            break;
        }

        uint8_t mailbox_bit        = ( uint8_t )( 1U << mailbox );
        bool    mailbox_empty      = ( tsr_snapshot & HW_CAN_TME_MASKS[mailbox] ) != 0U;
        bool    completion_pending = ( tsr_snapshot & HW_CAN_RQCP_MASKS[mailbox] ) != 0U;
        bool    already_owned      = ( state->tx_in_flight_mask & mailbox_bit ) != 0U;

        if ( !mailbox_empty || completion_pending || already_owned )
        {
            continue;
        }

        uint32_t                  queue_index = state->tx_tail & HW_CAN_TX_QUEUE_INDEX_MASK;
        const HwCanTxQueueSlot_T* slot        = &state->tx_queue[queue_index];

        instance->sTxMailBox[mailbox].TIR  = slot->tir_without_tx_request;
        instance->sTxMailBox[mailbox].TDTR = slot->tdtr;
        instance->sTxMailBox[mailbox].TDLR = slot->tdlr;
        instance->sTxMailBox[mailbox].TDHR = slot->tdhr;

        // Publish mailbox ownership before TXRQ can produce a completion event.
        state->tx_in_flight_mask |= mailbox_bit;
        __DMB();
        instance->sTxMailBox[mailbox].TIR = slot->tir_without_tx_request | CAN_TI0R_TXRQ;

        // The mailbox now owns a complete copy, so the software slot may be consumed.
        __DMB();
        ++state->tx_tail;
        ++state->diagnostics.tx_frames_submitted;
    }
}

HW_CAN_ALWAYS_INLINE void HW_CAN_Tx_Service_From_ISR( HwCanChannelState_T* state,
                                                      CAN_TypeDef*         instance )
{
    uint32_t tsr_snapshot = instance->TSR;

    HW_CAN_Collect_Tx_Completions_From_ISR( state, instance, tsr_snapshot );

    if ( ( instance->ESR & CAN_ESR_BOFF ) != 0U || state->state != HW_CAN_STATE_ACTIVE )
    {
        // A pending TX vector can run after foreground triggering has already
        // reported bus-off. Existing completions remain observable, but queued
        // software frames must not transfer to mailboxes because recovery can
        // preserve only software-owned frames.
        CLEAR_BIT( instance->IER, ( uint32_t )CAN_IER_TMEIE );
        return;
    }

    HW_CAN_Fill_Tx_Mailboxes_From_ISR( state, instance );

    bool queue_empty          = state->tx_head == state->tx_tail;
    bool no_mailbox_in_flight = state->tx_in_flight_mask == 0U;

    if ( queue_empty && no_mailbox_in_flight )
    {
        CLEAR_BIT( instance->IER, ( uint32_t )CAN_IER_TMEIE );
    }
    else
    {
        SET_BIT( instance->IER, CAN_IER_TMEIE );
    }
}

/**-----------------------------------------------------------------------------
 *  Private RX and Error Helpers
 *------------------------------------------------------------------------------
 */

HW_CAN_ALWAYS_INLINE void HW_CAN_Rx_Service_From_ISR( HwCanChannelState_T* state,
                                                      CAN_TypeDef*         instance )
{
    uint32_t rf0r_snapshot    = instance->RF0R;
    uint32_t pending_at_entry = rf0r_snapshot & CAN_RF0R_FMP0;

    if ( pending_at_entry > HW_CAN_RX_FIFO0_DEPTH )
    {
        pending_at_entry = HW_CAN_RX_FIFO0_DEPTH;
    }

    uint32_t fifo_clear_mask = 0U;

    if ( ( rf0r_snapshot & CAN_RF0R_FULL0 ) != 0U )
    {
        ++state->diagnostics.rx_fifo_full_events;
        state->diagnostics.latched_faults |= HW_CAN_FAULT_RX_FIFO_FULL;
        fifo_clear_mask |= CAN_RF0R_FULL0;
    }

    if ( ( rf0r_snapshot & CAN_RF0R_FOVR0 ) != 0U )
    {
        ++state->diagnostics.rx_fifo_overruns;
        state->diagnostics.latched_faults |= HW_CAN_FAULT_RX_FIFO_OVERRUN;
        fifo_clear_mask |= CAN_RF0R_FOVR0;
    }

    if ( fifo_clear_mask != 0U )
    {
        HW_CAN_WRITE_RF0R( instance, fifo_clear_mask );
    }

    // Process only the frames reported by the entry snapshot. New arrivals wait
    // for a later interrupt and cannot extend this ISR's execution time.
    for ( uint32_t frame_index = 0U; frame_index < pending_at_entry; ++frame_index )
    {
        if ( ( instance->RF0R & CAN_RF0R_FMP0 ) == 0U )
        {
            break;
        }

        uint32_t rir  = instance->sFIFOMailBox[0].RIR;
        uint32_t rdtr = instance->sFIFOMailBox[0].RDTR;
        uint32_t rdlr = instance->sFIFOMailBox[0].RDLR;
        uint32_t rdhr = instance->sFIFOMailBox[0].RDHR;

        ++state->diagnostics.rx_frames_received;

        uint32_t used = state->rx_head - state->rx_tail;
        if ( used >= HW_CAN_RX_QUEUE_DEPTH_FRAMES )
        {
            // Drop newest on software overflow. Releasing the hardware FIFO is
            // essential; retaining it would create a permanent interrupt storm.
            ++state->diagnostics.rx_software_drops;
            state->diagnostics.latched_faults |= HW_CAN_FAULT_RX_SOFTWARE_OVERFLOW;
        }
        else
        {
            uint32_t        queue_index = state->rx_head & HW_CAN_RX_QUEUE_INDEX_MASK;
            HwCanRxFrame_T* destination = &state->rx_queue[queue_index];

            destination->frame.is_extended_id  = ( rir & CAN_RI0R_IDE ) != 0U;
            destination->frame.is_remote_frame = ( rir & CAN_RI0R_RTR ) != 0U;
            destination->frame.identifier =
                destination->frame.is_extended_id
                    ? ( ( rir >> CAN_RI0R_EXID_Pos ) & HW_CAN_EXTENDED_IDENTIFIER_MAX )
                    : ( ( rir >> CAN_RI0R_STID_Pos ) & HW_CAN_STANDARD_IDENTIFIER_MAX );

            uint8_t raw_dlc = ( uint8_t )( ( rdtr & CAN_RDT0R_DLC ) >> CAN_RDT0R_DLC_Pos );
            destination->frame.dlc =
                raw_dlc <= HW_CAN_CLASSIC_MAX_DATA_BYTES ? raw_dlc : HW_CAN_CLASSIC_MAX_DATA_BYTES;
            destination->filter_match_index =
                ( uint8_t )( ( rdtr & CAN_RDT0R_FMI ) >> CAN_RDT0R_FMI_Pos );
            destination->timestamp =
                ( uint16_t )( ( rdtr & CAN_RDT0R_TIME ) >> CAN_RDT0R_TIME_Pos );

            destination->frame.data[0] = ( uint8_t )( rdlr >> 0U );
            destination->frame.data[1] = ( uint8_t )( rdlr >> 8U );
            destination->frame.data[2] = ( uint8_t )( rdlr >> 16U );
            destination->frame.data[3] = ( uint8_t )( rdlr >> 24U );
            destination->frame.data[4] = ( uint8_t )( rdhr >> 0U );
            destination->frame.data[5] = ( uint8_t )( rdhr >> 8U );
            destination->frame.data[6] = ( uint8_t )( rdhr >> 16U );
            destination->frame.data[7] = ( uint8_t )( rdhr >> 24U );

            // The ISR publishes the slot only after every field has been stored.
            __DMB();
            ++state->rx_head;
        }

        // RFOM0 is a command bit. A direct write avoids accidentally clearing
        // FULL0/FOVR0 write-one-to-clear flags that were not in the entry snapshot.
        HW_CAN_WRITE_RF0R( instance, CAN_RF0R_RFOM0 );
    }
}

HW_CAN_ALWAYS_INLINE void HW_CAN_Error_Service_From_ISR( HwCanChannelState_T* state,
                                                         CAN_TypeDef*         instance )
{
    uint32_t msr_snapshot = instance->MSR;
    uint32_t esr_snapshot = instance->ESR;

    if ( ( esr_snapshot & CAN_ESR_EWGF ) != 0U )
    {
        ++state->diagnostics.error_warning_events;
        state->diagnostics.latched_faults |= HW_CAN_FAULT_ERROR_WARNING;
    }

    if ( ( esr_snapshot & CAN_ESR_EPVF ) != 0U )
    {
        ++state->diagnostics.error_passive_events;
        state->diagnostics.latched_faults |= HW_CAN_FAULT_ERROR_PASSIVE;
    }

    if ( ( esr_snapshot & CAN_ESR_BOFF ) != 0U )
    {
        ++state->diagnostics.bus_off_events;
        state->diagnostics.latched_faults |= HW_CAN_FAULT_BUS_OFF;
    }

    HwCanLastError_T last_error =
        ( HwCanLastError_T )( ( esr_snapshot & CAN_ESR_LEC ) >> CAN_ESR_LEC_Pos );

    if ( last_error != HW_CAN_LAST_ERROR_NONE && last_error != HW_CAN_LAST_ERROR_SOFTWARE )
    {
        // Preserve the most recent real protocol error. A warning/passive SCE
        // interrupt with LEC=0 must not erase the cause captured previously.
        state->diagnostics.last_error = last_error;
        state->diagnostics.latched_faults |= HW_CAN_FAULT_PROTOCOL_ERROR;
    }

    // LEC is writable; clearing it after the snapshot allows a later protocol
    // error of the same type to create a new observable event.
    if ( ( esr_snapshot & CAN_ESR_LEC ) != 0U )
    {
        CLEAR_BIT( instance->ESR, ( uint32_t )CAN_ESR_LEC );
    }

    if ( ( msr_snapshot & CAN_MSR_ERRI ) != 0U )
    {
        // ERRI is write-one-to-clear and belongs to the SCE interrupt path.
        HW_CAN_WRITE_MSR( instance, CAN_MSR_ERRI );
    }
}

/**-----------------------------------------------------------------------------
 *  Private Lifecycle Helper
 *------------------------------------------------------------------------------
 */

static HwCanResult_T HW_CAN_Stop_Internal( HwCanChannelState_T*      state,
                                           const HwCanHardwareMap_T* hardware,
                                           bool                      preserve_software_queues )
{
    if ( state == NULL || hardware == NULL || hardware->handle == NULL
         || hardware->handle->Instance == NULL )
    {
        return HW_CAN_RESULT_HARDWARE_ERROR;
    }

    if ( state->state == HW_CAN_STATE_UNCONFIGURED && !state->hardware_started )
    {
        return HW_CAN_RESULT_NOT_CONFIGURED;
    }

    if ( !state->hardware_started )
    {
        if ( !preserve_software_queues )
        {
            HW_CAN_Reset_Queues( state );
        }
        state->state = state->configuration_valid ? HW_CAN_STATE_CONFIGURED : HW_CAN_STATE_FAULT;
        return HW_CAN_RESULT_OK;
    }

    // Publish the stopping/fault state before touching interrupt enables. An
    // already-pended TX vector may still run, but its state check prevents it
    // from transferring further software frames into hardware mailboxes.
    state->state = HW_CAN_STATE_FAULT;

    CAN_TypeDef* instance = hardware->handle->Instance;
    bool         notifications_disabled =
        HAL_CAN_DeactivateNotification( hardware->handle, HW_CAN_ALL_NOTIFICATION_MASK ) == HAL_OK;
    CLEAR_BIT( instance->IER, ( uint32_t )CAN_IER_TMEIE );

    uint32_t abort_mask = 0U;
    for ( uint32_t mailbox = 0U; mailbox < HW_CAN_TX_MAILBOX_COUNT; ++mailbox )
    {
        if ( ( state->tx_in_flight_mask & ( uint8_t )( 1U << mailbox ) ) != 0U )
        {
            abort_mask |= HW_CAN_ABRQ_MASKS[mailbox];
        }
    }

    if ( abort_mask != 0U )
    {
        HW_CAN_WRITE_TSR( instance, abort_mask );

        for ( uint32_t attempt = 0U;
              attempt < HW_CAN_ABORT_TIMEOUT_ITERATIONS && state->tx_in_flight_mask != 0U;
              ++attempt )
        {
            uint32_t tsr_snapshot = instance->TSR;
            if ( ( tsr_snapshot & ( CAN_TSR_RQCP0 | CAN_TSR_RQCP1 | CAN_TSR_RQCP2 ) ) != 0U )
            {
                HW_CAN_Collect_Tx_Completions_From_ISR( state, instance, tsr_snapshot );
            }
        }
    }

    if ( state->tx_in_flight_mask != 0U )
    {
        uint8_t abandoned = HW_CAN_Count_In_Flight( state->tx_in_flight_mask );
        state->diagnostics.tx_aborted += abandoned;
        state->diagnostics.latched_faults |= HW_CAN_FAULT_TX_ABORTED;
        state->tx_in_flight_mask = 0U;
    }

    if ( HAL_CAN_Stop( hardware->handle ) != HAL_OK )
    {
        state->state = HW_CAN_STATE_FAULT;
        return HW_CAN_RESULT_HARDWARE_ERROR;
    }

    state->hardware_started = false;

    HW_CAN_Reset_Hardware_Runtime_State( hardware );

    if ( !preserve_software_queues )
    {
        HW_CAN_Reset_Queues( state );
    }

    state->state = HW_CAN_STATE_CONFIGURED;

    return notifications_disabled ? HW_CAN_RESULT_OK : HW_CAN_RESULT_HARDWARE_ERROR;
}

/**-----------------------------------------------------------------------------
 *  Public Configuration and Lifecycle Functions
 *------------------------------------------------------------------------------
 */

HwCanResult_T HW_CAN_Configure_Channel( HwCanChannel_T channel, const HwCanConfig_T* config )
{
    if ( !HW_CAN_Is_Valid_Channel( channel ) )
    {
        return HW_CAN_RESULT_INVALID_CHANNEL;
    }

    if ( !HW_CAN_Is_Valid_Configuration( config ) )
    {
        return HW_CAN_RESULT_INVALID_ARGUMENT;
    }

    HwCanChannelState_T*      state    = &hw_can_channel_states[channel];
    const HwCanHardwareMap_T* hardware = &HW_CAN_HARDWARE_MAP[channel];

    if ( state->state == HW_CAN_STATE_ACTIVE || state->hardware_started )
    {
        return HW_CAN_RESULT_BUSY;
    }

    uint32_t         peripheral_clock_hz = HAL_RCC_GetPCLK1Freq();
    HwCanBitTiming_T timing              = { 0 };

    if ( !HW_CAN_Compute_Bit_Timing( peripheral_clock_hz, config, &timing ) )
    {
        return HW_CAN_RESULT_UNSUPPORTED_TIMING;
    }

    state->state = HW_CAN_STATE_FAULT;

    if ( !HW_CAN_Apply_HAL_Configuration( hardware, config, &timing ) )
    {
        return HW_CAN_RESULT_HARDWARE_ERROR;
    }

    if ( !HW_CAN_Apply_Filters( hardware, config ) )
    {
        // Best-effort deactivation prevents an incomplete new filter set from
        // remaining enabled after configuration reports failure.
        ( void )HW_CAN_Deactivate_Filter_Banks( hardware );
        return HW_CAN_RESULT_FILTER_ERROR;
    }

    HW_CAN_Store_Configuration( state, config, &timing );
    HW_CAN_Reset_Queues( state );
    HW_CAN_Reset_Diagnostics( state );

    // Remove FIFO contents, completion/error causes and NVIC requests left by
    // a prior run before the channel can be started with its new filters.
    HW_CAN_Reset_Hardware_Runtime_State( hardware );

    state->state = HW_CAN_STATE_CONFIGURED;
    return HW_CAN_RESULT_OK;
}

HwCanResult_T HW_CAN_Start_Channel( HwCanChannel_T channel )
{
    if ( !HW_CAN_Is_Valid_Channel( channel ) )
    {
        return HW_CAN_RESULT_INVALID_CHANNEL;
    }

    HwCanChannelState_T*      state    = &hw_can_channel_states[channel];
    const HwCanHardwareMap_T* hardware = &HW_CAN_HARDWARE_MAP[channel];

    if ( state->state == HW_CAN_STATE_ACTIVE || state->hardware_started )
    {
        return HW_CAN_RESULT_ALREADY_STARTED;
    }

    if ( state->state != HW_CAN_STATE_CONFIGURED || !state->configuration_valid )
    {
        return HW_CAN_RESULT_NOT_CONFIGURED;
    }

    if ( HAL_CAN_Start( hardware->handle ) != HAL_OK )
    {
        state->state = HW_CAN_STATE_FAULT;
        return HW_CAN_RESULT_HARDWARE_ERROR;
    }

    state->hardware_started = true;

    if ( HAL_CAN_ActivateNotification( hardware->handle, HW_CAN_RUNTIME_NOTIFICATION_MASK )
         != HAL_OK )
    {
        if ( HAL_CAN_Stop( hardware->handle ) == HAL_OK )
        {
            state->hardware_started = false;
            HW_CAN_Reset_Hardware_Runtime_State( hardware );
            state->state = HW_CAN_STATE_CONFIGURED;
        }
        else
        {
            // The channel is unusable without notifications, but HAL reported
            // that it remains started. Preserve that fact so a later stop or
            // deconfigure call retries the physical shutdown.
            state->state = HW_CAN_STATE_FAULT;
        }
        return HW_CAN_RESULT_HARDWARE_ERROR;
    }

    state->state = HW_CAN_STATE_ACTIVE;
    return HW_CAN_RESULT_OK;
}

HwCanResult_T HW_CAN_Stop_Channel( HwCanChannel_T channel )
{
    if ( !HW_CAN_Is_Valid_Channel( channel ) )
    {
        return HW_CAN_RESULT_INVALID_CHANNEL;
    }

    return HW_CAN_Stop_Internal( &hw_can_channel_states[channel], &HW_CAN_HARDWARE_MAP[channel],
                                 false );
}

HwCanResult_T HW_CAN_Deconfigure_Channel( HwCanChannel_T channel )
{
    if ( !HW_CAN_Is_Valid_Channel( channel ) )
    {
        return HW_CAN_RESULT_INVALID_CHANNEL;
    }

    HwCanChannelState_T*      state    = &hw_can_channel_states[channel];
    const HwCanHardwareMap_T* hardware = &HW_CAN_HARDWARE_MAP[channel];

    if ( state->hardware_started )
    {
        HwCanResult_T stop_result = HW_CAN_Stop_Internal( state, hardware, false );
        if ( stop_result != HW_CAN_RESULT_OK )
        {
            return stop_result;
        }
    }

    if ( state->state == HW_CAN_STATE_UNCONFIGURED )
    {
        return HW_CAN_RESULT_OK;
    }

    if ( !HW_CAN_Deactivate_Filter_Banks( hardware ) )
    {
        state->state = HW_CAN_STATE_FAULT;
        return HW_CAN_RESULT_FILTER_ERROR;
    }

    HW_CAN_Reset_Queues( state );
    HW_CAN_Reset_Diagnostics( state );
    memset( &state->configuration, 0, sizeof( state->configuration ) );
    memset( &state->timing, 0, sizeof( state->timing ) );
    state->hardware_started    = false;
    state->configuration_valid = false;
    state->state               = HW_CAN_STATE_UNCONFIGURED;

    return HW_CAN_RESULT_OK;
}

HwCanResult_T HW_CAN_Recover_Channel( HwCanChannel_T channel )
{
    if ( !HW_CAN_Is_Valid_Channel( channel ) )
    {
        return HW_CAN_RESULT_INVALID_CHANNEL;
    }

    HwCanChannelState_T*      state    = &hw_can_channel_states[channel];
    const HwCanHardwareMap_T* hardware = &HW_CAN_HARDWARE_MAP[channel];

    if ( state->state == HW_CAN_STATE_UNCONFIGURED || !state->configuration_valid )
    {
        return HW_CAN_RESULT_NOT_CONFIGURED;
    }

    if ( state->hardware_started )
    {
        HwCanResult_T stop_result = HW_CAN_Stop_Internal( state, hardware, true );
        if ( stop_result != HW_CAN_RESULT_OK )
        {
            return stop_result;
        }
    }

    state->state = HW_CAN_STATE_FAULT;

    if ( !HW_CAN_Apply_HAL_Configuration( hardware, &state->configuration, &state->timing ) )
    {
        return HW_CAN_RESULT_HARDWARE_ERROR;
    }

    if ( !HW_CAN_Apply_Filters( hardware, &state->configuration ) )
    {
        return HW_CAN_RESULT_FILTER_ERROR;
    }

    state->state               = HW_CAN_STATE_CONFIGURED;
    HwCanResult_T start_result = HW_CAN_Start_Channel( channel );
    if ( start_result != HW_CAN_RESULT_OK )
    {
        return start_result;
    }

    if ( state->tx_head != state->tx_tail )
    {
        return HW_CAN_Tx_Trigger( channel );
    }

    return HW_CAN_RESULT_OK;
}

bool HW_CAN_Is_Channel_Started( HwCanChannel_T channel )
{
    return HW_CAN_Is_Valid_Channel( channel )
           && hw_can_channel_states[channel].state == HW_CAN_STATE_ACTIVE
           && hw_can_channel_states[channel].hardware_started;
}

/**-----------------------------------------------------------------------------
 *  Public TX and RX Functions
 *------------------------------------------------------------------------------
 */

HwCanResult_T HW_CAN_Load_Tx_Buffer( HwCanChannel_T channel, const HwCanFrame_T* frames,
                                     uint32_t frame_count )
{
    if ( !HW_CAN_Is_Valid_Channel( channel ) )
    {
        return HW_CAN_RESULT_INVALID_CHANNEL;
    }

    HwCanChannelState_T* state = &hw_can_channel_states[channel];

    if ( state->state != HW_CAN_STATE_ACTIVE )
    {
        return state->state == HW_CAN_STATE_UNCONFIGURED ? HW_CAN_RESULT_NOT_CONFIGURED
                                                         : HW_CAN_RESULT_NOT_STARTED;
    }

    if ( frame_count == 0U )
    {
        return HW_CAN_RESULT_INVALID_ARGUMENT;
    }

    if ( frames == NULL || frame_count > HW_CAN_TX_QUEUE_DEPTH_FRAMES )
    {
        return HW_CAN_RESULT_INVALID_ARGUMENT;
    }

    // Validate the complete batch before making any queue entry visible.
    for ( uint32_t i = 0U; i < frame_count; ++i )
    {
        if ( !HW_CAN_Is_Valid_Frame( &frames[i] ) )
        {
            state->diagnostics.tx_frames_rejected += frame_count;
            return HW_CAN_RESULT_INVALID_ARGUMENT;
        }
    }

    uint32_t head      = state->tx_head;
    uint32_t used      = head - state->tx_tail;
    uint32_t available = HW_CAN_TX_QUEUE_DEPTH_FRAMES - used;

    if ( frame_count > available )
    {
        state->diagnostics.tx_frames_rejected += frame_count;
        return HW_CAN_RESULT_QUEUE_FULL;
    }

    for ( uint32_t i = 0U; i < frame_count; ++i )
    {
        uint32_t queue_index         = ( head + i ) & HW_CAN_TX_QUEUE_INDEX_MASK;
        state->tx_queue[queue_index] = HW_CAN_Encode_Tx_Frame( &frames[i] );
    }

    // Publication after all slot writes gives the ISR all-or-nothing visibility.
    __DMB();
    state->tx_head = head + frame_count;
    state->diagnostics.tx_frames_queued += frame_count;

    return HW_CAN_RESULT_OK;
}

HwCanResult_T HW_CAN_Tx_Trigger( HwCanChannel_T channel )
{
    if ( !HW_CAN_Is_Valid_Channel( channel ) )
    {
        return HW_CAN_RESULT_INVALID_CHANNEL;
    }

    HwCanChannelState_T*      state    = &hw_can_channel_states[channel];
    const HwCanHardwareMap_T* hardware = &HW_CAN_HARDWARE_MAP[channel];

    if ( state->state != HW_CAN_STATE_ACTIVE )
    {
        return state->state == HW_CAN_STATE_UNCONFIGURED ? HW_CAN_RESULT_NOT_CONFIGURED
                                                         : HW_CAN_RESULT_NOT_STARTED;
    }

    if ( ( hardware->handle->Instance->ESR & CAN_ESR_BOFF ) != 0U )
    {
        // Mark the low-level channel unusable before returning. A previously
        // pending TX vector also checks state/BOFF and therefore cannot consume
        // the just-published software batch before explicit recovery.
        state->state = HW_CAN_STATE_FAULT;
        return HW_CAN_RESULT_BUS_OFF;
    }

    SET_BIT( hardware->handle->Instance->IER, CAN_IER_TMEIE );

    // A software-pended hardware IRQ keeps the vector as the sole TX consumer.
    // It also avoids running CAN mailbox work inline inside the 100 us scheduler.
    NVIC_SetPendingIRQ( hardware->tx_irqn );

    return HW_CAN_RESULT_OK;
}

bool HW_CAN_Is_Tx_Complete( HwCanChannel_T channel )
{
    if ( !HW_CAN_Is_Valid_Channel( channel ) )
    {
        return false;
    }

    const HwCanChannelState_T* state = &hw_can_channel_states[channel];
    return state->state == HW_CAN_STATE_ACTIVE && state->tx_head == state->tx_tail
           && state->tx_in_flight_mask == 0U;
}

HwCanRxSpans_T HW_CAN_Rx_Peek( HwCanChannel_T channel )
{
    HwCanRxSpans_T spans = { 0 };

    if ( !HW_CAN_Is_Valid_Channel( channel ) )
    {
        return spans;
    }

    HwCanChannelState_T* state = &hw_can_channel_states[channel];
    uint32_t             tail  = state->rx_tail;
    uint32_t             head  = state->rx_head;

    // Pair with the ISR's publication barrier before reading slot contents.
    __DMB();

    uint32_t available = head - tail;
    if ( available == 0U )
    {
        return spans;
    }

    uint32_t read_index  = tail & HW_CAN_RX_QUEUE_INDEX_MASK;
    uint32_t first_count = HW_CAN_RX_QUEUE_DEPTH_FRAMES - read_index;
    if ( first_count > available )
    {
        first_count = available;
    }

    spans.first_span.frames      = &state->rx_queue[read_index];
    spans.first_span.frame_count = first_count;
    spans.total_frame_count      = available;

    uint32_t second_count = available - first_count;
    if ( second_count != 0U )
    {
        spans.second_span.frames      = &state->rx_queue[0];
        spans.second_span.frame_count = second_count;
    }

    return spans;
}

HwCanResult_T HW_CAN_Rx_Consume( HwCanChannel_T channel, uint32_t frame_count )
{
    if ( !HW_CAN_Is_Valid_Channel( channel ) )
    {
        return HW_CAN_RESULT_INVALID_CHANNEL;
    }

    HwCanChannelState_T* state     = &hw_can_channel_states[channel];
    uint32_t             available = state->rx_head - state->rx_tail;

    if ( frame_count > available )
    {
        return HW_CAN_RESULT_INVALID_ARGUMENT;
    }

    // Complete all reads from the returned spans before publishing free space.
    __DMB();
    state->rx_tail += frame_count;

    return HW_CAN_RESULT_OK;
}

/**-----------------------------------------------------------------------------
 *  Public Status Functions
 *------------------------------------------------------------------------------
 */

HwCanResult_T HW_CAN_Get_Status( HwCanChannel_T channel, HwCanStatus_T* status )
{
    if ( !HW_CAN_Is_Valid_Channel( channel ) )
    {
        return HW_CAN_RESULT_INVALID_CHANNEL;
    }

    if ( status == NULL )
    {
        return HW_CAN_RESULT_INVALID_ARGUMENT;
    }

    const HwCanChannelState_T* state    = &hw_can_channel_states[channel];
    const CAN_TypeDef*         instance = HW_CAN_HARDWARE_MAP[channel].handle->Instance;
    uint32_t                   esr      = instance != NULL ? instance->ESR : 0U;

    memset( status, 0, sizeof( *status ) );

    status->state = state->state;

    if ( state->configuration_valid )
    {
        status->requested_bitrate           = state->configuration.bitrate;
        status->actual_bitrate              = state->timing.actual_bitrate;
        status->actual_sample_point_permill = state->timing.sample_point_permill;
        status->bitrate_error_ppm           = state->timing.bitrate_error_ppm;
        status->prescaler                   = state->timing.prescaler;
        status->bit_segment_1_tq            = state->timing.bit_segment_1_tq;
        status->bit_segment_2_tq            = state->timing.bit_segment_2_tq;
        status->sync_jump_width_tq          = state->timing.sync_jump_width_tq;
    }

    status->tx_queue_frame_count = state->tx_head - state->tx_tail;
    status->rx_queue_frame_count = state->rx_head - state->rx_tail;
    status->tx_in_flight_count   = HW_CAN_Count_In_Flight( state->tx_in_flight_mask );

    status->error_warning = ( esr & CAN_ESR_EWGF ) != 0U;
    status->error_passive = ( esr & CAN_ESR_EPVF ) != 0U;
    status->bus_off       = ( esr & CAN_ESR_BOFF ) != 0U;
    // bxCAN stores the transmit error counter in ESR[23:16] and the receive
    // error counter in ESR[31:24]. Keeping the names aligned with the hardware
    // fields prevents a bus fault from being diagnosed in the wrong direction.
    status->transmit_error_count = ( uint8_t )( ( esr >> 16U ) & 0xFFU );
    status->receive_error_count  = ( uint8_t )( ( esr >> 24U ) & 0xFFU );

    status->last_error     = state->diagnostics.last_error;
    status->latched_faults = state->diagnostics.latched_faults;

    status->tx_frames_queued      = state->diagnostics.tx_frames_queued;
    status->tx_frames_rejected    = state->diagnostics.tx_frames_rejected;
    status->tx_frames_submitted   = state->diagnostics.tx_frames_submitted;
    status->tx_frames_succeeded   = state->diagnostics.tx_frames_succeeded;
    status->tx_arbitration_losses = state->diagnostics.tx_arbitration_losses;
    status->tx_errors             = state->diagnostics.tx_errors;
    status->tx_aborted            = state->diagnostics.tx_aborted;

    status->rx_frames_received  = state->diagnostics.rx_frames_received;
    status->rx_software_drops   = state->diagnostics.rx_software_drops;
    status->rx_fifo_full_events = state->diagnostics.rx_fifo_full_events;
    status->rx_fifo_overruns    = state->diagnostics.rx_fifo_overruns;

    status->error_warning_events = state->diagnostics.error_warning_events;
    status->error_passive_events = state->diagnostics.error_passive_events;
    status->bus_off_events       = state->diagnostics.bus_off_events;

    return HW_CAN_RESULT_OK;
}

HwCanResult_T HW_CAN_Clear_Diagnostics( HwCanChannel_T channel )
{
    if ( !HW_CAN_Is_Valid_Channel( channel ) )
    {
        return HW_CAN_RESULT_INVALID_CHANNEL;
    }

    HwCanChannelState_T* state = &hw_can_channel_states[channel];

    if ( state->state == HW_CAN_STATE_ACTIVE || state->hardware_started )
    {
        return HW_CAN_RESULT_BUSY;
    }

    HW_CAN_Reset_Diagnostics( state );
    return HW_CAN_RESULT_OK;
}

/**-----------------------------------------------------------------------------
 *  Interrupt Vector Definitions
 *------------------------------------------------------------------------------
 */

void HW_CAN_CH1_TX_IRQ_HANDLER( void )
{
    HW_CAN_Tx_Service_From_ISR( &hw_can_channel_states[HW_CAN_CHANNEL_1], hcan1.Instance );
}

void HW_CAN_CH1_RX_IRQ_HANDLER( void )
{
    HW_CAN_Rx_Service_From_ISR( &hw_can_channel_states[HW_CAN_CHANNEL_1], hcan1.Instance );
}

void HW_CAN_CH1_SCE_IRQ_HANDLER( void )
{
    HW_CAN_Error_Service_From_ISR( &hw_can_channel_states[HW_CAN_CHANNEL_1], hcan1.Instance );
}

void HW_CAN_CH2_TX_IRQ_HANDLER( void )
{
    HW_CAN_Tx_Service_From_ISR( &hw_can_channel_states[HW_CAN_CHANNEL_2], hcan2.Instance );
}

void HW_CAN_CH2_RX_IRQ_HANDLER( void )
{
    HW_CAN_Rx_Service_From_ISR( &hw_can_channel_states[HW_CAN_CHANNEL_2], hcan2.Instance );
}

void HW_CAN_CH2_SCE_IRQ_HANDLER( void )
{
    HW_CAN_Error_Service_From_ISR( &hw_can_channel_states[HW_CAN_CHANNEL_2], hcan2.Instance );
}
