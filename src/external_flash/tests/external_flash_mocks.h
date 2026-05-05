/******************************************************************************
 *  File:       external_flash_mocks.h
 *  Author:     Callum Rafferty
 *  Created:    5-May-2026
 *
 *  Description:
 *      Mock declarations for unit testing the external_flash module.
 *
 *  Notes:
 *      The test file provides gmock-backed shims for the hw_nand dependency.
 *      external_flash should be tested at the NAND-driver boundary because its
 *      production responsibility is storage policy, not QSPI or STM32 HAL
 *      behaviour.
 ******************************************************************************/

#ifndef EXTERNAL_FLASH_MOCKS_H
#define EXTERNAL_FLASH_MOCKS_H

#ifdef __cplusplus
extern "C"
{
#endif
// NOLINTBEGIN

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include "hw_nand.h"

#include <stdbool.h>
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

// NOLINTEND

#ifdef __cplusplus
}
#endif

#endif /* EXTERNAL_FLASH_MOCKS_H */
