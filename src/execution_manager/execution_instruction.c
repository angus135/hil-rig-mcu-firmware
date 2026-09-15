/******************************************************************************
 *  File:       execution_instruction.c
 *  Author:     Callum Rafferty
 *  Created:    15-Sep-2026
 *
 *  Description:
 *      Implementation of canonical Execution Manager instruction validation.
 *
 *  Notes:
 *      Validates byte stream framing, 4-byte boundaries, header fields,
 *      opcodes, channel numbers, payload lengths, and driver payload parameters
 *      before instructions are committed to storage or dispatched to hardware.
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include "execution_instruction.h"
#include "execution_operation_payloads.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */

/** Maximum allowed DAC channel index (channels 0..5). */
#define EXECUTION_VALIDATION_MAX_DAC_CHANNEL ( 5U )

/** Maximum 11-bit standard CAN identifier (0x7FF). */
#define EXECUTION_VALIDATION_MAX_CAN_STD_ID ( 0x7FFU )

/** Maximum single-call UART transmit byte count. */
#define EXECUTION_VALIDATION_MAX_UART_PAYLOAD_BYTES ( 255U )

/**-----------------------------------------------------------------------------
 *  Private (static) Function Prototypes
 *------------------------------------------------------------------------------
 */

static ExecutionInstructionValidationResult_T
EXECUTION_INSTRUCTION_ValidateDigitalOutput( uint8_t channel, const uint8_t* payload,
                                            uint16_t payload_length );

static ExecutionInstructionValidationResult_T
EXECUTION_INSTRUCTION_ValidateAnalogueOutput( uint8_t channel, const uint8_t* payload,
                                             uint16_t payload_length );

static ExecutionInstructionValidationResult_T
EXECUTION_INSTRUCTION_ValidatePwmUpdate( uint8_t channel, const uint8_t* payload,
                                        uint16_t payload_length );

static ExecutionInstructionValidationResult_T
EXECUTION_INSTRUCTION_ValidateCanTransmit( uint8_t channel, const uint8_t* payload,
                                          uint16_t payload_length );

static ExecutionInstructionValidationResult_T
EXECUTION_INSTRUCTION_ValidateSpiTransmit( uint8_t channel, const uint8_t* payload,
                                          uint16_t payload_length );

static ExecutionInstructionValidationResult_T
EXECUTION_INSTRUCTION_ValidateUartTransmit( uint8_t channel, const uint8_t* payload,
                                           uint16_t payload_length );

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

static ExecutionInstructionValidationResult_T
EXECUTION_INSTRUCTION_ValidateDigitalOutput( const uint8_t channel, const uint8_t* const payload,
                                            const uint16_t payload_length )
{
    if ( channel != EXECUTION_OPERATION_CHANNEL_UNUSED )
    {
        return EXECUTION_INSTRUCTION_VALIDATION_INVALID_CHANNEL;
    }

    if ( payload_length != sizeof( ExecutionDigitalOutputPayload_T ) )
    {
        return EXECUTION_INSTRUCTION_VALIDATION_INVALID_PAYLOAD_LENGTH;
    }

    ExecutionDigitalOutputPayload_T digital_payload;
    ( void )memcpy( &digital_payload, payload, sizeof( digital_payload ) );

    /* High and low masks must be disjoint */
    if ( ( digital_payload.high_bitmask & digital_payload.low_bitmask ) != 0U )
    {
        return EXECUTION_INSTRUCTION_VALIDATION_INVALID_PAYLOAD_DATA;
    }

    /* At least one pin must transition */
    if ( ( digital_payload.high_bitmask | digital_payload.low_bitmask ) == 0U )
    {
        return EXECUTION_INSTRUCTION_VALIDATION_INVALID_PAYLOAD_DATA;
    }

    return EXECUTION_INSTRUCTION_VALIDATION_OK;
}

static ExecutionInstructionValidationResult_T
EXECUTION_INSTRUCTION_ValidateAnalogueOutput( const uint8_t channel, const uint8_t* const payload,
                                             const uint16_t payload_length )
{
    if ( channel != EXECUTION_OPERATION_CHANNEL_UNUSED )
    {
        return EXECUTION_INSTRUCTION_VALIDATION_INVALID_CHANNEL;
    }

    if ( ( payload_length == 0U )
         || ( ( payload_length % EXECUTION_ANALOGUE_OUTPUT_FRAME_SIZE_BYTES ) != 0U )
         || ( payload_length > EXECUTION_ANALOGUE_OUTPUT_MAX_DATA_BYTES ) )
    {
        return EXECUTION_INSTRUCTION_VALIDATION_INVALID_PAYLOAD_LENGTH;
    }

    const uint8_t frame_count =
        ( uint8_t )( payload_length / EXECUTION_ANALOGUE_OUTPUT_FRAME_SIZE_BYTES );

    for ( uint8_t i = 0U; i < frame_count; i++ )
    {
        const uint8_t frame_first_byte =
            payload[i * EXECUTION_ANALOGUE_OUTPUT_FRAME_SIZE_BYTES];
        const uint8_t dac_channel = ( frame_first_byte >> 3U ) & 0x1FU;

        if ( dac_channel > EXECUTION_VALIDATION_MAX_DAC_CHANNEL )
        {
            return EXECUTION_INSTRUCTION_VALIDATION_INVALID_PAYLOAD_DATA;
        }
    }

    return EXECUTION_INSTRUCTION_VALIDATION_OK;
}

static ExecutionInstructionValidationResult_T
EXECUTION_INSTRUCTION_ValidatePwmUpdate( const uint8_t channel, const uint8_t* const payload,
                                        const uint16_t payload_length )
{
    if ( ( channel != EXECUTION_OPERATION_PWM_CHANNEL_LV )
         && ( channel != EXECUTION_OPERATION_PWM_CHANNEL_HV ) )
    {
        return EXECUTION_INSTRUCTION_VALIDATION_INVALID_CHANNEL;
    }

    if ( payload_length != sizeof( ExecutionPwmUpdatePayload_T ) )
    {
        return EXECUTION_INSTRUCTION_VALIDATION_INVALID_PAYLOAD_LENGTH;
    }

    ExecutionPwmUpdatePayload_T pwm_payload;
    ( void )memcpy( &pwm_payload, payload, sizeof( pwm_payload ) );

    /* ARR must be non-zero for active PWM output */
    if ( pwm_payload.arr == 0U )
    {
        return EXECUTION_INSTRUCTION_VALIDATION_INVALID_PAYLOAD_DATA;
    }

    /* Compare value CCR cannot exceed full period (ARR + 1) */
    if ( pwm_payload.ccr > ( ( uint32_t )pwm_payload.arr + 1U ) )
    {
        return EXECUTION_INSTRUCTION_VALIDATION_INVALID_PAYLOAD_DATA;
    }

    return EXECUTION_INSTRUCTION_VALIDATION_OK;
}

static ExecutionInstructionValidationResult_T
EXECUTION_INSTRUCTION_ValidateCanTransmit( const uint8_t channel, const uint8_t* const payload,
                                          const uint16_t payload_length )
{
    if ( ( channel != EXECUTION_OPERATION_CAN_CHANNEL_1 )
         && ( channel != EXECUTION_OPERATION_CAN_CHANNEL_2 ) )
    {
        return EXECUTION_INSTRUCTION_VALIDATION_INVALID_CHANNEL;
    }

    if ( ( payload_length == 0U )
         || ( ( payload_length % EXECUTION_CAN_PACKET_SIZE_BYTES ) != 0U ) )
    {
        return EXECUTION_INSTRUCTION_VALIDATION_INVALID_PAYLOAD_LENGTH;
    }

    const uint32_t packet_count = payload_length / EXECUTION_CAN_PACKET_SIZE_BYTES;

    for ( uint32_t i = 0U; i < packet_count; i++ )
    {
        ExecutionCanPacket_T packet;
        ( void )memcpy( &packet, payload + ( i * sizeof( ExecutionCanPacket_T ) ),
                        sizeof( packet ) );

        if ( packet.id > EXECUTION_VALIDATION_MAX_CAN_STD_ID )
        {
            return EXECUTION_INSTRUCTION_VALIDATION_INVALID_PAYLOAD_DATA;
        }

        if ( packet.dlc > EXECUTION_CAN_MAX_DATA_BYTES )
        {
            return EXECUTION_INSTRUCTION_VALIDATION_INVALID_PAYLOAD_DATA;
        }

        if ( packet.reserved != 0U )
        {
            return EXECUTION_INSTRUCTION_VALIDATION_INVALID_PAYLOAD_DATA;
        }

        /* Unused payload bytes beyond DLC must be zero */
        for ( uint8_t byte_idx = packet.dlc; byte_idx < EXECUTION_CAN_MAX_DATA_BYTES; byte_idx++ )
        {
            if ( packet.data[byte_idx] != 0U )
            {
                return EXECUTION_INSTRUCTION_VALIDATION_INVALID_PAYLOAD_DATA;
            }
        }
    }

    return EXECUTION_INSTRUCTION_VALIDATION_OK;
}

static ExecutionInstructionValidationResult_T
EXECUTION_INSTRUCTION_ValidateSpiTransmit( const uint8_t channel, const uint8_t* const payload,
                                          const uint16_t payload_length )
{
    if ( ( channel != EXECUTION_OPERATION_SPI_CHANNEL_1 )
         && ( channel != EXECUTION_OPERATION_SPI_CHANNEL_2 ) )
    {
        return EXECUTION_INSTRUCTION_VALIDATION_INVALID_CHANNEL;
    }

    if ( payload_length < sizeof( ExecutionSpiTransmitPayloadPrefix_T ) )
    {
        return EXECUTION_INSTRUCTION_VALIDATION_INVALID_PAYLOAD_LENGTH;
    }

    ExecutionSpiTransmitPayloadPrefix_T prefix;
    ( void )memcpy( &prefix, payload, sizeof( prefix ) );

    if ( prefix.packet_count == 0U )
    {
        return EXECUTION_INSTRUCTION_VALIDATION_INVALID_PAYLOAD_DATA;
    }

    const uint32_t header_and_sizes_bytes =
        EXECUTION_SPI_PREFIX_SIZE_BYTES
        + EXECUTION_SPI_PACKET_SIZES_LENGTH_BYTES( prefix.packet_count );

    if ( payload_length < header_and_sizes_bytes )
    {
        return EXECUTION_INSTRUCTION_VALIDATION_INVALID_PAYLOAD_LENGTH;
    }

    uint32_t total_data_bytes = 0U;
    for ( uint32_t i = 0U; i < prefix.packet_count; i++ )
    {
        uint32_t packet_size = 0U;
        ( void )memcpy( &packet_size,
                        payload + EXECUTION_SPI_PACKET_SIZES_OFFSET_BYTES
                            + ( i * EXECUTION_SPI_PACKET_SIZE_FIELD_BYTES ),
                        sizeof( packet_size ) );

        if ( packet_size == 0U )
        {
            return EXECUTION_INSTRUCTION_VALIDATION_INVALID_PAYLOAD_DATA;
        }

        total_data_bytes += packet_size;
    }

    if ( payload_length != ( header_and_sizes_bytes + total_data_bytes ) )
    {
        return EXECUTION_INSTRUCTION_VALIDATION_INVALID_PAYLOAD_DATA;
    }

    return EXECUTION_INSTRUCTION_VALIDATION_OK;
}

static ExecutionInstructionValidationResult_T
EXECUTION_INSTRUCTION_ValidateUartTransmit( const uint8_t channel, const uint8_t* const payload,
                                           const uint16_t payload_length )
{
    ( void )payload;

    if ( ( channel != EXECUTION_OPERATION_UART_CHANNEL_1 )
         && ( channel != EXECUTION_OPERATION_UART_CHANNEL_2 ) )
    {
        return EXECUTION_INSTRUCTION_VALIDATION_INVALID_CHANNEL;
    }

    if ( ( payload_length == 0U )
         || ( payload_length > EXECUTION_VALIDATION_MAX_UART_PAYLOAD_BYTES ) )
    {
        return EXECUTION_INSTRUCTION_VALIDATION_INVALID_PAYLOAD_LENGTH;
    }

    return EXECUTION_INSTRUCTION_VALIDATION_OK;
}

/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */

ExecutionInstructionValidationResult_T
EXECUTION_INSTRUCTION_Validate( const uint8_t* const data, const size_t length )
{
    if ( data == NULL )
    {
        return EXECUTION_INSTRUCTION_VALIDATION_INVALID_ARGUMENT;
    }

    if ( length < sizeof( ExecutionInstructionHeader_T ) )
    {
        return EXECUTION_INSTRUCTION_VALIDATION_BUFFER_TOO_SMALL;
    }

    if ( length > EXECUTION_INSTRUCTION_MAX_SIZE_BYTES )
    {
        return EXECUTION_INSTRUCTION_VALIDATION_INVALID_HEADER;
    }

    if ( ( ( ( uintptr_t )data ) % EXECUTION_OPERATION_ALIGNMENT_BYTES ) != 0U )
    {
        return EXECUTION_INSTRUCTION_VALIDATION_UNALIGNED;
    }

    ExecutionInstructionHeader_T header;
    ( void )memcpy( &header, data, sizeof( header ) );

    if ( header.reserved != 0U )
    {
        return EXECUTION_INSTRUCTION_VALIDATION_INVALID_HEADER;
    }

    if ( ( header.operations_length_bytes % EXECUTION_OPERATION_ALIGNMENT_BYTES ) != 0U )
    {
        return EXECUTION_INSTRUCTION_VALIDATION_INVALID_HEADER;
    }

    if ( length != ( sizeof( ExecutionInstructionHeader_T ) + header.operations_length_bytes ) )
    {
        return EXECUTION_INSTRUCTION_VALIDATION_INVALID_HEADER;
    }

    if ( header.operation_count == 0U )
    {
        if ( header.operations_length_bytes != 0U )
        {
            return EXECUTION_INSTRUCTION_VALIDATION_OPERATION_COUNT_MISMATCH;
        }
        return EXECUTION_INSTRUCTION_VALIDATION_OK;
    }

    if ( header.operations_length_bytes == 0U )
    {
        return EXECUTION_INSTRUCTION_VALIDATION_OPERATION_COUNT_MISMATCH;
    }

    size_t  offset        = sizeof( ExecutionInstructionHeader_T );
    uint8_t decoded_count = 0U;

    while ( offset < length )
    {
        if ( ( length - offset ) < EXECUTION_OPERATION_HEADER_SIZE_BYTES )
        {
            return EXECUTION_INSTRUCTION_VALIDATION_BUFFER_TOO_SMALL;
        }

        ExecutionOperationHeaderWord_T header_word = 0U;
        ( void )memcpy( &header_word, data + offset, sizeof( header_word ) );

        const ExecutionOperationOpcode_T opcode = EXECUTION_OPERATION_GET_OPCODE( header_word );
        const uint8_t                    channel =
            EXECUTION_OPERATION_GET_CHANNEL( header_word );
        const uint16_t payload_length =
            EXECUTION_OPERATION_GET_PAYLOAD_LENGTH_BYTES( header_word );
        const uint32_t encoded_size =
            EXECUTION_OPERATION_ENCODED_SIZE_BYTES( payload_length );

        if ( ( length - offset ) < encoded_size )
        {
            return EXECUTION_INSTRUCTION_VALIDATION_INVALID_PAYLOAD_LENGTH;
        }

        const uint8_t* const payload = data + offset + EXECUTION_OPERATION_HEADER_SIZE_BYTES;

        /* Verify that trailing 4-byte boundary padding bytes are zero */
        for ( uint32_t pad_idx = EXECUTION_OPERATION_HEADER_SIZE_BYTES + payload_length;
              pad_idx < encoded_size; pad_idx++ )
        {
            if ( data[offset + pad_idx] != 0U )
            {
                return EXECUTION_INSTRUCTION_VALIDATION_INVALID_PAYLOAD_DATA;
            }
        }

        ExecutionInstructionValidationResult_T op_result =
            EXECUTION_INSTRUCTION_VALIDATION_OK;

        switch ( opcode )
        {
            case EXECUTION_OPERATION_OPCODE_DIGITAL_OUTPUT_UPDATE:
                op_result = EXECUTION_INSTRUCTION_ValidateDigitalOutput(
                    channel, payload, payload_length );
                break;

            case EXECUTION_OPERATION_OPCODE_ANALOGUE_OUTPUT_BATCH:
                op_result = EXECUTION_INSTRUCTION_ValidateAnalogueOutput(
                    channel, payload, payload_length );
                break;

            case EXECUTION_OPERATION_OPCODE_PWM_UPDATE:
                op_result = EXECUTION_INSTRUCTION_ValidatePwmUpdate(
                    channel, payload, payload_length );
                break;

            case EXECUTION_OPERATION_OPCODE_CAN_TRANSMIT:
                op_result = EXECUTION_INSTRUCTION_ValidateCanTransmit(
                    channel, payload, payload_length );
                break;

            case EXECUTION_OPERATION_OPCODE_SPI_TRANSMIT:
                op_result = EXECUTION_INSTRUCTION_ValidateSpiTransmit(
                    channel, payload, payload_length );
                break;

            case EXECUTION_OPERATION_OPCODE_UART_TRANSMIT:
                op_result = EXECUTION_INSTRUCTION_ValidateUartTransmit(
                    channel, payload, payload_length );
                break;

            default:
                return EXECUTION_INSTRUCTION_VALIDATION_INVALID_OPCODE;
        }

        if ( op_result != EXECUTION_INSTRUCTION_VALIDATION_OK )
        {
            return op_result;
        }

        offset += encoded_size;
        decoded_count++;
    }

    if ( decoded_count != header.operation_count )
    {
        return EXECUTION_INSTRUCTION_VALIDATION_OPERATION_COUNT_MISMATCH;
    }

    if ( offset != length )
    {
        return EXECUTION_INSTRUCTION_VALIDATION_INVALID_HEADER;
    }

    return EXECUTION_INSTRUCTION_VALIDATION_OK;
}
