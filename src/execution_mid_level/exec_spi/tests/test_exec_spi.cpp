/******************************************************************************
 *  File:       test_exec_spi.cpp
 *  Author:     Angus Corr
 *  Created:    25-Apr-2026
 *
 *  Description:
 *      Unit tests for the execution-level SPI wrapper.
 *
 *      These tests verify that the EXEC SPI layer correctly forwards operations
 *      to the low-level HW SPI driver, maintains its minimal configuration
 *      state, copies RX span data into caller-owned buffers, and reports TX
 *      completion using the low-level TX empty status.
 *
 *  Notes:
 *      These tests mock the HW SPI driver functions used by exec_spi.c.
 *
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <cstring>

extern "C"
{
#include <stdint.h>
#include <stdbool.h>

#include "exec_spi.h"
#include "hw_spi.h"
}

/**-----------------------------------------------------------------------------
 *  Test Constants / Macros
 *------------------------------------------------------------------------------
 */

static constexpr uint32_t TEST_TX_SIZE_BYTES        = 5U;
static constexpr uint32_t TEST_RX_BUFFER_SIZE       = 32U;
static constexpr uint32_t TEST_SMALL_RX_BUFFER_SIZE = 4U;

/**-----------------------------------------------------------------------------
 *  Test Doubles / Mocks
 *------------------------------------------------------------------------------
 */

class MockHWSPI
{
public:
    MOCK_METHOD( bool, ConfigureChannel,
                 ( SPIPeripheral_T peripheral, HWSPIConfig_T configuration ), () );

    MOCK_METHOD( void, StartChannel, ( SPIPeripheral_T peripheral ), () );

    MOCK_METHOD( void, StopChannel, ( SPIPeripheral_T peripheral ), () );

    MOCK_METHOD( bool, LoadTxBuffer,
                 ( SPIPeripheral_T peripheral, const uint8_t* data, uint32_t size_bytes ), () );

    MOCK_METHOD( void, TxTrigger, ( SPIPeripheral_T peripheral ), () );

    MOCK_METHOD( HWSPIRxSpans_T, RxPeek, ( SPIPeripheral_T peripheral ), () );

    MOCK_METHOD( void, RxConsume, ( SPIPeripheral_T peripheral, uint32_t bytes_to_consume ), () );

    MOCK_METHOD( bool, TxBufferEmpty, ( SPIPeripheral_T peripheral ), () );
};

static MockHWSPI* g_mock_hw_spi = nullptr;

extern "C"
{

bool HW_SPI_Configure_Channel( SPIPeripheral_T peripheral, HWSPIConfig_T configuration )
{
    return g_mock_hw_spi->ConfigureChannel( peripheral, configuration );
}

void HW_SPI_Start_Channel( SPIPeripheral_T peripheral )
{
    g_mock_hw_spi->StartChannel( peripheral );
}

void HW_SPI_Stop_Channel( SPIPeripheral_T peripheral )
{
    g_mock_hw_spi->StopChannel( peripheral );
}

bool HW_SPI_Load_Tx_Buffer( SPIPeripheral_T peripheral, const uint8_t* data, uint32_t size )
{
    return g_mock_hw_spi->LoadTxBuffer( peripheral, data, size );
}

void HW_SPI_Tx_Trigger( SPIPeripheral_T peripheral )
{
    g_mock_hw_spi->TxTrigger( peripheral );
}

HWSPIRxSpans_T HW_SPI_Rx_Peek( SPIPeripheral_T peripheral )
{
    return g_mock_hw_spi->RxPeek( peripheral );
}

void HW_SPI_Rx_Consume( SPIPeripheral_T peripheral, uint32_t bytes_to_consume )
{
    g_mock_hw_spi->RxConsume( peripheral, bytes_to_consume );
}

bool HW_SPI_Tx_Buffer_Empty( SPIPeripheral_T peripheral )
{
    return g_mock_hw_spi->TxBufferEmpty( peripheral );
}
}

/**-----------------------------------------------------------------------------
 *  Test Fixture
 *------------------------------------------------------------------------------
 */

/**
 * @brief Test fixture for module tests.
 *
 * Provides a consistent setup/teardown environment for all test cases.
 */
class ExecSPITest : public ::testing::Test
{
protected:
    ::testing::StrictMock<MockHWSPI> mock_hw_spi;

    HWSPIConfig_T default_config = {
        .spi_mode  = SPI_MASTER_MODE,
        .data_size = SPI_SIZE_8_BIT,
        .first_bit = SPI_FIRST_MSB,
        .baud_rate = SPI_BAUD_352KBIT,
        .cpol      = SPI_CPOL_LOW,
        .cpha      = SPI_CPHA_1_EDGE,
    };

    void SetUp( void ) override
    {
        g_mock_hw_spi = &mock_hw_spi;

        ForceAllChannelsUnconfigured();
    }

    void TearDown( void ) override
    {
        g_mock_hw_spi = nullptr;
    }

    void ForceChannelUnconfigured( SPIPeripheral_T peripheral )
    {
        using ::testing::_;
        using ::testing::AnyNumber;
        using ::testing::Return;

        /*
         * exec_spi.c keeps private static channel state. To make each test
         * independent, force the selected channel back to UNCONFIGURED by
         * making configuration fail. If a previous test left the channel active,
         * EXEC_SPI_Configure_Channel() may also stop it first.
         */
        EXPECT_CALL( mock_hw_spi, StopChannel( peripheral ) ).Times( AnyNumber() );

        EXPECT_CALL( mock_hw_spi, ConfigureChannel( peripheral, _ ) ).WillOnce( Return( false ) );

        bool result = EXEC_SPI_Configure_Channel( peripheral, default_config );

        EXPECT_FALSE( result );

        ::testing::Mock::VerifyAndClearExpectations( &mock_hw_spi );
    }

    void ForceAllChannelsUnconfigured( void )
    {
        ForceChannelUnconfigured( SPI_CHANNEL_0 );
        ForceChannelUnconfigured( SPI_CHANNEL_1 );
        ForceChannelUnconfigured( SPI_DAC );
    }
};

/**-----------------------------------------------------------------------------
 *  Test Cases
 *------------------------------------------------------------------------------
 */

TEST_F( ExecSPITest, ConfigureChannel_UnconfiguredChannel_ConfiguresAndStartsChannel )
{
    using ::testing::_;

    EXPECT_CALL( mock_hw_spi, StopChannel( SPI_CHANNEL_0 ) ).Times( 0 );

    EXPECT_CALL( mock_hw_spi, ConfigureChannel( SPI_CHANNEL_0, _ ) )
        .WillOnce( ::testing::Return( true ) );

    EXPECT_CALL( mock_hw_spi, StartChannel( SPI_CHANNEL_0 ) ).Times( 1 );

    bool result = EXEC_SPI_Configure_Channel( SPI_CHANNEL_0, default_config );

    EXPECT_TRUE( result );
}

TEST_F( ExecSPITest, ConfigureChannel_ActiveChannel_StopsBeforeReconfiguring )
{
    using ::testing::_;
    using ::testing::InSequence;
    using ::testing::Return;

    {
        InSequence sequence;

        EXPECT_CALL( mock_hw_spi, ConfigureChannel( SPI_CHANNEL_0, _ ) ).WillOnce( Return( true ) );

        EXPECT_CALL( mock_hw_spi, StartChannel( SPI_CHANNEL_0 ) ).Times( 1 );
    }

    ASSERT_TRUE( EXEC_SPI_Configure_Channel( SPI_CHANNEL_0, default_config ) );

    ::testing::Mock::VerifyAndClearExpectations( &mock_hw_spi );

    HWSPIConfig_T updated_config = default_config;
    updated_config.spi_mode      = SPI_SLAVE_MODE;
    updated_config.cpol          = SPI_CPOL_HIGH;
    updated_config.cpha          = SPI_CPHA_2_EDGE;

    {
        InSequence sequence;

        EXPECT_CALL( mock_hw_spi, StopChannel( SPI_CHANNEL_0 ) ).Times( 1 );

        EXPECT_CALL( mock_hw_spi, ConfigureChannel( SPI_CHANNEL_0, _ ) ).WillOnce( Return( true ) );

        EXPECT_CALL( mock_hw_spi, StartChannel( SPI_CHANNEL_0 ) ).Times( 1 );
    }

    bool result = EXEC_SPI_Configure_Channel( SPI_CHANNEL_0, updated_config );

    EXPECT_TRUE( result );
}

TEST_F( ExecSPITest, ConfigureChannel_LowLevelConfigureFails_DoesNotStartChannel )
{
    using ::testing::_;

    EXPECT_CALL( mock_hw_spi, ConfigureChannel( SPI_CHANNEL_1, _ ) )
        .WillOnce( ::testing::Return( false ) );

    EXPECT_CALL( mock_hw_spi, StartChannel( SPI_CHANNEL_1 ) ).Times( 0 );

    bool result = EXEC_SPI_Configure_Channel( SPI_CHANNEL_1, default_config );

    EXPECT_FALSE( result );
}

TEST_F( ExecSPITest, ConfigureChannel_InvalidPeripheral_ReturnsFalseWithoutLowLevelCalls )
{
    SPIPeripheral_T invalid_peripheral = static_cast<SPIPeripheral_T>( 99 );

    EXPECT_CALL( mock_hw_spi, StopChannel( ::testing::_ ) ).Times( 0 );

    EXPECT_CALL( mock_hw_spi, ConfigureChannel( ::testing::_, ::testing::_ ) ).Times( 0 );

    EXPECT_CALL( mock_hw_spi, StartChannel( ::testing::_ ) ).Times( 0 );

    bool result = EXEC_SPI_Configure_Channel( invalid_peripheral, default_config );

    EXPECT_FALSE( result );
}

TEST_F( ExecSPITest, Transmit_LoadTxBufferSucceeds_TriggersTxAndReturnsTrue )
{
    using ::testing::ElementsAreArray;
    using ::testing::Pointee;
    using ::testing::Return;

    const uint8_t tx_data[TEST_TX_SIZE_BYTES] = { 1U, 2U, 3U, 4U, 5U };

    EXPECT_CALL( mock_hw_spi, LoadTxBuffer( SPI_CHANNEL_0, tx_data, TEST_TX_SIZE_BYTES ) )
        .WillOnce( Return( true ) );

    EXPECT_CALL( mock_hw_spi, TxTrigger( SPI_CHANNEL_0 ) ).Times( 1 );

    bool result = EXEC_SPI_Transmit( SPI_CHANNEL_0, tx_data, TEST_TX_SIZE_BYTES );

    EXPECT_TRUE( result );
}

TEST_F( ExecSPITest, Transmit_LoadTxBufferFails_DoesNotTriggerTxAndReturnsFalse )
{
    const uint8_t tx_data[TEST_TX_SIZE_BYTES] = { 1U, 2U, 3U, 4U, 5U };

    EXPECT_CALL( mock_hw_spi, LoadTxBuffer( SPI_CHANNEL_0, tx_data, TEST_TX_SIZE_BYTES ) )
        .WillOnce( ::testing::Return( false ) );

    EXPECT_CALL( mock_hw_spi, TxTrigger( SPI_CHANNEL_0 ) ).Times( 0 );

    bool result = EXEC_SPI_Transmit( SPI_CHANNEL_0, tx_data, TEST_TX_SIZE_BYTES );

    EXPECT_FALSE( result );
}

TEST_F( ExecSPITest, Receive_SingleSpanAvailable_CopiesDataUpdatesSizeAndConsumes )
{
    const uint8_t first_span_data[] = { 'H', 'e', 'l', 'l', 'o' };

    HWSPIRxSpans_T spans = {
        .first_span =
            {
                .data         = first_span_data,
                .length_bytes = sizeof( first_span_data ),
            },
        .second_span =
            {
                .data         = nullptr,
                .length_bytes = 0U,
            },
        .total_length_bytes = sizeof( first_span_data ),
    };

    uint8_t  rx_buffer[TEST_RX_BUFFER_SIZE] = { 0 };
    uint32_t rx_buffer_size_bytes           = sizeof( rx_buffer );

    EXPECT_CALL( mock_hw_spi, RxPeek( SPI_CHANNEL_1 ) ).WillOnce( ::testing::Return( spans ) );

    EXPECT_CALL( mock_hw_spi, RxConsume( SPI_CHANNEL_1, sizeof( first_span_data ) ) ).Times( 1 );

    bool result = EXEC_SPI_Receive( SPI_CHANNEL_1, rx_buffer, &rx_buffer_size_bytes );

    EXPECT_TRUE( result );
    EXPECT_EQ( sizeof( first_span_data ), rx_buffer_size_bytes );
    EXPECT_EQ( 0, std::memcmp( rx_buffer, first_span_data, sizeof( first_span_data ) ) );
}

TEST_F( ExecSPITest, Receive_TwoSpansAvailable_CopiesBothSpansInOrderAndConsumesTotal )
{
    const uint8_t first_span_data[]  = { 'A', 'B', 'C' };
    const uint8_t second_span_data[] = { 'D', 'E', 'F', 'G' };

    HWSPIRxSpans_T spans = {
        .first_span =
            {
                .data         = first_span_data,
                .length_bytes = sizeof( first_span_data ),
            },
        .second_span =
            {
                .data         = second_span_data,
                .length_bytes = sizeof( second_span_data ),
            },
        .total_length_bytes = sizeof( first_span_data ) + sizeof( second_span_data ),
    };

    const uint8_t expected_data[] = { 'A', 'B', 'C', 'D', 'E', 'F', 'G' };

    uint8_t  rx_buffer[TEST_RX_BUFFER_SIZE] = { 0 };
    uint32_t rx_buffer_size_bytes           = sizeof( rx_buffer );

    EXPECT_CALL( mock_hw_spi, RxPeek( SPI_CHANNEL_0 ) ).WillOnce( ::testing::Return( spans ) );

    EXPECT_CALL( mock_hw_spi, RxConsume( SPI_CHANNEL_0, sizeof( expected_data ) ) ).Times( 1 );

    bool result = EXEC_SPI_Receive( SPI_CHANNEL_0, rx_buffer, &rx_buffer_size_bytes );

    EXPECT_TRUE( result );
    EXPECT_EQ( sizeof( expected_data ), rx_buffer_size_bytes );
    EXPECT_EQ( 0, std::memcmp( rx_buffer, expected_data, sizeof( expected_data ) ) );
}

TEST_F( ExecSPITest, Receive_NoDataAvailable_UpdatesSizeToZeroAndConsumesZero )
{
    HWSPIRxSpans_T spans = {
        .first_span =
            {
                .data         = nullptr,
                .length_bytes = 0U,
            },
        .second_span =
            {
                .data         = nullptr,
                .length_bytes = 0U,
            },
        .total_length_bytes = 0U,
    };

    uint8_t  rx_buffer[TEST_RX_BUFFER_SIZE] = { 0xAAU };
    uint32_t rx_buffer_size_bytes           = sizeof( rx_buffer );

    EXPECT_CALL( mock_hw_spi, RxPeek( SPI_CHANNEL_0 ) ).WillOnce( ::testing::Return( spans ) );

    EXPECT_CALL( mock_hw_spi, RxConsume( SPI_CHANNEL_0, 0U ) ).Times( 1 );

    bool result = EXEC_SPI_Receive( SPI_CHANNEL_0, rx_buffer, &rx_buffer_size_bytes );

    EXPECT_TRUE( result );
    EXPECT_EQ( 0U, rx_buffer_size_bytes );
}

TEST_F( ExecSPITest, Receive_DestinationBufferTooSmall_ReturnsFalseAndDoesNotConsume )
{
    const uint8_t first_span_data[] = { 'H', 'e', 'l', 'l', 'o' };

    HWSPIRxSpans_T spans = {
        .first_span =
            {
                .data         = first_span_data,
                .length_bytes = sizeof( first_span_data ),
            },
        .second_span =
            {
                .data         = nullptr,
                .length_bytes = 0U,
            },
        .total_length_bytes = sizeof( first_span_data ),
    };

    uint8_t  rx_buffer[TEST_SMALL_RX_BUFFER_SIZE] = { 0 };
    uint32_t rx_buffer_size_bytes                 = sizeof( rx_buffer );

    EXPECT_CALL( mock_hw_spi, RxPeek( SPI_DAC ) ).WillOnce( ::testing::Return( spans ) );

    EXPECT_CALL( mock_hw_spi, RxConsume( SPI_DAC, ::testing::_ ) ).Times( 0 );

    bool result = EXEC_SPI_Receive( SPI_DAC, rx_buffer, &rx_buffer_size_bytes );

    EXPECT_FALSE( result );
    EXPECT_EQ( TEST_SMALL_RX_BUFFER_SIZE, rx_buffer_size_bytes );
}

TEST_F( ExecSPITest, IsTransmissionComplete_LowLevelReturnsTrue_ReturnsTrue )
{
    EXPECT_CALL( mock_hw_spi, TxBufferEmpty( SPI_CHANNEL_0 ) )
        .WillOnce( ::testing::Return( true ) );

    bool result = EXEC_SPI_Is_Transmission_Complete( SPI_CHANNEL_0 );

    EXPECT_TRUE( result );
}

TEST_F( ExecSPITest, IsTransmissionComplete_LowLevelReturnsFalse_ReturnsFalse )
{
    EXPECT_CALL( mock_hw_spi, TxBufferEmpty( SPI_CHANNEL_1 ) )
        .WillOnce( ::testing::Return( false ) );

    bool result = EXEC_SPI_Is_Transmission_Complete( SPI_CHANNEL_1 );

    EXPECT_FALSE( result );
}
