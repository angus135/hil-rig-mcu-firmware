/******************************************************************************
 *  File:       execution_instruction.h
 *
 *  Description:
 *      Shared stored-format definitions used by the Host Interface, Flash
 *      Manager, and Execution Manager.
 *
 *  Notes:
 *      One instruction contains all output operations scheduled for one
 *      Execution Manager tick. The Flash Manager uses only the fixed header
 *      and treats the following operation bytes as opaque. Operation headers,
 *      opcodes, and payload layouts will be added as separate decisions.
 ******************************************************************************/

#ifndef EXECUTION_INSTRUCTION_H
#define EXECUTION_INSTRUCTION_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdint.h>

/**
 * @brief Fixed header stored before the operations for one execution tick.
 *
 * operations_length_bytes includes operation headers, operation payloads, and
 * alignment padding. It excludes this instruction header. The Host Interface
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
