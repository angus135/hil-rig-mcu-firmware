/******************************************************************************
 *  File:       test_result_message_producer.cpp
 *  Author:     Callum Rafferty
 *  Created:    15-Sep-2026
 *
 *  Description:
 *      Unit tests and white-box tests for the Result Message Producer pipeline
 *      using GoogleTest and GoogleMock.
 *
 *      These tests verify:
 *        - Parameter validation (NULL out_message pointer).
 *        - Reset behavior and stream reinitialization.
 *        - Flash Manager status translation (BUSY, END_OF_STREAM, INTERNAL_ERROR).
 *        - Peripheral measurement decoding (Digital, Analogue, PWM capture).
 *        - Multi-record tick aggregation and future-tick peeking/preservation.
 *        - Split-chunk streaming reassembly across Flash read boundaries.
 *        - Corruption detection (non-monotonic ticks, invalid payload lengths,
 *          PWM high ticks > period ticks, unrecognized peripheral types).
 *        - Peripheral expansion stubs (UART, SPI, CAN receive).
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <vector>

extern "C"
{
#include "flash_manager/flash_manager.h"
#include "hil_rig_protocol/application/application_message.h"
#include "result_message_producer.h"

/* Direct include of .c for white-box testing of internal stream state */
#include "result_message_producer.c"  // NOLINT
}

using ::testing::_;
using ::testing::DoAll;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SetArgPointee;

/**-----------------------------------------------------------------------------
 *  Test Doubles / Mocks
 *------------------------------------------------------------------------------
 */

class MockFlashManagerResultDependencies
{
public:
    virtual ~MockFlashManagerResultDependencies() = default;

    MOCK_METHOD( FlashManagerResultTransferStatus_T, FLASH_MANAGER_ReadResultBytes,
                 ( uint8_t * destination, uint32_t destination_capacity_bytes,
                   uint32_t* bytes_read ) );
};

static MockFlashManagerResultDependencies* g_mock_flash_manager = nullptr;

/**-----------------------------------------------------------------------------
 *  Link Seam: Mocked C Function Definition
 *------------------------------------------------------------------------------
 */

extern "C" FlashManagerResultTransferStatus_T
FLASH_MANAGER_ReadResultBytes( uint8_t* destination, uint32_t destination_capacity_bytes,
                               uint32_t* bytes_read )
{
    if ( g_mock_flash_manager != nullptr )
    {
        return g_mock_flash_manager->FLASH_MANAGER_ReadResultBytes( destination,
                                                                    destination_capacity_bytes,
                                                                    bytes_read );
    }
    if ( bytes_read != nullptr )
    {
        *bytes_read = 0U;
    }
    return FLASH_MANAGER_RESULT_TRANSFER_END_OF_STREAM;
}

/**-----------------------------------------------------------------------------
 *  Helper Stream Injector
 *------------------------------------------------------------------------------
 */

class SimulatedFlashResultStream
{
public:
    void Clear()
    {
        stream_bytes_.clear();
        read_cursor_ = 0;
    }

    void AppendRecord( uint32_t timestamp, uint8_t peripheral_type, uint8_t channel,
                       const void* payload, uint16_t payload_length )
    {
        FlashManagerResultHeader_T header;
        header.timestamp            = timestamp;
        header.payload_length_bytes = payload_length;
        header.peripheral_type      = peripheral_type;
        header.channel              = channel;

        const auto* header_bytes = reinterpret_cast<const uint8_t*>( &header );
        stream_bytes_.insert( stream_bytes_.end(), header_bytes,
                              header_bytes + sizeof( header ) );

        if ( ( payload != nullptr ) && ( payload_length > 0 ) )
        {
            const auto* payload_bytes = reinterpret_cast<const uint8_t*>( payload );
            stream_bytes_.insert( stream_bytes_.end(), payload_bytes,
                                  payload_bytes + payload_length );
        }
    }

    FlashManagerResultTransferStatus_T ReadChunk( uint8_t* destination, uint32_t capacity,
                                                  uint32_t* bytes_read,
                                                  uint32_t  max_chunk_size = 0 )
    {
        if ( destination == nullptr || bytes_read == nullptr )
        {
            return FLASH_MANAGER_RESULT_TRANSFER_INVALID_ARGUMENT;
        }

        if ( read_cursor_ >= stream_bytes_.size() )
        {
            *bytes_read = 0;
            return FLASH_MANAGER_RESULT_TRANSFER_END_OF_STREAM;
        }

        uint32_t available = static_cast<uint32_t>( stream_bytes_.size() - read_cursor_ );
        uint32_t to_copy   = ( available < capacity ) ? available : capacity;

        if ( ( max_chunk_size > 0 ) && ( to_copy > max_chunk_size ) )
        {
            to_copy = max_chunk_size;
        }

        std::memcpy( destination, stream_bytes_.data() + read_cursor_, to_copy );
        read_cursor_ += to_copy;
        *bytes_read = to_copy;

        return FLASH_MANAGER_RESULT_TRANSFER_OK;
    }

private:
    std::vector<uint8_t> stream_bytes_;
    size_t               read_cursor_ = 0;
};

/**-----------------------------------------------------------------------------
 *  Test Fixture
 *------------------------------------------------------------------------------
 */

class ResultMessageProducerTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        mock_flash_ = std::make_unique<NiceMock<MockFlashManagerResultDependencies>>();
        g_mock_flash_manager = mock_flash_.get();
        simulated_stream_.Clear();
        RESULT_MESSAGE_PRODUCER_Reset();
    }

    void TearDown() override
    {
        g_mock_flash_manager = nullptr;
        mock_flash_.reset();
    }

    std::unique_ptr<NiceMock<MockFlashManagerResultDependencies>> mock_flash_;
    SimulatedFlashResultStream                                   simulated_stream_;
};

/**-----------------------------------------------------------------------------
 *  Tests: Parameter Validation & Lifecycle
 *------------------------------------------------------------------------------
 */

TEST_F( ResultMessageProducerTest, NullArgumentReturnsInvalidArgument )
{
    EXPECT_EQ( RESULT_MESSAGE_PRODUCER_ProduceNextMessage( nullptr ),
               RESULT_MESSAGE_PRODUCER_STATUS_INVALID_ARGUMENT );
}

TEST_F( ResultMessageProducerTest, ResetClearsStreamState )
{
    result_producer_stream.write_offset           = 100;
    result_producer_stream.read_offset            = 50;
    result_producer_stream.has_active_tick        = true;
    result_producer_stream.active_tick_number     = 42;
    result_producer_stream.is_flash_end_of_stream = true;

    RESULT_MESSAGE_PRODUCER_Reset();

    EXPECT_EQ( result_producer_stream.write_offset, 0U );
    EXPECT_EQ( result_producer_stream.read_offset, 0U );
    EXPECT_FALSE( result_producer_stream.has_active_tick );
    EXPECT_EQ( result_producer_stream.active_tick_number, 0U );
    EXPECT_FALSE( result_producer_stream.is_flash_end_of_stream );
}

/**-----------------------------------------------------------------------------
 *  Tests: Flash Manager Status Translation
 *------------------------------------------------------------------------------
 */

TEST_F( ResultMessageProducerTest, FlashBusyReturnsNoDataAvailable )
{
    EXPECT_CALL( *mock_flash_, FLASH_MANAGER_ReadResultBytes( _, _, _ ) )
        .WillOnce( DoAll( SetArgPointee<2>( 0U ), Return( FLASH_MANAGER_RESULT_TRANSFER_BUSY ) ) );

    HIL_Application_Message_T message;
    std::memset( &message, 0, sizeof( message ) );

    EXPECT_EQ( RESULT_MESSAGE_PRODUCER_ProduceNextMessage( &message ),
               RESULT_MESSAGE_PRODUCER_STATUS_NO_DATA_AVAILABLE );
}

TEST_F( ResultMessageProducerTest, EmptyFlashStreamReturnsEndOfStream )
{
    EXPECT_CALL( *mock_flash_, FLASH_MANAGER_ReadResultBytes( _, _, _ ) )
        .WillOnce(
            DoAll( SetArgPointee<2>( 0U ), Return( FLASH_MANAGER_RESULT_TRANSFER_END_OF_STREAM ) ) );

    HIL_Application_Message_T message;
    std::memset( &message, 0, sizeof( message ) );

    EXPECT_EQ( RESULT_MESSAGE_PRODUCER_ProduceNextMessage( &message ),
               RESULT_MESSAGE_PRODUCER_STATUS_END_OF_STREAM );
}

TEST_F( ResultMessageProducerTest, FlashInternalErrorReturnsInternalError )
{
    EXPECT_CALL( *mock_flash_, FLASH_MANAGER_ReadResultBytes( _, _, _ ) )
        .WillOnce( DoAll( SetArgPointee<2>( 0U ),
                          Return( FLASH_MANAGER_RESULT_TRANSFER_INTERNAL_ERROR ) ) );

    HIL_Application_Message_T message;
    std::memset( &message, 0, sizeof( message ) );

    EXPECT_EQ( RESULT_MESSAGE_PRODUCER_ProduceNextMessage( &message ),
               RESULT_MESSAGE_PRODUCER_STATUS_INTERNAL_ERROR );
}

/**-----------------------------------------------------------------------------
 *  Tests: Peripheral Measurement Decoding
 *------------------------------------------------------------------------------
 */

TEST_F( ResultMessageProducerTest, DecodeDigitalInputRecord )
{
    // Pin mask: pin 0, 2, 4, 8 high -> 0x0115
    const uint32_t digital_mask = ( 1U << 0 ) | ( 1U << 2 ) | ( 1U << 4 ) | ( 1U << 8 );
    simulated_stream_.AppendRecord( 0U, FLASH_MANAGER_RESULT_PERIPHERAL_DIGITAL_INPUT, 0U,
                                   &digital_mask, sizeof( digital_mask ) );

    EXPECT_CALL( *mock_flash_, FLASH_MANAGER_ReadResultBytes( _, _, _ ) )
        .WillRepeatedly( [this]( uint8_t* dest, uint32_t cap, uint32_t* read ) {
            return simulated_stream_.ReadChunk( dest, cap, read );
        } );

    HIL_Application_Message_T message;
    std::memset( &message, 0, sizeof( message ) );

    EXPECT_EQ( RESULT_MESSAGE_PRODUCER_ProduceNextMessage( &message ),
               RESULT_MESSAGE_PRODUCER_STATUS_OK );

    EXPECT_EQ( message.type, HIL_APPLICATION_MESSAGE_TYPE_TEST_RESULT );
    EXPECT_EQ( message.subtype, HIL_APPLICATION_MESSAGE_SUBTYPE_NONE );
    EXPECT_EQ( message.has_test_id, 1U );
    EXPECT_EQ( message.body.test_result.tick_number, 0U );
    EXPECT_EQ( message.body.test_result.condition, HIL_APPLICATION_RESULT_CONDITION_OK );

    EXPECT_EQ( message.body.test_result.digital_inputs[0].high, 1U );
    EXPECT_EQ( message.body.test_result.digital_inputs[1].high, 0U );
    EXPECT_EQ( message.body.test_result.digital_inputs[2].high, 1U );
    EXPECT_EQ( message.body.test_result.digital_inputs[3].high, 0U );
    EXPECT_EQ( message.body.test_result.digital_inputs[4].high, 1U );
    EXPECT_EQ( message.body.test_result.digital_inputs[8].high, 1U );
    EXPECT_EQ( message.body.test_result.digital_inputs[9].high, 0U );
}

TEST_F( ResultMessageProducerTest, DecodeAnalogueInputRecord )
{
    const uint32_t voltages[2] = { 1250000U, 3300000U };
    simulated_stream_.AppendRecord( 1U, FLASH_MANAGER_RESULT_PERIPHERAL_ANALOGUE_INPUT, 0U,
                                   voltages, sizeof( voltages ) );

    EXPECT_CALL( *mock_flash_, FLASH_MANAGER_ReadResultBytes( _, _, _ ) )
        .WillRepeatedly( [this]( uint8_t* dest, uint32_t cap, uint32_t* read ) {
            return simulated_stream_.ReadChunk( dest, cap, read );
        } );

    HIL_Application_Message_T message;
    std::memset( &message, 0, sizeof( message ) );

    EXPECT_EQ( RESULT_MESSAGE_PRODUCER_ProduceNextMessage( &message ),
               RESULT_MESSAGE_PRODUCER_STATUS_OK );

    EXPECT_EQ( message.body.test_result.tick_number, 1U );
    EXPECT_EQ( message.body.test_result.analog_inputs[0].microvolts, 1250000U );
    EXPECT_EQ( message.body.test_result.analog_inputs[1].microvolts, 3300000U );
}

TEST_F( ResultMessageProducerTest, DecodePwmCaptureRecords )
{
    // Channel 0: 90000 ticks period (1ms @ 90MHz), 45000 ticks high (50% = 5000 permyriad)
    const uint32_t pwm_ch0[2] = { 90000U, 45000U };
    simulated_stream_.AppendRecord( 2U, FLASH_MANAGER_RESULT_PERIPHERAL_PWM_CAPTURE, 0U,
                                   pwm_ch0, sizeof( pwm_ch0 ) );

    // Channel 1: 180000 ticks period (2ms @ 90MHz), 18000 ticks high (10% = 1000 permyriad)
    const uint32_t pwm_ch1[2] = { 180000U, 18000U };
    simulated_stream_.AppendRecord( 2U, FLASH_MANAGER_RESULT_PERIPHERAL_PWM_CAPTURE, 1U,
                                   pwm_ch1, sizeof( pwm_ch1 ) );

    EXPECT_CALL( *mock_flash_, FLASH_MANAGER_ReadResultBytes( _, _, _ ) )
        .WillRepeatedly( [this]( uint8_t* dest, uint32_t cap, uint32_t* read ) {
            return simulated_stream_.ReadChunk( dest, cap, read );
        } );

    HIL_Application_Message_T message;
    std::memset( &message, 0, sizeof( message ) );

    EXPECT_EQ( RESULT_MESSAGE_PRODUCER_ProduceNextMessage( &message ),
               RESULT_MESSAGE_PRODUCER_STATUS_OK );

    EXPECT_EQ( message.body.test_result.tick_number, 2U );
    EXPECT_EQ( message.body.test_result.pwm_inputs[0].period_nanoseconds, 1000000U );
    EXPECT_EQ( message.body.test_result.pwm_inputs[0].duty_cycle_permyriad, 5000U );
    EXPECT_EQ( message.body.test_result.pwm_inputs[1].period_nanoseconds, 2000000U );
    EXPECT_EQ( message.body.test_result.pwm_inputs[1].duty_cycle_permyriad, 1000U );
}

TEST_F( ResultMessageProducerTest, DecodePwmCaptureZeroTicksYieldsZeroOutput )
{
    const uint32_t pwm_zero[2] = { 0U, 0U };
    simulated_stream_.AppendRecord( 0U, FLASH_MANAGER_RESULT_PERIPHERAL_PWM_CAPTURE, 0U,
                                   pwm_zero, sizeof( pwm_zero ) );

    EXPECT_CALL( *mock_flash_, FLASH_MANAGER_ReadResultBytes( _, _, _ ) )
        .WillRepeatedly( [this]( uint8_t* dest, uint32_t cap, uint32_t* read ) {
            return simulated_stream_.ReadChunk( dest, cap, read );
        } );

    HIL_Application_Message_T message;
    std::memset( &message, 0, sizeof( message ) );

    EXPECT_EQ( RESULT_MESSAGE_PRODUCER_ProduceNextMessage( &message ),
               RESULT_MESSAGE_PRODUCER_STATUS_OK );

    EXPECT_EQ( message.body.test_result.pwm_inputs[0].period_nanoseconds, 0U );
    EXPECT_EQ( message.body.test_result.pwm_inputs[0].duty_cycle_permyriad, 0U );
}

/**-----------------------------------------------------------------------------
 *  Tests: Multi-Record Tick Aggregation & Multi-Tick Sequences
 *------------------------------------------------------------------------------
 */

TEST_F( ResultMessageProducerTest, AggregatesMultipleRecordsInSingleTick )
{
    // Tick 0: Digital, Analogue, and PWM
    const uint32_t digital_mask = 0x03;
    const uint32_t voltages[2]  = { 500000U, 1000000U };
    const uint32_t pwm[2]       = { 90000U, 90000U };  // 100% duty = 10000 permyriad

    simulated_stream_.AppendRecord( 0U, FLASH_MANAGER_RESULT_PERIPHERAL_DIGITAL_INPUT, 0U,
                                   &digital_mask, sizeof( digital_mask ) );
    simulated_stream_.AppendRecord( 0U, FLASH_MANAGER_RESULT_PERIPHERAL_ANALOGUE_INPUT, 0U,
                                   voltages, sizeof( voltages ) );
    simulated_stream_.AppendRecord( 0U, FLASH_MANAGER_RESULT_PERIPHERAL_PWM_CAPTURE, 0U, pwm,
                                   sizeof( pwm ) );

    // Tick 1: Digital
    const uint32_t next_digital = 0x00;
    simulated_stream_.AppendRecord( 1U, FLASH_MANAGER_RESULT_PERIPHERAL_DIGITAL_INPUT, 0U,
                                   &next_digital, sizeof( next_digital ) );

    EXPECT_CALL( *mock_flash_, FLASH_MANAGER_ReadResultBytes( _, _, _ ) )
        .WillRepeatedly( [this]( uint8_t* dest, uint32_t cap, uint32_t* read ) {
            return simulated_stream_.ReadChunk( dest, cap, read );
        } );

    // 1. First call: produces Tick 0 with all three peripherals combined
    HIL_Application_Message_T msg0;
    std::memset( &msg0, 0, sizeof( msg0 ) );

    EXPECT_EQ( RESULT_MESSAGE_PRODUCER_ProduceNextMessage( &msg0 ),
               RESULT_MESSAGE_PRODUCER_STATUS_OK );
    EXPECT_EQ( msg0.body.test_result.tick_number, 0U );
    EXPECT_EQ( msg0.body.test_result.digital_inputs[0].high, 1U );
    EXPECT_EQ( msg0.body.test_result.digital_inputs[1].high, 1U );
    EXPECT_EQ( msg0.body.test_result.analog_inputs[0].microvolts, 500000U );
    EXPECT_EQ( msg0.body.test_result.analog_inputs[1].microvolts, 1000000U );
    EXPECT_EQ( msg0.body.test_result.pwm_inputs[0].period_nanoseconds, 1000000U );
    EXPECT_EQ( msg0.body.test_result.pwm_inputs[0].duty_cycle_permyriad, 10000U );

    // 2. Second call: produces Tick 1
    HIL_Application_Message_T msg1;
    std::memset( &msg1, 0, sizeof( msg1 ) );

    EXPECT_EQ( RESULT_MESSAGE_PRODUCER_ProduceNextMessage( &msg1 ),
               RESULT_MESSAGE_PRODUCER_STATUS_OK );
    EXPECT_EQ( msg1.body.test_result.tick_number, 1U );
    EXPECT_EQ( msg1.body.test_result.digital_inputs[0].high, 0U );

    // 3. Third call: stream is exhausted -> END_OF_STREAM
    HIL_Application_Message_T msg2;
    std::memset( &msg2, 0, sizeof( msg2 ) );

    EXPECT_EQ( RESULT_MESSAGE_PRODUCER_ProduceNextMessage( &msg2 ),
               RESULT_MESSAGE_PRODUCER_STATUS_END_OF_STREAM );
}

/**-----------------------------------------------------------------------------
 *  Tests: Streaming & Chunk Boundary Reassembly
 *------------------------------------------------------------------------------
 */

TEST_F( ResultMessageProducerTest, ReassemblesRecordSplitAcrossSmallChunks )
{
    const uint32_t voltages[2] = { 2500000U, 1800000U };
    simulated_stream_.AppendRecord( 10U, FLASH_MANAGER_RESULT_PERIPHERAL_ANALOGUE_INPUT, 0U,
                                   voltages, sizeof( voltages ) );

    // Read in 3-byte chunks to force split headers and split payloads
    EXPECT_CALL( *mock_flash_, FLASH_MANAGER_ReadResultBytes( _, _, _ ) )
        .WillRepeatedly( [this]( uint8_t* dest, uint32_t cap, uint32_t* read ) {
            return simulated_stream_.ReadChunk( dest, cap, read, 3U );
        } );

    HIL_Application_Message_T message;
    std::memset( &message, 0, sizeof( message ) );

    EXPECT_EQ( RESULT_MESSAGE_PRODUCER_ProduceNextMessage( &message ),
               RESULT_MESSAGE_PRODUCER_STATUS_OK );
    EXPECT_EQ( message.body.test_result.tick_number, 10U );
    EXPECT_EQ( message.body.test_result.analog_inputs[0].microvolts, 2500000U );
    EXPECT_EQ( message.body.test_result.analog_inputs[1].microvolts, 1800000U );
}

/**-----------------------------------------------------------------------------
 *  Tests: Error Handling & Corruption Detection
 *------------------------------------------------------------------------------
 */

TEST_F( ResultMessageProducerTest, InvalidDigitalPayloadLengthTriggersCorruptData )
{
    // Digital input with 3 bytes instead of 4
    const uint8_t bad_payload[3] = { 0x01, 0x02, 0x03 };
    simulated_stream_.AppendRecord( 0U, FLASH_MANAGER_RESULT_PERIPHERAL_DIGITAL_INPUT, 0U,
                                   bad_payload, sizeof( bad_payload ) );

    EXPECT_CALL( *mock_flash_, FLASH_MANAGER_ReadResultBytes( _, _, _ ) )
        .WillRepeatedly( [this]( uint8_t* dest, uint32_t cap, uint32_t* read ) {
            return simulated_stream_.ReadChunk( dest, cap, read );
        } );

    HIL_Application_Message_T message;
    EXPECT_EQ( RESULT_MESSAGE_PRODUCER_ProduceNextMessage( &message ),
               RESULT_MESSAGE_PRODUCER_STATUS_CORRUPT_DATA );
}

TEST_F( ResultMessageProducerTest, PwmHighTicksGreaterThanPeriodTicksTriggersCorruptData )
{
    // high_ticks (100) > period_ticks (50)
    const uint32_t invalid_pwm[2] = { 50U, 100U };
    simulated_stream_.AppendRecord( 0U, FLASH_MANAGER_RESULT_PERIPHERAL_PWM_CAPTURE, 0U,
                                   invalid_pwm, sizeof( invalid_pwm ) );

    EXPECT_CALL( *mock_flash_, FLASH_MANAGER_ReadResultBytes( _, _, _ ) )
        .WillRepeatedly( [this]( uint8_t* dest, uint32_t cap, uint32_t* read ) {
            return simulated_stream_.ReadChunk( dest, cap, read );
        } );

    HIL_Application_Message_T message;
    EXPECT_EQ( RESULT_MESSAGE_PRODUCER_ProduceNextMessage( &message ),
               RESULT_MESSAGE_PRODUCER_STATUS_CORRUPT_DATA );
}

TEST_F( ResultMessageProducerTest, NonMonotonicTimestampInBufferTriggersCorruptData )
{
    const uint32_t digital_mask = 0U;
    simulated_stream_.AppendRecord( 5U, FLASH_MANAGER_RESULT_PERIPHERAL_DIGITAL_INPUT, 0U,
                                   &digital_mask, sizeof( digital_mask ) );
    simulated_stream_.AppendRecord( 3U, FLASH_MANAGER_RESULT_PERIPHERAL_DIGITAL_INPUT, 0U,
                                   &digital_mask, sizeof( digital_mask ) );

    EXPECT_CALL( *mock_flash_, FLASH_MANAGER_ReadResultBytes( _, _, _ ) )
        .WillRepeatedly( [this]( uint8_t* dest, uint32_t cap, uint32_t* read ) {
            return simulated_stream_.ReadChunk( dest, cap, read );
        } );

    HIL_Application_Message_T msg0;
    // Tick 3 behind Tick 5 in stream buffer triggers CORRUPT_DATA immediately
    EXPECT_EQ( RESULT_MESSAGE_PRODUCER_ProduceNextMessage( &msg0 ),
               RESULT_MESSAGE_PRODUCER_STATUS_CORRUPT_DATA );
}

TEST_F( ResultMessageProducerTest, NonMonotonicTimestampAcrossSeparateFetchesTriggersCorruptData )
{
    const uint32_t digital_mask = 0U;
    const uint32_t record_len   = sizeof( FlashManagerResultHeader_T ) + sizeof( digital_mask );

    simulated_stream_.AppendRecord( 5U, FLASH_MANAGER_RESULT_PERIPHERAL_DIGITAL_INPUT, 0U,
                                   &digital_mask, sizeof( digital_mask ) );
    simulated_stream_.AppendRecord( 3U, FLASH_MANAGER_RESULT_PERIPHERAL_DIGITAL_INPUT, 0U,
                                   &digital_mask, sizeof( digital_mask ) );

    // Deliver exactly record 1 on first fetch, then simulate BUSY so tick 5 is emitted, then deliver record 2
    int call_count = 0;
    EXPECT_CALL( *mock_flash_, FLASH_MANAGER_ReadResultBytes( _, _, _ ) )
        .WillRepeatedly( [this, record_len, &call_count]( uint8_t* dest, uint32_t cap,
                                                          uint32_t* read ) {
            call_count++;
            if ( call_count == 1 )
            {
                return simulated_stream_.ReadChunk( dest, cap, read, record_len );
            }
            if ( call_count == 2 )
            {
                *read = 0;
                return FLASH_MANAGER_RESULT_TRANSFER_BUSY;
            }
            return simulated_stream_.ReadChunk( dest, cap, read );
        } );

    HIL_Application_Message_T msg0;
    // Call 1 reads tick 5, encounters BUSY on next chunk, and returns NO_DATA_AVAILABLE
    EXPECT_EQ( RESULT_MESSAGE_PRODUCER_ProduceNextMessage( &msg0 ),
               RESULT_MESSAGE_PRODUCER_STATUS_NO_DATA_AVAILABLE );

    // Call 2 reads tick 3, detects 3 <= 5 (non-monotonic) and returns CORRUPT_DATA
    HIL_Application_Message_T msg1;
    EXPECT_EQ( RESULT_MESSAGE_PRODUCER_ProduceNextMessage( &msg1 ),
               RESULT_MESSAGE_PRODUCER_STATUS_CORRUPT_DATA );
}

TEST_F( ResultMessageProducerTest, NonMonotonicTimestampAfterEmittedTickTriggersCorruptData )
{
    const uint32_t digital_mask = 0U;

    // Stream contains Tick 5, Tick 7, and then Tick 6 (violating monotonicity after Tick 7)
    simulated_stream_.AppendRecord( 5U, FLASH_MANAGER_RESULT_PERIPHERAL_DIGITAL_INPUT, 0U,
                                   &digital_mask, sizeof( digital_mask ) );
    simulated_stream_.AppendRecord( 7U, FLASH_MANAGER_RESULT_PERIPHERAL_DIGITAL_INPUT, 0U,
                                   &digital_mask, sizeof( digital_mask ) );
    simulated_stream_.AppendRecord( 6U, FLASH_MANAGER_RESULT_PERIPHERAL_DIGITAL_INPUT, 0U,
                                   &digital_mask, sizeof( digital_mask ) );

    EXPECT_CALL( *mock_flash_, FLASH_MANAGER_ReadResultBytes( _, _, _ ) )
        .WillRepeatedly( [this]( uint8_t* dest, uint32_t cap, uint32_t* read ) {
            return simulated_stream_.ReadChunk( dest, cap, read );
        } );

    HIL_Application_Message_T msg0;
    // Call 1 emits tick 5 (since it peeks tick 7 and sees 7 > 5)
    EXPECT_EQ( RESULT_MESSAGE_PRODUCER_ProduceNextMessage( &msg0 ),
               RESULT_MESSAGE_PRODUCER_STATUS_OK );
    EXPECT_EQ( msg0.body.test_result.tick_number, 5U );

    // Call 2 processes tick 7, peeks tick 6 (6 <= 7), detects monotonicity violation and returns CORRUPT_DATA
    HIL_Application_Message_T msg1;
    EXPECT_EQ( RESULT_MESSAGE_PRODUCER_ProduceNextMessage( &msg1 ),
               RESULT_MESSAGE_PRODUCER_STATUS_CORRUPT_DATA );
}

TEST_F( ResultMessageProducerTest, UnknownPeripheralTypeTriggersCorruptData )
{
    const uint32_t dummy = 0U;
    simulated_stream_.AppendRecord( 0U, 99U, 0U, &dummy, sizeof( dummy ) );

    EXPECT_CALL( *mock_flash_, FLASH_MANAGER_ReadResultBytes( _, _, _ ) )
        .WillRepeatedly( [this]( uint8_t* dest, uint32_t cap, uint32_t* read ) {
            return simulated_stream_.ReadChunk( dest, cap, read );
        } );

    HIL_Application_Message_T message;
    EXPECT_EQ( RESULT_MESSAGE_PRODUCER_ProduceNextMessage( &message ),
               RESULT_MESSAGE_PRODUCER_STATUS_CORRUPT_DATA );
}

/**-----------------------------------------------------------------------------
 *  Tests: Peripheral Expansion Stubs (UART, SPI, CAN)
 *------------------------------------------------------------------------------
 */

TEST_F( ResultMessageProducerTest, PeripheralStubsProcessedWithoutError )
{
    const uint8_t dummy_rx[16] = { 0 };

    simulated_stream_.AppendRecord( 0U, FLASH_MANAGER_RESULT_PERIPHERAL_UART_RECEIVE, 0U,
                                   dummy_rx, sizeof( dummy_rx ) );
    simulated_stream_.AppendRecord( 0U, FLASH_MANAGER_RESULT_PERIPHERAL_SPI_RECEIVE, 0U,
                                   dummy_rx, sizeof( dummy_rx ) );
    simulated_stream_.AppendRecord( 0U, FLASH_MANAGER_RESULT_PERIPHERAL_CAN_RECEIVE, 0U,
                                   dummy_rx, sizeof( dummy_rx ) );

    EXPECT_CALL( *mock_flash_, FLASH_MANAGER_ReadResultBytes( _, _, _ ) )
        .WillRepeatedly( [this]( uint8_t* dest, uint32_t cap, uint32_t* read ) {
            return simulated_stream_.ReadChunk( dest, cap, read );
        } );

    HIL_Application_Message_T message;
    std::memset( &message, 0, sizeof( message ) );

    EXPECT_EQ( RESULT_MESSAGE_PRODUCER_ProduceNextMessage( &message ),
               RESULT_MESSAGE_PRODUCER_STATUS_OK );
    EXPECT_EQ( message.body.test_result.tick_number, 0U );
    EXPECT_EQ( message.body.test_result.condition, HIL_APPLICATION_RESULT_CONDITION_OK );
}
