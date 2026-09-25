/******************************************************************************
 *  File:       variable_result_message_producer.c
 *  Author:     Callum Rafferty
 *  Created:    24-Sep-2026
 *
 *  Description:
 *      Implementation of the variable result message producer for retrieving raw
 *      measurement records from Flash Manager, converting them into sparse
 *      captured records, and constructing outbound Application Variable Test
 *      Result messages (VARIABLE_TEST_RESULT, Type 34).
 *
 *  Notes:
 *      Converts one-based execution timestamps from Flash Manager into zero-based
 *      Application result ticks. Supports Digital, Analogue, PWM capture, UART,
 *      SPI, and CAN records with zero heap allocation.
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include "variable_result_message_producer.h"
#include "flash_manager/flash_manager.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */

/** @brief Internal staging capacity for reassembling records from Flash Manager. */
#define VAR_RESULT_PRODUCER_BUFFER_CAPACITY ( 1024U + sizeof( FlashManagerResultHeader_T ) )

/**
 * @brief Maximum captured payload staged for one tick.
 *
 * A variable result has a 23-byte envelope and a 12-byte body header. With at
 * most eleven distinct captured peripheral records, 400 payload bytes leave
 * room for every record header and alignment pad while keeping the complete
 * Application message within its 512-byte protocol ceiling.
 */
#define VAR_RESULT_PRODUCER_STAGED_PAYLOAD_CAPACITY ( 400U )

/** @brief Timer input clock frequency for PWM capture (TIM2 and TIM5 on APB1). */
#define VAR_RESULT_PRODUCER_PWM_TIMER_CLOCK_HZ ( 90000000U )

/** @brief Nanoseconds per second constant for period conversion. */
#define VAR_RESULT_PRODUCER_NANOSECONDS_PER_SECOND ( 1000000000ULL )

/** @brief Permyriad scaling factor (100% = 10000). */
#define VAR_RESULT_PRODUCER_PERMYRIAD_SCALE ( 10000ULL )

/**-----------------------------------------------------------------------------
 *  Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

/**
 * @brief Internal stream state for variable result production.
 */
typedef struct
{
    /** Staging buffer for streaming and reassembling record bytes from flash. */
    uint8_t buffer[VAR_RESULT_PRODUCER_BUFFER_CAPACITY];

    /** Byte offset of first unparsed byte in buffer. */
    size_t read_offset;

    /** Total valid bytes currently stored in buffer. */
    size_t write_offset;

    /** Set to true when Flash Manager signals end of stored result stream. */
    bool is_flash_end_of_stream;

    /** Indicates whether an active tick message is currently being aggregated. */
    bool has_active_tick;

    /** One-based execution timestamp of the result message currently being aggregated. */
    uint32_t active_tick_number;

    /** Indicates whether at least one complete tick message has been emitted. */
    bool has_emitted_tick;

    /** Timestamp of the most recently emitted tick message. */
    uint32_t last_emitted_timestamp;

    /** Number of zero-based result ticks already emitted. */
    uint32_t next_result_tick;

    /** Staged captured records for the active tick. */
    HIL_Application_Captured_Record_T staged_records[VARIABLE_RESULT_MAX_STAGED_RECORDS];

    /** Number of valid records in staged_records. */
    uint8_t staged_record_count;

    /** Buffer holding converted wire payloads referenced by staged_records spans. */
    uint8_t staged_payload_storage[VAR_RESULT_PRODUCER_STAGED_PAYLOAD_CAPACITY];

    /** Current write offset in staged_payload_storage. */
    size_t staged_payload_offset;

    /** Set when capture bytes are truncated to the bounded wire representation. */
    bool capture_overflow;

    /** Number of zero-based result ticks visible to the host. */
    uint32_t expected_tick_count;

    bool expected_tick_count_configured;
} VariableResultProducerStream_T;

/**-----------------------------------------------------------------------------
 *  Private (static) Variables
 *------------------------------------------------------------------------------
 */

/** @brief Singleton stream context for variable result message production. */
static VariableResultProducerStream_T s_var_stream;

/** @brief Live diagnostics state for variable result production. */
static VariableResultProducerDiagnostics_T s_var_diagnostics;

/**
 * @brief Physical GPIOD pin masks indexed by zero-based protocol DI channel.
 */
static const uint32_t
    VAR_RESULT_PRODUCER_DIGITAL_INPUT_PIN_MASKS[HIL_APPLICATION_DIGITAL_INPUT_CHANNEL_COUNT] = {
        1UL << 8U,  1UL << 9U, 1UL << 10U, 1UL << 11U, 1UL << 14U,
        1UL << 15U, 1UL << 0U, 1UL << 1U,  1UL << 2U,  1UL << 3U,
};

/**-----------------------------------------------------------------------------
 *  Private (static) Function Prototypes
 *------------------------------------------------------------------------------
 */

static FlashManagerResultTransferStatus_T
VAR_RESULT_PRODUCER_FetchFromFlash( VariableResultProducerStream_T* stream );

static bool VAR_RESULT_PRODUCER_PeekNextRecord( VariableResultProducerStream_T* stream,
                                                FlashManagerResultHeader_T*     header,
                                                const uint8_t**                 payload );

static void VAR_RESULT_PRODUCER_ConsumeRecord( VariableResultProducerStream_T* stream,
                                               uint16_t payload_length_bytes );

static bool VAR_RESULT_PRODUCER_DispatchRecord( const FlashManagerResultHeader_T* header,
                                                const uint8_t*                    payload,
                                                VariableResultProducerStream_T*   stream );

static void VAR_RESULT_PRODUCER_PopulateResultMetadata(
    const VariableResultProducerStream_T* stream, HIL_Application_Message_T* out_message );

static void VAR_RESULT_PRODUCER_EmitEmptyTick( VariableResultProducerStream_T* stream,
                                               HIL_Application_Message_T*     out_message );

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

/**
 * @brief Compacts buffer and fetches new result bytes from the Flash Manager.
 */
static FlashManagerResultTransferStatus_T
VAR_RESULT_PRODUCER_FetchFromFlash( VariableResultProducerStream_T* const stream )
{
    if ( stream->read_offset > 0U )
    {
        const size_t remaining_bytes = stream->write_offset - stream->read_offset;
        if ( remaining_bytes > 0U )
        {
            ( void )memmove( stream->buffer, stream->buffer + stream->read_offset,
                             remaining_bytes );
        }
        stream->write_offset = remaining_bytes;
        stream->read_offset  = 0U;
    }

    const size_t capacity = sizeof( stream->buffer ) - stream->write_offset;
    if ( capacity == 0U )
    {
        s_var_diagnostics.last_flash_status = FLASH_MANAGER_RESULT_TRANSFER_OK;
        return FLASH_MANAGER_RESULT_TRANSFER_OK;
    }

    uint32_t                                 bytes_read = 0U;
    const FlashManagerResultTransferStatus_T status     = FLASH_MANAGER_ReadResultBytes(
        stream->buffer + stream->write_offset, ( uint32_t )capacity, &bytes_read );

    s_var_diagnostics.last_flash_status = status;

    if ( ( status == FLASH_MANAGER_RESULT_TRANSFER_OK ) && ( bytes_read > 0U ) )
    {
        stream->write_offset += bytes_read;
    }

    return status;
}

/**
 * @brief Inspects whether a complete record is available in the stream buffer.
 */
static bool VAR_RESULT_PRODUCER_PeekNextRecord( VariableResultProducerStream_T* const stream,
                                                FlashManagerResultHeader_T* const     header,
                                                const uint8_t** const                 payload )
{
    const size_t buffered_bytes = stream->write_offset - stream->read_offset;

    if ( buffered_bytes < sizeof( FlashManagerResultHeader_T ) )
    {
        return false;
    }

    const uint8_t* const header_bytes = stream->buffer + stream->read_offset;
    ( void )memcpy( header, header_bytes, sizeof( FlashManagerResultHeader_T ) );

    const size_t total_record_size =
        sizeof( FlashManagerResultHeader_T ) + ( size_t )header->payload_length_bytes;

    if ( buffered_bytes < total_record_size )
    {
        return false;
    }

    *payload = header_bytes + sizeof( FlashManagerResultHeader_T );
    return true;
}

/**
 * @brief Advances the read cursor past a consumed record.
 */
static void VAR_RESULT_PRODUCER_ConsumeRecord( VariableResultProducerStream_T* const stream,
                                               const uint16_t payload_length_bytes )
{
    stream->read_offset += sizeof( FlashManagerResultHeader_T ) + ( size_t )payload_length_bytes;
}

/**
 * @brief Unpacks and converts flash records into captured protocol records.
 */
static bool VAR_RESULT_PRODUCER_DispatchRecord( const FlashManagerResultHeader_T* const header,
                                                const uint8_t* const                    payload,
                                                VariableResultProducerStream_T* const   stream )
{
    if ( ( payload == NULL ) || ( header == NULL ) )
    {
        return false;
    }

    switch ( header->peripheral_type )
    {
        case FLASH_MANAGER_RESULT_PERIPHERAL_DIGITAL_INPUT: {
            if ( ( header->payload_length_bytes != sizeof( uint32_t ) )
                 || ( stream->staged_record_count >= VARIABLE_RESULT_MAX_STAGED_RECORDS )
                 || ( ( stream->staged_payload_offset + 2U )
                      > sizeof( stream->staged_payload_storage ) ) )
            {
                return false;
            }

            uint32_t pin_mask = 0U;
            ( void )memcpy( &pin_mask, payload, sizeof( pin_mask ) );

            uint16_t channel_mask = 0U;
            for ( uint8_t ch = 0U; ch < HIL_APPLICATION_DIGITAL_INPUT_CHANNEL_COUNT; ch++ )
            {
                if ( ( pin_mask & VAR_RESULT_PRODUCER_DIGITAL_INPUT_PIN_MASKS[ch] ) != 0U )
                {
                    channel_mask |= ( uint16_t )( 1U << ch );
                }
            }

            uint8_t* const dest = &stream->staged_payload_storage[stream->staged_payload_offset];
            dest[0]             = ( uint8_t )( channel_mask & 0xFFU );
            dest[1]             = ( uint8_t )( ( channel_mask >> 8U ) & 0xFFU );

            HIL_Application_Captured_Record_T* const rec =
                &stream->staged_records[stream->staged_record_count++];
            rec->peripheral_type = HIL_APPLICATION_PERIPHERAL_DIGITAL_INPUT;
            rec->channel         = 0U;
            rec->data.data       = dest;
            rec->data.size       = 2U;

            stream->staged_payload_offset += 2U;
            return true;
        }

        case FLASH_MANAGER_RESULT_PERIPHERAL_ANALOGUE_INPUT: {
            if ( ( header->payload_length_bytes != ( 2U * sizeof( uint32_t ) ) )
                 || ( ( stream->staged_record_count + 2U ) > VARIABLE_RESULT_MAX_STAGED_RECORDS )
                 || ( ( stream->staged_payload_offset + 8U )
                      > sizeof( stream->staged_payload_storage ) ) )
            {
                return false;
            }

            // Channel 0
            uint8_t* const dest_ch0 =
                &stream->staged_payload_storage[stream->staged_payload_offset];
            ( void )memcpy( dest_ch0, payload, sizeof( uint32_t ) );

            HIL_Application_Captured_Record_T* const rec0 =
                &stream->staged_records[stream->staged_record_count++];
            rec0->peripheral_type = HIL_APPLICATION_PERIPHERAL_ANALOG_INPUT;
            rec0->channel         = 0U;
            rec0->data.data       = dest_ch0;
            rec0->data.size       = 4U;

            // Channel 1
            uint8_t* const dest_ch1 =
                &stream->staged_payload_storage[stream->staged_payload_offset + 4U];
            ( void )memcpy( dest_ch1, payload + sizeof( uint32_t ), sizeof( uint32_t ) );

            HIL_Application_Captured_Record_T* const rec1 =
                &stream->staged_records[stream->staged_record_count++];
            rec1->peripheral_type = HIL_APPLICATION_PERIPHERAL_ANALOG_INPUT;
            rec1->channel         = 1U;
            rec1->data.data       = dest_ch1;
            rec1->data.size       = 4U;

            stream->staged_payload_offset += 8U;
            return true;
        }

        case FLASH_MANAGER_RESULT_PERIPHERAL_PWM_CAPTURE: {
            if ( ( header->channel >= HIL_APPLICATION_PWM_INPUT_CHANNEL_COUNT )
                 || ( header->payload_length_bytes != ( 2U * sizeof( uint32_t ) ) )
                 || ( stream->staged_record_count >= VARIABLE_RESULT_MAX_STAGED_RECORDS )
                 || ( ( stream->staged_payload_offset + 6U )
                      > sizeof( stream->staged_payload_storage ) ) )
            {
                return false;
            }

            uint32_t period_ticks = 0U;
            uint32_t high_ticks   = 0U;
            ( void )memcpy( &period_ticks, payload, sizeof( uint32_t ) );
            ( void )memcpy( &high_ticks, payload + sizeof( uint32_t ), sizeof( uint32_t ) );

            uint32_t period_ns      = 0U;
            uint16_t duty_permyriad = 0U;

            if ( period_ticks > 0U )
            {
                if ( high_ticks > period_ticks )
                {
                    return false;
                }

                // In hardware Slave-Reset mode, +2 clock counts restore physical pulse duration
                const uint32_t corrected_period_ticks = period_ticks + 2U;
                const uint32_t corrected_high_ticks   = high_ticks + 2U;

                const uint64_t ns = ( ( uint64_t )corrected_period_ticks
                                      * VAR_RESULT_PRODUCER_NANOSECONDS_PER_SECOND )
                                    / VAR_RESULT_PRODUCER_PWM_TIMER_CLOCK_HZ;
                const uint64_t duty =
                    ( ( uint64_t )corrected_high_ticks * VAR_RESULT_PRODUCER_PERMYRIAD_SCALE )
                    / corrected_period_ticks;

                period_ns      = ( uint32_t )ns;
                duty_permyriad = ( uint16_t )duty;
            }

            uint8_t* const dest = &stream->staged_payload_storage[stream->staged_payload_offset];
            dest[0]             = ( uint8_t )( period_ns & 0xFFU );
            dest[1]             = ( uint8_t )( ( period_ns >> 8U ) & 0xFFU );
            dest[2]             = ( uint8_t )( ( period_ns >> 16U ) & 0xFFU );
            dest[3]             = ( uint8_t )( ( period_ns >> 24U ) & 0xFFU );
            dest[4]             = ( uint8_t )( duty_permyriad & 0xFFU );
            dest[5]             = ( uint8_t )( ( duty_permyriad >> 8U ) & 0xFFU );

            HIL_Application_Captured_Record_T* const rec =
                &stream->staged_records[stream->staged_record_count++];
            rec->peripheral_type = HIL_APPLICATION_PERIPHERAL_PWM_INPUT;
            rec->channel         = header->channel;
            rec->data.data       = dest;
            rec->data.size       = 6U;

            stream->staged_payload_offset += 6U;
            return true;
        }

        case FLASH_MANAGER_RESULT_PERIPHERAL_UART_RECEIVE: {
            if ( header->channel >= HIL_APPLICATION_UART_CHANNEL_COUNT )
            {
                return false;
            }
            if ( header->payload_length_bytes == 0U )
            {
                return true;
            }

            if ( stream->staged_record_count >= VARIABLE_RESULT_MAX_STAGED_RECORDS )
            {
                stream->capture_overflow = true;
                return true;
            }

            size_t copied_length = header->payload_length_bytes;
            if ( copied_length > UINT8_MAX )
            {
                copied_length           = UINT8_MAX;
                stream->capture_overflow = true;
            }
            const size_t available = sizeof( stream->staged_payload_storage )
                                     - stream->staged_payload_offset;
            if ( copied_length > available )
            {
                copied_length           = available;
                stream->capture_overflow = true;
            }
            if ( copied_length == 0U )
            {
                return true;
            }

            uint8_t* const dest = &stream->staged_payload_storage[stream->staged_payload_offset];
            ( void )memcpy( dest, payload, copied_length );

            HIL_Application_Captured_Record_T* const rec =
                &stream->staged_records[stream->staged_record_count++];
            rec->peripheral_type = HIL_APPLICATION_PERIPHERAL_UART;
            rec->channel         = header->channel;
            rec->data.data       = dest;
            rec->data.size       = ( uint8_t )copied_length;

            stream->staged_payload_offset += copied_length;
            return true;
        }

        case FLASH_MANAGER_RESULT_PERIPHERAL_SPI_RECEIVE: {
            if ( header->channel >= HIL_APPLICATION_SPI_CHANNEL_COUNT )
            {
                return false;
            }
            if ( header->payload_length_bytes == 0U )
            {
                return true;
            }

            if ( stream->staged_record_count >= VARIABLE_RESULT_MAX_STAGED_RECORDS )
            {
                stream->capture_overflow = true;
                return true;
            }

            size_t copied_length = header->payload_length_bytes;
            if ( copied_length > UINT8_MAX )
            {
                copied_length           = UINT8_MAX;
                stream->capture_overflow = true;
            }
            const size_t available = sizeof( stream->staged_payload_storage )
                                     - stream->staged_payload_offset;
            if ( copied_length > available )
            {
                copied_length           = available;
                stream->capture_overflow = true;
            }
            if ( copied_length == 0U )
            {
                return true;
            }

            uint8_t* const dest = &stream->staged_payload_storage[stream->staged_payload_offset];
            ( void )memcpy( dest, payload, copied_length );

            HIL_Application_Captured_Record_T* const rec =
                &stream->staged_records[stream->staged_record_count++];
            rec->peripheral_type = HIL_APPLICATION_PERIPHERAL_SPI;
            rec->channel         = header->channel;
            rec->data.data       = dest;
            rec->data.size       = ( uint8_t )copied_length;

            stream->staged_payload_offset += copied_length;
            return true;
        }

        case FLASH_MANAGER_RESULT_PERIPHERAL_CAN_RECEIVE: {
            if ( header->channel >= HIL_APPLICATION_CAN_CHANNEL_COUNT )
            {
                return false;
            }
            if ( header->payload_length_bytes == 0U )
            {
                return true;
            }
            if ( ( header->payload_length_bytes % 12U ) != 0U )
            {
                return false;
            }

            if ( stream->staged_record_count >= VARIABLE_RESULT_MAX_STAGED_RECORDS )
            {
                stream->capture_overflow = true;
                return true;
            }

            size_t copied_length = header->payload_length_bytes;
            if ( copied_length > UINT8_MAX )
            {
                copied_length           = UINT8_MAX - ( UINT8_MAX % 12U );
                stream->capture_overflow = true;
            }
            const size_t available = sizeof( stream->staged_payload_storage )
                                     - stream->staged_payload_offset;
            if ( copied_length > available )
            {
                copied_length           = available - ( available % 12U );
                stream->capture_overflow = true;
            }
            if ( copied_length == 0U )
            {
                return true;
            }

            uint8_t* const dest = &stream->staged_payload_storage[stream->staged_payload_offset];
            ( void )memcpy( dest, payload, copied_length );

            /* Zero the reserved 12th byte in every 12-byte classical CAN frame */
            for ( size_t f = 0U; f < copied_length; f += 12U )
            {
                dest[f + 11U] = 0U;
            }

            HIL_Application_Captured_Record_T* const rec =
                &stream->staged_records[stream->staged_record_count++];
            rec->peripheral_type = HIL_APPLICATION_PERIPHERAL_CAN;
            rec->channel         = header->channel;
            rec->data.data       = dest;
            rec->data.size       = ( uint8_t )copied_length;

            stream->staged_payload_offset += copied_length;
            return true;
        }

        default:
            return false;
    }
}

/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */

void VARIABLE_RESULT_MESSAGE_PRODUCER_Reset( void )
{
    ( void )memset( &s_var_stream, 0, sizeof( s_var_stream ) );
    ( void )memset( &s_var_diagnostics, 0, sizeof( s_var_diagnostics ) );
}

void VARIABLE_RESULT_MESSAGE_PRODUCER_SetExpectedTickCount( const uint32_t tick_count )
{
    s_var_stream.expected_tick_count = tick_count;
    s_var_stream.expected_tick_count_configured = true;
}

static void VAR_RESULT_PRODUCER_PopulateResultMetadata(
    const VariableResultProducerStream_T* const stream, HIL_Application_Message_T* const out_message )
{
    out_message->body.variable_test_result.condition =
        stream->capture_overflow ? HIL_APPLICATION_RESULT_CONDITION_PARTIAL
                                 : HIL_APPLICATION_RESULT_CONDITION_OK;
    out_message->body.variable_test_result.problem_detail =
        stream->capture_overflow ? HIL_APPLICATION_RESULT_PROBLEM_DETAIL_CAPTURE_OVERFLOW : 0U;
}

static void VAR_RESULT_PRODUCER_EmitEmptyTick( VariableResultProducerStream_T* const stream,
                                               HIL_Application_Message_T* const out_message )
{
    out_message->type        = HIL_APPLICATION_MESSAGE_TYPE_VARIABLE_TEST_RESULT;
    out_message->subtype     = HIL_APPLICATION_MESSAGE_SUBTYPE_NONE;
    out_message->has_test_id = 1U;
    out_message->body.variable_test_result.tick_number = stream->next_result_tick;
    VAR_RESULT_PRODUCER_PopulateResultMetadata( stream, out_message );
    out_message->body.variable_test_result.flags        = HIL_APPLICATION_RESULT_FLAG_COMPLETE_TICK;
    out_message->body.variable_test_result.record_count = 0U;
    out_message->body.variable_test_result.records       = NULL;

    stream->has_emitted_tick       = true;
    stream->last_emitted_timestamp = stream->next_result_tick + 1U;
    stream->next_result_tick++;
}

static Result_Message_Producer_Status_T
VAR_RESULT_PRODUCER_ProduceNextMessageInternal( HIL_Application_Message_T* const out_message )
{
    if ( out_message == NULL )
    {
        return RESULT_MESSAGE_PRODUCER_STATUS_INVALID_ARGUMENT;
    }

    VariableResultProducerStream_T* const stream = &s_var_stream;

    while ( 1 )
    {
        FlashManagerResultHeader_T header;
        const uint8_t*             payload = NULL;

        if ( !VAR_RESULT_PRODUCER_PeekNextRecord( stream, &header, &payload ) )
        {
            if ( stream->is_flash_end_of_stream )
            {
                if ( stream->has_active_tick )
                {
                    // Emit the final aggregated tick
                    out_message->type        = HIL_APPLICATION_MESSAGE_TYPE_VARIABLE_TEST_RESULT;
                    out_message->subtype     = HIL_APPLICATION_MESSAGE_SUBTYPE_NONE;
                    out_message->has_test_id = 1U;
                    out_message->body.variable_test_result.tick_number =
                        stream->active_tick_number - 1U;
                    if ( stream->expected_tick_count_configured
                         && ( ( stream->active_tick_number - 1U ) >= stream->expected_tick_count ) )
                    {
                        stream->has_active_tick = false;
                        return RESULT_MESSAGE_PRODUCER_STATUS_END_OF_STREAM;
                    }
                    VAR_RESULT_PRODUCER_PopulateResultMetadata( stream, out_message );
                    out_message->body.variable_test_result.flags =
                        HIL_APPLICATION_RESULT_FLAG_COMPLETE_TICK;
                    out_message->body.variable_test_result.record_count =
                        stream->staged_record_count;
                    out_message->body.variable_test_result.records =
                        ( stream->staged_record_count > 0U ) ? stream->staged_records : NULL;

                    stream->has_emitted_tick       = true;
                    stream->last_emitted_timestamp = stream->active_tick_number;
                    stream->next_result_tick       = stream->active_tick_number;
                    stream->has_active_tick        = false;
                    return RESULT_MESSAGE_PRODUCER_STATUS_OK;
                }

                if ( stream->expected_tick_count_configured
                     && ( stream->next_result_tick < stream->expected_tick_count ) )
                {
                    VAR_RESULT_PRODUCER_EmitEmptyTick( stream, out_message );
                    return RESULT_MESSAGE_PRODUCER_STATUS_OK;
                }

                return RESULT_MESSAGE_PRODUCER_STATUS_END_OF_STREAM;
            }

            const FlashManagerResultTransferStatus_T fetch_status =
                VAR_RESULT_PRODUCER_FetchFromFlash( stream );

            if ( fetch_status == FLASH_MANAGER_RESULT_TRANSFER_BUSY )
            {
                return RESULT_MESSAGE_PRODUCER_STATUS_NO_DATA_AVAILABLE;
            }

            if ( fetch_status == FLASH_MANAGER_RESULT_TRANSFER_END_OF_STREAM )
            {
                stream->is_flash_end_of_stream = true;
                continue;
            }

            if ( fetch_status != FLASH_MANAGER_RESULT_TRANSFER_OK )
            {
                return RESULT_MESSAGE_PRODUCER_STATUS_INTERNAL_ERROR;
            }

            continue;
        }

        // Complete record found in stream buffer
        if ( !stream->has_active_tick )
        {
            if ( header.timestamp == 0U )
            {
                return RESULT_MESSAGE_PRODUCER_STATUS_CORRUPT_DATA;
            }

            if ( stream->has_emitted_tick
                 && ( header.timestamp <= stream->last_emitted_timestamp ) )
            {
                return RESULT_MESSAGE_PRODUCER_STATUS_CORRUPT_DATA;
            }

            if ( stream->expected_tick_count_configured
                 && ( header.timestamp > ( stream->next_result_tick + 1U ) ) )
            {
                VAR_RESULT_PRODUCER_EmitEmptyTick( stream, out_message );
                return RESULT_MESSAGE_PRODUCER_STATUS_OK;
            }

            stream->has_active_tick       = true;
            stream->active_tick_number    = header.timestamp;
            stream->staged_record_count   = 0U;
            stream->staged_payload_offset = 0U;
        }

        if ( header.timestamp == stream->active_tick_number )
        {
            if ( !VAR_RESULT_PRODUCER_DispatchRecord( &header, payload, stream ) )
            {
                return RESULT_MESSAGE_PRODUCER_STATUS_CORRUPT_DATA;
            }

            VAR_RESULT_PRODUCER_ConsumeRecord( stream, header.payload_length_bytes );
        }
        else if ( header.timestamp > stream->active_tick_number )
        {
            /* Record belongs to a future tick; emit current staged tick */
            if ( stream->expected_tick_count_configured
                 && ( ( stream->active_tick_number - 1U ) >= stream->expected_tick_count ) )
            {
                stream->has_active_tick = false;
                continue;
            }
            out_message->type        = HIL_APPLICATION_MESSAGE_TYPE_VARIABLE_TEST_RESULT;
            out_message->subtype     = HIL_APPLICATION_MESSAGE_SUBTYPE_NONE;
            out_message->has_test_id = 1U;
            out_message->body.variable_test_result.tick_number = stream->active_tick_number - 1U;
            VAR_RESULT_PRODUCER_PopulateResultMetadata( stream, out_message );
            out_message->body.variable_test_result.flags =
                HIL_APPLICATION_RESULT_FLAG_COMPLETE_TICK;
            out_message->body.variable_test_result.record_count   = stream->staged_record_count;
            out_message->body.variable_test_result.records =
                ( stream->staged_record_count > 0U ) ? stream->staged_records : NULL;

            stream->has_emitted_tick       = true;
            stream->last_emitted_timestamp = stream->active_tick_number;
            stream->next_result_tick       = stream->active_tick_number;
            stream->has_active_tick        = false;
            return RESULT_MESSAGE_PRODUCER_STATUS_OK;
        }
        else
        {
            // Non-monotonic timestamp in flash stream
            return RESULT_MESSAGE_PRODUCER_STATUS_CORRUPT_DATA;
        }
    }
}

Result_Message_Producer_Status_T
VARIABLE_RESULT_MESSAGE_PRODUCER_ProduceNextMessage( HIL_Application_Message_T* const out_message )
{
    const Result_Message_Producer_Status_T status =
        VAR_RESULT_PRODUCER_ProduceNextMessageInternal( out_message );
    s_var_diagnostics.last_status = status;
    return status;
}

void VARIABLE_RESULT_MESSAGE_PRODUCER_GetDiagnostics(
    VariableResultProducerDiagnostics_T* const diags )
{
    if ( diags != NULL )
    {
        *diags                   = s_var_diagnostics;
        diags->buffered_bytes    = s_var_stream.write_offset - s_var_stream.read_offset;
        diags->read_offset       = s_var_stream.read_offset;
        diags->write_offset      = s_var_stream.write_offset;
        diags->active_tick_number = s_var_stream.active_tick_number;
        diags->next_result_tick  = s_var_stream.next_result_tick;
        diags->last_emitted_timestamp = s_var_stream.last_emitted_timestamp;
        diags->staged_record_count = ( uint8_t )s_var_stream.staged_record_count;
        diags->has_active_tick     = s_var_stream.has_active_tick;
        diags->has_emitted_tick    = s_var_stream.has_emitted_tick;
        diags->is_flash_end_of_stream = s_var_stream.is_flash_end_of_stream;
        diags->capture_overflow    = s_var_stream.capture_overflow;
    }
}
