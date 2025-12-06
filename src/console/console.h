/******************************************************************************
 *  File:       console.h
 *  Author:     Angus Corr
 *  Created:    6-Dec-2025
 *
 *  Description:
 *      Public interface for the Console module.
 *
 *  Notes:
 *      None
 ******************************************************************************/

#ifndef CONSOLE_H
#define CONSOLE_H

#ifdef __cplusplus
extern "C"
{
#endif

/*------------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include <stdint.h>
#include <stdbool.h>

/*------------------------------------------------------------------------------
 *  Public Defines / Macros
 *------------------------------------------------------------------------------
 */

/*------------------------------------------------------------------------------
 *  Public Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

/*------------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

/**
 * @brief Console Task
 *
 * The FreeRTOS task that runs all the console related logic
 */
void CONSOLE_Task(void* pvParameters);

#ifdef __cplusplus
}
#endif

#endif /* CONSOLE_H */
