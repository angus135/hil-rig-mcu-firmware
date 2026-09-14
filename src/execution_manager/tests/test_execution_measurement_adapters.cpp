/******************************************************************************
 *  File:       test_execution_measurement_adapters.cpp
 *  Description:
 *      Unit tests for execution-time CAN measurement reservation and commit.
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
static uint32_t                         cancel_count         = 0U;
static EXEC_CAN_Result_T                can_result           = EXEC_CAN_RESULT_OK;
static uint16_t                         can_packets_read     = 0U;
static EXEC_CAN_Channel_T               can_channel          = EXEC_CAN_CHANNEL_1;

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
FLASH_MANAGER_CommitResultRecordFromISR( const FlashManagerResultWriteLease_T*, uint32_t,
                                         uint8_t peripheral, uint8_t channel, uint16_t length,
                                         BaseType_t* )
{
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

extern "C" bool EXEC_SPI_Receive( ExecSPIChannel_T, uint8_t*, uint32_t, uint32_t* bytes_read )
{
    *bytes_read = 0U;
    return true;
}
extern "C" uint32_t EXEC_SPI_GetPendingReceiveBytes( ExecSPIChannel_T )
{
    return 0U;
}
extern "C" bool EXEC_UART_Read( ExecUartChannel_T, uint8_t*, uint32_t, uint32_t* bytes_read )
{
    *bytes_read = 0U;
    return true;
}
extern "C" void EXEC_ANALOGUE_INPUT_Read_Analogue_Inputs( ExecAnalogueInputVoltages_T )
{
}
extern "C" void EXEC_DIGITAL_INPUT_Sample_All( uint32_t* )
{
}
extern "C" bool EXEC_PWM_Capture_Consume( ExecPwmCaptureChannel_T, ExecPwmCaptureResult_T* result )
{
    result->has_new_data = false;
    return false;
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
        cancel_count         = 0U;
        can_result           = EXEC_CAN_RESULT_OK;
        can_packets_read     = 0U;
        can_channel          = EXEC_CAN_CHANNEL_1;
        result_storage.fill( 0U );
    }
};

TEST_F( ExecutionMeasurementAdaptersTest, EmptyCanQueueDoesNotCommit )
{
    EXPECT_TRUE(
        EXECUTION_MEASUREMENT_ADAPTER_SampleCanReceive( EXEC_CAN_CHANNEL_2, 7U, &task_woken ) );
    EXPECT_EQ( can_channel, EXEC_CAN_CHANNEL_2 );
    EXPECT_EQ( reserved_bytes, EXEC_CAN_MAX_BATCH_SIZE * sizeof( EXEC_CAN_Packet_T ) );
    EXPECT_EQ( committed_bytes, 0U );
    EXPECT_EQ( cancel_count, 1U );
}

TEST_F( ExecutionMeasurementAdaptersTest, CommitsOnlyReceivedCanPackets )
{
    can_packets_read = 2U;
    EXPECT_TRUE(
        EXECUTION_MEASUREMENT_ADAPTER_SampleCanReceive( EXEC_CAN_CHANNEL_1, 11U, &task_woken ) );
    EXPECT_EQ( committed_peripheral, FLASH_MANAGER_RESULT_PERIPHERAL_CAN_RECEIVE );
    EXPECT_EQ( committed_channel, EXEC_CAN_CHANNEL_1 );
    EXPECT_EQ( committed_bytes, 2U * sizeof( EXEC_CAN_Packet_T ) );
    EXPECT_EQ( cancel_count, 0U );
    const auto* packets = reinterpret_cast<const EXEC_CAN_Packet_T*>( result_storage.data() );
    EXPECT_EQ( packets[0].id, 0x100U );
    EXPECT_EQ( packets[1].data[1], 1U );
}

TEST_F( ExecutionMeasurementAdaptersTest, ReceiveFailureCancelsReservation )
{
    can_result = EXEC_CAN_RESULT_ERROR;
    EXPECT_FALSE(
        EXECUTION_MEASUREMENT_ADAPTER_SampleCanReceive( EXEC_CAN_CHANNEL_1, 1U, &task_woken ) );
    EXPECT_EQ( committed_bytes, 0U );
    EXPECT_EQ( cancel_count, 1U );
}

TEST_F( ExecutionMeasurementAdaptersTest, CommitFailureCancelsReservation )
{
    can_packets_read = 1U;
    commit_result    = FLASH_MANAGER_RESULT_COMMIT_INTERNAL_ERROR;
    EXPECT_FALSE(
        EXECUTION_MEASUREMENT_ADAPTER_SampleCanReceive( EXEC_CAN_CHANNEL_1, 1U, &task_woken ) );
    EXPECT_EQ( committed_bytes, sizeof( EXEC_CAN_Packet_T ) );
    EXPECT_EQ( cancel_count, 1U );
}
