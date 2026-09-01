/******************************************************************************
 *  File:       exec_can.c
 *  Author:     Timothy Vogelsang
 *  Created:    25-Mar-2026
 *
 *  Description:
 *      Execution-layer CAN lifecycle, validation, channel routing, type
 *      conversion, and combined buffered transmission.
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include "exec_can.h"

#include "hw_can.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/**-----------------------------------------------------------------------------
 *  Defines / Macros / Asserts
 *------------------------------------------------------------------------------
 */
#ifdef __cplusplus
#define EXEC_CAN_STATIC_ASSERT( condition, message ) static_assert( condition, message )
#define EXEC_CAN_ZERO_INITIALIZER                                                                  \
    {                                                                                              \
    }
#else
#define EXEC_CAN_STATIC_ASSERT( condition, message ) _Static_assert( condition, message )
#define EXEC_CAN_ZERO_INITIALIZER                                                                  \
    {                                                                                              \
        0                                                                                          \
    }
#endif

EXEC_CAN_STATIC_ASSERT( EXEC_CAN_MAX_PAYLOAD_SIZE == CAN_PACKET_SIZE,
                        "Execution and hardware CAN payload limits must match" );
EXEC_CAN_STATIC_ASSERT( EXEC_CAN_MAX_BATCH_SIZE <= HW_CAN_TX_QUEUE_CAPACITY,
                        "Execution CAN batches must fit the hardware TX queue" );
EXEC_CAN_STATIC_ASSERT( EXEC_CAN_MAX_BATCH_SIZE <= HW_CAN_RX_QUEUE_CAPACITY,
                        "Execution CAN receive storage must cover the hardware RX queue" );

/**-----------------------------------------------------------------------------
 *  Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

/**
 * @brief Execution-layer lifecycle state for one CAN channel.
 */
typedef struct ExecCANChannelState_T
{
    bool is_configured;
    bool is_started;
} ExecCANChannelState_T;

/**-----------------------------------------------------------------------------
 *  Public (global) and Extern Variables
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Private (static) Variables
 *------------------------------------------------------------------------------
 */

static ExecCANChannelState_T exec_can_state[EXEC_CAN_CHANNEL_COUNT] = EXEC_CAN_ZERO_INITIALIZER;

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

static bool EXEC_CAN_Channel_Is_Valid( EXEC_CAN_Channel_T channel )
{
    return channel == EXEC_CAN_CHANNEL_1 || channel == EXEC_CAN_CHANNEL_2;
}

static EXEC_CAN_Result_T EXEC_CAN_Map_Result( HW_CAN_Result_T result )
{
    switch ( result )
    {
        case HW_CAN_RESULT_OK:
            return EXEC_CAN_RESULT_OK;
        case HW_CAN_RESULT_BUSY:
            return EXEC_CAN_RESULT_BUSY;
        case HW_CAN_RESULT_EMPTY:
            return EXEC_CAN_RESULT_EMPTY;
        case HW_CAN_RESULT_NOT_CONFIGURED:
            return EXEC_CAN_RESULT_NOT_CONFIGURED;
        case HW_CAN_RESULT_NOT_STARTED:
            return EXEC_CAN_RESULT_NOT_STARTED;
        case HW_CAN_RESULT_TIMING_ERROR:
            return EXEC_CAN_RESULT_TIMING_ERROR;
        case HW_CAN_RESULT_FILTER_ERROR:
            return EXEC_CAN_RESULT_FILTER_ERROR;
        case HW_CAN_RESULT_ERROR:
        default:
            return EXEC_CAN_RESULT_ERROR;
    }
}

static HW_CAN_Result_T EXEC_CAN_HW_Configure( EXEC_CAN_Channel_T channel,
                                              EXEC_CAN_Config_T  configuration )
{
    if ( channel == EXEC_CAN_CHANNEL_1 )
    {
        return HW_CAN_Configure1( configuration.bitrate, configuration.filter_bank,
                                  configuration.filter_id, configuration.filter_mask );
    }

    return HW_CAN_Configure2( configuration.bitrate, configuration.filter_bank,
                              configuration.filter_id, configuration.filter_mask );
}

static HW_CAN_Result_T EXEC_CAN_HW_Start( EXEC_CAN_Channel_T channel )
{
    if ( channel == EXEC_CAN_CHANNEL_1 )
    {
        return HW_CAN_Start1();
    }
    return HW_CAN_Start2();
}

static HW_CAN_Result_T EXEC_CAN_HW_Stop( EXEC_CAN_Channel_T channel )
{
    if ( channel == EXEC_CAN_CHANNEL_1 )
    {
        return HW_CAN_Stop1();
    }
    return HW_CAN_Stop2();
}

static bool EXEC_CAN_HW_Is_Configured( EXEC_CAN_Channel_T channel )
{
    if ( channel == EXEC_CAN_CHANNEL_1 )
    {
        return HW_CAN_Is_Configured1();
    }

    return HW_CAN_Is_Configured2();
}

static bool EXEC_CAN_HW_Is_Started( EXEC_CAN_Channel_T channel )
{
    if ( channel == EXEC_CAN_CHANNEL_1 )
    {
        return HW_CAN_Is_Started1();
    }

    return HW_CAN_Is_Started2();
}

static EXEC_CAN_Tx_Status_T EXEC_CAN_Map_Tx_Status( HW_CAN_Tx_Status_T status )
{
    switch ( status )
    {
        case HW_CAN_TX_STATUS_IDLE:
            return EXEC_CAN_TX_STATUS_IDLE;
        case HW_CAN_TX_STATUS_ACTIVE:
            return EXEC_CAN_TX_STATUS_ACTIVE;
        case HW_CAN_TX_STATUS_COMPLETE:
            return EXEC_CAN_TX_STATUS_COMPLETE;
        case HW_CAN_TX_STATUS_ERROR:
        default:
            return EXEC_CAN_TX_STATUS_ERROR;
    }
}
/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */

EXEC_CAN_Result_T EXEC_CAN_Configure_Channel( EXEC_CAN_Channel_T       channel,
                                              const EXEC_CAN_Config_T* configuration )
{
    if ( !EXEC_CAN_Channel_Is_Valid( channel ) || configuration == NULL )
    {
        return EXEC_CAN_RESULT_INVALID_ARGUMENT;
    }

    ExecCANChannelState_T* state = &exec_can_state[channel];

    if ( !configuration->is_enabled )
    {
        if ( state->is_started )
        {
            HW_CAN_Result_T stop_result = EXEC_CAN_HW_Stop( channel );

            if ( stop_result != HW_CAN_RESULT_OK )
            {
                return EXEC_CAN_Map_Result( stop_result );
            }
        }

        /*
         * TODO: Apply the CAN transceiver safe state here when its board-level
         * control mapping is confirmed during hardware bring-up.
         */

        state->is_configured = false;
        state->is_started    = false;

        return EXEC_CAN_RESULT_OK;
    }

    if ( state->is_started )
    {
        return EXEC_CAN_RESULT_BUSY;
    }

    /*
     * A failed HW configuration invalidates the previous stopped
     * configuration, matching the HW CAN configuration contract.
     */
    state->is_configured = false;
    state->is_started    = false;

    HW_CAN_Result_T result = EXEC_CAN_HW_Configure( channel, *configuration );

    if ( result == HW_CAN_RESULT_OK )
    {
        state->is_configured = true;
    }

    return EXEC_CAN_Map_Result( result );
}

EXEC_CAN_Result_T EXEC_CAN_Start_Channel( EXEC_CAN_Channel_T channel )
{
    if ( !EXEC_CAN_Channel_Is_Valid( channel ) )
    {
        return EXEC_CAN_RESULT_INVALID_ARGUMENT;
    }

    ExecCANChannelState_T* state = &exec_can_state[channel];

    if ( !state->is_configured )
    {
        return EXEC_CAN_RESULT_NOT_CONFIGURED;
    }

    if ( state->is_started )
    {
        return EXEC_CAN_RESULT_BUSY;
    }

    HW_CAN_Result_T result = EXEC_CAN_HW_Start( channel );

    if ( result == HW_CAN_RESULT_OK )
    {
        state->is_started = true;
    }

    return EXEC_CAN_Map_Result( result );
}

EXEC_CAN_Result_T EXEC_CAN_Stop_Channel( EXEC_CAN_Channel_T channel )
{
    if ( !EXEC_CAN_Channel_Is_Valid( channel ) )
    {
        return EXEC_CAN_RESULT_INVALID_ARGUMENT;
    }

    ExecCANChannelState_T* state = &exec_can_state[channel];

    if ( !state->is_configured )
    {
        return EXEC_CAN_RESULT_NOT_CONFIGURED;
    }

    if ( !state->is_started )
    {
        return EXEC_CAN_RESULT_NOT_STARTED;
    }

    HW_CAN_Result_T result = EXEC_CAN_HW_Stop( channel );

    if ( result == HW_CAN_RESULT_OK )
    {
        state->is_started = false;
    }

    return EXEC_CAN_Map_Result( result );
}

bool EXEC_CAN_Is_Configured( EXEC_CAN_Channel_T channel )
{
    if ( !EXEC_CAN_Channel_Is_Valid( channel ) )
    {
        return false;
    }

    return exec_can_state[channel].is_configured;
}

bool EXEC_CAN_Is_Started( EXEC_CAN_Channel_T channel )
{
    if ( !EXEC_CAN_Channel_Is_Valid( channel ) )
    {
        return false;
    }

    return exec_can_state[channel].is_started;
}

EXEC_CAN_Result_T EXEC_CAN_Transmit( EXEC_CAN_Channel_T channel, const EXEC_CAN_Packet_T packets[],
                                     uint16_t packet_count )
{
    if ( !EXEC_CAN_Channel_Is_Valid( channel ) || packets == NULL || packet_count == 0U
         || packet_count > EXEC_CAN_MAX_BATCH_SIZE )
    {
        return EXEC_CAN_RESULT_INVALID_ARGUMENT;
    }

    CAN_Packet_T hardware_packets[EXEC_CAN_MAX_BATCH_SIZE] = EXEC_CAN_ZERO_INITIALIZER;
    /* TODO: Avoid full-capacity initialization and redundant packet translation
     * using a shared transport representation or prepared batch. Preserve
     * validation/ownership; do not cast between distinct packet struct types.
     */
    for ( uint16_t i = 0U; i < packet_count; i++ )
    {
        if ( packets[i].id > EXEC_CAN_STANDARD_ID_MAX
             || packets[i].dlc > EXEC_CAN_MAX_PAYLOAD_SIZE )
        {
            return EXEC_CAN_RESULT_INVALID_ARGUMENT;
        }

        hardware_packets[i].id  = packets[i].id;
        hardware_packets[i].dlc = packets[i].dlc;
        memcpy( hardware_packets[i].data, packets[i].data, packets[i].dlc );
    }

    HW_CAN_Result_T result = channel == EXEC_CAN_CHANNEL_1
                                 ? HW_CAN_Tx_Buffer_Write1( hardware_packets, packet_count )
                                 : HW_CAN_Tx_Buffer_Write2( hardware_packets, packet_count );
    if ( result != HW_CAN_RESULT_OK )
    {
        return EXEC_CAN_Map_Result( result );
    }

    result = channel == EXEC_CAN_CHANNEL_1 ? HW_CAN_Tx_Trigger1() : HW_CAN_Tx_Trigger2();
    if ( result != HW_CAN_RESULT_OK )
    {
        if ( channel == EXEC_CAN_CHANNEL_1 )
        {
            HW_CAN_Tx_Buffer_Cancel1();
        }
        else
        {
            HW_CAN_Tx_Buffer_Cancel2();
        }
    }

    return EXEC_CAN_Map_Result( result );
}

EXEC_CAN_Result_T EXEC_CAN_Receive( EXEC_CAN_Channel_T channel, EXEC_CAN_Packet_T destination[],
                                    uint16_t capacity, uint16_t* packets_read )
{
    if ( !EXEC_CAN_Channel_Is_Valid( channel ) || destination == NULL || packets_read == NULL )
    {
        return EXEC_CAN_RESULT_INVALID_ARGUMENT;
    }

    *packets_read = 0U;
    if ( capacity == 0U )
    {
        return EXEC_CAN_RESULT_OK;
    }

    uint16_t hardware_capacity = capacity;
    if ( hardware_capacity > EXEC_CAN_MAX_BATCH_SIZE )
    {
        hardware_capacity = EXEC_CAN_MAX_BATCH_SIZE;
    }

    /* TODO: Remove full-capacity temporary initialization and redundant RX
     * copies while preserving validation and defined unused payload bytes.
     * The conversion loop already processes only the received packet count.
     */
    CAN_Packet_T hardware_packets[EXEC_CAN_MAX_BATCH_SIZE] = EXEC_CAN_ZERO_INITIALIZER;
    uint16_t     count                                     = channel == EXEC_CAN_CHANNEL_1
                                                                 ? HW_CAN_Rx_Buffer_Read1( hardware_packets, hardware_capacity )
                                                                 : HW_CAN_Rx_Buffer_Read2( hardware_packets, hardware_capacity );

    for ( uint16_t i = 0U; i < count; i++ )
    {
        if ( hardware_packets[i].id > EXEC_CAN_STANDARD_ID_MAX
             || hardware_packets[i].dlc > EXEC_CAN_MAX_PAYLOAD_SIZE )
        {
            return EXEC_CAN_RESULT_ERROR;
        }

        destination[i].id  = hardware_packets[i].id;
        destination[i].dlc = hardware_packets[i].dlc;
        memset( destination[i].data, 0, sizeof( destination[i].data ) );
        memcpy( destination[i].data, hardware_packets[i].data, hardware_packets[i].dlc );
    }

    *packets_read = count;
    return EXEC_CAN_RESULT_OK;
}

EXEC_CAN_Tx_Status_T EXEC_CAN_Get_Tx_Status( EXEC_CAN_Channel_T channel )
{
    if ( !EXEC_CAN_Channel_Is_Valid( channel ) )
    {
        return EXEC_CAN_TX_STATUS_INVALID_CHANNEL;
    }

    HW_CAN_Tx_Status_T status =
        channel == EXEC_CAN_CHANNEL_1 ? HW_CAN_Tx_Status1() : HW_CAN_Tx_Status2();
    return EXEC_CAN_Map_Tx_Status( status );
}

EXEC_CAN_Result_T EXEC_CAN_Recover( EXEC_CAN_Channel_T channel )
{
    if ( !EXEC_CAN_Channel_Is_Valid( channel ) )
    {
        return EXEC_CAN_RESULT_INVALID_ARGUMENT;
    }

    ExecCANChannelState_T* state = &exec_can_state[channel];

    if ( !state->is_configured )
    {
        return EXEC_CAN_RESULT_NOT_CONFIGURED;
    }

    if ( !state->is_started )
    {
        return EXEC_CAN_RESULT_NOT_STARTED;
    }

    HW_CAN_Result_T result = channel == EXEC_CAN_CHANNEL_1 ? HW_CAN_Recover1() : HW_CAN_Recover2();

    if ( result == HW_CAN_RESULT_OK )
    {
        state->is_configured = true;
        state->is_started    = true;
    }
    else
    {
        /*
         * A failed recovery may leave the HW channel either started or
         * stopped, depending on whether HAL Stop or HAL Start failed.
         */
        state->is_configured = EXEC_CAN_HW_Is_Configured( channel );
        state->is_started    = EXEC_CAN_HW_Is_Started( channel );
    }

    return EXEC_CAN_Map_Result( result );
}

uint32_t EXEC_CAN_Get_Rx_Dropped_Count( EXEC_CAN_Channel_T channel )
{
    if ( !EXEC_CAN_Channel_Is_Valid( channel ) )
    {
        return 0U;
    }

    return channel == EXEC_CAN_CHANNEL_1 ? HW_CAN_Rx_Dropped_Count1() : HW_CAN_Rx_Dropped_Count2();
}
