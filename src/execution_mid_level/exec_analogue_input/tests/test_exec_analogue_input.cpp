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
using ::testing::Eq;
using ::testing::Return;
using ::testing::SetArrayArgument;

/**-----------------------------------------------------------------------------
 *  Test Doubles / Mocks
 *------------------------------------------------------------------------------
 */

class MockHwAdc
{
public:
    MOCK_METHOD( bool, ConfigureADCMeasurementFrequency,
                 ( decltype( AnalogueInputConfiguration_T{}.adc_sample_rate ) sample_rate ), () );

    MOCK_METHOD( void, ReadDmaMeasurements,
                 ( ADCMeasurement_T * destination, uint32_t sample_count ), () );
};

static MockHwAdc* g_hw_adc_mock = nullptr;

extern "C"
{
bool HW_ADC_Configure_ADC_Measurement_Frequency(
    decltype( AnalogueInputConfiguration_T{}.adc_sample_rate ) sample_rate )
{
    return g_hw_adc_mock->ConfigureADCMeasurementFrequency( sample_rate );
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
    AnalogueInputConfiguration_T configuration = {};
    configuration.channels_enabled.ch_0        = 0U;
    configuration.channels_enabled.ch_1        = 1U;

    EXPECT_CALL( mock_hw_adc, ConfigureADCMeasurementFrequency( _ ) ).Times( 0 );

    bool result = EXEC_ANALOGUE_INPUT_Configure_Analogue_Inputs( configuration );

    EXPECT_FALSE( result );
}

TEST_F( ExecAnalogueInputTest, ConfigureAnalogueInputs_ReturnsFalse_WhenChannel1IsDisabled )
{
    AnalogueInputConfiguration_T configuration = {};
    configuration.channels_enabled.ch_0        = 1U;
    configuration.channels_enabled.ch_1        = 0U;

    EXPECT_CALL( mock_hw_adc, ConfigureADCMeasurementFrequency( _ ) ).Times( 0 );

    bool result = EXEC_ANALOGUE_INPUT_Configure_Analogue_Inputs( configuration );

    EXPECT_FALSE( result );
}

TEST_F( ExecAnalogueInputTest, ConfigureAnalogueInputs_ReturnsFalse_WhenHwAdcConfigurationFails )
{
    AnalogueInputConfiguration_T configuration = {};
    configuration.channels_enabled.ch_0        = 1U;
    configuration.channels_enabled.ch_1        = 1U;

    EXPECT_CALL( mock_hw_adc,
                 ConfigureADCMeasurementFrequency( Eq( configuration.adc_sample_rate ) ) )
        .WillOnce( Return( false ) );

    bool result = EXEC_ANALOGUE_INPUT_Configure_Analogue_Inputs( configuration );

    EXPECT_FALSE( result );
}

TEST_F( ExecAnalogueInputTest,
        ConfigureAnalogueInputs_ReturnsTrue_WhenConfigurationIsValidAndHwAdcSucceeds )
{
    AnalogueInputConfiguration_T configuration = {};
    configuration.channels_enabled.ch_0        = 1U;
    configuration.channels_enabled.ch_1        = 1U;

    EXPECT_CALL( mock_hw_adc,
                 ConfigureADCMeasurementFrequency( Eq( configuration.adc_sample_rate ) ) )
        .WillOnce( Return( true ) );

    bool result = EXEC_ANALOGUE_INPUT_Configure_Analogue_Inputs( configuration );

    EXPECT_TRUE( result );
}

TEST_F( ExecAnalogueInputTest, ReadAnalogueInputs_AveragesEightSamplesAndStoresResults )
{
    uint32_t channel_0_voltage = 0U;
    uint32_t channel_1_voltage = 0U;

    AnalogueInputVoltages_T voltage_destination = {};
    voltage_destination.channel_0_voltage       = &channel_0_voltage;
    voltage_destination.channel_1_voltage       = &channel_1_voltage;

    ADCMeasurement_T measurements[TEST_SAMPLES_TAKEN] = {
        { .ch_0 = 10U, .ch_1 = 100U }, { .ch_0 = 20U, .ch_1 = 110U }, { .ch_0 = 30U, .ch_1 = 120U },
        { .ch_0 = 40U, .ch_1 = 130U }, { .ch_0 = 50U, .ch_1 = 140U }, { .ch_0 = 60U, .ch_1 = 150U },
        { .ch_0 = 70U, .ch_1 = 160U }, { .ch_0 = 80U, .ch_1 = 170U } };

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

    AnalogueInputVoltages_T voltage_destination = {};
    voltage_destination.channel_0_voltage       = &channel_0_voltage;
    voltage_destination.channel_1_voltage       = &channel_1_voltage;

    ADCMeasurement_T measurements[TEST_SAMPLES_TAKEN] = {
        { .ch_0 = 0U, .ch_1 = 0U }, { .ch_0 = 0U, .ch_1 = 0U }, { .ch_0 = 0U, .ch_1 = 0U },
        { .ch_0 = 0U, .ch_1 = 0U }, { .ch_0 = 0U, .ch_1 = 0U }, { .ch_0 = 0U, .ch_1 = 0U },
        { .ch_0 = 0U, .ch_1 = 0U }, { .ch_0 = 0U, .ch_1 = 0U } };

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

    AnalogueInputVoltages_T voltage_destination = {};
    voltage_destination.channel_0_voltage       = &channel_0_voltage;
    voltage_destination.channel_1_voltage       = &channel_1_voltage;

    ADCMeasurement_T measurements[TEST_SAMPLES_TAKEN] = {
        { .ch_0 = 8U, .ch_1 = 16U }, { .ch_0 = 8U, .ch_1 = 16U }, { .ch_0 = 8U, .ch_1 = 16U },
        { .ch_0 = 8U, .ch_1 = 16U }, { .ch_0 = 8U, .ch_1 = 16U }, { .ch_0 = 8U, .ch_1 = 16U },
        { .ch_0 = 8U, .ch_1 = 16U }, { .ch_0 = 72U, .ch_1 = 80U } };

    EXPECT_CALL( mock_hw_adc, ReadDmaMeasurements( _, TEST_SAMPLES_TAKEN ) )
        .WillOnce(
            DoAll( SetArrayArgument<0>( measurements, measurements + TEST_SAMPLES_TAKEN ) ) );

    EXEC_ANALOGUE_INPUT_Read_Analogue_Inputs( voltage_destination );

    EXPECT_EQ( channel_0_voltage, 16U );
    EXPECT_EQ( channel_1_voltage, 24U );
}
