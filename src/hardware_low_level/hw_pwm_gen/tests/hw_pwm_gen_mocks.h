/******************************************************************************
 *  File:       hw_pwm_gen_mocks.h
 *  Author:     Callum Rafferty
 *  Created:    25-Mar-2026
 *
 *  Description:
 *      Mock definitions of HAL types and functions for unit testing hw_pwm_gen module.
 *
 *  Notes:
 *
 ******************************************************************************/

#ifndef HW_PWM_GEN_MOCKS_H
#define HW_PWM_GEN_MOCKS_H

#ifdef __cplusplus
extern "C"
{
#endif
// NOLINTBEGIN

/**-----------------------------------------------------------------------------
 * Includes
 *----------------------------------------------------------------------------*/

#include <stdint.h>
#include <stdbool.h>

/**-----------------------------------------------------------------------------
 * Public Defines / Macros
 *----------------------------------------------------------------------------*/

#define HAL_OK ( 0 )
#define HAL_ERROR ( 1 )

#define ENABLE ( 1U )
#define DISABLE ( 0U )

/* Timer channels */
#define TIM_CHANNEL_1 ( 1U )
#define TIM_CHANNEL_2 ( 2U )
#define TIM_CHANNEL_3 ( 3U )
#define TIM_CHANNEL_4 ( 4U )

/* Event generation register */
#define TIM_EGR_UG ( 1U << 0 )

/**-----------------------------------------------------------------------------
 * Public Typedefs / Structures
 *----------------------------------------------------------------------------*/

typedef int HAL_StatusTypeDef;

/**
 * @brief Mock STM32 TIM peripheral registers
 */
typedef struct
{
    uint32_t CR1;
    uint32_t CR2;
    uint32_t SMCR;
    uint32_t DIER;
    uint32_t SR;
    uint32_t EGR;

    uint32_t CCMR1;
    uint32_t CCMR2;
    uint32_t CCER;

    uint32_t CNT;
    uint32_t PSC;
    uint32_t ARR;

    uint32_t CCR1;
    uint32_t CCR2;
    uint32_t CCR3;
    uint32_t CCR4;

} TIM_TypeDef;

/**
 * @brief Mock HAL timer handle
 */
typedef struct
{
    TIM_TypeDef* Instance;

} TIM_HandleTypeDef;

/**-----------------------------------------------------------------------------
 * Public Variables
 *----------------------------------------------------------------------------*/

extern TIM_HandleTypeDef htim12;
extern TIM_HandleTypeDef htim8;

extern TIM_TypeDef mock_tim12_regs;
extern TIM_TypeDef mock_tim8_regs;

/**-----------------------------------------------------------------------------
 * Public Function Prototypes
 *----------------------------------------------------------------------------*/

/* HAL Functions */

HAL_StatusTypeDef HAL_TIM_PWM_Start( TIM_HandleTypeDef* htim, uint32_t channel );

HAL_StatusTypeDef HAL_TIMEx_PWMN_Start( TIM_HandleTypeDef* htim, uint32_t channel );

/* LL Functions */

void LL_TIM_OC_SetCompareCH1( TIM_TypeDef* TIMx, uint32_t CompareValue );

void LL_TIM_OC_SetCompareCH2( TIM_TypeDef* TIMx, uint32_t CompareValue );

void LL_TIM_OC_SetCompareCH3( TIM_TypeDef* TIMx, uint32_t CompareValue );

void LL_TIM_OC_SetCompareCH4( TIM_TypeDef* TIMx, uint32_t CompareValue );

void LL_TIM_SetAutoReload( TIM_TypeDef* TIMx, uint32_t AutoReload );

void LL_TIM_SetPrescaler( TIM_TypeDef* TIMx, uint32_t Prescaler );

// NOLINTEND

#ifdef __cplusplus
}
#endif

#endif /* HW_PWM_GEN_MOCKS_H */
