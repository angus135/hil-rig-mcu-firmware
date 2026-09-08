/******************************************************************************
 *  File:       execution_operation_payloads.h
 *
 *  Description:
 *      Canonical opcode and payload layouts for scheduled output operations.
 *      The common operation word, digital-output payload, PWM-update payload,
 *      SPI-transmit payload, and raw UART-transmit payload are current
 *      implementation contracts. The remaining peripheral payloads document
 *      planned encodings and may change as their adapters are implemented.
 *
 *  Notes:
 *      One instruction represents all output work scheduled for one tick. It
 *      contains multiple operations. Each operation represents one logical
 *      peripheral update and may require more than one existing driver call.
 *      The layouts below describe the operation-specific payloads. The common
 *      operation header occupies one little-endian 32-bit word.
 *      Every operation begins on a four-byte boundary, and trailing zero
 *      padding extends each complete operation to a four-byte boundary.
 *
 *      The MCU Host Interface creates these payloads after validating the
 *      external test package against its paired driver configuration.
 *
 *      Design choices represented here:
 *      - Store an opcode rather than a function pointer.
 *      - Store one operation per logical peripheral update.
 *      - Keep the timestamp in the enclosing instruction header.
 *      - Keep opcode, channel, and payload length outside the call-specific
 *        payload rather than repeating them in each layout.
 *      - Define every payload in this header so the Host Interface does not
 *        depend on execution-driver headers or native driver structures.
 *      - Exclude I2C until its known hardware fault is resolved and its
 *        execution path can be validated.
 *
 *      Type declarations describe field widths and encoded storage only; they
 *      are not permission to serialize an external or compiler-native structure.
 *      The Host Interface must emit the stated little-endian words and bytes and
 *      validate every stated rule. Flash Manager word-backed storage preserves
 *      four-byte alignment across pages and ring mirrors. Adapters read aligned
 *      words directly and must not retain instruction-storage pointers.
 ******************************************************************************/

#ifndef EXECUTION_OPERATION_PAYLOADS_H
#define EXECUTION_OPERATION_PAYLOADS_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <stddef.h>
#include <stdint.h>

/**-----------------------------------------------------------------------------
 *  Public Defines / Macros
 *------------------------------------------------------------------------------
 */

/** Opcode storage type used by the common operation encoding. */
typedef uint8_t ExecutionOperationOpcode_T;

/** Every encoded operation begins on a four-byte boundary. */
#define EXECUTION_OPERATION_ALIGNMENT_BYTES ( 4U )

/** Fixed encoded size of the common operation header. */
#define EXECUTION_OPERATION_HEADER_SIZE_BYTES ( 4U )

/** Bit layout of the encoded little-endian operation-header word. */
#define EXECUTION_OPERATION_OPCODE_SHIFT ( 0U )
#define EXECUTION_OPERATION_CHANNEL_SHIFT ( 8U )
#define EXECUTION_OPERATION_PAYLOAD_LENGTH_SHIFT ( 16U )

#define EXECUTION_OPERATION_OPCODE_MASK UINT32_C( 0x000000FF )
#define EXECUTION_OPERATION_CHANNEL_MASK UINT32_C( 0x0000FF00 )
#define EXECUTION_OPERATION_PAYLOAD_LENGTH_MASK UINT32_C( 0xFFFF0000 )

/** Decode fields from one aligned operation-header word. */
#define EXECUTION_OPERATION_GET_OPCODE( header_word )                                              \
    ( ( ExecutionOperationOpcode_T )( ( ( header_word ) & EXECUTION_OPERATION_OPCODE_MASK )        \
                                      >> EXECUTION_OPERATION_OPCODE_SHIFT ) )

#define EXECUTION_OPERATION_GET_CHANNEL( header_word )                                             \
    ( ( uint8_t )( ( ( header_word ) & EXECUTION_OPERATION_CHANNEL_MASK )                          \
                   >> EXECUTION_OPERATION_CHANNEL_SHIFT ) )

#define EXECUTION_OPERATION_GET_PAYLOAD_LENGTH_BYTES( header_word )                                \
    ( ( uint16_t )( ( ( header_word ) & EXECUTION_OPERATION_PAYLOAD_LENGTH_MASK )                  \
                    >> EXECUTION_OPERATION_PAYLOAD_LENGTH_SHIFT ) )

/**
 * @brief Calculate the complete encoded operation size, including padding.
 *
 * payload_length_bytes excludes the common header and trailing padding.
 */
#define EXECUTION_OPERATION_ENCODED_SIZE_BYTES( payload_length_bytes )                             \
    ( ( EXECUTION_OPERATION_HEADER_SIZE_BYTES + ( uint32_t )( payload_length_bytes )               \
        + ( EXECUTION_OPERATION_ALIGNMENT_BYTES - 1U ) )                                           \
      & ~( EXECUTION_OPERATION_ALIGNMENT_BYTES - 1U ) )

/**
 * @brief Opcode values reserved by the current instruction format.
 *
 * Explicit values make the mapping independent of the compiler's enum size.
 * Values must not be reused. I2C operations are intentionally absent while I2C
 * is disabled by current hardware policy; new opcodes are appended.
 */
#define EXECUTION_OPERATION_OPCODE_DIGITAL_OUTPUT_UPDATE ( ( ExecutionOperationOpcode_T )0U )
#define EXECUTION_OPERATION_OPCODE_ANALOGUE_OUTPUT_BATCH ( ( ExecutionOperationOpcode_T )1U )
#define EXECUTION_OPERATION_OPCODE_PWM_UPDATE ( ( ExecutionOperationOpcode_T )2U )
#define EXECUTION_OPERATION_OPCODE_CAN_TRANSMIT ( ( ExecutionOperationOpcode_T )3U )
#define EXECUTION_OPERATION_OPCODE_SPI_TRANSMIT ( ( ExecutionOperationOpcode_T )4U )
#define EXECUTION_OPERATION_OPCODE_UART_TRANSMIT ( ( ExecutionOperationOpcode_T )5U )
#define EXECUTION_OPERATION_OPCODE_COUNT ( 6U )

/*
 * Multi-byte payload fields are stored least-significant byte first. The Host
 * Interface must write that byte order explicitly rather than copying an
 * arbitrary external or compiler-native structure into an operation.
 *
 * For every operation, the Host Interface must select one opcode and channel,
 * then pack its operation-specific payload exactly as documented below. The
 * enclosing instruction supplies the scheduled Execution Manager tick.
 */

/** Channel values stored outside the operation-specific payload. */
#define EXECUTION_OPERATION_CHANNEL_UNUSED ( 0U )
#define EXECUTION_OPERATION_PWM_CHANNEL_LV ( 0U )
#define EXECUTION_OPERATION_PWM_CHANNEL_HV ( 1U )
#define EXECUTION_OPERATION_CAN_CHANNEL_1 ( 0U )
#define EXECUTION_OPERATION_CAN_CHANNEL_2 ( 1U )
#define EXECUTION_OPERATION_SPI_CHANNEL_1 ( 0U )
#define EXECUTION_OPERATION_SPI_CHANNEL_2 ( 1U )
#define EXECUTION_OPERATION_UART_CHANNEL_1 ( 0U )
#define EXECUTION_OPERATION_UART_CHANNEL_2 ( 1U )

/** Analogue output instruction limits. */
#define EXECUTION_ANALOGUE_OUTPUT_FRAME_SIZE_BYTES ( 3U )
#define EXECUTION_ANALOGUE_OUTPUT_MAX_FRAMES ( 6U )
#define EXECUTION_ANALOGUE_OUTPUT_MAX_DATA_BYTES                                                   \
    ( EXECUTION_ANALOGUE_OUTPUT_FRAME_SIZE_BYTES * EXECUTION_ANALOGUE_OUTPUT_MAX_FRAMES )

/** CAN transmit instruction limits. */
#define EXECUTION_CAN_MAX_DATA_BYTES ( 8U )
#define EXECUTION_CAN_PACKET_SIZE_BYTES ( 12U )

/** SPI transmit payload field widths. */
#define EXECUTION_SPI_PREFIX_SIZE_BYTES ( 4U )
#define EXECUTION_SPI_PACKET_SIZE_FIELD_BYTES ( 4U )

/** Fixed payload lengths. */
#define EXECUTION_DIGITAL_OUTPUT_PAYLOAD_SIZE_BYTES ( 8U )
#define EXECUTION_PWM_UPDATE_PAYLOAD_SIZE_BYTES ( 6U )

/** Fields within each packed CAN packet. */
#define EXECUTION_CAN_PACKET_ID_OFFSET_BYTES ( 0U )
#define EXECUTION_CAN_PACKET_DLC_OFFSET_BYTES ( 2U )
#define EXECUTION_CAN_PACKET_DATA_OFFSET_BYTES ( 3U )
#define EXECUTION_CAN_PACKET_RESERVED_OFFSET_BYTES ( 11U )

/** SPI variable-payload offsets and length calculation. */
#define EXECUTION_SPI_PACKET_COUNT_OFFSET_BYTES ( 0U )
#define EXECUTION_SPI_PACKET_SIZES_OFFSET_BYTES ( EXECUTION_SPI_PREFIX_SIZE_BYTES )

#define EXECUTION_SPI_PACKET_SIZES_LENGTH_BYTES( packet_count )                                    \
    ( EXECUTION_SPI_PACKET_SIZE_FIELD_BYTES * ( uint32_t )( packet_count ) )

#define EXECUTION_SPI_DATA_OFFSET_BYTES( packet_count )                                            \
    ( EXECUTION_SPI_PACKET_SIZES_OFFSET_BYTES                                                      \
      + EXECUTION_SPI_PACKET_SIZES_LENGTH_BYTES( packet_count ) )

/**-----------------------------------------------------------------------------
 *  Public Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

/**
 * @brief Encoded common header word preceding every operation-specific payload.
 *
 * Encoded layout:
 *   byte 0      = opcode
 *   byte 1      = channel
 *   bytes 2..3  = payload_length_bytes, little-endian
 *
 * One aligned load produces a word with opcode in bits 0..7, channel in bits
 * 8..15, and payload_length_bytes in bits 16..31. payload_length_bytes excludes
 * this header and trailing alignment padding. The payload begins immediately
 * after this word. Zero to three zero-valued padding bytes follow the payload.
 */
typedef uint32_t ExecutionOperationHeaderWord_T;

/**
 * @brief Complete digital-output update for one execution tick.
 *
 * The masks describe the requested logical output levels. The adapter accounts
 * for the active-low hardware when applying them. channel must be
 * EXECUTION_OPERATION_CHANNEL_UNUSED.
 *
 * Host Interface packing:
 *   payload length = 8
 *   bytes 0..3     = high_bitmask, little-endian
 *   bytes 4..7     = low_bitmask, little-endian
 *
 * The masks contain prepared physical GPIO bits, not external-host channel
 * numbers. They must be disjoint, their union must be non-zero, and their union
 * must be a subset of the enabled digital-output mask for the paired session
 * configuration.
 *
 * The Host Interface combines all digital-output changes for one tick into at
 * most one operation. It omits the operation when no digital output changes on
 * that tick.
 */
typedef struct
{
    uint32_t high_bitmask;
    uint32_t low_bitmask;
} ExecutionDigitalOutputPayload_T;

/**
 * @brief Exact three-byte wire representation of one analogue-output update.
 *
 * An ANALOGUE_OUTPUT_BATCH payload is a non-empty sequence of these frames.
 * The operation payload length determines the frame count, so no separate
 * byte-count field is stored.
 *
 * Host Interface packing for every frame:
 *   byte 0 = ( analogue channel & 0x1f ) << 3; channel is 0..5
 *   byte 1 = upper eight bits of the 12-bit DAC count
 *   byte 2 = lower eight bits of the 12-bit DAC count
 *
 * The Host Interface combines all analogue-output changes for one tick into at
 * most one operation and preserves their required transmission order.
 *
 * Operation validation:
 *   channel        = EXECUTION_OPERATION_CHANNEL_UNUSED
 *   payload length = 3, 6, 9, 12, 15, or 18 bytes
 *   frame count    = payload length / EXECUTION_ANALOGUE_OUTPUT_FRAME_SIZE_BYTES
 */
typedef struct
{
    uint8_t bytes[EXECUTION_ANALOGUE_OUTPUT_FRAME_SIZE_BYTES];
} ExecutionAnalogueOutputFrame_T;

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
 * count is payload_length_bytes divided by sizeof( ExecutionCanPacket_T ). The
 * common header supplies the channel, so a separate payload prefix is
 * unnecessary. Driver-dependent limits are validated against the active
 * configuration rather than defined as instruction-format constants.
 *
 * Host Interface packing:
 *   channel        = EXECUTION_OPERATION_CAN_CHANNEL_1 or CHANNEL_2
 *   packet_count   = number of requested packets, greater than zero
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
 * remaining in the payload. packet_sizes begins at a uint32_t-aligned address
 * because both the common header and SPI prefix occupy complete words.
 *
 * A prefix is necessary here because packet_count tells the adapter where the
 * variable packet-size array ends and the packet data begins. Unlike CAN,
 * payload length alone cannot determine that boundary. The sizes and data are
 * part of the same operation payload even though they cannot be members of this
 * fixed-size C structure: both regions have variable length, and C permits at
 * most one flexible array at the end of a structure.
 *
 * Host Interface packing:
 *   channel         = EXECUTION_OPERATION_SPI_CHANNEL_1 or CHANNEL_2
 *   bytes 0..3      = packet_count, little-endian and greater than zero
 *   next 4*n bytes  = n packet sizes, each little-endian and greater than zero
 *   remaining bytes = packet data concatenated in packet order; exactly
 *                     packet_sizes[0] bytes for packet 0, then
 *                     packet_sizes[1] bytes for packet 1, and so on
 *   payload length  = 4 + ( 4 * packet_count ) + sum( packet_sizes )
 *
 * The packet sizes must account for every data byte exactly. Validation must
 * also enforce the configured SPI frame width, driver queue depth, TX buffer
 * capacity, and the Execution Manager maximum instruction length.
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
 *   channel        = EXECUTION_OPERATION_UART_CHANNEL_1 or CHANNEL_2
 *   payload length = number of transmit bytes; greater than zero and no more
 *                    than the validated single-call UART capacity
 *   payload        = transmit bytes in wire order
 *
 * Unlike SPI, UART has no packet boundaries or packet-size array. Unlike CAN,
 * it has no per-packet metadata. Consequently every payload byte is user data
 * and a fixed prefix or wrapper structure would contain no useful field.
 */

/**-----------------------------------------------------------------------------
 *  Layout checks
 *------------------------------------------------------------------------------
 */

#if defined( __cplusplus )
static_assert( sizeof( ExecutionOperationHeaderWord_T ) == EXECUTION_OPERATION_HEADER_SIZE_BYTES,
               "Execution operation header must occupy one word" );
static_assert( EXECUTION_OPERATION_ALIGNMENT_BYTES == sizeof( ExecutionOperationHeaderWord_T ),
               "Operation alignment must match the header word" );
static_assert( sizeof( ExecutionDigitalOutputPayload_T )
                   == EXECUTION_DIGITAL_OUTPUT_PAYLOAD_SIZE_BYTES,
               "Digital output payload layout changed" );
static_assert( offsetof( ExecutionDigitalOutputPayload_T, high_bitmask ) == 0U,
               "Digital output HIGH-mask offset changed" );
static_assert( offsetof( ExecutionDigitalOutputPayload_T, low_bitmask ) == 4U,
               "Digital output LOW-mask offset changed" );
static_assert( sizeof( ExecutionPwmUpdatePayload_T ) == EXECUTION_PWM_UPDATE_PAYLOAD_SIZE_BYTES,
               "PWM payload layout changed" );
static_assert( offsetof( ExecutionPwmUpdatePayload_T, arr ) == 0U, "PWM ARR offset changed" );
static_assert( offsetof( ExecutionPwmUpdatePayload_T, ccr ) == 2U, "PWM CCR offset changed" );
static_assert( offsetof( ExecutionPwmUpdatePayload_T, psc ) == 4U, "PWM PSC offset changed" );
static_assert( sizeof( ExecutionSpiTransmitPayloadPrefix_T ) == 4U,
               "SPI transmit prefix layout changed" );
static_assert( sizeof( ExecutionAnalogueOutputFrame_T )
                   == EXECUTION_ANALOGUE_OUTPUT_FRAME_SIZE_BYTES,
               "Analogue output frame layout changed" );
static_assert( sizeof( ExecutionCanPacket_T ) == EXECUTION_CAN_PACKET_SIZE_BYTES,
               "CAN instruction packet layout changed" );
#else
_Static_assert( sizeof( ExecutionOperationHeaderWord_T ) == EXECUTION_OPERATION_HEADER_SIZE_BYTES,
                "Execution operation header must occupy one word" );
_Static_assert( EXECUTION_OPERATION_ALIGNMENT_BYTES == sizeof( ExecutionOperationHeaderWord_T ),
                "Operation alignment must match the header word" );
_Static_assert( sizeof( ExecutionDigitalOutputPayload_T )
                    == EXECUTION_DIGITAL_OUTPUT_PAYLOAD_SIZE_BYTES,
                "Digital output payload layout changed" );
_Static_assert( offsetof( ExecutionDigitalOutputPayload_T, high_bitmask ) == 0U,
                "Digital output HIGH-mask offset changed" );
_Static_assert( offsetof( ExecutionDigitalOutputPayload_T, low_bitmask ) == 4U,
                "Digital output LOW-mask offset changed" );
_Static_assert( sizeof( ExecutionPwmUpdatePayload_T ) == EXECUTION_PWM_UPDATE_PAYLOAD_SIZE_BYTES,
                "PWM payload layout changed" );
_Static_assert( offsetof( ExecutionPwmUpdatePayload_T, arr ) == 0U, "PWM ARR offset changed" );
_Static_assert( offsetof( ExecutionPwmUpdatePayload_T, ccr ) == 2U, "PWM CCR offset changed" );
_Static_assert( offsetof( ExecutionPwmUpdatePayload_T, psc ) == 4U, "PWM PSC offset changed" );
_Static_assert( sizeof( ExecutionSpiTransmitPayloadPrefix_T ) == 4U,
                "SPI transmit prefix layout changed" );
_Static_assert( sizeof( ExecutionAnalogueOutputFrame_T )
                    == EXECUTION_ANALOGUE_OUTPUT_FRAME_SIZE_BYTES,
                "Analogue output frame layout changed" );
_Static_assert( sizeof( ExecutionCanPacket_T ) == EXECUTION_CAN_PACKET_SIZE_BYTES,
                "CAN instruction packet layout changed" );
#endif

#ifdef __cplusplus
}
#endif

#endif /* EXECUTION_OPERATION_PAYLOADS_H */
