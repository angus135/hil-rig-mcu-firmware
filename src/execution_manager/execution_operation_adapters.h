/******************************************************************************
 *  File:       execution_operation_adapters.h
 *  Author:     Callum Rafferty
 *  Created:    07/09/2026
 *
 *  Description:
 *      Zero-copy dispatch interface from canonical operation storage to
 *      execution-facing driver calls.
 ******************************************************************************/

#ifndef EXECUTION_OPERATION_ADAPTERS_H
#define EXECUTION_OPERATION_ADAPTERS_H

#ifdef __cplusplus
extern "C"
{
#endif

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include <stdint.h>
#include "execution_operation_payloads.h"
/**-----------------------------------------------------------------------------
 *  Public Defines / Macros
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Public Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

/**
 * @brief Result returned by every execution-operation adapter.
 */
typedef enum
{
    /** The driver accepted all data required by the operation. */
    EXECUTION_OPERATION_ADAPTER_ACCEPTED = 0,

    /**
     * The encoded channel, payload length, or payload contents were unsafe to
     * pass to the selected driver.
     */
    EXECUTION_OPERATION_ADAPTER_INVALID,

    /**
     * The operation was valid, but the driver could not accept it for
     * execution.
     */
    EXECUTION_OPERATION_ADAPTER_REJECTED
} ExecutionOperationAdapterResult_T;

/**
 * @brief Common signature used by the opcode-indexed adapter table.
 *
 * The Host Interface validates the encoded operation stream before storage.
 * The ISR walker and adapters rely on that session contract and perform no
 * repeated format validation.
 *
 * payload points directly into Flash Manager instruction storage. It remains
 * valid only until the enclosing instruction is consumed. An adapter or driver
 * must copy or queue all required data before returning ACCEPTED and must not
 * retain the pointer.
 *
 * @param channel              Peripheral or output variant selected by the operation.
 * @param payload              Read-only operation-specific payload bytes.
 * @param payload_length_bytes Exact payload length, excluding header and padding.
 */
typedef ExecutionOperationAdapterResult_T ( *ExecutionOperationAdapter_T )(
    uint8_t channel, const uint8_t* payload, uint16_t payload_length_bytes );

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

ExecutionOperationAdapterResult_T
EXECUTION_OPERATION_ADAPTER_ApplyOperations( const uint8_t* operations, uint8_t operation_count );

/**
 * @brief Applies one prevalidated digital-output update directly from aligned storage.
 *
 * @pre channel is EXECUTION_OPERATION_CHANNEL_UNUSED.
 * @pre payload points to an aligned, validated digital-output payload.
 * @pre payload_length_bytes is EXECUTION_DIGITAL_OUTPUT_PAYLOAD_SIZE_BYTES.
 *
 * @return EXECUTION_OPERATION_ADAPTER_ACCEPTED.
 */
ExecutionOperationAdapterResult_T
EXECUTION_OPERATION_ADAPTER_ApplyDigitalOutput( uint8_t channel, const uint8_t* payload,
                                                uint16_t payload_length_bytes );

/**
 * @brief Applies one prevalidated PWM update directly from aligned storage.
 *
 * The update writes ARR, CCR, and PSC preload values for the selected timer.
 * They become active together at the timer's next natural update event; this
 * adapter does not reset PWM phase or force an update event.
 *
 * @pre channel is EXECUTION_OPERATION_PWM_CHANNEL_LV or
 *      EXECUTION_OPERATION_PWM_CHANNEL_HV.
 * @pre payload points to an aligned, validated ExecutionPwmUpdatePayload_T.
 * @pre payload_length_bytes is EXECUTION_PWM_UPDATE_PAYLOAD_SIZE_BYTES.
 * @pre the selected PWM channel is configured and started.
 *
 * @return EXECUTION_OPERATION_ADAPTER_ACCEPTED.
 */
ExecutionOperationAdapterResult_T
EXECUTION_OPERATION_ADAPTER_ApplyPwmUpdate( uint8_t channel, const uint8_t* payload,
                                            uint16_t payload_length_bytes );

#ifdef __cplusplus
}
#endif

#endif /* EXECUTION_OPERATION_ADAPTERS_H */
