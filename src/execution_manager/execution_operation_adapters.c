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
#include "exec_pwm_gen.h"
#include "exec_uart.h"

#include <stdint.h>

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */

#define EXECUTION_OPERATION_DISPATCH_TABLE_SIZE ( EXECUTION_OPERATION_OPCODE_UART_TRANSMIT + 1U )

/**-----------------------------------------------------------------------------
 *  Private (static) Variables
 *------------------------------------------------------------------------------
 */

static const ExecutionOperationAdapter_T
    execution_operation_adapters[EXECUTION_OPERATION_DISPATCH_TABLE_SIZE] = {
        [EXECUTION_OPERATION_OPCODE_DIGITAL_OUTPUT_UPDATE] =
            EXECUTION_OPERATION_ADAPTER_ApplyDigitalOutput,
        [EXECUTION_OPERATION_OPCODE_PWM_UPDATE]    = EXECUTION_OPERATION_ADAPTER_ApplyPwmUpdate,
        [EXECUTION_OPERATION_OPCODE_UART_TRANSMIT] = EXECUTION_OPERATION_ADAPTER_ApplyUartTransmit,
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

    const ExecutionDigitalOutputPayload_T* digital_output =
        ( const ExecutionDigitalOutputPayload_T* )( const void* )payload;

    EXEC_DIGITAL_OUTPUT_Set_Output( digital_output->high_bitmask );

    EXEC_DIGITAL_OUTPUT_Reset_Output( digital_output->low_bitmask );

    return EXECUTION_OPERATION_ADAPTER_ACCEPTED;
}

ExecutionOperationAdapterResult_T
EXECUTION_OPERATION_ADAPTER_ApplyPwmUpdate( uint8_t channel, const uint8_t* payload,
                                            uint16_t payload_length_bytes )
{
    ( void )payload_length_bytes;

    const ExecutionPwmUpdatePayload_T* pwm_update =
        ( const ExecutionPwmUpdatePayload_T* )( const void* )payload;

    if ( channel == EXECUTION_OPERATION_PWM_CHANNEL_LV )
    {
        EXEC_PWM_GEN_Set_PWM_LV( pwm_update->arr, pwm_update->ccr, pwm_update->psc );
    }
    else
    {
        EXEC_PWM_GEN_Set_PWM_HV( pwm_update->arr, pwm_update->ccr, pwm_update->psc );
    }

    return EXECUTION_OPERATION_ADAPTER_ACCEPTED;
}

ExecutionOperationAdapterResult_T
EXECUTION_OPERATION_ADAPTER_ApplyUartTransmit( uint8_t channel, const uint8_t* payload,
                                               uint16_t payload_length_bytes )
{
    return EXEC_UART_Transmit( ( ExecUartChannel_T )channel, payload, payload_length_bytes )
               ? EXECUTION_OPERATION_ADAPTER_ACCEPTED
               : EXECUTION_OPERATION_ADAPTER_REJECTED;
}
