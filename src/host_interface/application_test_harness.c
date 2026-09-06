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

#include <limits.h>
#include <stddef.h>
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
_Static_assert( APPLICATION_TEST_HARNESS_MAX_VARIABLE_TRANSFERS_PER_TICK
                    == HIL_APPLICATION_ABSOLUTE_MAX_VARIABLE_DATA_COUNT_PTICK,
                "Application hardware-test transfer count must match the shared profile." );
_Static_assert( APPLICATION_TEST_HARNESS_MAX_EXPECTED_TICK_COUNT
                    == HIL_APPLICATION_ABSOLUTE_MAX_TICK_COUNT,
                "Application hardware-test tick limit must match the shared profile." );

typedef struct
{
    HIL_Application_Context_T context;
    HIL_Application_Message_T decoded_message;
    HIL_Application_Message_T outgoing_message;
    HIL_Application_Test_Id_T active_test_id;

    uint8_t digital_input_enabled[HIL_APPLICATION_DIGITAL_INPUT_CHANNEL_COUNT];
    uint8_t analog_input_enabled[HIL_APPLICATION_ANALOG_INPUT_CHANNEL_COUNT];
    uint8_t pwm_input_enabled[HIL_APPLICATION_PWM_INPUT_CHANNEL_COUNT];

    APPLICATION_TEST_HARNESS_Diagnostics_T diagnostics;
} APPLICATION_TEST_HARNESS_State_Data_T;

_Alignas( HIL_APPLICATION_DECODE_STORAGE_ALIGNMENT ) static uint8_t
    s_application_decode_storage[APPLICATION_TEST_HARNESS_MAX_VARIABLE_DATA_SIZE];
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
        hash =
            APPLICATION_TEST_HARNESS_Fnv1a_U32( hash, configuration->can[i].capture_limit_bytes );
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
        hash =
            APPLICATION_TEST_HARNESS_Fnv1a_U32( hash, configuration->spi[i].capture_limit_bytes );
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
        hash =
            APPLICATION_TEST_HARNESS_Fnv1a_U32( hash, configuration->uart[i].capture_limit_bytes );
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
        hash =
            APPLICATION_TEST_HARNESS_Fnv1a_U32( hash, configuration->i2c[i].capture_limit_bytes );
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

static HIL_Application_Status_T APPLICATION_TEST_HARNESS_Reject( HIL_Application_Status_T status )
{
    APPLICATION_TEST_HARNESS_Increment( &s_application_harness.diagnostics.semantic_rejections );
    APPLICATION_TEST_HARNESS_Set_Status( status );
    return status;
}

static void APPLICATION_TEST_HARNESS_Clear_Active_Transaction( void )
{
    memset( &s_application_harness.active_test_id, 0,
            sizeof( s_application_harness.active_test_id ) );
    memset( s_application_harness.digital_input_enabled, 0,
            sizeof( s_application_harness.digital_input_enabled ) );
    memset( s_application_harness.analog_input_enabled, 0,
            sizeof( s_application_harness.analog_input_enabled ) );
    memset( s_application_harness.pwm_input_enabled, 0,
            sizeof( s_application_harness.pwm_input_enabled ) );

    s_application_harness.diagnostics.next_expected_tick         = 0U;
    s_application_harness.diagnostics.active_expected_tick_count = 0U;
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
        .max_variable_transfers_per_tick = APPLICATION_TEST_HARNESS_MAX_VARIABLE_TRANSFERS_PER_TICK,
        .max_expected_tick_count         = APPLICATION_TEST_HARNESS_MAX_EXPECTED_TICK_COUNT,
    };

    return APPLICATION_TEST_HARNESS_Init_Internal( &config );
}

void APPLICATION_TEST_HARNESS_Reset_Transaction( void )
{
    APPLICATION_TEST_HARNESS_Clear_Active_Transaction();
}

static HIL_Application_Status_T APPLICATION_TEST_HARNESS_Accept_Configuration( void )
{
    const HIL_Application_Test_Configuration_T* configuration =
        &s_application_harness.decoded_message.body.test_configuration;
    const uint32_t digest = APPLICATION_TEST_HARNESS_Configuration_Digest( configuration );

    if ( s_application_harness.diagnostics.state
         == APPLICATION_TEST_HARNESS_STATE_ACCEPTING_INSTRUCTIONS )
    {
        return APPLICATION_TEST_HARNESS_Reject( HIL_APPLICATION_STATUS_VALIDATION_FAILED );
    }
    if ( s_application_harness.diagnostics.state
             != APPLICATION_TEST_HARNESS_STATE_WAITING_FOR_CONFIGURATION
         && s_application_harness.diagnostics.state != APPLICATION_TEST_HARNESS_STATE_COMPLETE )
    {
        return APPLICATION_TEST_HARNESS_Reject( HIL_APPLICATION_STATUS_VALIDATION_FAILED );
    }

    s_application_harness.active_test_id = s_application_harness.decoded_message.test_id;
    for ( size_t i = 0U; i < HIL_APPLICATION_DIGITAL_INPUT_CHANNEL_COUNT; ++i )
    {
        s_application_harness.digital_input_enabled[i] = configuration->digital_in[i].enabled;
    }
    for ( size_t i = 0U; i < HIL_APPLICATION_ANALOG_INPUT_CHANNEL_COUNT; ++i )
    {
        s_application_harness.analog_input_enabled[i] = configuration->analog_in[i].enabled;
    }
    for ( size_t i = 0U; i < HIL_APPLICATION_PWM_INPUT_CHANNEL_COUNT; ++i )
    {
        s_application_harness.pwm_input_enabled[i] = configuration->pwm_in[i].enabled;
    }

    s_application_harness.diagnostics.next_expected_tick = 0U;
    s_application_harness.diagnostics.active_expected_tick_count =
        configuration->expected_tick_count;
    s_application_harness.diagnostics.configuration_digest = digest;
    s_application_harness.diagnostics.state = APPLICATION_TEST_HARNESS_STATE_ACCEPTING_INSTRUCTIONS;
    APPLICATION_TEST_HARNESS_Increment(
        &s_application_harness.diagnostics.configurations_accepted );
    APPLICATION_TEST_HARNESS_Set_Status( HIL_APPLICATION_STATUS_OK );
    return HIL_APPLICATION_STATUS_OK;
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
    uint32_t                 instruction_digest;
    size_t                   encoded_size = 0U;
    size_t                   output_size  = 0U;
    HIL_Application_Status_T status;

    if ( s_application_harness.diagnostics.state
         != APPLICATION_TEST_HARNESS_STATE_ACCEPTING_INSTRUCTIONS )
    {
        return APPLICATION_TEST_HARNESS_Reject( HIL_APPLICATION_STATUS_VALIDATION_FAILED );
    }
    if ( memcmp( s_application_harness.decoded_message.test_id.bytes,
                 s_application_harness.active_test_id.bytes, HIL_APPLICATION_TEST_ID_SIZE )
         != 0 )
    {
        return APPLICATION_TEST_HARNESS_Reject( HIL_APPLICATION_STATUS_INCONSISTENT_TEST_ID );
    }
    if ( instruction->tick_number != s_application_harness.diagnostics.next_expected_tick )
    {
        return APPLICATION_TEST_HARNESS_Reject( HIL_APPLICATION_STATUS_INCONSISTENT_TICK );
    }

    instruction_digest = APPLICATION_TEST_HARNESS_Instruction_Digest( instruction );
    APPLICATION_TEST_HARNESS_Build_Result( instruction, instruction_digest );

    status = HIL_APPLICATION_Encoded_Size( &s_application_harness.context,
                                           &s_application_harness.outgoing_message, &encoded_size );
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

    status = HIL_APPLICATION_Encode_Message( &s_application_harness.context,
                                             &s_application_harness.outgoing_message, response,
                                             response_capacity, &output_size );
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

    *response_size                                       = output_size;
    s_application_harness.diagnostics.instruction_digest = instruction_digest;
    APPLICATION_TEST_HARNESS_Increment( &s_application_harness.diagnostics.instructions_accepted );
    APPLICATION_TEST_HARNESS_Increment( &s_application_harness.diagnostics.results_encoded );
    s_application_harness.diagnostics.next_expected_tick++;
    if ( s_application_harness.diagnostics.next_expected_tick
         == s_application_harness.diagnostics.active_expected_tick_count )
    {
        s_application_harness.diagnostics.state = APPLICATION_TEST_HARNESS_STATE_COMPLETE;
    }
    APPLICATION_TEST_HARNESS_Set_Status( HIL_APPLICATION_STATUS_OK );
    return HIL_APPLICATION_STATUS_OK;
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

    s_application_harness.diagnostics.last_decoded_message_type =
        ( uint32_t )s_application_harness.decoded_message.type;

    switch ( s_application_harness.decoded_message.type )
    {
        case HIL_APPLICATION_MESSAGE_TYPE_TEST_CONFIGURATION:
            return APPLICATION_TEST_HARNESS_Accept_Configuration();

        case HIL_APPLICATION_MESSAGE_TYPE_TEST_INSTRUCTION:
            return APPLICATION_TEST_HARNESS_Accept_Instruction( response, response_capacity,
                                                                response_size );

        default:
            return APPLICATION_TEST_HARNESS_Reject( HIL_APPLICATION_STATUS_UNSUPPORTED_MESSAGE );
    }
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
