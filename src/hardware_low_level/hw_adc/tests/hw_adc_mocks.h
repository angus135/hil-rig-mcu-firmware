/******************************************************************************
 *  File:       hw_adc_mocks.h
 *  Author:     Angus Corr
 *  Created:    25-Mar-2026
 *
 *  Description:
 *      Mock definitions of HAL types and functions for unit testing hw_adc module.
 *
 *  Notes:
 *
 ******************************************************************************/

#ifndef HW_ADC_MOCKS_H
#define HW_ADC_MOCKS_H

#ifdef __cplusplus
extern "C"
{
#endif
// NOLINTBEGIN

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include <stdbool.h>
#include <stdint.h>

/**-----------------------------------------------------------------------------
 *  Public Defines / Macros
 *------------------------------------------------------------------------------
 */

/* HAL status values */
#define HAL_OK ( 0 )
#define HAL_ERROR ( 1 )

/* ADC-related mock macros */
#define ADC_CHANNEL_14 ( 14U )
#define ADC_SAMPLETIME_15CYCLES ( 15U )

/* DMA-related mock macros */
#define DMA2 ( ( void* )0x40026400U )
#define LL_DMA_STREAM_0 ( 0 )

/**-----------------------------------------------------------------------------
 *  Public Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

typedef int HAL_StatusTypeDef;

typedef struct
{
    uint32_t Instance;
} ADC_HandleTypeDef;

typedef struct
{
    uint32_t Channel;
    uint32_t Rank;
    uint32_t SamplingTime;
    uint32_t Offset;
} ADC_ChannelConfTypeDef;

/**-----------------------------------------------------------------------------
 *  Public Variables
 *------------------------------------------------------------------------------
 */

ADC_HandleTypeDef hadc1;
ADC_HandleTypeDef hadc3;

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

/* HAL ADC functions */
HAL_StatusTypeDef HAL_ADC_Start_DMA( ADC_HandleTypeDef* hadc, uint32_t* p_data, uint32_t length );
HAL_StatusTypeDef HAL_ADC_Stop_DMA( ADC_HandleTypeDef* hadc );
HAL_StatusTypeDef HAL_ADC_ConfigChannel( ADC_HandleTypeDef*      hadc,
                                         ADC_ChannelConfTypeDef* s_config );
HAL_StatusTypeDef HAL_ADC_Start( ADC_HandleTypeDef* hadc );
HAL_StatusTypeDef HAL_ADC_PollForConversion( ADC_HandleTypeDef* hadc, uint32_t timeout );
uint32_t          HAL_ADC_GetValue( ADC_HandleTypeDef* hadc );
HAL_StatusTypeDef HAL_ADC_Stop( ADC_HandleTypeDef* hadc );

/* LL DMA functions */
uint32_t LL_DMA_GetDataLength( void* dma_x, uint32_t stream );

// NOLINTEND

#ifdef __cplusplus
}
#endif

#endif /* HW_UART_MOCKS_H */
