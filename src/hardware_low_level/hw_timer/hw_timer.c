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
#include "execution_manager.h"  // Include for EXECUTION_MANAGER_Process_From_ISR()

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */

/* Execution Timer Defines*/
#define EXECUTION_MANAGER_TIMER_INSTANCE TIM4
#define EXECUTION_MANAGER_TIMER_IRQ_HANDLER TIM4_IRQHandler
#define EXECUTION_MANAGER_TIMER_HANDLE htim4

/* Analogue Input Timer Defines */
#define ANALOGUE_INPUT_TIMER_HANDLE htim3

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

void EXECUTION_MANAGER_TIMER_IRQ_HANDLER( void )
{
#ifdef TEST_BUILD
#else
    if ( LL_TIM_IsActiveFlag_UPDATE( EXECUTION_MANAGER_TIMER_INSTANCE ) )
    {
        LL_TIM_ClearFlag_UPDATE( EXECUTION_MANAGER_TIMER_INSTANCE );

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
            EXECUTION_MANAGER_TIMER_HANDLE.Init.Prescaler = psc;
            EXECUTION_MANAGER_TIMER_HANDLE.Init.Period    = arr;
            if ( HAL_TIM_Base_Init( &EXECUTION_MANAGER_TIMER_HANDLE ) != HAL_OK )
            {
                Error_Handler();
            }
            // Reset counter to ensure consistent timing
            __HAL_TIM_SET_COUNTER( &EXECUTION_MANAGER_TIMER_HANDLE, 0u );
            // Clear any pending update flag to prevent immediate IRQs
            LL_TIM_ClearFlag_UPDATE( EXECUTION_MANAGER_TIMER_INSTANCE );
            break;
        case ANALOGUE_INPUT_TIMER:
            ANALOGUE_INPUT_TIMER_HANDLE.Init.Prescaler = psc;
            ANALOGUE_INPUT_TIMER_HANDLE.Init.Period    = arr;
            if ( HAL_TIM_Base_Init( &ANALOGUE_INPUT_TIMER_HANDLE ) != HAL_OK )
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
            LL_TIM_DisableCounter( EXECUTION_MANAGER_TIMER_INSTANCE );

            // Clear any pending update flag
            LL_TIM_ClearFlag_UPDATE( EXECUTION_MANAGER_TIMER_INSTANCE );

            // Enable update interrupt
            LL_TIM_EnableIT_UPDATE( EXECUTION_MANAGER_TIMER_INSTANCE );

            // Enable counter
            LL_TIM_EnableCounter( EXECUTION_MANAGER_TIMER_INSTANCE );
            break;
        case ANALOGUE_INPUT_TIMER:
            HAL_TIM_Base_Start( &ANALOGUE_INPUT_TIMER_HANDLE );
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
            LL_TIM_DisableIT_UPDATE( EXECUTION_MANAGER_TIMER_INSTANCE );

            // Stop the counter
            LL_TIM_DisableCounter( EXECUTION_MANAGER_TIMER_INSTANCE );

            // Clear any pending update flag (important)
            LL_TIM_ClearFlag_UPDATE( EXECUTION_MANAGER_TIMER_INSTANCE );
            break;
        case ANALOGUE_INPUT_TIMER:
            HAL_TIM_Base_Stop( &ANALOGUE_INPUT_TIMER_HANDLE );
            break;
        default:
            break;
    }
#endif
}
