/******************************************************************************
 *  File:       execution_instruction.h
 *  Author:     Callum Rafferty
 *  Created:    15-Sep-2026
 *
 *  Description:
 *      Public prepared-instruction format and canonical binary validation
 *      owned by the Execution Manager.
 *
 *  Notes:
 *      The Host Interface produces this format for storage. The Flash Manager
 *      transports it and treats the operation bytes as opaque. One instruction
 *      contains all output operations scheduled for one Execution Manager tick.
 *      The stored header is two little-endian 32-bit words and every operation
 *      boundary and complete instruction image is four-byte aligned.
 ******************************************************************************/

#ifndef EXECUTION_INSTRUCTION_H
#define EXECUTION_INSTRUCTION_H

#ifdef __cplusplus
extern "C"
{
#endif

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**-----------------------------------------------------------------------------
 *  Public Defines / Macros
 *------------------------------------------------------------------------------
 */

/** Maximum encoded size of one complete instruction, including its header. */
#define EXECUTION_INSTRUCTION_MAX_SIZE_BYTES ( 4096U )

/**-----------------------------------------------------------------------------
 *  Public Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

/**
 * @brief Return status codes for canonical execution instruction validation.
 */
typedef enum
{
    /** Instruction is valid, properly aligned, and all operations are safe to execute. */
    EXECUTION_INSTRUCTION_VALIDATION_OK = 0,

    /** data pointer is NULL or length is 0. */
    EXECUTION_INSTRUCTION_VALIDATION_INVALID_ARGUMENT,

    /** Buffer length is smaller than the 8-byte instruction header. */
    EXECUTION_INSTRUCTION_VALIDATION_BUFFER_TOO_SMALL,

    /** Instruction buffer or operation payload is not 4-byte aligned. */
    EXECUTION_INSTRUCTION_VALIDATION_UNALIGNED,

    /** Header fields violate protocol rules (e.g. non-zero reserved, unaligned length, length
       mismatch). */
    EXECUTION_INSTRUCTION_VALIDATION_INVALID_HEADER,

    /** Operation contains an unrecognized or unsupported opcode. */
    EXECUTION_INSTRUCTION_VALIDATION_INVALID_OPCODE,

    /** Operation specifies an invalid channel for the given opcode. */
    EXECUTION_INSTRUCTION_VALIDATION_INVALID_CHANNEL,

    /** Operation payload length does not match expected size for the given opcode. */
    EXECUTION_INSTRUCTION_VALIDATION_INVALID_PAYLOAD_LENGTH,

    /** Operation payload values violate hardware constraints (e.g. overlapping bitmasks, invalid
       duty). */
    EXECUTION_INSTRUCTION_VALIDATION_INVALID_PAYLOAD_DATA,

    /** Decoded operation count does not match the header operation_count. */
    EXECUTION_INSTRUCTION_VALIDATION_OPERATION_COUNT_MISMATCH,
} ExecutionInstructionValidationResult_T;

/**
 * @brief Fixed header stored before the operations for one execution tick.
 *
 * operations_length_bytes includes operation headers, operation payloads, and
 * four-byte alignment padding. It excludes this instruction header and must be
 * divisible by four. In the stored little-endian header, timestamp occupies
 * word zero; word one contains operations_length_bytes in bits 0-15,
 * operation_count in bits 16-23, and reserved in bits 24-31. The Host Interface
 * must set reserved to zero.
 */
typedef struct
{
    /** One-based execution boundary on which every contained operation is due. */
    uint32_t timestamp;

    /** Total encoded operation bytes following this header. */
    uint16_t operations_length_bytes;

    /** Number of complete operations encoded after this header. */
    uint8_t operation_count;

    /** Reserved for future instruction flags; must be zero. */
    uint8_t reserved;
} ExecutionInstructionHeader_T;

#if defined( __cplusplus )
static_assert( sizeof( ExecutionInstructionHeader_T ) == 8U,
               "Execution instruction header layout changed" );
#else
_Static_assert( sizeof( ExecutionInstructionHeader_T ) == 8U,
                "Execution instruction header layout changed" );
#endif

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

/**
 * @brief Validates a complete packed canonical instruction byte stream.
 *
 * @details Performs comprehensive structural, alignment, and parameter validation
 *          on a complete canonical execution instruction record:
 *            - Verifies 4-byte memory alignment of the instruction buffer.
 *            - Validates ExecutionInstructionHeader_T invariants (reserved == 0,
 *              operations_length_bytes % 4 == 0, total length match).
 *            - Decodes each operation header and checks opcode, channel, and
 *              payload length invariants.
 *            - Validates operation payload data against hardware limits (e.g.
 *              disjoint digital output bitmasks, DAC frame bounds, PWM ARR/CCR
 *              feasibility, CAN identifier and DLC limits).
 *            - Verifies that the total decoded operation count matches
 *              header->operation_count.
 *
 * @param[in] data   Pointer to 4-byte aligned instruction byte buffer. Must not be NULL.
 * @param[in] length Total byte length of the instruction buffer.
 *
 * @return EXECUTION_INSTRUCTION_VALIDATION_OK on success, or specific failure code.
 */
ExecutionInstructionValidationResult_T EXECUTION_INSTRUCTION_Validate( const uint8_t* data,
                                                                       size_t         length );

#ifdef __cplusplus
}
#endif

#endif /* EXECUTION_INSTRUCTION_H */
