/******************************************************************************
 *  File:       global_config.h
 *  Author:     Angus Corr
 *  Created:    28-Feb-2026
 *
 *  Description:
 *      This header lists a set of global configurations that can be set, enabled
 *      or disabled.
 *
 ******************************************************************************/

#ifndef GLOBAL_CONFIG_H
#define GLOBAL_CONFIG_H

#ifdef __cplusplus
extern "C"
{
#endif

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Public Defines / Macros
 *------------------------------------------------------------------------------
 */
#define GLOBAL_CONFIG__CONSOLE_ENABLED ( 1 )  // Enables or disables the console
#define GLOBAL_CONFIG__CONSOLE_FLASH_TEST_HARNESS_ENABLED ( 0 )  // Enables or disables heavy flash bringup test buffers (2KB)
#define GLOBAL_CONFIG__CONSOLE_UART_BLAST_ENABLED         ( 0 )  // Enables or disables UART loopback random blast buffers (2KB)

/**-----------------------------------------------------------------------------
 *  Public Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

#ifdef __cplusplus
}
#endif

#endif /* GLOBAL_CONFIG_H */
