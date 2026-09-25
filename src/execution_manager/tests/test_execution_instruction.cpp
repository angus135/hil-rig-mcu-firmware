/******************************************************************************
 *  File:       test_execution_instruction.cpp
 *  Author:     Callum Rafferty
 *  Created:    15-Sep-2026
 *
 *  Description:
 *      Unit tests for canonical Execution Manager instruction validation
 *      (EXECUTION_INSTRUCTION_Validate).
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <vector>

extern "C"
{
#include "execution_instruction.h"
#include "execution_operation_payloads.h"
}

/**-----------------------------------------------------------------------------
 *  Helper Functions
 *------------------------------------------------------------------------------
 */

namespace {

struct AlignedBuffer
{
    alignas( 4 ) uint8_t data[EXECUTION_INSTRUCTION_MAX_SIZE_BYTES];
};

void PackHeader( uint8_t* buffer, uint32_t timestamp, uint16_t operations_length_bytes,
                 uint8_t operation_count, uint8_t reserved = 0U )
{
    ExecutionInstructionHeader_T header;
    header.timestamp               = timestamp;
    header.operations_length_bytes = operations_length_bytes;
    header.operation_count         = operation_count;
    header.reserved                = reserved;
    ( void )memcpy( buffer, &header, sizeof( header ) );
}

void PackOperation( uint8_t* buffer, size_t& offset, ExecutionOperationOpcode_T opcode,
                    uint8_t channel, const void* payload, uint16_t payload_size )
{
    const uint32_t encoded_size = EXECUTION_OPERATION_ENCODED_SIZE_BYTES( payload_size );
    ( void )memset( buffer + offset, 0, encoded_size );

    const ExecutionOperationHeaderWord_T header_word =
        ( ( ( uint32_t )opcode << EXECUTION_OPERATION_OPCODE_SHIFT )
          & EXECUTION_OPERATION_OPCODE_MASK )
        | ( ( ( uint32_t )channel << EXECUTION_OPERATION_CHANNEL_SHIFT )
            & EXECUTION_OPERATION_CHANNEL_MASK )
        | ( ( ( uint32_t )payload_size << EXECUTION_OPERATION_PAYLOAD_LENGTH_SHIFT )
            & EXECUTION_OPERATION_PAYLOAD_LENGTH_MASK );

    ( void )memcpy( buffer + offset, &header_word, sizeof( header_word ) );
    if ( ( payload != nullptr ) && ( payload_size > 0U ) )
    {
        ( void )memcpy( buffer + offset + sizeof( header_word ), payload, payload_size );
    }
    offset += encoded_size;
}

}  // namespace

/**-----------------------------------------------------------------------------
 *  Unit Tests: Parameter & Header Validation
 *------------------------------------------------------------------------------
 */

TEST( ExecutionInstructionValidationTest, NullPointerReturnsInvalidArgument )
{
    EXPECT_EQ( EXECUTION_INSTRUCTION_Validate( nullptr, 8U ),
               EXECUTION_INSTRUCTION_VALIDATION_INVALID_ARGUMENT );
}

TEST( ExecutionInstructionValidationTest, BufferSmallerThanHeaderReturnsBufferTooSmall )
{
    AlignedBuffer buf;
    EXPECT_EQ( EXECUTION_INSTRUCTION_Validate( buf.data, 7U ),
               EXECUTION_INSTRUCTION_VALIDATION_BUFFER_TOO_SMALL );
}

TEST( ExecutionInstructionValidationTest, BufferExceedingMaxSizeReturnsInvalidHeader )
{
    AlignedBuffer buf;
    EXPECT_EQ(
        EXECUTION_INSTRUCTION_Validate( buf.data, EXECUTION_INSTRUCTION_MAX_SIZE_BYTES + 4U ),
        EXECUTION_INSTRUCTION_VALIDATION_INVALID_HEADER );
}

TEST( ExecutionInstructionValidationTest, UnalignedPointerReturnsUnaligned )
{
    AlignedBuffer buf;
    PackHeader( buf.data, 1U, 0U, 0U );
    EXPECT_EQ(
        EXECUTION_INSTRUCTION_Validate( buf.data + 1, sizeof( ExecutionInstructionHeader_T ) ),
        EXECUTION_INSTRUCTION_VALIDATION_UNALIGNED );
}

TEST( ExecutionInstructionValidationTest, NonZeroReservedFieldReturnsInvalidHeader )
{
    AlignedBuffer buf;
    PackHeader( buf.data, 1U, 0U, 0U, 1U );  // reserved = 1
    EXPECT_EQ( EXECUTION_INSTRUCTION_Validate( buf.data, sizeof( ExecutionInstructionHeader_T ) ),
               EXECUTION_INSTRUCTION_VALIDATION_INVALID_HEADER );
}

TEST( ExecutionInstructionValidationTest, UnalignedOperationsLengthReturnsInvalidHeader )
{
    AlignedBuffer buf;
    PackHeader( buf.data, 1U, 5U, 1U );  // 5 is not divisible by 4
    EXPECT_EQ(
        EXECUTION_INSTRUCTION_Validate( buf.data, sizeof( ExecutionInstructionHeader_T ) + 5U ),
        EXECUTION_INSTRUCTION_VALIDATION_INVALID_HEADER );
}

TEST( ExecutionInstructionValidationTest, LengthMismatchReturnsInvalidHeader )
{
    AlignedBuffer buf;
    PackHeader( buf.data, 1U, 8U, 1U );
    EXPECT_EQ(
        EXECUTION_INSTRUCTION_Validate( buf.data, sizeof( ExecutionInstructionHeader_T ) + 4U ),
        EXECUTION_INSTRUCTION_VALIDATION_INVALID_HEADER );
}

TEST( ExecutionInstructionValidationTest, ValidEmptyInstructionReturnsOk )
{
    AlignedBuffer buf;
    PackHeader( buf.data, 1U, 0U, 0U );
    EXPECT_EQ( EXECUTION_INSTRUCTION_Validate( buf.data, sizeof( ExecutionInstructionHeader_T ) ),
               EXECUTION_INSTRUCTION_VALIDATION_OK );
}

/**-----------------------------------------------------------------------------
 *  Unit Tests: Digital Output Operation Validation
 *------------------------------------------------------------------------------
 */

TEST( ExecutionInstructionValidationTest, ValidDigitalOutputReturnsOk )
{
    AlignedBuffer buf;
    size_t        offset = sizeof( ExecutionInstructionHeader_T );

    ExecutionDigitalOutputPayload_T payload = { .high_bitmask = 0x01U, .low_bitmask = 0x02U };
    PackOperation( buf.data, offset, EXECUTION_OPERATION_OPCODE_DIGITAL_OUTPUT_UPDATE,
                   EXECUTION_OPERATION_CHANNEL_UNUSED, &payload, sizeof( payload ) );

    PackHeader( buf.data, 1U,
                static_cast<uint16_t>( offset - sizeof( ExecutionInstructionHeader_T ) ), 1U );

    EXPECT_EQ( EXECUTION_INSTRUCTION_Validate( buf.data, offset ),
               EXECUTION_INSTRUCTION_VALIDATION_OK );
}

TEST( ExecutionInstructionValidationTest, DigitalOutputInvalidChannelReturnsInvalidChannel )
{
    AlignedBuffer buf;
    size_t        offset = sizeof( ExecutionInstructionHeader_T );

    ExecutionDigitalOutputPayload_T payload = { .high_bitmask = 0x01U, .low_bitmask = 0x00U };
    PackOperation( buf.data, offset, EXECUTION_OPERATION_OPCODE_DIGITAL_OUTPUT_UPDATE,
                   1U,  // channel must be UNUSED (0)
                   &payload, sizeof( payload ) );

    PackHeader( buf.data, 1U,
                static_cast<uint16_t>( offset - sizeof( ExecutionInstructionHeader_T ) ), 1U );

    EXPECT_EQ( EXECUTION_INSTRUCTION_Validate( buf.data, offset ),
               EXECUTION_INSTRUCTION_VALIDATION_INVALID_CHANNEL );
}

TEST( ExecutionInstructionValidationTest,
      DigitalOutputOverlappingBitmasksReturnsInvalidPayloadData )
{
    AlignedBuffer buf;
    size_t        offset = sizeof( ExecutionInstructionHeader_T );

    ExecutionDigitalOutputPayload_T payload = { .high_bitmask = 0x05U,
                                                .low_bitmask  = 0x04U };  // overlap on bit 2
    PackOperation( buf.data, offset, EXECUTION_OPERATION_OPCODE_DIGITAL_OUTPUT_UPDATE,
                   EXECUTION_OPERATION_CHANNEL_UNUSED, &payload, sizeof( payload ) );

    PackHeader( buf.data, 1U,
                static_cast<uint16_t>( offset - sizeof( ExecutionInstructionHeader_T ) ), 1U );

    EXPECT_EQ( EXECUTION_INSTRUCTION_Validate( buf.data, offset ),
               EXECUTION_INSTRUCTION_VALIDATION_INVALID_PAYLOAD_DATA );
}

/**-----------------------------------------------------------------------------
 *  Unit Tests: Analogue Output Operation Validation
 *------------------------------------------------------------------------------
 */

TEST( ExecutionInstructionValidationTest, ValidAnalogueBatchReturnsOk )
{
    AlignedBuffer buf;
    size_t        offset = sizeof( ExecutionInstructionHeader_T );

    // 2 frames (6 bytes)
    uint8_t batch[6] = {
        static_cast<uint8_t>( 0U << 3U ), 0x0F, 0xFF,  // channel 0, DAC count 4095
        static_cast<uint8_t>( 5U << 3U ), 0x07, 0xFF,  // channel 5, DAC count 2047
    };

    PackOperation( buf.data, offset, EXECUTION_OPERATION_OPCODE_ANALOGUE_OUTPUT_BATCH,
                   EXECUTION_OPERATION_CHANNEL_UNUSED, batch, sizeof( batch ) );

    PackHeader( buf.data, 1U,
                static_cast<uint16_t>( offset - sizeof( ExecutionInstructionHeader_T ) ), 1U );

    EXPECT_EQ( EXECUTION_INSTRUCTION_Validate( buf.data, offset ),
               EXECUTION_INSTRUCTION_VALIDATION_OK );
}

TEST( ExecutionInstructionValidationTest, AnalogueFrameInvalidChannelReturnsInvalidPayloadData )
{
    AlignedBuffer buf;
    size_t        offset = sizeof( ExecutionInstructionHeader_T );

    uint8_t frame[3] = { static_cast<uint8_t>( 6U << 3U ), 0x08,
                         0x00 };  // channel 6 is out of 0..5 range
    PackOperation( buf.data, offset, EXECUTION_OPERATION_OPCODE_ANALOGUE_OUTPUT_BATCH,
                   EXECUTION_OPERATION_CHANNEL_UNUSED, frame, sizeof( frame ) );

    PackHeader( buf.data, 1U,
                static_cast<uint16_t>( offset - sizeof( ExecutionInstructionHeader_T ) ), 1U );

    EXPECT_EQ( EXECUTION_INSTRUCTION_Validate( buf.data, offset ),
               EXECUTION_INSTRUCTION_VALIDATION_INVALID_PAYLOAD_DATA );
}

/**-----------------------------------------------------------------------------
 *  Unit Tests: PWM Update Operation Validation
 *------------------------------------------------------------------------------
 */

TEST( ExecutionInstructionValidationTest, ValidPwmUpdateReturnsOk )
{
    AlignedBuffer buf;
    size_t        offset = sizeof( ExecutionInstructionHeader_T );

    ExecutionPwmUpdatePayload_T payload = { .arr = 999U, .ccr = 500U, .psc = 0U };
    PackOperation( buf.data, offset, EXECUTION_OPERATION_OPCODE_PWM_UPDATE,
                   EXECUTION_OPERATION_PWM_CHANNEL_LV, &payload, sizeof( payload ) );

    PackHeader( buf.data, 1U,
                static_cast<uint16_t>( offset - sizeof( ExecutionInstructionHeader_T ) ), 1U );

    EXPECT_EQ( EXECUTION_INSTRUCTION_Validate( buf.data, offset ),
               EXECUTION_INSTRUCTION_VALIDATION_OK );
}

TEST( ExecutionInstructionValidationTest, PwmUpdateZeroArrReturnsInvalidPayloadData )
{
    AlignedBuffer buf;
    size_t        offset = sizeof( ExecutionInstructionHeader_T );

    ExecutionPwmUpdatePayload_T payload = { .arr = 0U, .ccr = 0U, .psc = 0U };
    PackOperation( buf.data, offset, EXECUTION_OPERATION_OPCODE_PWM_UPDATE,
                   EXECUTION_OPERATION_PWM_CHANNEL_LV, &payload, sizeof( payload ) );

    PackHeader( buf.data, 1U,
                static_cast<uint16_t>( offset - sizeof( ExecutionInstructionHeader_T ) ), 1U );

    EXPECT_EQ( EXECUTION_INSTRUCTION_Validate( buf.data, offset ),
               EXECUTION_INSTRUCTION_VALIDATION_INVALID_PAYLOAD_DATA );
}

TEST( ExecutionInstructionValidationTest, PwmUpdateCcrExceedingPeriodReturnsInvalidPayloadData )
{
    AlignedBuffer buf;
    size_t        offset = sizeof( ExecutionInstructionHeader_T );

    ExecutionPwmUpdatePayload_T payload = { .arr = 100U, .ccr = 102U, .psc = 0U };  // CCR > ARR + 1
    PackOperation( buf.data, offset, EXECUTION_OPERATION_OPCODE_PWM_UPDATE,
                   EXECUTION_OPERATION_PWM_CHANNEL_HV, &payload, sizeof( payload ) );

    PackHeader( buf.data, 1U,
                static_cast<uint16_t>( offset - sizeof( ExecutionInstructionHeader_T ) ), 1U );

    EXPECT_EQ( EXECUTION_INSTRUCTION_Validate( buf.data, offset ),
               EXECUTION_INSTRUCTION_VALIDATION_INVALID_PAYLOAD_DATA );
}

/**-----------------------------------------------------------------------------
 *  Unit Tests: Multi-Operation Count & Padding Validation
 *------------------------------------------------------------------------------
 */

TEST( ExecutionInstructionValidationTest, MultiOperationValidInstructionReturnsOk )
{
    AlignedBuffer buf;
    size_t        offset = sizeof( ExecutionInstructionHeader_T );

    ExecutionDigitalOutputPayload_T dig = { .high_bitmask = 0x04U, .low_bitmask = 0x00U };
    PackOperation( buf.data, offset, EXECUTION_OPERATION_OPCODE_DIGITAL_OUTPUT_UPDATE,
                   EXECUTION_OPERATION_CHANNEL_UNUSED, &dig, sizeof( dig ) );

    ExecutionPwmUpdatePayload_T pwm = { .arr = 1000U, .ccr = 250U, .psc = 4U };
    PackOperation( buf.data, offset, EXECUTION_OPERATION_OPCODE_PWM_UPDATE,
                   EXECUTION_OPERATION_PWM_CHANNEL_LV, &pwm, sizeof( pwm ) );

    PackHeader( buf.data, 1U,
                static_cast<uint16_t>( offset - sizeof( ExecutionInstructionHeader_T ) ), 2U );

    EXPECT_EQ( EXECUTION_INSTRUCTION_Validate( buf.data, offset ),
               EXECUTION_INSTRUCTION_VALIDATION_OK );
}

TEST( ExecutionInstructionValidationTest, OperationCountMismatchReturnsMismatch )
{
    AlignedBuffer buf;
    size_t        offset = sizeof( ExecutionInstructionHeader_T );

    ExecutionDigitalOutputPayload_T dig = { .high_bitmask = 0x04U, .low_bitmask = 0x00U };
    PackOperation( buf.data, offset, EXECUTION_OPERATION_OPCODE_DIGITAL_OUTPUT_UPDATE,
                   EXECUTION_OPERATION_CHANNEL_UNUSED, &dig, sizeof( dig ) );

    // Header claims 2 operations, but only 1 encoded
    PackHeader( buf.data, 1U,
                static_cast<uint16_t>( offset - sizeof( ExecutionInstructionHeader_T ) ), 2U );

    EXPECT_EQ( EXECUTION_INSTRUCTION_Validate( buf.data, offset ),
               EXECUTION_INSTRUCTION_VALIDATION_OPERATION_COUNT_MISMATCH );
}

TEST( ExecutionInstructionValidationTest, NonZeroPaddingByteReturnsInvalidPayloadData )
{
    AlignedBuffer buf;
    size_t        offset = sizeof( ExecutionInstructionHeader_T );

    // 3-byte payload padded to 4 bytes
    uint8_t payload[3] = { static_cast<uint8_t>( 1U << 3U ), 0x05, 0x00 };
    PackOperation( buf.data, offset, EXECUTION_OPERATION_OPCODE_ANALOGUE_OUTPUT_BATCH,
                   EXECUTION_OPERATION_CHANNEL_UNUSED, payload, sizeof( payload ) );

    // Corrupt padding byte at offset - 1
    buf.data[offset - 1] = 0xFFU;

    PackHeader( buf.data, 1U,
                static_cast<uint16_t>( offset - sizeof( ExecutionInstructionHeader_T ) ), 1U );

    EXPECT_EQ( EXECUTION_INSTRUCTION_Validate( buf.data, offset ),
               EXECUTION_INSTRUCTION_VALIDATION_INVALID_PAYLOAD_DATA );
}
