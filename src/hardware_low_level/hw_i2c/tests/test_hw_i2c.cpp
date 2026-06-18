/******************************************************************************
 *  File:       test_hw_i2c.cpp
 *  Author:     Coen Pasitchnyj
 *  Created:    20-Apr-2026
 *
 *  Description:
 *      Unit tests for the low-level hardware I2C driver.
 *
 *      These tests verify configuration, transfer setup, and receive-buffer
 *      behavior for the STM32F446ZE I2C1, I2C2, and FMPI2C1 channels.
 *
 *  Notes:
 *      - Production code is written in C; tests are written in C++.
 *      - C headers and implementation files are included inside extern "C".
 *      - hw_i2c_mocks.h provides the fake STM32 peripheral registers used by
 *        the test build.
 *      - The fixture resets shared driver state before each test.
 *
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <array>
#include <cstring>

extern "C"
{
#ifndef TEST_BUILD
#define TEST_BUILD
#endif
#include "hw_i2c_mocks.h"
#include "hw_i2c.h" /* Module under test */
#include <stdint.h>
#include <stdbool.h>

#include "../hw_i2c.c" /* Module under test */  // NOLINT
}

/**-----------------------------------------------------------------------------
 *  Test Constants / Macros
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Test Doubles / Mocks
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Test Fixture
 *------------------------------------------------------------------------------
 */

/**
 * @brief Test fixture for module tests.
 *      Unit tests for the low-level hardware I2C driver.
 *
 *      These tests verify configuration, transfer setup, and receive-buffer
 *      behavior for the STM32F446ZE I2C1, I2C2, and FMPI2C1 channels.
 * Provides a consistent setup/teardown environment for all test cases.
 */
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
    }

    void TearDown( void ) override
    {
    }
};

/**-----------------------------------------------------------------------------
 *  Test Cases
 *------------------------------------------------------------------------------
 */

TEST_F( HWI2CTest, ConfigureChannel_RejectsInvalidChannelAndNullConfig )
{
    EXPECT_EQ( HW_I2C_Configure_Channel( HW_I2C_CHANNEL_COUNT, nullptr ),
               HW_I2C_STATUS_INVALID_PARAM );
}

TEST_F( HWI2CTest, ConfigureInternal_RejectsOutOfRangeAddress )
{
    EXPECT_EQ( HW_I2C_Configure_Internal_FMPI2C1( 0x80U ), HW_I2C_STATUS_INVALID_PARAM );
}

TEST_F( HWI2CTest, ConfigureInternal_ProgramsFmpi2c1StateAndRegisters )
{
    EXPECT_EQ( HW_I2C_Configure_Internal_FMPI2C1( 0x33U ), HW_I2C_STATUS_OK );

    EXPECT_TRUE( hw_i2c_channel_state[HW_I2C_CHANNEL_FMPI2C1].configured );
    EXPECT_EQ( hw_i2c_channel_state[HW_I2C_CHANNEL_FMPI2C1].config.mode, HW_I2C_MODE_MASTER );
    EXPECT_EQ( hw_i2c_channel_state[HW_I2C_CHANNEL_FMPI2C1].config.speed, HW_I2C_SPEED_100KHZ );
    EXPECT_EQ( hw_i2c_channel_state[HW_I2C_CHANNEL_FMPI2C1].config.tx_transfer_path,
               HW_I2C_TRANSFER_INTERRUPT );
    EXPECT_EQ( hw_i2c_channel_state[HW_I2C_CHANNEL_FMPI2C1].config.rx_transfer_path,
               HW_I2C_TRANSFER_INTERRUPT );
    EXPECT_EQ( hw_i2c_channel_state[HW_I2C_CHANNEL_FMPI2C1].config.own_address_7bit, 0x33U );
    EXPECT_EQ( FMPI2C1->TIMINGR, FMPI2C1_TIMINGR );
    EXPECT_EQ( FMPI2C1->OAR1, ( 0x33U << 1U ) | FMPI2C_OAR1_OA1EN );
    EXPECT_NE( FMPI2C1->CR1 & FMPI2C_CR1_PE, 0U );
    EXPECT_NE( FMPI2C1->CR1 & FMPI2C_CR1_TXIE, 0U );
    EXPECT_NE( FMPI2C1->CR1 & FMPI2C_CR1_RXIE, 0U );
    EXPECT_NE( FMPI2C1->CR1 & FMPI2C_CR1_TCIE, 0U );
    EXPECT_NE( FMPI2C1->CR1 & FMPI2C_CR1_STOPIE, 0U );
    EXPECT_NE( FMPI2C1->CR1 & FMPI2C_CR1_ERRIE, 0U );
}

TEST_F( HWI2CTest, LoadStageBuffer_CopiesPayloadAndUpdatesLength )
{
    const std::array<uint8_t, 3U> payload = { 0x11U, 0x22U, 0x33U };

    EXPECT_TRUE( HW_I2C_Load_Stage_Buffer( HW_I2C_CHANNEL_1, payload.data(), payload.size() ) );
    EXPECT_EQ( hw_i2c_channel_state[HW_I2C_CHANNEL_1].tx_stage_length, payload.size() );
    EXPECT_EQ( std::memcmp( hw_i2c_channel_state[HW_I2C_CHANNEL_1].tx_stage_buffer, payload.data(),
                            payload.size() ),
               0 );
}

TEST_F( HWI2CTest, LoadStageBuffer_RejectsOversizedPayload )
{
    EXPECT_FALSE( HW_I2C_Load_Stage_Buffer( HW_I2C_CHANNEL_1, nullptr,
                                            static_cast<uint16_t>( HW_I2C_TX_STAGE_SIZE + 1U ) ) );
}

TEST_F( HWI2CTest, LoadStageBuffer_RejectsWhileTransferInProgress )
{
    hw_i2c_channel_state[HW_I2C_CHANNEL_2].transfer_in_progress = true;

    EXPECT_FALSE( HW_I2C_Load_Stage_Buffer( HW_I2C_CHANNEL_2, nullptr, 0U ) );
}

TEST_F( HWI2CTest, TriggerMasterTransmitInternal_ProgramsStartFrame )
{
    hw_i2c_channel_state[HW_I2C_CHANNEL_FMPI2C1].tx_stage_length = 5U;

    EXPECT_TRUE( HW_I2C_Trigger_Master_Transmit_Internal( 0x2AU ) );

    EXPECT_TRUE( hw_i2c_channel_state[HW_I2C_CHANNEL_FMPI2C1].transfer_in_progress );
    EXPECT_EQ( hw_i2c_channel_state[HW_I2C_CHANNEL_FMPI2C1].transfer_kind,
               HW_I2C_TRANSFER_KIND_MASTER_TX );
    EXPECT_EQ( hw_i2c_channel_state[HW_I2C_CHANNEL_FMPI2C1].target_address_7bit, 0x2AU );
    EXPECT_EQ( hw_i2c_channel_state[HW_I2C_CHANNEL_FMPI2C1].tx_remaining, 5U );
    EXPECT_EQ( FMPI2C1->CR2, ( 0x2AU << 1U ) | ( 5U << FMPI2C_CR2_NBYTES_Pos ) | FMPI2C_CR2_START
                                 | FMPI2C_CR2_AUTOEND );
}

TEST_F( HWI2CTest, TriggerMasterReceiveInternal_ProgramsReadStartFrame )
{
    EXPECT_TRUE( HW_I2C_Trigger_Master_Receive_Internal( 0x33U, 7U ) );

    EXPECT_TRUE( hw_i2c_channel_state[HW_I2C_CHANNEL_FMPI2C1].transfer_in_progress );
    EXPECT_EQ( hw_i2c_channel_state[HW_I2C_CHANNEL_FMPI2C1].transfer_kind,
               HW_I2C_TRANSFER_KIND_MASTER_RX );
    EXPECT_EQ( hw_i2c_channel_state[HW_I2C_CHANNEL_FMPI2C1].rx_expected_length, 7U );
    EXPECT_EQ( hw_i2c_channel_state[HW_I2C_CHANNEL_FMPI2C1].dma_rx_expected_length, 7U );
    EXPECT_EQ( FMPI2C1->CR2, ( 0x33U << 1U ) | FMPI2C_CR2_RD_WRN | ( 7U << FMPI2C_CR2_NBYTES_Pos )
                                 | FMPI2C_CR2_START | FMPI2C_CR2_AUTOEND );
}

TEST_F( HWI2CTest, PeekReceived_EmptyBufferReturnsEmptySpans )
{
    HWI2CRxPeek_T peek{};

    EXPECT_TRUE( HW_I2C_Peek_Received( HW_I2C_CHANNEL_1, &peek ) );
    EXPECT_EQ( peek.total_length, 0U );
    EXPECT_EQ( peek.first.data, nullptr );
    EXPECT_EQ( peek.first.length, 0U );
    EXPECT_EQ( peek.second.data, nullptr );
    EXPECT_EQ( peek.second.length, 0U );
}

TEST_F( HWI2CTest, PeekReceived_ReturnsSingleSpanForLinearUnreadData )
{
    HWI2CChannelState_T& state = hw_i2c_channel_state[HW_I2C_CHANNEL_1];
    state.rx_tail              = 3U;
    state.rx_head              = 8U;

    HWI2CRxPeek_T peek{};
    EXPECT_TRUE( HW_I2C_Peek_Received( HW_I2C_CHANNEL_1, &peek ) );
    EXPECT_EQ( peek.total_length, 5U );
    EXPECT_EQ( peek.first.data, &state.rx_ring_buffer[3U] );
    EXPECT_EQ( peek.first.length, 5U );
    EXPECT_EQ( peek.second.data, nullptr );
    EXPECT_EQ( peek.second.length, 0U );
}

TEST_F( HWI2CTest, PeekReceived_ReturnsWrappedSpans )
{
    HWI2CChannelState_T& state = hw_i2c_channel_state[HW_I2C_CHANNEL_2];
    state.rx_tail              = static_cast<uint16_t>( HW_I2C_RX_BUFFER_SIZE - 4U );
    state.rx_head              = 4U;

    HWI2CRxPeek_T peek{};
    EXPECT_TRUE( HW_I2C_Peek_Received( HW_I2C_CHANNEL_2, &peek ) );
    EXPECT_EQ( peek.total_length, 8U );
    EXPECT_EQ( peek.first.data, &state.rx_ring_buffer[HW_I2C_RX_BUFFER_SIZE - 4U] );
    EXPECT_EQ( peek.first.length, 4U );
    EXPECT_EQ( peek.second.data, &state.rx_ring_buffer[0] );
    EXPECT_EQ( peek.second.length, 4U );
}

TEST_F( HWI2CTest, ConsumeReceived_RejectsOverConsumptionAndAdvancesTailOnSuccess )
{
    HWI2CChannelState_T& state = hw_i2c_channel_state[HW_I2C_CHANNEL_1];
    state.rx_tail              = 12U;
    state.rx_head              = 20U;

    EXPECT_FALSE( HW_I2C_Consume_Received( HW_I2C_CHANNEL_1, 9U ) );
    EXPECT_TRUE( HW_I2C_Consume_Received( HW_I2C_CHANNEL_1, 5U ) );
    EXPECT_EQ( state.rx_tail, 17U );
}
