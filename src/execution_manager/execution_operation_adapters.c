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
#include "exec_spi.h"
#include "exec_uart.h"
#include "exec_analogue_output.h"
#include "exec_can.h"

#include <stddef.h>
#include <stdint.h>
#ifndef TEST_BUILD
#include "stm32f4xx.h"
#endif

_Static_assert( sizeof( ExecutionCanPacket_T ) == sizeof( EXEC_CAN_Packet_T ),
                "Execution CAN payload must match driver packet size" );
_Static_assert( offsetof( ExecutionCanPacket_T, id ) == offsetof( EXEC_CAN_Packet_T, id ),
                "Execution CAN identifier offset changed" );
_Static_assert( offsetof( ExecutionCanPacket_T, dlc ) == offsetof( EXEC_CAN_Packet_T, dlc ),
                "Execution CAN DLC offset changed" );
_Static_assert( offsetof( ExecutionCanPacket_T, data ) == offsetof( EXEC_CAN_Packet_T, data ),
                "Execution CAN data offset changed" );
/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */

#define EXECUTION_OPERATION_DISPATCH_TABLE_SIZE ( EXECUTION_OPERATION_OPCODE_COUNT )

/**-----------------------------------------------------------------------------
 *  Private (static) Variables
 *------------------------------------------------------------------------------
 */

static const ExecutionOperationAdapter_T
    execution_operation_adapters[EXECUTION_OPERATION_DISPATCH_TABLE_SIZE] = {
        [EXECUTION_OPERATION_OPCODE_DIGITAL_OUTPUT_UPDATE] =
            EXECUTION_OPERATION_ADAPTER_ApplyDigitalOutput,
        [EXECUTION_OPERATION_OPCODE_PWM_UPDATE]    = EXECUTION_OPERATION_ADAPTER_ApplyPwmUpdate,
        [EXECUTION_OPERATION_OPCODE_SPI_TRANSMIT]  = EXECUTION_OPERATION_ADAPTER_ApplySpiTransmit,
        [EXECUTION_OPERATION_OPCODE_UART_TRANSMIT] = EXECUTION_OPERATION_ADAPTER_ApplyUartTransmit,
        [EXECUTION_OPERATION_OPCODE_ANALOGUE_OUTPUT_BATCH] =
            EXECUTION_OPERATION_ADAPTER_ApplyAnalogueOutput,
        [EXECUTION_OPERATION_OPCODE_CAN_TRANSMIT] = EXECUTION_OPERATION_ADAPTER_ApplyCanTransmit,
};

static volatile bool                               execution_operation_failure_valid = false;
static volatile ExecutionOperationAdapterFailure_T execution_operation_failure       = { 0 };
static volatile ExecutionOperationTiming_T
    execution_operation_timing[EXECUTION_OPERATION_OPCODE_COUNT] = { 0 };

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
            execution_operation_failure.operation_index = operation_index;
            execution_operation_failure.opcode          = opcode;
            execution_operation_failure.channel         = channel;
            execution_operation_failure_valid           = true;
            return result;
        }

        operation += EXECUTION_OPERATION_ENCODED_SIZE_BYTES( payload_length_bytes );
    }

    return EXECUTION_OPERATION_ADAPTER_ACCEPTED;
}

ExecutionOperationAdapterResult_T EXECUTION_OPERATION_ADAPTER_ApplyOperationsProfiled(
    const uint8_t* operations, uint8_t operation_count )
{
#ifdef TEST_BUILD
    return EXECUTION_OPERATION_ADAPTER_ApplyOperations( operations, operation_count );
#else
    const uint8_t* operation = operations;

    for ( uint8_t operation_index = 0U; operation_index < operation_count; operation_index++ )
    {
        const ExecutionOperationHeaderWord_T header_word =
            *( const ExecutionOperationHeaderWord_T* )( const void* )operation;
        const ExecutionOperationOpcode_T opcode = EXECUTION_OPERATION_GET_OPCODE( header_word );
        const uint8_t channel = EXECUTION_OPERATION_GET_CHANNEL( header_word );
        const uint16_t payload_length_bytes =
            EXECUTION_OPERATION_GET_PAYLOAD_LENGTH_BYTES( header_word );

        const uint32_t start_cycles = DWT->CYCCNT;
        const ExecutionOperationAdapterResult_T result = execution_operation_adapters[opcode](
            channel, &operation[EXECUTION_OPERATION_HEADER_SIZE_BYTES], payload_length_bytes );
        const uint32_t elapsed_cycles = DWT->CYCCNT - start_cycles;

        execution_operation_timing[opcode].sample_count++;
        execution_operation_timing[opcode].total_cycles += elapsed_cycles;
        if ( elapsed_cycles > execution_operation_timing[opcode].maximum_cycles )
        {
            execution_operation_timing[opcode].maximum_cycles = elapsed_cycles;
        }

        if ( result != EXECUTION_OPERATION_ADAPTER_ACCEPTED )
        {
            execution_operation_failure.operation_index = operation_index;
            execution_operation_failure.opcode          = opcode;
            execution_operation_failure.channel         = channel;
            execution_operation_failure_valid           = true;
            return result;
        }

        operation += EXECUTION_OPERATION_ENCODED_SIZE_BYTES( payload_length_bytes );
    }

    return EXECUTION_OPERATION_ADAPTER_ACCEPTED;
#endif
}

void EXECUTION_OPERATION_ADAPTER_ResetTiming( void )
{
    for ( uint32_t opcode = 0U; opcode < EXECUTION_OPERATION_OPCODE_COUNT; opcode++ )
    {
        execution_operation_timing[opcode].sample_count   = 0U;
        execution_operation_timing[opcode].total_cycles   = 0U;
        execution_operation_timing[opcode].maximum_cycles = 0U;
    }
}

bool EXECUTION_OPERATION_ADAPTER_GetTiming( ExecutionOperationOpcode_T opcode,
                                            ExecutionOperationTiming_T* timing )
{
    if ( opcode >= EXECUTION_OPERATION_OPCODE_COUNT || timing == NULL )
    {
        return false;
    }

    timing->sample_count   = execution_operation_timing[opcode].sample_count;
    timing->total_cycles   = execution_operation_timing[opcode].total_cycles;
    timing->maximum_cycles = execution_operation_timing[opcode].maximum_cycles;
    return true;
}

void EXECUTION_OPERATION_ADAPTER_ResetFailure( void )
{
    execution_operation_failure_valid           = false;
    execution_operation_failure.operation_index = 0U;
    execution_operation_failure.opcode          = 0U;
    execution_operation_failure.channel         = 0U;
}

bool EXECUTION_OPERATION_ADAPTER_GetFailure( ExecutionOperationAdapterFailure_T* failure )
{
    if ( !execution_operation_failure_valid || failure == NULL )
    {
        return false;
    }

    failure->operation_index = execution_operation_failure.operation_index;
    failure->opcode          = execution_operation_failure.opcode;
    failure->channel         = execution_operation_failure.channel;
    return true;
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
EXECUTION_OPERATION_ADAPTER_ApplySpiTransmit( uint8_t channel, const uint8_t* payload,
                                              uint16_t payload_length_bytes )
{
    ( void )payload_length_bytes;

    const ExecutionSpiTransmitPayloadPrefix_T* prefix =
        ( const ExecutionSpiTransmitPayloadPrefix_T* )( const void* )payload;

    const uint32_t* packet_sizes =
        ( const uint32_t* )( const void* )&payload[EXECUTION_SPI_PACKET_SIZES_OFFSET_BYTES];

    const uint8_t* data = &payload[EXECUTION_SPI_DATA_OFFSET_BYTES( prefix->packet_count )];

    return EXEC_SPI_Transmit( ( ExecSPIChannel_T )channel, data, packet_sizes,
                              prefix->packet_count )
               ? EXECUTION_OPERATION_ADAPTER_ACCEPTED
               : EXECUTION_OPERATION_ADAPTER_REJECTED;
}

ExecutionOperationAdapterResult_T
EXECUTION_OPERATION_ADAPTER_ApplyUartTransmit( uint8_t channel, const uint8_t* payload,
                                               uint16_t payload_length_bytes )
{
    return EXEC_UART_Transmit( ( ExecUartChannel_T )channel, payload, payload_length_bytes )
               ? EXECUTION_OPERATION_ADAPTER_ACCEPTED
               : EXECUTION_OPERATION_ADAPTER_REJECTED;
}

ExecutionOperationAdapterResult_T
EXECUTION_OPERATION_ADAPTER_ApplyAnalogueOutput( uint8_t channel, const uint8_t* payload,
                                                 uint16_t payload_length_bytes )
{
    ( void )channel;

    return EXEC_ANALOGUE_OUTPUT_Submit_Prepared_Batch( payload, payload_length_bytes )
               ? EXECUTION_OPERATION_ADAPTER_ACCEPTED
               : EXECUTION_OPERATION_ADAPTER_REJECTED;
}

ExecutionOperationAdapterResult_T
EXECUTION_OPERATION_ADAPTER_ApplyCanTransmit( uint8_t channel, const uint8_t* payload,
                                              uint16_t payload_length_bytes )
{
    const uint16_t packet_count =
        ( uint16_t )( payload_length_bytes / EXECUTION_CAN_PACKET_SIZE_BYTES );

    const EXEC_CAN_Result_T result =
        EXEC_CAN_Transmit( ( EXEC_CAN_Channel_T )channel,
                           ( const EXEC_CAN_Packet_T* )( const void* )payload, packet_count );

    return result == EXEC_CAN_RESULT_OK ? EXECUTION_OPERATION_ADAPTER_ACCEPTED
                                        : EXECUTION_OPERATION_ADAPTER_REJECTED;
}
