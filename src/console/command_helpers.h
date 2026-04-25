/******************************************************************************
 *  File:       command_helpers.h
 *  Author:     Angus Corr
 *  Created:    25-Apr-2026
 *
 *  Description:
 *      Header file for declaration of helper functions for console
 *
 *  Notes:
 *      <Public assumptions, required initialisation order, dependencies, etc.>
 ******************************************************************************/

#ifndef COMMAND_HELPERS_H
#define COMMAND_HELPERS_H

#ifdef __cplusplus
extern "C"
{
#endif

#include "global_config.h"
#if GLOBAL_CONFIG__CONSOLE_ENABLED

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include <stdint.h>
#include <stdbool.h>
// Add any needed standard or project-specific includes here

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

void CONSOLE_SPI_Loopback_Print_Usage( void );

void CONSOLE_SPI_Loopback_Config( uint16_t argc, char* argv[] );

void CONSOLE_SPI_Loopback_Apply( uint16_t argc, char* argv[] );

void CONSOLE_SPI_Loopback_Load( uint16_t argc, char* argv[] );

void CONSOLE_SPI_Loopback_Clear( void );

void CONSOLE_SPI_Loopback_Status( void );

void CONSOLE_SPI_Loopback_Run( uint16_t argc, char* argv[] );

#endif

#ifdef __cplusplus
}
#endif

#endif /* COMMAND_HELPERS_H */
