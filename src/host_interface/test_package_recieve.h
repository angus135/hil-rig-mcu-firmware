/******************************************************************************
 *  File:       test_package_recieve.h
 *  Author:     Callum Rafferty
 *  Created:    25-Mar-2026
 *
 *  Description:
 *      Future application-layer conversion of received test packages into the
 *      canonical Flash Manager instruction stream.
 *
 *  Notes:
 *      The existing filename is retained for build compatibility. This module
 *      has no public implementation yet.
 *
 *      Before upload begins, conversion must know the total canonical byte
 *      length and guarantee a complete packed [header][payload] stream with
 *      nondecreasing timestamps, valid peripheral/channel routing, valid
 *      payload schemas, and records no larger than one NAND page.
 ******************************************************************************/

#ifndef TEST_PACKAGE_RECEIVE_H
#define TEST_PACKAGE_RECEIVE_H

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

#ifdef __cplusplus
}
#endif

#endif /* TEST_PACKAGE_RECEIVE_H */
