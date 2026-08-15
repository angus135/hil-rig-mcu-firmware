/******************************************************************************
 *  File:       hw_timer.h
 *  Author:     Angus Corr
 *  Created:    18-Dec-2025
 *
 *  Description:
 *      <Short description of the module, what it exposes, and how it should be used>
 *
 *  Notes:
 *      <Public assumptions, required initialisation order, dependencies, etc.>
 ******************************************************************************/

#ifndef HW_TIMER_H
#define HW_TIMER_H

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

/*
 * PWM_CAPTURE_TIMER_CH1 maps to PWM capture logical channel 1 (TIM2)
 * PWM_CAPTURE_TIMER_CH2 maps to PWM capture logical channel 2 (TIM5)
 *
 * This does NOT correspond to TIM_CHANNEL_1 / TIM_CHANNEL_2.
 */
typedef enum Timer_T
{
    EXECUTION_MANAGER_TIMER,
    ANALOGUE_INPUT_TIMER,
    SPI_CHANNEL_0_TIMER,
    SPI_CHANNEL_1_TIMER,
    SPI_DAC_TIMER,
    PWM_CAPTURE_TIMER_CH1,
    PWM_CAPTURE_TIMER_CH2,

} Timer_T;

/**
 * @brief ISR callback temporarily served by the execution timer.
 *
 * The callback runs directly inside TIM4_IRQHandler. It must obey the same
 * timing, interrupt-priority, and FreeRTOS FromISR restrictions as the
 * production Execution Manager callback.
 */
typedef void ( *HW_TIMER_ExecutionCallback_T )( void );

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

/**
 * @brief Configures the specified timer.
 *
 * @param timer - the timer to configure
 * @param psc - Prescalar
 * @param arr - AutoReload Register
 *
 * Configures the timer with a specified prescalar and autoreload register
value.
 */
void HW_TIMER_Configure_Timer( Timer_T timer, uint32_t psc, uint32_t arr );

/**
 * @brief Starts the specified timer
 *
 * @param timer - the timer to configure
 *
 */
void HW_TIMER_Start_Timer( Timer_T timer );

/**
 * @brief Stops the specified timer
 *
 * @param timer - the timer to configure
 *
 */
void HW_TIMER_Stop_Timer( Timer_T timer );

/**
 * @brief Overrides or restores the execution-timer ISR callback.
 *
 * @param callback Callback to invoke from TIM4_IRQHandler, or NULL to restore
 *        EXECUTION_MANAGER_Process_From_ISR().
 *
 * @warning The execution timer must be stopped before changing this callback.
 *          The caller owns that sequencing; this low-level API does not stop
 *          the timer or synchronise with an active ISR.
 * @warning The callback executes in interrupt context and must never block,
 *          access NAND, use task-context RTOS APIs, or retain Flash Manager
 *          instruction/result views after their documented lifetime.
 * @note This override exists to support controlled hardware bring-up. Normal
 *       firmware should leave the callback set to NULL.
 */
void HW_TIMER_Set_Execution_Callback( HW_TIMER_ExecutionCallback_T callback );

/**
 * @brief Gets the clock frequency of the specified timer in Hz.
 *
 * @param timer The timer for which to get the clock frequency.
 * @return uint32_t The clock frequency in Hz.
 */
uint32_t HW_TIMER_Get_Clock_Hz( Timer_T timer );

#ifdef __cplusplus
}
#endif

#endif /* HW_TIMER_H */
