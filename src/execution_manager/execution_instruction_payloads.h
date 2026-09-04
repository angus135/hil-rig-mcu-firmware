/******************************************************************************
 *  File:       execution_instruction_payloads.h
 *
 *  Description:
 *      Working sketch of the opcode and payload layouts for scheduled output
 *      instructions. This header supplements the design document by showing
 *      how the current decisions could be represented in C. It is expected to
 *      change as the design is reviewed and is not yet a released binary
 *      interface. Within this draft, however, the byte layouts below are the
 *      authoritative packing rules for the MCU Host Interface.
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
 *      The structures illustrate fixed layouts; they are not permission to
 *      memcpy an external or compiler-native structure into flash. The Host
 *      Interface must emit the stated bytes and validate every stated rule.
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

/*
 * Multi-byte payload fields are stored least-significant byte first. The Host
 * Interface must write that byte order explicitly rather than copying an
 * arbitrary external or compiler-native structure into the record.
 *
 * For every instruction, the Host Interface must:
 *   1. write the scheduled Execution Manager tick to the common timestamp;
 *   2. select exactly one opcode below;
 *   3. write the opcode's channel value, or CHANNEL_UNUSED;
 *   4. pack the opcode-specific payload exactly as documented below; and
 *   5. set payload_length_bytes to those payload bytes only. The common header
 *      itself is not included in payload_length_bytes.
 */

/** Channel values stored in the common instruction header. */
#define EXECUTION_INSTRUCTION_CHANNEL_UNUSED ( 0U )
#define EXECUTION_INSTRUCTION_PWM_CHANNEL_LV ( 0U )
#define EXECUTION_INSTRUCTION_PWM_CHANNEL_HV ( 1U )
#define EXECUTION_INSTRUCTION_CAN_CHANNEL_1 ( 0U )
#define EXECUTION_INSTRUCTION_CAN_CHANNEL_2 ( 1U )
#define EXECUTION_INSTRUCTION_SPI_CHANNEL_1 ( 0U )
#define EXECUTION_INSTRUCTION_SPI_CHANNEL_2 ( 1U )
#define EXECUTION_INSTRUCTION_UART_CHANNEL_1 ( 0U )
#define EXECUTION_INSTRUCTION_UART_CHANNEL_2 ( 1U )

/** Analogue output instruction limits. */
#define EXECUTION_ANALOGUE_OUTPUT_FRAME_SIZE_BYTES ( 3U )
#define EXECUTION_ANALOGUE_OUTPUT_MAX_FRAMES ( 6U )
#define EXECUTION_ANALOGUE_OUTPUT_MAX_DATA_BYTES                                           \
    ( EXECUTION_ANALOGUE_OUTPUT_FRAME_SIZE_BYTES * EXECUTION_ANALOGUE_OUTPUT_MAX_FRAMES )

/** CAN transmit instruction limits. */
#define EXECUTION_CAN_MAX_DATA_BYTES ( 8U )
#define EXECUTION_CAN_MAX_PACKETS ( 19U )
#define EXECUTION_CAN_PACKET_SIZE_BYTES ( 12U )

/** Fixed payload lengths. */
#define EXECUTION_DIGITAL_OUTPUT_PAYLOAD_SIZE_BYTES ( 4U )
#define EXECUTION_ANALOGUE_OUTPUT_PAYLOAD_SIZE_BYTES ( 19U )
#define EXECUTION_PWM_UPDATE_PAYLOAD_SIZE_BYTES ( 6U )

/** Fixed-payload byte offsets. */
#define EXECUTION_DIGITAL_OUTPUT_PIN_MASK_OFFSET_BYTES ( 0U )
#define EXECUTION_ANALOGUE_OUTPUT_FRAMES_OFFSET_BYTES ( 0U )
#define EXECUTION_ANALOGUE_OUTPUT_BYTE_COUNT_OFFSET_BYTES ( 18U )
#define EXECUTION_PWM_ARR_OFFSET_BYTES ( 0U )
#define EXECUTION_PWM_CCR_OFFSET_BYTES ( 2U )
#define EXECUTION_PWM_PSC_OFFSET_BYTES ( 4U )

/** Fields within each packed CAN packet. */
#define EXECUTION_CAN_PACKET_ID_OFFSET_BYTES ( 0U )
#define EXECUTION_CAN_PACKET_DLC_OFFSET_BYTES ( 2U )
#define EXECUTION_CAN_PACKET_DATA_OFFSET_BYTES ( 3U )
#define EXECUTION_CAN_PACKET_RESERVED_OFFSET_BYTES ( 11U )

/** SPI variable-payload offsets and length calculation. */
#define EXECUTION_SPI_PACKET_COUNT_OFFSET_BYTES ( 0U )
#define EXECUTION_SPI_PACKET_SIZES_OFFSET_BYTES ( 4U )
#define EXECUTION_SPI_PACKET_SIZES_LENGTH_BYTES( packet_count ) ( 4U * ( packet_count ) )
#define EXECUTION_SPI_DATA_OFFSET_BYTES( packet_count )                                      \
    ( EXECUTION_SPI_PACKET_SIZES_OFFSET_BYTES                                                \
      + EXECUTION_SPI_PACKET_SIZES_LENGTH_BYTES( packet_count ) )

/**-----------------------------------------------------------------------------
 *  Public Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

/**
 * Input to EXEC_DIGITAL_OUTPUT_Set_Output() or Reset_Output().
 *
 * Separate opcodes select the two functions. The prepared physical mask is the
 * complete API input, so channel must be EXECUTION_INSTRUCTION_CHANNEL_UNUSED.
 *
 * Host Interface packing:
 *   payload length = 4
 *   bytes 0..3     = pin_mask, little-endian
 *
 * The mask is the configured physical GPIO mask expected by the execution
 * driver, not an external-host channel number.
 */
typedef struct
{
    uint32_t pin_mask;
} ExecutionDigitalOutputPayload_T;

/**
 * @brief Prepared analogue output batch for one driver call.
 *
 * The MCU Host Interface creates the prepared three-byte frame data from the
 * requested output channel and voltage; the external host does not supply DAC
 * wire frames. byte_count is the valid prefix of bytes and must be a multiple
 * of EXECUTION_ANALOGUE_OUTPUT_FRAME_SIZE_BYTES. The common instruction
 * channel must be EXECUTION_INSTRUCTION_CHANNEL_UNUSED.
 *
 * Host Interface packing:
 *   payload length = 19
 *   bytes 0..17    = up to six consecutive three-byte prepared frames
 *   byte 18        = byte_count: 0, 3, 6, 9, 12, 15, or 18
 *
 * For each requested output, in execution order:
 *   frame byte 0 = ( analogue channel & 0x1f ) << 3; channel is 0..5
 *   frame byte 1 = upper 8 bits of the 12-bit DAC count
 *   frame byte 2 = lower 8 bits of the 12-bit DAC count
 *
 * The current conversion clamps voltage to 0..20 V and calculates:
 *   dac_count = round( voltage / 20 V * 4095 )
 *
 * Notice that the two count bytes are in DAC wire order (most-significant
 * first), not the little-endian order used for integer fields elsewhere.
 * bytes[byte_count..17] must be zero.
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
 *
 * Host Interface packing:
 *   payload length = 6
 *   bytes 0..1     = arr, little-endian
 *   bytes 2..3     = ccr, little-endian
 *   bytes 4..5     = psc, little-endian
 */
typedef struct
{
    uint16_t arr;
    uint16_t ccr;
    uint16_t psc;
} ExecutionPwmUpdatePayload_T;

/**
 * @brief One 12-byte standard CAN packet in a CAN_TRANSMIT payload.
 *
 * Host Interface packing for every packet:
 *   bytes 0..1  = 11-bit standard identifier, little-endian
 *   byte 2      = dlc in the range 0..8
 *   bytes 3..10 = data bytes; bytes beyond dlc must be zero
 *   byte 11     = reserved and must be zero
 *
 * The identifier must not contain CAN flag bits. Only standard 11-bit CAN
 * identifiers are accepted by this instruction.
 */
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
 *
 * Host Interface packing:
 *   channel        = EXECUTION_INSTRUCTION_CAN_CHANNEL_1 or CHANNEL_2
 *   packet_count   = number of requested packets, 1..EXECUTION_CAN_MAX_PACKETS
 *   payload length = packet_count * EXECUTION_CAN_PACKET_SIZE_BYTES
 *   payload        = packet_count consecutive ExecutionCanPacket_T layouts
 *
 * payload length must be a non-zero exact multiple of the packet size. There
 * is no packet-count field in the payload: the Execution Manager derives it
 * from payload length after validation.
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
 *
 * Host Interface packing:
 *   channel         = EXECUTION_INSTRUCTION_SPI_CHANNEL_1 or CHANNEL_2
 *   bytes 0..3      = packet_count, little-endian and greater than zero
 *   next 4*n bytes  = n packet sizes, each little-endian and greater than zero
 *   remaining bytes = packet data concatenated in packet order; exactly
 *                     packet_sizes[0] bytes for packet 0, then
 *                     packet_sizes[1] bytes for packet 1, and so on
 *   payload length  = 4 + ( 4 * packet_count ) + sum( packet_sizes )
 *
 * The packet sizes must account for every data byte exactly. Validation must
 * also enforce the configured SPI frame width, driver queue depth, TX buffer
 * capacity, and the Flash Manager maximum record length.
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
 *
 * Host Interface packing:
 *   channel        = EXECUTION_INSTRUCTION_UART_CHANNEL_1 or CHANNEL_2
 *   payload length = number of transmit bytes; greater than zero and no more
 *                    than the validated single-call UART capacity
 *   payload        = transmit bytes in wire order
 *
 * Unlike SPI, UART has no packet boundaries or packet-size array. Unlike CAN,
 * it has no per-packet metadata. Consequently every payload byte is user data
 * and a fixed prefix or wrapper structure would contain no useful field.
 */

/**-----------------------------------------------------------------------------
 *  Draft layout checks
 *------------------------------------------------------------------------------
 */

#if defined( __cplusplus )
static_assert( sizeof( ExecutionDigitalOutputPayload_T )
                   == EXECUTION_DIGITAL_OUTPUT_PAYLOAD_SIZE_BYTES,
               "Digital output payload layout changed" );
static_assert( sizeof( ExecutionPwmUpdatePayload_T ) == EXECUTION_PWM_UPDATE_PAYLOAD_SIZE_BYTES,
               "PWM payload layout changed" );
static_assert( sizeof( ExecutionSpiTransmitPayloadPrefix_T ) == 4U,
               "SPI transmit prefix layout changed" );
static_assert( sizeof( ExecutionAnalogueOutputBatchPayload_T )
                   == EXECUTION_ANALOGUE_OUTPUT_PAYLOAD_SIZE_BYTES,
               "Analogue output instruction payload layout changed" );
static_assert( sizeof( ExecutionCanPacket_T ) == EXECUTION_CAN_PACKET_SIZE_BYTES,
               "CAN instruction packet layout changed" );
#else
_Static_assert( sizeof( ExecutionDigitalOutputPayload_T )
                    == EXECUTION_DIGITAL_OUTPUT_PAYLOAD_SIZE_BYTES,
                "Digital output payload layout changed" );
_Static_assert( sizeof( ExecutionPwmUpdatePayload_T ) == EXECUTION_PWM_UPDATE_PAYLOAD_SIZE_BYTES,
                "PWM payload layout changed" );
_Static_assert( sizeof( ExecutionSpiTransmitPayloadPrefix_T ) == 4U,
                "SPI transmit prefix layout changed" );
_Static_assert( sizeof( ExecutionAnalogueOutputBatchPayload_T )
                    == EXECUTION_ANALOGUE_OUTPUT_PAYLOAD_SIZE_BYTES,
                "Analogue output instruction payload layout changed" );
_Static_assert( sizeof( ExecutionCanPacket_T ) == EXECUTION_CAN_PACKET_SIZE_BYTES,
                "CAN instruction packet layout changed" );
#endif

#ifdef __cplusplus
}
#endif

#endif /* EXECUTION_INSTRUCTION_PAYLOADS_H */
