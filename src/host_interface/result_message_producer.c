/******************************************************************************
 *  File:       result_message_producer.c
 *  Author:     Callum Rafferty
 *  Created:    15-Sep-2026
 *
 *  Description:
 *      Implementation of the result message producer for retrieving raw driver
 *      measurement records from Flash Manager, unpacking and aggregating them
 *      by tick timestamp, and constructing outbound Application Test Result
 *      protocol messages during the result transfer phase.
 *
 *  Notes:
 *      Reads packed [FlashManagerResultHeader_T][payload] records from Flash
 *      Manager, unpacks driver measurement payloads (Digital, Analogue, PWM capture),
 *      stubs unhandled peripheral streams (UART, SPI, CAN), and populates
 *      HIL_Application_Test_Result_T structures for host transmission.
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include "result_message_producer.h"
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
#define RESULT_PRODUCER_BUFFER_CAPACITY ( 1024U + sizeof( FlashManagerResultHeader_T ) )

/** @brief Timer input clock frequency for PWM capture (TIM2 and TIM5 on APB1). */
#define RESULT_PRODUCER_PWM_TIMER_CLOCK_HZ ( 90000000U )

/** @brief Nanoseconds per second constant for period conversion. */
#define RESULT_PRODUCER_NANOSECONDS_PER_SECOND ( 1000000000ULL )

/** @brief Permyriad scaling factor (100% = 10000). */
#define RESULT_PRODUCER_PERMYRIAD_SCALE ( 10000ULL )

/**-----------------------------------------------------------------------------
 *  Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

/**
 * @brief Internal stream state for managing flash reads and multi-record tick aggregation.
 */
typedef struct
{
    /** Staging buffer for streaming and reassembling record bytes. */
    uint8_t buffer[RESULT_PRODUCER_BUFFER_CAPACITY];

    /** Byte offset of the first unparsed byte in buffer. */
    size_t read_offset;

    /** Total valid bytes currently stored in buffer. */
    size_t write_offset;

    /** Set to true when Flash Manager signals end of stored result stream. */
    bool is_flash_end_of_stream;

    /** Sequential tick number (0..N-1) to emit to host. */
    uint32_t next_tick_number;

    /** Indicates whether an active tick message is currently being aggregated. */
    bool has_active_tick;

    /** Tick timestamp of the result message currently being aggregated. */
    uint32_t active_tick_number;

    /** Indicates whether at least one complete tick message has been emitted. */
    bool has_emitted_tick;

    /** Timestamp of the most recently emitted tick message. */
    uint32_t last_emitted_tick;

    /** Latched PWM input values held across ticks until updated. */
    HIL_Application_Pwm_Input_Value_T last_pwm_inputs[HIL_APPLICATION_PWM_INPUT_CHANNEL_COUNT];

    /** Partially or fully populated test result for active_tick_number. */
    HIL_Application_Test_Result_T staged_result;
} ResultProducerStream_T;

/**-----------------------------------------------------------------------------
 *  Public (global) and Extern Variables
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Private (static) Variables
 *------------------------------------------------------------------------------
 */

/** @brief Singleton stream context for result message production. */
static ResultProducerStream_T result_producer_stream;

/**
 * @brief Physical GPIOD pin masks indexed by zero-based protocol DI channel.
 *
 * Execution samples retain the GPIO port bit positions. The protocol exposes
 * digital inputs as a packed channel array, so the result boundary must map
 * physical DI1..DI10 to protocol channels 0..9.
 */
static const uint32_t
    RESULT_PRODUCER_DIGITAL_INPUT_PIN_MASKS[HIL_APPLICATION_DIGITAL_INPUT_CHANNEL_COUNT] = {
        1UL << 8U,  1UL << 9U, 1UL << 10U, 1UL << 11U, 1UL << 14U,
        1UL << 15U, 1UL << 0U, 1UL << 1U,  1UL << 2U,  1UL << 3U,
};

/**-----------------------------------------------------------------------------
 *  Private (static) Function Prototypes
 *------------------------------------------------------------------------------
 */

static FlashManagerResultTransferStatus_T
RESULT_PRODUCER_FetchFromFlash( ResultProducerStream_T* stream );

static bool RESULT_PRODUCER_PeekNextRecord( ResultProducerStream_T*     stream,
                                            FlashManagerResultHeader_T* header,
                                            const uint8_t**             payload );

static void RESULT_PRODUCER_ConsumeRecord( ResultProducerStream_T* stream,
                                           uint16_t                payload_length_bytes );

static bool RESULT_PRODUCER_DecodeDigitalInput( const uint8_t* payload, uint16_t length,
                                                HIL_Application_Test_Result_T* result );

static bool RESULT_PRODUCER_DecodeAnalogueInput( const uint8_t* payload, uint16_t length,
                                                 HIL_Application_Test_Result_T* result );

static bool RESULT_PRODUCER_DecodePwmCapture( uint8_t channel, const uint8_t* payload,
                                              uint16_t                       length,
                                              HIL_Application_Test_Result_T* result );

static bool RESULT_PRODUCER_DispatchRecord( const FlashManagerResultHeader_T* header,
                                            const uint8_t*                    payload,
                                            HIL_Application_Test_Result_T*    result );

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

/**
 * @brief Compacts buffer and fetches new result bytes from the Flash Manager.
 */
static FlashManagerResultTransferStatus_T
RESULT_PRODUCER_FetchFromFlash( ResultProducerStream_T* const stream )
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
        return FLASH_MANAGER_RESULT_TRANSFER_OK;
    }

    uint32_t                                 bytes_read = 0U;
    const FlashManagerResultTransferStatus_T status     = FLASH_MANAGER_ReadResultBytes(
        stream->buffer + stream->write_offset, ( uint32_t )capacity, &bytes_read );

    if ( ( status == FLASH_MANAGER_RESULT_TRANSFER_OK ) && ( bytes_read > 0U ) )
    {
        stream->write_offset += bytes_read;
    }

    return status;
}

/**
 * @brief Inspects whether a complete record is available in the stream buffer.
 */
static bool RESULT_PRODUCER_PeekNextRecord( ResultProducerStream_T* const     stream,
                                            FlashManagerResultHeader_T* const header,
                                            const uint8_t** const             payload )
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
static void RESULT_PRODUCER_ConsumeRecord( ResultProducerStream_T* const stream,
                                           const uint16_t                payload_length_bytes )
{
    stream->read_offset += sizeof( FlashManagerResultHeader_T ) + ( size_t )payload_length_bytes;
}

/**
 * @brief Unpacks a digital input 32-bit pinmask into protocol digital input values.
 */
static bool RESULT_PRODUCER_DecodeDigitalInput( const uint8_t* const payload, const uint16_t length,
                                                HIL_Application_Test_Result_T* const result )
{
    if ( ( payload == NULL ) || ( length != sizeof( uint32_t ) ) )
    {
        return false;
    }

    uint32_t pin_mask = 0U;
    ( void )memcpy( &pin_mask, payload, sizeof( pin_mask ) );

    for ( uint8_t channel = 0U; channel < HIL_APPLICATION_DIGITAL_INPUT_CHANNEL_COUNT; channel++ )
    {
        result->digital_inputs[channel].high =
            ( uint8_t )( ( pin_mask & RESULT_PRODUCER_DIGITAL_INPUT_PIN_MASKS[channel] ) != 0U );
    }

    return true;
}

/**
 * @brief Unpacks raw microvolt measurements for analogue input channels 0 and 1.
 */
static bool RESULT_PRODUCER_DecodeAnalogueInput( const uint8_t* const                 payload,
                                                 const uint16_t                       length,
                                                 HIL_Application_Test_Result_T* const result )
{
    if ( ( payload == NULL ) || ( length != ( 2U * sizeof( uint32_t ) ) ) )
    {
        return false;
    }

    uint32_t voltage_ch0 = 0U;
    uint32_t voltage_ch1 = 0U;

    ( void )memcpy( &voltage_ch0, payload, sizeof( uint32_t ) );
    ( void )memcpy( &voltage_ch1, payload + sizeof( uint32_t ), sizeof( uint32_t ) );

    result->analog_inputs[0].microvolts = voltage_ch0;
    result->analog_inputs[1].microvolts = voltage_ch1;

    return true;
}

/**
 * @brief Unpacks timer ticks into physical period nanoseconds and duty cycle permyriad.
 */
static bool RESULT_PRODUCER_DecodePwmCapture( const uint8_t channel, const uint8_t* const payload,
                                              const uint16_t                       length,
                                              HIL_Application_Test_Result_T* const result )
{
    if ( ( payload == NULL ) || ( channel >= HIL_APPLICATION_PWM_INPUT_CHANNEL_COUNT )
         || ( length != ( 2U * sizeof( uint32_t ) ) ) )
    {
        return false;
    }

    uint32_t period_ticks = 0U;
    uint32_t high_ticks   = 0U;

    ( void )memcpy( &period_ticks, payload, sizeof( uint32_t ) );
    ( void )memcpy( &high_ticks, payload + sizeof( uint32_t ), sizeof( uint32_t ) );

    if ( period_ticks == 0U )
    {
        result->pwm_inputs[channel].period_nanoseconds   = 0U;
        result->pwm_inputs[channel].duty_cycle_permyriad = 0U;
        result_producer_stream.last_pwm_inputs[channel]  = result->pwm_inputs[channel];
        return true;
    }

    if ( high_ticks > period_ticks )
    {
        return false;
    }

    /*
     * In hardware Slave-Reset mode, the STM32 timer slave-mode controller takes 2 timer clock
     * cycles to resynchronize the trigger and reset the counter on the rising edge.
     * Therefore, both the captured period and high time are reduced by exactly 2 timer clock
     * counts. We add 2 clock counts to restore the true physical pulse duration.
     */
    const uint32_t corrected_period_ticks = period_ticks + 2U;
    const uint32_t corrected_high_ticks   = high_ticks + 2U;

    const uint64_t period_ns =
        ( ( uint64_t )corrected_period_ticks * RESULT_PRODUCER_NANOSECONDS_PER_SECOND )
        / RESULT_PRODUCER_PWM_TIMER_CLOCK_HZ;
    const uint64_t duty_permyriad =
        ( ( uint64_t )corrected_high_ticks * RESULT_PRODUCER_PERMYRIAD_SCALE )
        / corrected_period_ticks;

    result->pwm_inputs[channel].period_nanoseconds   = ( uint32_t )period_ns;
    result->pwm_inputs[channel].duty_cycle_permyriad = ( uint16_t )duty_permyriad;
    result_producer_stream.last_pwm_inputs[channel]  = result->pwm_inputs[channel];

    return true;
}

/**
 * @brief Dispatches a flash result record to the corresponding peripheral decoder.
 */
static bool RESULT_PRODUCER_DispatchRecord( const FlashManagerResultHeader_T* const header,
                                            const uint8_t* const                    payload,
                                            HIL_Application_Test_Result_T* const    result )
{
    switch ( header->peripheral_type )
    {
        case FLASH_MANAGER_RESULT_PERIPHERAL_DIGITAL_INPUT:
            return RESULT_PRODUCER_DecodeDigitalInput( payload, header->payload_length_bytes,
                                                       result );

        case FLASH_MANAGER_RESULT_PERIPHERAL_ANALOGUE_INPUT:
            return RESULT_PRODUCER_DecodeAnalogueInput( payload, header->payload_length_bytes,
                                                        result );

        case FLASH_MANAGER_RESULT_PERIPHERAL_PWM_CAPTURE:
            return RESULT_PRODUCER_DecodePwmCapture( header->channel, payload,
                                                     header->payload_length_bytes, result );

        default:
            return false;
    }
}

/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */

/**
 * @brief Resets the result message producer state and internal stream reader.
 */
void RESULT_MESSAGE_PRODUCER_Reset( void )
{
    ( void )memset( &result_producer_stream, 0, sizeof( result_producer_stream ) );
}

/**
 * @brief Retrieves raw measurement record(s) from Flash Manager and constructs
 *        the next Application Test Result protocol message.
 */
Result_Message_Producer_Status_T
RESULT_MESSAGE_PRODUCER_ProduceNextMessage( HIL_Application_Message_T* const out_message )
{
    if ( out_message == NULL )
    {
        return RESULT_MESSAGE_PRODUCER_STATUS_INVALID_ARGUMENT;
    }

    ResultProducerStream_T* const stream = &result_producer_stream;

    while ( 1 )
    {
        FlashManagerResultHeader_T header;
        const uint8_t*             payload = NULL;

        if ( !RESULT_PRODUCER_PeekNextRecord( stream, &header, &payload ) )
        {
            if ( stream->is_flash_end_of_stream )
            {
                if ( stream->has_active_tick )
                {
                    // Emit the final aggregated tick
                    out_message->type             = HIL_APPLICATION_MESSAGE_TYPE_TEST_RESULT;
                    out_message->subtype          = HIL_APPLICATION_MESSAGE_SUBTYPE_NONE;
                    out_message->has_test_id      = 1U;
                    out_message->body.test_result = stream->staged_result;

                    stream->has_emitted_tick  = true;
                    stream->last_emitted_tick = stream->active_tick_number;
                    stream->next_tick_number++;
                    stream->has_active_tick = false;
                    return RESULT_MESSAGE_PRODUCER_STATUS_OK;
                }

                return RESULT_MESSAGE_PRODUCER_STATUS_END_OF_STREAM;
            }

            const FlashManagerResultTransferStatus_T fetch_status =
                RESULT_PRODUCER_FetchFromFlash( stream );

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
            /*
             * Execution boundaries are stored as one-based timestamps (1..N),
             * while Application result ticks are zero-based (0..N-1).
             */
            if ( header.timestamp == 0U )
            {
                return RESULT_MESSAGE_PRODUCER_STATUS_CORRUPT_DATA;
            }

            stream->has_active_tick    = true;
            stream->active_tick_number = stream->next_tick_number + 1U;
            ( void )memset( &stream->staged_result, 0, sizeof( stream->staged_result ) );
            stream->staged_result.tick_number = stream->next_tick_number;
            stream->staged_result.condition   = HIL_APPLICATION_RESULT_CONDITION_OK;
            /* Latch latest PWM input measurements into the new tick */
            for ( uint8_t ch = 0U; ch < HIL_APPLICATION_PWM_INPUT_CHANNEL_COUNT; ch++ )
            {
                stream->staged_result.pwm_inputs[ch] = stream->last_pwm_inputs[ch];
            }
        }

        if ( header.timestamp == stream->active_tick_number )
        {
            if ( !RESULT_PRODUCER_DispatchRecord( &header, payload, &stream->staged_result ) )
            {
                return RESULT_MESSAGE_PRODUCER_STATUS_CORRUPT_DATA;
            }

            RESULT_PRODUCER_ConsumeRecord( stream, header.payload_length_bytes );
        }
        else if ( header.timestamp > stream->active_tick_number )
        {
            /* Record belongs to a future tick; emit current tick and advance sequential counter */
            out_message->type             = HIL_APPLICATION_MESSAGE_TYPE_TEST_RESULT;
            out_message->subtype          = HIL_APPLICATION_MESSAGE_SUBTYPE_NONE;
            out_message->has_test_id      = 1U;
            out_message->body.test_result = stream->staged_result;

            stream->has_emitted_tick  = true;
            stream->last_emitted_tick = stream->active_tick_number;
            stream->next_tick_number++;
            stream->has_active_tick = false;
            return RESULT_MESSAGE_PRODUCER_STATUS_OK;
        }
        else
        {
            // Non-monotonic timestamp in flash stream
            return RESULT_MESSAGE_PRODUCER_STATUS_CORRUPT_DATA;
        }
    }
}
