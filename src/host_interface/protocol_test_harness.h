/******************************************************************************
 *  File:       protocol_test_harness.h
 *  Author:     OpenAI
 *  Created:    30-Aug-2026
 *
 *  Description:
 *      Legacy HRTP ECHO/STATUS diagnostic codec for Transport hardware tests.
 ******************************************************************************/

#ifndef PROTOCOL_TEST_HARNESS_H
#define PROTOCOL_TEST_HARNESS_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <stddef.h>
#include <stdint.h>

#define PROTOCOL_TEST_HARNESS_HEADER_SIZE 16U
#define PROTOCOL_TEST_HARNESS_VERSION 1U
#define PROTOCOL_TEST_HARNESS_OPCODE_ECHO_REQUEST 0x01U
#define PROTOCOL_TEST_HARNESS_OPCODE_STATUS_REQUEST 0x02U
#define PROTOCOL_TEST_HARNESS_OPCODE_ECHO_RESPONSE 0x81U
#define PROTOCOL_TEST_HARNESS_OPCODE_STATUS_RESPONSE 0x82U
#define PROTOCOL_TEST_HARNESS_SUPPORTED_FLAGS 0U
#define PROTOCOL_TEST_HARNESS_STATUS_SCHEMA_VERSION 2U
#define PROTOCOL_TEST_HARNESS_STATUS_FIELD_COUNT 32U
#define PROTOCOL_TEST_HARNESS_STATUS_PAYLOAD_SIZE \
    ( PROTOCOL_TEST_HARNESS_STATUS_FIELD_COUNT * sizeof( uint32_t ) )

typedef enum
{
    PROTOCOL_TEST_HARNESS_RESULT_OK = 0,
    PROTOCOL_TEST_HARNESS_RESULT_INVALID_ARGUMENT,
    PROTOCOL_TEST_HARNESS_RESULT_INVALID_MAGIC,
    PROTOCOL_TEST_HARNESS_RESULT_UNSUPPORTED_VERSION,
    PROTOCOL_TEST_HARNESS_RESULT_UNSUPPORTED_FLAGS,
    PROTOCOL_TEST_HARNESS_RESULT_UNSUPPORTED_OPCODE,
    PROTOCOL_TEST_HARNESS_RESULT_INVALID_LENGTH,
    PROTOCOL_TEST_HARNESS_RESULT_MESSAGE_TOO_LARGE,
    PROTOCOL_TEST_HARNESS_RESULT_BUFFER_TOO_SMALL
} PROTOCOL_TEST_HARNESS_Result_T;

typedef struct
{
    uint32_t link_state;
    uint32_t link_generation;
    uint32_t transport_event_count;
    uint32_t usb_rx_bytes;
    uint32_t usb_tx_bytes;
    uint32_t application_requests_received;
    uint32_t responses_submitted;
    uint32_t usb_tx_busy_retries;
    uint32_t invalid_hrtp_messages;
    uint32_t maximum_service_gap_ms;
    uint32_t transport_session_state;
    uint32_t compatibility_profile_id;
    uint32_t application_codec_initialized;
    uint32_t application_initialization_status;
    uint32_t non_hrtp_application_messages_received;
    uint32_t application_decode_failures;
    uint32_t application_semantic_rejections;
    uint32_t application_encode_failures;
    uint32_t configurations_accepted;
    uint32_t instructions_accepted;
    uint32_t results_encoded;
    uint32_t application_harness_state;
    uint32_t next_expected_tick;
    uint32_t active_expected_tick_count;
    uint32_t last_application_status;
    uint32_t last_decoded_application_message_type;
    uint32_t configuration_digest;
    uint32_t instruction_digest;
} PROTOCOL_TEST_HARNESS_Status_Data_T;

/**
 * Decode one complete request and build its complete response.
 *
 * Invalid requests are rejected without producing a response. No extra checksum
 * is added; Transport integrity plus ECHO comparison is the hardware-test oracle.
 */
PROTOCOL_TEST_HARNESS_Result_T PROTOCOL_TEST_HARNESS_Build_Response(
    const uint8_t* request, size_t request_length, size_t max_application_message_size,
    const PROTOCOL_TEST_HARNESS_Status_Data_T* status_data, uint8_t* response,
    size_t response_capacity, size_t* response_length );

#ifdef __cplusplus
}
#endif

#endif /* PROTOCOL_TEST_HARNESS_H */
