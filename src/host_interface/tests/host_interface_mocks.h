/******************************************************************************
 *  File:       host_interface_mocks.h
 *  Author:     Angus Corr
 *  Created:    27-Jul-2026
 *
 *  Description:
 *      Mock definitions of HAL types and functions for unit testing the host interface module.
 *
 *  Notes:
 *
 ******************************************************************************/

#ifndef HOST_INTERFACE_MOCKS_H
#define HOST_INTERFACE_MOCKS_H

#ifdef __cplusplus
extern "C"
{
#endif
// NOLINTBEGIN

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

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

/**
 * @brief  This function is executed in case of error occurrence.
 * @retval None
 */
void Error_Handler( void );

// NOLINTEND

#ifdef __cplusplus
}
#endif

#endif /* HOST_INTERFACE_MOCKS_H */
