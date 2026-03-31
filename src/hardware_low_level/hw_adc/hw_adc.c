/******************************************************************************
 *  File:       hw_adc.c
 *  Author:     Angus Corr
 *  Created:    25-Mar-2026
 *
 *  Description:
 *      Low-level ADC measurement handling for the HIL-RIG. This module
 *      configures ADC measurement frequency, controls timer-triggered DMA
 *      sampling, provides access to recent DMA measurements, and supports
 *      slower one-shot polled ADC reads.
 *
 *  Notes:
 *      This module separates ADC hardware access from higher-level application
 *      logic. Continuous analogue acquisition is intended to run in the
 *      background using a hardware timer and DMA so that execution-time code
 *      can retrieve recent measurements without performing blocking ADC
 *      conversions.
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#ifdef TEST_BUILD
#include "tests/hw_adc_mocks.h"
#else
#include "adc.h"
#include "tim.h"
#include "stm32f4xx_ll_dma.h"
#include "stm32f446xx.h"
#endif

#include "hw_adc.h"
#include "hw_timer.h"
#include <stdint.h>
#include <stdbool.h>
#include "stddef.h"

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */

#define HW_ADC_DMA_CHANNEL DMA2
#define HW_ADC_DMA_STREAM LL_DMA_STREAM_0
#define HW_ADC_ADC_PERIPHERAL &hadc1

#define VIN_ADC_HANDLE &hadc3
#define VIN_ADC_CHANNEL ADC_CHANNEL_14

// Enforcing DMA buffer to be a power of 2 in size
#define ADC_DMA_SHIFT_FACTOR 7
#define ADC_DMA_LEN ( 1 << ADC_DMA_SHIFT_FACTOR )

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
// This variable cannot be made static as it is referenced in inline functions
ADCMeasurement_T adc_dma_buf[ADC_DMA_LEN];

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
 * @returns bool - true if successful, otherwise false
 *
 * This function calls the Timer peripheral and the ADC peripheral to start triggering the ADC
 * peripheral at a previously pecified frequency which will transfer the result over DMA.
 */
bool HW_ADC_Start_DMA_Measurements( void )
{
    // TODO: add the ability to set the number of channels to 1 or more rather than default to 2.
    HW_TIMER_Start_Timer( ANALOGUE_INPUT_TIMER );
    HAL_StatusTypeDef status =
        HAL_ADC_Start_DMA( HW_ADC_ADC_PERIPHERAL, ( uint32_t* )adc_dma_buf,
                           ADC_DMA_LEN * sizeof( ADCMeasurement_T ) / sizeof( uint16_t ) );
    if ( status == HAL_OK )
    {
        return true;
    }
    else
    {
        return false;
    }
}

/**
 * @brief Stops the measurements of the DMA channels
 *
 * @returns bool - true if successful, otherwise false
 *
 */
bool HW_ADC_Stop_DMA_Measurements( void )
{
    HW_TIMER_Stop_Timer( ANALOGUE_INPUT_TIMER );
    HAL_StatusTypeDef status = HAL_ADC_Stop_DMA( HW_ADC_ADC_PERIPHERAL );
    if ( status == HAL_OK )
    {
        return true;
    }
    else
    {
        return false;
    }
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
    uint32_t psc = 0;
    uint32_t arr = 0;
    switch ( rate )
    {
        case ADC_SAMPLE_RATE_100K_HZ:
            psc = ADC_SAMPLE_100K_PSC;
            arr = ADC_SAMPLE_100K_ARR;
            break;
        case ADC_SAMPLE_RATE_50K_HZ:
            psc = ADC_SAMPLE_50K_PSC;
            arr = ADC_SAMPLE_50K_ARR;
            break;
        case ADC_SAMPLE_RATE_10K_HZ:
            psc = ADC_SAMPLE_10K_PSC;
            arr = ADC_SAMPLE_10K_ARR;
            break;
        case ADC_SAMPLE_RATE_5K_HZ:
            psc = ADC_SAMPLE_5K_PSC;
            arr = ADC_SAMPLE_5K_ARR;
            break;
        case ADC_SAMPLE_RATE_1K_HZ:
            psc = ADC_SAMPLE_1K_PSC;
            arr = ADC_SAMPLE_1K_ARR;
            break;
        case ADC_SAMPLE_RATE_500_HZ:
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
 * @brief Reads a certain number of the most recent DMA measurements (in reverse chronological
 * order)
 *
 * @param measurements - pointer to array to be filled with the most recent measurements
 * @param number       - number of previous measurements to read
 *
 * @note
 *  - The caller must ensure that `measurements` has capacity for at least `number` elements.
 *
 *  - This function performs a non-blocking snapshot read from a continuously updating DMA circular
 * buffer. No locking or synchronisation is performed with the DMA engine.
 *
 *  - Correct operation relies on the assumption that the DMA does not overwrite samples that are
 * being read. This is ensured if: • `number` is sufficiently small relative to ADC_DMA_LEN, and •
 * the function executes significantly faster than the DMA update rate.
 *
 *  - If these conditions are violated, samples may be:
 *      • inconsistent (partially updated), or
 *      • overwritten during the read, resulting in undefined ordering.
 *
 *  - If `number > ADC_DMA_LEN`, the buffer will wrap and older samples will repeat.
 *
 *  - If `number == 0`, no data will be written.
 *
 *  - The returned data is ordered such that:
 *      measurements[0] = most recent sample
 *      measurements[1] = previous sample
 *      ...
 *
 * @warning
 *  This function is not safe for large reads at high sampling frequencies without additional
 *  synchronisation (e.g., double buffering or DMA half-transfer handling).
 */
inline void HW_ADC_Read_DMA_Measurements( ADCMeasurement_T* measurements, uint32_t number )
{
    uint32_t current_index = ADC_DMA_LEN
                             - ( LL_DMA_GetDataLength( HW_ADC_DMA_CHANNEL, HW_ADC_DMA_STREAM )
                                 >> 1 );  // Dividing by 2 because of 2 ADC measurements per struct

    if ( current_index >= ADC_DMA_LEN )
    {
        current_index = 0U;
    }
    for ( uint32_t i = 0; i < number; i++ )
    {
        measurements[i] = adc_dma_buf[( current_index - i - 1 ) & ( ADC_DMA_LEN - 1 )];
    }
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
uint16_t HW_ADC_Read_Polled_Measurement( ADCSource_T source )
{
    ADC_HandleTypeDef* hadc    = NULL;
    uint32_t           channel = 0;
    uint16_t           value   = 0;
    switch ( source )
    {
        case ( ADC_SOURCE_VIN ):
            hadc    = VIN_ADC_HANDLE;
            channel = VIN_ADC_CHANNEL;
            break;
        default:
            return UINT16_MAX;
    }

    ADC_ChannelConfTypeDef s_config = { 0 };

    s_config.Channel      = channel;
    s_config.Rank         = 1;
    s_config.SamplingTime = ADC_SAMPLETIME_15CYCLES;  // TODO: Adjust
    s_config.Offset       = 0U;

    if ( HAL_ADC_ConfigChannel( hadc, &s_config ) != HAL_OK )
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
}
