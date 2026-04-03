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
#include <stdint.h>
#include <stdbool.h>
// Add other required includes here

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */
#define ADC_DMA_LEN 100
#ifndef TEST_BUILD
#define HW_ADC_DMA_CHANNEL DMA2
#define HW_ADC_DMA_STREAM 0
#define HW_ADC_ADC_PERIPHERAL &hadc1

#endif

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
 * @brief Starts th measurements of the DMA channels
 *
 * This function calls the Timer3 peripheral and the ADC1 peripheral to start polling
 */
void HW_ADC_Start_DMA_Measurements( void )
{
#ifdef TEST_BUILD
#else
    HAL_TIM_Base_Start( &htim3 );
    HAL_ADC_Start_DMA( HW_ADC_ADC_PERIPHERAL, ( uint32_t* )adc_dma_buf,
                       ADC_DMA_LEN * sizeof( ADCMeasurement_T ) / sizeof( uint16_t ) );
#endif
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
void HW_ADC_Read_DMA_Measurements( ADCMeasurement_T* measurements, uint32_t number )
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
