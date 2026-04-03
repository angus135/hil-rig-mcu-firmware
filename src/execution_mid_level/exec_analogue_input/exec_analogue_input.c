/******************************************************************************
 *  File:       exec_analogue_input.c
 *  Author:     Angus Corr
 *  Created:    25-Mar-2026
 *
 *  Description:
 *      <Short description of the module's purpose and responsibilities>
 *
 *  Notes:
 *      <Any design notes, dependencies, or assumptions go here>
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include "exec_analogue_input.h"
#include <stdint.h>
#include <stdbool.h>

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Public (global) and Extern Variables
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Private (static) Variables
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Private (static) Function Prototypes
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

/**
 * @brief Configures the Analogue Inputs to run
 *
 * @param configuration - a struct containing all the configuration information for during execution
 *
 * @returns bool - returns true if configuration is valid, returns false otherwise
 *
 * Returns UINT16_MAX if there is a problem in retrieving the selected source adc value.
 *
 */
bool EXEC_ANALOGUE_INPUT_Configure_Analogue_Inputs( AnalogueInputConfiguration_T configuration )
{
}

/**
 * @brief Reads Analogue Inputs
 *
 * @param source - source to poll from
 *
 * Returns UINT16_MAX if there is a problem in retrieving the selected source adc value.
 *
 */
void EXEC_ANALOGUE_INPUT_Read_Analogue_Inputs( void )
{
    
}
