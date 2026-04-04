/******************************************************************************
 *  File:       test_hw_adc.cpp
 *  Author:     Angus Corr
 *  Created:    04-Apr-2026
 *
 *  Description:
 *      Unit tests for the hw_adc module using GoogleTest and GoogleMock.
 *      This file validates the public API and behaviour defined in hw_adc.h.
 *
 *  Notes:
 *      - Production code is written in C; tests are written in C++.
 *      - C headers must be included inside an extern "C" block.
 *      - GoogleMock may be used to mock external dependencies.
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>

extern "C"
{
#include "hw_adc_mocks.h"
#include "hw_adc.h"
#include <stdint.h>
#include <stdbool.h>

#include "hw_adc.c" /* Module under test */  // NOLINT
}
/**-----------------------------------------------------------------------------
 *  Test Constants / Macros
 *------------------------------------------------------------------------------
 */

using ::testing::_;
using ::testing::Eq;
using ::testing::Invoke;
using ::testing::Return;

/**-----------------------------------------------------------------------------
 *  Test Doubles / Mocks
 *------------------------------------------------------------------------------
 */

class MockHWADC
{
public:
    MOCK_METHOD( HAL_StatusTypeDef, StartDMA,
                 ( ADC_HandleTypeDef * hadc, uint32_t* p_data, uint32_t length ), () );

    MOCK_METHOD( HAL_StatusTypeDef, StopDMA, ( ADC_HandleTypeDef * hadc ), () );

    MOCK_METHOD( uint32_t, GetDMALength, ( void* dma_x, uint32_t stream ), () );

    MOCK_METHOD( HAL_StatusTypeDef, ConfigChannel,
                 ( ADC_HandleTypeDef * hadc, ADC_ChannelConfTypeDef* s_config ), () );

    MOCK_METHOD( HAL_StatusTypeDef, StartADC, ( ADC_HandleTypeDef * hadc ), () );

    MOCK_METHOD( HAL_StatusTypeDef, PollForConversion,
                 ( ADC_HandleTypeDef * hadc, uint32_t timeout ), () );

    MOCK_METHOD( uint32_t, GetValue, ( ADC_HandleTypeDef * hadc ), () );

    MOCK_METHOD( HAL_StatusTypeDef, StopADC, ( ADC_HandleTypeDef * hadc ), () );
};

static MockHWADC* g_mock = nullptr;

// NOLINTBEGIN
extern "C" HAL_StatusTypeDef HAL_ADC_Start_DMA( ADC_HandleTypeDef* hadc, uint32_t* p_data,
                                                uint32_t length )
{
    if ( !g_mock )
    {
        return HAL_ERROR;
    }
    return g_mock->StartDMA( hadc, p_data, length );
}

extern "C" HAL_StatusTypeDef HAL_ADC_Stop_DMA( ADC_HandleTypeDef* hadc )
{
    if ( !g_mock )
    {
        return HAL_ERROR;
    }
    return g_mock->StopDMA( hadc );
}

extern "C" uint32_t LL_DMA_GetDataLength( void* dma_x, uint32_t stream )
{
    if ( !g_mock )
    {
        return 0U;
    }
    return g_mock->GetDMALength( dma_x, stream );
}

extern "C" HAL_StatusTypeDef HAL_ADC_ConfigChannel( ADC_HandleTypeDef*      hadc,
                                                    ADC_ChannelConfTypeDef* s_config )
{
    if ( !g_mock )
    {
        return HAL_ERROR;
    }
    return g_mock->ConfigChannel( hadc, s_config );
}

extern "C" HAL_StatusTypeDef HAL_ADC_Start( ADC_HandleTypeDef* hadc )
{
    if ( !g_mock )
    {
        return HAL_ERROR;
    }
    return g_mock->StartADC( hadc );
}

extern "C" HAL_StatusTypeDef HAL_ADC_PollForConversion( ADC_HandleTypeDef* hadc, uint32_t timeout )
{
    if ( !g_mock )
    {
        return HAL_ERROR;
    }
    return g_mock->PollForConversion( hadc, timeout );
}

extern "C" uint32_t HAL_ADC_GetValue( ADC_HandleTypeDef* hadc )
{
    if ( !g_mock )
    {
        return 0U;
    }
    return g_mock->GetValue( hadc );
}

extern "C" HAL_StatusTypeDef HAL_ADC_Stop( ADC_HandleTypeDef* hadc )
{
    if ( !g_mock )
    {
        return HAL_ERROR;
    }
    return g_mock->StopADC( hadc );
}
// NOLINTEND

/**-----------------------------------------------------------------------------
 *  Test Fixture
 *------------------------------------------------------------------------------
 */

/**
 * @brief Test fixture for module tests.
 *
 * Provides a consistent setup/teardown environment for all test cases.
 */
class HWADCTest : public ::testing::Test
{
protected:
    MockHWADC mock;

    void SetUp( void ) override
    {
        g_mock = &mock;

        hadc1.Instance = 1U;
        hadc3.Instance = 3U;

        for ( uint32_t i = 0U; i < 100U; i++ )
        {
            adc_dma_buf[i].ch_0 = 0U;
            adc_dma_buf[i].ch_1 = 0U;
        }
    }

    void TearDown( void ) override
    {
        g_mock = nullptr;
    }
};

/**-----------------------------------------------------------------------------
 *  Test Cases
 *------------------------------------------------------------------------------
 */

TEST_F( HWADCTest, StartDMAMeasurements_StartsADCWithExpectedBufferAndLength )
{
    EXPECT_CALL( mock, StartDMA( Eq( &hadc1 ), Eq( reinterpret_cast<uint32_t*>( adc_dma_buf ) ),
                                 Eq( 200U ) ) )
        .WillOnce( Return( HAL_OK ) );

    HW_ADC_Start_DMA_Measurements();
}

TEST_F( HWADCTest, StopDMAMeasurements_StopsADC )
{
    EXPECT_CALL( mock, StopDMA( Eq( &hadc1 ) ) ).WillOnce( Return( HAL_OK ) );

    HW_ADC_Stop_DMA_Measurements();
}

TEST_F( HWADCTest, ReadDMAMeasurements_ReadsMostRecentSamplesInReverseOrder )
{
    adc_dma_buf[8].ch_0  = 80U;
    adc_dma_buf[8].ch_1  = 81U;
    adc_dma_buf[9].ch_0  = 90U;
    adc_dma_buf[9].ch_1  = 91U;
    adc_dma_buf[10].ch_0 = 100U;
    adc_dma_buf[10].ch_1 = 101U;

    ADCMeasurement_T results[3] = { 0 };

    EXPECT_CALL( mock, GetDMALength( Eq( DMA2 ), Eq( 0U ) ) ).WillOnce( Return( 80U ) );

    HW_ADC_Read_DMA_Measurements( results, 3U );

    EXPECT_EQ( results[0].ch_0, 100U );
    EXPECT_EQ( results[0].ch_1, 101U );
    EXPECT_EQ( results[1].ch_0, 90U );
    EXPECT_EQ( results[1].ch_1, 91U );
    EXPECT_EQ( results[2].ch_0, 80U );
    EXPECT_EQ( results[2].ch_1, 81U );
}

TEST_F( HWADCTest, ReadPolledMeasurements_ReturnsMaxForInvalidSource )
{
    const uint16_t value = HW_ADC_Read_Polled_Measurements( static_cast<ADCSource_T>( 999 ) );

    EXPECT_EQ( value, UINT16_MAX );
}

TEST_F( HWADCTest, ReadPolledMeasurements_ReturnsMaxWhenConfigChannelFails )
{
    EXPECT_CALL( mock, ConfigChannel( Eq( &hadc3 ), _ ) )
        .WillOnce( Invoke( []( ADC_HandleTypeDef* hadc, ADC_ChannelConfTypeDef* s_config ) {
            EXPECT_EQ( hadc, &hadc3 );
            EXPECT_EQ( s_config->Channel, ADC_CHANNEL_14 );
            EXPECT_EQ( s_config->Rank, 1U );
            EXPECT_EQ( s_config->SamplingTime, ADC_SAMPLETIME_15CYCLES );
            EXPECT_EQ( s_config->Offset, 0U );
            return HAL_ERROR;
        } ) );

    EXPECT_CALL( mock, StartADC( _ ) ).Times( 0 );
    EXPECT_CALL( mock, PollForConversion( _, _ ) ).Times( 0 );
    EXPECT_CALL( mock, GetValue( _ ) ).Times( 0 );
    EXPECT_CALL( mock, StopADC( _ ) ).Times( 0 );

    const uint16_t value = HW_ADC_Read_Polled_Measurements( ADC_SOURCE_VIN );

    EXPECT_EQ( value, UINT16_MAX );
}

TEST_F( HWADCTest, ReadPolledMeasurements_ReturnsMaxWhenStartFails )
{
    EXPECT_CALL( mock, ConfigChannel( Eq( &hadc3 ), _ ) ).WillOnce( Return( HAL_OK ) );
    EXPECT_CALL( mock, StartADC( Eq( &hadc3 ) ) ).WillOnce( Return( HAL_ERROR ) );
    EXPECT_CALL( mock, PollForConversion( _, _ ) ).Times( 0 );
    EXPECT_CALL( mock, GetValue( _ ) ).Times( 0 );
    EXPECT_CALL( mock, StopADC( _ ) ).Times( 0 );

    const uint16_t value = HW_ADC_Read_Polled_Measurements( ADC_SOURCE_VIN );

    EXPECT_EQ( value, UINT16_MAX );
}

TEST_F( HWADCTest, ReadPolledMeasurements_ReturnsMaxWhenPollFailsAndStillStopsADC )
{
    EXPECT_CALL( mock, ConfigChannel( Eq( &hadc3 ), _ ) ).WillOnce( Return( HAL_OK ) );
    EXPECT_CALL( mock, StartADC( Eq( &hadc3 ) ) ).WillOnce( Return( HAL_OK ) );
    EXPECT_CALL( mock, PollForConversion( Eq( &hadc3 ), Eq( 10U ) ) )
        .WillOnce( Return( HAL_ERROR ) );
    EXPECT_CALL( mock, StopADC( Eq( &hadc3 ) ) ).WillOnce( Return( HAL_OK ) );
    EXPECT_CALL( mock, GetValue( _ ) ).Times( 0 );

    const uint16_t value = HW_ADC_Read_Polled_Measurements( ADC_SOURCE_VIN );

    EXPECT_EQ( value, UINT16_MAX );
}

TEST_F( HWADCTest, ReadPolledMeasurements_ReturnsMaxWhenFinalStopFails )
{
    EXPECT_CALL( mock, ConfigChannel( Eq( &hadc3 ), _ ) ).WillOnce( Return( HAL_OK ) );
    EXPECT_CALL( mock, StartADC( Eq( &hadc3 ) ) ).WillOnce( Return( HAL_OK ) );
    EXPECT_CALL( mock, PollForConversion( Eq( &hadc3 ), Eq( 10U ) ) ).WillOnce( Return( HAL_OK ) );
    EXPECT_CALL( mock, GetValue( Eq( &hadc3 ) ) ).WillOnce( Return( 1234U ) );
    EXPECT_CALL( mock, StopADC( Eq( &hadc3 ) ) ).WillOnce( Return( HAL_ERROR ) );

    const uint16_t value = HW_ADC_Read_Polled_Measurements( ADC_SOURCE_VIN );

    EXPECT_EQ( value, UINT16_MAX );
}

TEST_F( HWADCTest, ReadPolledMeasurements_ReturnsADCValueWhenSequenceSucceeds )
{
    EXPECT_CALL( mock, ConfigChannel( Eq( &hadc3 ), _ ) )
        .WillOnce( Invoke( []( ADC_HandleTypeDef* hadc, ADC_ChannelConfTypeDef* s_config ) {
            EXPECT_EQ( hadc, &hadc3 );
            EXPECT_EQ( s_config->Channel, ADC_CHANNEL_14 );
            EXPECT_EQ( s_config->Rank, 1U );
            EXPECT_EQ( s_config->SamplingTime, ADC_SAMPLETIME_15CYCLES );
            EXPECT_EQ( s_config->Offset, 0U );
            return HAL_OK;
        } ) );

    EXPECT_CALL( mock, StartADC( Eq( &hadc3 ) ) ).WillOnce( Return( HAL_OK ) );
    EXPECT_CALL( mock, PollForConversion( Eq( &hadc3 ), Eq( 10U ) ) ).WillOnce( Return( HAL_OK ) );
    EXPECT_CALL( mock, GetValue( Eq( &hadc3 ) ) ).WillOnce( Return( 2048U ) );
    EXPECT_CALL( mock, StopADC( Eq( &hadc3 ) ) ).WillOnce( Return( HAL_OK ) );

    const uint16_t value = HW_ADC_Read_Polled_Measurements( ADC_SOURCE_VIN );

    EXPECT_EQ( value, 2048U );
}
