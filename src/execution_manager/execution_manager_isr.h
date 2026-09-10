/******************************************************************************
 *  File:       execution_manager_isr.h
 *
 *  Description:
 *      Narrow execution-tick interface called by the execution timer ISR.
 ******************************************************************************/

#ifndef EXECUTION_MANAGER_ISR_H
#define EXECUTION_MANAGER_ISR_H

#ifdef __cplusplus
extern "C"
{
#endif

#include "execution_manager.h"

/**
 * @brief Processes one execution tick.
 *
 * Tick zero represents configured initial conditions. The first timer
 * interrupt advances to and processes boundary tick one. Future measurement
 * collection runs after that advance and before output dispatch, so every
 * operation in one invocation shares one authoritative boundary timestamp.
 * A terminal result is latched and returned to the timer-owning integration
 * layer.
 *
 * @param[in,out] higher_priority_task_woken Accumulated FreeRTOS ISR wake flag.
 *        The caller initializes it to pdFALSE and passes it to
 *        portYIELD_FROM_ISR() only after this function returns.
 */
ExecutionManagerTickResult_T
EXECUTION_MANAGER_ProcessTickFromISR( BaseType_t* higher_priority_task_woken );

#ifdef __cplusplus
}
#endif

#endif /* EXECUTION_MANAGER_ISR_H */
