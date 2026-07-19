/******************************************************************************
 *  File:       exec_can.c
 *  Author:     HIL-RIG Firmware Team
 *  Created:    25-Mar-2026
 *
 *  Description:
 *      Execution-level lifecycle and bounded frame-transfer composition for CAN.
 *
 *  Notes:
 *      Configuration and recovery are cold-path operations. Transmit performs
 *      one all-or-nothing low-level load followed by one trigger. Receive uses
 *      the low-level frame-span peek/consume ownership model.
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include "exec_can.h"
#include "hw_can.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */

#if defined( __GNUC__ )
#define EXEC_CAN_ALWAYS_INLINE static inline __attribute__( ( always_inline ) )
#else
#define EXEC_CAN_ALWAYS_INLINE static inline
#endif

/**-----------------------------------------------------------------------------
 *  Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

/**
 * @brief Execution layer's lifecycle view for one CAN channel.
 */
typedef enum ExecCanChannelState_T
{
    EXEC_CAN_STATE_UNCONFIGURED = 0U,
    EXEC_CAN_STATE_ACTIVE,
    EXEC_CAN_STATE_FAULT,
} ExecCanChannelState_T;

/**-----------------------------------------------------------------------------
 *  Private Variables
 *------------------------------------------------------------------------------
 */

static ExecCanChannelState_T exec_can_channel_states[HW_CAN_CHANNEL_COUNT];

/**-----------------------------------------------------------------------------
 *  Private Helpers
 *------------------------------------------------------------------------------
 */

EXEC_CAN_ALWAYS_INLINE bool EXEC_CAN_Is_Valid_Channel( HwCanChannel_T channel )
{
    return ( uint32_t )channel < HW_CAN_CHANNEL_COUNT;
}

/**-----------------------------------------------------------------------------
 *  Public Configuration and Lifecycle Functions
 *------------------------------------------------------------------------------
 */

HwCanResult_T EXEC_CAN_Configure_Channel( HwCanChannel_T channel, const HwCanConfig_T* config )
{
    if ( !EXEC_CAN_Is_Valid_Channel( channel ) )
    {
        return HW_CAN_RESULT_INVALID_CHANNEL;
    }

    if ( config == NULL )
    {
        return HW_CAN_RESULT_INVALID_ARGUMENT;
    }

    if ( exec_can_channel_states[channel] != EXEC_CAN_STATE_UNCONFIGURED )
    {
        // A faulted execution channel can still correspond to a physically
        // started bxCAN peripheral when an earlier HAL rollback failed. Stop
        // both active and faulted channels before applying new configuration.
        HwCanResult_T stop_result = HW_CAN_Stop_Channel( channel );
        if ( stop_result != HW_CAN_RESULT_OK )
        {
            exec_can_channel_states[channel] = EXEC_CAN_STATE_FAULT;
            return stop_result;
        }
    }

    HwCanResult_T configure_result = HW_CAN_Configure_Channel( channel, config );
    if ( configure_result != HW_CAN_RESULT_OK )
    {
        // Configuration can fail after HAL has accepted timing but while the
        // shared filter banks are being programmed. Best-effort deconfiguration
        // makes the execution-level guarantee explicit: callers never inherit a
        // partially configured channel after this function reports failure.
        HwCanResult_T rollback_result    = HW_CAN_Deconfigure_Channel( channel );
        exec_can_channel_states[channel] = rollback_result == HW_CAN_RESULT_OK
                                               ? EXEC_CAN_STATE_UNCONFIGURED
                                               : EXEC_CAN_STATE_FAULT;
        return configure_result;
    }

    HwCanResult_T start_result = HW_CAN_Start_Channel( channel );
    if ( start_result != HW_CAN_RESULT_OK )
    {
        // Best-effort deconfiguration prevents a partially configured channel
        // from appearing active to later execution-path calls.
        HwCanResult_T rollback_result    = HW_CAN_Deconfigure_Channel( channel );
        exec_can_channel_states[channel] = rollback_result == HW_CAN_RESULT_OK
                                               ? EXEC_CAN_STATE_UNCONFIGURED
                                               : EXEC_CAN_STATE_FAULT;
        return start_result;
    }

    exec_can_channel_states[channel] = EXEC_CAN_STATE_ACTIVE;
    return HW_CAN_RESULT_OK;
}

HwCanResult_T EXEC_CAN_Deconfigure_Channel( HwCanChannel_T channel )
{
    if ( !EXEC_CAN_Is_Valid_Channel( channel ) )
    {
        return HW_CAN_RESULT_INVALID_CHANNEL;
    }

    HwCanResult_T result = HW_CAN_Deconfigure_Channel( channel );
    if ( result == HW_CAN_RESULT_OK )
    {
        exec_can_channel_states[channel] = EXEC_CAN_STATE_UNCONFIGURED;
    }
    else
    {
        exec_can_channel_states[channel] = EXEC_CAN_STATE_FAULT;
    }

    return result;
}

HwCanResult_T EXEC_CAN_Recover_Channel( HwCanChannel_T channel )
{
    if ( !EXEC_CAN_Is_Valid_Channel( channel ) )
    {
        return HW_CAN_RESULT_INVALID_CHANNEL;
    }

    if ( exec_can_channel_states[channel] == EXEC_CAN_STATE_UNCONFIGURED )
    {
        return HW_CAN_RESULT_NOT_CONFIGURED;
    }

    HwCanResult_T result = HW_CAN_Recover_Channel( channel );
    exec_can_channel_states[channel] =
        result == HW_CAN_RESULT_OK ? EXEC_CAN_STATE_ACTIVE : EXEC_CAN_STATE_FAULT;

    return result;
}

/**-----------------------------------------------------------------------------
 *  Public Transfer Functions
 *------------------------------------------------------------------------------
 */

HwCanResult_T EXEC_CAN_Transmit( HwCanChannel_T channel, const HwCanFrame_T* frames,
                                 uint32_t frame_count )
{
    if ( !EXEC_CAN_Is_Valid_Channel( channel ) )
    {
        return HW_CAN_RESULT_INVALID_CHANNEL;
    }

    if ( exec_can_channel_states[channel] != EXEC_CAN_STATE_ACTIVE )
    {
        return HW_CAN_RESULT_NOT_CONFIGURED;
    }

    HwCanResult_T load_result = HW_CAN_Load_Tx_Buffer( channel, frames, frame_count );
    if ( load_result != HW_CAN_RESULT_OK )
    {
        if ( load_result == HW_CAN_RESULT_BUS_OFF || load_result == HW_CAN_RESULT_HARDWARE_ERROR )
        {
            exec_can_channel_states[channel] = EXEC_CAN_STATE_FAULT;
        }
        return load_result;
    }

    HwCanResult_T trigger_result = HW_CAN_Tx_Trigger( channel );
    if ( trigger_result == HW_CAN_RESULT_BUS_OFF || trigger_result == HW_CAN_RESULT_HARDWARE_ERROR )
    {
        // The frames remain queued. Recovery can preserve and resubmit them.
        exec_can_channel_states[channel] = EXEC_CAN_STATE_FAULT;
    }

    return trigger_result;
}

HwCanResult_T EXEC_CAN_Receive( HwCanChannel_T channel, HwCanRxFrame_T* destination,
                                uint32_t destination_capacity_frames, uint32_t* frames_read )
{
    if ( !EXEC_CAN_Is_Valid_Channel( channel ) )
    {
        return HW_CAN_RESULT_INVALID_CHANNEL;
    }

    if ( frames_read == NULL || ( destination == NULL && destination_capacity_frames != 0U ) )
    {
        return HW_CAN_RESULT_INVALID_ARGUMENT;
    }

    if ( exec_can_channel_states[channel] != EXEC_CAN_STATE_ACTIVE )
    {
        return HW_CAN_RESULT_NOT_CONFIGURED;
    }

    *frames_read = 0U;

    if ( destination_capacity_frames == 0U )
    {
        return HW_CAN_RESULT_OK;
    }

    HwCanRxSpans_T spans          = HW_CAN_Rx_Peek( channel );
    uint32_t       frames_to_copy = spans.total_frame_count;

    if ( frames_to_copy > destination_capacity_frames )
    {
        frames_to_copy = destination_capacity_frames;
    }

    uint32_t first_copy = spans.first_span.frame_count;
    if ( first_copy > frames_to_copy )
    {
        first_copy = frames_to_copy;
    }

    if ( first_copy != 0U )
    {
        memcpy( destination, spans.first_span.frames, first_copy * sizeof( HwCanRxFrame_T ) );
    }

    uint32_t second_copy = frames_to_copy - first_copy;
    if ( second_copy != 0U )
    {
        memcpy( &destination[first_copy], spans.second_span.frames,
                second_copy * sizeof( HwCanRxFrame_T ) );
    }

    HwCanResult_T consume_result = HW_CAN_Rx_Consume( channel, frames_to_copy );
    if ( consume_result != HW_CAN_RESULT_OK )
    {
        return consume_result;
    }

    *frames_read = frames_to_copy;
    return HW_CAN_RESULT_OK;
}

bool EXEC_CAN_Is_Transmission_Complete( HwCanChannel_T channel )
{
    return EXEC_CAN_Is_Valid_Channel( channel )
           && exec_can_channel_states[channel] == EXEC_CAN_STATE_ACTIVE
           && HW_CAN_Is_Tx_Complete( channel );
}

HwCanResult_T EXEC_CAN_Get_Status( HwCanChannel_T channel, HwCanStatus_T* status )
{
    if ( !EXEC_CAN_Is_Valid_Channel( channel ) )
    {
        return HW_CAN_RESULT_INVALID_CHANNEL;
    }

    return HW_CAN_Get_Status( channel, status );
}

HwCanResult_T EXEC_CAN_Clear_Diagnostics( HwCanChannel_T channel )
{
    if ( !EXEC_CAN_Is_Valid_Channel( channel ) )
    {
        return HW_CAN_RESULT_INVALID_CHANNEL;
    }

    return HW_CAN_Clear_Diagnostics( channel );
}
