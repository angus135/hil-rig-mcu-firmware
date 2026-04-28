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
#include "execution_manager.h"
#endif

#include <stdbool.h>
#include "hw_timer.h"
#include "hw_spi.h"
#include <stdint.h>

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */

/* Execution Timer Defines*/
#define EXECUTION_MANAGER_TIMER_INSTANCE TIM4
#define EXECUTION_MANAGER_TIMER_IRQ_HANDLER TIM4_IRQHandler
#define EXECUTION_MANAGER_TIMER_HANDLE htim4

#define ANALOGUE_INPUT_TIMER_HANDLE htim3

/* SPI Channel 0 Timer Defines*/
#define SPI_CHANNEL_0_TIMER_INSTANCE TIM11
#define SPI_CHANNEL_0_TIMER_IRQ_HANDLER TIM1_TRG_COM_TIM11_IRQHandler
#define SPI_CHANNEL_0_TIMER_HANDLE htim11

/* SPI Channel 1 Timer Defines*/
#define SPI_CHANNEL_1_TIMER_INSTANCE TIM6
#define SPI_CHANNEL_1_TIMER_IRQ_HANDLER TIM6_DAC_IRQHandler
#define SPI_CHANNEL_1_TIMER_HANDLE htim6

/* SPI DAC Timer Defines*/
#define SPI_DAC_TIMER_INSTANCE TIM7
#define SPI_DAC_TIMER_IRQ_HANDLER TIM7_IRQHandler
#define SPI_DAC_TIMER_HANDLE htim7
#define PWM_CAPTURE_TIMER_CH1_HANDLE htim2
#define PWM_CAPTURE_TIMER_CH1_PRIMARY_CHANNEL TIM_CHANNEL_1
#define PWM_CAPTURE_TIMER_CH1_SECONDARY_CHANNEL TIM_CHANNEL_2

#define PWM_CAPTURE_TIMER_CH2_HANDLE htim5
#define PWM_CAPTURE_TIMER_CH2_PRIMARY_CHANNEL TIM_CHANNEL_1
#define PWM_CAPTURE_TIMER_CH2_SECONDARY_CHANNEL TIM_CHANNEL_2

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

void SPI_CHANNEL_0_TIMER_IRQ_HANDLER( void )
{
#ifdef TEST_BUILD
#else
    if ( LL_TIM_IsActiveFlag_UPDATE( SPI_CHANNEL_0_TIMER_INSTANCE )
         && LL_TIM_IsEnabledIT_UPDATE( SPI_CHANNEL_0_TIMER_INSTANCE ) )
    {
        LL_TIM_ClearFlag_UPDATE( SPI_CHANNEL_0_TIMER_INSTANCE );
        LL_TIM_DisableIT_UPDATE( SPI_CHANNEL_0_TIMER_INSTANCE );

        // Running Process
        HW_SPI_Timer_Callback_From_ISR( SPI_CHANNEL_0 );
    }
#endif
}

void SPI_CHANNEL_1_TIMER_IRQ_HANDLER( void )
{
#ifdef TEST_BUILD
#else
    if ( LL_TIM_IsActiveFlag_UPDATE( SPI_CHANNEL_1_TIMER_INSTANCE )
         && LL_TIM_IsEnabledIT_UPDATE( SPI_CHANNEL_1_TIMER_INSTANCE ) )
    {
        LL_TIM_ClearFlag_UPDATE( SPI_CHANNEL_1_TIMER_INSTANCE );
        LL_TIM_DisableIT_UPDATE( SPI_CHANNEL_1_TIMER_INSTANCE );

        // Running Process
        HW_SPI_Timer_Callback_From_ISR( SPI_CHANNEL_1 );
    }
#endif
}
void SPI_DAC_TIMER_IRQ_HANDLER( void )
{
#ifdef TEST_BUILD
#else
    if ( LL_TIM_IsActiveFlag_UPDATE( SPI_DAC_TIMER_INSTANCE )
         && LL_TIM_IsEnabledIT_UPDATE( SPI_DAC_TIMER_INSTANCE ) )
    {
        LL_TIM_ClearFlag_UPDATE( SPI_DAC_TIMER_INSTANCE );
        LL_TIM_DisableIT_UPDATE( SPI_DAC_TIMER_INSTANCE );

        // Running Process
        HW_SPI_Timer_Callback_From_ISR( SPI_DAC );
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
        case SPI_CHANNEL_0_TIMER:
            HW_TIMER_Stop_Timer( SPI_CHANNEL_0_TIMER );
            SPI_CHANNEL_0_TIMER_HANDLE.Init.Prescaler = psc;
            SPI_CHANNEL_0_TIMER_HANDLE.Init.Period    = arr;
            if ( HAL_TIM_Base_Init( &SPI_CHANNEL_0_TIMER_HANDLE ) != HAL_OK )
            {
                Error_Handler();
            }
            // Reset counter to ensure consistent timing
            __HAL_TIM_SET_COUNTER( &SPI_CHANNEL_0_TIMER_HANDLE, 0u );
            // Clear any pending update flag to prevent immediate IRQs
            LL_TIM_ClearFlag_UPDATE( SPI_CHANNEL_0_TIMER_INSTANCE );
            break;
        case SPI_CHANNEL_1_TIMER:
            HW_TIMER_Stop_Timer( SPI_CHANNEL_1_TIMER );
            SPI_CHANNEL_1_TIMER_HANDLE.Init.Prescaler = psc;
            SPI_CHANNEL_1_TIMER_HANDLE.Init.Period    = arr;
            if ( HAL_TIM_Base_Init( &SPI_CHANNEL_1_TIMER_HANDLE ) != HAL_OK )
            {
                Error_Handler();
            }
            // Reset counter to ensure consistent timing
            __HAL_TIM_SET_COUNTER( &SPI_CHANNEL_1_TIMER_HANDLE, 0u );
            // Clear any pending update flag to prevent immediate IRQs
            LL_TIM_ClearFlag_UPDATE( SPI_CHANNEL_1_TIMER_INSTANCE );
            break;
        case SPI_DAC_TIMER:
            HW_TIMER_Stop_Timer( SPI_DAC_TIMER );
            SPI_DAC_TIMER_HANDLE.Init.Prescaler = psc;
            SPI_DAC_TIMER_HANDLE.Init.Period    = arr;
            if ( HAL_TIM_Base_Init( &SPI_DAC_TIMER_HANDLE ) != HAL_OK )
            {
                Error_Handler();
            }
            // Reset counter to ensure consistent timing
            __HAL_TIM_SET_COUNTER( &SPI_DAC_TIMER_HANDLE, 0u );
            // Clear any pending update flag to prevent immediate IRQs
            LL_TIM_ClearFlag_UPDATE( SPI_DAC_TIMER_INSTANCE );
        case PWM_CAPTURE_TIMER_CH1:
            PWM_CAPTURE_TIMER_CH1_HANDLE.Init.Prescaler = psc;
            PWM_CAPTURE_TIMER_CH1_HANDLE.Init.Period    = arr;

            if ( HAL_TIM_IC_Init( &PWM_CAPTURE_TIMER_CH1_HANDLE ) != HAL_OK )
            {
                Error_Handler();
            }

            __HAL_TIM_SET_COUNTER( &PWM_CAPTURE_TIMER_CH1_HANDLE, 0u );
            break;

        case PWM_CAPTURE_TIMER_CH2:
            PWM_CAPTURE_TIMER_CH2_HANDLE.Init.Prescaler = psc;
            PWM_CAPTURE_TIMER_CH2_HANDLE.Init.Period    = arr;

            if ( HAL_TIM_IC_Init( &PWM_CAPTURE_TIMER_CH2_HANDLE ) != HAL_OK )
            {
                Error_Handler();
            }

            __HAL_TIM_SET_COUNTER( &PWM_CAPTURE_TIMER_CH2_HANDLE, 0u );
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
        case SPI_CHANNEL_0_TIMER:
            // Ensure counter is stopped while configuring
            LL_TIM_DisableCounter( SPI_CHANNEL_0_TIMER_INSTANCE );

            LL_TIM_SetCounter( SPI_CHANNEL_0_TIMER_INSTANCE, 0U );

            // Clear any pending update flag
            LL_TIM_ClearFlag_UPDATE( SPI_CHANNEL_0_TIMER_INSTANCE );

            // Enable update interrupt
            LL_TIM_EnableIT_UPDATE( SPI_CHANNEL_0_TIMER_INSTANCE );

            // Enable counter
            LL_TIM_EnableCounter( SPI_CHANNEL_0_TIMER_INSTANCE );
            break;
        case SPI_CHANNEL_1_TIMER:
            // Ensure counter is stopped while configuring
            LL_TIM_DisableCounter( SPI_CHANNEL_1_TIMER_INSTANCE );

            LL_TIM_SetCounter( SPI_CHANNEL_1_TIMER_INSTANCE, 0U );

            // Clear any pending update flag
            LL_TIM_ClearFlag_UPDATE( SPI_CHANNEL_1_TIMER_INSTANCE );

            // Enable update interrupt
            LL_TIM_EnableIT_UPDATE( SPI_CHANNEL_1_TIMER_INSTANCE );

            // Enable counter
            LL_TIM_EnableCounter( SPI_CHANNEL_1_TIMER_INSTANCE );
            break;
        case SPI_DAC_TIMER:
            // Ensure counter is stopped while configuring
            LL_TIM_DisableCounter( SPI_DAC_TIMER_INSTANCE );

            LL_TIM_SetCounter( SPI_DAC_TIMER_INSTANCE, 0U );

            // Clear any pending update flag
            LL_TIM_ClearFlag_UPDATE( SPI_DAC_TIMER_INSTANCE );

            // Enable update interrupt
            LL_TIM_EnableIT_UPDATE( SPI_DAC_TIMER_INSTANCE );

            // Enable counter
            LL_TIM_EnableCounter( SPI_DAC_TIMER_INSTANCE );
            break;
        case ANALOGUE_INPUT_TIMER:
            HAL_TIM_Base_Start( &ANALOGUE_INPUT_TIMER_HANDLE );
            break;
        case PWM_CAPTURE_TIMER_CH1:
            // Reset counter to ensure consistent capture timing
            __HAL_TIM_SET_COUNTER( &PWM_CAPTURE_TIMER_CH1_HANDLE, 0u );
            // Start input capture on both channels for PWM capture
            HAL_TIM_IC_Start( &PWM_CAPTURE_TIMER_CH1_HANDLE,
                              PWM_CAPTURE_TIMER_CH1_PRIMARY_CHANNEL );
            HAL_TIM_IC_Start( &PWM_CAPTURE_TIMER_CH1_HANDLE,
                              PWM_CAPTURE_TIMER_CH1_SECONDARY_CHANNEL );
            break;

        case PWM_CAPTURE_TIMER_CH2:
            // Reset counter to ensure consistent capture timing
            __HAL_TIM_SET_COUNTER( &PWM_CAPTURE_TIMER_CH2_HANDLE, 0u );
            // Start input capture on both channels for PWM capture
            HAL_TIM_IC_Start( &PWM_CAPTURE_TIMER_CH2_HANDLE,
                              PWM_CAPTURE_TIMER_CH2_PRIMARY_CHANNEL );
            HAL_TIM_IC_Start( &PWM_CAPTURE_TIMER_CH2_HANDLE,
                              PWM_CAPTURE_TIMER_CH2_SECONDARY_CHANNEL );
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
        case SPI_CHANNEL_0_TIMER:
            LL_TIM_DisableIT_UPDATE( SPI_CHANNEL_0_TIMER_INSTANCE );
            LL_TIM_DisableCounter( SPI_CHANNEL_0_TIMER_INSTANCE );
            LL_TIM_ClearFlag_UPDATE( SPI_CHANNEL_0_TIMER_INSTANCE );
            break;

        case SPI_CHANNEL_1_TIMER:
            LL_TIM_DisableIT_UPDATE( SPI_CHANNEL_1_TIMER_INSTANCE );
            LL_TIM_DisableCounter( SPI_CHANNEL_1_TIMER_INSTANCE );
            LL_TIM_ClearFlag_UPDATE( SPI_CHANNEL_1_TIMER_INSTANCE );
            break;

        case SPI_DAC_TIMER:
            LL_TIM_DisableIT_UPDATE( SPI_DAC_TIMER_INSTANCE );
            LL_TIM_DisableCounter( SPI_DAC_TIMER_INSTANCE );
            LL_TIM_ClearFlag_UPDATE( SPI_DAC_TIMER_INSTANCE );
        case PWM_CAPTURE_TIMER_CH1:
            // Stop input capture on both channels for PWM capture
            HAL_TIM_IC_Stop( &PWM_CAPTURE_TIMER_CH1_HANDLE, PWM_CAPTURE_TIMER_CH1_PRIMARY_CHANNEL );
            HAL_TIM_IC_Stop( &PWM_CAPTURE_TIMER_CH1_HANDLE,
                             PWM_CAPTURE_TIMER_CH1_SECONDARY_CHANNEL );
            // Reset counter to ensure consistent capture timing when restarted
            __HAL_TIM_SET_COUNTER( &PWM_CAPTURE_TIMER_CH1_HANDLE, 0u );
            break;

        case PWM_CAPTURE_TIMER_CH2:
            // Stop input capture on both channels for PWM capture
            HAL_TIM_IC_Stop( &PWM_CAPTURE_TIMER_CH2_HANDLE, PWM_CAPTURE_TIMER_CH2_PRIMARY_CHANNEL );
            HAL_TIM_IC_Stop( &PWM_CAPTURE_TIMER_CH2_HANDLE,
                             PWM_CAPTURE_TIMER_CH2_SECONDARY_CHANNEL );
            // Reset counter to ensure consistent capture timing when restarted
            __HAL_TIM_SET_COUNTER( &PWM_CAPTURE_TIMER_CH2_HANDLE, 0u );
            break;
        default:
            break;
    }
#endif
}
