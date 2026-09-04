/******************************************************************************
 *  File:       execution_manager_isr.h
 *  Author:     Angus Corr
 *  Created:    31-Jul-2026
 *
 *  Description:
 *      Narrow integration interface between the execution timer ISR and the
 *      Execution Manager.
 *
 *  Notes:
 *      This is not a task-context lifecycle API.
 ******************************************************************************/

#ifndef EXECUTION_MANAGER_ISR_H
#define EXECUTION_MANAGER_ISR_H

#ifdef __cplusplus
extern "C"
{
#endif

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

/**
 * @brief Processes one execution tick from the execution timer ISR.
 */
void EXECUTION_MANAGER_Process_From_ISR( void );

#ifdef __cplusplus
}
#endif

#endif /* EXECUTION_MANAGER_ISR_H */
