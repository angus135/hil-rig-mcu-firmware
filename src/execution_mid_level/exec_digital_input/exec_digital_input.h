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
#include <stdbool.h>

/**-----------------------------------------------------------------------------
 *  Public Defines / Macros
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Public Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

typedef enum DigitalInputMode_T
{
    DIGITAL_INPUT_MODE_DISABLED,
    DIGITAL_INPUT_MODE_3V3,
    DIGITAL_INPUT_MODE_5V,
    DIGITAL_INPUT_MODE_12V,
    DIGITAL_INPUT_MODE_24V
} DigitalInputMode_T;

typedef struct DigitalInputChannelConfig_T
{
    DigitalInputMode_T channel_0_mode;
    DigitalInputMode_T channel_1_mode;
    DigitalInputMode_T channel_2_mode;
    DigitalInputMode_T channel_3_mode;
    DigitalInputMode_T channel_4_mode;
    DigitalInputMode_T channel_5_mode;
    DigitalInputMode_T channel_6_mode;
    DigitalInputMode_T channel_7_mode;
    DigitalInputMode_T channel_8_mode;
    DigitalInputMode_T channel_9_mode;

} DigitalInputChannelConfig_T;
/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

/**
 * @brief Configures digital input channel modes for execution-time sampling.
 *
 * @param channel_config  Pointer to per-channel mode configuration.
 *
 * This function stores which digital input channels are enabled or disabled
 * based on the provided mode for each channel.
 */
void EXEC_DigitalInput_Configure( const DigitalInputChannelConfig_T* channel_config );

/**
 * @brief Samples all configured digital inputs.
 *
 * @param dest_addr  Pointer to destination for the masked input word.
 *
 * This function reads the digital input port state and writes back a masked
 * result containing only channels currently enabled by configuration.
 */
void EXEC_DigitalInput_SampleAll( uint32_t* dest_addr );

#ifdef __cplusplus
}
#endif

#endif /* EXEC_DIGITAL_INPUT_H */
