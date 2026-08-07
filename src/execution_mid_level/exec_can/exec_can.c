/******************************************************************************
 *  File:       exec_can.c
 *  Author:     Timothy Vogelsang
 *  Created:    25-Mar-2026
 *
 *  Description:
 *      Execution-layer CAN validation, channel routing, type conversion, and
 *      combined buffered transmission.
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

_Static_assert( EXEC_CAN_MAX_PAYLOAD_SIZE == CAN_PACKET_SIZE,
                "Execution and hardware CAN payload limits must match" );
_Static_assert( EXEC_CAN_MAX_BATCH_SIZE <= HW_CAN_TX_QUEUE_CAPACITY,
                "Execution CAN batches must fit the hardware TX queue" );
_Static_assert( EXEC_CAN_MAX_BATCH_SIZE <= HW_CAN_RX_QUEUE_CAPACITY,
                "Execution CAN receive storage must cover the hardware RX queue" );

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
        case HW_CAN_RESULT_ERROR:
        default:
            return EXEC_CAN_RESULT_ERROR;
    }
}

static EXEC_CAN_Result_T EXEC_CAN_Map_Configuration_Result( int result )
{
    switch ( result )
    {
        case 0:
            return EXEC_CAN_RESULT_OK;
        case 1:
            return EXEC_CAN_RESULT_TIMING_ERROR;
        case 2:
            return EXEC_CAN_RESULT_FILTER_ERROR;
        case 3:
            return EXEC_CAN_RESULT_START_ERROR;
        default:
            return EXEC_CAN_RESULT_ERROR;
    }
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

EXEC_CAN_Result_T EXEC_CAN_Configure( EXEC_CAN_Channel_T channel, uint32_t bitrate,
                                      uint16_t filter_bank, uint16_t filter_id,
                                      uint16_t filter_mask )
{
    if ( !EXEC_CAN_Channel_Is_Valid( channel ) )
    {
        return EXEC_CAN_RESULT_INVALID_ARGUMENT;
    }

    int result = channel == EXEC_CAN_CHANNEL_1
                     ? HW_CAN_Configure1( bitrate, filter_bank, filter_id, filter_mask )
                     : HW_CAN_Configure2( bitrate, filter_bank, filter_id, filter_mask );
    return EXEC_CAN_Map_Configuration_Result( result );
}

EXEC_CAN_Result_T EXEC_CAN_Transmit( EXEC_CAN_Channel_T channel, const EXEC_CAN_Packet_T packets[],
                                     uint16_t packet_count )
{
    if ( !EXEC_CAN_Channel_Is_Valid( channel ) || packets == NULL || packet_count == 0U
         || packet_count > EXEC_CAN_MAX_BATCH_SIZE )
    {
        return EXEC_CAN_RESULT_INVALID_ARGUMENT;
    }

    CAN_Packet_T hardware_packets[EXEC_CAN_MAX_BATCH_SIZE] = { 0 };
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

    CAN_Packet_T hardware_packets[EXEC_CAN_MAX_BATCH_SIZE] = { 0 };
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

    HW_CAN_Result_T result = channel == EXEC_CAN_CHANNEL_1 ? HW_CAN_Recover1() : HW_CAN_Recover2();
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
