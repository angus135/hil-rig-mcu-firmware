/******************************************************************************
 *  File:       hw_adc.h
 *  Author:     Callum Rafferty
 *  Created:    25-Mar-2026
 *
 *  Description:
 *      <Short description of the module, what it exposes, and how it should be used>
 *
 *  Notes:
 *      <Public assumptions, required initialisation order, dependencies, etc.>
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

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

/**
 * @brief Starts th measurements of the DMA channels 
 *
 * This function calls the Timer3 peripheral and the ADC1 peripheral to start polling 
 */
void HW_ADC_Start_DMA_Measurements( void );

/**
 * @brief Reads a certain number of the previous DMA measurements (unordered)
 *
 * @param measurements - pointer to array to be filled with last number of measurements
 * @param number - the number of previous measurements to read
 *
 * Note: it is the callers responsibility to ensure that measurements has enough memory allocated to fit 'number' of measurements.
 */
void HW_ADC_Read_DMA_Measurements( ADCMeasurement_T* measurements, uint32_t number );

#ifdef __cplusplus
}
#endif

#endif /* <FILENAME>_H */
