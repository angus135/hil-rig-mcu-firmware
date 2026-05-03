/******************************************************************************
 *  File:      console_logic_expander.h
 *  Author:    Coen Pasitchnyj
*  Created:    3-May-2026
 *
 *  Description:
 *      <Short description of the module, what it exposes, and how it should be used>
 *
 *  Notes:
 *      <Public assumptions, required initialisation order, dependencies, etc.>
 ******************************************************************************/

#ifndef CONSOLE_LOGIC_EXPANDER_H
#define CONSOLE_LOGIC_EXPANDER_H

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
#include "logic_expander.h"

/**-----------------------------------------------------------------------------
 *  Public Defines / Macros
 *------------------------------------------------------------------------------
 */

bool CONSOLE_Parse_Expander_Port( const char* arg, LogicExpanderPort_T* port );

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

#endif /* CONSOLE_LOGIC_EXPANDER_H */
