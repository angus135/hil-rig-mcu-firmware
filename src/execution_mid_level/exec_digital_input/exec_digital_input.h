/******************************************************************************
 *  File:       exec_digital_input.h
 *  Author:     Angus Corr
 *  Created:    25-Mar-2026
 *
 *  Description:
 *      <Short description of the module, what it exposes, and how it should be used>
 *
 *  Notes:
 *      <Public assumptions, required initialisation order, dependencies, etc.>
 ******************************************************************************/

#ifndef EXEC_DIGITAL_INPUT_H
#define EXEC_DIGITAL_INPUT_H

#ifdef __cplusplus
extern "C"
{
#endif

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */
#include "../../hardware_low_level/hw_gpio/hw_gpio.h"
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

typedef enum DIGITAL_INPUT_MODE_T
{
    DIGITAL_INPUT_MODE_3V3,
    DIGITAL_INPUT_MODE_5V,
    DIGITAL_INPUT_MODE_12V,
    DIGITAL_INPUT_MODE_24V
} DIGITAL_INPUT_MODE_T;

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

void EXEC_DigitalInput_Configure( const DIGITAL_INPUT_MODE_T* modes, uint8_t num_channels );

void EXEC_DigitalInput_SampleAll( bool* dest_buffer );

bool EXEC_DigitalInput_Sample( DIGITAL_INPUT_T input );

#ifdef __cplusplus
}
#endif

#endif /* EXEC_DIGITAL_INPUT_H */
