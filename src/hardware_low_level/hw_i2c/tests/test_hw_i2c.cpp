/******************************************************************************
 *  File:       test_hw_i2c.cpp
 *  Description:
 *      White-box tests for I2C lifecycle, complete-message queueing, and RX
 *      publication.
 ******************************************************************************/

#include <gtest/gtest.h>
#include <array>
#include <cstring>

extern "C"
{
#ifndef TEST_BUILD
#define TEST_BUILD
#endif
#include "hw_i2c_mocks.h"
#include "hw_i2c.h"
#include <stdbool.h>
#include <stdint.h>

#include "../hw_i2c.c"  // NOLINT
}

class HWI2CTest : public ::testing::Test
{
protected:
    void SetUp( void ) override
    {
        std::memset( hw_i2c_channel_state, 0, sizeof( hw_i2c_channel_state ) );
        std::memset( &hw_i2c_mock_i2c1, 0, sizeof( hw_i2c_mock_i2c1 ) );
        std::memset( &hw_i2c_mock_i2c2, 0, sizeof( hw_i2c_mock_i2c2 ) );
        std::memset( &hw_i2c_mock_dma1, 0, sizeof( hw_i2c_mock_dma1 ) );
        std::memset( &hw_i2c_mock_dma1_stream2, 0, sizeof( hw_i2c_mock_dma1_stream2 ) );
        std::memset( &hw_i2c_mock_dma1_stream7, 0, sizeof( hw_i2c_mock_dma1_stream7 ) );
        std::memset( &hw_i2c_mock_fmpi2c1, 0, sizeof( hw_i2c_mock_fmpi2c1 ) );
        std::memset( hw_i2c_mock_nvic_enabled, 0, sizeof( hw_i2c_mock_nvic_enabled ) );
    }

    static void ConfigureExternal( HWI2CChannel_T channel, HWI2CMode_T mode,
                                   HWI2CTransferPath_T tx_path = HW_I2C_TRANSFER_INTERRUPT,
                                   HWI2CTransferPath_T rx_path = HW_I2C_TRANSFER_INTERRUPT )
    {
        const HWI2CChannelConfig_T config = {
            .mode             = mode,
            .speed            = HW_I2C_SPEED_100KHZ,
            .tx_transfer_path = tx_path,
            .rx_transfer_path = rx_path,
            .own_address_7bit = 0x31U,
        };
        ASSERT_EQ( HW_I2C_Configure_Channel( channel, &config ), HW_I2C_STATUS_OK );
        ASSERT_EQ( HW_I2C_Start_Channel( channel ), HW_I2C_STATUS_OK );
    }

    static void StageAndPublish( HWI2CChannelState_T& state, HWI2CTransferKind_T kind,
                                 uint16_t address, const uint8_t* data, uint16_t length )
    {
        state.transfer_kind       = kind;
        state.target_address_7bit = address;
        state.rx_received_length  = length;
        if ( length > 0U )
        {
            std::memcpy( state.rx_staging_buffer, data, length );
        }
        ASSERT_TRUE( HW_I2C_Publish_Received_Message( &state ) );
    }
};

TEST_F( HWI2CTest, ConfigureRejectsInvalidChannelAndAddress )
{
    const HWI2CChannelConfig_T config = {
        .mode             = HW_I2C_MODE_MASTER,
        .speed            = HW_I2C_SPEED_100KHZ,
        .tx_transfer_path = HW_I2C_TRANSFER_INTERRUPT,
        .rx_transfer_path = HW_I2C_TRANSFER_INTERRUPT,
        .own_address_7bit = 0x12U,
    };

    EXPECT_EQ( HW_I2C_Configure_Channel( HW_I2C_CHANNEL_COUNT, &config ),
               HW_I2C_STATUS_INVALID_PARAM );
    EXPECT_EQ( HW_I2C_Configure_Internal_FMPI2C1( 0x80U ), HW_I2C_STATUS_INVALID_PARAM );
}

TEST_F( HWI2CTest, ConfigureLeavesExternalChannelConfiguredAndStopped )
{
    const HWI2CChannelConfig_T config = {
        .mode             = HW_I2C_MODE_MASTER,
        .speed            = HW_I2C_SPEED_100KHZ,
        .tx_transfer_path = HW_I2C_TRANSFER_INTERRUPT,
        .rx_transfer_path = HW_I2C_TRANSFER_INTERRUPT,
        .own_address_7bit = 0x31U,
    };

    ASSERT_EQ( HW_I2C_Configure_Channel( HW_I2C_CHANNEL_1, &config ), HW_I2C_STATUS_OK );

    EXPECT_TRUE( HW_I2C_Is_Channel_Configured( HW_I2C_CHANNEL_1 ) );
    EXPECT_FALSE( HW_I2C_Is_Channel_Started( HW_I2C_CHANNEL_1 ) );
    EXPECT_EQ( I2C3->CR1 & I2C_CR1_PE, 0U );
    EXPECT_EQ( I2C3->CR2 & ( I2C_CR2_DMAEN | I2C_CR2_LAST ), 0U );
}

TEST_F( HWI2CTest, StartEnablesConfiguredExternalChannel )
{
    const HWI2CChannelConfig_T config = {
        .mode             = HW_I2C_MODE_MASTER,
        .speed            = HW_I2C_SPEED_100KHZ,
        .tx_transfer_path = HW_I2C_TRANSFER_INTERRUPT,
        .rx_transfer_path = HW_I2C_TRANSFER_INTERRUPT,
        .own_address_7bit = 0x31U,
    };

    ASSERT_EQ( HW_I2C_Configure_Channel( HW_I2C_CHANNEL_1, &config ), HW_I2C_STATUS_OK );

    EXPECT_EQ( HW_I2C_Start_Channel( HW_I2C_CHANNEL_1 ), HW_I2C_STATUS_OK );
    EXPECT_TRUE( HW_I2C_Is_Channel_Started( HW_I2C_CHANNEL_1 ) );
    EXPECT_NE( I2C3->CR1 & I2C_CR1_PE, 0U );
    EXPECT_EQ( HW_I2C_Start_Channel( HW_I2C_CHANNEL_1 ), HW_I2C_STATUS_BUSY );
}

TEST_F( HWI2CTest, StopDisablesIdleChannelAndRetainsConfiguration )
{
    ConfigureExternal( HW_I2C_CHANNEL_1, HW_I2C_MODE_MASTER );

    EXPECT_EQ( HW_I2C_Stop_Channel( HW_I2C_CHANNEL_1 ), HW_I2C_STATUS_OK );
    EXPECT_TRUE( HW_I2C_Is_Channel_Configured( HW_I2C_CHANNEL_1 ) );
    EXPECT_FALSE( HW_I2C_Is_Channel_Started( HW_I2C_CHANNEL_1 ) );
    EXPECT_EQ( I2C3->CR1 & I2C_CR1_PE, 0U );
    EXPECT_EQ( HW_I2C_Stop_Channel( HW_I2C_CHANNEL_1 ), HW_I2C_STATUS_BUSY );

    EXPECT_EQ( HW_I2C_Start_Channel( HW_I2C_CHANNEL_1 ), HW_I2C_STATUS_OK );
    EXPECT_NE( I2C3->CR1 & I2C_CR1_PE, 0U );
}

TEST_F( HWI2CTest, LifecycleRejectsUnconfiguredAndNonExternalChannels )
{
    EXPECT_EQ( HW_I2C_Start_Channel( HW_I2C_CHANNEL_1 ), HW_I2C_STATUS_NOT_CONFIGURED );
    EXPECT_EQ( HW_I2C_Stop_Channel( HW_I2C_CHANNEL_1 ), HW_I2C_STATUS_NOT_CONFIGURED );
    EXPECT_EQ( HW_I2C_Start_Channel( HW_I2C_CHANNEL_FMPI2C1 ), HW_I2C_STATUS_INVALID_PARAM );
    EXPECT_EQ( HW_I2C_Stop_Channel( HW_I2C_CHANNEL_FMPI2C1 ), HW_I2C_STATUS_INVALID_PARAM );
    EXPECT_EQ( HW_I2C_Start_Channel( HW_I2C_CHANNEL_COUNT ), HW_I2C_STATUS_INVALID_PARAM );
    EXPECT_EQ( HW_I2C_Stop_Channel( HW_I2C_CHANNEL_COUNT ), HW_I2C_STATUS_INVALID_PARAM );
    EXPECT_FALSE( HW_I2C_Is_Channel_Configured( HW_I2C_CHANNEL_FMPI2C1 ) );
    EXPECT_FALSE( HW_I2C_Is_Channel_Started( HW_I2C_CHANNEL_FMPI2C1 ) );
}

TEST_F( HWI2CTest, StopRejectsChannelWithActiveMasterTransaction )
{
    ConfigureExternal( HW_I2C_CHANNEL_1, HW_I2C_MODE_MASTER );
    const uint8_t payload = 0x5AU;

    ASSERT_EQ( HW_I2C_Enqueue_Master_Transmit( HW_I2C_CHANNEL_1, 0x20U, &payload, 1U ),
               HW_I2C_STATUS_OK );

    EXPECT_EQ( HW_I2C_Stop_Channel( HW_I2C_CHANNEL_1 ), HW_I2C_STATUS_BUSY );
    EXPECT_TRUE( HW_I2C_Is_Channel_Started( HW_I2C_CHANNEL_1 ) );
    EXPECT_NE( I2C3->CR1 & I2C_CR1_PE, 0U );
}

TEST_F( HWI2CTest, InternalChannelIsRunningButExcludedFromExternalLifecycleQueries )
{
    ASSERT_EQ( HW_I2C_Configure_Internal_FMPI2C1( 0x33U ), HW_I2C_STATUS_OK );

    EXPECT_TRUE( hw_i2c_channel_state[HW_I2C_CHANNEL_FMPI2C1].is_configured );
    EXPECT_TRUE( hw_i2c_channel_state[HW_I2C_CHANNEL_FMPI2C1].is_started );
    EXPECT_FALSE( HW_I2C_Is_Channel_Configured( HW_I2C_CHANNEL_FMPI2C1 ) );
    EXPECT_FALSE( HW_I2C_Is_Channel_Started( HW_I2C_CHANNEL_FMPI2C1 ) );
}

TEST_F( HWI2CTest, EnqueuePreservesCompleteTransactionsInFifoOrderWhileActive )
{
    ConfigureExternal( HW_I2C_CHANNEL_1, HW_I2C_MODE_MASTER );
    const std::array<uint8_t, 3U> first = { 0x10U, 0x11U, 0x12U };
    const std::array<uint8_t, 2U> third = { 0x30U, 0x31U };

    EXPECT_EQ(
        HW_I2C_Enqueue_Master_Transmit( HW_I2C_CHANNEL_1, 0x20U, first.data(), first.size() ),
        HW_I2C_STATUS_OK );
    EXPECT_EQ( HW_I2C_Enqueue_Master_Receive( HW_I2C_CHANNEL_1, 0x21U, 7U ), HW_I2C_STATUS_OK );
    EXPECT_EQ(
        HW_I2C_Enqueue_Master_Transmit( HW_I2C_CHANNEL_1, 0x22U, third.data(), third.size() ),
        HW_I2C_STATUS_OK );

    const HWI2CChannelState_T& state = hw_i2c_channel_state[HW_I2C_CHANNEL_1];
    ASSERT_TRUE( state.master_queue_active );
    ASSERT_EQ( state.master_queue_count, 3U );
    EXPECT_EQ( state.master_queue[0].transfer_kind, HW_I2C_TRANSFER_KIND_MASTER_TX );
    EXPECT_EQ( state.master_queue[0].target_address_7bit, 0x20U );
    EXPECT_EQ( state.master_queue[0].length, first.size() );
    EXPECT_EQ( std::memcmp( state.master_queue[0].tx_payload, first.data(), first.size() ), 0 );
    EXPECT_EQ( state.master_queue[1].transfer_kind, HW_I2C_TRANSFER_KIND_MASTER_RX );
    EXPECT_EQ( state.master_queue[1].target_address_7bit, 0x21U );
    EXPECT_EQ( state.master_queue[1].length, 7U );
    EXPECT_EQ( state.master_queue[2].transfer_kind, HW_I2C_TRANSFER_KIND_MASTER_TX );
    EXPECT_EQ( state.master_queue[2].target_address_7bit, 0x22U );
    EXPECT_EQ( state.master_queue[2].length, third.size() );
    EXPECT_EQ( std::memcmp( state.master_queue[2].tx_payload, third.data(), third.size() ), 0 );
}

TEST_F( HWI2CTest, QueueFullRejectsWithoutChangingQueueStateOrPayload )
{
    ConfigureExternal( HW_I2C_CHANNEL_1, HW_I2C_MODE_MASTER );
    I2C3->SR2 = I2C_SR2_BUSY;

    for ( uint8_t index = 0U; index < HW_I2C_MASTER_TRANSACTION_QUEUE_DEPTH; ++index )
    {
        const uint8_t payload = index;
        ASSERT_EQ( HW_I2C_Enqueue_Master_Transmit(
                       HW_I2C_CHANNEL_1, static_cast<uint16_t>( 0x20U + index ), &payload, 1U ),
                   HW_I2C_STATUS_OK );
    }

    HWI2CChannelState_T& state                  = hw_i2c_channel_state[HW_I2C_CHANNEL_1];
    const uint8_t        original_first_payload = state.master_queue[0].tx_payload[0];
    const uint8_t        replacement            = 0xEEU;
    EXPECT_EQ( HW_I2C_Enqueue_Master_Transmit( HW_I2C_CHANNEL_1, 0x55U, &replacement, 1U ),
               HW_I2C_STATUS_BUSY );
    EXPECT_EQ( state.master_queue_count, HW_I2C_MASTER_TRANSACTION_QUEUE_DEPTH );
    EXPECT_EQ( state.master_queue_head, 0U );
    EXPECT_EQ( state.master_queue_tail, 0U );
    EXPECT_EQ( state.master_queue[0].tx_payload[0], original_first_payload );
}

TEST_F( HWI2CTest, MasterReceiveMayExactlyFillReservedByteCapacity )
{
    ConfigureExternal( HW_I2C_CHANNEL_1, HW_I2C_MODE_MASTER );
    HWI2CChannelState_T&            state = hw_i2c_channel_state[HW_I2C_CHANNEL_1];
    const std::array<uint8_t, 100U> completed{};
    StageAndPublish( state, HW_I2C_TRANSFER_KIND_MASTER_RX, 0x20U, completed.data(),
                     completed.size() );

    EXPECT_EQ( HW_I2C_Enqueue_Master_Receive( HW_I2C_CHANNEL_1, 0x21U, 412U ), HW_I2C_STATUS_OK );
}

TEST_F( HWI2CTest, MasterReceiveRejectsWhenReservedBytesWouldOverflow )
{
    ConfigureExternal( HW_I2C_CHANNEL_1, HW_I2C_MODE_MASTER );
    HWI2CChannelState_T&            state = hw_i2c_channel_state[HW_I2C_CHANNEL_1];
    const std::array<uint8_t, 100U> completed{};
    StageAndPublish( state, HW_I2C_TRANSFER_KIND_MASTER_RX, 0x20U, completed.data(),
                     completed.size() );

    EXPECT_EQ( HW_I2C_Enqueue_Master_Receive( HW_I2C_CHANNEL_1, 0x21U, 413U ), HW_I2C_STATUS_BUSY );
    EXPECT_EQ( state.master_queue_count, 0U );
}

TEST_F( HWI2CTest, MasterReceiveRejectsWhenFutureDescriptorCapacityWouldOverflow )
{
    ConfigureExternal( HW_I2C_CHANNEL_1, HW_I2C_MODE_MASTER );
    HWI2CChannelState_T& state = hw_i2c_channel_state[HW_I2C_CHANNEL_1];
    for ( uint8_t index = 0U; index < 7U; ++index )
    {
        const uint8_t completed = index;
        StageAndPublish( state, HW_I2C_TRANSFER_KIND_MASTER_RX, 0x20U, &completed, 1U );
    }

    EXPECT_EQ( HW_I2C_Enqueue_Master_Receive( HW_I2C_CHANNEL_1, 0x21U, 1U ), HW_I2C_STATUS_OK );
    EXPECT_EQ( HW_I2C_Enqueue_Master_Receive( HW_I2C_CHANNEL_1, 0x22U, 1U ), HW_I2C_STATUS_BUSY );
    EXPECT_EQ( state.master_queue_count, 1U );
}

TEST_F( HWI2CTest, ConsumingCompletedMessageReleasesReservedCapacity )
{
    ConfigureExternal( HW_I2C_CHANNEL_1, HW_I2C_MODE_MASTER );
    HWI2CChannelState_T&                             state = hw_i2c_channel_state[HW_I2C_CHANNEL_1];
    const std::array<uint8_t, HW_I2C_RX_BUFFER_SIZE> completed{};
    StageAndPublish( state, HW_I2C_TRANSFER_KIND_MASTER_RX, 0x20U, completed.data(),
                     completed.size() );

    EXPECT_EQ( HW_I2C_Enqueue_Master_Receive( HW_I2C_CHANNEL_1, 0x21U, 1U ), HW_I2C_STATUS_BUSY );
    ASSERT_TRUE( HW_I2C_Consume_Received_Message( HW_I2C_CHANNEL_1 ) );
    EXPECT_EQ( HW_I2C_Enqueue_Master_Receive( HW_I2C_CHANNEL_1, 0x21U, HW_I2C_RX_BUFFER_SIZE ),
               HW_I2C_STATUS_OK );
}

TEST_F( HWI2CTest, RejectedReceiveReservationDoesNotMutateQueue )
{
    ConfigureExternal( HW_I2C_CHANNEL_1, HW_I2C_MODE_MASTER );
    HWI2CChannelState_T&            state = hw_i2c_channel_state[HW_I2C_CHANNEL_1];
    const std::array<uint8_t, 500U> completed{};
    StageAndPublish( state, HW_I2C_TRANSFER_KIND_MASTER_RX, 0x20U, completed.data(),
                     completed.size() );
    I2C3->SR2                = I2C_SR2_BUSY;
    const uint8_t tx_payload = 0x5AU;
    ASSERT_EQ( HW_I2C_Enqueue_Master_Transmit( HW_I2C_CHANNEL_1, 0x30U, &tx_payload, 1U ),
               HW_I2C_STATUS_OK );
    ASSERT_EQ( HW_I2C_Enqueue_Master_Receive( HW_I2C_CHANNEL_1, 0x31U, 12U ), HW_I2C_STATUS_OK );

    const uint8_t original_head  = state.master_queue_head;
    const uint8_t original_tail  = state.master_queue_tail;
    const uint8_t original_count = state.master_queue_count;
    std::array<HWI2CMasterTransaction_T, HW_I2C_MASTER_TRANSACTION_QUEUE_DEPTH> queue_snapshot{};
    std::memcpy( queue_snapshot.data(), state.master_queue, sizeof( state.master_queue ) );

    EXPECT_EQ( HW_I2C_Enqueue_Master_Receive( HW_I2C_CHANNEL_1, 0x32U, 1U ), HW_I2C_STATUS_BUSY );
    EXPECT_EQ( state.master_queue_head, original_head );
    EXPECT_EQ( state.master_queue_tail, original_tail );
    EXPECT_EQ( state.master_queue_count, original_count );
    EXPECT_EQ(
        std::memcmp( queue_snapshot.data(), state.master_queue, sizeof( state.master_queue ) ), 0 );
}

TEST_F( HWI2CTest, DmaCompletionDoesNotReleaseMasterTxBeforePeripheralBtfAndStop )
{
    ConfigureExternal( HW_I2C_CHANNEL_2, HW_I2C_MODE_MASTER, HW_I2C_TRANSFER_DMA,
                       HW_I2C_TRANSFER_DMA );
    const uint8_t payload[] = { 0xA1U, 0xB2U };
    ASSERT_EQ(
        HW_I2C_Enqueue_Master_Transmit( HW_I2C_CHANNEL_2, 0x44U, payload, sizeof( payload ) ),
        HW_I2C_STATUS_OK );
    const uint8_t queued_while_active = 0xC3U;
    ASSERT_EQ( HW_I2C_Enqueue_Master_Transmit( HW_I2C_CHANNEL_2, 0x45U, &queued_while_active, 1U ),
               HW_I2C_STATUS_OK );

    DMA1->HISR = DMA_HISR_TCIF7;
    HW_I2C_DMA_TX_IRQ_CHANNEL_2();

    const HWI2CChannelState_T& state = hw_i2c_channel_state[HW_I2C_CHANNEL_2];
    EXPECT_TRUE( state.dma_tx_transfer_complete );
    EXPECT_TRUE( state.master_queue_active );
    EXPECT_EQ( state.master_queue_count, 2U );
    EXPECT_FALSE( HW_I2C_Is_Transaction_Queue_Complete( HW_I2C_CHANNEL_2 ) );
}

TEST_F( HWI2CTest, BtfCompletionReleasesOneEntryAndStartsNextAfterBusIdle )
{
    ConfigureExternal( HW_I2C_CHANNEL_1, HW_I2C_MODE_MASTER );
    const uint8_t first[]  = { 0x11U };
    const uint8_t second[] = { 0x22U };
    ASSERT_EQ( HW_I2C_Enqueue_Master_Transmit( HW_I2C_CHANNEL_1, 0x40U, first, 1U ),
               HW_I2C_STATUS_OK );
    ASSERT_EQ( HW_I2C_Enqueue_Master_Transmit( HW_I2C_CHANNEL_1, 0x41U, second, 1U ),
               HW_I2C_STATUS_OK );

    HWI2CChannelState_T& state = hw_i2c_channel_state[HW_I2C_CHANNEL_1];
    state.tx_remaining         = 0U;
    I2C3->SR1                  = I2C_SR1_BTF;
    I2C3->SR2                  = I2C_SR2_BUSY;
    HW_I2C_EV_IRQ_CHANNEL_1();

    EXPECT_EQ( state.master_queue_count, 2U );
    EXPECT_TRUE( state.master_queue_active );
    HW_I2C_Service_Transaction_Queue( HW_I2C_CHANNEL_1 );
    EXPECT_EQ( state.master_queue_count, 2U );

    I2C3->SR1 = 0U;
    I2C3->SR2 = 0U;
    HW_I2C_Service_Transaction_Queue( HW_I2C_CHANNEL_1 );
    EXPECT_EQ( state.master_queue_count, 1U );
    EXPECT_TRUE( state.master_queue_active );
    EXPECT_EQ( state.target_address_7bit, 0x41U );
    EXPECT_EQ( state.tx_remaining, 1U );
}

TEST_F( HWI2CTest, QueueCompletionStaysFalseUntilStopConditionAndBusIdle )
{
    ConfigureExternal( HW_I2C_CHANNEL_1, HW_I2C_MODE_MASTER );
    const uint8_t payload = 0x71U;
    ASSERT_EQ( HW_I2C_Enqueue_Master_Transmit( HW_I2C_CHANNEL_1, 0x30U, &payload, 1U ),
               HW_I2C_STATUS_OK );

    HWI2CChannelState_T& state = hw_i2c_channel_state[HW_I2C_CHANNEL_1];
    state.tx_remaining         = 0U;
    I2C3->SR1                  = I2C_SR1_BTF;
    I2C3->SR2                  = I2C_SR2_BUSY;
    HW_I2C_EV_IRQ_CHANNEL_1();
    EXPECT_FALSE( HW_I2C_Is_Transaction_Queue_Complete( HW_I2C_CHANNEL_1 ) );
    EXPECT_EQ( state.master_queue_count, 1U );

    I2C3->SR1 = 0U;
    I2C3->SR2 = 0U;
    EXPECT_TRUE( HW_I2C_Is_Transaction_Queue_Complete( HW_I2C_CHANNEL_1 ) );
    EXPECT_EQ( state.master_queue_count, 0U );
}

TEST_F( HWI2CTest, FmpiAutoendStopReleasesExactlyOneEntry )
{
    ASSERT_EQ( HW_I2C_Configure_Internal_FMPI2C1( 0x33U ), HW_I2C_STATUS_OK );
    const uint8_t first[]  = { 0x01U };
    const uint8_t second[] = { 0x02U, 0x03U };
    ASSERT_EQ( HW_I2C_Enqueue_Master_Transmit( HW_I2C_CHANNEL_FMPI2C1, 0x20U, first, 1U ),
               HW_I2C_STATUS_OK );
    ASSERT_EQ( HW_I2C_Enqueue_Master_Transmit( HW_I2C_CHANNEL_FMPI2C1, 0x21U, second, 2U ),
               HW_I2C_STATUS_OK );

    FMPI2C1->ISR = FMPI2C_ISR_STOPF | FMPI2C_ISR_BUSY;
    HW_I2C_EV_IRQ_FMPI2C1();
    EXPECT_EQ( hw_i2c_channel_state[HW_I2C_CHANNEL_FMPI2C1].master_queue_count, 2U );

    FMPI2C1->ISR = 0U;
    HW_I2C_Service_Transaction_Queue( HW_I2C_CHANNEL_FMPI2C1 );
    const HWI2CChannelState_T& state = hw_i2c_channel_state[HW_I2C_CHANNEL_FMPI2C1];
    EXPECT_EQ( state.master_queue_count, 1U );
    EXPECT_EQ( state.target_address_7bit, 0x21U );
    EXPECT_EQ( state.tx_remaining, 2U );
}

TEST_F( HWI2CTest, NackLatchesFailureAndFlushesMasterQueue )
{
    ASSERT_EQ( HW_I2C_Configure_Internal_FMPI2C1( 0x33U ), HW_I2C_STATUS_OK );
    const uint8_t payload = 0x91U;
    ASSERT_EQ( HW_I2C_Enqueue_Master_Transmit( HW_I2C_CHANNEL_FMPI2C1, 0x20U, &payload, 1U ),
               HW_I2C_STATUS_OK );
    ASSERT_EQ( HW_I2C_Enqueue_Master_Transmit( HW_I2C_CHANNEL_FMPI2C1, 0x21U, &payload, 1U ),
               HW_I2C_STATUS_OK );

    FMPI2C1->ISR = FMPI2C_ISR_NACKF;
    HW_I2C_ER_IRQ_FMPI2C1();

    const HWI2CChannelState_T& state = hw_i2c_channel_state[HW_I2C_CHANNEL_FMPI2C1];
    EXPECT_EQ( state.master_queue_count, 0U );
    EXPECT_FALSE( state.master_queue_active );
    EXPECT_EQ( HW_I2C_Get_And_Clear_Transfer_Result( HW_I2C_CHANNEL_FMPI2C1 ),
               HW_I2C_STATUS_ERROR );
    EXPECT_EQ( HW_I2C_Get_And_Clear_Transfer_Result( HW_I2C_CHANNEL_FMPI2C1 ), HW_I2C_STATUS_OK );
}

TEST_F( HWI2CTest, DmaErrorLatchesFailureAndFlushesMasterQueue )
{
    ConfigureExternal( HW_I2C_CHANNEL_2, HW_I2C_MODE_MASTER, HW_I2C_TRANSFER_DMA,
                       HW_I2C_TRANSFER_DMA );
    const uint8_t payload = 0x52U;
    ASSERT_EQ( HW_I2C_Enqueue_Master_Transmit( HW_I2C_CHANNEL_2, 0x40U, &payload, 1U ),
               HW_I2C_STATUS_OK );
    ASSERT_EQ( HW_I2C_Enqueue_Master_Transmit( HW_I2C_CHANNEL_2, 0x41U, &payload, 1U ),
               HW_I2C_STATUS_OK );

    DMA1->HISR = DMA_HISR_TEIF7;
    HW_I2C_DMA_TX_IRQ_CHANNEL_2();
    EXPECT_EQ( hw_i2c_channel_state[HW_I2C_CHANNEL_2].master_queue_count, 0U );
    EXPECT_EQ( HW_I2C_Get_And_Clear_Transfer_Result( HW_I2C_CHANNEL_2 ), HW_I2C_STATUS_ERROR );
}

TEST_F( HWI2CTest, RecoveryDiscardsActiveQueuedAndCompletedReceiveWork )
{
    ConfigureExternal( HW_I2C_CHANNEL_2, HW_I2C_MODE_MASTER, HW_I2C_TRANSFER_DMA,
                       HW_I2C_TRANSFER_DMA );
    HWI2CChannelState_T& state       = hw_i2c_channel_state[HW_I2C_CHANNEL_2];
    const uint8_t        completed[] = { 0x11U, 0x22U };
    StageAndPublish( state, HW_I2C_TRANSFER_KIND_MASTER_RX, 0x40U, completed, sizeof( completed ) );

    ASSERT_EQ( HW_I2C_Enqueue_Master_Receive( HW_I2C_CHANNEL_2, 0x41U, 4U ), HW_I2C_STATUS_OK );
    const uint8_t follower = 0x33U;
    ASSERT_EQ( HW_I2C_Enqueue_Master_Transmit( HW_I2C_CHANNEL_2, 0x42U, &follower, 1U ),
               HW_I2C_STATUS_OK );
    ASSERT_TRUE( state.master_queue_active );
    ASSERT_EQ( state.master_queue_count, 2U );
    ASSERT_NE( DMA1_Stream2->CR & DMA_SxCR_EN, 0U );

    EXPECT_EQ( HW_I2C_Recover_Channel( HW_I2C_CHANNEL_2 ), HW_I2C_STATUS_ERROR );

    EXPECT_FALSE( state.transfer_in_progress );
    EXPECT_FALSE( state.master_queue_active );
    EXPECT_EQ( state.transfer_kind, HW_I2C_TRANSFER_KIND_IDLE );
    EXPECT_EQ( state.master_queue_count, 0U );
    EXPECT_EQ( state.master_queue_head, 0U );
    EXPECT_EQ( state.master_queue_tail, 0U );
    EXPECT_EQ( state.rx_transfer_length, 0U );
    EXPECT_EQ( state.rx_received_length, 0U );
    EXPECT_EQ( state.rx_count, 0U );
    EXPECT_EQ( state.rx_message_count, 0U );
    EXPECT_EQ( DMA1_Stream2->CR & DMA_SxCR_EN, 0U );
    EXPECT_EQ( DMA1_Stream7->CR & DMA_SxCR_EN, 0U );
    EXPECT_EQ( I2C2->CR2 & I2C_CR2_DMAEN, 0U );

    EXPECT_EQ( HW_I2C_Get_And_Clear_Transfer_Result( HW_I2C_CHANNEL_2 ), HW_I2C_STATUS_ERROR );
    EXPECT_EQ( HW_I2C_Enqueue_Master_Transmit( HW_I2C_CHANNEL_2, 0x43U, &follower, 1U ),
               HW_I2C_STATUS_OK );
}

TEST_F( HWI2CTest, RecoveryValidatesChannelAndConfiguration )
{
    EXPECT_EQ( HW_I2C_Recover_Channel( HW_I2C_CHANNEL_COUNT ), HW_I2C_STATUS_INVALID_PARAM );
    EXPECT_EQ( HW_I2C_Recover_Channel( HW_I2C_CHANNEL_1 ), HW_I2C_STATUS_NOT_CONFIGURED );
}

TEST_F( HWI2CTest, RecoveryPreservesStoppedExternalLifecycleState )
{
    const HWI2CChannelConfig_T config = {
        .mode             = HW_I2C_MODE_MASTER,
        .speed            = HW_I2C_SPEED_100KHZ,
        .tx_transfer_path = HW_I2C_TRANSFER_INTERRUPT,
        .rx_transfer_path = HW_I2C_TRANSFER_INTERRUPT,
        .own_address_7bit = 0x31U,
    };
    ASSERT_EQ( HW_I2C_Configure_Channel( HW_I2C_CHANNEL_1, &config ), HW_I2C_STATUS_OK );

    EXPECT_EQ( HW_I2C_Recover_Channel( HW_I2C_CHANNEL_1 ), HW_I2C_STATUS_ERROR );
    EXPECT_TRUE( HW_I2C_Is_Channel_Configured( HW_I2C_CHANNEL_1 ) );
    EXPECT_FALSE( HW_I2C_Is_Channel_Started( HW_I2C_CHANNEL_1 ) );
    EXPECT_EQ( I2C3->CR1 & I2C_CR1_PE, 0U );
}

TEST_F( HWI2CTest, RecoveryPreservesStartedExternalLifecycleState )
{
    ConfigureExternal( HW_I2C_CHANNEL_1, HW_I2C_MODE_MASTER );

    EXPECT_EQ( HW_I2C_Recover_Channel( HW_I2C_CHANNEL_1 ), HW_I2C_STATUS_ERROR );
    EXPECT_TRUE( HW_I2C_Is_Channel_Started( HW_I2C_CHANNEL_1 ) );
    EXPECT_NE( I2C3->CR1 & I2C_CR1_PE, 0U );
}

TEST_F( HWI2CTest, BusErrorLatchesFailureAndFlushesMasterQueue )
{
    ConfigureExternal( HW_I2C_CHANNEL_1, HW_I2C_MODE_MASTER );
    const uint8_t payload = 0x62U;
    ASSERT_EQ( HW_I2C_Enqueue_Master_Transmit( HW_I2C_CHANNEL_1, 0x40U, &payload, 1U ),
               HW_I2C_STATUS_OK );
    ASSERT_EQ( HW_I2C_Enqueue_Master_Transmit( HW_I2C_CHANNEL_1, 0x41U, &payload, 1U ),
               HW_I2C_STATUS_OK );

    I2C3->SR1 = I2C_SR1_BERR;
    HW_I2C_ER_IRQ_CHANNEL_1();
    EXPECT_EQ( hw_i2c_channel_state[HW_I2C_CHANNEL_1].master_queue_count, 0U );
    EXPECT_EQ( HW_I2C_Get_And_Clear_Transfer_Result( HW_I2C_CHANNEL_1 ), HW_I2C_STATUS_ERROR );
}

TEST_F( HWI2CTest, ExternalMasterNackLatchesFailureAndFlushesMasterQueue )
{
    ConfigureExternal( HW_I2C_CHANNEL_1, HW_I2C_MODE_MASTER );
    const uint8_t payload = 0x63U;
    ASSERT_EQ( HW_I2C_Enqueue_Master_Transmit( HW_I2C_CHANNEL_1, 0x40U, &payload, 1U ),
               HW_I2C_STATUS_OK );
    ASSERT_EQ( HW_I2C_Enqueue_Master_Transmit( HW_I2C_CHANNEL_1, 0x41U, &payload, 1U ),
               HW_I2C_STATUS_OK );

    I2C3->SR1 = I2C_SR1_AF;
    HW_I2C_ER_IRQ_CHANNEL_1();

    EXPECT_EQ( hw_i2c_channel_state[HW_I2C_CHANNEL_1].master_queue_count, 0U );
    EXPECT_FALSE( hw_i2c_channel_state[HW_I2C_CHANNEL_1].master_queue_active );
    EXPECT_EQ( HW_I2C_Get_And_Clear_Transfer_Result( HW_I2C_CHANNEL_1 ), HW_I2C_STATUS_ERROR );
}

TEST_F( HWI2CTest, MasterRequestValidationRejectsInvalidInputsBeforeStateAccess )
{
    ConfigureExternal( HW_I2C_CHANNEL_1, HW_I2C_MODE_MASTER );
    const uint8_t payload = 0xAAU;

    EXPECT_EQ( HW_I2C_Enqueue_Master_Transmit( HW_I2C_CHANNEL_COUNT, 0x20U, &payload, 1U ),
               HW_I2C_STATUS_INVALID_PARAM );
    EXPECT_EQ( HW_I2C_Enqueue_Master_Transmit( HW_I2C_CHANNEL_1, 0x80U, &payload, 1U ),
               HW_I2C_STATUS_INVALID_PARAM );
    EXPECT_EQ( HW_I2C_Enqueue_Master_Transmit( HW_I2C_CHANNEL_1, 0x20U, nullptr, 1U ),
               HW_I2C_STATUS_INVALID_PARAM );
    EXPECT_EQ( HW_I2C_Enqueue_Master_Transmit( HW_I2C_CHANNEL_1, 0x20U, &payload, 0U ),
               HW_I2C_STATUS_INVALID_PARAM );
    EXPECT_EQ( HW_I2C_Enqueue_Master_Transmit( HW_I2C_CHANNEL_1, 0x20U, nullptr, 0U ),
               HW_I2C_STATUS_INVALID_PARAM );
    EXPECT_FALSE( HW_I2C_Trigger_Master_Receive_External( HW_I2C_CHANNEL_1, 0x20U, 0U ) );
    EXPECT_FALSE( HW_I2C_Trigger_Master_Receive_External(
        HW_I2C_CHANNEL_1, 0x20U, static_cast<uint16_t>( HW_I2C_RX_BUFFER_SIZE + 1U ) ) );
    EXPECT_FALSE( HW_I2C_Trigger_Master_Receive_External( HW_I2C_CHANNEL_COUNT, 0x20U, 1U ) );
    EXPECT_FALSE( HW_I2C_Trigger_Master_Receive_External( HW_I2C_CHANNEL_1, 0x80U, 1U ) );
    EXPECT_EQ( hw_i2c_channel_state[HW_I2C_CHANNEL_1].master_queue_count, 0U );
}

TEST_F( HWI2CTest, FmpiAccepts255BytesAndRejects256BytesForTxAndRx )
{
    ASSERT_EQ( HW_I2C_Configure_Internal_FMPI2C1( 0x33U ), HW_I2C_STATUS_OK );
    std::array<uint8_t, 256U> payload{};

    EXPECT_EQ(
        HW_I2C_Enqueue_Master_Transmit( HW_I2C_CHANNEL_FMPI2C1, 0x20U, payload.data(), 255U ),
        HW_I2C_STATUS_OK );
    EXPECT_EQ(
        HW_I2C_Enqueue_Master_Transmit( HW_I2C_CHANNEL_FMPI2C1, 0x20U, payload.data(), 256U ),
        HW_I2C_STATUS_INVALID_PARAM );

    ASSERT_EQ( HW_I2C_Configure_Internal_FMPI2C1( 0x33U ), HW_I2C_STATUS_OK );
    EXPECT_EQ( HW_I2C_Enqueue_Master_Receive( HW_I2C_CHANNEL_FMPI2C1, 0x20U, 255U ),
               HW_I2C_STATUS_OK );
    EXPECT_EQ( HW_I2C_Enqueue_Master_Receive( HW_I2C_CHANNEL_FMPI2C1, 0x20U, 256U ),
               HW_I2C_STATUS_INVALID_PARAM );
}

TEST_F( HWI2CTest, OneByteMasterReceiveNacksAndStopsBeforeReadingRxne )
{
    ConfigureExternal( HW_I2C_CHANNEL_1, HW_I2C_MODE_MASTER );
    ASSERT_EQ( HW_I2C_Enqueue_Master_Receive( HW_I2C_CHANNEL_1, 0x20U, 1U ), HW_I2C_STATUS_OK );
    HWI2CChannelState_T& state = hw_i2c_channel_state[HW_I2C_CHANNEL_1];

    I2C3->SR1 = I2C_SR1_ADDR;
    HW_I2C_EV_IRQ_CHANNEL_1();
    EXPECT_EQ( I2C3->SR1 & I2C_SR1_ADDR, 0U );
    EXPECT_EQ( I2C3->CR1 & I2C_CR1_ACK, 0U );
    EXPECT_EQ( I2C3->CR1 & I2C_CR1_POS, 0U );
    EXPECT_NE( I2C3->CR1 & I2C_CR1_STOP, 0U );
    EXPECT_EQ( state.rx_received_length, 0U );

    I2C3->DR  = 0xA1U;
    I2C3->SR1 = I2C_SR1_RXNE;
    HW_I2C_EV_IRQ_CHANNEL_1();
    EXPECT_EQ( state.rx_expected_length, 0U );
    EXPECT_EQ( state.rx_received_length, 1U );
    EXPECT_EQ( state.rx_staging_buffer[0], 0xA1U );
    EXPECT_TRUE( state.completion_condition_seen );
}

TEST_F( HWI2CTest, TwoByteMasterReceiveUsesPosAndBtfTail )
{
    ConfigureExternal( HW_I2C_CHANNEL_1, HW_I2C_MODE_MASTER );
    ASSERT_EQ( HW_I2C_Enqueue_Master_Receive( HW_I2C_CHANNEL_1, 0x20U, 2U ), HW_I2C_STATUS_OK );
    HWI2CChannelState_T& state = hw_i2c_channel_state[HW_I2C_CHANNEL_1];

    I2C3->SR1 = I2C_SR1_ADDR;
    HW_I2C_EV_IRQ_CHANNEL_1();
    EXPECT_EQ( I2C3->CR1 & I2C_CR1_ACK, 0U );
    EXPECT_NE( I2C3->CR1 & I2C_CR1_POS, 0U );
    EXPECT_EQ( I2C3->CR1 & I2C_CR1_STOP, 0U );

    I2C3->DR  = 0xB2U;
    I2C3->SR1 = I2C_SR1_BTF;
    HW_I2C_EV_IRQ_CHANNEL_1();
    EXPECT_EQ( state.rx_expected_length, 0U );
    EXPECT_EQ( state.rx_received_length, 2U );
    EXPECT_NE( I2C3->CR1 & I2C_CR1_STOP, 0U );
    EXPECT_EQ( I2C3->CR1 & I2C_CR1_POS, 0U );
    EXPECT_TRUE( state.completion_condition_seen );
}

TEST_F( HWI2CTest, ThreeByteMasterReceiveUsesTwoBtfTailSteps )
{
    ConfigureExternal( HW_I2C_CHANNEL_2, HW_I2C_MODE_MASTER );
    ASSERT_EQ( HW_I2C_Enqueue_Master_Receive( HW_I2C_CHANNEL_2, 0x20U, 3U ), HW_I2C_STATUS_OK );
    HWI2CChannelState_T& state = hw_i2c_channel_state[HW_I2C_CHANNEL_2];

    I2C2->SR1 = I2C_SR1_ADDR;
    HW_I2C_EV_IRQ_CHANNEL_2();
    EXPECT_NE( I2C2->CR1 & I2C_CR1_ACK, 0U );
    EXPECT_NE( I2C2->CR2 & I2C_CR2_ITBUFEN, 0U );

    I2C2->SR1 = I2C_SR1_RXNE;
    HW_I2C_EV_IRQ_CHANNEL_2();
    EXPECT_EQ( state.rx_received_length, 0U );
    EXPECT_EQ( I2C2->CR2 & I2C_CR2_ITBUFEN, 0U );

    I2C2->DR  = 0xC3U;
    I2C2->SR1 = I2C_SR1_BTF;
    HW_I2C_EV_IRQ_CHANNEL_2();
    EXPECT_EQ( state.rx_expected_length, 2U );
    EXPECT_EQ( state.rx_received_length, 1U );
    EXPECT_EQ( I2C2->CR1 & I2C_CR1_ACK, 0U );
    EXPECT_EQ( I2C2->CR1 & I2C_CR1_STOP, 0U );
    EXPECT_FALSE( state.completion_condition_seen );

    I2C2->DR  = 0xC4U;
    I2C2->SR1 = I2C_SR1_BTF;
    HW_I2C_EV_IRQ_CHANNEL_2();
    EXPECT_EQ( state.rx_expected_length, 0U );
    EXPECT_EQ( state.rx_received_length, 3U );
    EXPECT_NE( I2C2->CR1 & I2C_CR1_STOP, 0U );
    EXPECT_TRUE( state.completion_condition_seen );
}

TEST_F( HWI2CTest, LongerInterruptMasterReceiveSwitchesFromRxneToBtfTail )
{
    ConfigureExternal( HW_I2C_CHANNEL_1, HW_I2C_MODE_MASTER );
    ASSERT_EQ( HW_I2C_Enqueue_Master_Receive( HW_I2C_CHANNEL_1, 0x20U, 4U ), HW_I2C_STATUS_OK );
    HWI2CChannelState_T& state = hw_i2c_channel_state[HW_I2C_CHANNEL_1];

    I2C3->SR1 = I2C_SR1_ADDR;
    HW_I2C_EV_IRQ_CHANNEL_1();
    EXPECT_NE( I2C3->CR1 & I2C_CR1_ACK, 0U );
    EXPECT_NE( I2C3->CR2 & I2C_CR2_ITBUFEN, 0U );

    I2C3->DR  = 0xD1U;
    I2C3->SR1 = I2C_SR1_RXNE;
    HW_I2C_EV_IRQ_CHANNEL_1();
    EXPECT_EQ( state.rx_expected_length, 3U );
    EXPECT_EQ( I2C3->CR2 & I2C_CR2_ITBUFEN, 0U );

    I2C3->DR  = 0xD2U;
    I2C3->SR1 = I2C_SR1_BTF;
    HW_I2C_EV_IRQ_CHANNEL_1();
    EXPECT_EQ( state.rx_expected_length, 2U );
    EXPECT_EQ( state.rx_received_length, 2U );
    EXPECT_EQ( I2C3->CR1 & I2C_CR1_ACK, 0U );
    EXPECT_EQ( I2C3->CR1 & I2C_CR1_STOP, 0U );

    I2C3->DR  = 0xD4U;
    I2C3->SR1 = I2C_SR1_BTF;
    HW_I2C_EV_IRQ_CHANNEL_1();
    EXPECT_EQ( state.rx_expected_length, 0U );
    EXPECT_EQ( state.rx_received_length, 4U );
    EXPECT_NE( I2C3->CR1 & I2C_CR1_STOP, 0U );
    EXPECT_TRUE( state.completion_condition_seen );
}

TEST_F( HWI2CTest, DmaMasterReceiveUsesLastAndClearsTailControlsOnCompletion )
{
    ConfigureExternal( HW_I2C_CHANNEL_2, HW_I2C_MODE_MASTER, HW_I2C_TRANSFER_DMA,
                       HW_I2C_TRANSFER_DMA );
    ASSERT_EQ( HW_I2C_Enqueue_Master_Receive( HW_I2C_CHANNEL_2, 0x20U, 6U ), HW_I2C_STATUS_OK );
    HWI2CChannelState_T& state = hw_i2c_channel_state[HW_I2C_CHANNEL_2];
    EXPECT_NE( I2C2->CR2 & I2C_CR2_DMAEN, 0U );
    EXPECT_NE( I2C2->CR2 & I2C_CR2_LAST, 0U );

    I2C2->SR1 = I2C_SR1_ADDR;
    HW_I2C_EV_IRQ_CHANNEL_2();
    EXPECT_EQ( I2C2->SR1 & I2C_SR1_ADDR, 0U );
    EXPECT_NE( I2C2->CR1 & I2C_CR1_ACK, 0U );
    EXPECT_NE( I2C2->CR2 & I2C_CR2_LAST, 0U );

    DMA1->LISR = DMA_LISR_TCIF2;
    HW_I2C_DMA_RX_IRQ_CHANNEL_2();
    EXPECT_EQ( state.rx_received_length, 6U );
    EXPECT_TRUE( state.dma_rx_transfer_complete );
    EXPECT_EQ( DMA1_Stream2->CR & DMA_SxCR_EN, 0U );
    EXPECT_EQ( I2C2->CR2 & I2C_CR2_DMAEN, 0U );
    EXPECT_EQ( I2C2->CR2 & I2C_CR2_LAST, 0U );
    EXPECT_EQ( I2C2->CR1 & ( I2C_CR1_ACK | I2C_CR1_POS ), 0U );
    EXPECT_NE( I2C2->CR1 & I2C_CR1_STOP, 0U );
    EXPECT_TRUE( state.completion_condition_seen );
}

TEST_F( HWI2CTest, SlaveTriggersRejectAnActiveTransfer )
{
    ConfigureExternal( HW_I2C_CHANNEL_1, HW_I2C_MODE_SLAVE );
    const uint8_t payload = 0x77U;
    ASSERT_TRUE( HW_I2C_Load_Stage_Buffer( HW_I2C_CHANNEL_1, &payload, 1U ) );
    ASSERT_TRUE( HW_I2C_Trigger_Slave_Transmit_External( HW_I2C_CHANNEL_1 ) );
    EXPECT_FALSE( HW_I2C_Trigger_Slave_Transmit_External( HW_I2C_CHANNEL_1 ) );
    EXPECT_FALSE( HW_I2C_Trigger_Slave_Receive_External( HW_I2C_CHANNEL_1, 1U ) );
}

TEST_F( HWI2CTest, SlaveTransmitTerminalNackWaitsForStopWithoutLatchingError )
{
    ConfigureExternal( HW_I2C_CHANNEL_1, HW_I2C_MODE_SLAVE );
    const uint8_t payload = 0x78U;
    ASSERT_TRUE( HW_I2C_Load_Stage_Buffer( HW_I2C_CHANNEL_1, &payload, 1U ) );
    ASSERT_TRUE( HW_I2C_Trigger_Slave_Transmit_External( HW_I2C_CHANNEL_1 ) );

    I2C3->SR1 = I2C_SR1_AF;
    HW_I2C_ER_IRQ_CHANNEL_1();

    EXPECT_TRUE( hw_i2c_channel_state[HW_I2C_CHANNEL_1].transfer_in_progress );
    EXPECT_EQ( hw_i2c_channel_state[HW_I2C_CHANNEL_1].transfer_kind,
               HW_I2C_TRANSFER_KIND_SLAVE_TX );
    EXPECT_EQ( HW_I2C_Get_And_Clear_Transfer_Result( HW_I2C_CHANNEL_1 ), HW_I2C_STATUS_OK );

    I2C3->SR1 = I2C_SR1_STOPF;
    I2C3->SR2 = 0U;
    HW_I2C_EV_IRQ_CHANNEL_1();

    EXPECT_FALSE( hw_i2c_channel_state[HW_I2C_CHANNEL_1].transfer_in_progress );
    EXPECT_EQ( hw_i2c_channel_state[HW_I2C_CHANNEL_1].transfer_kind, HW_I2C_TRANSFER_KIND_IDLE );
    EXPECT_EQ( HW_I2C_Get_And_Clear_Transfer_Result( HW_I2C_CHANNEL_1 ), HW_I2C_STATUS_OK );
}

TEST_F( HWI2CTest, LegacyOverflowQueryClearsMatchingLatchedResult )
{
    ConfigureExternal( HW_I2C_CHANNEL_1, HW_I2C_MODE_MASTER );
    HWI2CChannelState_T& state = hw_i2c_channel_state[HW_I2C_CHANNEL_1];
    state.overflow_occurred    = true;
    state.transfer_result      = HW_I2C_STATUS_OVERFLOW;

    EXPECT_TRUE( HW_I2C_Get_Overflow_Status( HW_I2C_CHANNEL_1 ) );
    EXPECT_FALSE( HW_I2C_Get_Overflow_Status( HW_I2C_CHANNEL_1 ) );

    const uint8_t payload = 0x79U;
    EXPECT_EQ( HW_I2C_Enqueue_Master_Transmit( HW_I2C_CHANNEL_1, 0x20U, &payload, 1U ),
               HW_I2C_STATUS_OK );
}

TEST_F( HWI2CTest, InterruptReceiveIsNotPublishedUntilStop )
{
    ConfigureExternal( HW_I2C_CHANNEL_1, HW_I2C_MODE_SLAVE );
    ASSERT_TRUE( HW_I2C_Trigger_Slave_Receive_External( HW_I2C_CHANNEL_1, 4U ) );

    I2C3->DR  = 0xA5U;
    I2C3->SR1 = I2C_SR1_RXNE;
    HW_I2C_EV_IRQ_CHANNEL_1();

    HWI2CRxMessagePeek_T message{};
    ASSERT_TRUE( HW_I2C_Peek_Received_Message( HW_I2C_CHANNEL_1, &message ) );
    EXPECT_EQ( message.descriptor.transfer_kind, HW_I2C_TRANSFER_KIND_IDLE );

    I2C3->SR1 = I2C_SR1_STOPF;
    HW_I2C_EV_IRQ_CHANNEL_1();
    ASSERT_TRUE( HW_I2C_Peek_Received_Message( HW_I2C_CHANNEL_1, &message ) );
    EXPECT_EQ( message.descriptor.transfer_kind, HW_I2C_TRANSFER_KIND_SLAVE_RX );
    EXPECT_EQ( message.descriptor.target_address_7bit, 0U );
    EXPECT_EQ( message.descriptor.length, 1U );
    ASSERT_EQ( message.first.length, 1U );
    EXPECT_EQ( message.first.data[0], 0xA5U );
}

TEST_F( HWI2CTest, DmaSlaveStopPublishesActualPartialLength )
{
    ConfigureExternal( HW_I2C_CHANNEL_2, HW_I2C_MODE_SLAVE, HW_I2C_TRANSFER_DMA,
                       HW_I2C_TRANSFER_DMA );
    ASSERT_TRUE( HW_I2C_Trigger_Slave_Receive_External( HW_I2C_CHANNEL_2, 10U ) );
    HWI2CChannelState_T& state = hw_i2c_channel_state[HW_I2C_CHANNEL_2];
    state.rx_staging_buffer[0] = 1U;
    state.rx_staging_buffer[1] = 2U;
    state.rx_staging_buffer[2] = 3U;
    state.rx_staging_buffer[3] = 4U;
    DMA1_Stream2->NDTR         = 6U;
    I2C2->SR1                  = I2C_SR1_STOPF;
    HW_I2C_EV_IRQ_CHANNEL_2();

    HWI2CRxMessagePeek_T message{};
    ASSERT_TRUE( HW_I2C_Peek_Received_Message( HW_I2C_CHANNEL_2, &message ) );
    EXPECT_EQ( message.descriptor.length, 4U );
    EXPECT_EQ( message.descriptor.transfer_kind, HW_I2C_TRANSFER_KIND_SLAVE_RX );
    EXPECT_EQ( message.descriptor.target_address_7bit, 0U );
}

TEST_F( HWI2CTest, TwoReceivedTransactionsRemainTwoMessages )
{
    HWI2CChannelState_T& state    = hw_i2c_channel_state[HW_I2C_CHANNEL_1];
    const uint8_t        first[]  = { 0x10U, 0x11U };
    const uint8_t        second[] = { 0x20U, 0x21U, 0x22U };
    StageAndPublish( state, HW_I2C_TRANSFER_KIND_MASTER_RX, 0x30U, first, sizeof( first ) );
    StageAndPublish( state, HW_I2C_TRANSFER_KIND_SLAVE_RX, 0U, second, sizeof( second ) );

    HWI2CRxMessagePeek_T message{};
    ASSERT_TRUE( HW_I2C_Peek_Received_Message( HW_I2C_CHANNEL_1, &message ) );
    EXPECT_EQ( message.descriptor.length, 2U );
    EXPECT_EQ( message.descriptor.target_address_7bit, 0x30U );
    ASSERT_TRUE( HW_I2C_Consume_Received_Message( HW_I2C_CHANNEL_1 ) );

    ASSERT_TRUE( HW_I2C_Peek_Received_Message( HW_I2C_CHANNEL_1, &message ) );
    EXPECT_EQ( message.descriptor.length, 3U );
    EXPECT_EQ( message.descriptor.transfer_kind, HW_I2C_TRANSFER_KIND_SLAVE_RX );
    EXPECT_EQ( message.first.data[0], 0x20U );
}

TEST_F( HWI2CTest, WrappedRxStorageRetainsOneMessageBoundary )
{
    HWI2CChannelState_T& state = hw_i2c_channel_state[HW_I2C_CHANNEL_2];
    state.rx_head              = static_cast<uint16_t>( HW_I2C_RX_BUFFER_SIZE - 2U );
    state.rx_tail              = state.rx_head;
    const uint8_t payload[]    = { 1U, 2U, 3U, 4U };
    StageAndPublish( state, HW_I2C_TRANSFER_KIND_MASTER_RX, 0x52U, payload, sizeof( payload ) );

    HWI2CRxMessagePeek_T message{};
    ASSERT_TRUE( HW_I2C_Peek_Received_Message( HW_I2C_CHANNEL_2, &message ) );
    EXPECT_EQ( message.descriptor.length, 4U );
    EXPECT_EQ( message.first.length, 2U );
    EXPECT_EQ( message.second.length, 2U );
    EXPECT_EQ( message.first.data[0], 1U );
    EXPECT_EQ( message.first.data[1], 2U );
    EXPECT_EQ( message.second.data[0], 3U );
    EXPECT_EQ( message.second.data[1], 4U );
}
