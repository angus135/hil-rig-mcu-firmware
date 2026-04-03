/******************************************************************************
 *  File:       hw_adc.c
 *  Author:     Angus Corr
 *  Created:    25-Mar-2026
 *
 *  Description:
 *      <Short description of the module's purpose and responsibilities>
 *
 *  Notes:
 *      <Any design notes, dependencies, or assumptions go here>
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#ifndef TEST_BUILD
#include "adc.h"
#include "tim.h"
#include "stm32f4xx_ll_dma.h"
#include "stm32f446xx.h"
#endif

#include "hw_adc.h"
#include "hw_timer.h"
#include <stdint.h>
#include <stdbool.h>
// Add other required includes here

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */

#ifndef TEST_BUILD
#define HW_ADC_DMA_CHANNEL DMA2
#define HW_ADC_DMA_STREAM 0
#define HW_ADC_ADC_PERIPHERAL &hadc1

#define VIN_ADC_HANDLE &hadc3
#define VIN_ADC_CHANNEL ADC_CHANNEL_14

#endif

#define ADC_DMA_LEN 100

// Defines for DMA timing
#define ADC_SAMPLE_100K_ARR 899
#define ADC_SAMPLE_100K_PSC 0
#define ADC_SAMPLE_50K_ARR 899
#define ADC_SAMPLE_50K_PSC 1
#define ADC_SAMPLE_10K_ARR 8999
#define ADC_SAMPLE_10K_PSC 0
#define ADC_SAMPLE_5K_ARR 8999
#define ADC_SAMPLE_5K_PSC 1
#define ADC_SAMPLE_1K_ARR 8999
#define ADC_SAMPLE_1K_PSC 9
#define ADC_SAMPLE_500_ARR 8999
#define ADC_SAMPLE_500_PSC 19

/**-----------------------------------------------------------------------------
 *  Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Public (global) and Extern Variables
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Private (static) Variables
 *------------------------------------------------------------------------------
 */
static ADCMeasurement_T adc_dma_buf[ADC_DMA_LEN];

/**-----------------------------------------------------------------------------
 *  Private (static) Function Prototypes
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */

/**
 * @brief Starts the measurements of the DMA channels
 *
 * This function calls the Timer peripheral and the ADC peripheral to start polling at a previously
 * specified frequency
 */
void HW_ADC_Start_DMA_Measurements( void )
{
    // TODO: add the ability to set the number of channels to 1 or more rather than default to 2.
#ifdef TEST_BUILD
#else
    HW_TIMER_Start_Timer( ANALOGUE_INPUT_TIMER );
    HAL_ADC_Start_DMA( HW_ADC_ADC_PERIPHERAL, ( uint32_t* )adc_dma_buf,
                       ADC_DMA_LEN * sizeof( ADCMeasurement_T ) / sizeof( uint16_t ) );
#endif
}

/**
 * @brief Stops the measurements of the DMA channels
 *
 */
void HW_ADC_Stop_DMA_Measurements( void )
{
#ifdef TEST_BUILD
#else
    HW_TIMER_Stop_Timer( ANALOGUE_INPUT_TIMER );
    HAL_ADC_Stop_DMA( HW_ADC_ADC_PERIPHERAL );
#endif
}

/**
 * @brief Starts the measurements of the DMA channels
 *
 * @param rate - the sample rate which the ADC measurement is being configured to sample at
 *
 * @return bool - true if rate is supported, otherwise false
 *
 * Note: All channels will be sampled at this same rate.
 */
bool HW_ADC_Configure_ADC_Measurement_Frequency( ADCSampleRates_T rate )
{
#ifdef TEST_BUILD
#else
#endif
    uint32_t psc = 0;
    uint32_t arr = 0;
    switch ( rate )
    {
        case ADC_SAMPLE_RATE_100K:
            psc = ADC_SAMPLE_100K_PSC;
            arr = ADC_SAMPLE_100K_ARR;
            break;
        case ADC_SAMPLE_RATE_50K:
            psc = ADC_SAMPLE_50K_PSC;
            arr = ADC_SAMPLE_50K_ARR;
            break;
        case ADC_SAMPLE_RATE_10K:
            psc = ADC_SAMPLE_10K_PSC;
            arr = ADC_SAMPLE_10K_ARR;
            break;
        case ADC_SAMPLE_RATE_5K:
            psc = ADC_SAMPLE_5K_PSC;
            arr = ADC_SAMPLE_5K_ARR;
            break;
        case ADC_SAMPLE_RATE_1K:
            psc = ADC_SAMPLE_1K_PSC;
            arr = ADC_SAMPLE_1K_ARR;
            break;
        case ADC_SAMPLE_RATE_500:
            psc = ADC_SAMPLE_500_PSC;
            arr = ADC_SAMPLE_500_ARR;
            break;
        default:
            return false;
    }
    HW_TIMER_Configure_Timer( ANALOGUE_INPUT_TIMER, psc, arr );
    return true;
}

/**
 * @brief Reads a certain number of the previous DMA measurements (unordered)
 *
 * @param measurements - pointer to array to be filled with last number of measurements
 * @param number - the number of previous measurements to read
 *
 * Note: it is the callers responsibility to ensure that measurements has enough memory allocated to
 * fit 'number' of measurements.
 */
inline void HW_ADC_Read_DMA_Measurements( ADCMeasurement_T* measurements, uint32_t number )
{
#ifdef TEST_BUILD
    ( void )number;
    ( void )measurements;
#else
    uint32_t current_index =
        ( ADC_DMA_LEN - LL_DMA_GetDataLength( HW_ADC_DMA_CHANNEL, HW_ADC_DMA_STREAM ) )
        >> 1;  // Dividing by 2 because of 2 ADC measurements
    if ( current_index >= ADC_DMA_LEN )
    {
        current_index = 0U;
    }
    for ( uint32_t i = 0; i < number; i++ )
    {
        measurements[i] = adc_dma_buf[( current_index + ADC_DMA_LEN - i ) % ADC_DMA_LEN];
    }
#endif
}

/**
 * @brief Polls a certain ADC source
 *
 * @param source - source to poll from
 *
 * @returns uint16_t result - the result of the polling on that source
 *
 * Returns UINT16_MAX if there is a problem in retrieving the selected source adc value.
 *
 */
uint16_t HW_ADC_Read_Polled_Measurements( ADCSource_T source )
{
#ifdef TEST_BUILD
    ( void )source;
    return UINT16_MAX;
#else
    ADC_HandleTypeDef* hadc;
    uint32_t           channel;
    uint16_t           value;
    switch ( source )
    {
        case ( ADC_SOURCE_VIN ):
            hadc    = VIN_ADC_HANDLE;
            channel = VIN_ADC_CHANNEL;
            break;
        default:
            return UINT16_MAX;
    }

    ADC_ChannelConfTypeDef sConfig = { 0 };

    sConfig.Channel      = channel;
    sConfig.Rank         = 1;
    sConfig.SamplingTime = ADC_SAMPLETIME_15CYCLES; /* example */
    sConfig.Offset       = 0U;

    if ( HAL_ADC_ConfigChannel( hadc, &sConfig ) != HAL_OK )
    {
        return UINT16_MAX;
    }

    if ( HAL_ADC_Start( hadc ) != HAL_OK )
    {
        return UINT16_MAX;
    }

    if ( HAL_ADC_PollForConversion( hadc, 10U ) != HAL_OK )
    {
        ( void )HAL_ADC_Stop( hadc );
        return UINT16_MAX;
    }

    value = ( uint16_t )HAL_ADC_GetValue( hadc );

    if ( HAL_ADC_Stop( hadc ) != HAL_OK )
    {
        return UINT16_MAX;
    }

    return value;
#endif
}
