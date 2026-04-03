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
#include "hw_adc.h"
#include <stdint.h>
#include <stdbool.h>

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */

#define SAMPLES_SHIFT_FACTOR 3
#define SAMPLES_TAKEN ( 1 << SAMPLES_SHIFT_FACTOR )

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
ADCMeasurement_T results[SAMPLES_TAKEN] = { 0 };

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

// TODO: determine how the voltages will be stored
inline uint32_t EXEC_ANALOGUE_INPUT_Convert_ADC_To_Voltage( uint32_t adc_value )
{
    return adc_value;
}

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
    bool status = false;
    // TODO: Update this when can dynamically adjust the number of channels
    if ( configuration.channels_enabled.ch_0 == 0 || configuration.channels_enabled.ch_1 == 0 )
    {
        return false;
    }

    // Configuring measurement frequency
    status = HW_ADC_Configure_ADC_Measurement_Frequency( configuration.adc_sample_rate );
    if ( !status )
    {
        return status;
    }

    return status;
}

/**
 * @brief Reads Analogue Inputs
 *
 * @param voltage_destination - struct containing the pointers to where the voltages should be
 * stored
 *
 */
inline void EXEC_ANALOGUE_INPUT_Read_Analogue_Inputs( AnalogueInputVoltages_T voltage_destination )
{
    // Get the measurements from DMA buffer
    HW_ADC_Read_DMA_Measurements( results, SAMPLES_TAKEN );

    // Taking the average and storing in destination
    uint32_t adc_channel_0 = 0;
    uint32_t adc_channel_1 = 0;
    for ( uint32_t i = 0; i < SAMPLES_TAKEN; i++ )
    {
        adc_channel_0 += results[i].ch_0;
        adc_channel_1 += results[i].ch_1;
    }
    *voltage_destination.channel_0_voltage =
        EXEC_ANALOGUE_INPUT_Convert_ADC_To_Voltage( adc_channel_0 >> SAMPLES_SHIFT_FACTOR );
    *voltage_destination.channel_1_voltage =
        EXEC_ANALOGUE_INPUT_Convert_ADC_To_Voltage( adc_channel_1 >> SAMPLES_SHIFT_FACTOR );
}
