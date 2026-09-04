/******************************************************************************
 *  File:       execution_instruction_payloads.h
 *
 *  Description:
 *      Working sketch of the opcode and payload layouts for scheduled output
 *      instructions. This header supplements the design document by showing
 *      how the current decisions could be represented in C. It is expected to
 *      change as the design is reviewed and is not yet an implementation
 *      contract.
 *
 *  Notes:
 *      One instruction represents one execution-driver API call. The common
 *      instruction header owns timestamp, opcode, channel, and payload length;
 *      the layouts below describe only the call-specific payload.
 *
 *      The MCU Host Interface creates these payloads after validating the
 *      external test package against its paired driver configuration.
 *
 *      Design choices represented here:
 *      - Store an opcode rather than a function pointer.
 *      - Store one record per useful driver API call.
 *      - Keep timestamp, opcode, channel, and payload length in the common
 *        record header rather than repeating them in each payload.
 *      - Define every payload in this header so the Host Interface does not
 *        depend on execution-driver headers or native driver structures.
 *      - Exclude I2C until its known hardware fault is resolved and its
 *        execution path can be validated.
 *
 *      Before typed payload fields or arrays are read directly, the final
 *      record design must guarantee their alignment in every Flash Manager
 *      buffer location, including page and ring crossings. Adapters must
 *      translate these instruction-owned layouts into driver inputs without
 *      making the Host Interface depend on a driver header.
 ******************************************************************************/

#ifndef EXECUTION_INSTRUCTION_PAYLOADS_H
#define EXECUTION_INSTRUCTION_PAYLOADS_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdint.h>

/**-----------------------------------------------------------------------------
 *  Public Defines / Macros
 *------------------------------------------------------------------------------
 */

/** Opcode storage type used by the common instruction header. */
typedef uint8_t ExecutionInstructionOpcode_T;

/**
 * @brief Output operations currently supported by this draft.
 *
 * Explicit values make the draft mapping independent of the compiler's enum
 * size. Values must not be reused after the format is released. I2C operations
 * are intentionally absent while I2C is disabled by the current hardware
 * policy; new opcodes can be appended after that path is validated.
 */
#define EXECUTION_INSTRUCTION_OPCODE_DIGITAL_OUTPUT_SET ( ( ExecutionInstructionOpcode_T ) 0U )
#define EXECUTION_INSTRUCTION_OPCODE_DIGITAL_OUTPUT_RESET ( ( ExecutionInstructionOpcode_T ) 1U )
#define EXECUTION_INSTRUCTION_OPCODE_ANALOGUE_OUTPUT_BATCH ( ( ExecutionInstructionOpcode_T ) 2U )
#define EXECUTION_INSTRUCTION_OPCODE_PWM_UPDATE ( ( ExecutionInstructionOpcode_T ) 3U )
#define EXECUTION_INSTRUCTION_OPCODE_CAN_TRANSMIT ( ( ExecutionInstructionOpcode_T ) 4U )
#define EXECUTION_INSTRUCTION_OPCODE_SPI_TRANSMIT ( ( ExecutionInstructionOpcode_T ) 5U )
#define EXECUTION_INSTRUCTION_OPCODE_UART_TRANSMIT ( ( ExecutionInstructionOpcode_T ) 6U )
#define EXECUTION_INSTRUCTION_OPCODE_COUNT ( 7U )

/** Analogue output instruction limits. */
#define EXECUTION_ANALOGUE_OUTPUT_FRAME_SIZE_BYTES ( 3U )
#define EXECUTION_ANALOGUE_OUTPUT_MAX_FRAMES ( 6U )
#define EXECUTION_ANALOGUE_OUTPUT_MAX_DATA_BYTES                                           \
    ( EXECUTION_ANALOGUE_OUTPUT_FRAME_SIZE_BYTES * EXECUTION_ANALOGUE_OUTPUT_MAX_FRAMES )

/** CAN transmit instruction limits. */
#define EXECUTION_CAN_MAX_DATA_BYTES ( 8U )
#define EXECUTION_CAN_MAX_PACKETS ( 19U )

/**-----------------------------------------------------------------------------
 *  Public Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

/**
 * Input to EXEC_DIGITAL_OUTPUT_Set_Output() or Reset_Output().
 *
 * Separate opcodes select the two functions. The prepared physical mask is the
 * complete API input, so the common instruction channel is unused.
 */
typedef struct
{
    uint32_t pin_mask;
} ExecutionDigitalOutputPayload_T;

/**
 * @brief Prepared analogue output batch for one driver call.
 *
 * The MCU Host Interface creates the prepared three-byte frame data; the
 * external host does not supply DAC frames. byte_count is the valid prefix of
 * bytes and must be a multiple of EXECUTION_ANALOGUE_OUTPUT_FRAME_SIZE_BYTES.
 * The common instruction channel is unused.
 */
typedef struct
{
    uint8_t bytes[EXECUTION_ANALOGUE_OUTPUT_MAX_DATA_BYTES];
    uint8_t byte_count;
} ExecutionAnalogueOutputBatchPayload_T;

/**
 * Inputs to the selected LV or HV PWM update call.
 *
 * The common channel selects EXEC_PWM_GEN_Set_PWM_LV() or Set_PWM_HV(). These
 * three values are calculated before upload and are the complete API inputs.
 */
typedef struct
{
    uint16_t arr;
    uint16_t ccr;
    uint16_t psc;
} ExecutionPwmUpdatePayload_T;

/** One standard CAN packet in a CAN_TRANSMIT instruction payload. */
typedef struct
{
    uint16_t id;
    uint8_t  dlc;
    uint8_t  data[EXECUTION_CAN_MAX_DATA_BYTES];

    /** Explicitly stored as zero so the layout contains no compiler padding. */
    uint8_t reserved;
} ExecutionCanPacket_T;

/*
 * A CAN_TRANSMIT payload is a non-empty ExecutionCanPacket_T array. The packet
 * count is payload_length_bytes divided by sizeof( ExecutionCanPacket_T ) and
 * must not exceed EXECUTION_CAN_MAX_PACKETS. The common header supplies the
 * channel, so a separate payload prefix is unnecessary.
 */

/**
 * @brief Fixed start of an SPI payload followed by sizes and packet data.
 *
 * Payload layout:
 *   [ExecutionSpiTransmitPayloadPrefix_T]
 *   [uint32_t packet_sizes[packet_count]]
 *   [uint8_t data[sum(packet_sizes)]]
 *
 * The common header supplies channel. packet_count is passed as num_packets.
 * The data length is not stored separately because EXEC_SPI_Transmit() does
 * not take it; validation must prove that the packet sizes sum to the bytes
 * remaining in the payload. packet_sizes must begin at a uint32_t-aligned
 * address in the final design.
 *
 * A prefix is necessary here because packet_count tells the adapter where the
 * variable packet-size array ends and the packet data begins. Unlike CAN,
 * payload length alone cannot determine that boundary. The sizes and data are
 * part of the same payload record even though they cannot be members of this
 * fixed-size C structure: both regions have variable length, and C permits at
 * most one flexible array at the end of a structure.
 */
typedef struct
{
    uint32_t packet_count;
} ExecutionSpiTransmitPayloadPrefix_T;

/*
 * UART_TRANSMIT has no fixed payload structure. The complete payload is the
 * byte range passed to EXEC_UART_Transmit(). The common header supplies the
 * channel and payload length. Upload validation must limit the length to the
 * amount that the UART driver can accept in one call. A structure containing
 * only a variable byte array would not add any information to this format.
 */

/**-----------------------------------------------------------------------------
 *  Draft layout checks
 *------------------------------------------------------------------------------
 */

#if defined( __cplusplus )
static_assert( sizeof( ExecutionDigitalOutputPayload_T ) == 4U,
               "Digital output payload layout changed" );
static_assert( sizeof( ExecutionPwmUpdatePayload_T ) == 6U, "PWM payload layout changed" );
static_assert( sizeof( ExecutionSpiTransmitPayloadPrefix_T ) == 4U,
               "SPI transmit prefix layout changed" );
static_assert( sizeof( ExecutionAnalogueOutputBatchPayload_T ) == 19U,
               "Analogue output instruction payload layout changed" );
static_assert( sizeof( ExecutionCanPacket_T ) == 12U,
               "CAN instruction packet layout changed" );
#else
_Static_assert( sizeof( ExecutionDigitalOutputPayload_T ) == 4U,
                "Digital output payload layout changed" );
_Static_assert( sizeof( ExecutionPwmUpdatePayload_T ) == 6U, "PWM payload layout changed" );
_Static_assert( sizeof( ExecutionSpiTransmitPayloadPrefix_T ) == 4U,
                "SPI transmit prefix layout changed" );
_Static_assert( sizeof( ExecutionAnalogueOutputBatchPayload_T ) == 19U,
                "Analogue output instruction payload layout changed" );
_Static_assert( sizeof( ExecutionCanPacket_T ) == 12U,
                "CAN instruction packet layout changed" );
#endif

#ifdef __cplusplus
}
#endif

#endif /* EXECUTION_INSTRUCTION_PAYLOADS_H */
