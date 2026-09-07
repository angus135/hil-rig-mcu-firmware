/******************************************************************************
 *  File:       execution_operation_adapters.c
 *  Author:     Callum Rafferty
 *  Created:    07/09/2026
 *
 *  Description:
 *      Zero-copy adapters from encoded Execution Manager operations to the
 *      existing execution-driver APIs.
 *
 *  Notes:
 *      Payload pointers refer to aligned Flash Manager word storage and are
 *      valid only until the enclosing instruction is consumed.
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */
#include "execution_operation_adapters.h"
#include "execution_operation_payloads.h"
#include "exec_digital_output.h"

#include <stdint.h>

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */

#define EXECUTION_OPERATION_IMPLEMENTED_OPCODE_COUNT ( 1U )

/**-----------------------------------------------------------------------------
 *  Private (static) Variables
 *------------------------------------------------------------------------------
 */

static const ExecutionOperationAdapter_T
    execution_operation_adapters[EXECUTION_OPERATION_IMPLEMENTED_OPCODE_COUNT] = {
        [EXECUTION_OPERATION_OPCODE_DIGITAL_OUTPUT_UPDATE] =
            EXECUTION_OPERATION_ADAPTER_ApplyDigitalOutput,
};

/**-----------------------------------------------------------------------------
 *  Private (static) Function Prototypes
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */

ExecutionOperationAdapterResult_T
EXECUTION_OPERATION_ADAPTER_ApplyOperations( const uint8_t* operations, uint8_t operation_count )
{
    const uint8_t* operation = operations;

    for ( uint8_t operation_index = 0U; operation_index < operation_count; operation_index++ )
    {
        ExecutionOperationHeaderWord_T header_word =
            *( const ExecutionOperationHeaderWord_T* )( const void* )operation;

        ExecutionOperationOpcode_T opcode = EXECUTION_OPERATION_GET_OPCODE( header_word );

        uint8_t channel = EXECUTION_OPERATION_GET_CHANNEL( header_word );

        uint16_t payload_length_bytes = EXECUTION_OPERATION_GET_PAYLOAD_LENGTH_BYTES( header_word );

        ExecutionOperationAdapterResult_T result = execution_operation_adapters[opcode](
            channel, &operation[EXECUTION_OPERATION_HEADER_SIZE_BYTES], payload_length_bytes );

        if ( result != EXECUTION_OPERATION_ADAPTER_ACCEPTED )
        {
            return result;
        }

        operation += EXECUTION_OPERATION_ENCODED_SIZE_BYTES( payload_length_bytes );
    }

    return EXECUTION_OPERATION_ADAPTER_ACCEPTED;
}

ExecutionOperationAdapterResult_T
EXECUTION_OPERATION_ADAPTER_ApplyDigitalOutput( uint8_t channel, const uint8_t* payload,
                                                uint16_t payload_length_bytes )
{
    ( void )channel;
    ( void )payload_length_bytes;

    const uint32_t* payload_words = ( const uint32_t* )( const void* )payload;

    EXEC_DIGITAL_OUTPUT_Set_Output(
        payload_words[EXECUTION_DIGITAL_OUTPUT_HIGH_BITMASK_WORD_INDEX] );

    EXEC_DIGITAL_OUTPUT_Reset_Output(
        payload_words[EXECUTION_DIGITAL_OUTPUT_LOW_BITMASK_WORD_INDEX] );

    return EXECUTION_OPERATION_ADAPTER_ACCEPTED;
}
