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
#include <limits>
#include <string.h>

extern "C"
{
#include "exec_analogue_output.h" /* Module under test */
#include "hw_spi.h"
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

static bool VerifySpiChannelSetupConfig( SPIChannel_T        peripheral,
                                         const HWSPIConfig_T configuration )
{
    EXPECT_TRUE( ( peripheral == SPI_DAC ) && ( configuration.spi_mode == SPI_MASTER_MODE )
                 && ( configuration.data_size == SPI_SIZE_8_BIT )
                 && ( configuration.first_bit == SPI_FIRST_MSB )
                 && ( configuration.baud_rate == SPI_BAUD_703KBIT )
                 && ( configuration.cpol == SPI_CPOL_LOW )
                 && ( configuration.cpha == SPI_CPHA_1_EDGE )
                 && ( configuration.nss_pin == GPIO_SPI4_NSS ) );

    return true;
}

static void VerifyLoadedFrame( SPIChannel_T peripheral, const uint8_t* data, uint32_t size_bytes,
                               const std::array<uint8_t, 3U>& expected_frame )
{
    EXPECT_EQ( peripheral, SPI_DAC );
    EXPECT_EQ( size_bytes, 3U );
    EXPECT_EQ( 0, memcmp( data, expected_frame.data(), expected_frame.size() ) );
}

static void VerifyPreparedFrame( const AnalogueOutputPreparedFrame_T& prepared_frame,
                                 const std::array<uint8_t, 3U>&       expected_frame )
{
    EXPECT_EQ( 0, memcmp( prepared_frame.bytes, expected_frame.data(), expected_frame.size() ) );
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
    MOCK_METHOD( bool, ConfigureChannel, ( SPIChannel_T peripheral, HWSPIConfig_T configuration ),
                 () );

    MOCK_METHOD( bool, StartChannel, ( SPIChannel_T peripheral ), () );

    MOCK_METHOD( bool, LoadTxBuffer,
                 ( SPIChannel_T peripheral, const uint8_t* data, uint32_t size_bytes ), () );

    MOCK_METHOD( void, TxTrigger, ( SPIChannel_T peripheral ), () );
};

static MockHWSPI* g_mock_hw_spi = nullptr;

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

bool HW_SPI_Load_Tx_Buffer( SPIChannel_T peripheral, const uint8_t* data, uint32_t size )
{
    return g_mock_hw_spi->LoadTxBuffer( peripheral, data, size );
}

void HW_SPI_Tx_Trigger( SPIChannel_T peripheral )
{
    g_mock_hw_spi->TxTrigger( peripheral );
}
}

static void ExpectFrameLoad( MockHWSPI& mock_hw_spi, const std::array<uint8_t, 3U>& expected_frame )
{
    using ::testing::_;
    using ::testing::Invoke;

    EXPECT_CALL( mock_hw_spi, LoadTxBuffer( SPI_DAC, _, 3U ) )
        .WillOnce( Invoke(
            [expected_frame]( SPIChannel_T peripheral, const uint8_t* data, uint32_t size_bytes ) {
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

        EXPECT_CALL( mock_hw_spi, LoadTxBuffer( SPI_DAC, _, _ ) ).WillOnce( Return( false ) );

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

        EXPECT_CALL( mock_hw_spi, TxTrigger( SPI_DAC ) ).Times( 1 );
    }

    void ExpectSuccessfulSetup( void )
    {
        using ::testing::_;
        using ::testing::Return;

        EXPECT_CALL( mock_hw_spi, ConfigureChannel( SPI_DAC, _ ) )
            .WillOnce( ::testing::Invoke( VerifySpiChannelSetupConfig ) );

        EXPECT_CALL( mock_hw_spi, StartChannel( SPI_DAC ) ).WillOnce( Return( true ) );
    }

    void ExpectSuccessfulConfig( bool use_external_vref )
    {
        ExpectStartupFrames( use_external_vref );
    }

    void ExpectSingleWriteFrame( const std::array<uint8_t, 3U>& expected_frame )
    {
        using ::testing::_;
        using ::testing::Invoke;

        EXPECT_CALL( mock_hw_spi, LoadTxBuffer( SPI_DAC, _, 3U ) )
            .WillOnce( Invoke( [expected_frame]( SPIChannel_T peripheral, const uint8_t* data,
                                                 uint32_t size_bytes ) {
                EXPECT_EQ( peripheral, SPI_DAC );
                EXPECT_EQ( size_bytes, 3U );
                EXPECT_EQ( 0, memcmp( data, expected_frame.data(), expected_frame.size() ) );
                return true;
            } ) );

        EXPECT_CALL( mock_hw_spi, TxTrigger( SPI_DAC ) ).Times( 1 );
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

    EXPECT_CALL( mock_hw_spi, ConfigureChannel( SPI_DAC, _ ) ).WillOnce( Return( false ) );
    EXPECT_CALL( mock_hw_spi, StartChannel( SPI_DAC ) ).Times( 0 );

    bool result = EXEC_ANALOGUE_OUTPUT_SPI_Channel_Setup();

    EXPECT_FALSE( result );
}

TEST_F( ExecAnalogueOutputTest, SpiChannelSetup_StartFails_ReturnsFalse )
{
    using ::testing::_;
    using ::testing::Return;

    EXPECT_CALL( mock_hw_spi, ConfigureChannel( SPI_DAC, _ ) ).WillOnce( Return( true ) );
    EXPECT_CALL( mock_hw_spi, StartChannel( SPI_DAC ) ).WillOnce( Return( false ) );

    EXPECT_FALSE( EXEC_ANALOGUE_OUTPUT_SPI_Channel_Setup() );
}

TEST_F( ExecAnalogueOutputTest, Config_ExternalVref_LoadsStartupFramesAndMarksConfigured )
{
    ExpectSuccessfulConfig( true );

    bool result = EXEC_ANALOGUE_OUTPUT_Config( true );

    EXPECT_TRUE( result );
    EXPECT_TRUE( EXEC_ANALOG_OUTPUT_Is_Configured() );
}

TEST_F( ExecAnalogueOutputTest, Config_VddReference_LoadsStartupFramesAndMarksConfigured )
{
    ExpectSuccessfulConfig( false );

    bool result = EXEC_ANALOGUE_OUTPUT_Config( false );

    EXPECT_TRUE( result );
    EXPECT_TRUE( EXEC_ANALOG_OUTPUT_Is_Configured() );
}

TEST_F( ExecAnalogueOutputTest, Config_LoadFailure_ReturnsFalseAndLeavesModuleUnconfigured )
{
    using ::testing::_;
    using ::testing::Return;

    EXPECT_CALL( mock_hw_spi, LoadTxBuffer( SPI_DAC, _, _ ) ).WillOnce( Return( false ) );
    EXPECT_CALL( mock_hw_spi, TxTrigger( SPI_DAC ) ).Times( 0 );

    bool result = EXEC_ANALOGUE_OUTPUT_Config( false );

    EXPECT_FALSE( result );
    EXPECT_FALSE( EXEC_ANALOG_OUTPUT_Is_Configured() );
}

TEST_F( ExecAnalogueOutputTest, PreparedFrame_ContainsExactlyThreeWireBytes )
{
    EXPECT_EQ( sizeof( AnalogueOutputPreparedFrame_T ), 3U );
}

TEST_F( ExecAnalogueOutputTest, PrepareFrame_AllSupportedChannelsProduceGoldenCommandBytes )
{
    constexpr std::array<std::array<uint8_t, 3U>, 6U> EXPECTED_FRAMES = { {
        { 0x00U, 0x00U, 0x00U },
        { 0x08U, 0x00U, 0x00U },
        { 0x10U, 0x00U, 0x00U },
        { 0x18U, 0x00U, 0x00U },
        { 0x20U, 0x00U, 0x00U },
        { 0x28U, 0x00U, 0x00U },
    } };

    for ( uint8_t channel = 0U; channel < EXPECTED_FRAMES.size(); channel++ )
    {
        AnalogueOutputPreparedFrame_T prepared_frame = {};

        ASSERT_TRUE( EXEC_ANALOG_OUTPUT_Prepare_Frame( channel, 0.0F, &prepared_frame ) );
        VerifyPreparedFrame( prepared_frame, EXPECTED_FRAMES[channel] );
    }
}

TEST_F( ExecAnalogueOutputTest, PrepareFrame_RejectsDisabledAndInvalidChannels )
{
    constexpr std::array<uint8_t, 4U> INVALID_CHANNELS = { 6U, 7U, 8U, UINT8_MAX };

    for ( uint8_t channel : INVALID_CHANNELS )
    {
        AnalogueOutputPreparedFrame_T prepared_frame = { { 0xAAU, 0x55U, 0xA5U } };

        EXPECT_FALSE( EXEC_ANALOG_OUTPUT_Prepare_Frame( channel, 10.0F, &prepared_frame ) );
        VerifyPreparedFrame( prepared_frame, { 0xAAU, 0x55U, 0xA5U } );
    }
}

TEST_F( ExecAnalogueOutputTest, PrepareFrame_ProducesGoldenZeroMidrangeAndMaximumBytes )
{
    AnalogueOutputPreparedFrame_T prepared_frame = {};

    ASSERT_TRUE( EXEC_ANALOG_OUTPUT_Prepare_Frame( 0U, 0.0F, &prepared_frame ) );
    VerifyPreparedFrame( prepared_frame, { 0x00U, 0x00U, 0x00U } );

    ASSERT_TRUE( EXEC_ANALOG_OUTPUT_Prepare_Frame( 2U, 10.0F, &prepared_frame ) );
    VerifyPreparedFrame( prepared_frame, { 0x10U, 0x08U, 0x00U } );

    ASSERT_TRUE( EXEC_ANALOG_OUTPUT_Prepare_Frame( 5U, 20.0F, &prepared_frame ) );
    VerifyPreparedFrame( prepared_frame, { 0x28U, 0x0FU, 0xFFU } );
}

TEST_F( ExecAnalogueOutputTest, PrepareFrame_PreservesFiniteClamping )
{
    AnalogueOutputPreparedFrame_T prepared_frame = {};

    ASSERT_TRUE( EXEC_ANALOG_OUTPUT_Prepare_Frame( 1U, -3.5F, &prepared_frame ) );
    VerifyPreparedFrame( prepared_frame, { 0x08U, 0x00U, 0x00U } );

    ASSERT_TRUE( EXEC_ANALOG_OUTPUT_Prepare_Frame( 4U, 99.0F, &prepared_frame ) );
    VerifyPreparedFrame( prepared_frame, { 0x20U, 0x0FU, 0xFFU } );
}

TEST_F( ExecAnalogueOutputTest, PrepareFrame_PreservesFractionalScalingAndRounding )
{
    AnalogueOutputPreparedFrame_T prepared_frame = {};

    ASSERT_TRUE( EXEC_ANALOG_OUTPUT_Prepare_Frame( 1U, 12.34F, &prepared_frame ) );
    VerifyPreparedFrame( prepared_frame, { 0x08U, 0x09U, 0xDFU } );

    ASSERT_TRUE( EXEC_ANALOG_OUTPUT_Prepare_Frame( 0U, 0.0024F, &prepared_frame ) );
    VerifyPreparedFrame( prepared_frame, { 0x00U, 0x00U, 0x00U } );

    ASSERT_TRUE( EXEC_ANALOG_OUTPUT_Prepare_Frame( 0U, 0.0025F, &prepared_frame ) );
    VerifyPreparedFrame( prepared_frame, { 0x00U, 0x00U, 0x01U } );

    ASSERT_TRUE( EXEC_ANALOG_OUTPUT_Prepare_Frame( 0U, 15.0F, &prepared_frame ) );
    VerifyPreparedFrame( prepared_frame, { 0x00U, 0x0BU, 0xFFU } );
}

TEST_F( ExecAnalogueOutputTest, PrepareFrame_RejectsNonFiniteVoltagesWithoutChangingDestination )
{
    constexpr std::array<float, 3U> NON_FINITE_VOLTAGES = {
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(),
    };

    for ( float input_voltage_v : NON_FINITE_VOLTAGES )
    {
        AnalogueOutputPreparedFrame_T prepared_frame = { { 0xAAU, 0x55U, 0xA5U } };

        EXPECT_FALSE( EXEC_ANALOG_OUTPUT_Prepare_Frame( 0U, input_voltage_v, &prepared_frame ) );
        VerifyPreparedFrame( prepared_frame, { 0xAAU, 0x55U, 0xA5U } );
    }
}

TEST_F( ExecAnalogueOutputTest, PrepareFrame_RejectsNullDestination )
{
    EXPECT_FALSE( EXEC_ANALOG_OUTPUT_Prepare_Frame( 0U, 10.0F, nullptr ) );
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

    ExpectSingleWriteFrame( { 0x00U, 0x00U, 0x00U } );

    bool result = EXEC_ANALOG_OUTPUT_Write_Voltage( 0U, -3.5F );

    EXPECT_TRUE( result );
}

TEST_F( ExecAnalogueOutputTest, WriteVoltage_ClampsHighVoltageAndWritesFullScaleCode )
{
    ExpectSuccessfulConfig( true );
    ASSERT_TRUE( EXEC_ANALOGUE_OUTPUT_Config( true ) );
    ::testing::Mock::VerifyAndClearExpectations( &mock_hw_spi );

    ExpectSingleWriteFrame( { 0x28U, 0x0FU, 0xFFU } );

    bool result = EXEC_ANALOG_OUTPUT_Write_Voltage( 5U, 99.0F );

    EXPECT_TRUE( result );
}

TEST_F( ExecAnalogueOutputTest, WriteVoltage_MidScaleVoltageRoundsToNearestCount )
{
    ExpectSuccessfulConfig( false );
    ASSERT_TRUE( EXEC_ANALOGUE_OUTPUT_Config( false ) );
    ::testing::Mock::VerifyAndClearExpectations( &mock_hw_spi );

    ExpectSingleWriteFrame( { 0x10U, 0x08U, 0x00U } );

    bool result = EXEC_ANALOG_OUTPUT_Write_Voltage( 2U, 10.0F );

    EXPECT_TRUE( result );
}

TEST_F( ExecAnalogueOutputTest, WriteVoltage_ZeroVoltageWritesZeroCode )
{
    ExpectSuccessfulConfig( false );
    ASSERT_TRUE( EXEC_ANALOGUE_OUTPUT_Config( false ) );
    ::testing::Mock::VerifyAndClearExpectations( &mock_hw_spi );

    ExpectSingleWriteFrame( { 0x18U, 0x00U, 0x00U } );

    EXPECT_TRUE( EXEC_ANALOG_OUTPUT_Write_Voltage( 3U, 0.0F ) );
}

TEST_F( ExecAnalogueOutputTest, WriteVoltage_MaximumVoltageWritesFullScaleCode )
{
    ExpectSuccessfulConfig( false );
    ASSERT_TRUE( EXEC_ANALOGUE_OUTPUT_Config( false ) );
    ::testing::Mock::VerifyAndClearExpectations( &mock_hw_spi );

    ExpectSingleWriteFrame( { 0x20U, 0x0FU, 0xFFU } );

    EXPECT_TRUE( EXEC_ANALOG_OUTPUT_Write_Voltage( 4U, 20.0F ) );
}

TEST_F( ExecAnalogueOutputTest, WriteVoltage_PreservesDataByteOrder )
{
    ExpectSuccessfulConfig( false );
    ASSERT_TRUE( EXEC_ANALOGUE_OUTPUT_Config( false ) );
    ::testing::Mock::VerifyAndClearExpectations( &mock_hw_spi );

    /* 12.34 V maps to count 2527 (0x09DF) in the existing implementation. */
    ExpectSingleWriteFrame( { 0x08U, 0x09U, 0xDFU } );

    EXPECT_TRUE( EXEC_ANALOG_OUTPUT_Write_Voltage( 1U, 12.34F ) );
}

TEST_F( ExecAnalogueOutputTest, WriteVoltage_PreservesRepresentativeRoundingBoundaries )
{
    using ::testing::InSequence;

    ExpectSuccessfulConfig( false );
    ASSERT_TRUE( EXEC_ANALOGUE_OUTPUT_Config( false ) );
    ::testing::Mock::VerifyAndClearExpectations( &mock_hw_spi );

    InSequence sequence;

    /* These values fall immediately below and above the first half-count boundary. */
    ExpectSingleWriteFrame( { 0x00U, 0x00U, 0x00U } );
    EXPECT_TRUE( EXEC_ANALOG_OUTPUT_Write_Voltage( 0U, 0.0024F ) );

    ExpectSingleWriteFrame( { 0x00U, 0x00U, 0x01U } );
    EXPECT_TRUE( EXEC_ANALOG_OUTPUT_Write_Voltage( 0U, 0.0025F ) );

    /* 15 V maps to count 3071 (0x0BFF), preserving round-to-nearest behavior. */
    ExpectSingleWriteFrame( { 0x00U, 0x0BU, 0xFFU } );
    EXPECT_TRUE( EXEC_ANALOG_OUTPUT_Write_Voltage( 0U, 15.0F ) );
}

TEST_F( ExecAnalogueOutputTest, WriteVoltage_PacksCommandByteForEverySupportedChannel )
{
    using ::testing::InSequence;

    constexpr std::array<std::array<uint8_t, 3U>, 6U> EXPECTED_FRAMES = { {
        { 0x00U, 0x00U, 0x00U },
        { 0x08U, 0x00U, 0x00U },
        { 0x10U, 0x00U, 0x00U },
        { 0x18U, 0x00U, 0x00U },
        { 0x20U, 0x00U, 0x00U },
        { 0x28U, 0x00U, 0x00U },
    } };

    ExpectSuccessfulConfig( false );
    ASSERT_TRUE( EXEC_ANALOGUE_OUTPUT_Config( false ) );
    ::testing::Mock::VerifyAndClearExpectations( &mock_hw_spi );

    InSequence sequence;

    for ( uint8_t channel = 0U; channel < EXPECTED_FRAMES.size(); channel++ )
    {
        ExpectSingleWriteFrame( EXPECTED_FRAMES[channel] );
        EXPECT_TRUE( EXEC_ANALOG_OUTPUT_Write_Voltage( channel, 0.0F ) );
    }
}

TEST_F( ExecAnalogueOutputTest, WriteVoltage_InvalidChannel_ReturnsFalseWithoutSPIWrite )
{
    ExpectSuccessfulConfig( false );
    ASSERT_TRUE( EXEC_ANALOGUE_OUTPUT_Config( false ) );
    ::testing::Mock::VerifyAndClearExpectations( &mock_hw_spi );

    EXPECT_CALL( mock_hw_spi, LoadTxBuffer( SPI_DAC, ::testing::_, ::testing::_ ) ).Times( 0 );
    EXPECT_CALL( mock_hw_spi, TxTrigger( SPI_DAC ) ).Times( 0 );

    bool result = EXEC_ANALOG_OUTPUT_Write_Voltage( 6U, 12.0F );

    EXPECT_FALSE( result );
}
