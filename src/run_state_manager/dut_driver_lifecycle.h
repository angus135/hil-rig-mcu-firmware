/******************************************************************************
 *  File:       dut_driver_lifecycle.h
 *  Author:     Callum Rafferty
 *  Created:    26-Aug-2026
 *
 *  Description:
 *      Public interface for coordinated DUT-facing driver lifecycle control.
 *
 *  Notes:
 *      These functions compose the relevant execution-driver APIs for one test
 *      lifecycle. The Run State Manager is their only intended caller. This
 *      module owns no run state, RTOS task, execution timer, Flash Manager
 *      sequencing, or transport operation.
 ******************************************************************************/

#ifndef DUT_DRIVER_LIFECYCLE_H
#define DUT_DRIVER_LIFECYCLE_H

#ifdef __cplusplus
extern "C"
{
#endif

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include <stdbool.h>

/**-----------------------------------------------------------------------------
 *  Public Defines / Macros
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Public Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

/**
 * @brief Applies the active test configuration to all DUT-facing drivers.
 *
 * Implementations must leave drivers stopped. If configuration partially
 * succeeds, all affected drivers must be returned to a safe stopped condition
 * before failure is returned.
 *
 * @returns true if every required driver was configured, otherwise false.
 */
bool DUT_DRIVER_LIFECYCLE_Configure( void );

/**
 * @brief Starts all configured DUT-facing drivers.
 *
 * The Run State Manager starts the execution timer only after this succeeds.
 * If a driver fails to start, every driver already started by this call must be
 * stopped before failure is returned.
 *
 * @returns true if every required driver was started, otherwise false.
 */
bool DUT_DRIVER_LIFECYCLE_Start( void );

/**
 * @brief Stops all DUT-facing drivers after execution has stopped.
 *
 * This operation must be idempotent and must attempt every required stop even
 * if an earlier driver reports a failure.
 */
void DUT_DRIVER_LIFECYCLE_Stop( void );

/**
 * @brief Places DUT-facing drivers into their normal idle condition.
 *
 * This operation must be idempotent. DUT power remains enabled in idle.
 */
void DUT_DRIVER_LIFECYCLE_EnterIdle( void );

/**
 * @brief Places DUT-facing drivers and DUT power into their fault condition.
 *
 * This operation must be idempotent and best-effort: all safety actions must be
 * attempted even if an earlier action fails.
 */
void DUT_DRIVER_LIFECYCLE_EnterFault( void );

#ifdef __cplusplus
}
#endif

#endif /* DUT_DRIVER_LIFECYCLE_H */
