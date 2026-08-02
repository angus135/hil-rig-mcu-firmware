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
#include <type_traits>

extern "C"
{
#include "exec_analogue_output.h" /* Module under test */
#include "hw_spi.h"
#include <stdint.h>
#include <stdbool.h>
}

static constexpr std::array<uint8_t, 33U> ANALOGUE_OUTPUT_STARTUP_PACKET_EXTERNAL_VREF = {
    0x40U, 0xFFU, 0xFFU, 0x50U, 0x00U, 0x00U, 0x48U, 0xF0U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x08U, 0x00U, 0x00U, 0x10U, 0x00U, 0x00U, 0x18U, 0x00U, 0x00U, 0x20U,
    0x00U, 0x00U, 0x28U, 0x00U, 0x00U, 0x30U, 0x00U, 0x00U, 0x38U, 0x00U, 0x00U,
};

static constexpr std::array<uint8_t, 33U> ANALOGUE_OUTPUT_STARTUP_PACKET_VDD_REFERENCE = {
    0x40U, 0x00U, 0x00U, 0x50U, 0x00U, 0x00U, 0x48U, 0xF0U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x08U, 0x00U, 0x00U, 0x10U, 0x00U, 0x00U, 0x18U, 0x00U, 0x00U, 0x20U,
    0x00U, 0x00U, 0x28U, 0x00U, 0x00U, 0x30U, 0x00U, 0x00U, 0x38U, 0x00U, 0x00U,
};

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

static void VerifyPreparedFrame( const AnalogueOutputPreparedFrame_T& prepared_frame,
                                 const std::array<uint8_t, 3U>&       expected_frame )
{
    EXPECT_EQ( 0, memcmp( prepared_frame.bytes, expected_frame.data(), expected_frame.size() ) );
}

template <size_t SIZE_BYTES>
static void ExpectPayloadLoad( MockHWSPI&                             mock_hw_spi,
                               const std::array<uint8_t, SIZE_BYTES>& expected_payload,
                               bool                                   accepted = true );

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

    MOCK_METHOD( bool, TxIsComplete, ( SPIChannel_T peripheral ), () );
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

bool HW_SPI_Tx_Is_Complete( SPIChannel_T peripheral )
{
    return g_mock_hw_spi->TxIsComplete( peripheral );
}
}

template <size_t SIZE_BYTES>
static void ExpectPayloadLoad( MockHWSPI&                             mock_hw_spi,
                               const std::array<uint8_t, SIZE_BYTES>& expected_payload,
                               bool                                   accepted )
{
    using ::testing::_;
    using ::testing::Invoke;

    EXPECT_CALL( mock_hw_spi, LoadTxBuffer( SPI_DAC, _, ( uint32_t )SIZE_BYTES ) )
        .WillOnce( Invoke( [expected_payload, accepted](
                               SPIChannel_T peripheral, const uint8_t* data, uint32_t size_bytes ) {
            EXPECT_EQ( peripheral, SPI_DAC );
            EXPECT_EQ( size_bytes, ( uint32_t )SIZE_BYTES );
            EXPECT_EQ( 0, memcmp( data, expected_payload.data(), expected_payload.size() ) );
            return accepted;
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

    void ExpectStartupPacket( bool use_external_vref, bool initial_tx_complete = true )
    {
        using ::testing::InSequence;
        using ::testing::Return;

        const auto& expected_packet = use_external_vref
                                          ? ANALOGUE_OUTPUT_STARTUP_PACKET_EXTERNAL_VREF
                                          : ANALOGUE_OUTPUT_STARTUP_PACKET_VDD_REFERENCE;

        InSequence sequence;

        ExpectPayloadLoad( mock_hw_spi, expected_packet );
        EXPECT_CALL( mock_hw_spi, TxTrigger( SPI_DAC ) ).Times( 1 );
        EXPECT_CALL( mock_hw_spi, TxIsComplete( SPI_DAC ) )
            .WillOnce( Return( initial_tx_complete ) );
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
        ExpectStartupPacket( use_external_vref );
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
    EXPECT_EQ( EXEC_ANALOG_OUTPUT_Get_State(), EXEC_ANALOG_OUTPUT_STATE_UNCONFIGURED );
}

TEST_F( ExecAnalogueOutputTest, SpiChannelSetup_ConfigureFails_DoesNotStartChannel )
{
    using ::testing::_;
    using ::testing::Return;

    EXPECT_CALL( mock_hw_spi, ConfigureChannel( SPI_DAC, _ ) ).WillOnce( Return( false ) );
    EXPECT_CALL( mock_hw_spi, StartChannel( SPI_DAC ) ).Times( 0 );

    bool result = EXEC_ANALOGUE_OUTPUT_SPI_Channel_Setup();

    EXPECT_FALSE( result );
    EXPECT_EQ( EXEC_ANALOG_OUTPUT_Get_State(), EXEC_ANALOG_OUTPUT_STATE_FAULTED );
}

TEST_F( ExecAnalogueOutputTest, SpiChannelSetup_StartFails_ReturnsFalse )
{
    using ::testing::_;
    using ::testing::Return;

    EXPECT_CALL( mock_hw_spi, ConfigureChannel( SPI_DAC, _ ) ).WillOnce( Return( true ) );
    EXPECT_CALL( mock_hw_spi, StartChannel( SPI_DAC ) ).WillOnce( Return( false ) );

    EXPECT_FALSE( EXEC_ANALOGUE_OUTPUT_SPI_Channel_Setup() );
    EXPECT_EQ( EXEC_ANALOG_OUTPUT_Get_State(), EXEC_ANALOG_OUTPUT_STATE_FAULTED );
}

TEST_F( ExecAnalogueOutputTest, Config_ExternalVrefLoadsOneAtomicStartupPacketAndBecomesReady )
{
    ExpectSuccessfulConfig( true );

    bool result = EXEC_ANALOGUE_OUTPUT_Config( true );

    EXPECT_TRUE( result );
    EXPECT_TRUE( EXEC_ANALOG_OUTPUT_Is_Configured() );
}

TEST_F( ExecAnalogueOutputTest, Config_VddReferenceLoadsOneAtomicStartupPacketAndBecomesReady )
{
    ExpectSuccessfulConfig( false );

    bool result = EXEC_ANALOGUE_OUTPUT_Config( false );

    EXPECT_TRUE( result );
    EXPECT_TRUE( EXEC_ANALOG_OUTPUT_Is_Configured() );
}

TEST_F( ExecAnalogueOutputTest, Config_LoadFailureReturnsFalseAndLeavesModuleFaulted )
{
    ExpectPayloadLoad( mock_hw_spi, ANALOGUE_OUTPUT_STARTUP_PACKET_VDD_REFERENCE, false );
    EXPECT_CALL( mock_hw_spi, TxTrigger( SPI_DAC ) ).Times( 0 );

    bool result = EXEC_ANALOGUE_OUTPUT_Config( false );

    EXPECT_FALSE( result );
    EXPECT_FALSE( EXEC_ANALOG_OUTPUT_Is_Configured() );
    EXPECT_EQ( EXEC_ANALOG_OUTPUT_Get_State(), EXEC_ANALOG_OUTPUT_STATE_FAULTED );
}

TEST_F( ExecAnalogueOutputTest, Config_QueuedStartupTransitionsFromInitializingToReady )
{
    using ::testing::Return;

    ExpectStartupPacket( false, false );

    ASSERT_TRUE( EXEC_ANALOGUE_OUTPUT_Config( false ) );

    EXPECT_CALL( mock_hw_spi, TxIsComplete( SPI_DAC ) ).WillOnce( Return( false ) );
    EXPECT_EQ( EXEC_ANALOG_OUTPUT_Get_State(), EXEC_ANALOG_OUTPUT_STATE_INITIALIZING );

    EXPECT_CALL( mock_hw_spi, TxIsComplete( SPI_DAC ) ).WillOnce( Return( true ) );
    EXPECT_TRUE( EXEC_ANALOG_OUTPUT_Is_Configured() );
    EXPECT_EQ( EXEC_ANALOG_OUTPUT_Get_State(), EXEC_ANALOG_OUTPUT_STATE_READY );
}

TEST_F( ExecAnalogueOutputTest, Config_ReconfigurationFailureAndRecoveryAreDeterministic )
{
    using ::testing::_;
    using ::testing::Return;

    ExpectSuccessfulConfig( false );
    ASSERT_TRUE( EXEC_ANALOGUE_OUTPUT_Config( false ) );
    EXPECT_EQ( EXEC_ANALOG_OUTPUT_Get_State(), EXEC_ANALOG_OUTPUT_STATE_READY );
    ::testing::Mock::VerifyAndClearExpectations( &mock_hw_spi );

    EXPECT_CALL( mock_hw_spi, LoadTxBuffer( SPI_DAC, _, 33U ) ).WillOnce( Return( false ) );
    EXPECT_FALSE( EXEC_ANALOGUE_OUTPUT_Config( true ) );
    EXPECT_EQ( EXEC_ANALOG_OUTPUT_Get_State(), EXEC_ANALOG_OUTPUT_STATE_FAULTED );
    ::testing::Mock::VerifyAndClearExpectations( &mock_hw_spi );

    ExpectSuccessfulConfig( true );
    EXPECT_TRUE( EXEC_ANALOGUE_OUTPUT_Config( true ) );
    EXPECT_EQ( EXEC_ANALOG_OUTPUT_Get_State(), EXEC_ANALOG_OUTPUT_STATE_READY );
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

TEST_F( ExecAnalogueOutputTest, PreparedBatch_UsesFixedInlineEighteenBytePayload )
{
    EXPECT_EQ( EXEC_ANALOG_OUTPUT_BATCH_MAX_BYTES, 18U );
    EXPECT_EQ( sizeof( AnalogueOutputPreparedBatch_T::bytes ), 18U );
    EXPECT_EQ( sizeof( AnalogueOutputPreparedBatch_T ), 19U );
    EXPECT_TRUE( std::is_trivially_copyable<AnalogueOutputPreparedBatch_T>::value );
    EXPECT_TRUE( std::is_standard_layout<AnalogueOutputPreparedBatch_T>::value );
}

TEST_F( ExecAnalogueOutputTest, BatchInit_CreatesEmptyClearedBatch )
{
    AnalogueOutputPreparedBatch_T prepared_batch;
    memset( &prepared_batch, 0xA5, sizeof( prepared_batch ) );

    ASSERT_TRUE( EXEC_ANALOG_OUTPUT_Batch_Init( &prepared_batch ) );

    EXPECT_EQ( prepared_batch.byte_count, 0U );
    for ( uint8_t byte : prepared_batch.bytes )
    {
        EXPECT_EQ( byte, 0U );
    }
}

TEST_F( ExecAnalogueOutputTest, BatchInit_RejectsNullDestination )
{
    EXPECT_FALSE( EXEC_ANALOG_OUTPUT_Batch_Init( nullptr ) );
}

TEST_F( ExecAnalogueOutputTest, BatchAppend_OneFrameProducesThreeContiguousBytes )
{
    const AnalogueOutputPreparedFrame_T frame = { { 0x18U, 0x09U, 0xDFU } };
    AnalogueOutputPreparedBatch_T       prepared_batch;
    ASSERT_TRUE( EXEC_ANALOG_OUTPUT_Batch_Init( &prepared_batch ) );

    ASSERT_TRUE( EXEC_ANALOG_OUTPUT_Batch_Append( &prepared_batch, &frame ) );

    EXPECT_EQ( prepared_batch.byte_count, 3U );
    EXPECT_EQ( 0, memcmp( prepared_batch.bytes, frame.bytes, sizeof( frame ) ) );
}

TEST_F( ExecAnalogueOutputTest, BatchAppend_MultipleFramesPreserveExactOrderAndCount )
{
    constexpr std::array<AnalogueOutputPreparedFrame_T, 3U> FRAMES         = { {
        { { 0x10U, 0x01U, 0x23U } },
        { { 0x00U, 0x04U, 0x56U } },
        { { 0x28U, 0x07U, 0x89U } },
    } };
    constexpr std::array<uint8_t, 9U>                       EXPECTED_BYTES = {
        0x10U, 0x01U, 0x23U, 0x00U, 0x04U, 0x56U, 0x28U, 0x07U, 0x89U,
    };
    AnalogueOutputPreparedBatch_T prepared_batch;
    ASSERT_TRUE( EXEC_ANALOG_OUTPUT_Batch_Init( &prepared_batch ) );

    for ( uint8_t index = 0U; index < FRAMES.size(); index++ )
    {
        ASSERT_TRUE( EXEC_ANALOG_OUTPUT_Batch_Append( &prepared_batch, &FRAMES[index] ) );
        EXPECT_EQ( prepared_batch.byte_count,
                   ( uint8_t )( ( index + 1U ) * EXEC_ANALOG_OUTPUT_FRAME_SIZE_BYTES ) );
    }

    EXPECT_EQ( 0, memcmp( prepared_batch.bytes, EXPECTED_BYTES.data(), EXPECTED_BYTES.size() ) );
}

TEST_F( ExecAnalogueOutputTest, BatchAppend_SixFramesAcceptedAndSeventhLeavesBatchUnchanged )
{
    constexpr std::array<AnalogueOutputPreparedFrame_T, 7U> FRAMES         = { {
        { { 0x00U, 0x00U, 0x01U } },
        { { 0x08U, 0x00U, 0x02U } },
        { { 0x10U, 0x00U, 0x03U } },
        { { 0x18U, 0x00U, 0x04U } },
        { { 0x20U, 0x00U, 0x05U } },
        { { 0x28U, 0x00U, 0x06U } },
        { { 0x00U, 0x00U, 0x07U } },
    } };
    constexpr std::array<uint8_t, 18U>                      EXPECTED_BYTES = {
        0x00U, 0x00U, 0x01U, 0x08U, 0x00U, 0x02U, 0x10U, 0x00U, 0x03U,
        0x18U, 0x00U, 0x04U, 0x20U, 0x00U, 0x05U, 0x28U, 0x00U, 0x06U,
    };
    AnalogueOutputPreparedBatch_T prepared_batch;
    ASSERT_TRUE( EXEC_ANALOG_OUTPUT_Batch_Init( &prepared_batch ) );

    for ( uint8_t index = 0U; index < EXEC_ANALOG_OUTPUT_BATCH_MAX_FRAMES; index++ )
    {
        ASSERT_TRUE( EXEC_ANALOG_OUTPUT_Batch_Append( &prepared_batch, &FRAMES[index] ) );
        EXPECT_EQ( prepared_batch.byte_count,
                   ( uint8_t )( ( index + 1U ) * EXEC_ANALOG_OUTPUT_FRAME_SIZE_BYTES ) );
    }

    EXPECT_EQ( prepared_batch.byte_count, 18U );
    EXPECT_EQ( 0, memcmp( prepared_batch.bytes, EXPECTED_BYTES.data(), EXPECTED_BYTES.size() ) );

    const AnalogueOutputPreparedBatch_T batch_before_rejection = prepared_batch;
    EXPECT_FALSE( EXEC_ANALOG_OUTPUT_Batch_Append( &prepared_batch, &FRAMES[6] ) );
    EXPECT_EQ( 0, memcmp( &prepared_batch, &batch_before_rejection, sizeof( prepared_batch ) ) );
}

TEST_F( ExecAnalogueOutputTest, BatchAppend_RejectsNullAndMalformedInputsWithoutChangingBatch )
{
    const AnalogueOutputPreparedFrame_T frame = { { 0x00U, 0x00U, 0x00U } };
    AnalogueOutputPreparedBatch_T       prepared_batch;
    ASSERT_TRUE( EXEC_ANALOG_OUTPUT_Batch_Init( &prepared_batch ) );

    const AnalogueOutputPreparedBatch_T empty_batch = prepared_batch;
    EXPECT_FALSE( EXEC_ANALOG_OUTPUT_Batch_Append( nullptr, &frame ) );
    EXPECT_FALSE( EXEC_ANALOG_OUTPUT_Batch_Append( &prepared_batch, nullptr ) );
    EXPECT_EQ( 0, memcmp( &prepared_batch, &empty_batch, sizeof( prepared_batch ) ) );

    prepared_batch.byte_count                           = 1U;
    const AnalogueOutputPreparedBatch_T malformed_batch = prepared_batch;
    EXPECT_FALSE( EXEC_ANALOG_OUTPUT_Batch_Append( &prepared_batch, &frame ) );
    EXPECT_EQ( 0, memcmp( &prepared_batch, &malformed_batch, sizeof( prepared_batch ) ) );
}

TEST_F( ExecAnalogueOutputTest, SubmitPreparedBatch_NotConfiguredRejectsWithoutSPITraffic )
{
    AnalogueOutputPreparedBatch_T prepared_batch;
    ASSERT_TRUE( EXEC_ANALOG_OUTPUT_Batch_Init( &prepared_batch ) );

    EXPECT_FALSE( EXEC_ANALOG_OUTPUT_Submit_Prepared_Batch( &prepared_batch ) );
}

TEST_F( ExecAnalogueOutputTest, SubmitPreparedBatch_InitializingRejectsWithoutLoadOrTrigger )
{
    using ::testing::Return;

    const AnalogueOutputPreparedFrame_T frame = { { 0x00U, 0x00U, 0x00U } };
    AnalogueOutputPreparedBatch_T       prepared_batch;
    ASSERT_TRUE( EXEC_ANALOG_OUTPUT_Batch_Init( &prepared_batch ) );
    ASSERT_TRUE( EXEC_ANALOG_OUTPUT_Batch_Append( &prepared_batch, &frame ) );

    ExpectStartupPacket( false, false );
    ASSERT_TRUE( EXEC_ANALOGUE_OUTPUT_Config( false ) );
    ::testing::Mock::VerifyAndClearExpectations( &mock_hw_spi );

    EXPECT_CALL( mock_hw_spi, TxIsComplete( SPI_DAC ) )
        .Times( 2 )
        .WillRepeatedly( Return( false ) );

    EXPECT_FALSE( EXEC_ANALOG_OUTPUT_Submit_Prepared_Batch( &prepared_batch ) );
    EXPECT_EQ( EXEC_ANALOG_OUTPUT_Get_State(), EXEC_ANALOG_OUTPUT_STATE_INITIALIZING );
}

TEST_F( ExecAnalogueOutputTest, SubmitPreparedBatch_EmptyBatchIsSuccessfulNoOp )
{
    ExpectSuccessfulConfig( false );
    ASSERT_TRUE( EXEC_ANALOGUE_OUTPUT_Config( false ) );
    ::testing::Mock::VerifyAndClearExpectations( &mock_hw_spi );

    AnalogueOutputPreparedBatch_T prepared_batch;
    ASSERT_TRUE( EXEC_ANALOG_OUTPUT_Batch_Init( &prepared_batch ) );

    EXPECT_TRUE( EXEC_ANALOG_OUTPUT_Submit_Prepared_Batch( &prepared_batch ) );
}

TEST_F( ExecAnalogueOutputTest, SubmitPreparedBatch_OneFrameLoadsOnceThenTriggersOnce )
{
    using ::testing::InSequence;

    constexpr std::array<uint8_t, 3U>   EXPECTED_BYTES = { 0x18U, 0x09U, 0xDFU };
    const AnalogueOutputPreparedFrame_T frame          = { { 0x18U, 0x09U, 0xDFU } };
    AnalogueOutputPreparedBatch_T       prepared_batch;
    ASSERT_TRUE( EXEC_ANALOG_OUTPUT_Batch_Init( &prepared_batch ) );
    ASSERT_TRUE( EXEC_ANALOG_OUTPUT_Batch_Append( &prepared_batch, &frame ) );

    ExpectSuccessfulConfig( false );
    ASSERT_TRUE( EXEC_ANALOGUE_OUTPUT_Config( false ) );
    ::testing::Mock::VerifyAndClearExpectations( &mock_hw_spi );

    InSequence sequence;
    ExpectPayloadLoad( mock_hw_spi, EXPECTED_BYTES );
    EXPECT_CALL( mock_hw_spi, TxTrigger( SPI_DAC ) ).Times( 1 );

    EXPECT_TRUE( EXEC_ANALOG_OUTPUT_Submit_Prepared_Batch( &prepared_batch ) );
}

TEST_F( ExecAnalogueOutputTest, SubmitPreparedBatch_SixFramesLoadOneExactEighteenBytePayload )
{
    using ::testing::InSequence;

    constexpr std::array<AnalogueOutputPreparedFrame_T, 6U> FRAMES         = { {
        { { 0x00U, 0x00U, 0x01U } },
        { { 0x08U, 0x00U, 0x02U } },
        { { 0x10U, 0x00U, 0x03U } },
        { { 0x18U, 0x00U, 0x04U } },
        { { 0x20U, 0x00U, 0x05U } },
        { { 0x28U, 0x00U, 0x06U } },
    } };
    constexpr std::array<uint8_t, 18U>                      EXPECTED_BYTES = {
        0x00U, 0x00U, 0x01U, 0x08U, 0x00U, 0x02U, 0x10U, 0x00U, 0x03U,
        0x18U, 0x00U, 0x04U, 0x20U, 0x00U, 0x05U, 0x28U, 0x00U, 0x06U,
    };
    AnalogueOutputPreparedBatch_T prepared_batch;
    ASSERT_TRUE( EXEC_ANALOG_OUTPUT_Batch_Init( &prepared_batch ) );
    for ( const AnalogueOutputPreparedFrame_T& frame : FRAMES )
    {
        ASSERT_TRUE( EXEC_ANALOG_OUTPUT_Batch_Append( &prepared_batch, &frame ) );
    }

    ExpectSuccessfulConfig( false );
    ASSERT_TRUE( EXEC_ANALOGUE_OUTPUT_Config( false ) );
    ::testing::Mock::VerifyAndClearExpectations( &mock_hw_spi );

    InSequence sequence;
    ExpectPayloadLoad( mock_hw_spi, EXPECTED_BYTES );
    EXPECT_CALL( mock_hw_spi, TxTrigger( SPI_DAC ) ).Times( 1 );

    EXPECT_TRUE( EXEC_ANALOG_OUTPUT_Submit_Prepared_Batch( &prepared_batch ) );
}

TEST_F( ExecAnalogueOutputTest, SubmitPreparedBatch_LoadRejectionReturnsFalseWithoutTrigger )
{
    constexpr std::array<uint8_t, 3U>   EXPECTED_BYTES = { 0x18U, 0x09U, 0xDFU };
    const AnalogueOutputPreparedFrame_T frame          = { { 0x18U, 0x09U, 0xDFU } };
    AnalogueOutputPreparedBatch_T       prepared_batch;
    ASSERT_TRUE( EXEC_ANALOG_OUTPUT_Batch_Init( &prepared_batch ) );
    ASSERT_TRUE( EXEC_ANALOG_OUTPUT_Batch_Append( &prepared_batch, &frame ) );

    ExpectSuccessfulConfig( false );
    ASSERT_TRUE( EXEC_ANALOGUE_OUTPUT_Config( false ) );
    ::testing::Mock::VerifyAndClearExpectations( &mock_hw_spi );

    ExpectPayloadLoad( mock_hw_spi, EXPECTED_BYTES, false );
    EXPECT_CALL( mock_hw_spi, TxTrigger( SPI_DAC ) ).Times( 0 );

    EXPECT_FALSE( EXEC_ANALOG_OUTPUT_Submit_Prepared_Batch( &prepared_batch ) );
}

TEST_F( ExecAnalogueOutputTest, SubmitPreparedBatch_RejectsMalformedAndNullBatches )
{
    ExpectSuccessfulConfig( false );
    ASSERT_TRUE( EXEC_ANALOGUE_OUTPUT_Config( false ) );
    ::testing::Mock::VerifyAndClearExpectations( &mock_hw_spi );

    AnalogueOutputPreparedBatch_T prepared_batch;
    ASSERT_TRUE( EXEC_ANALOG_OUTPUT_Batch_Init( &prepared_batch ) );
    prepared_batch.byte_count = 1U;

    EXPECT_FALSE( EXEC_ANALOG_OUTPUT_Submit_Prepared_Batch( &prepared_batch ) );
    EXPECT_FALSE( EXEC_ANALOG_OUTPUT_Submit_Prepared_Batch( nullptr ) );
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

TEST_F( ExecAnalogueOutputTest, WriteVoltage_InvalidChannelsReturnFalseWithoutSPIWrite )
{
    constexpr std::array<uint8_t, 3U> INVALID_CHANNELS = { 6U, 7U, UINT8_MAX };

    ExpectSuccessfulConfig( false );
    ASSERT_TRUE( EXEC_ANALOGUE_OUTPUT_Config( false ) );
    ::testing::Mock::VerifyAndClearExpectations( &mock_hw_spi );

    EXPECT_CALL( mock_hw_spi, LoadTxBuffer( SPI_DAC, ::testing::_, ::testing::_ ) ).Times( 0 );
    EXPECT_CALL( mock_hw_spi, TxTrigger( SPI_DAC ) ).Times( 0 );

    for ( uint8_t channel : INVALID_CHANNELS )
    {
        EXPECT_FALSE( EXEC_ANALOG_OUTPUT_Write_Voltage( channel, 12.0F ) );
    }
}

TEST_F( ExecAnalogueOutputTest, WriteVoltage_NonFiniteInputsReturnFalseWithoutSPIWrite )
{
    constexpr std::array<float, 3U> NON_FINITE_VOLTAGES = {
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(),
    };

    ExpectSuccessfulConfig( false );
    ASSERT_TRUE( EXEC_ANALOGUE_OUTPUT_Config( false ) );
    ::testing::Mock::VerifyAndClearExpectations( &mock_hw_spi );

    for ( float input_voltage_v : NON_FINITE_VOLTAGES )
    {
        EXPECT_FALSE( EXEC_ANALOG_OUTPUT_Write_Voltage( 0U, input_voltage_v ) );
    }
}

TEST_F( ExecAnalogueOutputTest, WriteVoltage_LoadRejectionPropagatesWithoutTrigger )
{
    ExpectSuccessfulConfig( false );
    ASSERT_TRUE( EXEC_ANALOGUE_OUTPUT_Config( false ) );
    ::testing::Mock::VerifyAndClearExpectations( &mock_hw_spi );

    constexpr std::array<uint8_t, 3U> EXPECTED_BYTES = { 0x10U, 0x08U, 0x00U };
    ExpectPayloadLoad( mock_hw_spi, EXPECTED_BYTES, false );
    EXPECT_CALL( mock_hw_spi, TxTrigger( SPI_DAC ) ).Times( 0 );

    EXPECT_FALSE( EXEC_ANALOG_OUTPUT_Write_Voltage( 2U, 10.0F ) );
}
