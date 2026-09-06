/******************************************************************************
 *  File:       protocol_test_harness.c
 *  Author:     OpenAI
 *  Created:    30-Aug-2026
 *
 *  Description:
 *      Legacy HRTP ECHO/STATUS diagnostic codec for Transport hardware tests.
 ******************************************************************************/

#include "protocol_test_harness.h"

#include "hil_rig_protocol/version.h"

#include <string.h>

_Static_assert( PROTOCOL_TEST_HARNESS_STATUS_FIELD_COUNT == 32U,
                "HRTP STATUS v2 field count must remain fixed." );
_Static_assert( PROTOCOL_TEST_HARNESS_STATUS_PAYLOAD_SIZE == 128U,
                "HRTP STATUS v2 payload must contain 32 little-endian uint32_t fields." );

static uint16_t PROTOCOL_TEST_HARNESS_Read_U16_LE( const uint8_t* data )
{
    return ( uint16_t )( ( uint16_t )data[0] | ( ( uint16_t )data[1] << 8U ) );
}

static uint32_t PROTOCOL_TEST_HARNESS_Read_U32_LE( const uint8_t* data )
{
    return ( uint32_t )data[0] | ( ( uint32_t )data[1] << 8U ) | ( ( uint32_t )data[2] << 16U )
           | ( ( uint32_t )data[3] << 24U );
}

static void PROTOCOL_TEST_HARNESS_Write_U16_LE( uint8_t* data, uint16_t value )
{
    data[0] = ( uint8_t )( value & 0xFFU );
    data[1] = ( uint8_t )( ( value >> 8U ) & 0xFFU );
}

static void PROTOCOL_TEST_HARNESS_Write_U32_LE( uint8_t* data, uint32_t value )
{
    data[0] = ( uint8_t )( value & 0xFFU );
    data[1] = ( uint8_t )( ( value >> 8U ) & 0xFFU );
    data[2] = ( uint8_t )( ( value >> 16U ) & 0xFFU );
    data[3] = ( uint8_t )( ( value >> 24U ) & 0xFFU );
}

static void PROTOCOL_TEST_HARNESS_Write_Header( uint8_t* response, uint8_t opcode,
                                                uint32_t request_id, uint32_t payload_length )
{
    response[0] = ( uint8_t )'H';
    response[1] = ( uint8_t )'R';
    response[2] = ( uint8_t )'T';
    response[3] = ( uint8_t )'P';
    response[4] = PROTOCOL_TEST_HARNESS_VERSION;
    response[5] = opcode;
    PROTOCOL_TEST_HARNESS_Write_U16_LE( &response[6], 0U );
    PROTOCOL_TEST_HARNESS_Write_U32_LE( &response[8], request_id );
    PROTOCOL_TEST_HARNESS_Write_U32_LE( &response[12], payload_length );
}

static void
PROTOCOL_TEST_HARNESS_Write_Status_Payload( uint8_t*                                   payload,
                                            const PROTOCOL_TEST_HARNESS_Status_Data_T* status_data )
{
    PROTOCOL_TEST_HARNESS_Write_U32_LE( &payload[0U * 4U],
                                        PROTOCOL_TEST_HARNESS_STATUS_SCHEMA_VERSION );
    PROTOCOL_TEST_HARNESS_Write_U32_LE( &payload[1U * 4U], status_data->link_state );
    PROTOCOL_TEST_HARNESS_Write_U32_LE( &payload[2U * 4U], status_data->link_generation );
    PROTOCOL_TEST_HARNESS_Write_U32_LE( &payload[3U * 4U], status_data->transport_event_count );
    PROTOCOL_TEST_HARNESS_Write_U32_LE( &payload[4U * 4U], status_data->usb_rx_bytes );
    PROTOCOL_TEST_HARNESS_Write_U32_LE( &payload[5U * 4U], status_data->usb_tx_bytes );
    PROTOCOL_TEST_HARNESS_Write_U32_LE( &payload[6U * 4U],
                                        status_data->application_requests_received );
    PROTOCOL_TEST_HARNESS_Write_U32_LE( &payload[7U * 4U], status_data->responses_submitted );
    PROTOCOL_TEST_HARNESS_Write_U32_LE( &payload[8U * 4U], status_data->usb_tx_busy_retries );
    PROTOCOL_TEST_HARNESS_Write_U32_LE( &payload[9U * 4U], status_data->invalid_hrtp_messages );
    PROTOCOL_TEST_HARNESS_Write_U32_LE( &payload[10U * 4U], status_data->maximum_service_gap_ms );
    PROTOCOL_TEST_HARNESS_Write_U32_LE( &payload[11U * 4U], status_data->transport_session_state );
    PROTOCOL_TEST_HARNESS_Write_U32_LE( &payload[12U * 4U], status_data->compatibility_profile_id );
    PROTOCOL_TEST_HARNESS_Write_U32_LE( &payload[13U * 4U], HIL_RIG_PROTOCOL_VERSION_MAJOR );
    PROTOCOL_TEST_HARNESS_Write_U32_LE( &payload[14U * 4U], HIL_RIG_PROTOCOL_VERSION_MINOR );
    PROTOCOL_TEST_HARNESS_Write_U32_LE( &payload[15U * 4U], HIL_RIG_PROTOCOL_VERSION_PATCH );
    PROTOCOL_TEST_HARNESS_Write_U32_LE( &payload[16U * 4U],
                                        status_data->application_codec_initialized );
    PROTOCOL_TEST_HARNESS_Write_U32_LE( &payload[17U * 4U],
                                        status_data->application_initialization_status );
    PROTOCOL_TEST_HARNESS_Write_U32_LE(
        &payload[18U * 4U], status_data->non_hrtp_application_messages_received );
    PROTOCOL_TEST_HARNESS_Write_U32_LE( &payload[19U * 4U],
                                        status_data->application_decode_failures );
    PROTOCOL_TEST_HARNESS_Write_U32_LE( &payload[20U * 4U],
                                        status_data->application_semantic_rejections );
    PROTOCOL_TEST_HARNESS_Write_U32_LE( &payload[21U * 4U],
                                        status_data->application_encode_failures );
    PROTOCOL_TEST_HARNESS_Write_U32_LE( &payload[22U * 4U],
                                        status_data->configurations_accepted );
    PROTOCOL_TEST_HARNESS_Write_U32_LE( &payload[23U * 4U], status_data->instructions_accepted );
    PROTOCOL_TEST_HARNESS_Write_U32_LE( &payload[24U * 4U], status_data->results_encoded );
    PROTOCOL_TEST_HARNESS_Write_U32_LE( &payload[25U * 4U],
                                        status_data->application_harness_state );
    PROTOCOL_TEST_HARNESS_Write_U32_LE( &payload[26U * 4U], status_data->next_expected_tick );
    PROTOCOL_TEST_HARNESS_Write_U32_LE( &payload[27U * 4U],
                                        status_data->active_expected_tick_count );
    PROTOCOL_TEST_HARNESS_Write_U32_LE( &payload[28U * 4U], status_data->last_application_status );
    PROTOCOL_TEST_HARNESS_Write_U32_LE(
        &payload[29U * 4U], status_data->last_decoded_application_message_type );
    PROTOCOL_TEST_HARNESS_Write_U32_LE( &payload[30U * 4U], status_data->configuration_digest );
    PROTOCOL_TEST_HARNESS_Write_U32_LE( &payload[31U * 4U], status_data->instruction_digest );
}

PROTOCOL_TEST_HARNESS_Result_T PROTOCOL_TEST_HARNESS_Build_Response(
    const uint8_t* request, size_t request_length, size_t max_application_message_size,
    const PROTOCOL_TEST_HARNESS_Status_Data_T* status_data, uint8_t* response,
    size_t response_capacity, size_t* response_length )
{
    uint16_t flags            = 0U;
    uint32_t request_id       = 0U;
    uint32_t payload_length   = 0U;
    uint64_t total_length_u64 = 0U;
    size_t   total_length     = 0U;

    if ( response_length != NULL )
    {
        *response_length = 0U;
    }

    if ( request == NULL || response == NULL || response_length == NULL
         || max_application_message_size < PROTOCOL_TEST_HARNESS_HEADER_SIZE )
    {
        return PROTOCOL_TEST_HARNESS_RESULT_INVALID_ARGUMENT;
    }

    if ( request_length < PROTOCOL_TEST_HARNESS_HEADER_SIZE )
    {
        return PROTOCOL_TEST_HARNESS_RESULT_INVALID_LENGTH;
    }

    if ( request[0] != ( uint8_t )'H' || request[1] != ( uint8_t )'R'
         || request[2] != ( uint8_t )'T' || request[3] != ( uint8_t )'P' )
    {
        return PROTOCOL_TEST_HARNESS_RESULT_INVALID_MAGIC;
    }

    if ( request[4] != PROTOCOL_TEST_HARNESS_VERSION )
    {
        return PROTOCOL_TEST_HARNESS_RESULT_UNSUPPORTED_VERSION;
    }

    flags = PROTOCOL_TEST_HARNESS_Read_U16_LE( &request[6] );
    if ( ( flags & ( uint16_t )~PROTOCOL_TEST_HARNESS_SUPPORTED_FLAGS ) != 0U )
    {
        return PROTOCOL_TEST_HARNESS_RESULT_UNSUPPORTED_FLAGS;
    }

    payload_length   = PROTOCOL_TEST_HARNESS_Read_U32_LE( &request[12] );
    total_length_u64 = ( uint64_t )PROTOCOL_TEST_HARNESS_HEADER_SIZE + payload_length;
    if ( ( uint64_t )request_length != total_length_u64 )
    {
        return PROTOCOL_TEST_HARNESS_RESULT_INVALID_LENGTH;
    }

    if ( total_length_u64 > ( uint64_t )max_application_message_size )
    {
        return PROTOCOL_TEST_HARNESS_RESULT_MESSAGE_TOO_LARGE;
    }
    total_length = ( size_t )total_length_u64;

    request_id = PROTOCOL_TEST_HARNESS_Read_U32_LE( &request[8] );

    switch ( request[5] )
    {
        case PROTOCOL_TEST_HARNESS_OPCODE_ECHO_REQUEST:
            if ( response_capacity < total_length )
            {
                *response_length = total_length;
                return PROTOCOL_TEST_HARNESS_RESULT_BUFFER_TOO_SMALL;
            }

            PROTOCOL_TEST_HARNESS_Write_Header(
                response, PROTOCOL_TEST_HARNESS_OPCODE_ECHO_RESPONSE, request_id, payload_length );
            if ( payload_length > 0U )
            {
                memcpy( &response[PROTOCOL_TEST_HARNESS_HEADER_SIZE],
                        &request[PROTOCOL_TEST_HARNESS_HEADER_SIZE], payload_length );
            }
            *response_length = total_length;
            return PROTOCOL_TEST_HARNESS_RESULT_OK;

        case PROTOCOL_TEST_HARNESS_OPCODE_STATUS_REQUEST:
            if ( payload_length != 0U )
            {
                return PROTOCOL_TEST_HARNESS_RESULT_INVALID_LENGTH;
            }

            if ( status_data == NULL )
            {
                return PROTOCOL_TEST_HARNESS_RESULT_INVALID_ARGUMENT;
            }

            total_length =
                PROTOCOL_TEST_HARNESS_HEADER_SIZE + PROTOCOL_TEST_HARNESS_STATUS_PAYLOAD_SIZE;
            if ( total_length > max_application_message_size )
            {
                return PROTOCOL_TEST_HARNESS_RESULT_MESSAGE_TOO_LARGE;
            }

            if ( response_capacity < total_length )
            {
                *response_length = total_length;
                return PROTOCOL_TEST_HARNESS_RESULT_BUFFER_TOO_SMALL;
            }

            PROTOCOL_TEST_HARNESS_Write_Header(
                response, PROTOCOL_TEST_HARNESS_OPCODE_STATUS_RESPONSE, request_id,
                PROTOCOL_TEST_HARNESS_STATUS_PAYLOAD_SIZE );
            PROTOCOL_TEST_HARNESS_Write_Status_Payload(
                &response[PROTOCOL_TEST_HARNESS_HEADER_SIZE], status_data );
            *response_length = total_length;
            return PROTOCOL_TEST_HARNESS_RESULT_OK;

        default:
            return PROTOCOL_TEST_HARNESS_RESULT_UNSUPPORTED_OPCODE;
    }
}
