/******************************************************************************
 *  File:       hw_adc.h
 *  Author:     Angus Corr
 *  Created:    25-Mar-2026
 *
 *  Description:
 *      Public interface for low-level ADC measurement handling. This module
 *      exposes functions for configuring ADC sample timing, starting and
 *      stopping DMA-based measurements, retrieving recent DMA samples, and
 *      performing supported polled ADC reads.
 *
 *  Notes:
 *      Intended to be used by higher-level modules that require ADC data but
 *      should not directly manage ADC, DMA, or timer hardware details.
 *      Continuous DMA measurements are primarily intended for execution-time
 *      analogue input handling, while polled reads support slower supervisory
 *      or monitoring measurements.
 ******************************************************************************/

#ifndef HW_ADC_H
#define HW_ADC_H

#ifdef __cplusplus
extern "C"
{
#endif

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include <stdint.h>
#include <stdbool.h>

/**-----------------------------------------------------------------------------
 *  Public Defines / Macros
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Public Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

typedef struct ADCMeasurement_T
{
    uint16_t ch_0;
    uint16_t ch_1;
} ADCMeasurement_T;

typedef enum ADCSampleRates_T
{
    ADC_SAMPLE_RATE_100K,
    ADC_SAMPLE_RATE_50K,
    ADC_SAMPLE_RATE_10K,
    ADC_SAMPLE_RATE_5K,
    ADC_SAMPLE_RATE_1K,
    ADC_SAMPLE_RATE_500,
} ADCSampleRates_T;

typedef enum ADCSource_T
{
    ADC_SOURCE_VIN,
} ADCSource_T;

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

/**
 * @brief Starts the measurements of the DMA channels
 *
 * This function calls the Timer peripheral and the ADC peripheral to start polling
 */
void HW_ADC_Start_DMA_Measurements( void );

/**
 * @brief Stops the measurements of the DMA channels
 *
 */
void HW_ADC_Stop_DMA_Measurements( void );

/**
 * @brief Starts the measurements of the DMA channels
 *
 * @param rate - the sample rate which the ADC measurement is being configured to sample at
 *
 * @return bool - true if rate is supported, otherwise false
 *
 * Note: All channels will be sampled at this same rate.
 */
bool HW_ADC_Configure_ADC_Measurement_Frequency( ADCSampleRates_T rate );

/**
 * @brief Reads a certain number of the previous DMA measurements (unordered)
 *
 * @param measurements - pointer to array to be filled with last number of measurements
 * @param number - the number of previous measurements to read
 *
 * Note: it is the callers responsibility to ensure that measurements has enough memory allocated to
 * fit 'number' of measurements.
 */
void HW_ADC_Read_DMA_Measurements( ADCMeasurement_T* measurements, uint32_t number );

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
uint16_t HW_ADC_Read_Polled_Measurements( ADCSource_T source );

#ifdef __cplusplus
}
#endif

#endif /* HW_ADC_H */
