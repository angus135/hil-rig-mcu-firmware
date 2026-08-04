/******************************************************************************
 *  File:       result_buffer.h
 *  Author:     Callum Rafferty
 *  Created:    25-Mar-2026
 *
 *  Description:
 *      Public interface for the Result Buffer module.
 *
 *  Notes:
 *      This is currently a placeholder; no result-buffer API is exposed.
 ******************************************************************************/

#ifndef RESULT_BUFFER_H
#define RESULT_BUFFER_H

#ifdef __cplusplus
extern "C"
{
#endif

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include "flash_manager.h"
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
 * @brief Resets all result-buffer ownership and cursor state.
 *
 * Any outstanding lease becomes invalid. Stored bytes are not cleared because
 * they are inaccessible until overwritten and committed again.
 */
void RESULT_BUFFER_Reset( void );

#ifdef __cplusplus
}
#endif

#endif /* RESULT_BUFFER_H */
