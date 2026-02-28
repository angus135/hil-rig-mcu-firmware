/******************************************************************************
 *  File:       test_scheduler.h
 *  Author:     Angus Corr
 *  Created:    20-Dec-2025
 *
 *  Description:
 *      Public interface for the Background module.
 *
 *  Notes:
 *      None
 ******************************************************************************/

#ifndef TEST_SCHEDULER_H
#define TEST_SCHEDULER_H

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

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

/**
 * @brief Starts the Test Scheduler
 *
 */
void TEST_SCHEDULER_Start( void );

/**
 * @brief Stops the Test Scheduler
 *
 */
void TEST_SCHEDULER_Stop( void );

/**
 * @brief Test Scheduler Initialization
 *
 * Initialises the test schedular based on the selected frequency mode.
 */
void TEST_SCHEDULER_Init( void );

#ifdef __cplusplus
}
#endif

#endif /* TEST_SCHEDULER_H */
