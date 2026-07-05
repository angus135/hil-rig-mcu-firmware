/******************************************************************************
 *  File:       test_exec_analogue_output.cpp
 *  Author:     Angus Corr
 *  Created:    25-Mar-2026
 *
 *  Description:
 *      Unit tests for the analogue output execution-layer driver.
 *
 *      These tests verify that the module configures the SPI transport with
 *      the expected settings, emits the correct DAC startup frames, clamps and
 *      scales voltage writes, and rejects invalid use before configuration.
 *
 *  Notes:
 *      These tests mock the HW SPI driver functions used by the module.
 *
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <array>
#include <string.h>

extern "C"
{
#include "exec_analogue_output.h" /* Module under test */
// adjust the CMake to include the hw_spi for the unit tests
#include "../../../hardware_low_level/hw_spi/hw_spi.h"
#include <stdint.h>
#include <stdbool.h>
}

static constexpr std::array<std::array<uint8_t, 3U>, 11U>
    ANALOGUE_OUTPUT_STARTUP_FRAMES_EXTERNAL_VREF = { {
        { 0x40U, 0xFFU, 0xFFU },
        { 0x50U, 0x00U, 0x00U },
        { 0x48U, 0xF0U, 0x00U },
        { 0x00U, 0x00U, 0x00U },
        { 0x08U, 0x00U, 0x00U },
        { 0x10U, 0x00U, 0x00U },
        { 0x18U, 0x00U, 0x00U },
        { 0x20U, 0x00U, 0x00U },
        { 0x28U, 0x00U, 0x00U },
        { 0x30U, 0x00U, 0x00U },
        { 0x38U, 0x00U, 0x00U },
    } };

static constexpr std::array<std::array<uint8_t, 3U>, 11U>
    ANALOGUE_OUTPUT_STARTUP_FRAMES_INTERNAL_VREF = { {
        { 0x40U, 0x00U, 0x00U },
        { 0x50U, 0x00U, 0x00U },
        { 0x48U, 0xF0U, 0x00U },
        { 0x00U, 0x00U, 0x00U },
        { 0x08U, 0x00U, 0x00U },
        { 0x10U, 0x00U, 0x00U },
        { 0x18U, 0x00U, 0x00U },
        { 0x20U, 0x00U, 0x00U },
        { 0x28U, 0x00U, 0x00U },
        { 0x30U, 0x00U, 0x00U },
        { 0x38U, 0x00U, 0x00U },
    } };

class MockHWSPI;

static bool VerifySpiChannelSetupConfig( SPIPeripheral_T     peripheral,
                                         const HWSPIConfig_T configuration )
{
    EXPECT_TRUE( ( peripheral == SPI_CHANNEL_0 ) && ( configuration.spi_mode == SPI_MASTER_MODE )
                 && ( configuration.data_size == SPI_SIZE_8_BIT )
                 && ( configuration.first_bit == SPI_FIRST_MSB )
                 && ( configuration.baud_rate == SPI_BAUD_2M813BIT )
                 && ( configuration.cpol == SPI_CPOL_LOW )
                 && ( configuration.cpha == SPI_CPHA_1_EDGE ) );

    return true;
}

static void VerifyLoadedFrame( SPIPeripheral_T peripheral, const uint8_t* data, uint32_t size_bytes,
                               const std::array<uint8_t, 3U>& expected_frame )
{
    EXPECT_EQ( peripheral, SPI_CHANNEL_0 );
    EXPECT_EQ( size_bytes, 3U );
    EXPECT_EQ( 0, memcmp( data, expected_frame.data(), expected_frame.size() ) );
}

static void ExpectFrameLoad( MockHWSPI&                     mock_hw_spi,
                             const std::array<uint8_t, 3U>& expected_frame );

/**-----------------------------------------------------------------------------
 *  Test Constants / Macros
 *------------------------------------------------------------------------------
 */

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

    MOCK_METHOD( bool, LoadTxBuffer,
                 ( SPIPeripheral_T peripheral, const uint8_t* data, uint32_t size_bytes ), () );

    MOCK_METHOD( void, TxTrigger, ( SPIPeripheral_T peripheral ), () );
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

bool HW_SPI_Load_Tx_Buffer( SPIPeripheral_T peripheral, const uint8_t* data, uint32_t size )
{
    return g_mock_hw_spi->LoadTxBuffer( peripheral, data, size );
}

void HW_SPI_Tx_Trigger( SPIPeripheral_T peripheral )
{
    g_mock_hw_spi->TxTrigger( peripheral );
}
}

static void ExpectFrameLoad( MockHWSPI& mock_hw_spi, const std::array<uint8_t, 3U>& expected_frame )
{
    using ::testing::_;
    using ::testing::Invoke;

    EXPECT_CALL( mock_hw_spi, LoadTxBuffer( SPI_CHANNEL_0, _, 3U ) )
        .WillOnce( Invoke( [expected_frame]( SPIPeripheral_T peripheral, const uint8_t* data,
                                             uint32_t size_bytes ) {
            VerifyLoadedFrame( peripheral, data, size_bytes, expected_frame );
            return true;
        } ) );
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
class ExecAnalogueOutputTest : public ::testing::Test
{
protected:
    ::testing::StrictMock<MockHWSPI> mock_hw_spi;

    void SetUp( void ) override
    {
        g_mock_hw_spi = &mock_hw_spi;
        ForceModuleUnconfigured();
    }

    void TearDown( void ) override
    {
        g_mock_hw_spi = nullptr;
    }

    void ForceModuleUnconfigured( void )
    {
        using ::testing::_;
        using ::testing::Return;

        EXPECT_CALL( mock_hw_spi, LoadTxBuffer( SPI_CHANNEL_0, _, _ ) ).WillOnce( Return( false ) );

        bool result = EXEC_ANALOGUE_OUTPUT_Config( false );

        EXPECT_FALSE( result );

        ::testing::Mock::VerifyAndClearExpectations( &mock_hw_spi );
    }

    void ExpectStartupFrames( bool use_external_vref )
    {
        using ::testing::_;
        using ::testing::InSequence;

        const auto& expected_frames = use_external_vref
                                          ? ANALOGUE_OUTPUT_STARTUP_FRAMES_EXTERNAL_VREF
                                          : ANALOGUE_OUTPUT_STARTUP_FRAMES_INTERNAL_VREF;

        InSequence sequence;

        for ( const auto& expected_frame : expected_frames )
        {
            ExpectFrameLoad( mock_hw_spi, expected_frame );
        }

        EXPECT_CALL( mock_hw_spi, TxTrigger( SPI_CHANNEL_0 ) ).Times( 1 );
    }

    void ExpectSuccessfulSetup( void )
    {
        using ::testing::_;
        using ::testing::Return;

        EXPECT_CALL( mock_hw_spi, ConfigureChannel( SPI_CHANNEL_0, _ ) )
            .WillOnce( ::testing::Invoke( VerifySpiChannelSetupConfig ) );

        EXPECT_CALL( mock_hw_spi, StartChannel( SPI_CHANNEL_0 ) ).Times( 1 );
    }

    void ExpectSuccessfulConfig( bool use_external_vref )
    {
        ExpectStartupFrames( use_external_vref );
    }

    void ExpectSingleWriteFrame( uint8_t channel, uint16_t count )
    {
        using ::testing::_;
        using ::testing::Invoke;

        const std::array<uint8_t, 3U> expected_frame = {
            static_cast<uint8_t>( ( channel & 0x1FU ) << 3U ),
            static_cast<uint8_t>( ( count >> 8U ) & 0xFFU ),
            static_cast<uint8_t>( count & 0xFFU ),
        };

        EXPECT_CALL( mock_hw_spi, LoadTxBuffer( SPI_CHANNEL_0, _, 3U ) )
            .WillOnce( Invoke( [expected_frame]( SPIPeripheral_T peripheral, const uint8_t* data,
                                                 uint32_t size_bytes ) {
                EXPECT_EQ( peripheral, SPI_CHANNEL_0 );
                EXPECT_EQ( size_bytes, 3U );
                EXPECT_EQ( 0, memcmp( data, expected_frame.data(), expected_frame.size() ) );
                return true;
            } ) );

        EXPECT_CALL( mock_hw_spi, TxTrigger( SPI_CHANNEL_0 ) ).Times( 1 );
    }
};

/**-----------------------------------------------------------------------------
 *  Test Cases
 *------------------------------------------------------------------------------
 */

TEST_F( ExecAnalogueOutputTest, SpiChannelSetup_ConfiguresAndStartsChannel )
{
    ExpectSuccessfulSetup();

    bool result = EXEC_ANALOGUE_OUTPUT_SPI_Channel_Setup();

    EXPECT_TRUE( result );
}

TEST_F( ExecAnalogueOutputTest, SpiChannelSetup_ConfigureFails_DoesNotStartChannel )
{
    using ::testing::_;
    using ::testing::Return;

    EXPECT_CALL( mock_hw_spi, ConfigureChannel( SPI_CHANNEL_0, _ ) ).WillOnce( Return( false ) );
    EXPECT_CALL( mock_hw_spi, StartChannel( SPI_CHANNEL_0 ) ).Times( 0 );

    bool result = EXEC_ANALOGUE_OUTPUT_SPI_Channel_Setup();

    EXPECT_FALSE( result );
}

TEST_F( ExecAnalogueOutputTest, Config_ExternalVref_LoadsStartupFramesAndMarksConfigured )
{
    ExpectSuccessfulConfig( true );

    bool result = EXEC_ANALOGUE_OUTPUT_Config( true );

    EXPECT_TRUE( result );
    EXPECT_TRUE( EXEC_ANALOG_OUTPUT_Is_Configured() );
}

TEST_F( ExecAnalogueOutputTest, Config_LoadFailure_ReturnsFalseAndLeavesModuleUnconfigured )
{
    using ::testing::_;
    using ::testing::Return;

    EXPECT_CALL( mock_hw_spi, LoadTxBuffer( SPI_CHANNEL_0, _, _ ) ).WillOnce( Return( false ) );
    EXPECT_CALL( mock_hw_spi, TxTrigger( SPI_CHANNEL_0 ) ).Times( 0 );

    bool result = EXEC_ANALOGUE_OUTPUT_Config( false );

    EXPECT_FALSE( result );
    EXPECT_FALSE( EXEC_ANALOG_OUTPUT_Is_Configured() );
}

TEST_F( ExecAnalogueOutputTest, WriteVoltage_NotConfigured_ReturnsFalseWithoutSPITraffic )
{
    EXPECT_FALSE( EXEC_ANALOG_OUTPUT_Is_Configured() );

    bool result = EXEC_ANALOG_OUTPUT_Write_Voltage( 0U, 10.0F );

    EXPECT_FALSE( result );
}

TEST_F( ExecAnalogueOutputTest, WriteVoltage_ClampsLowVoltageAndWritesZeroCode )
{
    ExpectSuccessfulConfig( false );
    ASSERT_TRUE( EXEC_ANALOGUE_OUTPUT_Config( false ) );
    ::testing::Mock::VerifyAndClearExpectations( &mock_hw_spi );

    ExpectSingleWriteFrame( 0U, 0U );

    bool result = EXEC_ANALOG_OUTPUT_Write_Voltage( 0U, -3.5F );

    EXPECT_TRUE( result );
}

TEST_F( ExecAnalogueOutputTest, WriteVoltage_ClampsHighVoltageAndWritesFullScaleCode )
{
    ExpectSuccessfulConfig( true );
    ASSERT_TRUE( EXEC_ANALOGUE_OUTPUT_Config( true ) );
    ::testing::Mock::VerifyAndClearExpectations( &mock_hw_spi );

    ExpectSingleWriteFrame( 5U, 4095U );

    bool result = EXEC_ANALOG_OUTPUT_Write_Voltage( 5U, 99.0F );

    EXPECT_TRUE( result );
}

TEST_F( ExecAnalogueOutputTest, WriteVoltage_MidScaleVoltageRoundsToNearestCount )
{
    ExpectSuccessfulConfig( false );
    ASSERT_TRUE( EXEC_ANALOGUE_OUTPUT_Config( false ) );
    ::testing::Mock::VerifyAndClearExpectations( &mock_hw_spi );

    ExpectSingleWriteFrame( 2U, 2048U );

    bool result = EXEC_ANALOG_OUTPUT_Write_Voltage( 2U, 10.0F );

    EXPECT_TRUE( result );
}

TEST_F( ExecAnalogueOutputTest, WriteVoltage_InvalidChannel_ReturnsFalseWithoutSPIWrite )
{
    ExpectSuccessfulConfig( false );
    ASSERT_TRUE( EXEC_ANALOGUE_OUTPUT_Config( false ) );
    ::testing::Mock::VerifyAndClearExpectations( &mock_hw_spi );

    EXPECT_CALL( mock_hw_spi, LoadTxBuffer( SPI_CHANNEL_0, ::testing::_, ::testing::_ ) )
        .Times( 0 );
    EXPECT_CALL( mock_hw_spi, TxTrigger( SPI_CHANNEL_0 ) ).Times( 0 );

    bool result = EXEC_ANALOG_OUTPUT_Write_Voltage( 6U, 12.0F );

    EXPECT_FALSE( result );
}
