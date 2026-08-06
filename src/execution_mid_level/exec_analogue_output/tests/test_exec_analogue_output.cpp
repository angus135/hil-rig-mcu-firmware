/******************************************************************************
 *  File:       test_exec_analogue_output.cpp
 *  Description:
 *      Unit tests for the analogue-output execution-layer lifecycle.
 ******************************************************************************/

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <array>
#include <cstring>

extern "C"
{
#include "exec_analogue_output.h"
#include "hw_spi.h"
#include "logic_expander.h"
}

using ::testing::_;
using ::testing::Invoke;
using ::testing::Return;
using ::testing::StrictMock;

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
    ANALOGUE_OUTPUT_STARTUP_FRAMES_VDD = { {
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

class MockHWSPI
{
public:
    MOCK_METHOD( bool, ConfigureChannel, ( SPIChannel_T, HWSPIConfig_T ), () );
    MOCK_METHOD( bool, StartChannel, ( SPIChannel_T ), () );
    MOCK_METHOD( bool, StopChannel, ( SPIChannel_T ), () );
    MOCK_METHOD( bool, LoadTxBuffer, ( SPIChannel_T, const uint8_t*, uint32_t ), () );
    MOCK_METHOD( void, TxTrigger, ( SPIChannel_T ), () );
    MOCK_METHOD( bool, TxIsComplete, ( SPIChannel_T ), () );
};

class MockLogicExpander
{
public:
    MOCK_METHOD( LogicExpanderStatus_T, LoadControlBit,
                 ( LogicExpanderIndex_T, LogicExpanderPort_T, uint8_t, bool ), () );
    MOCK_METHOD( LogicExpanderStatus_T, SendControlBits, (), () );
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

bool HW_SPI_Tx_Is_Complete( SPIChannel_T peripheral )
{
    return g_mock_hw_spi->TxIsComplete( peripheral );
}

LogicExpanderStatus_T LOGIC_EXPANDER_Load_Control_Bit( LogicExpanderIndex_T expander_index,
                                                       LogicExpanderPort_T port, uint8_t bit_index,
                                                       bool bit_value )
{
    return g_mock_logic_expander->LoadControlBit( expander_index, port, bit_index, bit_value );
}

LogicExpanderStatus_T LOGIC_EXPANDER_Send_Control_Bits( void )
{
    return g_mock_logic_expander->SendControlBits();
}
}

static bool VerifySpiConfiguration( SPIChannel_T peripheral, HWSPIConfig_T configuration )
{
    EXPECT_EQ( peripheral, SPI_DAC );
    EXPECT_EQ( configuration.spi_mode, SPI_MASTER_MODE );
    EXPECT_EQ( configuration.data_size, SPI_SIZE_8_BIT );
    EXPECT_EQ( configuration.first_bit, SPI_FIRST_MSB );
    EXPECT_EQ( configuration.baud_rate, SPI_BAUD_703KBIT );
    EXPECT_EQ( configuration.cpol, SPI_CPOL_LOW );
    EXPECT_EQ( configuration.cpha, SPI_CPHA_1_EDGE );
    EXPECT_EQ( configuration.nss_pin, GPIO_SPI4_NSS );
    return true;
}

class ExecAnalogueOutputTest : public ::testing::Test
{
protected:
    StrictMock<MockHWSPI>         mock_hw_spi;
    StrictMock<MockLogicExpander> mock_logic_expander;

    void SetUp() override
    {
        g_mock_hw_spi         = &mock_hw_spi;
        g_mock_logic_expander = &mock_logic_expander;
        ResetModuleState();
    }

    void TearDown() override
    {
        g_mock_hw_spi         = nullptr;
        g_mock_logic_expander = nullptr;
    }

    void ExpectEnableBit( bool enable, LogicExpanderStatus_T result = LOGIC_EXPANDER_STATUS_OK )
    {
        EXPECT_CALL( mock_logic_expander,
                     LoadControlBit( LOGIC_EXPANDER_DEVICE_I2C_AO, LOGIC_EXPANDER_PORT_B, 0U,
                                     enable ) )
            .WillOnce( Return( result ) );
    }

    void ResetModuleState()
    {
        if ( EXEC_ANALOGUE_OUTPUT_Is_Started() )
        {
            ExpectEnableBit( false );
            EXPECT_CALL( mock_logic_expander, SendControlBits() )
                .WillOnce( Return( LOGIC_EXPANDER_STATUS_OK ) );
            ExpectSafeOutputFrames();
            ASSERT_TRUE( EXEC_ANALOGUE_OUTPUT_Stop() );
            ::testing::Mock::VerifyAndClearExpectations( &mock_hw_spi );
            ::testing::Mock::VerifyAndClearExpectations( &mock_logic_expander );
        }

        /* A rejected enable-bit load resets Configure() to the unconfigured state. */
        if ( EXEC_ANALOGUE_OUTPUT_Is_Configured() )
        {
            EXPECT_CALL( mock_hw_spi, StopChannel( SPI_DAC ) ).WillOnce( Return( true ) );
        }
        ExpectEnableBit( false, LOGIC_EXPANDER_STATUS_ERROR );
        ASSERT_FALSE( EXEC_ANALOGUE_OUTPUT_Configure( false ) );
        ::testing::Mock::VerifyAndClearExpectations( &mock_hw_spi );
        ::testing::Mock::VerifyAndClearExpectations( &mock_logic_expander );
    }

    void ExpectDisabledOutputCommit()
    {
        ExpectEnableBit( false );
        EXPECT_CALL( mock_logic_expander, SendControlBits() )
            .WillOnce( Return( LOGIC_EXPANDER_STATUS_OK ) );
    }

    void ExpectSpiSetup()
    {
        EXPECT_CALL( mock_hw_spi, ConfigureChannel( SPI_DAC, _ ) )
            .WillOnce( Invoke( VerifySpiConfiguration ) );
        EXPECT_CALL( mock_hw_spi, StartChannel( SPI_DAC ) ).WillOnce( Return( true ) );
    }

    void ExpectStartupFrames( bool use_external_vref )
    {
        const auto& frames = use_external_vref ? ANALOGUE_OUTPUT_STARTUP_FRAMES_EXTERNAL_VREF
                                               : ANALOGUE_OUTPUT_STARTUP_FRAMES_VDD;

        ::testing::InSequence sequence;
        for ( const auto& expected_frame : frames )
        {
            EXPECT_CALL( mock_hw_spi, LoadTxBuffer( SPI_DAC, _, 3U ) )
                .WillOnce( Invoke( [expected_frame]( SPIChannel_T, const uint8_t* data,
                                                     uint32_t size ) {
                    EXPECT_EQ( size, expected_frame.size() );
                    EXPECT_EQ( std::memcmp( data, expected_frame.data(), expected_frame.size() ),
                               0 );
                    return true;
                } ) );
        }
        EXPECT_CALL( mock_hw_spi, TxTrigger( SPI_DAC ) );
    }

    void ExpectSuccessfulConfigure( bool use_external_vref )
    {
        ExpectDisabledOutputCommit();
        ExpectSpiSetup();
        ExpectStartupFrames( use_external_vref );
    }

    void ConfigureSuccessfully( bool use_external_vref = false )
    {
        ExpectSuccessfulConfigure( use_external_vref );
        ASSERT_TRUE( EXEC_ANALOGUE_OUTPUT_Configure( use_external_vref ) );
        ::testing::Mock::VerifyAndClearExpectations( &mock_hw_spi );
        ::testing::Mock::VerifyAndClearExpectations( &mock_logic_expander );
    }

    void StartSuccessfully()
    {
        EXPECT_CALL( mock_hw_spi, TxIsComplete( SPI_DAC ) ).WillOnce( Return( true ) );
        ExpectEnableBit( true );
        EXPECT_CALL( mock_logic_expander, SendControlBits() )
            .WillOnce( Return( LOGIC_EXPANDER_STATUS_OK ) );
        ASSERT_TRUE( EXEC_ANALOGUE_OUTPUT_Start() );
        ::testing::Mock::VerifyAndClearExpectations( &mock_hw_spi );
        ::testing::Mock::VerifyAndClearExpectations( &mock_logic_expander );
    }

    void ExpectVoltageFrame( uint8_t channel, uint16_t count )
    {
        const std::array<uint8_t, 3U> expected = {
            static_cast<uint8_t>( ( channel & 0x1FU ) << 3U ),
            static_cast<uint8_t>( ( count >> 8U ) & 0xFFU ),
            static_cast<uint8_t>( count & 0xFFU ),
        };

        EXPECT_CALL( mock_hw_spi, LoadTxBuffer( SPI_DAC, _, 3U ) )
            .WillOnce( Invoke( [expected]( SPIChannel_T, const uint8_t* data, uint32_t size ) {
                EXPECT_EQ( size, expected.size() );
                EXPECT_EQ( std::memcmp( data, expected.data(), expected.size() ), 0 );
                return true;
            } ) );
        EXPECT_CALL( mock_hw_spi, TxTrigger( SPI_DAC ) );
    }

    void ExpectSafeOutputFrames()
    {
        ::testing::InSequence sequence;
        for ( uint8_t channel = 0U; channel < 6U; channel++ )
        {
            const std::array<uint8_t, 3U> expected = {
                static_cast<uint8_t>( ( channel & 0x1FU ) << 3U ), 0U, 0U
            };

            EXPECT_CALL( mock_hw_spi, LoadTxBuffer( SPI_DAC, _, 3U ) )
                .WillOnce( Invoke( [expected]( SPIChannel_T, const uint8_t* data, uint32_t size ) {
                    EXPECT_EQ( size, expected.size() );
                    EXPECT_EQ( std::memcmp( data, expected.data(), expected.size() ), 0 );
                    return true;
                } ) );
        }
        EXPECT_CALL( mock_hw_spi, TxTrigger( SPI_DAC ) );
    }
};

TEST_F( ExecAnalogueOutputTest, ConfigureDisablesOutputAndProgramsExternalReference )
{
    ExpectSuccessfulConfigure( true );
    EXPECT_TRUE( EXEC_ANALOGUE_OUTPUT_Configure( true ) );
    EXPECT_TRUE( EXEC_ANALOGUE_OUTPUT_Is_Configured() );
    EXPECT_FALSE( EXEC_ANALOGUE_OUTPUT_Is_Started() );
}

TEST_F( ExecAnalogueOutputTest, ConfigureReturnsFalseWhenEnableBitCannotBeLoaded )
{
    ExpectEnableBit( false, LOGIC_EXPANDER_STATUS_ERROR );
    EXPECT_FALSE( EXEC_ANALOGUE_OUTPUT_Configure( false ) );
    EXPECT_FALSE( EXEC_ANALOGUE_OUTPUT_Is_Configured() );
}

TEST_F( ExecAnalogueOutputTest, ConfigureReturnsFalseWhenExpanderCommitFails )
{
    ExpectEnableBit( false );
    EXPECT_CALL( mock_logic_expander, SendControlBits() )
        .WillOnce( Return( LOGIC_EXPANDER_STATUS_BUSY ) );
    EXPECT_FALSE( EXEC_ANALOGUE_OUTPUT_Configure( false ) );
    EXPECT_FALSE( EXEC_ANALOGUE_OUTPUT_Is_Configured() );
}

TEST_F( ExecAnalogueOutputTest, ConfigureReturnsFalseWhenSpiConfigurationFails )
{
    ExpectDisabledOutputCommit();
    EXPECT_CALL( mock_hw_spi, ConfigureChannel( SPI_DAC, _ ) ).WillOnce( Return( false ) );
    EXPECT_FALSE( EXEC_ANALOGUE_OUTPUT_Configure( false ) );
    EXPECT_FALSE( EXEC_ANALOGUE_OUTPUT_Is_Configured() );
}

TEST_F( ExecAnalogueOutputTest, ConfigureReturnsFalseWhenSpiStartFails )
{
    ExpectDisabledOutputCommit();
    EXPECT_CALL( mock_hw_spi, ConfigureChannel( SPI_DAC, _ ) )
        .WillOnce( Invoke( VerifySpiConfiguration ) );
    EXPECT_CALL( mock_hw_spi, StartChannel( SPI_DAC ) ).WillOnce( Return( false ) );
    EXPECT_FALSE( EXEC_ANALOGUE_OUTPUT_Configure( false ) );
    EXPECT_FALSE( EXEC_ANALOGUE_OUTPUT_Is_Configured() );
}

TEST_F( ExecAnalogueOutputTest, ConfigureReturnsFalseWhenStartupFrameCannotBeQueued )
{
    ExpectDisabledOutputCommit();
    ExpectSpiSetup();
    EXPECT_CALL( mock_hw_spi, LoadTxBuffer( SPI_DAC, _, 3U ) ).WillOnce( Return( false ) );
    EXPECT_CALL( mock_hw_spi, StopChannel( SPI_DAC ) ).WillOnce( Return( true ) );
    EXPECT_FALSE( EXEC_ANALOGUE_OUTPUT_Configure( false ) );
    EXPECT_FALSE( EXEC_ANALOGUE_OUTPUT_Is_Configured() );
}

TEST_F( ExecAnalogueOutputTest, ConfigureAgainWhileStoppedRestartsSpiConfiguration )
{
    ConfigureSuccessfully();
    EXPECT_CALL( mock_hw_spi, StopChannel( SPI_DAC ) ).WillOnce( Return( true ) );
    ExpectSuccessfulConfigure( true );

    EXPECT_TRUE( EXEC_ANALOGUE_OUTPUT_Configure( true ) );
    EXPECT_TRUE( EXEC_ANALOGUE_OUTPUT_Is_Configured() );
    EXPECT_FALSE( EXEC_ANALOGUE_OUTPUT_Is_Started() );
}

TEST_F( ExecAnalogueOutputTest, ConfigureAgainLeavesExistingConfigurationWhenSpiStopFails )
{
    ConfigureSuccessfully();
    EXPECT_CALL( mock_hw_spi, StopChannel( SPI_DAC ) ).WillOnce( Return( false ) );

    EXPECT_FALSE( EXEC_ANALOGUE_OUTPUT_Configure( true ) );
    EXPECT_TRUE( EXEC_ANALOGUE_OUTPUT_Is_Configured() );
    EXPECT_FALSE( EXEC_ANALOGUE_OUTPUT_Is_Started() );
}

TEST_F( ExecAnalogueOutputTest, StartRequiresConfiguration )
{
    EXPECT_FALSE( EXEC_ANALOGUE_OUTPUT_Start() );
}

TEST_F( ExecAnalogueOutputTest, StartWaitsForDacConfigurationTransfer )
{
    ConfigureSuccessfully();
    EXPECT_CALL( mock_hw_spi, TxIsComplete( SPI_DAC ) ).WillOnce( Return( false ) );
    EXPECT_FALSE( EXEC_ANALOGUE_OUTPUT_Start() );
    EXPECT_FALSE( EXEC_ANALOGUE_OUTPUT_Is_Started() );
}

TEST_F( ExecAnalogueOutputTest, StartEnablesOutputAfterDacConfigurationCompletes )
{
    ConfigureSuccessfully();
    StartSuccessfully();
    EXPECT_TRUE( EXEC_ANALOGUE_OUTPUT_Is_Started() );
}

TEST_F( ExecAnalogueOutputTest, StartFailureLeavesOutputStopped )
{
    ConfigureSuccessfully();
    ::testing::InSequence sequence;
    EXPECT_CALL( mock_hw_spi, TxIsComplete( SPI_DAC ) ).WillOnce( Return( true ) );
    ExpectEnableBit( true );
    EXPECT_CALL( mock_logic_expander, SendControlBits() )
        .WillOnce( Return( LOGIC_EXPANDER_STATUS_BUSY ) );
    ExpectEnableBit( false );

    EXPECT_FALSE( EXEC_ANALOGUE_OUTPUT_Start() );
    EXPECT_FALSE( EXEC_ANALOGUE_OUTPUT_Is_Started() );
}

TEST_F( ExecAnalogueOutputTest, ConfigureIsRejectedWhileOutputIsStarted )
{
    ConfigureSuccessfully();
    StartSuccessfully();

    EXPECT_FALSE( EXEC_ANALOGUE_OUTPUT_Configure( true ) );
    EXPECT_TRUE( EXEC_ANALOGUE_OUTPUT_Is_Configured() );
    EXPECT_TRUE( EXEC_ANALOGUE_OUTPUT_Is_Started() );
}

TEST_F( ExecAnalogueOutputTest, StopDisablesOutputAndRetainsConfiguration )
{
    ConfigureSuccessfully();
    StartSuccessfully();
    ExpectEnableBit( false );
    EXPECT_CALL( mock_logic_expander, SendControlBits() )
        .WillOnce( Return( LOGIC_EXPANDER_STATUS_OK ) );
    ExpectSafeOutputFrames();

    EXPECT_TRUE( EXEC_ANALOGUE_OUTPUT_Stop() );
    EXPECT_TRUE( EXEC_ANALOGUE_OUTPUT_Is_Configured() );
    EXPECT_FALSE( EXEC_ANALOGUE_OUTPUT_Is_Started() );
}

TEST_F( ExecAnalogueOutputTest, StopFailureLeavesStartedStateSetForRetry )
{
    ConfigureSuccessfully();
    StartSuccessfully();
    ExpectEnableBit( false );
    EXPECT_CALL( mock_logic_expander, SendControlBits() )
        .WillOnce( Return( LOGIC_EXPANDER_STATUS_BUSY ) );

    EXPECT_FALSE( EXEC_ANALOGUE_OUTPUT_Stop() );
    EXPECT_TRUE( EXEC_ANALOGUE_OUTPUT_Is_Started() );
}

TEST_F( ExecAnalogueOutputTest, StopSafeFrameFailureRequiresReconfiguration )
{
    ConfigureSuccessfully();
    StartSuccessfully();
    ExpectEnableBit( false );
    EXPECT_CALL( mock_logic_expander, SendControlBits() )
        .WillOnce( Return( LOGIC_EXPANDER_STATUS_OK ) );
    EXPECT_CALL( mock_hw_spi, LoadTxBuffer( SPI_DAC, _, 3U ) ).WillOnce( Return( false ) );
    EXPECT_CALL( mock_hw_spi, StopChannel( SPI_DAC ) ).WillOnce( Return( true ) );

    EXPECT_FALSE( EXEC_ANALOGUE_OUTPUT_Stop() );
    EXPECT_FALSE( EXEC_ANALOGUE_OUTPUT_Is_Started() );
    EXPECT_FALSE( EXEC_ANALOGUE_OUTPUT_Is_Configured() );
}

TEST_F( ExecAnalogueOutputTest, RestartWaitsForSafeOutputFramesToComplete )
{
    ConfigureSuccessfully();
    StartSuccessfully();
    ExpectEnableBit( false );
    EXPECT_CALL( mock_logic_expander, SendControlBits() )
        .WillOnce( Return( LOGIC_EXPANDER_STATUS_OK ) );
    ExpectSafeOutputFrames();
    ASSERT_TRUE( EXEC_ANALOGUE_OUTPUT_Stop() );
    ::testing::Mock::VerifyAndClearExpectations( &mock_hw_spi );
    ::testing::Mock::VerifyAndClearExpectations( &mock_logic_expander );

    EXPECT_CALL( mock_hw_spi, TxIsComplete( SPI_DAC ) ).WillOnce( Return( false ) );
    EXPECT_FALSE( EXEC_ANALOGUE_OUTPUT_Start() );
    EXPECT_FALSE( EXEC_ANALOGUE_OUTPUT_Is_Started() );
}

TEST_F( ExecAnalogueOutputTest, StopRequiresStartedState )
{
    ConfigureSuccessfully();
    EXPECT_FALSE( EXEC_ANALOGUE_OUTPUT_Stop() );
}

TEST_F( ExecAnalogueOutputTest, WriteVoltageRequiresStartedState )
{
    ConfigureSuccessfully();
    EXPECT_FALSE( EXEC_ANALOGUE_OUTPUT_Write_Voltage( 0U, 10.0F ) );
}

TEST_F( ExecAnalogueOutputTest, WriteVoltageClampsLowValueToZero )
{
    ConfigureSuccessfully();
    StartSuccessfully();
    ExpectVoltageFrame( 0U, 0U );
    EXPECT_TRUE( EXEC_ANALOGUE_OUTPUT_Write_Voltage( 0U, -3.5F ) );
}

TEST_F( ExecAnalogueOutputTest, WriteVoltageClampsHighValueToFullScale )
{
    ConfigureSuccessfully();
    StartSuccessfully();
    ExpectVoltageFrame( 5U, 4095U );
    EXPECT_TRUE( EXEC_ANALOGUE_OUTPUT_Write_Voltage( 5U, 99.0F ) );
}

TEST_F( ExecAnalogueOutputTest, WriteVoltageRoundsMidScaleToNearestCount )
{
    ConfigureSuccessfully();
    StartSuccessfully();
    ExpectVoltageFrame( 2U, 2048U );
    EXPECT_TRUE( EXEC_ANALOGUE_OUTPUT_Write_Voltage( 2U, 10.0F ) );
}

TEST_F( ExecAnalogueOutputTest, WriteVoltageRejectsUnusedDacChannel )
{
    ConfigureSuccessfully();
    StartSuccessfully();
    EXPECT_FALSE( EXEC_ANALOGUE_OUTPUT_Write_Voltage( 6U, 12.0F ) );
}
