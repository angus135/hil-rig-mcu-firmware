/******************************************************************************
 *  File:       test_execution_measurement_adapters.cpp
 *  Description:
 *      Unit tests for prepared measurement dispatch and result lease handling.
 ******************************************************************************/

#include <gtest/gtest.h>

extern "C"
{
#include "execution_measurement_adapters.c"  // NOLINT
}

#include <array>
#include <cstdint>
#include <cstring>

static std::array<uint8_t, 512U>        result_storage{};
static bool                             reserve_result       = true;
static FlashManagerResultCommitStatus_T commit_result        = FLASH_MANAGER_RESULT_COMMIT_OK;
static uint16_t                         reserved_bytes       = 0U;
static uint16_t                         committed_bytes      = 0U;
static uint8_t                          committed_peripheral = 0U;
static uint8_t                          committed_channel    = 0U;
static uint32_t                         committed_timestamp  = 0U;
static uint32_t                         cancel_count         = 0U;
static EXEC_CAN_Result_T                can_result           = EXEC_CAN_RESULT_OK;
static uint16_t                         can_packets_read     = 0U;
static uint16_t                         can_pending_packets  = 0U;
static EXEC_CAN_Channel_T               can_channel          = EXEC_CAN_CHANNEL_1;
static bool                             spi_receive_result   = true;
static uint32_t                         spi_pending_bytes    = 0U;
static uint32_t                         spi_bytes_read       = 0U;
static bool                             uart_read_result     = true;
static uint32_t                         uart_pending_bytes   = 0U;
static uint32_t                         uart_bytes_read      = 0U;
static uint32_t                         digital_input_sample = 0U;
static ExecPwmCaptureResult_T           pwm_capture_result   = {};
static bool                             pwm_capture_accepted = false;

extern "C" bool FLASH_MANAGER_ReserveResultRecordFromISR( uint16_t                        capacity,
                                                          FlashManagerResultWriteLease_T* lease )
{
    reserved_bytes = capacity;
    if ( !reserve_result || lease == nullptr )
    {
        if ( lease != nullptr )
        {
            *lease = {};
        }
        return false;
    }
    lease->payload                = result_storage.data();
    lease->lease_id               = 1U;
    lease->payload_capacity_bytes = capacity;
    return true;
}

extern "C" bool FLASH_MANAGER_CancelResultRecordFromISR( const FlashManagerResultWriteLease_T* )
{
    cancel_count++;
    return true;
}

extern "C" FlashManagerResultCommitStatus_T
FLASH_MANAGER_CommitResultRecordFromISR( const FlashManagerResultWriteLease_T*, uint32_t timestamp,
                                         uint8_t peripheral, uint8_t channel, uint16_t length,
                                         BaseType_t* )
{
    committed_timestamp  = timestamp;
    committed_peripheral = peripheral;
    committed_channel    = channel;
    committed_bytes      = length;
    return commit_result;
}

extern "C" EXEC_CAN_Result_T EXEC_CAN_Receive( EXEC_CAN_Channel_T channel,
                                               EXEC_CAN_Packet_T destination[], uint16_t capacity,
                                               uint16_t* packets_read )
{
    can_channel = channel;
    if ( packets_read != nullptr )
    {
        *packets_read = can_packets_read;
    }
    for ( uint16_t index = 0U; index < can_packets_read && index < capacity; index++ )
    {
        destination[index].id      = static_cast<uint16_t>( 0x100U + index );
        destination[index].dlc     = 2U;
        destination[index].data[0] = 0xA5U;
        destination[index].data[1] = static_cast<uint8_t>( index );
    }
    return can_result;
}
extern "C" uint16_t EXEC_CAN_GetPendingReceivePackets( EXEC_CAN_Channel_T channel )
{
    can_channel = channel;
    return can_pending_packets;
}

extern "C" bool EXEC_SPI_Receive( ExecSPIChannel_T, uint8_t* destination, uint32_t capacity,
                                  uint32_t* bytes_read )
{
    *bytes_read = spi_bytes_read;
    ( void )memset( destination, 0xA5, spi_bytes_read < capacity ? spi_bytes_read : capacity );
    return spi_receive_result;
}
extern "C" uint32_t EXEC_SPI_GetPendingReceiveBytes( ExecSPIChannel_T )
{
    return spi_pending_bytes;
}
extern "C" bool EXEC_UART_Read( ExecUartChannel_T, uint8_t* destination, uint32_t capacity,
                                uint32_t* bytes_read )
{
    *bytes_read = uart_bytes_read;
    ( void )memset( destination, 0x55, uart_bytes_read < capacity ? uart_bytes_read : capacity );
    return uart_read_result;
}
extern "C" uint32_t EXEC_UART_GetPendingReceiveBytes( ExecUartChannel_T )
{
    return uart_pending_bytes;
}
extern "C" void EXEC_ANALOGUE_INPUT_Read_Analogue_Inputs( ExecAnalogueInputVoltages_T voltages )
{
    *voltages.channel_0_voltage = 123U;
    *voltages.channel_1_voltage = 456U;
}
extern "C" void EXEC_DIGITAL_INPUT_Sample_All( uint32_t* sample )
{
    *sample = digital_input_sample;
}
extern "C" bool EXEC_PWM_Capture_Consume( ExecPwmCaptureChannel_T, ExecPwmCaptureResult_T* result )
{
    *result = pwm_capture_result;
    return pwm_capture_accepted;
}

class ExecutionMeasurementAdaptersTest : public ::testing::Test
{
protected:
    BaseType_t task_woken = pdFALSE;

    void SetUp() override
    {
        reserve_result       = true;
        commit_result        = FLASH_MANAGER_RESULT_COMMIT_OK;
        reserved_bytes       = 0U;
        committed_bytes      = 0U;
        committed_peripheral = 0U;
        committed_channel    = 0U;
        committed_timestamp  = 0U;
        cancel_count         = 0U;
        can_result           = EXEC_CAN_RESULT_OK;
        can_packets_read     = 0U;
        can_pending_packets  = 0U;
        can_channel          = EXEC_CAN_CHANNEL_1;
        spi_receive_result   = true;
        spi_pending_bytes    = 0U;
        spi_bytes_read       = 0U;
        uart_read_result     = true;
        uart_pending_bytes   = 0U;
        uart_bytes_read      = 0U;
        digital_input_sample = 0U;
        pwm_capture_result   = {};
        pwm_capture_accepted = false;
        result_storage.fill( 0U );
    }
};

TEST_F( ExecutionMeasurementAdaptersTest, PrepareBuildsOnlyEnabledMeasurementsInFixedOrder )
{
    ExecutionMeasurementConfiguration_T configuration = {};
    configuration.analogue_input_enabled              = true;
    configuration.digital_input_enabled               = true;
    configuration.pwm_capture_enabled_mask            = 1U << 1U;
    configuration.uart_receive_enabled_mask           = 1U << 0U;
    configuration.spi_receive_enabled_mask            = 1U << 1U;
    configuration.can_receive_enabled_mask            = 1U << 0U;

    EXECUTION_MEASUREMENT_ADAPTER_Prepare( &configuration );

    ASSERT_EQ( active_measurement_count, 6U );
    EXPECT_EQ( active_measurement_adapters[0].type, EXECUTION_MEASUREMENT_ANALOGUE_INPUT );
    EXPECT_EQ( active_measurement_adapters[1].type, EXECUTION_MEASUREMENT_DIGITAL_INPUT );
    EXPECT_EQ( active_measurement_adapters[2].type, EXECUTION_MEASUREMENT_PWM_CAPTURE );
    EXPECT_EQ( active_measurement_adapters[2].channel, 1U );
    EXPECT_EQ( active_measurement_adapters[3].type, EXECUTION_MEASUREMENT_UART_RECEIVE );
    EXPECT_EQ( active_measurement_adapters[4].type, EXECUTION_MEASUREMENT_SPI_RECEIVE );
    EXPECT_EQ( active_measurement_adapters[4].channel, 1U );
    EXPECT_EQ( active_measurement_adapters[5].type, EXECUTION_MEASUREMENT_CAN_RECEIVE );
}

TEST_F( ExecutionMeasurementAdaptersTest, DigitalInputCommitsOneSampleAtBoundaryTimestamp )
{
    digital_input_sample = 0x12345678U;

    ASSERT_TRUE( EXECUTION_MEASUREMENT_ADAPTER_SampleDigitalInput( 0U, 17U, &task_woken ) );
    EXPECT_EQ( reserved_bytes, sizeof( uint32_t ) );
    EXPECT_EQ( committed_timestamp, 17U );
    EXPECT_EQ( committed_peripheral, FLASH_MANAGER_RESULT_PERIPHERAL_DIGITAL_INPUT );
    EXPECT_EQ( committed_bytes, sizeof( uint32_t ) );
    uint32_t stored_sample = 0U;
    std::memcpy( &stored_sample, result_storage.data(), sizeof( stored_sample ) );
    EXPECT_EQ( stored_sample, digital_input_sample );
}

TEST_F( ExecutionMeasurementAdaptersTest, AnalogueInputCommitsBothDriverValues )
{
    ASSERT_TRUE( EXECUTION_MEASUREMENT_ADAPTER_SampleAnalogueInput( 0U, 9U, &task_woken ) );
    EXPECT_EQ( reserved_bytes, 2U * sizeof( uint32_t ) );
    EXPECT_EQ( committed_peripheral, FLASH_MANAGER_RESULT_PERIPHERAL_ANALOGUE_INPUT );
    EXPECT_EQ( committed_bytes, 2U * sizeof( uint32_t ) );
    uint32_t values[2] = {};
    std::memcpy( values, result_storage.data(), sizeof( values ) );
    EXPECT_EQ( values[0], 123U );
    EXPECT_EQ( values[1], 456U );
}

TEST_F( ExecutionMeasurementAdaptersTest, PwmCaptureWithoutNewDataProducesNoRecord )
{
    pwm_capture_result.has_new_data = false;

    EXPECT_TRUE( EXECUTION_MEASUREMENT_ADAPTER_SamplePwmCapture( 1U, 3U, &task_woken ) );
    EXPECT_EQ( reserved_bytes, 0U );
    EXPECT_EQ( committed_bytes, 0U );
}

TEST_F( ExecutionMeasurementAdaptersTest, ValidPwmCaptureCommitsPeriodAndHighTime )
{
    pwm_capture_result.has_new_data = true;
    pwm_capture_result.is_valid     = true;
    pwm_capture_result.period_ticks = 1000U;
    pwm_capture_result.high_ticks   = 250U;
    pwm_capture_accepted            = true;

    ASSERT_TRUE( EXECUTION_MEASUREMENT_ADAPTER_SamplePwmCapture( 1U, 4U, &task_woken ) );
    EXPECT_EQ( committed_peripheral, FLASH_MANAGER_RESULT_PERIPHERAL_PWM_CAPTURE );
    EXPECT_EQ( committed_channel, 1U );
    uint32_t values[2] = {};
    std::memcpy( values, result_storage.data(), sizeof( values ) );
    EXPECT_EQ( values[0], 1000U );
    EXPECT_EQ( values[1], 250U );
}

TEST_F( ExecutionMeasurementAdaptersTest, EmptyUartPollDoesNotReserveARecord )
{
    reserve_result = false;
    EXPECT_TRUE( EXECUTION_MEASUREMENT_ADAPTER_SampleUartReceive( 0U, 5U, &task_woken ) );
    EXPECT_EQ( reserved_bytes, 0U );
    EXPECT_EQ( committed_bytes, 0U );
    EXPECT_EQ( cancel_count, 0U );
}

TEST_F( ExecutionMeasurementAdaptersTest, UartCommitsOnlyBytesReportedByDriver )
{
    uart_pending_bytes = 13U;
    uart_bytes_read = 13U;

    ASSERT_TRUE( EXECUTION_MEASUREMENT_ADAPTER_SampleUartReceive( 1U, 6U, &task_woken ) );
    EXPECT_EQ( committed_peripheral, FLASH_MANAGER_RESULT_PERIPHERAL_UART_RECEIVE );
    EXPECT_EQ( committed_channel, 1U );
    EXPECT_EQ( committed_bytes, 13U );
    EXPECT_EQ( reserved_bytes, 13U );
}

TEST_F( ExecutionMeasurementAdaptersTest, EmptySpiSnapshotDoesNotReserveARecord )
{
    EXPECT_TRUE( EXECUTION_MEASUREMENT_ADAPTER_SampleSpiReceive( 0U, 7U, &task_woken ) );
    EXPECT_EQ( reserved_bytes, 0U );
    EXPECT_EQ( committed_bytes, 0U );
}

TEST_F( ExecutionMeasurementAdaptersTest, SpiReservationIsBoundedAndCommitsBytesRead )
{
    spi_pending_bytes = EXEC_SPI_MAX_RX_CHUNK_SIZE + 10U;
    spi_bytes_read    = 21U;

    ASSERT_TRUE( EXECUTION_MEASUREMENT_ADAPTER_SampleSpiReceive( 1U, 8U, &task_woken ) );
    EXPECT_EQ( reserved_bytes, EXEC_SPI_MAX_RX_CHUNK_SIZE );
    EXPECT_EQ( committed_peripheral, FLASH_MANAGER_RESULT_PERIPHERAL_SPI_RECEIVE );
    EXPECT_EQ( committed_channel, 1U );
    EXPECT_EQ( committed_bytes, 21U );
}

TEST_F( ExecutionMeasurementAdaptersTest, EmptyCanQueueDoesNotCommit )
{
    reserve_result = false;
    EXPECT_TRUE(
        EXECUTION_MEASUREMENT_ADAPTER_SampleCanReceive( EXEC_CAN_CHANNEL_2, 7U, &task_woken ) );
    EXPECT_EQ( can_channel, EXEC_CAN_CHANNEL_2 );
    EXPECT_EQ( reserved_bytes, 0U );
    EXPECT_EQ( committed_bytes, 0U );
    EXPECT_EQ( cancel_count, 0U );
}

TEST_F( ExecutionMeasurementAdaptersTest, CommitsOnlyReceivedCanPackets )
{
    can_pending_packets = 2U;
    can_packets_read = 2U;
    EXPECT_TRUE(
        EXECUTION_MEASUREMENT_ADAPTER_SampleCanReceive( EXEC_CAN_CHANNEL_1, 11U, &task_woken ) );
    EXPECT_EQ( committed_peripheral, FLASH_MANAGER_RESULT_PERIPHERAL_CAN_RECEIVE );
    EXPECT_EQ( committed_channel, EXEC_CAN_CHANNEL_1 );
    EXPECT_EQ( committed_bytes, 2U * sizeof( EXEC_CAN_Packet_T ) );
    EXPECT_EQ( reserved_bytes, 2U * sizeof( EXEC_CAN_Packet_T ) );
    EXPECT_EQ( cancel_count, 0U );
    const auto* packets = reinterpret_cast<const EXEC_CAN_Packet_T*>( result_storage.data() );
    EXPECT_EQ( packets[0].id, 0x100U );
    EXPECT_EQ( packets[1].data[1], 1U );
}

TEST_F( ExecutionMeasurementAdaptersTest, ReceiveFailureCancelsReservation )
{
    can_pending_packets = 1U;
    can_result = EXEC_CAN_RESULT_ERROR;
    EXPECT_FALSE(
        EXECUTION_MEASUREMENT_ADAPTER_SampleCanReceive( EXEC_CAN_CHANNEL_1, 1U, &task_woken ) );
    EXPECT_EQ( committed_bytes, 0U );
    EXPECT_EQ( cancel_count, 1U );
}

TEST_F( ExecutionMeasurementAdaptersTest, CommitFailureCancelsReservation )
{
    can_pending_packets = 1U;
    can_packets_read = 1U;
    commit_result    = FLASH_MANAGER_RESULT_COMMIT_INTERNAL_ERROR;
    EXPECT_FALSE(
        EXECUTION_MEASUREMENT_ADAPTER_SampleCanReceive( EXEC_CAN_CHANNEL_1, 1U, &task_woken ) );
    EXPECT_EQ( committed_bytes, sizeof( EXEC_CAN_Packet_T ) );
    EXPECT_EQ( cancel_count, 1U );
}
