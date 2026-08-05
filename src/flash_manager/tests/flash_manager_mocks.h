/******************************************************************************
 *  File:       flash_manager_mocks.h
 *  Author:     Callum Rafferty
 *  Created:    5-Aug-2026
 *
 *  Description:
 *      Mock declarations for unit testing the Flash Manager module.
 *
 *  Notes:
 *      The test sources provide RTOS and external-flash function shims. The
 *      geometry control below configures the EXTERNAL_FLASH_GetInfo() double
 *      shared with the result-buffer tests in the flash_manager_tests target.
 ******************************************************************************/

#ifndef FLASH_MANAGER_MOCKS_H
#define FLASH_MANAGER_MOCKS_H

#ifdef __cplusplus
extern "C"
{
#endif
// NOLINTBEGIN

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include "external_flash.h"

#include <stdint.h>

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

/** Configures the shared EXTERNAL_FLASH_GetInfo() test double. */
void FLASH_MANAGER_TEST_ConfigureExternalFlashInfo( ExternalFlashStatus_T status,
                                                    uint32_t              page_size_bytes );

// NOLINTEND

#ifdef __cplusplus
}
#endif

#endif /* FLASH_MANAGER_MOCKS_H */
