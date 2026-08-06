/******************************************************************************
 *  File:       test_exec_analogue_input.cpp
 *  Author:     Angus Corr
 *  Created:    25-Mar-2026
 *
 *  Description:
 *
 *  Notes:
 *
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>

extern "C"
{
#include "exec_analogue_input.h" /* Module under test */
#include "hw_adc.h"
#include <stdint.h>
#include <stdbool.h>
}

/**-----------------------------------------------------------------------------
 *  Test Constants / Macros
 *------------------------------------------------------------------------------
 */

constexpr uint32_t TEST_SAMPLES_TAKEN = 8U;

using ::testing::_;
using ::testing::DoAll;
using ::testing::Return;
using ::testing::SetArrayArgument;

/**-----------------------------------------------------------------------------
 *  Test Doubles / Mocks
 *------------------------------------------------------------------------------
 */

class MockHwAdc
{
public:
    MOCK_METHOD( bool, ConfigureADCMeasurementFrequency, ( ADCSampleRates_T sample_rate ), () );

    MOCK_METHOD( bool, StartDmaMeasurements, () );

    MOCK_METHOD( bool, StopDmaMeasurements, () );

    MOCK_METHOD( void, ReadDmaMeasurements,
                 ( ADCMeasurement_T * destination, uint32_t sample_count ), () );
};

static MockHwAdc* g_hw_adc_mock = nullptr;

extern "C"
{
bool HW_ADC_Configure_ADC_Measurement_Frequency( ADCSampleRates_T sample_rate )
{
    return g_hw_adc_mock->ConfigureADCMeasurementFrequency( sample_rate );
}

bool HW_ADC_Start_DMA_Measurements( void )
{
    return g_hw_adc_mock->StartDmaMeasurements();
}

bool HW_ADC_Stop_DMA_Measurements( void )
{
    return g_hw_adc_mock->StopDmaMeasurements();
}

void HW_ADC_Read_DMA_Measurements( ADCMeasurement_T* destination, uint32_t sample_count )
{
    g_hw_adc_mock->ReadDmaMeasurements( destination, sample_count );
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
class ExecAnalogueInputTest : public ::testing::Test
{
protected:
    MockHwAdc mock_hw_adc;

    void SetUp( void ) override
    {
        g_hw_adc_mock = &mock_hw_adc;
    }

    void TearDown( void ) override
    {
        g_hw_adc_mock = nullptr;
    }
};

/**-----------------------------------------------------------------------------
 *  Test Cases
 *------------------------------------------------------------------------------
 */

TEST_F( ExecAnalogueInputTest, ConfigureAnalogueInputs_ReturnsFalse_WhenChannel0IsDisabled )
{
    ExecAnalogueInputConfig_T configuration = {};
    configuration.sample_rate               = EXEC_ANALOGUE_INPUT_SAMPLE_RATE_10K_HZ;
    configuration.ch_0_is_enabled           = false;
    configuration.ch_1_is_enabled           = true;

    EXPECT_CALL( mock_hw_adc, ConfigureADCMeasurementFrequency( _ ) ).Times( 0 );

    bool result = EXEC_ANALOGUE_INPUT_Configure_Analogue_Inputs( configuration );

    EXPECT_FALSE( result );
}

TEST_F( ExecAnalogueInputTest, ConfigureAnalogueInputs_ReturnsFalse_WhenChannel1IsDisabled )
{
    ExecAnalogueInputConfig_T configuration = {};
    configuration.sample_rate               = EXEC_ANALOGUE_INPUT_SAMPLE_RATE_10K_HZ;
    configuration.ch_0_is_enabled           = true;
    configuration.ch_1_is_enabled           = false;

    EXPECT_CALL( mock_hw_adc, ConfigureADCMeasurementFrequency( _ ) ).Times( 0 );

    bool result = EXEC_ANALOGUE_INPUT_Configure_Analogue_Inputs( configuration );

    EXPECT_FALSE( result );
}

TEST_F( ExecAnalogueInputTest, ConfigureAnalogueInputs_ReturnsFalse_WhenHwAdcConfigurationFails )
{
    ExecAnalogueInputConfig_T configuration = {};
    configuration.sample_rate               = EXEC_ANALOGUE_INPUT_SAMPLE_RATE_50K_HZ;
    configuration.ch_0_is_enabled           = true;
    configuration.ch_1_is_enabled           = true;

    EXPECT_CALL( mock_hw_adc, ConfigureADCMeasurementFrequency( ADC_SAMPLE_RATE_50K_HZ ) )
        .WillOnce( Return( false ) );

    bool result = EXEC_ANALOGUE_INPUT_Configure_Analogue_Inputs( configuration );

    EXPECT_FALSE( result );
}

TEST_F( ExecAnalogueInputTest, ConfigureAnalogueInputs_TranslatesAllSupportedSampleRates )
{
    struct SampleRateMapping_T
    {
        ExecAnalogueInputSampleRate_T exec_rate;
        ADCSampleRates_T              hw_rate;
    };

    const SampleRateMapping_T mappings[] = {
        { EXEC_ANALOGUE_INPUT_SAMPLE_RATE_100K_HZ, ADC_SAMPLE_RATE_100K_HZ },
        { EXEC_ANALOGUE_INPUT_SAMPLE_RATE_50K_HZ, ADC_SAMPLE_RATE_50K_HZ },
        { EXEC_ANALOGUE_INPUT_SAMPLE_RATE_10K_HZ, ADC_SAMPLE_RATE_10K_HZ },
        { EXEC_ANALOGUE_INPUT_SAMPLE_RATE_5K_HZ, ADC_SAMPLE_RATE_5K_HZ },
        { EXEC_ANALOGUE_INPUT_SAMPLE_RATE_1K_HZ, ADC_SAMPLE_RATE_1K_HZ },
        { EXEC_ANALOGUE_INPUT_SAMPLE_RATE_500_HZ, ADC_SAMPLE_RATE_500_HZ },
    };

    for ( const SampleRateMapping_T& mapping : mappings )
    {
        ExecAnalogueInputConfig_T configuration = {};
        configuration.sample_rate               = mapping.exec_rate;
        configuration.ch_0_is_enabled           = true;
        configuration.ch_1_is_enabled           = true;

        EXPECT_CALL( mock_hw_adc, ConfigureADCMeasurementFrequency( mapping.hw_rate ) )
            .WillOnce( Return( true ) );

        EXPECT_TRUE( EXEC_ANALOGUE_INPUT_Configure_Analogue_Inputs( configuration ) );
    }
}

TEST_F( ExecAnalogueInputTest, ConfigureAnalogueInputs_RejectsInvalidExecutionSampleRate )
{
    ExecAnalogueInputConfig_T configuration = {};
    configuration.sample_rate               = static_cast<ExecAnalogueInputSampleRate_T>( 999U );
    configuration.ch_0_is_enabled           = true;
    configuration.ch_1_is_enabled           = true;

    EXPECT_CALL( mock_hw_adc, ConfigureADCMeasurementFrequency( _ ) ).Times( 0 );

    EXPECT_FALSE( EXEC_ANALOGUE_INPUT_Configure_Analogue_Inputs( configuration ) );
}

TEST_F( ExecAnalogueInputTest, StartReturnsTrueWhenHardwareStartSucceeds )
{
    EXPECT_CALL( mock_hw_adc, StartDmaMeasurements() ).WillOnce( Return( true ) );

    EXPECT_TRUE( EXEC_ANALOGUE_INPUT_Start() );
}

TEST_F( ExecAnalogueInputTest, StartReturnsFalseWhenHardwareStartFails )
{
    EXPECT_CALL( mock_hw_adc, StartDmaMeasurements() ).WillOnce( Return( false ) );

    EXPECT_FALSE( EXEC_ANALOGUE_INPUT_Start() );
}

TEST_F( ExecAnalogueInputTest, StopReturnsTrueWhenHardwareStopSucceeds )
{
    EXPECT_CALL( mock_hw_adc, StopDmaMeasurements() ).WillOnce( Return( true ) );

    EXPECT_TRUE( EXEC_ANALOGUE_INPUT_Stop() );
}

TEST_F( ExecAnalogueInputTest, StopReturnsFalseWhenHardwareStopFails )
{
    EXPECT_CALL( mock_hw_adc, StopDmaMeasurements() ).WillOnce( Return( false ) );

    EXPECT_FALSE( EXEC_ANALOGUE_INPUT_Stop() );
}

TEST_F( ExecAnalogueInputTest, ReadAnalogueInputs_AveragesEightSamplesAndStoresResults )
{
    uint32_t channel_0_voltage = 0U;
    uint32_t channel_1_voltage = 0U;

    ExecAnalogueInputVoltages_T voltage_destination = {};
    voltage_destination.channel_0_voltage           = &channel_0_voltage;
    voltage_destination.channel_1_voltage           = &channel_1_voltage;

    ADCMeasurement_T measurements[TEST_SAMPLES_TAKEN] = {
        { 10U, 100U }, { 20U, 110U }, { 30U, 120U }, { 40U, 130U },
        { 50U, 140U }, { 60U, 150U }, { 70U, 160U }, { 80U, 170U } };

    EXPECT_CALL( mock_hw_adc, ReadDmaMeasurements( _, TEST_SAMPLES_TAKEN ) )
        .WillOnce(
            DoAll( SetArrayArgument<0>( measurements, measurements + TEST_SAMPLES_TAKEN ) ) );

    EXEC_ANALOGUE_INPUT_Read_Analogue_Inputs( voltage_destination );

    EXPECT_EQ( channel_0_voltage, 45U );
    EXPECT_EQ( channel_1_voltage, 135U );
}

TEST_F( ExecAnalogueInputTest, ReadAnalogueInputs_StoresZeroes_WhenAllSamplesAreZero )
{
    uint32_t channel_0_voltage = 123U;
    uint32_t channel_1_voltage = 456U;

    ExecAnalogueInputVoltages_T voltage_destination = {};
    voltage_destination.channel_0_voltage           = &channel_0_voltage;
    voltage_destination.channel_1_voltage           = &channel_1_voltage;

    ADCMeasurement_T measurements[TEST_SAMPLES_TAKEN] = { { 0U, 0U }, { 0U, 0U }, { 0U, 0U },
                                                          { 0U, 0U }, { 0U, 0U }, { 0U, 0U },
                                                          { 0U, 0U }, { 0U, 0U } };

    EXPECT_CALL( mock_hw_adc, ReadDmaMeasurements( _, TEST_SAMPLES_TAKEN ) )
        .WillOnce(
            DoAll( SetArrayArgument<0>( measurements, measurements + TEST_SAMPLES_TAKEN ) ) );

    EXEC_ANALOGUE_INPUT_Read_Analogue_Inputs( voltage_destination );

    EXPECT_EQ( channel_0_voltage, 0U );
    EXPECT_EQ( channel_1_voltage, 0U );
}

TEST_F( ExecAnalogueInputTest, ReadAnalogueInputs_UsesAllSamplesInAverage )
{
    uint32_t channel_0_voltage = 0U;
    uint32_t channel_1_voltage = 0U;

    ExecAnalogueInputVoltages_T voltage_destination = {};
    voltage_destination.channel_0_voltage           = &channel_0_voltage;
    voltage_destination.channel_1_voltage           = &channel_1_voltage;

    ADCMeasurement_T measurements[TEST_SAMPLES_TAKEN] = { { 8U, 16U }, { 8U, 16U }, { 8U, 16U },
                                                          { 8U, 16U }, { 8U, 16U }, { 8U, 16U },
                                                          { 8U, 16U }, { 72U, 80U } };

    EXPECT_CALL( mock_hw_adc, ReadDmaMeasurements( _, TEST_SAMPLES_TAKEN ) )
        .WillOnce(
            DoAll( SetArrayArgument<0>( measurements, measurements + TEST_SAMPLES_TAKEN ) ) );

    EXEC_ANALOGUE_INPUT_Read_Analogue_Inputs( voltage_destination );

    EXPECT_EQ( channel_0_voltage, 16U );
    EXPECT_EQ( channel_1_voltage, 24U );
}
