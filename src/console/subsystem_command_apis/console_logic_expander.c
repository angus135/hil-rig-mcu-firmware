/******************************************************************************
 *  File:       console_logic_expander.c
 *  Author:     Coen Pasitchnyj
 *  Created:    3-May-2026
 *
 *  Description:
 *      Console command API for logic expander functionality.
 *
 *      This module owns logic expander related console command handling.
 *
 *  Notes:
 *      The top level console command handler dispatches to this module for the
 *      "expander" command namespace.
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include "console.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include "console_logic_expander.h"

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Private Variables
 *------------------------------------------------------------------------------
 */


/**-----------------------------------------------------------------------------
 *  Private Function Prototypes
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */



/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */

bool CONSOLE_Parse_Expander_Port( const char* arg, LogicExpanderPort_T* port )
{
    if ( ( arg == NULL ) || ( port == NULL ) )
    {
        return false;
    }

    if ( strcmp( arg, "A" ) == 0 )
    {
        *port = LOGIC_EXPANDER_PORT_A;
        return true;
    }

    if ( strcmp( arg, "B" ) == 0 )
    {
        *port = LOGIC_EXPANDER_PORT_B;
        return true;
    }

    return false;
}
