/******************************************************************************
 *  File:       run_state_manager.h
 *  Author:     Angus Corr
 *  Created:    25-Mar-2026
 *
 *  Description:
 *      Public interface for the Background module.
 *
 *  Notes:
 *      None
 ******************************************************************************/

#ifndef RUN_STATE_MANAGER_H
#define RUN_STATE_MANAGER_H

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
typedef enum FrequencyMode_T
{
    FREQUENCY_100HZ,
    FREQUENCY_1KHZ,
    FREQUENCY_10KHZ,
} FrequencyMode_T;

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

/**
 * @brief Starts the Test Scheduler
 *
 */
void RUN_STATE_MANAGER_Start( void );

/**
 * @brief Stops the Test Scheduler
 *
 */
void RUN_STATE_MANAGER_Stop( void );

/**
 * @brief Sets the frequency mode of the test scheduler
 *
 * @param mode - the selected frequency mode
 *
 * Note: currently only supports 100Hz, 1kHz or 10kHz
 *
 */
void RUN_STATE_MANAGER_Set_Frequency_Mode( FrequencyMode_T mode );

/**
 * @brief Test Scheduler Initialization
 *
 * Initialises the test schedular based on the selected frequency mode.
 */
void RUN_STATE_MANAGER_Init( void );

#ifdef __cplusplus
}
#endif

#endif /* RUN_STATE_MANAGER_H */
