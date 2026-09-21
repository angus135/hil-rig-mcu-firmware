/******************************************************************************
 *  File:       instruction_message_handler.c
 *  Author:     Callum Rafferty
 *  Created:    15-Sep-2026
 *
 *  Description:
 *      Implementation of incoming application instruction message handling,
 *      canonical Execution Manager instruction conversion, peripheral state
 *      delta filtering, and Flash Manager upload submission.
 *
 *  Notes:
 *      Uses state delta tracking to convert application protocol full-state
 *      snapshots into sparse execution updates. When the protocol is later
 *      updated to send delta updates directly, this tracking layer can be
 *      cleanly removed without modifying the underlying encoders or Flash
 *      Manager upload integration.
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include "instruction_message_handler.h"
#include "exec_analogue_output.h"
#include "exec_digital_output.h"
#include "execution_manager/execution_instruction.h"
#include "execution_manager/execution_operation_payloads.h"
#include "flash_manager/flash_manager.h"
#include "hw_pwm_gen.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

/**
 * @brief Cursor context for sequential, 4-byte aligned operation appending.
 *
 * Encapsulates the output destination buffer, total capacity, current byte offset,
 * and the cumulative count of encoded operations within the current instruction.
 */
typedef struct
{
    /** Pointer to the start of the output buffer. */
    uint8_t* buffer;

    /** Total capacity of buffer in bytes. */
    size_t capacity;

    /** Current write offset in bytes, aligned to a 4-byte boundary. */
    size_t offset;

    /** Number of complete operations appended to this instruction. */
    uint8_t operation_count;
} HostInstructionWriter_T;

/**
 * @brief State tracker for converting protocol full-state messages into delta updates.
 *
 * @note This state tracker is a transitional layer while the application protocol
 *       transmits full state snapshots on every tick. When the protocol is updated
 *       to transmit sparse delta updates directly, this tracking layer can be removed.
 */
typedef struct
{
    /** Last observed digital output states for channels 0..9. */
    uint8_t digital_outputs[HIL_APPLICATION_DIGITAL_OUTPUT_CHANNEL_COUNT];

    /** Last observed analogue output microvolts for channels 0..5. */
    uint32_t analog_outputs[HIL_APPLICATION_ANALOG_OUTPUT_CHANNEL_COUNT];

    /** Last observed PWM period and duty for LV and HV channels. */
    HIL_Application_Pwm_Output_Value_T pwm_outputs[HIL_APPLICATION_PWM_OUTPUT_CHANNEL_COUNT];

    /** Set to true after HOST_INSTRUCTION_HANDLER_Reset() establishes baseline conditions. */
    bool initialized;
} HostInstructionStateTracker_T;

/**-----------------------------------------------------------------------------
 *  Public (global) and Extern Variables
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Private (static) Variables
 *------------------------------------------------------------------------------
 */

/** @brief Monotonically increasing execution timestamp of the last processed instruction. */
static uint32_t last_instruction_timestamp = 0U;

/** @brief Indicates whether at least one instruction has been processed since reset. */
static bool has_received_instruction = false;

/** @brief Retained peripheral state for detecting genuine output transitions. */
static HostInstructionStateTracker_T tracked_peripheral_state = { 0 };

/**-----------------------------------------------------------------------------
 *  Private (static) Function Prototypes
 *------------------------------------------------------------------------------
 */

/**
 * @brief Validates an incoming application instruction message.
 *
 * @param[in] instruction Pointer to the incoming application test instruction.
 *
 * @return HOST_INTERFACE_STATUS_OK if valid, or corresponding error status.
 */
static HOST_Interface_Status_T HOST_INSTRUCTION_HANDLER_ValidateInstruction(
    const HIL_Application_Test_Instruction_T* instruction );

/**
 * @brief Appends one operation to the instruction buffer, maintaining 4-byte alignment.
 *
 * @param[in,out] writer              Cursor context tracking buffer position and operation count.
 * @param[in]     opcode              Execution operation opcode.
 * @param[in]     channel             Peripheral channel identifier or UNUSED sentinel.
 * @param[in]     payload             Pointer to operation payload data (may be NULL if
 * payload_size_bytes is 0).
 * @param[in]     payload_size_bytes  Exact byte size of the payload (excluding header and padding).
 *
 * @return HOST_INTERFACE_STATUS_OK on success, or HOST_INTERFACE_STATUS_BUFFER_TOO_SMALL.
 */
static HOST_Interface_Status_T
HOST_INSTRUCTION_HANDLER_AppendOperation( HostInstructionWriter_T*   writer,
                                          ExecutionOperationOpcode_T opcode, uint8_t channel,
                                          const void* payload, uint16_t payload_size_bytes );

/**
 * @brief Detects digital output transitions and encodes a digital output operation.
 *
 * @param[in]     instruction Pointer to the incoming application test instruction.
 * @param[in,out] writer      Cursor context for appending the operation.
 *
 * @return HOST_INTERFACE_STATUS_OK on success or when no outputs changed.
 */
static HOST_Interface_Status_T HOST_INSTRUCTION_HANDLER_EncodeDigitalOutputs(
    const HIL_Application_Test_Instruction_T* instruction, HostInstructionWriter_T* writer );

/**
 * @brief Detects analogue output changes and encodes an analogue batch operation.
 *
 * @param[in]     instruction Pointer to the incoming application test instruction.
 * @param[in,out] writer      Cursor context for appending the operation.
 *
 * @return HOST_INTERFACE_STATUS_OK on success, or HOST_INTERFACE_STATUS_VALIDATION_FAILED.
 */
static HOST_Interface_Status_T HOST_INSTRUCTION_HANDLER_EncodeAnalogueOutputs(
    const HIL_Application_Test_Instruction_T* instruction, HostInstructionWriter_T* writer );

/**
 * @brief Detects PWM parameter changes and encodes LV/HV PWM update operations.
 *
 * @param[in]     instruction Pointer to the incoming application test instruction.
 * @param[in,out] writer      Cursor context for appending the operation.
 *
 * @return HOST_INTERFACE_STATUS_OK on success, or HOST_INTERFACE_STATUS_VALIDATION_FAILED.
 */
static HOST_Interface_Status_T
HOST_INSTRUCTION_HANDLER_EncodePwmOutputs( const HIL_Application_Test_Instruction_T* instruction,
                                           HostInstructionWriter_T*                  writer );

/**
 * @brief Placeholder stub for future UART transmit instruction encoding.
 *
 * @param[in]     instruction Pointer to the incoming application test instruction.
 * @param[in,out] writer      Cursor context for appending the operation.
 *
 * @return HOST_INTERFACE_STATUS_OK.
 */
static HOST_Interface_Status_T HOST_INSTRUCTION_HANDLER_EncodeUartTransmitStub(
    const HIL_Application_Test_Instruction_T* instruction, HostInstructionWriter_T* writer );

/**
 * @brief Placeholder stub for future SPI transmit instruction encoding.
 *
 * @param[in]     instruction Pointer to the incoming application test instruction.
 * @param[in,out] writer      Cursor context for appending the operation.
 *
 * @return HOST_INTERFACE_STATUS_OK.
 */
static HOST_Interface_Status_T HOST_INSTRUCTION_HANDLER_EncodeSpiTransmitStub(
    const HIL_Application_Test_Instruction_T* instruction, HostInstructionWriter_T* writer );

/**
 * @brief Placeholder stub for future CAN transmit instruction encoding.
 *
 * @param[in]     instruction Pointer to the incoming application test instruction.
 * @param[in,out] writer      Cursor context for appending the operation.
 *
 * @return HOST_INTERFACE_STATUS_OK.
 */
static HOST_Interface_Status_T HOST_INSTRUCTION_HANDLER_EncodeCanTransmitStub(
    const HIL_Application_Test_Instruction_T* instruction, HostInstructionWriter_T* writer );

/**
 * @brief Converts an application instruction into canonical Execution Manager format.
 *
 * @param[in]  instruction           Pointer to incoming application test instruction.
 * @param[out] destination           Destination buffer for packed canonical bytes.
 * @param[in]  destination_capacity  Maximum capacity of destination buffer.
 * @param[out] bytes_written         Total canonical instruction bytes written (0 for output-free
 * tick).
 *
 * @return HOST_INTERFACE_STATUS_OK on success, or error status on failure.
 */
static HOST_Interface_Status_T
HOST_INSTRUCTION_HANDLER_ConvertInstruction( const HIL_Application_Test_Instruction_T* instruction,
                                             uint8_t* destination, size_t destination_capacity,
                                             size_t* bytes_written );

/**
 * @brief Submits a canonical instruction byte stream chunk to the Flash Manager.
 *
 * @param[in] data   Pointer to contiguous canonical instruction bytes.
 * @param[in] length Number of bytes to upload.
 *
 * @return HOST_INTERFACE_STATUS_OK on success, or corresponding error status.
 */
static HOST_Interface_Status_T HOST_INSTRUCTION_HANDLER_UploadToFlash( const uint8_t* data,
                                                                       size_t         length );

/**
 * @brief Validates an incoming application instruction message against protocol invariants.
 *
 * Enforces:
 *   - Non-null instruction argument.
 *   - Strictly increasing tick order across the upload sequence (monotonic, non-consecutive
 * allowed).
 *   - Digital output state values are strictly 0 or 1.
 *   - PWM duty cycle is in the valid 0..10000 permyriad range (0..100%).
 *   - PWM duty cycle is zero when period is zero (disabled output invariant).
 */
static HOST_Interface_Status_T HOST_INSTRUCTION_HANDLER_ValidateInstruction(
    const HIL_Application_Test_Instruction_T* const instruction )
{
    if ( instruction == NULL )
    {
        return HOST_INTERFACE_STATUS_INVALID_ARGUMENT;
    }

    if ( has_received_instruction && ( instruction->tick_number <= last_instruction_timestamp ) )
    {
        return HOST_INTERFACE_STATUS_INCONSISTENT_TICK;
    }

    for ( uint8_t i = 0U; i < HIL_APPLICATION_DIGITAL_OUTPUT_CHANNEL_COUNT; i++ )
    {
        if ( instruction->digital_outputs[i].high > 1U )
        {
            return HOST_INTERFACE_STATUS_VALIDATION_FAILED;
        }
    }

    for ( uint8_t channel = 0U; channel < HIL_APPLICATION_PWM_OUTPUT_CHANNEL_COUNT; channel++ )
    {
        const uint32_t period_ns      = instruction->pwm_outputs[channel].period_nanoseconds;
        const uint16_t duty_permyriad = instruction->pwm_outputs[channel].duty_cycle_permyriad;

        if ( duty_permyriad > 10000U )
        {
            return HOST_INTERFACE_STATUS_VALIDATION_FAILED;
        }

        if ( ( period_ns == 0U ) && ( duty_permyriad != 0U ) )
        {
            return HOST_INTERFACE_STATUS_VALIDATION_FAILED;
        }
    }

    return HOST_INTERFACE_STATUS_OK;
}

/**
 * @brief Appends one operation to the instruction buffer, maintaining 4-byte alignment.
 *
 * Formats a 32-bit little-endian operation header word containing opcode, channel,
 * and payload length, copies the payload, and guarantees zero-padding up to the
 * next 4-byte boundary.
 */
static HOST_Interface_Status_T HOST_INSTRUCTION_HANDLER_AppendOperation(
    HostInstructionWriter_T* const writer, const ExecutionOperationOpcode_T opcode,
    const uint8_t channel, const void* const payload, const uint16_t payload_size_bytes )
{
    const uint32_t encoded_size_bytes =
        EXECUTION_OPERATION_ENCODED_SIZE_BYTES( payload_size_bytes );

    if ( ( writer->offset + encoded_size_bytes ) > writer->capacity )
    {
        return HOST_INTERFACE_STATUS_BUFFER_TOO_SMALL;
    }

    uint8_t* const operation_start = writer->buffer + writer->offset;

    // Clear operation memory to guarantee zero-padding on trailing boundary bytes
    ( void )memset( operation_start, 0, encoded_size_bytes );

    // Pack 32-bit header word: [payload_length_bytes:16][channel:8][opcode:8]
    const ExecutionOperationHeaderWord_T header_word =
        ( ( ( uint32_t )opcode << EXECUTION_OPERATION_OPCODE_SHIFT )
          & EXECUTION_OPERATION_OPCODE_MASK )
        | ( ( ( uint32_t )channel << EXECUTION_OPERATION_CHANNEL_SHIFT )
            & EXECUTION_OPERATION_CHANNEL_MASK )
        | ( ( ( uint32_t )payload_size_bytes << EXECUTION_OPERATION_PAYLOAD_LENGTH_SHIFT )
            & EXECUTION_OPERATION_PAYLOAD_LENGTH_MASK );

    ( void )memcpy( operation_start, &header_word, sizeof( header_word ) );

    if ( ( payload != NULL ) && ( payload_size_bytes > 0U ) )
    {
        ( void )memcpy( operation_start + sizeof( header_word ), payload, payload_size_bytes );
    }

    writer->offset += encoded_size_bytes;
    writer->operation_count++;

    return HOST_INTERFACE_STATUS_OK;
}

/**
 * @brief Detects digital output transitions and encodes a digital output operation.
 *
 * Compares requested digital output pin states against the tracked baseline. If any
 * pin transitions (0 -> 1 or 1 -> 0), the affected pins are mapped to hardware GPIO
 * pinmasks via EXEC_DIGITAL_OUTPUT_Combine_Port_Pin_Masks() and appended as an
 * EXECUTION_OPERATION_OPCODE_DIGITAL_OUTPUT_UPDATE operation. If no pins change,
 * the operation is omitted.
 */
static HOST_Interface_Status_T HOST_INSTRUCTION_HANDLER_EncodeDigitalOutputs(
    const HIL_Application_Test_Instruction_T* const instruction,
    HostInstructionWriter_T* const                  writer )
{
    GPIOOutput_T high_pins[HIL_APPLICATION_DIGITAL_OUTPUT_CHANNEL_COUNT];
    GPIOOutput_T low_pins[HIL_APPLICATION_DIGITAL_OUTPUT_CHANNEL_COUNT];
    uint8_t      high_count = 0U;
    uint8_t      low_count  = 0U;

    for ( uint8_t index = 0U; index < HIL_APPLICATION_DIGITAL_OUTPUT_CHANNEL_COUNT; index++ )
    {
        const uint8_t new_state  = ( instruction->digital_outputs[index].high != 0U ) ? 1U : 0U;
        const uint8_t prev_state = tracked_peripheral_state.digital_outputs[index];

        if ( !tracked_peripheral_state.initialized || ( new_state != prev_state ) )
        {
            const GPIOOutput_T pin = ( GPIOOutput_T )( ( uint32_t )DIGITAL_OUTPUT_0 + index );
            if ( new_state != 0U )
            {
                high_pins[high_count++] = pin;
            }
            else
            {
                low_pins[low_count++] = pin;
            }
            tracked_peripheral_state.digital_outputs[index] = new_state;
        }
    }

    if ( ( high_count == 0U ) && ( low_count == 0U ) )
    {
        // No digital output state changes on this tick
        return HOST_INTERFACE_STATUS_OK;
    }

    const uint32_t high_mask =
        ( high_count > 0U ) ? EXEC_DIGITAL_OUTPUT_Combine_Port_Pin_Masks( high_pins, high_count )
                            : 0U;
    const uint32_t low_mask =
        ( low_count > 0U ) ? EXEC_DIGITAL_OUTPUT_Combine_Port_Pin_Masks( low_pins, low_count ) : 0U;

    ExecutionDigitalOutputPayload_T payload;
    payload.high_bitmask = high_mask;
    payload.low_bitmask  = low_mask;

    return HOST_INSTRUCTION_HANDLER_AppendOperation(
        writer, EXECUTION_OPERATION_OPCODE_DIGITAL_OUTPUT_UPDATE,
        EXECUTION_OPERATION_CHANNEL_UNUSED, &payload, sizeof( payload ) );
}

/**
 * @brief Detects analogue output changes and encodes an analogue batch operation.
 *
 * Compares requested microvolt levels for each DAC channel against the tracked baseline.
 * If any channel changes, its voltage is converted to Volts, prepared as an exact 3-byte
 * DAC SPI wire frame via EXEC_ANALOGUE_OUTPUT_Prepare_Frame(), and appended as an
 * EXECUTION_OPERATION_OPCODE_ANALOGUE_OUTPUT_BATCH operation containing 1 to 6 frames.
 * If no channels change, the operation is omitted.
 */
static HOST_Interface_Status_T HOST_INSTRUCTION_HANDLER_EncodeAnalogueOutputs(
    const HIL_Application_Test_Instruction_T* const instruction,
    HostInstructionWriter_T* const                  writer )
{
    AnalogueOutputPreparedBatch_T batch;
    ( void )memset( &batch, 0, sizeof( batch ) );
    uint8_t changed_count = 0U;

    for ( uint8_t channel = 0U; channel < HIL_APPLICATION_ANALOG_OUTPUT_CHANNEL_COUNT; channel++ )
    {
        const uint32_t new_voltage_uv  = instruction->analog_outputs[channel].microvolts;
        const uint32_t prev_voltage_uv = tracked_peripheral_state.analog_outputs[channel];

        if ( !tracked_peripheral_state.initialized || ( new_voltage_uv != prev_voltage_uv ) )
        {
            const float voltage_v = ( float )new_voltage_uv / HOST_INSTRUCTION_MICROVOLTS_PER_VOLT;
            AnalogueOutputPreparedFrame_T frame;
            ( void )memset( &frame, 0, sizeof( frame ) );

            if ( !EXEC_ANALOGUE_OUTPUT_Prepare_Frame( channel, voltage_v, &frame ) )
            {
                return HOST_INTERFACE_STATUS_VALIDATION_FAILED;
            }

            ( void )memcpy( &batch.bytes[changed_count * EXEC_ANALOGUE_OUTPUT_FRAME_SIZE_BYTES],
                            frame.bytes, sizeof( frame.bytes ) );
            changed_count++;
            tracked_peripheral_state.analog_outputs[channel] = new_voltage_uv;
        }
    }

    if ( changed_count == 0U )
    {
        // No analogue output state changes on this tick
        return HOST_INTERFACE_STATUS_OK;
    }

    const uint16_t batch_size_bytes =
        ( uint16_t )( changed_count * EXEC_ANALOGUE_OUTPUT_FRAME_SIZE_BYTES );

    return HOST_INSTRUCTION_HANDLER_AppendOperation(
        writer, EXECUTION_OPERATION_OPCODE_ANALOGUE_OUTPUT_BATCH,
        EXECUTION_OPERATION_CHANNEL_UNUSED, batch.bytes, batch_size_bytes );
}

/**
 * @brief Detects PWM parameter changes and encodes LV/HV PWM update operations.
 *
 * Compares period and duty against the tracked baseline for LV (index 0) and HV (index 1).
 * When changes are detected on an active channel (period_nanoseconds > 0), computes PSC, ARR, and
 * CCR register values via HW_PWM_GEN_compute_*() and appends an
 * EXECUTION_OPERATION_OPCODE_PWM_UPDATE operation. If unchanged, the operation is omitted.
 */
static HOST_Interface_Status_T HOST_INSTRUCTION_HANDLER_EncodePwmOutputs(
    const HIL_Application_Test_Instruction_T* const instruction,
    HostInstructionWriter_T* const                  writer )
{
    for ( uint8_t channel = 0U; channel < HIL_APPLICATION_PWM_OUTPUT_CHANNEL_COUNT; channel++ )
    {
        const uint32_t period_ns      = instruction->pwm_outputs[channel].period_nanoseconds;
        const uint16_t duty_permyriad = instruction->pwm_outputs[channel].duty_cycle_permyriad;

        const uint32_t prev_period_ns =
            tracked_peripheral_state.pwm_outputs[channel].period_nanoseconds;
        const uint16_t prev_duty_permyriad =
            tracked_peripheral_state.pwm_outputs[channel].duty_cycle_permyriad;

        if ( tracked_peripheral_state.initialized && ( period_ns == prev_period_ns )
             && ( duty_permyriad == prev_duty_permyriad ) )
        {
            // Unchanged on this tick
            continue;
        }

        tracked_peripheral_state.pwm_outputs[channel].period_nanoseconds   = period_ns;
        tracked_peripheral_state.pwm_outputs[channel].duty_cycle_permyriad = duty_permyriad;

        if ( period_ns == 0U )
        {
            // Period of zero indicates disabled/inactive output; no update operation needed
            continue;
        }

        const uint32_t timer_clock_hz = ( channel == EXECUTION_OPERATION_PWM_CHANNEL_LV )
                                            ? HOST_INSTRUCTION_PWM_LV_TIMER_CLOCK_HZ
                                            : HOST_INSTRUCTION_PWM_HV_TIMER_CLOCK_HZ;

        const uint32_t frequency_hz  = HOST_INSTRUCTION_NANOSECONDS_PER_SECOND / period_ns;
        const uint16_t duty_permille = ( uint16_t )( duty_permyriad / 10U );

        ExecutionPwmUpdatePayload_T payload;
        ( void )memset( &payload, 0, sizeof( payload ) );

        if ( !HW_PWM_GEN_compute_psc( frequency_hz, timer_clock_hz, &payload.psc )
             || !HW_PWM_GEN_compute_arr( frequency_hz, timer_clock_hz, payload.psc, &payload.arr )
             || !HW_PWM_GEN_compute_ccr( duty_permille, payload.arr, &payload.ccr ) )
        {
            return HOST_INTERFACE_STATUS_VALIDATION_FAILED;
        }

        const HOST_Interface_Status_T status = HOST_INSTRUCTION_HANDLER_AppendOperation(
            writer, EXECUTION_OPERATION_OPCODE_PWM_UPDATE, channel, &payload, sizeof( payload ) );

        if ( status != HOST_INTERFACE_STATUS_OK )
        {
            return status;
        }
    }

    return HOST_INTERFACE_STATUS_OK;
}

/**
 * @brief Placeholder stub for future UART transmit instruction encoding.
 */
static HOST_Interface_Status_T HOST_INSTRUCTION_HANDLER_EncodeUartTransmitStub(
    const HIL_Application_Test_Instruction_T* const instruction,
    HostInstructionWriter_T* const                  writer )
{
    ( void )instruction;
    ( void )writer;
    return HOST_INTERFACE_STATUS_OK;
}

/**
 * @brief Placeholder stub for future SPI transmit instruction encoding.
 */
static HOST_Interface_Status_T HOST_INSTRUCTION_HANDLER_EncodeSpiTransmitStub(
    const HIL_Application_Test_Instruction_T* const instruction,
    HostInstructionWriter_T* const                  writer )
{
    ( void )instruction;
    ( void )writer;
    return HOST_INTERFACE_STATUS_OK;
}

/**
 * @brief Placeholder stub for future CAN transmit instruction encoding.
 */
static HOST_Interface_Status_T HOST_INSTRUCTION_HANDLER_EncodeCanTransmitStub(
    const HIL_Application_Test_Instruction_T* const instruction,
    HostInstructionWriter_T* const                  writer )
{
    ( void )instruction;
    ( void )writer;
    return HOST_INTERFACE_STATUS_OK;
}

/**
 * @brief Converts an application instruction into canonical Execution Manager format.
 *
 * Iterates through each peripheral encoder, reserving space for the 8-byte instruction header
 * at the start of destination. If at least one operation is generated, builds the
 * ExecutionInstructionHeader_T with timestamp, operation count, and total operation byte length.
 */
static HOST_Interface_Status_T HOST_INSTRUCTION_HANDLER_ConvertInstruction(
    const HIL_Application_Test_Instruction_T* const instruction, uint8_t* const destination,
    const size_t destination_capacity, size_t* const bytes_written )
{
    if ( destination_capacity < sizeof( ExecutionInstructionHeader_T ) )
    {
        return HOST_INTERFACE_STATUS_BUFFER_TOO_SMALL;
    }

    HostInstructionWriter_T writer;
    writer.buffer          = destination;
    writer.capacity        = destination_capacity;
    writer.offset          = sizeof( ExecutionInstructionHeader_T );
    writer.operation_count = 0U;

    // 1. Digital Outputs
    HOST_Interface_Status_T status =
        HOST_INSTRUCTION_HANDLER_EncodeDigitalOutputs( instruction, &writer );
    if ( status != HOST_INTERFACE_STATUS_OK )
    {
        return status;
    }

    // 2. Analogue Outputs
    status = HOST_INSTRUCTION_HANDLER_EncodeAnalogueOutputs( instruction, &writer );
    if ( status != HOST_INTERFACE_STATUS_OK )
    {
        return status;
    }

    // 3. PWM Outputs
    status = HOST_INSTRUCTION_HANDLER_EncodePwmOutputs( instruction, &writer );
    if ( status != HOST_INTERFACE_STATUS_OK )
    {
        return status;
    }

    // 4. Peripheral Stubs (for future protocol extensions)
    status = HOST_INSTRUCTION_HANDLER_EncodeUartTransmitStub( instruction, &writer );
    if ( status != HOST_INTERFACE_STATUS_OK )
    {
        return status;
    }

    status = HOST_INSTRUCTION_HANDLER_EncodeSpiTransmitStub( instruction, &writer );
    if ( status != HOST_INTERFACE_STATUS_OK )
    {
        return status;
    }

    status = HOST_INSTRUCTION_HANDLER_EncodeCanTransmitStub( instruction, &writer );
    if ( status != HOST_INTERFACE_STATUS_OK )
    {
        return status;
    }

    // If no operations changed on this tick, this is an output-free tick
    if ( writer.operation_count == 0U )
    {
        *bytes_written = 0U;
        return HOST_INTERFACE_STATUS_OK;
    }

    // 5. Build Final Instruction Header
    const uint16_t operations_length_bytes =
        ( uint16_t )( writer.offset - sizeof( ExecutionInstructionHeader_T ) );

    ExecutionInstructionHeader_T header;
    header.timestamp               = instruction->tick_number;
    header.operations_length_bytes = operations_length_bytes;
    header.operation_count         = writer.operation_count;
    header.reserved                = 0U;

    ( void )memcpy( destination, &header, sizeof( header ) );

    *bytes_written = writer.offset;
    return HOST_INTERFACE_STATUS_OK;
}

/**
 * @brief Submits a canonical instruction byte stream chunk to the Flash Manager.
 */
static HOST_Interface_Status_T HOST_INSTRUCTION_HANDLER_UploadToFlash( const uint8_t* const data,
                                                                       const size_t         length )
{
    const FlashManagerInstructionUploadRequestStatus_T upload_status =
        FLASH_MANAGER_SubmitInstructionUploadBytes( data, ( uint32_t )length );

    if ( upload_status == FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_ACCEPTED )
    {
        return HOST_INTERFACE_STATUS_OK;
    }

    if ( upload_status == FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_BUSY )
    {
        return HOST_INTERFACE_STATUS_INTERNAL_ERROR;
    }

    if ( upload_status == FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_INVALID_STATE )
    {
        return HOST_INTERFACE_STATUS_STATE_TRANSITION_FAILURE;
    }

    return HOST_INTERFACE_STATUS_INTERNAL_ERROR;
}

/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */

/**
 * @brief Resets the instruction message handler state.
 */
void HOST_INSTRUCTION_HANDLER_Reset( void )
{
    last_instruction_timestamp = 0U;
    has_received_instruction   = false;
    ( void )memset( &tracked_peripheral_state, 0, sizeof( tracked_peripheral_state ) );
    tracked_peripheral_state.initialized = true;
}

/**
 * @brief Handles an incoming application layer instruction message.
 */
HOST_Interface_Status_T HOST_INSTRUCTION_HANDLER_HandleInstruction(
    const HIL_Application_Test_Instruction_T* const instruction )
{
    const HOST_Interface_Status_T validation_status =
        HOST_INSTRUCTION_HANDLER_ValidateInstruction( instruction );
    if ( validation_status != HOST_INTERFACE_STATUS_OK )
    {
        return validation_status;
    }

#if defined( __cplusplus )
    alignas( 4 ) static uint8_t instruction_buffer[EXECUTION_INSTRUCTION_MAX_SIZE_BYTES];
#else
    _Alignas( 4 ) static uint8_t instruction_buffer[EXECUTION_INSTRUCTION_MAX_SIZE_BYTES];
#endif
    size_t instruction_size_bytes = 0U;

    const HOST_Interface_Status_T status = HOST_INSTRUCTION_HANDLER_ConvertInstruction(
        instruction, instruction_buffer, sizeof( instruction_buffer ), &instruction_size_bytes );

    if ( status != HOST_INTERFACE_STATUS_OK )
    {
        return status;
    }

    // Output-free tick: no operations changed, no flash instruction to upload
    if ( instruction_size_bytes == 0U )
    {
        last_instruction_timestamp = instruction->tick_number;
        has_received_instruction   = true;
        return HOST_INTERFACE_STATUS_OK;
    }

    // Layer 2: Canonical Execution Manager validation check
    const ExecutionInstructionValidationResult_T canonical_val_status =
        EXECUTION_INSTRUCTION_Validate( instruction_buffer, instruction_size_bytes );
    if ( canonical_val_status != EXECUTION_INSTRUCTION_VALIDATION_OK )
    {
        return HOST_INTERFACE_STATUS_VALIDATION_FAILED;
    }

    const HOST_Interface_Status_T upload_status =
        HOST_INSTRUCTION_HANDLER_UploadToFlash( instruction_buffer, instruction_size_bytes );

    if ( upload_status == HOST_INTERFACE_STATUS_OK )
    {
        last_instruction_timestamp = instruction->tick_number;
        has_received_instruction   = true;
    }

    return upload_status;
}