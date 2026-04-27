/******************************************************************************
 *  File:       hw_timer.c
 *  Author:     Angus Corr
 *  Created:    18-Dec-2025
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

// #undef TEST_BUILD
#ifndef TEST_BUILD
#include "tim.h"
#include "stm32f4xx_ll_tim.h"
#include "stm32f4xx_ll_bus.h"
#include "stm32f4xx_ll_system.h"
#endif

#include <stdbool.h>
#include "hw_timer.h"
#include <stdint.h>
#include <stdbool.h>

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */

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

void TIM2_IRQHandler( void )
{
#ifdef TEST_BUILD
#else
    if ( LL_TIM_IsActiveFlag_UPDATE( TIM2 ) )
    {
        LL_TIM_ClearFlag_UPDATE( TIM2 );

        // Running Process
        EXECUTION_MANAGER_Process_From_ISR();
    }
#endif
}

void HW_TIMER_Configure_Timer( Timer_T timer, uint32_t psc, uint32_t arr )
{

#ifdef TEST_BUILD
    ( void )timer;
    ( void )psc;
    ( void )arr;
#else
    switch ( timer )
    {
        case EXECUTION_MANAGER_TIMER:
            HW_TIMER_Stop_Timer( EXECUTION_MANAGER_TIMER );
            htim2.Init.Prescaler = psc;
            htim2.Init.Period    = arr;
            if ( HAL_TIM_Base_Init( &htim2 ) != HAL_OK )
            {
                Error_Handler();
            }
            break;
        case ANALOGUE_INPUT_TIMER:
            htim3.Init.Prescaler = psc;
            htim3.Init.Period    = arr;
            if ( HAL_TIM_Base_Init( &htim3 ) != HAL_OK )
            {
                Error_Handler();
            }
            break;
        default:
            break;
    }
#endif
}

void HW_TIMER_Start_Timer( Timer_T timer )
{
#ifdef TEST_BUILD
    ( void )timer;
#else
    switch ( timer )
    {
        case EXECUTION_MANAGER_TIMER:
            // Ensure counter is stopped while configuring
            LL_TIM_DisableCounter( TIM2 );

            // Clear any pending update flag
            LL_TIM_ClearFlag_UPDATE( TIM2 );

            // Enable update interrupt
            LL_TIM_EnableIT_UPDATE( TIM2 );

            // Enable counter
            LL_TIM_EnableCounter( TIM2 );
        case ANALOGUE_INPUT_TIMER:
            HAL_TIM_Base_Start( &htim3 );
            break;
        default:
            break;
    }
#endif
}

void HW_TIMER_Stop_Timer( Timer_T timer )
{
#ifdef TEST_BUILD
    ( void )timer;
#else

    switch ( timer )
    {
        case EXECUTION_MANAGER_TIMER:
            // Disable update interrupt first (prevents new IRQs)
            LL_TIM_DisableIT_UPDATE( TIM2 );

            // Stop the counter
            LL_TIM_DisableCounter( TIM2 );

            // Clear any pending update flag (important)
            LL_TIM_ClearFlag_UPDATE( TIM2 );
        case ANALOGUE_INPUT_TIMER:
            HAL_TIM_Base_Stop( &htim3 );
            break;
        default:
            break;
    }
#endif
}
