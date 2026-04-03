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

// Enum for the different forms of post processing to be applied to the analogue measurements
typedef enum AnalogueInputProccess_T
{
    ANALOGUE_INPUT_PROCESS_MEAN,
    ANALOGUE_INPUT_PROCESS_MEDIAN,
} AnalogueInputProccess_T;

// Configuration struct containing all the configuration information for the analogue inputs
typedef struct AnalogueInputConfiguration_T
{
    ADCSampleRates_T adc_sample_rate;  // What rate will the ADC be sampling each channel at?
    AnalogueInputProccess_T process; // What process will be applied to samples?
    uint8_t          samples_taken; // How many samples will the process be applied to?
    ADCMeasurement_T channels_enabled; // Which channels are enabled? 0 for false, otherwise true.
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
 * @param source - source to poll from
 *
 * Returns UINT16_MAX if there is a problem in retrieving the selected source adc value.
 *
 */
void EXEC_ANALOGUE_INPUT_Read_Analogue_Inputs( void );


#ifdef __cplusplus
}
#endif

#endif /* EXEC_ANALOGUE_INPUT_H */
