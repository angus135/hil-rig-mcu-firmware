/******************************************************************************
 *  File:       execution_instruction.h
 *
 *  Description:
 *      Public prepared-instruction format owned by the Execution Manager.
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

#include <stdint.h>

/** Maximum encoded size of one complete instruction, including its header. */
#define EXECUTION_INSTRUCTION_MAX_SIZE_BYTES ( 4096U )

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
    /** Execution Manager tick on which every contained operation is due. */
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

#ifdef __cplusplus
}
#endif

#endif /* EXECUTION_INSTRUCTION_H */
