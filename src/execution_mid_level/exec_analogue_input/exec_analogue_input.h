/******************************************************************************
 *  File:       exec_analogue_input.h
 *  Author:     Angus Corr
 *  Created:    25-Mar-2026
 *
 *  Description:
 *      Public interface for execution-time analogue input handling. This
 *      module exposes configuration and read functions used by the execution
 *      manager to obtain processed analogue input values during a test run.
 *
 *  Notes:
 *      Intended for use by the execution subsystem rather than as a general-
 *      purpose ADC interface. Depends on hw_adc for low-level measurement
 *      acquisition. The execution manager is expected to configure this module
 *      before use and provide destinations for storing the resulting analogue
 *      input values.
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
    ADCSampleRates_T adc_sample_rate;  // What rate will the ADC be sampling each channel at?
    bool             ch_0_is_enabled;
    bool             ch_1_is_enabled;
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
 * Note: voltage destinations must be valid as this function does not validate pointers
 * Additionally, this function does not contain any validation of the DMA buffers, DMA peripheral or
 * ADC peripheral, and mearly acts as a low cost way of accessing the DMA buffer and providing
 * conversion. If validation of the peripherals is to be done it will need to be done somewhere
 * outside of this
 *
 * @param voltage_destination - struct containing the pointers to where the voltages should be
 * stored
 *
 */
void EXEC_ANALOGUE_INPUT_Read_Analogue_Inputs( AnalogueInputVoltages_T voltage_destination );

#ifdef __cplusplus
}
#endif

#endif /* EXEC_ANALOGUE_INPUT_H */
