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
        TEST_SCHEDULER_Process_From_ISR();
    }
#endif
}

void HW_TIMER_Start_Test_Scheduling_Timer( void )
{
#ifdef TEST_BUILD
#else
    // Ensure counter is stopped while configuring
    LL_TIM_DisableCounter( TIM2 );

    // Clear any pending update flag
    LL_TIM_ClearFlag_UPDATE( TIM2 );

    // Enable update interrupt
    LL_TIM_EnableIT_UPDATE( TIM2 );

    // Enable counter
    LL_TIM_EnableCounter( TIM2 );
#endif
}

void HW_TIMER_Stop_Test_Scheduling_Timer( void )
{
#ifdef TEST_BUILD
#else
    // Disable update interrupt first (prevents new IRQs)
    LL_TIM_DisableIT_UPDATE( TIM2 );

    // Stop the counter
    LL_TIM_DisableCounter( TIM2 );

    // Clear any pending update flag (important)
    LL_TIM_ClearFlag_UPDATE( TIM2 );
#endif
}
