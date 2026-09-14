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

#include <stdbool.h>
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

/** Identifies the first operation rejected by the adapter walker. */
typedef struct
{
    uint8_t                    operation_index;
    ExecutionOperationOpcode_T opcode;
    uint8_t                    channel;
} ExecutionOperationAdapterFailure_T;

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

typedef struct
{
    uint32_t sample_count;
    uint64_t total_cycles;
    uint32_t maximum_cycles;
} ExecutionOperationTiming_T;

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

ExecutionOperationAdapterResult_T
EXECUTION_OPERATION_ADAPTER_ApplyOperations( const uint8_t* operations, uint8_t operation_count );

/** Profiling-only dispatcher. The normal dispatcher contains no timing instrumentation. */
ExecutionOperationAdapterResult_T
EXECUTION_OPERATION_ADAPTER_ApplyOperationsProfiled( const uint8_t* operations,
                                                     uint8_t        operation_count );

/** Clears profiling timing while the execution timer is stopped. */
void EXECUTION_OPERATION_ADAPTER_ResetTiming( void );

/** Copies an atomic task-context snapshot. Returns false for invalid input. */
bool EXECUTION_OPERATION_ADAPTER_GetTiming( ExecutionOperationOpcode_T  opcode,
                                            ExecutionOperationTiming_T* timing );

/** Clears the retained adapter rejection diagnostic before a new run. */
void EXECUTION_OPERATION_ADAPTER_ResetFailure( void );

/** Returns true and copies the retained rejection diagnostic when one exists. */
bool EXECUTION_OPERATION_ADAPTER_GetFailure( ExecutionOperationAdapterFailure_T* failure );

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
 * @details
 * PWM Phase Policy:
 * The update writes ARR, CCR, and PSC preload registers for the selected timer.
 * These shadow values become active synchronously at the timer's next natural
 * update event (UEV / counter overflow).
 *
 * This design intentionally avoids forcing an immediate update event (TIM_EGR_UG)
 * or resetting the counter (CNT=0) across the TIM4 execution boundary, ensuring
 * that ongoing pulses are not truncated into runt pulses or signal glitches.
 * A 0% duty or disabled state during execution is represented deterministically
 * by setting CCR = 0 (constant inactive level) without stopping the timer clock.
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

/**
 * @brief Submits one prevalidated variable-size SPI packet batch.
 *
 * @pre channel is EXECUTION_OPERATION_SPI_CHANNEL_1 or
 *      EXECUTION_OPERATION_SPI_CHANNEL_2.
 * @pre payload points to an aligned SPI prefix followed by packet_count
 *      uint32_t sizes and the corresponding contiguous packet data.
 * @pre every packet size is valid for the configured SPI channel and all
 *      sizes exactly account for the remaining unpadded payload bytes.
 *
 * The SPI driver copies the complete batch before returning, so it does not
 * retain any pointer into Flash Manager instruction storage.
 *
 * @return EXECUTION_OPERATION_ADAPTER_ACCEPTED when the complete batch was
 *         queued and TX was triggered; otherwise
 *         EXECUTION_OPERATION_ADAPTER_REJECTED.
 */
ExecutionOperationAdapterResult_T
EXECUTION_OPERATION_ADAPTER_ApplySpiTransmit( uint8_t channel, const uint8_t* payload,
                                              uint16_t payload_length_bytes );

/**
 * @brief Queues one prevalidated UART payload into driver-owned DMA storage.
 *
 * @pre channel is EXECUTION_OPERATION_UART_CHANNEL_1 or
 *      EXECUTION_OPERATION_UART_CHANNEL_2.
 * @pre channel, payload, and payload_length_bytes satisfy the validated
 *      session contract, and the selected UART channel is ready for TX.
 *
 * The UART driver copies the complete payload before returning, so it does not
 * retain the Flash Manager storage pointer.
 *
 * @return EXECUTION_OPERATION_ADAPTER_ACCEPTED when the complete payload was
 *         queued and the DMA pump was accepted; otherwise
 *         EXECUTION_OPERATION_ADAPTER_REJECTED.
 */
ExecutionOperationAdapterResult_T
EXECUTION_OPERATION_ADAPTER_ApplyUartTransmit( uint8_t channel, const uint8_t* payload,
                                               uint16_t payload_length_bytes );

/**
 * @brief Queues prevalidated analogue-output DAC frames without copying in the adapter.
 *
 * @pre channel is EXECUTION_OPERATION_CHANNEL_UNUSED.
 * @pre payload points to one or more contiguous three-byte prepared DAC frames.
 * @pre payload_length_bytes is a non-zero multiple of three and has been
 *      admitted against the active analogue-output configuration and schedule.
 *
 * The analogue-output driver copies the frames into SPI-DAC DMA-owned storage.
 */
ExecutionOperationAdapterResult_T
EXECUTION_OPERATION_ADAPTER_ApplyAnalogueOutput( uint8_t channel, const uint8_t* payload,
                                                 uint16_t payload_length_bytes );

/**
 * @brief Transmits a prevalidated CAN packet batch.
 *
 * @pre channel is EXECUTION_OPERATION_CAN_CHANNEL_1 or
 *      EXECUTION_OPERATION_CAN_CHANNEL_2.
 * @pre payload points to aligned ExecutionCanPacket_T records whose layout
 *      matches EXEC_CAN_Packet_T.
 * @pre payload_length_bytes is a non-zero multiple of
 *      EXECUTION_CAN_PACKET_SIZE_BYTES.
 *
 * The current CAN driver performs its existing validation and driver-owned
 * copy. This adapter performs no additional runtime validation.
 */
ExecutionOperationAdapterResult_T
EXECUTION_OPERATION_ADAPTER_ApplyCanTransmit( uint8_t channel, const uint8_t* payload,
                                              uint16_t payload_length_bytes );

#ifdef __cplusplus
}
#endif

#endif /* EXECUTION_OPERATION_ADAPTERS_H */
