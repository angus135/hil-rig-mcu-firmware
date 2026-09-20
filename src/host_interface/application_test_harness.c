/******************************************************************************
 *  File:       application_test_harness.c
 *  Author:     OpenAI
 *  Created:    06-Sep-2026
 *
 *  Description:
 *      Test-only Application-layer transaction harness for Transport hardware
 *      testing. Application wire sizing, encoding, decoding and structural
 *      validation are delegated exclusively to the public shared codec.
 ******************************************************************************/

#include "application_test_harness.h"

#include "hil_rig_protocol/version.h"

#include <limits.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define APPLICATION_TEST_HARNESS_FNV1A_OFFSET_BASIS UINT32_C( 2166136261 )
#define APPLICATION_TEST_HARNESS_FNV1A_PRIME UINT32_C( 16777619 )
#define APPLICATION_TEST_HARNESS_ANALOG_ORACLE_MODULUS UINT32_C( 20000001 )

_Static_assert( APPLICATION_TEST_HARNESS_MAX_ENCODED_MESSAGE_SIZE
                    == HIL_APPLICATION_DEFAULT_MAX_MESSAGE_SIZE,
                "Application hardware-test message size must match the shared profile." );
_Static_assert( APPLICATION_TEST_HARNESS_MAX_VARIABLE_DATA_SIZE
                    == HIL_APPLICATION_ABSOLUTE_MAX_VARIABLE_DATA_SIZE,
                "Application hardware-test variable-data size must match the shared profile." );
_Static_assert( APPLICATION_TEST_HARNESS_MAX_EXPECTED_TICK_COUNT
                    == HIL_APPLICATION_ABSOLUTE_MAX_TICK_COUNT,
                "Application hardware-test tick limit must match the shared profile." );

typedef struct
{
    HIL_Application_Peripheral_Type_T peripheral_type;
    uint8_t channel;
    uint16_t data_offset;
    uint16_t size;
} APPLICATION_TEST_HARNESS_Retained_Record_T;

typedef struct
{
    uint8_t valid;
    uint32_t tick_number;
    uint16_t record_count;
    uint8_t data[APPLICATION_TEST_HARNESS_MAX_VARIABLE_ASSEMBLY_BYTES];
    APPLICATION_TEST_HARNESS_Retained_Record_T
        records[APPLICATION_TEST_HARNESS_MAX_VARIABLE_RECORDS_PER_TICK];
} APPLICATION_TEST_HARNESS_Retained_Tick_T;

typedef struct
{
    HIL_Application_Context_T context;
    HIL_Application_Message_T decoded_message;
    HIL_Application_Message_T outgoing_message;
    HIL_Application_Test_Id_T active_test_id;
    uint8_t                   protocol_version_confirmed;

    uint8_t digital_input_enabled[HIL_APPLICATION_DIGITAL_INPUT_CHANNEL_COUNT];
    uint8_t digital_output_enabled[HIL_APPLICATION_DIGITAL_OUTPUT_CHANNEL_COUNT];
    uint8_t digital_output_initial_high[HIL_APPLICATION_DIGITAL_OUTPUT_CHANNEL_COUNT];
    uint8_t analog_input_enabled[HIL_APPLICATION_ANALOG_INPUT_CHANNEL_COUNT];
    uint8_t analog_output_enabled[HIL_APPLICATION_ANALOG_OUTPUT_CHANNEL_COUNT];
    uint8_t pwm_input_enabled[HIL_APPLICATION_PWM_INPUT_CHANNEL_COUNT];
    uint8_t pwm_output_enabled[HIL_APPLICATION_PWM_OUTPUT_CHANNEL_COUNT];
    uint32_t pwm_output_initial_period[HIL_APPLICATION_PWM_OUTPUT_CHANNEL_COUNT];
    uint16_t pwm_output_initial_duty[HIL_APPLICATION_PWM_OUTPUT_CHANNEL_COUNT];
    uint8_t uart_enabled[HIL_APPLICATION_UART_CHANNEL_COUNT];
    uint8_t spi_enabled[HIL_APPLICATION_SPI_CHANNEL_COUNT];
    uint8_t can_enabled[HIL_APPLICATION_CAN_CHANNEL_COUNT];
    uint8_t i2c_enabled[HIL_APPLICATION_I2C_CHANNEL_COUNT];

    HIL_Application_Test_Instruction_T fixed_instructions[
        APPLICATION_TEST_HARNESS_MAX_RETAINED_TICKS];
    uint8_t fixed_instruction_valid[APPLICATION_TEST_HARNESS_MAX_RETAINED_TICKS];
    APPLICATION_TEST_HARNESS_Retained_Tick_T variable_ticks[
        APPLICATION_TEST_HARNESS_MAX_RETAINED_TICKS];
    APPLICATION_TEST_HARNESS_Retained_Record_T current_records[
        APPLICATION_TEST_HARNESS_MAX_VARIABLE_RECORDS_PER_TICK];
    uint8_t current_tick_data[APPLICATION_TEST_HARNESS_MAX_VARIABLE_ASSEMBLY_BYTES];
    HIL_Application_Captured_Record_T output_records[
        APPLICATION_TEST_HARNESS_MAX_RESULT_RECORDS_PER_CHUNK];
    uint16_t current_record_count;
    uint32_t current_tick_number;
    uint32_t last_submitted_tick;
    uint8_t has_submitted_tick;
    uint8_t selected_instruction_family;
    uint8_t selected_result_family;
    uint8_t selected_fault_mode;
    uint16_t synthetic_capture_capacity;
    uint32_t current_assembly_bytes;
    uint8_t current_chunk_count;
    uint8_t current_tick_active;
    uint32_t next_result_tick;
    uint16_t result_record_cursor;
    uint16_t result_total_record_count;
    uint8_t result_prepared;
    HIL_Application_Result_Condition_T result_condition;
    uint32_t result_problem_detail;
    uint8_t output_record_count;
    uint8_t output_tick_complete;
    uint8_t output_data[APPLICATION_TEST_HARNESS_MAX_ENCODED_MESSAGE_SIZE];
    uint16_t output_data_size;
    uint8_t output_pending;
    uint8_t output_encoded;

    APPLICATION_TEST_HARNESS_Diagnostics_T diagnostics;
} APPLICATION_TEST_HARNESS_State_Data_T;

_Alignas( HIL_APPLICATION_DECODE_STORAGE_ALIGNMENT ) static uint8_t
    s_application_decode_storage[APPLICATION_TEST_HARNESS_MAX_VARIABLE_ASSEMBLY_BYTES];
static APPLICATION_TEST_HARNESS_State_Data_T s_application_harness = { 0 };

static void APPLICATION_TEST_HARNESS_Increment( uint32_t* counter )
{
    if ( *counter != UINT32_MAX )
    {
        ( *counter )++;
    }
}

static void APPLICATION_TEST_HARNESS_Set_Status( HIL_Application_Status_T status )
{
    s_application_harness.diagnostics.last_application_status = ( uint32_t )status;
}

static HIL_Application_Status_T APPLICATION_TEST_HARNESS_Build_Response_For_Id(
    uint8_t* response, size_t response_capacity, size_t* response_size,
    HIL_Application_Response_Scope_T scope, HIL_Application_Response_Outcome_T outcome,
    HIL_Application_Response_Reason_T reason, uint32_t tick_number, uint32_t detail,
    const HIL_Application_Test_Id_T* response_test_id,
    HIL_Application_Control_Command_T control_command,
    HIL_Application_Global_Control_Command_T global_control_command );

static bool APPLICATION_TEST_HARNESS_Result_Chunks_Fit(
    const APPLICATION_TEST_HARNESS_Retained_Tick_T* retained );

static uint32_t APPLICATION_TEST_HARNESS_Fnv1a_Byte( uint32_t hash, uint8_t value )
{
    hash ^= value;
    return hash * APPLICATION_TEST_HARNESS_FNV1A_PRIME;
}

static uint32_t APPLICATION_TEST_HARNESS_Fnv1a_U8( uint32_t hash, uint8_t value )
{
    return APPLICATION_TEST_HARNESS_Fnv1a_Byte( hash, value );
}

static uint32_t APPLICATION_TEST_HARNESS_Fnv1a_U16( uint32_t hash, uint16_t value )
{
    hash = APPLICATION_TEST_HARNESS_Fnv1a_Byte( hash, ( uint8_t )( value & 0xffU ) );
    hash = APPLICATION_TEST_HARNESS_Fnv1a_Byte( hash, ( uint8_t )( ( value >> 8U ) & 0xffU ) );
    return hash;
}

static uint32_t APPLICATION_TEST_HARNESS_Fnv1a_U32( uint32_t hash, uint32_t value )
{
    hash = APPLICATION_TEST_HARNESS_Fnv1a_Byte( hash, ( uint8_t )( value & 0xffU ) );
    hash = APPLICATION_TEST_HARNESS_Fnv1a_Byte( hash, ( uint8_t )( ( value >> 8U ) & 0xffU ) );
    hash = APPLICATION_TEST_HARNESS_Fnv1a_Byte( hash, ( uint8_t )( ( value >> 16U ) & 0xffU ) );
    hash = APPLICATION_TEST_HARNESS_Fnv1a_Byte( hash, ( uint8_t )( ( value >> 24U ) & 0xffU ) );
    return hash;
}

static uint32_t APPLICATION_TEST_HARNESS_Configuration_Digest(
    const HIL_Application_Test_Configuration_T* configuration )
{
    uint32_t hash = APPLICATION_TEST_HARNESS_FNV1A_OFFSET_BASIS;

    hash = APPLICATION_TEST_HARNESS_Fnv1a_U32( hash, configuration->tick_duration_us.microseconds );
    hash = APPLICATION_TEST_HARNESS_Fnv1a_U32( hash, configuration->expected_tick_count );
    hash = APPLICATION_TEST_HARNESS_Fnv1a_U32( hash, configuration->flags );

    for ( size_t i = 0U; i < HIL_APPLICATION_DIGITAL_INPUT_CHANNEL_COUNT; ++i )
    {
        hash = APPLICATION_TEST_HARNESS_Fnv1a_U8( hash, configuration->digital_in[i].enabled );
        hash = APPLICATION_TEST_HARNESS_Fnv1a_U8(
            hash, ( uint8_t )configuration->digital_in[i].voltage_level );
    }
    for ( size_t i = 0U; i < HIL_APPLICATION_DIGITAL_OUTPUT_CHANNEL_COUNT; ++i )
    {
        hash = APPLICATION_TEST_HARNESS_Fnv1a_U8( hash, configuration->digital_out[i].enabled );
        hash = APPLICATION_TEST_HARNESS_Fnv1a_U8(
            hash, ( uint8_t )configuration->digital_out[i].voltage_level );
        hash =
            APPLICATION_TEST_HARNESS_Fnv1a_U8( hash, configuration->digital_out[i].initial_high );
    }
    for ( size_t i = 0U; i < HIL_APPLICATION_ANALOG_INPUT_CHANNEL_COUNT; ++i )
    {
        hash = APPLICATION_TEST_HARNESS_Fnv1a_U8( hash, configuration->analog_in[i].enabled );
    }
    for ( size_t i = 0U; i < HIL_APPLICATION_ANALOG_OUTPUT_CHANNEL_COUNT; ++i )
    {
        hash = APPLICATION_TEST_HARNESS_Fnv1a_U8( hash, configuration->analog_out[i].enabled );
    }
    for ( size_t i = 0U; i < HIL_APPLICATION_PWM_INPUT_CHANNEL_COUNT; ++i )
    {
        hash = APPLICATION_TEST_HARNESS_Fnv1a_U8( hash, configuration->pwm_in[i].enabled );
        hash = APPLICATION_TEST_HARNESS_Fnv1a_U8(
            hash, ( uint8_t )configuration->pwm_in[i].voltage_level );
    }
    for ( size_t i = 0U; i < HIL_APPLICATION_PWM_OUTPUT_CHANNEL_COUNT; ++i )
    {
        hash = APPLICATION_TEST_HARNESS_Fnv1a_U8( hash, configuration->pwm_out[i].enabled );
        hash = APPLICATION_TEST_HARNESS_Fnv1a_U8(
            hash, ( uint8_t )configuration->pwm_out[i].voltage_level );
        hash = APPLICATION_TEST_HARNESS_Fnv1a_U32(
            hash, configuration->pwm_out[i].initial_period_nanoseconds );
        hash = APPLICATION_TEST_HARNESS_Fnv1a_U16(
            hash, configuration->pwm_out[i].initial_duty_cycle_permyriad );
    }
    for ( size_t i = 0U; i < HIL_APPLICATION_CAN_CHANNEL_COUNT; ++i )
    {
        hash = APPLICATION_TEST_HARNESS_Fnv1a_U8( hash, configuration->can[i].enabled );
        hash = APPLICATION_TEST_HARNESS_Fnv1a_U32( hash, configuration->can[i].bit_rate );
        hash = APPLICATION_TEST_HARNESS_Fnv1a_U16( hash, configuration->can[i].filter_id );
        hash = APPLICATION_TEST_HARNESS_Fnv1a_U16( hash, configuration->can[i].filter_mask );
    }
    for ( size_t i = 0U; i < HIL_APPLICATION_SPI_CHANNEL_COUNT; ++i )
    {
        hash = APPLICATION_TEST_HARNESS_Fnv1a_U8( hash, configuration->spi[i].enabled );
        hash = APPLICATION_TEST_HARNESS_Fnv1a_U32( hash, configuration->spi[i].bit_rate );
        hash = APPLICATION_TEST_HARNESS_Fnv1a_U8( hash, ( uint8_t )configuration->spi[i].role );
        hash =
            APPLICATION_TEST_HARNESS_Fnv1a_U8( hash, ( uint8_t )configuration->spi[i].data_width );
        hash =
            APPLICATION_TEST_HARNESS_Fnv1a_U8( hash, ( uint8_t )configuration->spi[i].bit_order );
        hash = APPLICATION_TEST_HARNESS_Fnv1a_U8( hash,
                                                  ( uint8_t )configuration->spi[i].clock_polarity );
        hash =
            APPLICATION_TEST_HARNESS_Fnv1a_U8( hash, ( uint8_t )configuration->spi[i].clock_phase );
    }
    for ( size_t i = 0U; i < HIL_APPLICATION_UART_CHANNEL_COUNT; ++i )
    {
        hash = APPLICATION_TEST_HARNESS_Fnv1a_U8( hash, configuration->uart[i].enabled );
        hash = APPLICATION_TEST_HARNESS_Fnv1a_U32( hash, configuration->uart[i].baud_rate );
        hash = APPLICATION_TEST_HARNESS_Fnv1a_U8(
            hash, ( uint8_t )configuration->uart[i].electrical_mode );
        hash = APPLICATION_TEST_HARNESS_Fnv1a_U8( hash,
                                                  ( uint8_t )configuration->uart[i].word_length );
        hash = APPLICATION_TEST_HARNESS_Fnv1a_U8( hash, ( uint8_t )configuration->uart[i].parity );
        hash =
            APPLICATION_TEST_HARNESS_Fnv1a_U8( hash, ( uint8_t )configuration->uart[i].stop_bits );
        hash = APPLICATION_TEST_HARNESS_Fnv1a_U8( hash, configuration->uart[i].rx_enabled );
        hash = APPLICATION_TEST_HARNESS_Fnv1a_U8( hash, configuration->uart[i].tx_enabled );
    }
    for ( size_t i = 0U; i < HIL_APPLICATION_I2C_CHANNEL_COUNT; ++i )
    {
        hash = APPLICATION_TEST_HARNESS_Fnv1a_U8( hash, configuration->i2c[i].enabled );
        hash = APPLICATION_TEST_HARNESS_Fnv1a_U32( hash, configuration->i2c[i].bit_rate );
        hash = APPLICATION_TEST_HARNESS_Fnv1a_U8( hash, ( uint8_t )configuration->i2c[i].role );
        hash = APPLICATION_TEST_HARNESS_Fnv1a_U16( hash, configuration->i2c[i].own_address_7bit );
        hash = APPLICATION_TEST_HARNESS_Fnv1a_U8( hash,
                                                  ( uint8_t )configuration->i2c[i].voltage_level );
        hash = APPLICATION_TEST_HARNESS_Fnv1a_U8( hash, ( uint8_t )configuration->i2c[i].pull_up );
    }

    hash = APPLICATION_TEST_HARNESS_Fnv1a_U8( hash, configuration->extension_data.size );
    for ( size_t i = 0U; i < configuration->extension_data.size; ++i )
    {
        hash = APPLICATION_TEST_HARNESS_Fnv1a_Byte( hash, configuration->extension_data.data[i] );
    }

    return hash;
}

static uint32_t
APPLICATION_TEST_HARNESS_Instruction_Digest( const HIL_Application_Test_Instruction_T* instruction )
{
    uint32_t hash = APPLICATION_TEST_HARNESS_FNV1A_OFFSET_BASIS;

    hash = APPLICATION_TEST_HARNESS_Fnv1a_U32( hash, instruction->tick_number );
    for ( size_t i = 0U; i < HIL_APPLICATION_DIGITAL_OUTPUT_CHANNEL_COUNT; ++i )
    {
        hash = APPLICATION_TEST_HARNESS_Fnv1a_U8( hash, instruction->digital_outputs[i].high );
    }
    for ( size_t i = 0U; i < HIL_APPLICATION_ANALOG_OUTPUT_CHANNEL_COUNT; ++i )
    {
        hash =
            APPLICATION_TEST_HARNESS_Fnv1a_U32( hash, instruction->analog_outputs[i].microvolts );
    }
    for ( size_t i = 0U; i < HIL_APPLICATION_PWM_OUTPUT_CHANNEL_COUNT; ++i )
    {
        hash = APPLICATION_TEST_HARNESS_Fnv1a_U32( hash,
                                                   instruction->pwm_outputs[i].period_nanoseconds );
        hash = APPLICATION_TEST_HARNESS_Fnv1a_U16(
            hash, instruction->pwm_outputs[i].duty_cycle_permyriad );
    }

    return hash;
}

static void APPLICATION_TEST_HARNESS_Record_Decode_Failure( HIL_Application_Status_T status )
{
    APPLICATION_TEST_HARNESS_Increment( &s_application_harness.diagnostics.decode_failures );
    APPLICATION_TEST_HARNESS_Set_Status( status );
}

static void APPLICATION_TEST_HARNESS_Clear_Current_Upload( void )
{
    memset( s_application_harness.current_records, 0,
            sizeof( s_application_harness.current_records ) );
    memset( s_application_harness.current_tick_data, 0,
            sizeof( s_application_harness.current_tick_data ) );
    s_application_harness.current_record_count = 0U;
    s_application_harness.current_assembly_bytes = 0U;
    s_application_harness.current_chunk_count = 0U;
    s_application_harness.current_tick_active = 0U;
}

static HIL_Application_Status_T APPLICATION_TEST_HARNESS_Reject( HIL_Application_Status_T status )
{
    APPLICATION_TEST_HARNESS_Increment( &s_application_harness.diagnostics.semantic_rejections );
    if ( s_application_harness.diagnostics.state == APPLICATION_TEST_HARNESS_STATE_ACCEPTING_INSTRUCTIONS
         || s_application_harness.diagnostics.state == APPLICATION_TEST_HARNESS_STATE_READY_TO_START )
    {
        s_application_harness.diagnostics.state = APPLICATION_TEST_HARNESS_STATE_UPLOAD_INVALID;
    }
    APPLICATION_TEST_HARNESS_Set_Status( status );
    return status;
}

static HIL_Application_Status_T APPLICATION_TEST_HARNESS_Reject_Response(
    HIL_Application_Status_T status, HIL_Application_Response_Scope_T scope,
    const HIL_Application_Test_Id_T* response_test_id, uint32_t tick_number,
    HIL_Application_Response_Reason_T reason, HIL_Application_Control_Command_T control_command,
    HIL_Application_Global_Control_Command_T global_control_command, uint8_t* response,
    size_t response_capacity, size_t* response_size )
{
    APPLICATION_TEST_HARNESS_Increment( &s_application_harness.diagnostics.semantic_rejections );
    APPLICATION_TEST_HARNESS_Clear_Current_Upload();
    if ( s_application_harness.diagnostics.state
             == APPLICATION_TEST_HARNESS_STATE_ACCEPTING_INSTRUCTIONS
         || s_application_harness.diagnostics.state
                == APPLICATION_TEST_HARNESS_STATE_READY_TO_START )
    {
        s_application_harness.diagnostics.state = APPLICATION_TEST_HARNESS_STATE_UPLOAD_INVALID;
    }
    APPLICATION_TEST_HARNESS_Set_Status( status );
    HIL_Application_Status_T encode_status = APPLICATION_TEST_HARNESS_Build_Response_For_Id(
        response, response_capacity, response_size, scope,
        HIL_APPLICATION_RESPONSE_OUTCOME_REJECTED, reason, tick_number, 0U, response_test_id,
        control_command, global_control_command );
    return encode_status == HIL_APPLICATION_STATUS_OK ? status : encode_status;
}

static HIL_Application_Status_T APPLICATION_TEST_HARNESS_Reject_Unconfirmed_Request(
    uint8_t* response, size_t response_capacity, size_t* response_size )
{
    const HIL_Application_Message_T* message = &s_application_harness.decoded_message;
    switch ( message->type )
    {
        case HIL_APPLICATION_MESSAGE_TYPE_TEST_CONFIGURATION:
            return APPLICATION_TEST_HARNESS_Reject_Response(
                HIL_APPLICATION_STATUS_VERSION_MISMATCH,
                HIL_APPLICATION_RESPONSE_SCOPE_TEST_CONFIGURATION, &message->test_id, 0U,
                HIL_APPLICATION_RESPONSE_REASON_OPERATION_NOT_ALLOWED,
                HIL_APPLICATION_CONTROL_INVALID, HIL_APPLICATION_GLOBAL_CONTROL_INVALID, response,
                response_capacity, response_size );
        case HIL_APPLICATION_MESSAGE_TYPE_TEST_INSTRUCTION:
            return APPLICATION_TEST_HARNESS_Reject_Response(
                HIL_APPLICATION_STATUS_VERSION_MISMATCH, HIL_APPLICATION_RESPONSE_SCOPE_TICK,
                &message->test_id, message->body.test_instruction.tick_number,
                HIL_APPLICATION_RESPONSE_REASON_OPERATION_NOT_ALLOWED,
                HIL_APPLICATION_CONTROL_INVALID, HIL_APPLICATION_GLOBAL_CONTROL_INVALID, response,
                response_capacity, response_size );
        case HIL_APPLICATION_MESSAGE_TYPE_UPDATE_INSTRUCTION:
            return APPLICATION_TEST_HARNESS_Reject_Response(
                HIL_APPLICATION_STATUS_VERSION_MISMATCH, HIL_APPLICATION_RESPONSE_SCOPE_TICK,
                &message->test_id, message->body.update_instruction.tick_number,
                HIL_APPLICATION_RESPONSE_REASON_OPERATION_NOT_ALLOWED,
                HIL_APPLICATION_CONTROL_INVALID, HIL_APPLICATION_GLOBAL_CONTROL_INVALID, response,
                response_capacity, response_size );
        case HIL_APPLICATION_MESSAGE_TYPE_FINALIZE_TEST_UPLOAD:
            return APPLICATION_TEST_HARNESS_Reject_Response(
                HIL_APPLICATION_STATUS_VERSION_MISMATCH,
                HIL_APPLICATION_RESPONSE_SCOPE_COMPLETE_TEST, &message->test_id, 0U,
                HIL_APPLICATION_RESPONSE_REASON_OPERATION_NOT_ALLOWED,
                HIL_APPLICATION_CONTROL_INVALID, HIL_APPLICATION_GLOBAL_CONTROL_INVALID, response,
                response_capacity, response_size );
        case HIL_APPLICATION_MESSAGE_TYPE_EXECUTION_CONTROL:
            return APPLICATION_TEST_HARNESS_Reject_Response(
                HIL_APPLICATION_STATUS_VERSION_MISMATCH,
                HIL_APPLICATION_RESPONSE_SCOPE_EXECUTION_CONTROL, &message->test_id, 0U,
                HIL_APPLICATION_RESPONSE_REASON_OPERATION_NOT_ALLOWED,
                message->body.execution_control.command, HIL_APPLICATION_GLOBAL_CONTROL_INVALID,
                response, response_capacity, response_size );
        case HIL_APPLICATION_MESSAGE_TYPE_GLOBAL_CONTROL:
            return APPLICATION_TEST_HARNESS_Reject_Response(
                HIL_APPLICATION_STATUS_VERSION_MISMATCH,
                HIL_APPLICATION_RESPONSE_SCOPE_GLOBAL_CONTROL, NULL, 0U,
                HIL_APPLICATION_RESPONSE_REASON_OPERATION_NOT_ALLOWED,
                HIL_APPLICATION_CONTROL_INVALID, message->body.global_control.command, response,
                response_capacity, response_size );
        default:
            return APPLICATION_TEST_HARNESS_Reject( HIL_APPLICATION_STATUS_VERSION_MISMATCH );
    }
}

static HIL_Application_Status_T
APPLICATION_TEST_HARNESS_Encode_Message( const HIL_Application_Message_T* outgoing_message,
                                         uint8_t* response, size_t response_capacity,
                                         size_t* response_size )
{
    size_t                   encoded_size = 0U;
    size_t                   output_size  = 0U;
    HIL_Application_Status_T status;

    status = HIL_APPLICATION_Encoded_Size( &s_application_harness.context, outgoing_message,
                                           &encoded_size );
    if ( status != HIL_APPLICATION_STATUS_OK || encoded_size == 0U
         || encoded_size > APPLICATION_TEST_HARNESS_MAX_ENCODED_MESSAGE_SIZE )
    {
        if ( status == HIL_APPLICATION_STATUS_OK )
        {
            status = HIL_APPLICATION_STATUS_INTERNAL_ERROR;
        }
        APPLICATION_TEST_HARNESS_Increment( &s_application_harness.diagnostics.encode_failures );
        APPLICATION_TEST_HARNESS_Set_Status( status );
        return status;
    }

    status = HIL_APPLICATION_Encode_Message( &s_application_harness.context, outgoing_message,
                                             response, response_capacity, &output_size );
    if ( status != HIL_APPLICATION_STATUS_OK || output_size != encoded_size )
    {
        if ( status == HIL_APPLICATION_STATUS_OK )
        {
            status = HIL_APPLICATION_STATUS_INTERNAL_ERROR;
        }
        APPLICATION_TEST_HARNESS_Increment( &s_application_harness.diagnostics.encode_failures );
        APPLICATION_TEST_HARNESS_Set_Status( status );
        return status;
    }

    *response_size = output_size;
    return HIL_APPLICATION_STATUS_OK;
}

static HIL_Application_Status_T APPLICATION_TEST_HARNESS_Build_Response_For_Id(
    uint8_t* response, size_t response_capacity, size_t* response_size,
    HIL_Application_Response_Scope_T scope, HIL_Application_Response_Outcome_T outcome,
    HIL_Application_Response_Reason_T reason, uint32_t tick_number, uint32_t detail,
    const HIL_Application_Test_Id_T* response_test_id,
    HIL_Application_Control_Command_T control_command,
    HIL_Application_Global_Control_Command_T global_control_command )
{
    HIL_Application_Response_T* application_response;

    memset( &s_application_harness.outgoing_message, 0,
            sizeof( s_application_harness.outgoing_message ) );
    s_application_harness.outgoing_message.type = HIL_APPLICATION_MESSAGE_TYPE_RESPONSE;
    s_application_harness.outgoing_message.subtype = HIL_APPLICATION_MESSAGE_SUBTYPE_NONE;
    s_application_harness.outgoing_message.has_test_id = response_test_id != NULL ? 1U : 0U;
    if ( response_test_id != NULL )
    {
        s_application_harness.outgoing_message.test_id = *response_test_id;
    }
    application_response = &s_application_harness.outgoing_message.body.response;
    application_response->scope = scope;
    application_response->outcome = outcome;
    application_response->reason = reason;
    application_response->tick_number = tick_number;
    application_response->control_command = control_command;
    application_response->global_control_command = global_control_command;
    application_response->detail = detail;
    return APPLICATION_TEST_HARNESS_Encode_Message( &s_application_harness.outgoing_message,
                                                    response, response_capacity, response_size );
}

static HIL_Application_Status_T APPLICATION_TEST_HARNESS_Build_Response(
    uint8_t* response, size_t response_capacity, size_t* response_size,
    HIL_Application_Response_Scope_T scope, HIL_Application_Response_Outcome_T outcome,
    HIL_Application_Response_Reason_T reason, uint32_t tick_number, uint32_t detail,
    uint8_t has_test_id, HIL_Application_Control_Command_T control_command,
    HIL_Application_Global_Control_Command_T global_control_command )
{
    return APPLICATION_TEST_HARNESS_Build_Response_For_Id(
        response, response_capacity, response_size, scope, outcome, reason, tick_number, detail,
        has_test_id != 0U ? &s_application_harness.active_test_id : NULL, control_command,
        global_control_command );
}

static void APPLICATION_TEST_HARNESS_Select_Profile(
    const HIL_Application_Test_Configuration_T* configuration )
{
    const HIL_Application_Byte_Span_T extension = configuration->extension_data;

    s_application_harness.selected_result_family = 0U;
    s_application_harness.selected_fault_mode = 0U;
    s_application_harness.synthetic_capture_capacity = 0U;
    if ( extension.size >= 8U && memcmp( extension.data, "HTV3", 4U ) == 0 )
    {
        s_application_harness.selected_result_family = extension.data[4U];
        s_application_harness.selected_fault_mode = extension.data[5U];
        s_application_harness.synthetic_capture_capacity =
            ( uint16_t )extension.data[6U] | ( uint16_t )( ( uint16_t )extension.data[7U] << 8U );
        if ( s_application_harness.synthetic_capture_capacity == 0U )
        {
            s_application_harness.synthetic_capture_capacity = 4096U;
        }
        s_application_harness.diagnostics.selected_test_profile =
            s_application_harness.selected_result_family +
            ( ( uint32_t )s_application_harness.selected_fault_mode << 8U );
        s_application_harness.diagnostics.selected_fault_mode =
            s_application_harness.selected_fault_mode;
    }
}

static void APPLICATION_TEST_HARNESS_Clear_Active_Transaction( void )
{
    memset( &s_application_harness.active_test_id, 0,
            sizeof( s_application_harness.active_test_id ) );
    memset( s_application_harness.digital_input_enabled, 0,
            sizeof( s_application_harness.digital_input_enabled ) );
    memset( s_application_harness.digital_output_enabled, 0,
            sizeof( s_application_harness.digital_output_enabled ) );
    memset( s_application_harness.digital_output_initial_high, 0,
            sizeof( s_application_harness.digital_output_initial_high ) );
    memset( s_application_harness.analog_input_enabled, 0,
            sizeof( s_application_harness.analog_input_enabled ) );
    memset( s_application_harness.analog_output_enabled, 0,
            sizeof( s_application_harness.analog_output_enabled ) );
    memset( s_application_harness.pwm_input_enabled, 0,
            sizeof( s_application_harness.pwm_input_enabled ) );
    memset( s_application_harness.pwm_output_enabled, 0,
            sizeof( s_application_harness.pwm_output_enabled ) );
    memset( s_application_harness.pwm_output_initial_period, 0,
            sizeof( s_application_harness.pwm_output_initial_period ) );
    memset( s_application_harness.pwm_output_initial_duty, 0,
            sizeof( s_application_harness.pwm_output_initial_duty ) );
    memset( s_application_harness.uart_enabled, 0,
            sizeof( s_application_harness.uart_enabled ) );
    memset( s_application_harness.spi_enabled, 0,
            sizeof( s_application_harness.spi_enabled ) );
    memset( s_application_harness.can_enabled, 0,
            sizeof( s_application_harness.can_enabled ) );
    memset( s_application_harness.i2c_enabled, 0,
            sizeof( s_application_harness.i2c_enabled ) );
    memset( s_application_harness.fixed_instructions, 0,
            sizeof( s_application_harness.fixed_instructions ) );
    memset( s_application_harness.fixed_instruction_valid, 0,
            sizeof( s_application_harness.fixed_instruction_valid ) );
    memset( s_application_harness.variable_ticks, 0,
            sizeof( s_application_harness.variable_ticks ) );
    memset( s_application_harness.current_records, 0,
            sizeof( s_application_harness.current_records ) );
    memset( s_application_harness.current_tick_data, 0,
            sizeof( s_application_harness.current_tick_data ) );
    memset( &s_application_harness.outgoing_message, 0,
            sizeof( s_application_harness.outgoing_message ) );
    s_application_harness.current_record_count = 0U;
    s_application_harness.current_tick_number = 0U;
    s_application_harness.last_submitted_tick = 0U;
    s_application_harness.has_submitted_tick = 0U;
    s_application_harness.selected_instruction_family = 0U;
    s_application_harness.selected_result_family = 0U;
    s_application_harness.selected_fault_mode = 0U;
    s_application_harness.synthetic_capture_capacity = 0U;
    s_application_harness.current_assembly_bytes = 0U;
    s_application_harness.current_chunk_count = 0U;
    s_application_harness.current_tick_active = 0U;
    s_application_harness.next_result_tick = 0U;
    s_application_harness.result_record_cursor = 0U;
    s_application_harness.result_total_record_count = 0U;
    s_application_harness.result_prepared = 0U;
    s_application_harness.result_condition = HIL_APPLICATION_RESULT_CONDITION_OK;
    s_application_harness.result_problem_detail = 0U;
    s_application_harness.output_record_count = 0U;
    s_application_harness.output_tick_complete = 0U;
    memset( s_application_harness.output_data, 0, sizeof( s_application_harness.output_data ) );
    s_application_harness.output_data_size = 0U;
    s_application_harness.output_pending = 0U;
    s_application_harness.output_encoded = 0U;

    s_application_harness.diagnostics.next_expected_tick         = 0U;
    s_application_harness.diagnostics.active_expected_tick_count = 0U;
    s_application_harness.diagnostics.selected_instruction_family = 0U;
    s_application_harness.diagnostics.selected_result_family = 0U;
    s_application_harness.diagnostics.completed_instruction_ticks = 0U;
    s_application_harness.diagnostics.current_chunk_count = 0U;
    s_application_harness.diagnostics.selected_test_profile = 0U;
    s_application_harness.diagnostics.selected_fault_mode = 0U;
    s_application_harness.diagnostics.spontaneous_output_pending = 0U;
    s_application_harness.diagnostics.state =
        s_application_harness.diagnostics.codec_initialized != 0U
            ? APPLICATION_TEST_HARNESS_STATE_WAITING_FOR_CONFIGURATION
            : APPLICATION_TEST_HARNESS_STATE_UNINITIALIZED;
}

static HIL_Application_Status_T
APPLICATION_TEST_HARNESS_Init_Internal( const HIL_Application_Config_T* config )
{
    HIL_Application_Status_T status;

    memset( &s_application_harness, 0, sizeof( s_application_harness ) );
    memset( s_application_decode_storage, 0, sizeof( s_application_decode_storage ) );
    s_application_harness.diagnostics.state = APPLICATION_TEST_HARNESS_STATE_UNINITIALIZED;

    status = HIL_APPLICATION_Init( &s_application_harness.context, config );
    s_application_harness.diagnostics.initialization_status = ( uint32_t )status;
    APPLICATION_TEST_HARNESS_Set_Status( status );
    if ( status != HIL_APPLICATION_STATUS_OK )
    {
        return status;
    }

    s_application_harness.diagnostics.codec_initialized = 1U;
    s_application_harness.diagnostics.state =
        APPLICATION_TEST_HARNESS_STATE_WAITING_FOR_CONFIGURATION;
    return HIL_APPLICATION_STATUS_OK;
}

HIL_Application_Status_T APPLICATION_TEST_HARNESS_Init( void )
{
    const HIL_Application_Config_T config = {
        .max_encoded_message_size        = APPLICATION_TEST_HARNESS_MAX_ENCODED_MESSAGE_SIZE,
        .max_variable_data_size          = APPLICATION_TEST_HARNESS_MAX_VARIABLE_DATA_SIZE,
        .max_expected_tick_count         = APPLICATION_TEST_HARNESS_MAX_EXPECTED_TICK_COUNT,
    };

    return APPLICATION_TEST_HARNESS_Init_Internal( &config );
}

void APPLICATION_TEST_HARNESS_Reset_Transaction( void )
{
    APPLICATION_TEST_HARNESS_Clear_Active_Transaction();
}

void APPLICATION_TEST_HARNESS_Reset_Session( void )
{
    APPLICATION_TEST_HARNESS_Clear_Active_Transaction();
    s_application_harness.protocol_version_confirmed = 0U;
}

static HIL_Application_Status_T APPLICATION_TEST_HARNESS_Accept_Configuration(
    uint8_t* response, size_t response_capacity, size_t* response_size )
{
    const HIL_Application_Test_Configuration_T* configuration =
        &s_application_harness.decoded_message.body.test_configuration;
    const HIL_Application_Test_Id_T request_test_id = s_application_harness.decoded_message.test_id;
    const uint32_t digest = APPLICATION_TEST_HARNESS_Configuration_Digest( configuration );

    if ( s_application_harness.diagnostics.state
         == APPLICATION_TEST_HARNESS_STATE_ACCEPTING_INSTRUCTIONS )
    {
        return APPLICATION_TEST_HARNESS_Reject_Response(
            HIL_APPLICATION_STATUS_VALIDATION_FAILED,
            HIL_APPLICATION_RESPONSE_SCOPE_TEST_CONFIGURATION, &request_test_id, 0U,
            HIL_APPLICATION_RESPONSE_REASON_OPERATION_NOT_ALLOWED, HIL_APPLICATION_CONTROL_INVALID,
            HIL_APPLICATION_GLOBAL_CONTROL_INVALID, response, response_capacity, response_size );
    }
    if ( s_application_harness.diagnostics.state
             != APPLICATION_TEST_HARNESS_STATE_WAITING_FOR_CONFIGURATION
         && s_application_harness.diagnostics.state != APPLICATION_TEST_HARNESS_STATE_COMPLETE
         && s_application_harness.diagnostics.state
                != APPLICATION_TEST_HARNESS_STATE_UPLOAD_INVALID )
    {
        return APPLICATION_TEST_HARNESS_Reject_Response(
            HIL_APPLICATION_STATUS_VALIDATION_FAILED,
            HIL_APPLICATION_RESPONSE_SCOPE_TEST_CONFIGURATION, &request_test_id, 0U,
            HIL_APPLICATION_RESPONSE_REASON_OPERATION_NOT_ALLOWED, HIL_APPLICATION_CONTROL_INVALID,
            HIL_APPLICATION_GLOBAL_CONTROL_INVALID, response, response_capacity, response_size );
    }
    if ( configuration->expected_tick_count > APPLICATION_TEST_HARNESS_MAX_RETAINED_TICKS )
    {
        return APPLICATION_TEST_HARNESS_Reject_Response(
            HIL_APPLICATION_STATUS_BUFFER_TOO_SMALL,
            HIL_APPLICATION_RESPONSE_SCOPE_TEST_CONFIGURATION, &request_test_id, 0U,
            HIL_APPLICATION_RESPONSE_REASON_STORAGE_UNAVAILABLE, HIL_APPLICATION_CONTROL_INVALID,
            HIL_APPLICATION_GLOBAL_CONTROL_INVALID, response, response_capacity, response_size );
    }
    if ( configuration->extension_data.size >= 4U
         && memcmp( configuration->extension_data.data, "HTV3", 4U ) == 0
         && ( configuration->extension_data.size < 8U
              || configuration->extension_data.data[4U] > 1U
              || configuration->extension_data.data[5U] > 2U ) )
    {
        return APPLICATION_TEST_HARNESS_Reject_Response(
            HIL_APPLICATION_STATUS_VALIDATION_FAILED,
            HIL_APPLICATION_RESPONSE_SCOPE_TEST_CONFIGURATION, &request_test_id, 0U,
            HIL_APPLICATION_RESPONSE_REASON_VALIDATION_FAILED, HIL_APPLICATION_CONTROL_INVALID,
            HIL_APPLICATION_GLOBAL_CONTROL_INVALID, response, response_capacity, response_size );
    }

    APPLICATION_TEST_HARNESS_Clear_Active_Transaction();
    s_application_harness.active_test_id = request_test_id;
    APPLICATION_TEST_HARNESS_Select_Profile( configuration );
    for ( size_t i = 0U; i < HIL_APPLICATION_DIGITAL_INPUT_CHANNEL_COUNT; ++i )
    {
        s_application_harness.digital_input_enabled[i] = configuration->digital_in[i].enabled;
        s_application_harness.digital_output_enabled[i] = configuration->digital_out[i].enabled;
        s_application_harness.digital_output_initial_high[i] =
            configuration->digital_out[i].initial_high;
    }
    for ( size_t i = 0U; i < HIL_APPLICATION_ANALOG_INPUT_CHANNEL_COUNT; ++i )
    {
        s_application_harness.analog_input_enabled[i] = configuration->analog_in[i].enabled;
    }
    for ( size_t i = 0U; i < HIL_APPLICATION_ANALOG_OUTPUT_CHANNEL_COUNT; ++i )
    {
        s_application_harness.analog_output_enabled[i] = configuration->analog_out[i].enabled;
    }
    for ( size_t i = 0U; i < HIL_APPLICATION_PWM_INPUT_CHANNEL_COUNT; ++i )
    {
        s_application_harness.pwm_input_enabled[i] = configuration->pwm_in[i].enabled;
        s_application_harness.pwm_output_enabled[i] = configuration->pwm_out[i].enabled;
        s_application_harness.pwm_output_initial_period[i] =
            configuration->pwm_out[i].initial_period_nanoseconds;
        s_application_harness.pwm_output_initial_duty[i] =
            configuration->pwm_out[i].initial_duty_cycle_permyriad;
    }
    for ( size_t i = 0U; i < HIL_APPLICATION_UART_CHANNEL_COUNT; ++i )
    {
        s_application_harness.uart_enabled[i] = configuration->uart[i].enabled;
    }
    for ( size_t i = 0U; i < HIL_APPLICATION_SPI_CHANNEL_COUNT; ++i )
    {
        s_application_harness.spi_enabled[i] = configuration->spi[i].enabled;
    }
    for ( size_t i = 0U; i < HIL_APPLICATION_CAN_CHANNEL_COUNT; ++i )
    {
        s_application_harness.can_enabled[i] = configuration->can[i].enabled;
    }
    for ( size_t i = 0U; i < HIL_APPLICATION_I2C_CHANNEL_COUNT; ++i )
    {
        s_application_harness.i2c_enabled[i] = configuration->i2c[i].enabled;
    }

    s_application_harness.diagnostics.next_expected_tick = 0U;
    s_application_harness.diagnostics.active_expected_tick_count =
        configuration->expected_tick_count;
    s_application_harness.diagnostics.configuration_digest = digest;
    s_application_harness.diagnostics.state = APPLICATION_TEST_HARNESS_STATE_ACCEPTING_INSTRUCTIONS;
    s_application_harness.diagnostics.selected_instruction_family = 0U;
    s_application_harness.diagnostics.selected_result_family =
        s_application_harness.selected_result_family;
    APPLICATION_TEST_HARNESS_Increment(
        &s_application_harness.diagnostics.configurations_accepted );
    APPLICATION_TEST_HARNESS_Set_Status( HIL_APPLICATION_STATUS_OK );
    return APPLICATION_TEST_HARNESS_Build_Response(
        response, response_capacity, response_size,
        HIL_APPLICATION_RESPONSE_SCOPE_TEST_CONFIGURATION,
        HIL_APPLICATION_RESPONSE_OUTCOME_ACCEPTED, HIL_APPLICATION_RESPONSE_REASON_NONE, 0U, 0U,
        1U, HIL_APPLICATION_CONTROL_INVALID, HIL_APPLICATION_GLOBAL_CONTROL_INVALID );
}

static void
APPLICATION_TEST_HARNESS_Build_Result( const HIL_Application_Test_Instruction_T* instruction,
                                       uint32_t instruction_digest )
{
    HIL_Application_Test_Result_T* result;

    memset( &s_application_harness.outgoing_message, 0,
            sizeof( s_application_harness.outgoing_message ) );
    s_application_harness.outgoing_message.type        = HIL_APPLICATION_MESSAGE_TYPE_TEST_RESULT;
    s_application_harness.outgoing_message.subtype     = HIL_APPLICATION_MESSAGE_SUBTYPE_NONE;
    s_application_harness.outgoing_message.has_test_id = 1U;
    s_application_harness.outgoing_message.test_id     = s_application_harness.active_test_id;

    result              = &s_application_harness.outgoing_message.body.test_result;
    result->tick_number = instruction->tick_number;
    for ( size_t i = 0U; i < HIL_APPLICATION_DIGITAL_INPUT_CHANNEL_COUNT; ++i )
    {
        result->digital_inputs[i].high = s_application_harness.digital_input_enabled[i] != 0U
                                             ? instruction->digital_outputs[i].high
                                             : 0U;
    }

    result->analog_inputs[0].microvolts =
        s_application_harness.analog_input_enabled[0] != 0U
            ? s_application_harness.diagnostics.configuration_digest
                  % APPLICATION_TEST_HARNESS_ANALOG_ORACLE_MODULUS
            : 0U;
    result->analog_inputs[1].microvolts =
        s_application_harness.analog_input_enabled[1] != 0U
            ? instruction_digest % APPLICATION_TEST_HARNESS_ANALOG_ORACLE_MODULUS
            : 0U;

    for ( size_t i = 0U; i < HIL_APPLICATION_PWM_INPUT_CHANNEL_COUNT; ++i )
    {
        if ( s_application_harness.pwm_input_enabled[i] != 0U )
        {
            result->pwm_inputs[i].period_nanoseconds =
                instruction->pwm_outputs[i].period_nanoseconds;
            result->pwm_inputs[i].duty_cycle_permyriad =
                instruction->pwm_outputs[i].duty_cycle_permyriad;
        }
        else
        {
            result->pwm_inputs[i].period_nanoseconds   = 0U;
            result->pwm_inputs[i].duty_cycle_permyriad = 0U;
        }
    }

    result->condition      = HIL_APPLICATION_RESULT_CONDITION_OK;
    result->problem_detail = 0U;
}

static HIL_Application_Status_T
APPLICATION_TEST_HARNESS_Accept_Instruction( uint8_t* response, size_t response_capacity,
                                             size_t* response_size )
{
    const HIL_Application_Test_Instruction_T* instruction =
        &s_application_harness.decoded_message.body.test_instruction;
    const HIL_Application_Test_Id_T request_test_id = s_application_harness.decoded_message.test_id;

    if ( s_application_harness.diagnostics.state
         != APPLICATION_TEST_HARNESS_STATE_ACCEPTING_INSTRUCTIONS )
    {
        return APPLICATION_TEST_HARNESS_Reject_Response(
            HIL_APPLICATION_STATUS_VALIDATION_FAILED,
            HIL_APPLICATION_RESPONSE_SCOPE_TICK, &request_test_id, instruction->tick_number,
            HIL_APPLICATION_RESPONSE_REASON_OPERATION_NOT_ALLOWED, HIL_APPLICATION_CONTROL_INVALID,
            HIL_APPLICATION_GLOBAL_CONTROL_INVALID, response, response_capacity, response_size );
    }
    if ( memcmp( s_application_harness.decoded_message.test_id.bytes,
                 s_application_harness.active_test_id.bytes, HIL_APPLICATION_TEST_ID_SIZE )
         != 0 )
    {
        return APPLICATION_TEST_HARNESS_Reject_Response(
            HIL_APPLICATION_STATUS_INCONSISTENT_TEST_ID,
            HIL_APPLICATION_RESPONSE_SCOPE_TICK, &request_test_id, instruction->tick_number,
            HIL_APPLICATION_RESPONSE_REASON_INCONSISTENT_TEST_ID, HIL_APPLICATION_CONTROL_INVALID,
            HIL_APPLICATION_GLOBAL_CONTROL_INVALID, response, response_capacity, response_size );
    }
    if ( s_application_harness.selected_instruction_family == 2U )
    {
        return APPLICATION_TEST_HARNESS_Reject_Response(
            HIL_APPLICATION_STATUS_VALIDATION_FAILED,
            HIL_APPLICATION_RESPONSE_SCOPE_TICK, &request_test_id, instruction->tick_number,
            HIL_APPLICATION_RESPONSE_REASON_OPERATION_NOT_ALLOWED, HIL_APPLICATION_CONTROL_INVALID,
            HIL_APPLICATION_GLOBAL_CONTROL_INVALID, response, response_capacity, response_size );
    }
    if ( instruction->tick_number >= APPLICATION_TEST_HARNESS_MAX_RETAINED_TICKS
         || instruction->tick_number >= s_application_harness.diagnostics.active_expected_tick_count
         || ( s_application_harness.has_submitted_tick != 0U
              && instruction->tick_number <= s_application_harness.last_submitted_tick )
         || s_application_harness.fixed_instruction_valid[instruction->tick_number] != 0U )
    {
        return APPLICATION_TEST_HARNESS_Reject_Response(
            HIL_APPLICATION_STATUS_INCONSISTENT_TICK,
            HIL_APPLICATION_RESPONSE_SCOPE_TICK, &request_test_id, instruction->tick_number,
            HIL_APPLICATION_RESPONSE_REASON_INVALID_TICK, HIL_APPLICATION_CONTROL_INVALID,
            HIL_APPLICATION_GLOBAL_CONTROL_INVALID, response, response_capacity, response_size );
    }

    s_application_harness.selected_instruction_family = 1U;
    s_application_harness.diagnostics.selected_instruction_family = 1U;
    s_application_harness.fixed_instructions[instruction->tick_number] = *instruction;
    s_application_harness.fixed_instruction_valid[instruction->tick_number] = 1U;
    s_application_harness.last_submitted_tick = instruction->tick_number;
    s_application_harness.has_submitted_tick = 1U;
    s_application_harness.diagnostics.instruction_digest =
        APPLICATION_TEST_HARNESS_Instruction_Digest( instruction );
    APPLICATION_TEST_HARNESS_Increment( &s_application_harness.diagnostics.instructions_accepted );
    APPLICATION_TEST_HARNESS_Increment(
        &s_application_harness.diagnostics.completed_instruction_ticks );
    s_application_harness.diagnostics.next_expected_tick = instruction->tick_number + 1U;
    APPLICATION_TEST_HARNESS_Set_Status( HIL_APPLICATION_STATUS_OK );
    return APPLICATION_TEST_HARNESS_Build_Response(
        response, response_capacity, response_size, HIL_APPLICATION_RESPONSE_SCOPE_TICK,
        HIL_APPLICATION_RESPONSE_OUTCOME_ACCEPTED, HIL_APPLICATION_RESPONSE_REASON_NONE,
        instruction->tick_number, 0U, 1U, HIL_APPLICATION_CONTROL_INVALID,
        HIL_APPLICATION_GLOBAL_CONTROL_INVALID );
}

static HIL_Application_Status_T
APPLICATION_TEST_HARNESS_Handle_System_Info_Request( uint8_t* response, size_t response_capacity,
                                                     size_t* response_size )
{
    const HIL_Application_System_Info_Request_T* request =
        &s_application_harness.decoded_message.body.system_info_request;
    HIL_Application_Status_T                version_status;
    HIL_Application_Status_T                status;
    HIL_Application_System_Info_Response_T* system_info_response;

    version_status = HIL_APPLICATION_Check_Protocol_Version( request->application_protocol_major,
                                                             request->application_protocol_minor,
                                                             request->application_protocol_patch );

    memset( &s_application_harness.outgoing_message, 0,
            sizeof( s_application_harness.outgoing_message ) );
    s_application_harness.outgoing_message.type = HIL_APPLICATION_MESSAGE_TYPE_SYSTEM_INFO_RESPONSE;
    s_application_harness.outgoing_message.subtype     = HIL_APPLICATION_MESSAGE_SUBTYPE_BASIC;
    s_application_harness.outgoing_message.has_test_id = 0U;

    system_info_response = &s_application_harness.outgoing_message.body.system_info_response;
    system_info_response->application_protocol_major = HIL_RIG_PROTOCOL_VERSION_MAJOR;
    system_info_response->application_protocol_minor = HIL_RIG_PROTOCOL_VERSION_MINOR;
    system_info_response->application_protocol_patch = HIL_RIG_PROTOCOL_VERSION_PATCH;
    system_info_response->firmware_version_major     = 0U;
    system_info_response->firmware_version_minor     = 0U;
    system_info_response->firmware_version_patch     = 0U;

    status = APPLICATION_TEST_HARNESS_Encode_Message( &s_application_harness.outgoing_message,
                                                      response, response_capacity, response_size );
    if ( status != HIL_APPLICATION_STATUS_OK )
    {
        s_application_harness.protocol_version_confirmed = 0U;
        return status;
    }

    s_application_harness.protocol_version_confirmed =
        version_status == HIL_APPLICATION_STATUS_OK ? 1U : 0U;
    APPLICATION_TEST_HARNESS_Set_Status( HIL_APPLICATION_STATUS_OK );
    return HIL_APPLICATION_STATUS_OK;
}

static uint32_t APPLICATION_TEST_HARNESS_Aligned_Record_Size( uint16_t payload_size )
{
    return 4U + ( ( uint32_t )payload_size + 3U ) / 4U * 4U;
}

static bool APPLICATION_TEST_HARNESS_Is_Communication( HIL_Application_Peripheral_Type_T type )
{
    return type == HIL_APPLICATION_PERIPHERAL_UART || type == HIL_APPLICATION_PERIPHERAL_SPI
           || type == HIL_APPLICATION_PERIPHERAL_CAN;
}

static bool APPLICATION_TEST_HARNESS_Has_Fixed_Pair( const HIL_Application_Logical_Operation_T* op )
{
    for ( uint16_t i = 0U; i < s_application_harness.current_record_count; ++i )
    {
        const APPLICATION_TEST_HARNESS_Retained_Record_T* retained =
            &s_application_harness.current_records[i];
        if ( !APPLICATION_TEST_HARNESS_Is_Communication( op->peripheral_type )
             && retained->peripheral_type == op->peripheral_type
             && retained->channel == op->channel )
        {
            return true;
        }
    }
    return false;
}

static bool APPLICATION_TEST_HARNESS_Operation_Is_Enabled(
    const HIL_Application_Logical_Operation_T* operation )
{
    switch ( operation->peripheral_type )
    {
        case HIL_APPLICATION_PERIPHERAL_DIGITAL_OUTPUT:
            return operation->channel < HIL_APPLICATION_DIGITAL_OUTPUT_CHANNEL_COUNT
                   && s_application_harness.digital_output_enabled[operation->channel] != 0U
                   && s_application_harness.digital_input_enabled[operation->channel] != 0U;
        case HIL_APPLICATION_PERIPHERAL_ANALOG_OUTPUT:
            return operation->channel < HIL_APPLICATION_ANALOG_OUTPUT_CHANNEL_COUNT
                   && s_application_harness.analog_output_enabled[operation->channel] != 0U
                   && s_application_harness.analog_input_enabled[operation->channel <
                                                                    HIL_APPLICATION_ANALOG_INPUT_CHANNEL_COUNT
                                                                    ? operation->channel
                                                                    : 0U]
                          != 0U;
        case HIL_APPLICATION_PERIPHERAL_PWM_OUTPUT:
            return operation->channel < HIL_APPLICATION_PWM_OUTPUT_CHANNEL_COUNT
                   && s_application_harness.pwm_output_enabled[operation->channel] != 0U
                   && s_application_harness.pwm_input_enabled[operation->channel] != 0U;
        case HIL_APPLICATION_PERIPHERAL_UART:
            return operation->channel < HIL_APPLICATION_UART_CHANNEL_COUNT
                   && s_application_harness.uart_enabled[operation->channel] != 0U;
        case HIL_APPLICATION_PERIPHERAL_SPI:
            return operation->channel < HIL_APPLICATION_SPI_CHANNEL_COUNT
                   && s_application_harness.spi_enabled[operation->channel] != 0U;
        case HIL_APPLICATION_PERIPHERAL_CAN:
            return operation->channel < HIL_APPLICATION_CAN_CHANNEL_COUNT
                   && s_application_harness.can_enabled[operation->channel] != 0U;
        case HIL_APPLICATION_PERIPHERAL_I2C:
        default:
            return false;
    }
}

static HIL_Application_Status_T APPLICATION_TEST_HARNESS_Accept_Update_Instruction(
    uint8_t* response, size_t response_capacity, size_t* response_size )
{
    const HIL_Application_Update_Instruction_T* update =
        &s_application_harness.decoded_message.body.update_instruction;
    const HIL_Application_Test_Id_T request_test_id = s_application_harness.decoded_message.test_id;

    if ( s_application_harness.diagnostics.state
         != APPLICATION_TEST_HARNESS_STATE_ACCEPTING_INSTRUCTIONS )
    {
        return APPLICATION_TEST_HARNESS_Reject_Response(
            HIL_APPLICATION_STATUS_VALIDATION_FAILED, HIL_APPLICATION_RESPONSE_SCOPE_TICK,
            &request_test_id, update->tick_number,
            HIL_APPLICATION_RESPONSE_REASON_OPERATION_NOT_ALLOWED, HIL_APPLICATION_CONTROL_INVALID,
            HIL_APPLICATION_GLOBAL_CONTROL_INVALID, response, response_capacity, response_size );
    }
    if ( s_application_harness.selected_instruction_family == 1U )
    {
        return APPLICATION_TEST_HARNESS_Reject_Response(
            HIL_APPLICATION_STATUS_VALIDATION_FAILED, HIL_APPLICATION_RESPONSE_SCOPE_TICK,
            &request_test_id, update->tick_number,
            HIL_APPLICATION_RESPONSE_REASON_OPERATION_NOT_ALLOWED, HIL_APPLICATION_CONTROL_INVALID,
            HIL_APPLICATION_GLOBAL_CONTROL_INVALID, response, response_capacity, response_size );
    }
    if ( memcmp( s_application_harness.decoded_message.test_id.bytes,
                 s_application_harness.active_test_id.bytes, HIL_APPLICATION_TEST_ID_SIZE )
         != 0 )
    {
        return APPLICATION_TEST_HARNESS_Reject_Response(
            HIL_APPLICATION_STATUS_INCONSISTENT_TEST_ID, HIL_APPLICATION_RESPONSE_SCOPE_TICK,
            &request_test_id, update->tick_number,
            HIL_APPLICATION_RESPONSE_REASON_INCONSISTENT_TEST_ID, HIL_APPLICATION_CONTROL_INVALID,
            HIL_APPLICATION_GLOBAL_CONTROL_INVALID, response, response_capacity, response_size );
    }
    if ( update->tick_number >= APPLICATION_TEST_HARNESS_MAX_RETAINED_TICKS
         || update->tick_number >= s_application_harness.diagnostics.active_expected_tick_count )
    {
        return APPLICATION_TEST_HARNESS_Reject_Response(
            HIL_APPLICATION_STATUS_VALIDATION_FAILED, HIL_APPLICATION_RESPONSE_SCOPE_TICK,
            &request_test_id, update->tick_number,
            HIL_APPLICATION_RESPONSE_REASON_INVALID_TICK, HIL_APPLICATION_CONTROL_INVALID,
            HIL_APPLICATION_GLOBAL_CONTROL_INVALID, response, response_capacity, response_size );
    }
    if ( s_application_harness.current_tick_active == 0U )
    {
        if ( s_application_harness.has_submitted_tick != 0U
             && update->tick_number <= s_application_harness.last_submitted_tick )
        {
            return APPLICATION_TEST_HARNESS_Reject_Response(
                HIL_APPLICATION_STATUS_INCONSISTENT_TICK, HIL_APPLICATION_RESPONSE_SCOPE_TICK,
                &request_test_id, update->tick_number,
                HIL_APPLICATION_RESPONSE_REASON_INVALID_TICK, HIL_APPLICATION_CONTROL_INVALID,
                HIL_APPLICATION_GLOBAL_CONTROL_INVALID, response, response_capacity, response_size );
        }
        s_application_harness.current_tick_number = update->tick_number;
        s_application_harness.current_tick_active = 1U;
        s_application_harness.current_record_count = 0U;
        s_application_harness.current_assembly_bytes = 0U;
        s_application_harness.current_chunk_count = 0U;
    }
    else if ( update->tick_number != s_application_harness.current_tick_number )
    {
        return APPLICATION_TEST_HARNESS_Reject_Response(
            HIL_APPLICATION_STATUS_INCONSISTENT_TICK, HIL_APPLICATION_RESPONSE_SCOPE_TICK,
            &request_test_id, update->tick_number,
            HIL_APPLICATION_RESPONSE_REASON_INVALID_TICK, HIL_APPLICATION_CONTROL_INVALID,
            HIL_APPLICATION_GLOBAL_CONTROL_INVALID, response, response_capacity, response_size );
    }
    if ( s_application_harness.current_chunk_count >=
         APPLICATION_TEST_HARNESS_MAX_VARIABLE_TRANSFERS_PER_TICK )
    {
        return APPLICATION_TEST_HARNESS_Reject_Response(
            HIL_APPLICATION_STATUS_BUFFER_TOO_SMALL, HIL_APPLICATION_RESPONSE_SCOPE_TICK,
            &request_test_id, update->tick_number,
            HIL_APPLICATION_RESPONSE_REASON_STORAGE_UNAVAILABLE, HIL_APPLICATION_CONTROL_INVALID,
            HIL_APPLICATION_GLOBAL_CONTROL_INVALID, response, response_capacity, response_size );
    }

    for ( uint8_t i = 0U; i < update->operation_count; ++i )
    {
        const HIL_Application_Logical_Operation_T* operation = &update->operations[i];
        if ( s_application_harness.current_record_count >=
             sizeof( s_application_harness.current_records )
                 / sizeof( s_application_harness.current_records[0] )
             || APPLICATION_TEST_HARNESS_Has_Fixed_Pair( operation ) )
        {
            return APPLICATION_TEST_HARNESS_Reject_Response(
                HIL_APPLICATION_STATUS_VALIDATION_FAILED, HIL_APPLICATION_RESPONSE_SCOPE_TICK,
                &request_test_id, update->tick_number,
                HIL_APPLICATION_RESPONSE_REASON_VALIDATION_FAILED, HIL_APPLICATION_CONTROL_INVALID,
                HIL_APPLICATION_GLOBAL_CONTROL_INVALID, response, response_capacity, response_size );
        }
        if ( !APPLICATION_TEST_HARNESS_Operation_Is_Enabled( operation ) )
        {
            return APPLICATION_TEST_HARNESS_Reject_Response(
                HIL_APPLICATION_STATUS_NOT_IMPLEMENTED, HIL_APPLICATION_RESPONSE_SCOPE_TICK,
                &request_test_id, update->tick_number,
                HIL_APPLICATION_RESPONSE_REASON_OPERATION_NOT_ALLOWED, HIL_APPLICATION_CONTROL_INVALID,
                HIL_APPLICATION_GLOBAL_CONTROL_INVALID, response, response_capacity, response_size );
        }
        if ( s_application_harness.current_assembly_bytes + operation->payload.size
             > APPLICATION_TEST_HARNESS_MAX_VARIABLE_ASSEMBLY_BYTES )
        {
            return APPLICATION_TEST_HARNESS_Reject_Response(
                HIL_APPLICATION_STATUS_BUFFER_TOO_SMALL, HIL_APPLICATION_RESPONSE_SCOPE_TICK,
                &request_test_id, update->tick_number,
                HIL_APPLICATION_RESPONSE_REASON_STORAGE_UNAVAILABLE, HIL_APPLICATION_CONTROL_INVALID,
                HIL_APPLICATION_GLOBAL_CONTROL_INVALID, response, response_capacity, response_size );
        }
        APPLICATION_TEST_HARNESS_Retained_Record_T* retained =
            &s_application_harness.current_records[s_application_harness.current_record_count++];
        retained->peripheral_type = operation->peripheral_type;
        retained->channel = operation->channel;
        retained->data_offset = ( uint16_t )s_application_harness.current_assembly_bytes;
        retained->size = operation->payload.size;
        memcpy( &s_application_harness.current_tick_data[retained->data_offset],
                operation->payload.data, operation->payload.size );
        s_application_harness.current_assembly_bytes += operation->payload.size;
        APPLICATION_TEST_HARNESS_Increment(
            &s_application_harness.diagnostics.variable_operations_accepted );
    }
    s_application_harness.selected_instruction_family = 2U;
    s_application_harness.diagnostics.selected_instruction_family = 2U;
    s_application_harness.current_chunk_count++;
    s_application_harness.diagnostics.current_chunk_count =
        s_application_harness.current_chunk_count;
    if ( s_application_harness.current_chunk_count > s_application_harness.diagnostics.maximum_chunk_count )
    {
        s_application_harness.diagnostics.maximum_chunk_count =
            s_application_harness.current_chunk_count;
    }
    if ( update->flags == HIL_APPLICATION_INSTRUCTION_FLAG_HAS_MORE_CHUNKS )
    {
        return HIL_APPLICATION_STATUS_OK;
    }

    APPLICATION_TEST_HARNESS_Retained_Tick_T* tick =
        &s_application_harness.variable_ticks[s_application_harness.current_tick_number];
    tick->valid = 1U;
    tick->tick_number = s_application_harness.current_tick_number;
    tick->record_count = s_application_harness.current_record_count;
    memcpy( tick->records, s_application_harness.current_records,
            s_application_harness.current_record_count * sizeof( tick->records[0] ) );
    memcpy( tick->data, s_application_harness.current_tick_data,
            s_application_harness.current_assembly_bytes );
    s_application_harness.last_submitted_tick = s_application_harness.current_tick_number;
    s_application_harness.has_submitted_tick = 1U;
    APPLICATION_TEST_HARNESS_Clear_Current_Upload();
    APPLICATION_TEST_HARNESS_Increment(
        &s_application_harness.diagnostics.completed_instruction_ticks );
    APPLICATION_TEST_HARNESS_Set_Status( HIL_APPLICATION_STATUS_OK );
    return APPLICATION_TEST_HARNESS_Build_Response(
        response, response_capacity, response_size, HIL_APPLICATION_RESPONSE_SCOPE_TICK,
        HIL_APPLICATION_RESPONSE_OUTCOME_ACCEPTED, HIL_APPLICATION_RESPONSE_REASON_NONE,
        update->tick_number, 0U, 1U, HIL_APPLICATION_CONTROL_INVALID,
        HIL_APPLICATION_GLOBAL_CONTROL_INVALID );
}

static HIL_Application_Status_T APPLICATION_TEST_HARNESS_Finalize(
    uint8_t* response, size_t response_capacity, size_t* response_size )
{
    const HIL_Application_Finalize_Test_Upload_T* finalize =
        &s_application_harness.decoded_message.body.finalize_test_upload;
    const HIL_Application_Test_Id_T request_test_id = s_application_harness.decoded_message.test_id;
    APPLICATION_TEST_HARNESS_Increment( &s_application_harness.diagnostics.finalization_requests );
    if ( finalize->flags != 0U || s_application_harness.current_tick_active != 0U
         || s_application_harness.diagnostics.state
                != APPLICATION_TEST_HARNESS_STATE_ACCEPTING_INSTRUCTIONS )
    {
        return APPLICATION_TEST_HARNESS_Reject_Response(
            HIL_APPLICATION_STATUS_VALIDATION_FAILED,
            HIL_APPLICATION_RESPONSE_SCOPE_COMPLETE_TEST, &request_test_id, 0U,
            finalize->flags != 0U ? HIL_APPLICATION_RESPONSE_REASON_VALIDATION_FAILED
                                  : HIL_APPLICATION_RESPONSE_REASON_OPERATION_NOT_ALLOWED,
            HIL_APPLICATION_CONTROL_INVALID, HIL_APPLICATION_GLOBAL_CONTROL_INVALID, response,
            response_capacity, response_size );
    }
    if ( memcmp( s_application_harness.decoded_message.test_id.bytes,
                 s_application_harness.active_test_id.bytes, HIL_APPLICATION_TEST_ID_SIZE )
         != 0 )
    {
        return APPLICATION_TEST_HARNESS_Reject_Response(
            HIL_APPLICATION_STATUS_INCONSISTENT_TEST_ID,
            HIL_APPLICATION_RESPONSE_SCOPE_COMPLETE_TEST, &request_test_id, 0U,
            HIL_APPLICATION_RESPONSE_REASON_INCONSISTENT_TEST_ID, HIL_APPLICATION_CONTROL_INVALID,
            HIL_APPLICATION_GLOBAL_CONTROL_INVALID, response, response_capacity, response_size );
    }
    if ( s_application_harness.selected_result_family != 0U )
    {
        for ( uint32_t tick = 0U;
              tick < s_application_harness.diagnostics.active_expected_tick_count; ++tick )
        {
            if ( s_application_harness.variable_ticks[tick].valid != 0U
                 && !APPLICATION_TEST_HARNESS_Result_Chunks_Fit(
                     &s_application_harness.variable_ticks[tick] ) )
            {
                return APPLICATION_TEST_HARNESS_Reject_Response(
                    HIL_APPLICATION_STATUS_BUFFER_TOO_SMALL,
                    HIL_APPLICATION_RESPONSE_SCOPE_COMPLETE_TEST, &request_test_id, 0U,
                    HIL_APPLICATION_RESPONSE_REASON_STORAGE_UNAVAILABLE,
                    HIL_APPLICATION_CONTROL_INVALID, HIL_APPLICATION_GLOBAL_CONTROL_INVALID,
                    response, response_capacity, response_size );
            }
        }
    }
    s_application_harness.diagnostics.state = APPLICATION_TEST_HARNESS_STATE_READY_TO_START;
    APPLICATION_TEST_HARNESS_Increment( &s_application_harness.diagnostics.accepted_finalizations );
    APPLICATION_TEST_HARNESS_Set_Status( HIL_APPLICATION_STATUS_OK );
    return APPLICATION_TEST_HARNESS_Build_Response(
        response, response_capacity, response_size, HIL_APPLICATION_RESPONSE_SCOPE_COMPLETE_TEST,
        HIL_APPLICATION_RESPONSE_OUTCOME_ACCEPTED, HIL_APPLICATION_RESPONSE_REASON_NONE, 0U, 0U,
        1U, HIL_APPLICATION_CONTROL_INVALID, HIL_APPLICATION_GLOBAL_CONTROL_INVALID );
}

static HIL_Application_Status_T
APPLICATION_TEST_HARNESS_Handle_Execution_Control( uint8_t* response, size_t response_capacity,
                                                   size_t* response_size )
{
    const HIL_Application_Execution_Control_T* control =
        &s_application_harness.decoded_message.body.execution_control;
    const HIL_Application_Test_Id_T request_test_id = s_application_harness.decoded_message.test_id;
    const bool test_id_matches =
        memcmp( request_test_id.bytes, s_application_harness.active_test_id.bytes,
                HIL_APPLICATION_TEST_ID_SIZE ) == 0;

    if ( control->command != HIL_APPLICATION_CONTROL_START
         && control->command != HIL_APPLICATION_CONTROL_ABORT )
    {
        return APPLICATION_TEST_HARNESS_Reject_Response(
            HIL_APPLICATION_STATUS_INVALID_SUBTYPE,
            HIL_APPLICATION_RESPONSE_SCOPE_EXECUTION_CONTROL, &request_test_id, 0U,
            HIL_APPLICATION_RESPONSE_REASON_VALIDATION_FAILED, control->command,
            HIL_APPLICATION_GLOBAL_CONTROL_INVALID, response, response_capacity, response_size );
    }
    if ( control->command == HIL_APPLICATION_CONTROL_START )
    {
        if ( s_application_harness.diagnostics.state
             != APPLICATION_TEST_HARNESS_STATE_READY_TO_START )
        {
            return APPLICATION_TEST_HARNESS_Reject_Response(
                HIL_APPLICATION_STATUS_VALIDATION_FAILED,
                HIL_APPLICATION_RESPONSE_SCOPE_EXECUTION_CONTROL, &request_test_id, 0U,
                HIL_APPLICATION_RESPONSE_REASON_OPERATION_NOT_ALLOWED, control->command,
                HIL_APPLICATION_GLOBAL_CONTROL_INVALID, response, response_capacity, response_size );
        }
        if ( !test_id_matches )
        {
            return APPLICATION_TEST_HARNESS_Reject_Response(
                HIL_APPLICATION_STATUS_INCONSISTENT_TEST_ID,
                HIL_APPLICATION_RESPONSE_SCOPE_EXECUTION_CONTROL, &request_test_id, 0U,
                HIL_APPLICATION_RESPONSE_REASON_INCONSISTENT_TEST_ID, control->command,
                HIL_APPLICATION_GLOBAL_CONTROL_INVALID, response, response_capacity, response_size );
        }
        s_application_harness.diagnostics.state = APPLICATION_TEST_HARNESS_STATE_EMITTING_RESULTS;
        s_application_harness.next_result_tick = 0U;
        s_application_harness.result_record_cursor = 0U;
        s_application_harness.result_total_record_count = 0U;
        s_application_harness.result_prepared = 0U;
        s_application_harness.output_record_count = 0U;
        s_application_harness.output_tick_complete = 0U;
        s_application_harness.output_pending = 1U;
    }
    else if ( control->command == HIL_APPLICATION_CONTROL_ABORT )
    {
        if ( s_application_harness.diagnostics.state
                 != APPLICATION_TEST_HARNESS_STATE_WAITING_FOR_CONFIGURATION
             && !test_id_matches )
        {
            return APPLICATION_TEST_HARNESS_Reject_Response(
                HIL_APPLICATION_STATUS_INCONSISTENT_TEST_ID,
                HIL_APPLICATION_RESPONSE_SCOPE_EXECUTION_CONTROL, &request_test_id, 0U,
                HIL_APPLICATION_RESPONSE_REASON_INCONSISTENT_TEST_ID, control->command,
                HIL_APPLICATION_GLOBAL_CONTROL_INVALID, response, response_capacity, response_size );
        }
        APPLICATION_TEST_HARNESS_Reset_Transaction();
    }
    APPLICATION_TEST_HARNESS_Set_Status( HIL_APPLICATION_STATUS_OK );
    return APPLICATION_TEST_HARNESS_Build_Response_For_Id(
        response, response_capacity, response_size,
        HIL_APPLICATION_RESPONSE_SCOPE_EXECUTION_CONTROL,
        HIL_APPLICATION_RESPONSE_OUTCOME_COMPLETED, HIL_APPLICATION_RESPONSE_REASON_NONE, 0U, 0U,
        &request_test_id, control->command, HIL_APPLICATION_GLOBAL_CONTROL_INVALID );
}

static HIL_Application_Status_T
APPLICATION_TEST_HARNESS_Handle_Global_Control( uint8_t* response, size_t response_capacity,
                                                size_t* response_size )
{
    const HIL_Application_Global_Control_T* control =
        &s_application_harness.decoded_message.body.global_control;
    if ( control->command != HIL_APPLICATION_GLOBAL_CONTROL_RESET_APPLICATION
         || control->flags != 0U )
    {
        return APPLICATION_TEST_HARNESS_Reject_Response(
            HIL_APPLICATION_STATUS_VALIDATION_FAILED,
            HIL_APPLICATION_RESPONSE_SCOPE_GLOBAL_CONTROL, NULL, 0U,
            HIL_APPLICATION_RESPONSE_REASON_VALIDATION_FAILED, HIL_APPLICATION_CONTROL_INVALID,
            control->command, response, response_capacity, response_size );
    }
    APPLICATION_TEST_HARNESS_Reset_Transaction();
    APPLICATION_TEST_HARNESS_Set_Status( HIL_APPLICATION_STATUS_OK );
    return APPLICATION_TEST_HARNESS_Build_Response(
        response, response_capacity, response_size, HIL_APPLICATION_RESPONSE_SCOPE_GLOBAL_CONTROL,
        HIL_APPLICATION_RESPONSE_OUTCOME_COMPLETED, HIL_APPLICATION_RESPONSE_REASON_NONE, 0U, 0U,
        0U, HIL_APPLICATION_CONTROL_INVALID, control->command );
}

HIL_Application_Status_T APPLICATION_TEST_HARNESS_Handle_Message( const uint8_t* message,
                                                                  size_t         message_size,
                                                                  uint8_t*       response,
                                                                  size_t         response_capacity,
                                                                  size_t*        response_size )
{
    HIL_Application_Status_T status;
    size_t                   required_storage = 0U;
    size_t                   used_storage     = 0U;
    uint8_t*                 decode_storage   = NULL;

    if ( response_size != NULL )
    {
        *response_size = 0U;
    }

    if ( s_application_harness.diagnostics.codec_initialized == 0U )
    {
        APPLICATION_TEST_HARNESS_Set_Status( HIL_APPLICATION_STATUS_UNINITIALIZED );
        return HIL_APPLICATION_STATUS_UNINITIALIZED;
    }
    if ( message == NULL || message_size == 0U || response == NULL || response_size == NULL )
    {
        APPLICATION_TEST_HARNESS_Set_Status( HIL_APPLICATION_STATUS_INVALID_ARGUMENT );
        return HIL_APPLICATION_STATUS_INVALID_ARGUMENT;
    }

    APPLICATION_TEST_HARNESS_Increment(
        &s_application_harness.diagnostics.application_messages_received );

    status = HIL_APPLICATION_Decode_Storage_Size( &s_application_harness.context, message,
                                                  message_size, &required_storage );
    if ( status != HIL_APPLICATION_STATUS_OK )
    {
        if ( status == HIL_APPLICATION_STATUS_NOT_IMPLEMENTED )
        {
            APPLICATION_TEST_HARNESS_Increment(
                &s_application_harness.diagnostics.i2c_not_implemented_rejections );
        }
        APPLICATION_TEST_HARNESS_Record_Decode_Failure( status );
        return status;
    }
    if ( required_storage > sizeof( s_application_decode_storage ) )
    {
        status = HIL_APPLICATION_STATUS_BUFFER_TOO_SMALL;
        APPLICATION_TEST_HARNESS_Record_Decode_Failure( status );
        return status;
    }
    if ( required_storage > 0U )
    {
        decode_storage = s_application_decode_storage;
    }
    if ( required_storage > s_application_harness.diagnostics.maximum_decode_storage_required )
    {
        s_application_harness.diagnostics.maximum_decode_storage_required =
            ( uint32_t )required_storage;
    }

    status = HIL_APPLICATION_Decode_Message(
        &s_application_harness.context, message, message_size,
        &s_application_harness.decoded_message, decode_storage,
        decode_storage != NULL ? sizeof( s_application_decode_storage ) : 0U, &used_storage );
    if ( status != HIL_APPLICATION_STATUS_OK || used_storage != required_storage )
    {
        if ( status == HIL_APPLICATION_STATUS_OK )
        {
            status = HIL_APPLICATION_STATUS_INTERNAL_ERROR;
        }
        APPLICATION_TEST_HARNESS_Record_Decode_Failure( status );
        return status;
    }
    s_application_harness.diagnostics.decode_storage_used = ( uint32_t )used_storage;

    s_application_harness.diagnostics.last_decoded_message_type =
        ( uint32_t )s_application_harness.decoded_message.type;

    if ( s_application_harness.decoded_message.type
             != HIL_APPLICATION_MESSAGE_TYPE_SYSTEM_INFO_REQUEST
         && s_application_harness.protocol_version_confirmed == 0U )
    {
        return APPLICATION_TEST_HARNESS_Reject_Unconfirmed_Request(
            response, response_capacity, response_size );
    }

    switch ( s_application_harness.decoded_message.type )
    {
        case HIL_APPLICATION_MESSAGE_TYPE_SYSTEM_INFO_REQUEST:
            return APPLICATION_TEST_HARNESS_Handle_System_Info_Request( response, response_capacity,
                                                                        response_size );

        case HIL_APPLICATION_MESSAGE_TYPE_TEST_CONFIGURATION:
            return APPLICATION_TEST_HARNESS_Accept_Configuration(
                response, response_capacity, response_size );

        case HIL_APPLICATION_MESSAGE_TYPE_TEST_INSTRUCTION:
            return APPLICATION_TEST_HARNESS_Accept_Instruction( response, response_capacity,
                                                                response_size );

        case HIL_APPLICATION_MESSAGE_TYPE_UPDATE_INSTRUCTION:
            return APPLICATION_TEST_HARNESS_Accept_Update_Instruction(
                response, response_capacity, response_size );

        case HIL_APPLICATION_MESSAGE_TYPE_FINALIZE_TEST_UPLOAD:
            return APPLICATION_TEST_HARNESS_Finalize( response, response_capacity, response_size );

        case HIL_APPLICATION_MESSAGE_TYPE_EXECUTION_CONTROL:
            return APPLICATION_TEST_HARNESS_Handle_Execution_Control( response, response_capacity,
                                                                      response_size );

        case HIL_APPLICATION_MESSAGE_TYPE_GLOBAL_CONTROL:
            return APPLICATION_TEST_HARNESS_Handle_Global_Control( response, response_capacity,
                                                                   response_size );

        case HIL_APPLICATION_MESSAGE_TYPE_RESPONSE:
        case HIL_APPLICATION_MESSAGE_TYPE_ERROR:
            status = APPLICATION_TEST_HARNESS_Encode_Message(
                &s_application_harness.decoded_message, response, response_capacity,
                response_size );
            if ( status == HIL_APPLICATION_STATUS_OK )
            {
                APPLICATION_TEST_HARNESS_Set_Status( HIL_APPLICATION_STATUS_OK );
            }
            return status;

        case HIL_APPLICATION_MESSAGE_TYPE_INVALID:
        case HIL_APPLICATION_MESSAGE_TYPE_SYSTEM_INFO_RESPONSE:
        case HIL_APPLICATION_MESSAGE_TYPE_TEST_RESULT:
        case HIL_APPLICATION_MESSAGE_TYPE_VARIABLE_TEST_RESULT:
        case HIL_APPLICATION_MESSAGE_TYPE_RESERVED:
        default:
            return APPLICATION_TEST_HARNESS_Reject( HIL_APPLICATION_STATUS_UNSUPPORTED_MESSAGE );
    }
}

static HIL_Application_Peripheral_Type_T
APPLICATION_TEST_HARNESS_Result_Type( HIL_Application_Peripheral_Type_T type )
{
    switch ( type )
    {
        case HIL_APPLICATION_PERIPHERAL_DIGITAL_OUTPUT:
            return HIL_APPLICATION_PERIPHERAL_DIGITAL_INPUT;
        case HIL_APPLICATION_PERIPHERAL_ANALOG_OUTPUT:
            return HIL_APPLICATION_PERIPHERAL_ANALOG_INPUT;
        case HIL_APPLICATION_PERIPHERAL_PWM_OUTPUT:
            return HIL_APPLICATION_PERIPHERAL_PWM_INPUT;
        default:
            return type;
    }
}

static bool APPLICATION_TEST_HARNESS_Result_Has_Pair(
    uint8_t record_count, HIL_Application_Peripheral_Type_T peripheral_type, uint8_t channel )
{
    for ( uint8_t i = 0U; i < record_count; ++i )
    {
        if ( s_application_harness.output_records[i].peripheral_type == peripheral_type
             && s_application_harness.output_records[i].channel == channel )
        {
            return true;
        }
    }
    return false;
}

static uint16_t APPLICATION_TEST_HARNESS_Spi_Capture(
    const uint8_t* source, uint16_t source_size, uint8_t* destination, uint16_t destination_capacity )
{
    if ( source == NULL || destination == NULL || source_size == 0U )
    {
        return 0U;
    }
    const uint8_t packet_count = source[0];
    const uint16_t header_size = ( uint16_t )packet_count + 1U;
    if ( packet_count == 0U || header_size > source_size )
    {
        return 0U;
    }
    uint16_t data_size = ( uint16_t )( source_size - header_size );
    if ( data_size > destination_capacity )
    {
        return 0U;
    }
    memcpy( destination, &source[header_size], data_size );
    return data_size;
}

static bool APPLICATION_TEST_HARNESS_Result_Chunk_Has_Pair(
    const APPLICATION_TEST_HARNESS_Retained_Tick_T* retained, uint16_t first_record,
    uint16_t last_record, HIL_Application_Peripheral_Type_T peripheral_type, uint8_t channel )
{
    for ( uint16_t i = first_record; i < last_record; ++i )
    {
        if ( APPLICATION_TEST_HARNESS_Result_Type( retained->records[i].peripheral_type )
                 == peripheral_type
             && retained->records[i].channel == channel )
        {
            return true;
        }
    }
    return false;
}

static bool APPLICATION_TEST_HARNESS_Result_Chunks_Fit(
    const APPLICATION_TEST_HARNESS_Retained_Tick_T* retained )
{
    uint16_t cursor = 0U;
    uint8_t  chunk_count = 0U;

    while ( cursor < retained->record_count )
    {
        uint16_t encoded_record_bytes = 0U;
        uint8_t  record_count = 0U;
        const uint16_t chunk_start = cursor;

        if ( chunk_count >= APPLICATION_TEST_HARNESS_MAX_RESULT_CHUNKS_PER_TICK )
        {
            return false;
        }
        while ( cursor < retained->record_count )
        {
            const APPLICATION_TEST_HARNESS_Retained_Record_T* record =
                &retained->records[cursor];
            const HIL_Application_Peripheral_Type_T result_type =
                APPLICATION_TEST_HARNESS_Result_Type( record->peripheral_type );
            uint16_t result_size = record->size;
            if ( result_type == HIL_APPLICATION_PERIPHERAL_SPI )
            {
                const uint8_t packet_count = retained->data[record->data_offset];
                const uint16_t header_size = ( uint16_t )packet_count + 1U;
                if ( packet_count == 0U || header_size > record->size )
                {
                    return false;
                }
                result_size = ( uint16_t )( record->size - header_size );
            }
            const uint32_t record_size = APPLICATION_TEST_HARNESS_Aligned_Record_Size( result_size );
            if ( record_count >= APPLICATION_TEST_HARNESS_MAX_RESULT_RECORDS_PER_CHUNK
                 || ( record_count > 0U
                      && ( ( uint32_t )23U + 12U + encoded_record_bytes + record_size
                           > APPLICATION_TEST_HARNESS_MAX_ENCODED_MESSAGE_SIZE
                           || APPLICATION_TEST_HARNESS_Result_Chunk_Has_Pair(
                               retained, chunk_start, cursor, result_type, record->channel ) ) ) )
            {
                break;
            }
            encoded_record_bytes += ( uint16_t )record_size;
            record_count++;
            cursor++;
        }
        if ( record_count == 0U )
        {
            return false;
        }
        chunk_count++;
    }
    return true;
}

static void APPLICATION_TEST_HARNESS_Prepare_Variable_Result( uint32_t tick_number )
{
    const APPLICATION_TEST_HARNESS_Retained_Tick_T* retained =
        &s_application_harness.variable_ticks[tick_number];
    uint16_t visible_count = retained->valid != 0U ? retained->record_count : 0U;

    s_application_harness.result_record_cursor = 0U;
    s_application_harness.result_condition = HIL_APPLICATION_RESULT_CONDITION_OK;
    s_application_harness.result_problem_detail = 0U;
    if ( s_application_harness.selected_fault_mode == 2U )
    {
        visible_count = 0U;
        s_application_harness.result_condition =
            HIL_APPLICATION_RESULT_CONDITION_EXECUTION_PROBLEM;
        s_application_harness.result_problem_detail = 2U;
    }
    else if ( s_application_harness.selected_fault_mode == 1U && visible_count > 0U )
    {
        uint32_t used = 0U;
        uint16_t retained_count = 0U;
        for ( uint16_t i = 0U; i < visible_count; ++i )
        {
            if ( used + retained->records[i].size > s_application_harness.synthetic_capture_capacity )
            {
                break;
            }
            used += retained->records[i].size;
            retained_count++;
        }
        if ( retained_count < visible_count )
        {
            visible_count = retained_count;
            s_application_harness.result_condition = HIL_APPLICATION_RESULT_CONDITION_PARTIAL;
            s_application_harness.result_problem_detail =
                HIL_APPLICATION_RESULT_PROBLEM_DETAIL_CAPTURE_OVERFLOW;
            APPLICATION_TEST_HARNESS_Increment(
                &s_application_harness.diagnostics.capture_overflow_events );
        }
    }
    s_application_harness.result_total_record_count = visible_count;
    s_application_harness.result_prepared = 1U;
}

static void APPLICATION_TEST_HARNESS_Build_Variable_Result( uint32_t tick_number )
{
    HIL_Application_Variable_Test_Result_T* result;
    const APPLICATION_TEST_HARNESS_Retained_Tick_T* retained =
        &s_application_harness.variable_ticks[tick_number];
    uint16_t encoded_record_bytes = 0U;
    uint8_t record_count = 0U;

    if ( s_application_harness.result_prepared == 0U )
    {
        APPLICATION_TEST_HARNESS_Prepare_Variable_Result( tick_number );
    }

    memset( &s_application_harness.outgoing_message, 0,
            sizeof( s_application_harness.outgoing_message ) );
    s_application_harness.outgoing_message.type =
        HIL_APPLICATION_MESSAGE_TYPE_VARIABLE_TEST_RESULT;
    s_application_harness.outgoing_message.subtype = HIL_APPLICATION_MESSAGE_SUBTYPE_NONE;
    s_application_harness.outgoing_message.has_test_id = 1U;
    s_application_harness.outgoing_message.test_id = s_application_harness.active_test_id;
    result = &s_application_harness.outgoing_message.body.variable_test_result;
    result->tick_number = tick_number;
    result->condition = s_application_harness.result_condition;
    result->problem_detail = s_application_harness.result_problem_detail;
    result->flags = HIL_APPLICATION_RESULT_FLAG_COMPLETE_TICK;
    s_application_harness.output_data_size = 0U;

    for ( uint16_t i = s_application_harness.result_record_cursor;
          i < s_application_harness.result_total_record_count; ++i )
    {
        const APPLICATION_TEST_HARNESS_Retained_Record_T* retained_record = &retained->records[i];
        const HIL_Application_Peripheral_Type_T result_type =
            APPLICATION_TEST_HARNESS_Result_Type( retained_record->peripheral_type );
        const uint8_t* source = &retained->data[retained_record->data_offset];
        const uint8_t* result_data = source;
        uint16_t result_size = retained_record->size;
        if ( result_type == HIL_APPLICATION_PERIPHERAL_SPI )
        {
            result_data = &s_application_harness.output_data[s_application_harness.output_data_size];
            result_size = APPLICATION_TEST_HARNESS_Spi_Capture(
                source, retained_record->size, s_application_harness.output_data
                    + s_application_harness.output_data_size,
                ( uint16_t )( sizeof( s_application_harness.output_data )
                              - s_application_harness.output_data_size ) );
            if ( result_size == 0U )
            {
                break;
            }
            s_application_harness.output_data_size += result_size;
        }
        const uint32_t record_size = APPLICATION_TEST_HARNESS_Aligned_Record_Size( result_size );
        if ( record_count > 0U
             && ( ( uint32_t )23U + 12U + encoded_record_bytes + record_size
                  > APPLICATION_TEST_HARNESS_MAX_ENCODED_MESSAGE_SIZE
                  || APPLICATION_TEST_HARNESS_Result_Has_Pair( record_count, result_type,
                                                                retained_record->channel ) ) )
        {
            break;
        }
        if ( record_count >= APPLICATION_TEST_HARNESS_MAX_RESULT_RECORDS_PER_CHUNK )
        {
            break;
        }
        if ( result_size > UINT8_MAX )
        {
            break;
        }
        const uint8_t encoded_size = ( uint8_t )result_size;
        s_application_harness.output_records[record_count].peripheral_type = result_type;
        s_application_harness.output_records[record_count].channel = retained_record->channel;
        s_application_harness.output_records[record_count].data.data = result_data;
        s_application_harness.output_records[record_count].data.size = encoded_size;
        encoded_record_bytes += ( uint16_t )record_size;
        record_count++;
    }

    result->record_count = record_count;
    result->records = record_count == 0U ? NULL : s_application_harness.output_records;
    s_application_harness.output_record_count = record_count;
    s_application_harness.output_tick_complete =
        s_application_harness.result_record_cursor + record_count
        >= s_application_harness.result_total_record_count;
    result->flags = s_application_harness.output_tick_complete != 0U
                        ? HIL_APPLICATION_RESULT_FLAG_COMPLETE_TICK
                        : HIL_APPLICATION_RESULT_FLAG_HAS_MORE_CHUNKS;
}

static void APPLICATION_TEST_HARNESS_Build_Initial_Instruction(
    HIL_Application_Test_Instruction_T* instruction, uint32_t tick_number )
{
    memset( instruction, 0, sizeof( *instruction ) );
    instruction->tick_number = tick_number;
    for ( size_t i = 0U; i < HIL_APPLICATION_DIGITAL_OUTPUT_CHANNEL_COUNT; ++i )
    {
        instruction->digital_outputs[i].high = s_application_harness.digital_output_initial_high[i];
    }
    for ( size_t i = 0U; i < HIL_APPLICATION_PWM_OUTPUT_CHANNEL_COUNT; ++i )
    {
        instruction->pwm_outputs[i].period_nanoseconds =
            s_application_harness.pwm_output_initial_period[i];
        instruction->pwm_outputs[i].duty_cycle_permyriad =
            s_application_harness.pwm_output_initial_duty[i];
    }
}

static void APPLICATION_TEST_HARNESS_Build_Fixed_Result( uint32_t tick_number )
{
    HIL_Application_Test_Instruction_T initial_instruction;
    const HIL_Application_Test_Instruction_T* instruction;
    if ( s_application_harness.fixed_instruction_valid[tick_number] != 0U )
    {
        instruction = &s_application_harness.fixed_instructions[tick_number];
    }
    else
    {
        APPLICATION_TEST_HARNESS_Build_Initial_Instruction( &initial_instruction, tick_number );
        instruction = &initial_instruction;
    }
    APPLICATION_TEST_HARNESS_Build_Result(
        instruction, APPLICATION_TEST_HARNESS_Instruction_Digest( instruction ) );
    s_application_harness.output_record_count = 0U;
    s_application_harness.output_tick_complete = 1U;
    s_application_harness.outgoing_message.body.test_result.tick_number = tick_number;
    if ( s_application_harness.selected_fault_mode == 2U )
    {
        s_application_harness.outgoing_message.body.test_result.condition =
            HIL_APPLICATION_RESULT_CONDITION_EXECUTION_PROBLEM;
        s_application_harness.outgoing_message.body.test_result.problem_detail = 2U;
    }
}

HIL_Application_Status_T APPLICATION_TEST_HARNESS_Poll_Output( uint8_t* output,
                                                               size_t output_capacity,
                                                               size_t* output_size )
{
    HIL_Application_Status_T status;

    if ( output_size == NULL )
    {
        return HIL_APPLICATION_STATUS_INVALID_ARGUMENT;
    }
    *output_size = 0U;
    if ( s_application_harness.output_pending == 0U
         || s_application_harness.diagnostics.state
                != APPLICATION_TEST_HARNESS_STATE_EMITTING_RESULTS )
    {
        s_application_harness.diagnostics.spontaneous_output_pending = 0U;
        return HIL_APPLICATION_STATUS_INCOMPLETE_DATA;
    }
    if ( output == NULL )
    {
        return HIL_APPLICATION_STATUS_INVALID_ARGUMENT;
    }
    if ( s_application_harness.output_encoded == 0U
         && s_application_harness.selected_result_family == 0U )
    {
        APPLICATION_TEST_HARNESS_Build_Fixed_Result( s_application_harness.next_result_tick );
    }
    else if ( s_application_harness.output_encoded == 0U )
    {
        APPLICATION_TEST_HARNESS_Build_Variable_Result( s_application_harness.next_result_tick );
    }
    status = APPLICATION_TEST_HARNESS_Encode_Message( &s_application_harness.outgoing_message,
                                                      output, output_capacity, output_size );
    if ( status == HIL_APPLICATION_STATUS_OK )
    {
        s_application_harness.output_encoded = 1U;
        s_application_harness.diagnostics.spontaneous_output_pending = 1U;
    }
    return status;
}

HIL_Application_Status_T APPLICATION_TEST_HARNESS_Commit_Output( void )
{
    if ( s_application_harness.output_pending == 0U
         || s_application_harness.output_encoded == 0U )
    {
        return HIL_APPLICATION_STATUS_INCOMPLETE_DATA;
    }
    APPLICATION_TEST_HARNESS_Increment( &s_application_harness.diagnostics.result_messages_emitted );
    for ( uint8_t i = 0U; i < s_application_harness.output_record_count; ++i )
    {
        APPLICATION_TEST_HARNESS_Increment( &s_application_harness.diagnostics.result_records_emitted );
    }
    if ( s_application_harness.selected_result_family != 0U
         && s_application_harness.output_tick_complete == 0U )
    {
        s_application_harness.result_record_cursor =
            ( uint16_t )( s_application_harness.result_record_cursor
                          + s_application_harness.output_record_count );
        s_application_harness.output_encoded = 0U;
        s_application_harness.diagnostics.spontaneous_output_pending = 1U;
        return HIL_APPLICATION_STATUS_OK;
    }
    APPLICATION_TEST_HARNESS_Increment( &s_application_harness.diagnostics.results_encoded );
    s_application_harness.next_result_tick++;
    s_application_harness.result_record_cursor = 0U;
    s_application_harness.result_total_record_count = 0U;
    s_application_harness.result_prepared = 0U;
    s_application_harness.output_encoded = 0U;
    if ( s_application_harness.next_result_tick
         >= s_application_harness.diagnostics.active_expected_tick_count )
    {
        s_application_harness.output_pending = 0U;
        s_application_harness.diagnostics.spontaneous_output_pending = 0U;
        s_application_harness.diagnostics.state = APPLICATION_TEST_HARNESS_STATE_COMPLETE;
    }
    return HIL_APPLICATION_STATUS_OK;
}

const APPLICATION_TEST_HARNESS_Diagnostics_T* APPLICATION_TEST_HARNESS_Get_Diagnostics( void )
{
    return &s_application_harness.diagnostics;
}

#ifdef TEST_BUILD
void APPLICATION_TEST_HARNESS_Test_Reset( void )
{
    memset( &s_application_harness, 0, sizeof( s_application_harness ) );
    memset( s_application_decode_storage, 0, sizeof( s_application_decode_storage ) );
}

HIL_Application_Status_T
APPLICATION_TEST_HARNESS_Test_Init_With_Config( const HIL_Application_Config_T* config )
{
    if ( config == NULL )
    {
        APPLICATION_TEST_HARNESS_Test_Reset();
        s_application_harness.diagnostics.initialization_status =
            HIL_APPLICATION_STATUS_INVALID_ARGUMENT;
        APPLICATION_TEST_HARNESS_Set_Status( HIL_APPLICATION_STATUS_INVALID_ARGUMENT );
        return HIL_APPLICATION_STATUS_INVALID_ARGUMENT;
    }
    return APPLICATION_TEST_HARNESS_Init_Internal( config );
}

uintptr_t APPLICATION_TEST_HARNESS_Test_Decode_Storage_Address( void )
{
    return ( uintptr_t )s_application_decode_storage;
}
#endif
