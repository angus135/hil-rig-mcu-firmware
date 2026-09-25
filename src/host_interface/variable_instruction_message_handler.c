/******************************************************************************
 *  File:       variable_instruction_message_handler.c
 *  Author:     Callum Rafferty
 *  Created:    24-Sep-2026
 *
 *  Description:
 *      Implementation of incoming application update instruction message handling,
 *      canonical Execution Manager instruction conversion, hardware validation,
 *      and Flash Manager upload submission.
 *
 *  Notes:
 *      Processes sparse logical operations (Digital, Analogue, PWM, UART, SPI, CAN)
 *      from UPDATE_INSTRUCTION messages without multi-chunk fragmentation. Converts
 *      protocol ticks to Execution Manager instruction timestamps.
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include "variable_instruction_message_handler.h"
#include "instruction_message_handler.h"
#include "exec_analogue_output.h"
#include "exec_digital_output.h"
#include "execution_manager/execution_instruction.h"
#include "execution_manager/execution_operation_payloads.h"
#include "flash_manager/flash_manager.h"
#include "hw_pwm_gen.h"
#include "test_configuration.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */

/**
 * @brief Conservative canonical-size bound for one maximum-sized wire instruction.
 *
 * SPI has the largest conversion ratio because each one-byte packet length becomes
 * a four-byte canonical length. Other operation families expand by no more than
 * this four-times bound.
 */
#define HOST_VAR_MAX_CANONICAL_INSTRUCTION_BOUND_BYTES                                             \
    ( sizeof( ExecutionInstructionHeader_T ) + ( 4U * HIL_APPLICATION_ABSOLUTE_MAX_MESSAGE_SIZE ) )

#if defined( __cplusplus )
static_assert( EXECUTION_INSTRUCTION_MAX_SIZE_BYTES
                   >= HOST_VAR_MAX_CANONICAL_INSTRUCTION_BOUND_BYTES,
               "Canonical instruction buffer is smaller than the maximum wire conversion" );
#else
_Static_assert( EXECUTION_INSTRUCTION_MAX_SIZE_BYTES
                    >= HOST_VAR_MAX_CANONICAL_INSTRUCTION_BOUND_BYTES,
                "Canonical instruction buffer is smaller than the maximum wire conversion" );
#endif

/**-----------------------------------------------------------------------------
 *  Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

/**
 * @brief Cursor context for sequential, 4-byte aligned operation appending.
 */
typedef struct
{
    /** Pointer to destination buffer. */
    uint8_t* buffer;

    /** Total buffer capacity in bytes. */
    size_t capacity;

    /** Current write offset in bytes, aligned to a 4-byte boundary. */
    size_t offset;

    /** Number of complete operations appended to this instruction. */
    uint8_t operation_count;
} HostInstructionWriter_T;

/**
 * @brief Tracked digital output pin states for detecting transitions.
 */
typedef struct
{
    /** Last observed digital output states for channels 0..9. */
    uint8_t digital_outputs[HIL_APPLICATION_DIGITAL_OUTPUT_CHANNEL_COUNT];

    /** Set to true after Reset() establishes baseline conditions. */
    bool initialized;
} HostVarInstructionStateTracker_T;

/**-----------------------------------------------------------------------------
 *  Private (static) Variables
 *------------------------------------------------------------------------------
 */

/** @brief Monotonically increasing execution tick of the last processed instruction. */
static uint32_t last_instruction_tick = 0U;

/** @brief Indicates whether at least one instruction has been processed since reset. */
static bool has_received_instruction = false;

/** @brief Retained digital output pin state for transition detection. */
static HostVarInstructionStateTracker_T tracked_digital_state;

/**-----------------------------------------------------------------------------
 *  Private (static) Function Prototypes
 *------------------------------------------------------------------------------
 */

static HOST_Interface_Status_T
HOST_VAR_INSTRUCTION_AppendOperation( HostInstructionWriter_T*   writer,
                                      ExecutionOperationOpcode_T opcode, uint8_t channel,
                                      const void* payload, uint16_t payload_size_bytes );

static HOST_Interface_Status_T
HOST_VAR_INSTRUCTION_EncodeDigital( const HIL_Application_Logical_Operation_T* op,
                                    const DutDriverConfiguration_T* config, bool config_valid,
                                    HostInstructionWriter_T* writer );

static HOST_Interface_Status_T HOST_VAR_INSTRUCTION_EncodeAnalogue(
    const HIL_Application_Logical_Operation_T* op, const DutDriverConfiguration_T* config,
    bool config_valid, AnalogueOutputPreparedBatch_T* batch, uint8_t* batch_frame_count );

static HOST_Interface_Status_T
HOST_VAR_INSTRUCTION_EncodePwm( const HIL_Application_Logical_Operation_T* op,
                                const DutDriverConfiguration_T* config, bool config_valid,
                                HostInstructionWriter_T* writer );

static HOST_Interface_Status_T
HOST_VAR_INSTRUCTION_EncodeUart( const HIL_Application_Logical_Operation_T* op,
                                 const DutDriverConfiguration_T* config, bool config_valid,
                                 HostInstructionWriter_T* writer );

static HOST_Interface_Status_T
HOST_VAR_INSTRUCTION_EncodeSpi( const HIL_Application_Logical_Operation_T* op,
                                const DutDriverConfiguration_T* config, bool config_valid,
                                HostInstructionWriter_T* writer );

static HOST_Interface_Status_T
HOST_VAR_INSTRUCTION_EncodeCan( const HIL_Application_Logical_Operation_T* op,
                                const DutDriverConfiguration_T* config, bool config_valid,
                                HostInstructionWriter_T* writer );

static HOST_Interface_Status_T
HOST_VAR_INSTRUCTION_ConvertInstruction( const HIL_Application_Update_Instruction_T* instruction,
                                         uint8_t* destination, size_t destination_capacity,
                                         size_t* bytes_written );

static HOST_Interface_Status_T HOST_VAR_INSTRUCTION_UploadToFlash( const uint8_t* data,
                                                                   size_t         length );

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

/**
 * @brief Appends one operation to the instruction buffer, maintaining 4-byte alignment.
 */
static HOST_Interface_Status_T HOST_VAR_INSTRUCTION_AppendOperation(
    HostInstructionWriter_T* const writer, const ExecutionOperationOpcode_T opcode,
    const uint8_t channel, const void* const payload, const uint16_t payload_size_bytes )
{
    const uint32_t encoded_size_bytes =
        EXECUTION_OPERATION_ENCODED_SIZE_BYTES( payload_size_bytes );

    if ( ( writer->offset + encoded_size_bytes ) > writer->capacity )
    {
        return HOST_INTERFACE_STATUS_BUFFER_TOO_SMALL;
    }

    uint8_t* const operation_start = writer->buffer + writer->offset;

    // Clear operation memory to guarantee zero-padding on trailing boundary bytes
    ( void )memset( operation_start, 0, encoded_size_bytes );

    // Pack 32-bit header word: [payload_length_bytes:16][channel:8][opcode:8]
    const ExecutionOperationHeaderWord_T header_word =
        ( ( ( uint32_t )opcode << EXECUTION_OPERATION_OPCODE_SHIFT )
          & EXECUTION_OPERATION_OPCODE_MASK )
        | ( ( ( uint32_t )channel << EXECUTION_OPERATION_CHANNEL_SHIFT )
            & EXECUTION_OPERATION_CHANNEL_MASK )
        | ( ( ( uint32_t )payload_size_bytes << EXECUTION_OPERATION_PAYLOAD_LENGTH_SHIFT )
            & EXECUTION_OPERATION_PAYLOAD_LENGTH_MASK );

    ( void )memcpy( operation_start, &header_word, sizeof( header_word ) );

    if ( ( payload != NULL ) && ( payload_size_bytes > 0U ) )
    {
        ( void )memcpy( operation_start + sizeof( header_word ), payload, payload_size_bytes );
    }

    writer->offset += encoded_size_bytes;
    writer->operation_count++;

    return HOST_INTERFACE_STATUS_OK;
}

/**
 * @brief Decodes a 2-byte digital bank mask and encodes transitions into an execution operation.
 */
static HOST_Interface_Status_T
HOST_VAR_INSTRUCTION_EncodeDigital( const HIL_Application_Logical_Operation_T* const op,
                                    const DutDriverConfiguration_T* const            config,
                                    const bool config_valid, HostInstructionWriter_T* const writer )
{
    if ( ( op->channel != 0U ) || ( op->payload.size != 2U ) || ( op->payload.data == NULL ) )
    {
        return HOST_INTERFACE_STATUS_VALIDATION_FAILED;
    }

    const uint16_t mask =
        ( uint16_t )op->payload.data[0] | ( ( uint16_t )op->payload.data[1] << 8U );

    // Reserved bits 10..15 must be zero
    if ( ( mask & 0xFC00U ) != 0U )
    {
        return HOST_INTERFACE_STATUS_VALIDATION_FAILED;
    }

    GPIOOutput_T high_pins[HIL_APPLICATION_DIGITAL_OUTPUT_CHANNEL_COUNT];
    GPIOOutput_T low_pins[HIL_APPLICATION_DIGITAL_OUTPUT_CHANNEL_COUNT];
    uint8_t      high_count = 0U;
    uint8_t      low_count  = 0U;

    for ( uint8_t i = 0U; i < HIL_APPLICATION_DIGITAL_OUTPUT_CHANNEL_COUNT; i++ )
    {
        const uint8_t new_state  = ( ( mask & ( 1U << i ) ) != 0U ) ? 1U : 0U;
        const uint8_t prev_state = tracked_digital_state.digital_outputs[i];

        if ( config_valid && ( new_state != 0U ) )
        {
            if ( ( i >= ( uint8_t )EXEC_DIGITAL_OUTPUT_CHANNEL_COUNT )
                 || !config->digital_outputs.channels[i].is_enabled )
            {
                return HOST_INTERFACE_STATUS_VALIDATION_FAILED;
            }
        }

        if ( !tracked_digital_state.initialized || ( new_state != prev_state ) )
        {
            const GPIOOutput_T pin = ( GPIOOutput_T )( ( uint32_t )DIGITAL_OUTPUT_0 + i );
            if ( new_state != 0U )
            {
                high_pins[high_count++] = pin;
            }
            else
            {
                low_pins[low_count++] = pin;
            }
            tracked_digital_state.digital_outputs[i] = new_state;
        }
    }

    if ( ( high_count == 0U ) && ( low_count == 0U ) )
    {
        return HOST_INTERFACE_STATUS_OK;
    }

    const uint32_t high_mask =
        ( high_count > 0U ) ? EXEC_DIGITAL_OUTPUT_Combine_Port_Pin_Masks( high_pins, high_count )
                            : 0U;
    const uint32_t low_mask =
        ( low_count > 0U ) ? EXEC_DIGITAL_OUTPUT_Combine_Port_Pin_Masks( low_pins, low_count ) : 0U;

    ExecutionDigitalOutputPayload_T payload;
    payload.high_bitmask = high_mask;
    payload.low_bitmask  = low_mask;

    return HOST_VAR_INSTRUCTION_AppendOperation(
        writer, EXECUTION_OPERATION_OPCODE_DIGITAL_OUTPUT_UPDATE,
        EXECUTION_OPERATION_CHANNEL_UNUSED, &payload, sizeof( payload ) );
}

/**
 * @brief Decodes 4-byte microvolts analogue output and adds DAC SPI frame to batch.
 */
static HOST_Interface_Status_T HOST_VAR_INSTRUCTION_EncodeAnalogue(
    const HIL_Application_Logical_Operation_T* const op,
    const DutDriverConfiguration_T* const config, const bool config_valid,
    AnalogueOutputPreparedBatch_T* const batch, uint8_t* const batch_frame_count )
{
    if ( ( op->channel >= HIL_APPLICATION_ANALOG_OUTPUT_CHANNEL_COUNT )
         || ( op->payload.size != 4U ) || ( op->payload.data == NULL ) )
    {
        return HOST_INTERFACE_STATUS_VALIDATION_FAILED;
    }

    if ( config_valid && !config->analogue_output.is_enabled )
    {
        return HOST_INTERFACE_STATUS_VALIDATION_FAILED;
    }

    if ( *batch_frame_count >= EXECUTION_ANALOGUE_OUTPUT_MAX_FRAMES )
    {
        return HOST_INTERFACE_STATUS_VALIDATION_FAILED;
    }

    const uint32_t microvolts =
        ( uint32_t )op->payload.data[0] | ( ( uint32_t )op->payload.data[1] << 8U )
        | ( ( uint32_t )op->payload.data[2] << 16U ) | ( ( uint32_t )op->payload.data[3] << 24U );

    const float voltage_v = ( float )microvolts / HOST_VAR_INSTRUCTION_MICROVOLTS_PER_VOLT;

    AnalogueOutputPreparedFrame_T frame;
    ( void )memset( &frame, 0, sizeof( frame ) );

    if ( !EXEC_ANALOGUE_OUTPUT_Prepare_Frame( op->channel, voltage_v, &frame ) )
    {
        return HOST_INTERFACE_STATUS_VALIDATION_FAILED;
    }

    ( void )memcpy( &batch->bytes[( *batch_frame_count ) * EXEC_ANALOGUE_OUTPUT_FRAME_SIZE_BYTES],
                    frame.bytes, sizeof( frame.bytes ) );
    ( *batch_frame_count )++;

    return HOST_INTERFACE_STATUS_OK;
}

/**
 * @brief Decodes 6-byte PWM output (period + duty) and encodes timer registers.
 */
static HOST_Interface_Status_T
HOST_VAR_INSTRUCTION_EncodePwm( const HIL_Application_Logical_Operation_T* const op,
                                const DutDriverConfiguration_T* const            config,
                                const bool config_valid, HostInstructionWriter_T* const writer )
{
    if ( ( op->channel >= HIL_APPLICATION_PWM_OUTPUT_CHANNEL_COUNT ) || ( op->payload.size != 6U )
         || ( op->payload.data == NULL ) )
    {
        return HOST_INTERFACE_STATUS_VALIDATION_FAILED;
    }

    if ( config_valid )
    {
        if ( ( op->channel >= ( uint8_t )EXEC_PWM_GEN_CHANNEL_COUNT )
             || !config->pwm_generation_channels[op->channel].is_enabled )
        {
            return HOST_INTERFACE_STATUS_VALIDATION_FAILED;
        }
    }

    const uint32_t period_ns =
        ( uint32_t )op->payload.data[0] | ( ( uint32_t )op->payload.data[1] << 8U )
        | ( ( uint32_t )op->payload.data[2] << 16U ) | ( ( uint32_t )op->payload.data[3] << 24U );

    const uint16_t duty_permyriad =
        ( uint16_t )op->payload.data[4] | ( ( uint16_t )op->payload.data[5] << 8U );

    if ( duty_permyriad > 10000U )
    {
        return HOST_INTERFACE_STATUS_VALIDATION_FAILED;
    }

    if ( period_ns == 0U )
    {
        if ( duty_permyriad != 0U )
        {
            return HOST_INTERFACE_STATUS_VALIDATION_FAILED;
        }
        // Disabled output; no operation emitted
        return HOST_INTERFACE_STATUS_OK;
    }

    const uint32_t timer_clock_hz = ( op->channel == EXECUTION_OPERATION_PWM_CHANNEL_LV )
                                        ? HOST_VAR_INSTRUCTION_PWM_LV_TIMER_CLOCK_HZ
                                        : HOST_VAR_INSTRUCTION_PWM_HV_TIMER_CLOCK_HZ;

    const uint32_t frequency_hz  = HOST_VAR_INSTRUCTION_NANOSECONDS_PER_SECOND / period_ns;
    const uint16_t duty_permille = ( uint16_t )( duty_permyriad / 10U );

    ExecutionPwmUpdatePayload_T payload;
    ( void )memset( &payload, 0, sizeof( payload ) );

    if ( !HW_PWM_GEN_compute_psc( frequency_hz, timer_clock_hz, &payload.psc )
         || !HW_PWM_GEN_compute_arr( frequency_hz, timer_clock_hz, payload.psc, &payload.arr )
         || !HW_PWM_GEN_compute_ccr( duty_permille, payload.arr, &payload.ccr ) )
    {
        return HOST_INTERFACE_STATUS_VALIDATION_FAILED;
    }

    return HOST_VAR_INSTRUCTION_AppendOperation( writer, EXECUTION_OPERATION_OPCODE_PWM_UPDATE,
                                                 op->channel, &payload, sizeof( payload ) );
}

/**
 * @brief Appends raw UART TX data bytes.
 */
static HOST_Interface_Status_T
HOST_VAR_INSTRUCTION_EncodeUart( const HIL_Application_Logical_Operation_T* const op,
                                 const DutDriverConfiguration_T* const            config,
                                 const bool config_valid, HostInstructionWriter_T* const writer )
{
    if ( ( op->channel >= HIL_APPLICATION_UART_CHANNEL_COUNT ) || ( op->payload.size == 0U )
         || ( op->payload.data == NULL ) )
    {
        return HOST_INTERFACE_STATUS_VALIDATION_FAILED;
    }

    if ( config_valid )
    {
        if ( ( op->channel >= ( uint8_t )EXEC_UART_CHANNEL_COUNT )
             || !config->uart_channels[op->channel].is_enabled )
        {
            return HOST_INTERFACE_STATUS_VALIDATION_FAILED;
        }
    }

    return HOST_VAR_INSTRUCTION_AppendOperation( writer, EXECUTION_OPERATION_OPCODE_UART_TRANSMIT,
                                                 op->channel, op->payload.data,
                                                 ( uint16_t )op->payload.size );
}

/**
 * @brief Converts protocol packetised SPI layout into Execution Manager SPI format.
 */
static HOST_Interface_Status_T
HOST_VAR_INSTRUCTION_EncodeSpi( const HIL_Application_Logical_Operation_T* const op,
                                const DutDriverConfiguration_T* const            config,
                                const bool config_valid, HostInstructionWriter_T* const writer )
{
    if ( ( op->channel >= HIL_APPLICATION_SPI_CHANNEL_COUNT ) || ( op->payload.size < 2U )
         || ( op->payload.data == NULL ) )
    {
        return HOST_INTERFACE_STATUS_VALIDATION_FAILED;
    }

    if ( config_valid )
    {
        if ( ( op->channel >= ( uint8_t )TEST_CONFIGURATION_SPI_CHANNEL_COUNT )
             || !config->spi_channels[op->channel].is_enabled )
        {
            return HOST_INTERFACE_STATUS_VALIDATION_FAILED;
        }
    }

    const uint8_t packet_count = op->payload.data[0];
    if ( ( packet_count == 0U ) || ( op->payload.size <= ( 1U + ( size_t )packet_count ) ) )
    {
        return HOST_INTERFACE_STATUS_VALIDATION_FAILED;
    }

    const uint8_t* const packet_lengths = &op->payload.data[1];
    const uint8_t* const packet_data    = &op->payload.data[1U + packet_count];
    const size_t         data_len       = ( size_t )op->payload.size - 1U - ( size_t )packet_count;

    size_t length_sum = 0U;
    for ( uint8_t i = 0U; i < packet_count; i++ )
    {
        if ( packet_lengths[i] == 0U )
        {
            return HOST_INTERFACE_STATUS_VALIDATION_FAILED;
        }
        length_sum += packet_lengths[i];
    }

    if ( length_sum != data_len )
    {
        return HOST_INTERFACE_STATUS_VALIDATION_FAILED;
    }

    const size_t converted_payload_len = EXECUTION_SPI_PREFIX_SIZE_BYTES
                                         + EXECUTION_SPI_PACKET_SIZES_LENGTH_BYTES( packet_count )
                                         + data_len;

    const size_t                  operation_offset = writer->offset;
    const HOST_Interface_Status_T append_status    = HOST_VAR_INSTRUCTION_AppendOperation(
        writer, EXECUTION_OPERATION_OPCODE_SPI_TRANSMIT, op->channel, NULL,
        ( uint16_t )converted_payload_len );
    if ( append_status != HOST_INTERFACE_STATUS_OK )
    {
        return append_status;
    }

    uint8_t* const converted_payload =
        writer->buffer + operation_offset + sizeof( ExecutionOperationHeaderWord_T );

    // 1. Prefix: packet_count (4 bytes LE)
    const uint32_t packet_count_u32 = ( uint32_t )packet_count;
    ( void )memcpy( &converted_payload[EXECUTION_SPI_PACKET_COUNT_OFFSET_BYTES], &packet_count_u32,
                    sizeof( packet_count_u32 ) );

    // 2. Packet sizes (4 bytes LE each)
    for ( uint8_t i = 0U; i < packet_count; i++ )
    {
        const uint32_t pkt_len_u32 = ( uint32_t )packet_lengths[i];
        ( void )memcpy( &converted_payload[EXECUTION_SPI_PACKET_SIZES_OFFSET_BYTES
                                           + ( i * EXECUTION_SPI_PACKET_SIZE_FIELD_BYTES )],
                        &pkt_len_u32, sizeof( pkt_len_u32 ) );
    }

    // 3. Packet data bytes
    ( void )memcpy( &converted_payload[EXECUTION_SPI_DATA_OFFSET_BYTES( packet_count )],
                    packet_data, data_len );

    return HOST_INTERFACE_STATUS_OK;
}

/**
 * @brief Appends 12-byte standard CAN packets.
 */
static HOST_Interface_Status_T
HOST_VAR_INSTRUCTION_EncodeCan( const HIL_Application_Logical_Operation_T* const op,
                                const DutDriverConfiguration_T* const            config,
                                const bool config_valid, HostInstructionWriter_T* const writer )
{
    if ( ( op->channel >= HIL_APPLICATION_CAN_CHANNEL_COUNT ) || ( op->payload.size == 0U )
         || ( ( op->payload.size % EXECUTION_CAN_PACKET_SIZE_BYTES ) != 0U )
         || ( op->payload.data == NULL ) )
    {
        return HOST_INTERFACE_STATUS_VALIDATION_FAILED;
    }

    if ( config_valid )
    {
        if ( ( op->channel >= ( uint8_t )EXEC_CAN_CHANNEL_COUNT )
             || !config->can_channels[op->channel].is_enabled )
        {
            return HOST_INTERFACE_STATUS_VALIDATION_FAILED;
        }
    }

    return HOST_VAR_INSTRUCTION_AppendOperation( writer, EXECUTION_OPERATION_OPCODE_CAN_TRANSMIT,
                                                 op->channel, op->payload.data,
                                                 ( uint16_t )op->payload.size );
}

/**
 * @brief Converts an application update instruction into canonical Execution Manager format.
 */
static HOST_Interface_Status_T HOST_VAR_INSTRUCTION_ConvertInstruction(
    const HIL_Application_Update_Instruction_T* const instruction, uint8_t* const destination,
    const size_t destination_capacity, size_t* const bytes_written )
{
    if ( destination_capacity < sizeof( ExecutionInstructionHeader_T ) )
    {
        return HOST_INTERFACE_STATUS_BUFFER_TOO_SMALL;
    }

    HostInstructionWriter_T writer;
    writer.buffer          = destination;
    writer.capacity        = destination_capacity;
    writer.offset          = sizeof( ExecutionInstructionHeader_T );
    writer.operation_count = 0U;

    DutDriverConfiguration_T active_config;
    ( void )memset( &active_config, 0, sizeof( active_config ) );
    const bool config_valid = TEST_CONFIGURATION_GetActive( &active_config );

    AnalogueOutputPreparedBatch_T dac_batch;
    ( void )memset( &dac_batch, 0, sizeof( dac_batch ) );
    uint8_t dac_batch_frame_count = 0U;

    for ( uint8_t i = 0U; i < instruction->operation_count; i++ )
    {
        const HIL_Application_Logical_Operation_T* const op     = &instruction->operations[i];
        HOST_Interface_Status_T                          status = HOST_INTERFACE_STATUS_OK;

        switch ( op->peripheral_type )
        {
            case HIL_APPLICATION_PERIPHERAL_DIGITAL_OUTPUT:
                status =
                    HOST_VAR_INSTRUCTION_EncodeDigital( op, &active_config, config_valid, &writer );
                break;

            case HIL_APPLICATION_PERIPHERAL_ANALOG_OUTPUT:
                status = HOST_VAR_INSTRUCTION_EncodeAnalogue( op, &active_config, config_valid,
                                                              &dac_batch, &dac_batch_frame_count );
                break;

            case HIL_APPLICATION_PERIPHERAL_PWM_OUTPUT:
                status =
                    HOST_VAR_INSTRUCTION_EncodePwm( op, &active_config, config_valid, &writer );
                break;

            case HIL_APPLICATION_PERIPHERAL_UART:
                status =
                    HOST_VAR_INSTRUCTION_EncodeUart( op, &active_config, config_valid, &writer );
                break;

            case HIL_APPLICATION_PERIPHERAL_SPI:
                status =
                    HOST_VAR_INSTRUCTION_EncodeSpi( op, &active_config, config_valid, &writer );
                break;

            case HIL_APPLICATION_PERIPHERAL_CAN:
                status =
                    HOST_VAR_INSTRUCTION_EncodeCan( op, &active_config, config_valid, &writer );
                break;

            case HIL_APPLICATION_PERIPHERAL_INVALID:
            case HIL_APPLICATION_PERIPHERAL_DIGITAL_INPUT:
            case HIL_APPLICATION_PERIPHERAL_ANALOG_INPUT:
            case HIL_APPLICATION_PERIPHERAL_PWM_INPUT:
            case HIL_APPLICATION_PERIPHERAL_I2C:
            case HIL_APPLICATION_PERIPHERAL_RESERVED:
            default:
                return HOST_INTERFACE_STATUS_VALIDATION_FAILED;
        }

        if ( status != HOST_INTERFACE_STATUS_OK )
        {
            return status;
        }
    }

    // Append analogue batch if any frames were prepared
    if ( dac_batch_frame_count > 0U )
    {
        const uint16_t batch_size =
            ( uint16_t )( dac_batch_frame_count * EXEC_ANALOGUE_OUTPUT_FRAME_SIZE_BYTES );

        const HOST_Interface_Status_T status = HOST_VAR_INSTRUCTION_AppendOperation(
            &writer, EXECUTION_OPERATION_OPCODE_ANALOGUE_OUTPUT_BATCH,
            EXECUTION_OPERATION_CHANNEL_UNUSED, dac_batch.bytes, batch_size );

        if ( status != HOST_INTERFACE_STATUS_OK )
        {
            return status;
        }
    }

    // Output-free tick
    if ( writer.operation_count == 0U )
    {
        *bytes_written = 0U;
        return HOST_INTERFACE_STATUS_OK;
    }

    // Build ExecutionInstructionHeader_T using the same tick mapping as legacy instructions.
    const uint16_t operations_length_bytes =
        ( uint16_t )( writer.offset - sizeof( ExecutionInstructionHeader_T ) );

    ExecutionInstructionHeader_T header;
    header.timestamp               = instruction->tick_number;
    header.operations_length_bytes = operations_length_bytes;
    header.operation_count         = writer.operation_count;
    header.reserved                = 0U;

    ( void )memcpy( destination, &header, sizeof( header ) );

    *bytes_written = writer.offset;
    return HOST_INTERFACE_STATUS_OK;
}

/**
 * @brief Submits a canonical instruction byte stream chunk to the Flash Manager.
 */
static HOST_Interface_Status_T HOST_VAR_INSTRUCTION_UploadToFlash( const uint8_t* const data,
                                                                   const size_t         length )
{
    const FlashManagerInstructionUploadRequestStatus_T upload_status =
        FLASH_MANAGER_SubmitInstructionUploadBytes( data, ( uint32_t )length );

    if ( upload_status == FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_ACCEPTED )
    {
        return HOST_INTERFACE_STATUS_OK;
    }

    if ( upload_status == FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_BUSY )
    {
        return HOST_INTERFACE_STATUS_INTERNAL_ERROR;
    }

    if ( upload_status == FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_INVALID_STATE )
    {
        return HOST_INTERFACE_STATUS_STATE_TRANSITION_FAILURE;
    }

    return HOST_INTERFACE_STATUS_INTERNAL_ERROR;
}

/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */

void HOST_VARIABLE_INSTRUCTION_HANDLER_Reset( void )
{
    last_instruction_tick    = 0U;
    has_received_instruction = false;
    ( void )memset( &tracked_digital_state, 0, sizeof( tracked_digital_state ) );

    DutDriverConfiguration_T active_config;
    ( void )memset( &active_config, 0, sizeof( active_config ) );
    if ( TEST_CONFIGURATION_GetActive( &active_config ) )
    {
        for ( uint8_t i = 0U; i < HIL_APPLICATION_DIGITAL_OUTPUT_CHANNEL_COUNT; i++ )
        {
            if ( active_config.digital_outputs.channels[i].is_enabled )
            {
                tracked_digital_state.digital_outputs[i] =
                    active_config.digital_outputs.channels[i].initial_high ? 1U : 0U;
            }
        }
    }

    tracked_digital_state.initialized = true;
}

HOST_Interface_Status_T HOST_VARIABLE_INSTRUCTION_HANDLER_HandleInstruction(
    const HIL_Application_Update_Instruction_T* const instruction )
{
    if ( ( instruction == NULL ) || ( instruction->operations == NULL )
         || ( instruction->operation_count == 0U ) )
    {
        return HOST_INTERFACE_STATUS_INVALID_ARGUMENT;
    }

    if ( has_received_instruction && ( instruction->tick_number <= last_instruction_tick ) )
    {
        return HOST_INTERFACE_STATUS_INCONSISTENT_TICK;
    }

    uint8_t* const instruction_buffer     = HOST_INSTRUCTION_HANDLER_GetSharedBuffer();
    size_t         instruction_size_bytes = 0U;

    const HOST_Interface_Status_T status = HOST_VAR_INSTRUCTION_ConvertInstruction(
        instruction, instruction_buffer, EXECUTION_INSTRUCTION_MAX_SIZE_BYTES,
        &instruction_size_bytes );

    if ( status != HOST_INTERFACE_STATUS_OK )
    {
        return status;
    }

    // Output-free tick: no operations changed, no flash instruction to upload
    if ( instruction_size_bytes == 0U )
    {
        last_instruction_tick    = instruction->tick_number;
        has_received_instruction = true;
        return HOST_INTERFACE_STATUS_OK;
    }

    // Layer 2: Canonical Execution Manager validation check
    const ExecutionInstructionValidationResult_T canonical_val_status =
        EXECUTION_INSTRUCTION_Validate( instruction_buffer, instruction_size_bytes );
    if ( canonical_val_status != EXECUTION_INSTRUCTION_VALIDATION_OK )
    {
        return HOST_INTERFACE_STATUS_VALIDATION_FAILED;
    }

    const HOST_Interface_Status_T upload_status =
        HOST_VAR_INSTRUCTION_UploadToFlash( instruction_buffer, instruction_size_bytes );

    if ( upload_status == HOST_INTERFACE_STATUS_OK )
    {
        last_instruction_tick    = instruction->tick_number;
        has_received_instruction = true;
    }

    return upload_status;
}
