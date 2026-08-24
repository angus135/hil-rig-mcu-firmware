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
 *      lifecycle state and Logic Expander mapping, copies RX span data into
 *      caller-owned buffers, and reports TX
 *      completion using the low-level TX empty status.
 *
 *  Notes:
 *      These tests mock the HW SPI and Logic Expander functions used by exec_spi.c.
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
#include "logic_expander.h"
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
    MOCK_METHOD( bool, ConfigureChannel, ( SPIChannel_T peripheral, HWSPIConfig_T configuration ),
                 () );

    MOCK_METHOD( bool, StartChannel, ( SPIChannel_T peripheral ), () );

    MOCK_METHOD( bool, StopChannel, ( SPIChannel_T peripheral ), () );

    MOCK_METHOD( bool, LoadTxBuffer,
                 ( SPIChannel_T peripheral, const uint8_t* data, uint32_t size_bytes ), () );

    MOCK_METHOD( void, TxTrigger, ( SPIChannel_T peripheral ), () );

    MOCK_METHOD( HWSPIRxSpans_T, RxPeek, ( SPIChannel_T peripheral ), () );

    MOCK_METHOD( void, RxConsume, ( SPIChannel_T peripheral, uint32_t bytes_to_consume ), () );

    MOCK_METHOD( bool, TxIsComplete, ( SPIChannel_T peripheral ), () );
    MOCK_METHOD( bool, TxIsFaulted, ( SPIChannel_T peripheral ), () );
};

class MockLogicExpander
{
public:
    MOCK_METHOD( LogicExpanderStatus_T, LoadControlBit,
                 ( LogicExpanderIndex_T, LogicExpanderPort_T, uint8_t, bool ));
    MOCK_METHOD( LogicExpanderStatus_T, SendControlBits, () );
};

static MockHWSPI*         g_mock_hw_spi         = nullptr;
static MockLogicExpander* g_mock_logic_expander = nullptr;

extern "C"
{

bool HW_SPI_Configure_Channel( SPIChannel_T peripheral, HWSPIConfig_T configuration )
{
    return g_mock_hw_spi->ConfigureChannel( peripheral, configuration );
}

bool HW_SPI_Start_Channel( SPIChannel_T peripheral )
{
    return g_mock_hw_spi->StartChannel( peripheral );
}

bool HW_SPI_Stop_Channel( SPIChannel_T peripheral )
{
    return g_mock_hw_spi->StopChannel( peripheral );
}

bool HW_SPI_Load_Tx_Buffer( SPIChannel_T peripheral, const uint8_t* data, uint32_t size )
{
    return g_mock_hw_spi->LoadTxBuffer( peripheral, data, size );
}

void HW_SPI_Tx_Trigger( SPIChannel_T peripheral )
{
    g_mock_hw_spi->TxTrigger( peripheral );
}

HWSPIRxSpans_T HW_SPI_Rx_Peek( SPIChannel_T peripheral )
{
    return g_mock_hw_spi->RxPeek( peripheral );
}

void HW_SPI_Rx_Consume( SPIChannel_T peripheral, uint32_t bytes_to_consume )
{
    g_mock_hw_spi->RxConsume( peripheral, bytes_to_consume );
}

bool HW_SPI_Tx_Is_Complete( SPIChannel_T peripheral )
{
    return g_mock_hw_spi->TxIsComplete( peripheral );
}

bool HW_SPI_Tx_Is_Faulted( SPIChannel_T peripheral )
{
    return g_mock_hw_spi->TxIsFaulted( peripheral );
}

LogicExpanderStatus_T LOGIC_EXPANDER_Load_Control_Bit( LogicExpanderIndex_T expander,
                                                       LogicExpanderPort_T port, uint8_t bit_index,
                                                       bool value )
{
    return g_mock_logic_expander->LoadControlBit( expander, port, bit_index, value );
}

LogicExpanderStatus_T LOGIC_EXPANDER_Send_Control_Bits( void )
{
    return g_mock_logic_expander->SendControlBits();
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
    ::testing::StrictMock<MockHWSPI>       mock_hw_spi;
    ::testing::NiceMock<MockLogicExpander> mock_logic_expander;

    HWSPIConfig_T default_config = {
        .spi_mode  = SPI_MASTER_MODE,
        .data_size = SPI_SIZE_8_BIT,
        .first_bit = SPI_FIRST_MSB,
        .baud_rate = SPI_BAUD_352KBIT,
        .cpol      = SPI_CPOL_LOW,
        .cpha      = SPI_CPHA_1_EDGE,
        .nss_pin   = GPIO_SPI1_NSS,
    };

    void SetUp( void ) override
    {
        g_mock_hw_spi         = &mock_hw_spi;
        g_mock_logic_expander = &mock_logic_expander;

        ON_CALL( mock_logic_expander, LoadControlBit )
            .WillByDefault( ::testing::Return( LOGIC_EXPANDER_STATUS_OK ) );
        ON_CALL( mock_logic_expander, SendControlBits )
            .WillByDefault( ::testing::Return( LOGIC_EXPANDER_STATUS_OK ) );

        ForceAllChannelsDisabled();
    }

    void TearDown( void ) override
    {
        g_mock_hw_spi         = nullptr;
        g_mock_logic_expander = nullptr;
    }

    ExecSPIConfig_T MakeEnabledConfig( SPIMode_T mode = SPI_MASTER_MODE )
    {
        ExecSPIConfig_T config = {
            .is_enabled = true,
            .hardware   = default_config,
        };
        config.hardware.spi_mode = mode;
        return config;
    }

    void ForceChannelDisabled( SPIChannel_T peripheral )
    {
        using ::testing::AnyNumber;
        using ::testing::Return;

        EXPECT_CALL( mock_hw_spi, TxIsComplete( peripheral ) )
            .Times( AnyNumber() )
            .WillRepeatedly( Return( true ) );
        EXPECT_CALL( mock_hw_spi, TxIsFaulted( peripheral ) )
            .Times( AnyNumber() )
            .WillRepeatedly( Return( false ) );
        EXPECT_CALL( mock_hw_spi, StopChannel( peripheral ) )
            .Times( AnyNumber() )
            .WillRepeatedly( Return( true ) );

        const ExecSPIConfig_T config = { .is_enabled = false };
        EXPECT_TRUE( EXEC_SPI_Configure_Channel( peripheral, &config ) );

        ::testing::Mock::VerifyAndClearExpectations( &mock_hw_spi );
    }

    void ForceAllChannelsDisabled( void )
    {
        ForceChannelDisabled( SPI_CHANNEL_0 );
        ForceChannelDisabled( SPI_CHANNEL_1 );
    }
};

/**-----------------------------------------------------------------------------
 *  Test Cases
 *------------------------------------------------------------------------------
 */
TEST_F( ExecSPITest, ConfigureChannel_MasterKeepsChannelStoppedAndMapsChannel0ToB4B5 )
{
    using ::testing::_;
    using ::testing::InSequence;
    using ::testing::Return;

    const ExecSPIConfig_T config = MakeEnabledConfig( SPI_MASTER_MODE );
    InSequence            sequence;

    EXPECT_CALL( mock_logic_expander,
                 LoadControlBit( LOGIC_EXPANDER_PWM_SPI, LOGIC_EXPANDER_PORT_B, 5U, true ) )
        .WillOnce( Return( LOGIC_EXPANDER_STATUS_OK ) );
    EXPECT_CALL( mock_logic_expander,
                 LoadControlBit( LOGIC_EXPANDER_PWM_SPI, LOGIC_EXPANDER_PORT_B, 4U, false ) )
        .WillOnce( Return( LOGIC_EXPANDER_STATUS_OK ) );
    EXPECT_CALL( mock_logic_expander, SendControlBits() )
        .WillOnce( Return( LOGIC_EXPANDER_STATUS_OK ) );
    EXPECT_CALL( mock_hw_spi, ConfigureChannel( SPI_CHANNEL_0, _ ) ).WillOnce( Return( true ) );
    EXPECT_CALL( mock_hw_spi, StartChannel( SPI_CHANNEL_0 ) ).Times( 0 );

    EXPECT_TRUE( EXEC_SPI_Configure_Channel( SPI_CHANNEL_0, &config ) );
    EXPECT_TRUE( EXEC_SPI_Is_Configured( SPI_CHANNEL_0 ) );
    EXPECT_FALSE( EXEC_SPI_Is_Started( SPI_CHANNEL_0 ) );
}

TEST_F( ExecSPITest, ConfigureChannel_SlaveMapsChannel1ToB6B7 )
{
    using ::testing::_;
    using ::testing::InSequence;
    using ::testing::Return;

    const ExecSPIConfig_T config = MakeEnabledConfig( SPI_SLAVE_MODE );
    InSequence            sequence;

    EXPECT_CALL( mock_logic_expander,
                 LoadControlBit( LOGIC_EXPANDER_PWM_SPI, LOGIC_EXPANDER_PORT_B, 7U, false ) )
        .WillOnce( Return( LOGIC_EXPANDER_STATUS_OK ) );
    EXPECT_CALL( mock_logic_expander,
                 LoadControlBit( LOGIC_EXPANDER_PWM_SPI, LOGIC_EXPANDER_PORT_B, 6U, false ) )
        .WillOnce( Return( LOGIC_EXPANDER_STATUS_OK ) );
    EXPECT_CALL( mock_logic_expander, SendControlBits() )
        .WillOnce( Return( LOGIC_EXPANDER_STATUS_OK ) );
    EXPECT_CALL( mock_hw_spi, ConfigureChannel( SPI_CHANNEL_1, _ ) ).WillOnce( Return( true ) );

    EXPECT_TRUE( EXEC_SPI_Configure_Channel( SPI_CHANNEL_1, &config ) );
}

TEST_F( ExecSPITest, ConfigureChannel_DisabledAppliesSafeB4B5StateWithoutHardwareConfigure )
{
    using ::testing::_;
    using ::testing::InSequence;
    using ::testing::Return;

    const ExecSPIConfig_T config = { .is_enabled = false };
    InSequence            sequence;

    EXPECT_CALL( mock_logic_expander,
                 LoadControlBit( LOGIC_EXPANDER_PWM_SPI, LOGIC_EXPANDER_PORT_B, 5U, false ) )
        .WillOnce( Return( LOGIC_EXPANDER_STATUS_OK ) );
    EXPECT_CALL( mock_logic_expander,
                 LoadControlBit( LOGIC_EXPANDER_PWM_SPI, LOGIC_EXPANDER_PORT_B, 4U, false ) )
        .WillOnce( Return( LOGIC_EXPANDER_STATUS_OK ) );
    EXPECT_CALL( mock_logic_expander, SendControlBits() )
        .WillOnce( Return( LOGIC_EXPANDER_STATUS_OK ) );
    EXPECT_CALL( mock_hw_spi, ConfigureChannel( _, _ ) ).Times( 0 );

    EXPECT_TRUE( EXEC_SPI_Configure_Channel( SPI_CHANNEL_0, &config ) );
    EXPECT_FALSE( EXEC_SPI_Is_Configured( SPI_CHANNEL_0 ) );
}

TEST_F( ExecSPITest, ConfigureChannel_RejectsNullInvalidAndDacChannels )
{
    const ExecSPIConfig_T config             = MakeEnabledConfig();
    SPIChannel_T          invalid_peripheral = static_cast<SPIChannel_T>( 99 );

    EXPECT_FALSE( EXEC_SPI_Configure_Channel( SPI_CHANNEL_0, nullptr ) );
    EXPECT_FALSE( EXEC_SPI_Configure_Channel( invalid_peripheral, &config ) );
    EXPECT_FALSE( EXEC_SPI_Configure_Channel( SPI_DAC, &config ) );
}

TEST_F( ExecSPITest, StartAndStopChannel_FollowHardwareAndExternalEnableOrdering )
{
    using ::testing::_;
    using ::testing::InSequence;
    using ::testing::Return;

    const ExecSPIConfig_T config = MakeEnabledConfig( SPI_MASTER_MODE );

    EXPECT_CALL( mock_hw_spi, ConfigureChannel( SPI_CHANNEL_0, _ ) ).WillOnce( Return( true ) );
    ASSERT_TRUE( EXEC_SPI_Configure_Channel( SPI_CHANNEL_0, &config ) );
    ::testing::Mock::VerifyAndClearExpectations( &mock_hw_spi );

    {
        InSequence sequence;
        EXPECT_CALL( mock_hw_spi, StartChannel( SPI_CHANNEL_0 ) ).WillOnce( Return( true ) );
        EXPECT_CALL( mock_logic_expander,
                     LoadControlBit( LOGIC_EXPANDER_PWM_SPI, LOGIC_EXPANDER_PORT_B, 5U, true ) )
            .WillOnce( Return( LOGIC_EXPANDER_STATUS_OK ) );
        EXPECT_CALL( mock_logic_expander,
                     LoadControlBit( LOGIC_EXPANDER_PWM_SPI, LOGIC_EXPANDER_PORT_B, 4U, true ) )
            .WillOnce( Return( LOGIC_EXPANDER_STATUS_OK ) );
        EXPECT_CALL( mock_logic_expander, SendControlBits() )
            .WillOnce( Return( LOGIC_EXPANDER_STATUS_OK ) );
    }
    ASSERT_TRUE( EXEC_SPI_Start_Channel( SPI_CHANNEL_0 ) );

    {
        InSequence sequence;
        EXPECT_CALL( mock_hw_spi, TxIsComplete( SPI_CHANNEL_0 ) ).WillOnce( Return( true ) );
        EXPECT_CALL( mock_logic_expander,
                     LoadControlBit( LOGIC_EXPANDER_PWM_SPI, LOGIC_EXPANDER_PORT_B, 5U, true ) )
            .WillOnce( Return( LOGIC_EXPANDER_STATUS_OK ) );
        EXPECT_CALL( mock_logic_expander,
                     LoadControlBit( LOGIC_EXPANDER_PWM_SPI, LOGIC_EXPANDER_PORT_B, 4U, false ) )
            .WillOnce( Return( LOGIC_EXPANDER_STATUS_OK ) );
        EXPECT_CALL( mock_logic_expander, SendControlBits() )
            .WillOnce( Return( LOGIC_EXPANDER_STATUS_OK ) );
        EXPECT_CALL( mock_hw_spi, StopChannel( SPI_CHANNEL_0 ) ).WillOnce( Return( true ) );
    }
    EXPECT_TRUE( EXEC_SPI_Stop_Channel( SPI_CHANNEL_0 ) );
    EXPECT_TRUE( EXEC_SPI_Is_Configured( SPI_CHANNEL_0 ) );
    EXPECT_FALSE( EXEC_SPI_Is_Started( SPI_CHANNEL_0 ) );
}

TEST_F( ExecSPITest, StopChannel_RejectsIncompleteNonFaultedTransmission )
{
    using ::testing::_;
    using ::testing::Return;

    const ExecSPIConfig_T config = MakeEnabledConfig();
    EXPECT_CALL( mock_hw_spi, ConfigureChannel( SPI_CHANNEL_0, _ ) ).WillOnce( Return( true ) );
    ASSERT_TRUE( EXEC_SPI_Configure_Channel( SPI_CHANNEL_0, &config ) );
    EXPECT_CALL( mock_hw_spi, StartChannel( SPI_CHANNEL_0 ) ).WillOnce( Return( true ) );
    ASSERT_TRUE( EXEC_SPI_Start_Channel( SPI_CHANNEL_0 ) );
    ::testing::Mock::VerifyAndClearExpectations( &mock_hw_spi );

    EXPECT_CALL( mock_hw_spi, TxIsComplete( SPI_CHANNEL_0 ) ).WillOnce( Return( false ) );
    EXPECT_CALL( mock_hw_spi, TxIsFaulted( SPI_CHANNEL_0 ) ).WillOnce( Return( false ) );
    EXPECT_CALL( mock_hw_spi, StopChannel( SPI_CHANNEL_0 ) ).Times( 0 );

    EXPECT_FALSE( EXEC_SPI_Stop_Channel( SPI_CHANNEL_0 ) );
    EXPECT_TRUE( EXEC_SPI_Is_Started( SPI_CHANNEL_0 ) );
}

TEST_F( ExecSPITest, StartChannel_ExternalEnableFailureRollsHardwareBackToStopped )
{
    using ::testing::_;
    using ::testing::Return;

    const ExecSPIConfig_T config = MakeEnabledConfig();
    EXPECT_CALL( mock_hw_spi, ConfigureChannel( SPI_CHANNEL_0, _ ) ).WillOnce( Return( true ) );
    ASSERT_TRUE( EXEC_SPI_Configure_Channel( SPI_CHANNEL_0, &config ) );
    ::testing::Mock::VerifyAndClearExpectations( &mock_hw_spi );

    EXPECT_CALL( mock_hw_spi, StartChannel( SPI_CHANNEL_0 ) ).WillOnce( Return( true ) );
    EXPECT_CALL( mock_logic_expander, SendControlBits() )
        .WillOnce( Return( LOGIC_EXPANDER_STATUS_BUSY ) )
        .WillOnce( Return( LOGIC_EXPANDER_STATUS_OK ) );
    EXPECT_CALL( mock_hw_spi, StopChannel( SPI_CHANNEL_0 ) ).WillOnce( Return( true ) );

    EXPECT_FALSE( EXEC_SPI_Start_Channel( SPI_CHANNEL_0 ) );
    EXPECT_TRUE( EXEC_SPI_Is_Configured( SPI_CHANNEL_0 ) );
    EXPECT_FALSE( EXEC_SPI_Is_Started( SPI_CHANNEL_0 ) );
}

TEST_F( ExecSPITest, StopChannel_FaultedTransmissionPermitsHardwareRecoveryStop )
{
    using ::testing::_;
    using ::testing::Return;

    const ExecSPIConfig_T config = MakeEnabledConfig();
    EXPECT_CALL( mock_hw_spi, ConfigureChannel( SPI_CHANNEL_0, _ ) ).WillOnce( Return( true ) );
    ASSERT_TRUE( EXEC_SPI_Configure_Channel( SPI_CHANNEL_0, &config ) );
    EXPECT_CALL( mock_hw_spi, StartChannel( SPI_CHANNEL_0 ) ).WillOnce( Return( true ) );
    ASSERT_TRUE( EXEC_SPI_Start_Channel( SPI_CHANNEL_0 ) );
    ::testing::Mock::VerifyAndClearExpectations( &mock_hw_spi );

    EXPECT_CALL( mock_hw_spi, TxIsComplete( SPI_CHANNEL_0 ) ).WillOnce( Return( false ) );
    EXPECT_CALL( mock_hw_spi, TxIsFaulted( SPI_CHANNEL_0 ) ).WillOnce( Return( true ) );
    EXPECT_CALL( mock_hw_spi, StopChannel( SPI_CHANNEL_0 ) ).WillOnce( Return( true ) );

    EXPECT_TRUE( EXEC_SPI_Stop_Channel( SPI_CHANNEL_0 ) );
    EXPECT_TRUE( EXEC_SPI_Is_Configured( SPI_CHANNEL_0 ) );
    EXPECT_FALSE( EXEC_SPI_Is_Started( SPI_CHANNEL_0 ) );
}

TEST_F( ExecSPITest, Transmit_SinglePacket_LoadsPacketTriggersOnceAndReturnsTrue )
{
    using ::testing::InSequence;
    using ::testing::Return;

    const uint8_t  tx_data[TEST_TX_SIZE_BYTES] = { 1U, 2U, 3U, 4U, 5U };
    const uint32_t packet_sizes[]              = { TEST_TX_SIZE_BYTES };

    {
        InSequence sequence;

        EXPECT_CALL( mock_hw_spi, LoadTxBuffer( SPI_CHANNEL_0, tx_data, TEST_TX_SIZE_BYTES ) )
            .WillOnce( Return( true ) );

        EXPECT_CALL( mock_hw_spi, TxTrigger( SPI_CHANNEL_0 ) ).Times( 1 );
    }

    bool result = EXEC_SPI_Transmit(
        SPI_CHANNEL_0, tx_data, packet_sizes,
        static_cast<uint32_t>( sizeof( packet_sizes ) / sizeof( packet_sizes[0] ) ) );

    EXPECT_TRUE( result );
}

TEST_F( ExecSPITest, Transmit_MultiplePackets_LoadsEachPacketThenTriggersOnce )
{
    using ::testing::InSequence;
    using ::testing::Invoke;
    using ::testing::Return;

    const uint8_t tx_data[] = {
        0x10U, 0x11U,         // Packet 0
        0x20U, 0x21U, 0x22U,  // Packet 1
        0x30U                 // Packet 2
    };
    const uint32_t packet_sizes[] = { 2U, 3U, 1U };

    {
        InSequence sequence;

        EXPECT_CALL( mock_hw_spi, LoadTxBuffer( SPI_CHANNEL_0, &tx_data[0], packet_sizes[0] ) )
            .WillOnce(
                Invoke( [&]( SPIChannel_T peripheral, const uint8_t* data, uint32_t size_bytes ) {
                    EXPECT_EQ( peripheral, SPI_CHANNEL_0 );
                    EXPECT_EQ( size_bytes, 2U );
                    EXPECT_EQ( 0, std::memcmp( data, &tx_data[0], 2U ) );
                    return true;
                } ) );

        EXPECT_CALL( mock_hw_spi, LoadTxBuffer( SPI_CHANNEL_0, &tx_data[2], packet_sizes[1] ) )
            .WillOnce(
                Invoke( [&]( SPIChannel_T peripheral, const uint8_t* data, uint32_t size_bytes ) {
                    EXPECT_EQ( peripheral, SPI_CHANNEL_0 );
                    EXPECT_EQ( size_bytes, 3U );
                    EXPECT_EQ( 0, std::memcmp( data, &tx_data[2], 3U ) );
                    return true;
                } ) );

        EXPECT_CALL( mock_hw_spi, LoadTxBuffer( SPI_CHANNEL_0, &tx_data[5], packet_sizes[2] ) )
            .WillOnce(
                Invoke( [&]( SPIChannel_T peripheral, const uint8_t* data, uint32_t size_bytes ) {
                    EXPECT_EQ( peripheral, SPI_CHANNEL_0 );
                    EXPECT_EQ( size_bytes, 1U );
                    EXPECT_EQ( 0, std::memcmp( data, &tx_data[5], 1U ) );
                    return true;
                } ) );

        EXPECT_CALL( mock_hw_spi, TxTrigger( SPI_CHANNEL_0 ) ).Times( 1 );
    }

    bool result = EXEC_SPI_Transmit(
        SPI_CHANNEL_0, tx_data, packet_sizes,
        static_cast<uint32_t>( sizeof( packet_sizes ) / sizeof( packet_sizes[0] ) ) );

    EXPECT_TRUE( result );
}

TEST_F( ExecSPITest, Transmit_FirstPacketLoadFails_DoesNotTriggerTxAndReturnsFalse )
{
    using ::testing::InSequence;
    using ::testing::Return;

    const uint8_t  tx_data[TEST_TX_SIZE_BYTES] = { 1U, 2U, 3U, 4U, 5U };
    const uint32_t packet_sizes[]              = { TEST_TX_SIZE_BYTES };

    {
        InSequence sequence;

        EXPECT_CALL( mock_hw_spi, LoadTxBuffer( SPI_CHANNEL_0, tx_data, TEST_TX_SIZE_BYTES ) )
            .WillOnce( Return( false ) );
    }

    EXPECT_CALL( mock_hw_spi, TxTrigger( SPI_CHANNEL_0 ) ).Times( 0 );

    bool result = EXEC_SPI_Transmit(
        SPI_CHANNEL_0, tx_data, packet_sizes,
        static_cast<uint32_t>( sizeof( packet_sizes ) / sizeof( packet_sizes[0] ) ) );

    EXPECT_FALSE( result );
}

TEST_F( ExecSPITest, Transmit_LaterPacketLoadFails_DoesNotLoadRemainingPacketsOrTriggerTx )
{
    using ::testing::InSequence;
    using ::testing::Return;

    const uint8_t tx_data[] = {
        0x01U, 0x02U,         // Packet 0
        0x10U, 0x11U, 0x12U,  // Packet 1
        0xF0U                 // Packet 2
    };
    const uint32_t packet_sizes[] = { 2U, 3U, 1U };

    {
        InSequence sequence;

        EXPECT_CALL( mock_hw_spi, LoadTxBuffer( SPI_CHANNEL_0, &tx_data[0], packet_sizes[0] ) )
            .WillOnce( Return( true ) );

        EXPECT_CALL( mock_hw_spi, LoadTxBuffer( SPI_CHANNEL_0, &tx_data[2], packet_sizes[1] ) )
            .WillOnce( Return( false ) );
    }

    EXPECT_CALL( mock_hw_spi, TxTrigger( SPI_CHANNEL_0 ) ).Times( 0 );

    bool result = EXEC_SPI_Transmit(
        SPI_CHANNEL_0, tx_data, packet_sizes,
        static_cast<uint32_t>( sizeof( packet_sizes ) / sizeof( packet_sizes[0] ) ) );

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
    EXPECT_CALL( mock_hw_spi, TxIsComplete( SPI_CHANNEL_0 ) ).WillOnce( ::testing::Return( true ) );

    bool result = EXEC_SPI_Is_Transmission_Complete( SPI_CHANNEL_0 );

    EXPECT_TRUE( result );
}

TEST_F( ExecSPITest, IsTransmissionComplete_LowLevelReturnsFalse_ReturnsFalse )
{
    EXPECT_CALL( mock_hw_spi, TxIsComplete( SPI_CHANNEL_1 ) )
        .WillOnce( ::testing::Return( false ) );

    bool result = EXEC_SPI_Is_Transmission_Complete( SPI_CHANNEL_1 );

    EXPECT_FALSE( result );
}
