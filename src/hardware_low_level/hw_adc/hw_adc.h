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
    ADC_SAMPLE_RATE_100K_HZ,
    ADC_SAMPLE_RATE_50K_HZ,
    ADC_SAMPLE_RATE_10K_HZ,
    ADC_SAMPLE_RATE_5K_HZ,
    ADC_SAMPLE_RATE_1K_HZ,
    ADC_SAMPLE_RATE_500_HZ,
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
 * This function calls the Timer peripheral and the ADC peripheral to start triggering the ADC
 * peripheral at a previously pecified frequency which will transfer the result over DMA.
 */
bool HW_ADC_Start_DMA_Measurements( void );

/**
 * @brief Stops the measurements of the DMA channels
 *
 * @returns bool - true if successful, otherwise false
 *
 */
bool HW_ADC_Stop_DMA_Measurements( void );

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
uint16_t HW_ADC_Read_Polled_Measurement( ADCSource_T source );

#ifdef __cplusplus
}
#endif

#endif /* HW_ADC_H */
