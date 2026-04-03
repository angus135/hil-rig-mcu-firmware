/******************************************************************************
 *  File:       exec_analogue_input.h
 *  Author:     Angus Corr
 *  Created:    25-Mar-2026
 *
 *  Description:
 *      <Short description of the module, what it exposes, and how it should be used>
 *
 *  Notes:
 *      <Public assumptions, required initialisation order, dependencies, etc.>
 ******************************************************************************/

#ifndef EXEC_ANALOGUE_INPUT_H
#define EXEC_ANALOGUE_INPUT_H

#ifdef __cplusplus
extern "C"
{
#endif

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include "hw_adc.h"
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

// Configuration struct containing all the configuration information for the analogue inputs
typedef struct AnalogueInputConfiguration_T
{
    ADCSampleRates_T adc_sample_rate;   // What rate will the ADC be sampling each channel at?
    ADCMeasurement_T channels_enabled;  // Which channels are enabled? 0 for false, otherwise true.
} AnalogueInputConfiguration_T;

// This struct contains pointers to where the Analogue Input voltages should be stored.
// The Execution Manager should set the pointers in this struct to the places where the
// data is to be stored
typedef struct AnalogueInputVoltages_T
{
    uint32_t* channel_0_voltage;
    uint32_t* channel_1_voltage;
} AnalogueInputVoltages_T;

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
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
bool EXEC_ANALOGUE_INPUT_Configure_Analogue_Inputs( AnalogueInputConfiguration_T configuration );

/**
 * @brief Reads Analogue Inputs
 *
 * @param voltage_destination - struct containing the pointers to where the voltages should be
 * stored
 *
 *
 */
void EXEC_ANALOGUE_INPUT_Read_Analogue_Inputs( AnalogueInputVoltages_T voltage_destination );

#ifdef __cplusplus
}
#endif

#endif /* EXEC_ANALOGUE_INPUT_H */
