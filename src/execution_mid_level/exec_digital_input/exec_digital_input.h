/******************************************************************************
 *  File:       exec_digital_input.h
 *  Author:     Coen Pasitchnyj
 *  Created:    6-April-2026
 *
 *  Description:
 *      Public interface for execution-time digital input handling. This
 *      module exposes configuration and read functions used by the execution
 *      manager to obtain boolean digital input values during a test run.
 *
 *  Notes:
 *      Intended for use by the execution subsystem rather than as a general-
 *      purpose digital input interface. Depends on hw_gpio for low-level GPIO
 *      control.
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
#include "hw_gpio.h"
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
