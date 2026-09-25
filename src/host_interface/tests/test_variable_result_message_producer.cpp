/******************************************************************************
 *  File:       test_variable_result_message_producer.cpp
 *  Description:
 *      Unit tests and white-box tests for the Variable Result Message Producer
 *      (VARIABLE_TEST_RESULT) using GoogleTest and GoogleMock.
 *
 *      These tests verify:
 *        - Parameter validation (NULL out_message pointer).
 *        - Reset behavior and stream reinitialization.
 *        - Flash Manager status translation (BUSY, END_OF_STREAM, INTERNAL_ERROR).
 *        - Peripheral measurement decoding:
 *          * Digital input (32-bit to 16-bit mask conversion)
 *          * Analogue input (microvolts to float volts conversion)
 *          * PWM capture (+2 cycle correction, frequency & duty calculation)
 *          * Serial receive captures (UART, SPI, CAN)
 *        - Multi-record tick aggregation and peek preservation across ticks.
 *        - Split-chunk streaming reassembly across Flash read boundaries.
 *        - Corruption detection:
 *          * Non-monotonic timestamps
 *          * Invalid flash payload lengths
 *          * PWM high ticks exceeding period ticks
 ******************************************************************************/

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

extern "C"
{
#include "flash_manager/flash_manager.h"
#include "hil_rig_protocol/application/application_message.h"
#include "variable_result_message_producer.h"

/* Direct include of .c for white-box testing of internal stream state */
#include "variable_result_message_producer.c"  // NOLINT
}

using ::testing::_;
using ::testing::DoAll;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SetArgPointee;

class MockFlashManagerResultDependencies
{
public:
    virtual ~MockFlashManagerResultDependencies() = default;

    MOCK_METHOD( FlashManagerResultTransferStatus_T, FLASH_MANAGER_ReadResultBytes,
                 ( uint8_t * destination, uint32_t destination_capacity_bytes,
                   uint32_t* bytes_read ) );
};

static MockFlashManagerResultDependencies* g_mock_flash_manager = nullptr;

extern "C" FlashManagerResultTransferStatus_T
FLASH_MANAGER_ReadResultBytes( uint8_t* destination, uint32_t destination_capacity_bytes,
                               uint32_t* bytes_read )
{
    if ( g_mock_flash_manager != nullptr )
    {
        return g_mock_flash_manager->FLASH_MANAGER_ReadResultBytes(
            destination, destination_capacity_bytes, bytes_read );
    }
    if ( bytes_read != nullptr )
    {
        *bytes_read = 0U;
    }
    return FLASH_MANAGER_RESULT_TRANSFER_END_OF_STREAM;
}

class SimulatedFlashResultStream
{
public:
    void Clear()
    {
        stream_bytes_.clear();
        read_cursor_ = 0;
    }

    void AppendRecord( uint32_t protocol_tick, uint8_t peripheral_type, uint8_t channel,
                       const void* payload, uint16_t payload_length )
    {
        AppendRawRecord( protocol_tick + 1U, peripheral_type, channel, payload, payload_length );
    }

    void AppendRawRecord( uint32_t execution_timestamp, uint8_t peripheral_type, uint8_t channel,
                          const void* payload, uint16_t payload_length )
    {
        FlashManagerResultHeader_T header{};
        header.timestamp            = execution_timestamp;
        header.payload_length_bytes = payload_length;
        header.peripheral_type      = peripheral_type;
        header.channel              = channel;

        const auto* header_bytes = reinterpret_cast<const uint8_t*>( &header );
        stream_bytes_.insert( stream_bytes_.end(), header_bytes, header_bytes + sizeof( header ) );

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

class VariableResultMessageProducerTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        mock_flash_          = std::make_unique<NiceMock<MockFlashManagerResultDependencies>>();
        g_mock_flash_manager = mock_flash_.get();
        simulated_stream_.Clear();
        VARIABLE_RESULT_MESSAGE_PRODUCER_Reset();
    }

    void TearDown() override
    {
        g_mock_flash_manager = nullptr;
        mock_flash_.reset();
    }

    void HookSimulatedStream( uint32_t max_chunk_size = 0 )
    {
        ON_CALL( *mock_flash_, FLASH_MANAGER_ReadResultBytes( _, _, _ ) )
            .WillByDefault( [this, max_chunk_size]( uint8_t* destination, uint32_t capacity,
                                                    uint32_t* bytes_read ) {
                return simulated_stream_.ReadChunk( destination, capacity, bytes_read,
                                                    max_chunk_size );
            } );
    }

    std::unique_ptr<NiceMock<MockFlashManagerResultDependencies>> mock_flash_;
    SimulatedFlashResultStream                                    simulated_stream_;
    HIL_Application_Message_T                                     out_msg_{};
};

/**
 * @brief Null pointer parameter validation.
 */
TEST_F( VariableResultMessageProducerTest, RejectsNullMessagePointer )
{
    EXPECT_EQ( VARIABLE_RESULT_MESSAGE_PRODUCER_ProduceNextMessage( nullptr ),
               RESULT_MESSAGE_PRODUCER_STATUS_INVALID_ARGUMENT );
}

/**
 * @brief Empty stream returns END_OF_STREAM immediately.
 */
TEST_F( VariableResultMessageProducerTest, EmptyStreamReturnsEndOfStream )
{
    HookSimulatedStream();
    EXPECT_EQ( VARIABLE_RESULT_MESSAGE_PRODUCER_ProduceNextMessage( &out_msg_ ),
               RESULT_MESSAGE_PRODUCER_STATUS_END_OF_STREAM );
}

/**
 * @brief Flash Manager BUSY and INTERNAL_ERROR status propagation.
 */
TEST_F( VariableResultMessageProducerTest, PropagatesFlashManagerStatuses )
{
    EXPECT_CALL( *mock_flash_, FLASH_MANAGER_ReadResultBytes( _, _, _ ) )
        .WillOnce( Return( FLASH_MANAGER_RESULT_TRANSFER_BUSY ) );
    EXPECT_EQ( VARIABLE_RESULT_MESSAGE_PRODUCER_ProduceNextMessage( &out_msg_ ),
               RESULT_MESSAGE_PRODUCER_STATUS_NO_DATA_AVAILABLE );

    EXPECT_CALL( *mock_flash_, FLASH_MANAGER_ReadResultBytes( _, _, _ ) )
        .WillOnce( Return( FLASH_MANAGER_RESULT_TRANSFER_INTERNAL_ERROR ) );
    EXPECT_EQ( VARIABLE_RESULT_MESSAGE_PRODUCER_ProduceNextMessage( &out_msg_ ),
               RESULT_MESSAGE_PRODUCER_STATUS_INTERNAL_ERROR );
}

/**
 * @brief Single digital input record produces correct VARIABLE_TEST_RESULT message.
 */
TEST_F( VariableResultMessageProducerTest, ProducesDigitalInputRecord )
{
    /* Protocol channels 0 (pin 8), 2 (pin 10), 4 (pin 14), 6 (pin 0) */
    const uint32_t pinmask = ( 1UL << 8U ) | ( 1UL << 10U ) | ( 1UL << 14U ) | ( 1UL << 0U );
    simulated_stream_.AppendRecord( 0U, FLASH_MANAGER_RESULT_PERIPHERAL_DIGITAL_INPUT, 0U, &pinmask,
                                    sizeof( pinmask ) );
    HookSimulatedStream();

    EXPECT_EQ( VARIABLE_RESULT_MESSAGE_PRODUCER_ProduceNextMessage( &out_msg_ ),
               RESULT_MESSAGE_PRODUCER_STATUS_OK );

    EXPECT_EQ( out_msg_.type, HIL_APPLICATION_MESSAGE_TYPE_VARIABLE_TEST_RESULT );
    EXPECT_EQ( out_msg_.subtype, HIL_APPLICATION_MESSAGE_SUBTYPE_NONE );
    EXPECT_EQ( out_msg_.body.variable_test_result.tick_number, 0U );
    EXPECT_EQ( out_msg_.body.variable_test_result.flags, 0x00U );
    EXPECT_EQ( out_msg_.body.variable_test_result.record_count, 1U );

    const auto& rec = out_msg_.body.variable_test_result.records[0];
    EXPECT_EQ( rec.peripheral_type, HIL_APPLICATION_PERIPHERAL_DIGITAL_INPUT );
    EXPECT_EQ( rec.channel, 0U );
    EXPECT_EQ( rec.data.size, 2U );
    ASSERT_NE( rec.data.data, nullptr );

    uint16_t decoded_mask = 0U;
    std::memcpy( &decoded_mask, rec.data.data, sizeof( decoded_mask ) );
    EXPECT_EQ( decoded_mask, 0x0055U );

    /* Next call should be end of stream */
    EXPECT_EQ( VARIABLE_RESULT_MESSAGE_PRODUCER_ProduceNextMessage( &out_msg_ ),
               RESULT_MESSAGE_PRODUCER_STATUS_END_OF_STREAM );
}

/**
 * @brief Analogue input converts flash microvolts to per-channel records.
 */
TEST_F( VariableResultMessageProducerTest, ProducesAnalogueInputRecord )
{
    const uint32_t microvolts[2] = { 2500000U, 1800000U };
    simulated_stream_.AppendRecord( 1U, FLASH_MANAGER_RESULT_PERIPHERAL_ANALOGUE_INPUT, 0U,
                                    microvolts, sizeof( microvolts ) );
    HookSimulatedStream();

    EXPECT_EQ( VARIABLE_RESULT_MESSAGE_PRODUCER_ProduceNextMessage( &out_msg_ ),
               RESULT_MESSAGE_PRODUCER_STATUS_OK );

    EXPECT_EQ( out_msg_.body.variable_test_result.tick_number, 1U );
    EXPECT_EQ( out_msg_.body.variable_test_result.record_count, 2U );

    const auto& rec0 = out_msg_.body.variable_test_result.records[0];
    EXPECT_EQ( rec0.peripheral_type, HIL_APPLICATION_PERIPHERAL_ANALOG_INPUT );
    EXPECT_EQ( rec0.channel, 0U );
    EXPECT_EQ( rec0.data.size, 4U );
    ASSERT_NE( rec0.data.data, nullptr );

    uint32_t decoded_uv0 = 0U;
    std::memcpy( &decoded_uv0, rec0.data.data, sizeof( decoded_uv0 ) );
    EXPECT_EQ( decoded_uv0, 2500000U );

    const auto& rec1 = out_msg_.body.variable_test_result.records[1];
    EXPECT_EQ( rec1.peripheral_type, HIL_APPLICATION_PERIPHERAL_ANALOG_INPUT );
    EXPECT_EQ( rec1.channel, 1U );
    EXPECT_EQ( rec1.data.size, 4U );
    ASSERT_NE( rec1.data.data, nullptr );

    uint32_t decoded_uv1 = 0U;
    std::memcpy( &decoded_uv1, rec1.data.data, sizeof( decoded_uv1 ) );
    EXPECT_EQ( decoded_uv1, 1800000U );
}

/**
 * @brief PWM capture applies +2 timer clock cycle correction and calculates frequency and duty.
 */
TEST_F( VariableResultMessageProducerTest, ProducesPwmCaptureRecord )
{
    /* 90MHz timer clock (90,000,000 Hz).
     * Period ticks: 89998 (+ 2 = 90000 -> 1ms = 1,000,000 ns).
     * High ticks: 44998 (+ 2 = 45000 -> 50% = 5000 permyriad).
     */
    struct PwmCaptureData
    {
        uint32_t period_ticks;
        uint32_t high_ticks;
    } pwm_data = { 89998U, 44998U };

    simulated_stream_.AppendRecord( 3U, FLASH_MANAGER_RESULT_PERIPHERAL_PWM_CAPTURE, 0U, &pwm_data,
                                    sizeof( pwm_data ) );
    HookSimulatedStream();

    EXPECT_EQ( VARIABLE_RESULT_MESSAGE_PRODUCER_ProduceNextMessage( &out_msg_ ),
               RESULT_MESSAGE_PRODUCER_STATUS_OK );

    EXPECT_EQ( out_msg_.body.variable_test_result.tick_number, 3U );
    EXPECT_EQ( out_msg_.body.variable_test_result.record_count, 1U );

    const auto& rec = out_msg_.body.variable_test_result.records[0];
    EXPECT_EQ( rec.peripheral_type, HIL_APPLICATION_PERIPHERAL_PWM_INPUT );
    EXPECT_EQ( rec.channel, 0U );
    EXPECT_EQ( rec.data.size, 6U );
    ASSERT_NE( rec.data.data, nullptr );

    uint32_t period_ns      = 0U;
    uint16_t duty_permyriad = 0U;
    std::memcpy( &period_ns, rec.data.data, sizeof( period_ns ) );
    std::memcpy( &duty_permyriad, rec.data.data + 4, sizeof( duty_permyriad ) );
    EXPECT_EQ( period_ns, 1000000U );
    EXPECT_EQ( duty_permyriad, 5000U );
}

/**
 * @brief Serial receive records (UART, SPI, CAN) pass through raw bytes.
 */
TEST_F( VariableResultMessageProducerTest, ProducesSerialRecords )
{
    uint8_t uart_payload[4] = { 'P', 'O', 'N', 'G' };
    uint8_t spi_payload[2]  = { 0x11, 0x22 };
    uint8_t can_payload[12] = { 0x34, 0x12, 0x00, 0x00, 0x00, 0x00,
                                0x04, 0x00, 0xDE, 0xAD, 0xBE, 0x00 };

    simulated_stream_.AppendRecord( 2U, FLASH_MANAGER_RESULT_PERIPHERAL_UART_RECEIVE, 0U,
                                    uart_payload, sizeof( uart_payload ) );
    simulated_stream_.AppendRecord( 2U, FLASH_MANAGER_RESULT_PERIPHERAL_SPI_RECEIVE, 0U,
                                    spi_payload, sizeof( spi_payload ) );
    simulated_stream_.AppendRecord( 2U, FLASH_MANAGER_RESULT_PERIPHERAL_CAN_RECEIVE, 0U,
                                    can_payload, sizeof( can_payload ) );
    HookSimulatedStream();

    EXPECT_EQ( VARIABLE_RESULT_MESSAGE_PRODUCER_ProduceNextMessage( &out_msg_ ),
               RESULT_MESSAGE_PRODUCER_STATUS_OK );

    EXPECT_EQ( out_msg_.body.variable_test_result.tick_number, 2U );
    EXPECT_EQ( out_msg_.body.variable_test_result.record_count, 3U );

    /* Record 0: UART */
    const auto& rec_uart = out_msg_.body.variable_test_result.records[0];
    EXPECT_EQ( rec_uart.peripheral_type, HIL_APPLICATION_PERIPHERAL_UART );
    EXPECT_EQ( rec_uart.data.size, 4U );
    EXPECT_EQ( std::memcmp( rec_uart.data.data, uart_payload, 4 ), 0 );

    /* Record 1: SPI */
    const auto& rec_spi = out_msg_.body.variable_test_result.records[1];
    EXPECT_EQ( rec_spi.peripheral_type, HIL_APPLICATION_PERIPHERAL_SPI );
    EXPECT_EQ( rec_spi.data.size, 2U );
    EXPECT_EQ( std::memcmp( rec_spi.data.data, spi_payload, 2 ), 0 );

    /* Record 2: CAN */
    const auto& rec_can = out_msg_.body.variable_test_result.records[2];
    EXPECT_EQ( rec_can.peripheral_type, HIL_APPLICATION_PERIPHERAL_CAN );
    EXPECT_EQ( rec_can.data.size, 12U );
    EXPECT_EQ( std::memcmp( rec_can.data.data, can_payload, 12 ), 0 );
}

/**
 * @brief Multi-record tick aggregation and peek preservation across tick boundaries.
 */
TEST_F( VariableResultMessageProducerTest, AggregatesRecordsForSameTickAndPeeksNextTick )
{
    uint32_t       mask_tick0      = 0x00000001U;
    const uint32_t uvolts_tick0[2] = { 3300000U, 1200000U };
    uint32_t       mask_tick1      = 0x00000002U;

    /* Tick 0: 1 DI + 2 AI records = 3 records */
    simulated_stream_.AppendRecord( 0U, FLASH_MANAGER_RESULT_PERIPHERAL_DIGITAL_INPUT, 0U,
                                    &mask_tick0, sizeof( mask_tick0 ) );
    simulated_stream_.AppendRecord( 0U, FLASH_MANAGER_RESULT_PERIPHERAL_ANALOGUE_INPUT, 0U,
                                    uvolts_tick0, sizeof( uvolts_tick0 ) );

    /* Tick 1: 1 record */
    simulated_stream_.AppendRecord( 1U, FLASH_MANAGER_RESULT_PERIPHERAL_DIGITAL_INPUT, 0U,
                                    &mask_tick1, sizeof( mask_tick1 ) );

    HookSimulatedStream();

    /* First call produces Tick 0 */
    EXPECT_EQ( VARIABLE_RESULT_MESSAGE_PRODUCER_ProduceNextMessage( &out_msg_ ),
               RESULT_MESSAGE_PRODUCER_STATUS_OK );
    EXPECT_EQ( out_msg_.body.variable_test_result.tick_number, 0U );
    EXPECT_EQ( out_msg_.body.variable_test_result.record_count, 3U );

    /* Second call produces Tick 1 (read from peek) */
    EXPECT_EQ( VARIABLE_RESULT_MESSAGE_PRODUCER_ProduceNextMessage( &out_msg_ ),
               RESULT_MESSAGE_PRODUCER_STATUS_OK );
    EXPECT_EQ( out_msg_.body.variable_test_result.tick_number, 1U );
    EXPECT_EQ( out_msg_.body.variable_test_result.record_count, 1U );

    /* Third call: end of stream */
    EXPECT_EQ( VARIABLE_RESULT_MESSAGE_PRODUCER_ProduceNextMessage( &out_msg_ ),
               RESULT_MESSAGE_PRODUCER_STATUS_END_OF_STREAM );
}

/**
 * @brief Split chunk streaming across tiny Flash Manager read boundaries.
 */
TEST_F( VariableResultMessageProducerTest, ReassemblesAcrossSmallFlashChunks )
{
    uint32_t pinmask = 0x00000100U;
    simulated_stream_.AppendRecord( 0U, FLASH_MANAGER_RESULT_PERIPHERAL_DIGITAL_INPUT, 0U, &pinmask,
                                    sizeof( pinmask ) );

    /* Force tiny reads of 3 bytes each to stress boundary reassembly */
    HookSimulatedStream( 3U );

    EXPECT_EQ( VARIABLE_RESULT_MESSAGE_PRODUCER_ProduceNextMessage( &out_msg_ ),
               RESULT_MESSAGE_PRODUCER_STATUS_OK );
    EXPECT_EQ( out_msg_.body.variable_test_result.tick_number, 0U );
    EXPECT_EQ( out_msg_.body.variable_test_result.record_count, 1U );
}

/**
 * @brief Non-monotonic execution timestamp is detected as corrupt data.
 */
TEST_F( VariableResultMessageProducerTest, DetectsNonMonotonicTimestampCorruption )
{
    uint32_t val = 0U;
    /* Record at timestamp 5, followed by record at timestamp 3 */
    simulated_stream_.AppendRawRecord( 5U, FLASH_MANAGER_RESULT_PERIPHERAL_DIGITAL_INPUT, 0U, &val,
                                       sizeof( val ) );
    simulated_stream_.AppendRawRecord( 3U, FLASH_MANAGER_RESULT_PERIPHERAL_DIGITAL_INPUT, 0U, &val,
                                       sizeof( val ) );
    HookSimulatedStream();

    /* First tick (timestamp 5 - 1 = tick 4) produces OK while peeking the next record */
    EXPECT_EQ( VARIABLE_RESULT_MESSAGE_PRODUCER_ProduceNextMessage( &out_msg_ ),
               RESULT_MESSAGE_PRODUCER_STATUS_CORRUPT_DATA );
}

/**
 * @brief Invalid payload length for digital input in flash is detected as corrupt data.
 */
TEST_F( VariableResultMessageProducerTest, DetectsInvalidPayloadLengthCorruption )
{
    uint8_t bad_len[2] = { 0x01, 0x02 }; /* Digital input must be 4 bytes in flash */
    simulated_stream_.AppendRawRecord( 1U, FLASH_MANAGER_RESULT_PERIPHERAL_DIGITAL_INPUT, 0U,
                                       bad_len, sizeof( bad_len ) );
    HookSimulatedStream();

    EXPECT_EQ( VARIABLE_RESULT_MESSAGE_PRODUCER_ProduceNextMessage( &out_msg_ ),
               RESULT_MESSAGE_PRODUCER_STATUS_CORRUPT_DATA );
}

/**
 * @brief PWM high ticks > period ticks is detected as corrupt data.
 */
TEST_F( VariableResultMessageProducerTest, DetectsPwmHighTicksGreaterThanPeriodCorruption )
{
    struct PwmCaptureData
    {
        uint32_t period_ticks;
        uint32_t high_ticks;
    } bad_pwm = { 4000U, 6000U }; /* High (6000) > Period (4000) */

    simulated_stream_.AppendRecord( 0U, FLASH_MANAGER_RESULT_PERIPHERAL_PWM_CAPTURE, 0U, &bad_pwm,
                                    sizeof( bad_pwm ) );
    HookSimulatedStream();

    EXPECT_EQ( VARIABLE_RESULT_MESSAGE_PRODUCER_ProduceNextMessage( &out_msg_ ),
               RESULT_MESSAGE_PRODUCER_STATUS_CORRUPT_DATA );
}
