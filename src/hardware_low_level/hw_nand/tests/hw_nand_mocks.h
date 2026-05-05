/******************************************************************************
 *  File:       hw_nand_mocks.h
 *  Author:     Callum Rafferty
 *  Created:    5-May-2026
 *
 *  Description:
 *      Mock definitions for unit testing hw_nand.
 *
 *  Notes:
 *
 ******************************************************************************/

#ifndef HW_NAND_MOCKS_H
#define HW_NAND_MOCKS_H

#ifdef __cplusplus
extern "C"
{
#endif
// NOLINTBEGIN

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include <stdbool.h>
#include <stdint.h>

#include "hw_qspi.h"

/**-----------------------------------------------------------------------------
 *  Public Defines / Macros
 *------------------------------------------------------------------------------
 */

/*
 * hw_nand unit tests mock the public hw_qspi API. The concrete mock object and
 * function shims live in test_hw_nand.cpp, matching the structure used by the
 * other hardware_low_level tests.
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

#endif /* HW_NAND_MOCKS_H */
