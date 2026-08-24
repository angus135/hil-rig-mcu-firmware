/******************************************************************************
 *  File:       exec_analogue_input.c
 *  Author:     Angus Corr
 *  Created:    25-Mar-2026
 *
 *  Description:
 *      Execution-layer analogue input handling for the HIL-RIG. This module
 *      configures execution-time analogue input sampling and processes recent
 *      ADC DMA measurements into analogue input values for use by the
 *      execution manager.
 *
 *  Notes:
 *      This module is intended for the time-critical execution path and relies
 *      on hw_adc to perform background ADC acquisition. It does not directly
 *      control ADC hardware, and assumes recent samples are already being
 *      captured through the timer-triggered DMA measurement path.
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include "exec_analogue_input.h"
#include "hw_adc.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */

// Use a power-of-two sample count so averaging can be performed with a bit shift
// instead of integer division. This keeps processing cost low in the execution path.
#define SAMPLES_SHIFT_FACTOR 3
#define SAMPLES_TAKEN ( 1 << SAMPLES_SHIFT_FACTOR )

/**-----------------------------------------------------------------------------
 *  Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */
typedef enum
{
    EXEC_ANALOGUE_INPUT_STATE_DISABLED = 0,
    EXEC_ANALOGUE_INPUT_STATE_CONFIGURED,
    EXEC_ANALOGUE_INPUT_STATE_STARTED,
} ExecAnalogueInputState_T;

/**-----------------------------------------------------------------------------
 *  Public (global) and Extern Variables
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Private (static) Variables
 *------------------------------------------------------------------------------
 */

static ExecAnalogueInputState_T analogue_input_state = EXEC_ANALOGUE_INPUT_STATE_DISABLED;

/**-----------------------------------------------------------------------------
 *  Private (static) Function Prototypes
 *------------------------------------------------------------------------------
 */

static inline uint32_t EXEC_ANALOGUE_INPUT_Convert_ADC_To_Voltage( uint32_t adc_value );

static bool EXEC_ANALOGUE_INPUT_Translate_Sample_Rate( ExecAnalogueInputSampleRate_T exec_rate,
                                                       ADCSampleRates_T*             hw_rate );

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

// TODO: determine how the voltages will be stored
static inline uint32_t EXEC_ANALOGUE_INPUT_Convert_ADC_To_Voltage( uint32_t adc_value )
{
    return adc_value;
}

static bool EXEC_ANALOGUE_INPUT_Translate_Sample_Rate( ExecAnalogueInputSampleRate_T exec_rate,
                                                       ADCSampleRates_T*             hw_rate )
{
    if ( hw_rate == NULL )
    {
        return false;
    }

    switch ( exec_rate )
    {
        case EXEC_ANALOGUE_INPUT_SAMPLE_RATE_100K_HZ:
            *hw_rate = ADC_SAMPLE_RATE_100K_HZ;
            break;

        case EXEC_ANALOGUE_INPUT_SAMPLE_RATE_50K_HZ:
            *hw_rate = ADC_SAMPLE_RATE_50K_HZ;
            break;

        case EXEC_ANALOGUE_INPUT_SAMPLE_RATE_10K_HZ:
            *hw_rate = ADC_SAMPLE_RATE_10K_HZ;
            break;

        case EXEC_ANALOGUE_INPUT_SAMPLE_RATE_5K_HZ:
            *hw_rate = ADC_SAMPLE_RATE_5K_HZ;
            break;

        case EXEC_ANALOGUE_INPUT_SAMPLE_RATE_1K_HZ:
            *hw_rate = ADC_SAMPLE_RATE_1K_HZ;
            break;

        case EXEC_ANALOGUE_INPUT_SAMPLE_RATE_500_HZ:
            *hw_rate = ADC_SAMPLE_RATE_500_HZ;
            break;

        default:
            return false;
    }

    return true;
}

/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */

/**
 * @brief Configures the execution-layer analogue input path.
 *
 * This function validates the requested analogue input configuration and applies
 * the ADC measurement sample rate required during execution.
 *
 * At present, both analogue input channels must be enabled. Dynamic channel
 * enable/disable support has not yet been implemented, so the function returns
 * false if either channel is disabled.
 *
 * The ADC hardware setup itself is handled by the hw_adc module. This function
 * translates the execution-layer sample rate and applies the corresponding ADC
 * measurement frequency without starting DMA acquisition.
 *
 * @param config
 *      Analogue input configuration used during execution. This currently
 *      includes the channel enable flags and requested ADC sample rate.
 *
 * @return true
 *      The configuration was accepted and the ADC measurement frequency was
 *      configured successfully.
 *
 * @return false
 *      The configuration is not currently supported, or the ADC measurement
 *      frequency could not be configured.
 */
bool EXEC_ANALOGUE_INPUT_Configure( const ExecAnalogueInputConfig_T* config )
{
    if ( config == NULL )
    {
        return false;
    }

    if ( !config->is_enabled )
    {
        if ( analogue_input_state == EXEC_ANALOGUE_INPUT_STATE_STARTED )
        {
            if ( !EXEC_ANALOGUE_INPUT_Stop() )
            {
                return false;
            }
        }

        /*
         * hw_adc has no deconfigure operation. The previous sample-rate
         * configuration is retained, but DMA acquisition remains stopped.
         */
        analogue_input_state = EXEC_ANALOGUE_INPUT_STATE_DISABLED;

        return true;
    }

    if ( analogue_input_state == EXEC_ANALOGUE_INPUT_STATE_STARTED )
    {
        return false;
    }

    /*
     * Dynamic channel selection is not supported by the fixed ADC scan and
     * DMA configuration. Both channels must currently be enabled.
     */
    if ( !config->ch_0_is_enabled || !config->ch_1_is_enabled )
    {
        return false;
    }

    ADCSampleRates_T hw_sample_rate;

    if ( !EXEC_ANALOGUE_INPUT_Translate_Sample_Rate( config->sample_rate, &hw_sample_rate ) )
    {
        return false;
    }

    if ( !HW_ADC_Configure_ADC_Measurement_Frequency( hw_sample_rate ) )
    {
        return false;
    }

    analogue_input_state = EXEC_ANALOGUE_INPUT_STATE_CONFIGURED;

    return true;
}

bool EXEC_ANALOGUE_INPUT_Start( void )
{
    if ( analogue_input_state != EXEC_ANALOGUE_INPUT_STATE_CONFIGURED )
    {
        return false;
    }

    if ( !HW_ADC_Start_DMA_Measurements() )
    {
        return false;
    }

    analogue_input_state = EXEC_ANALOGUE_INPUT_STATE_STARTED;

    return true;
}

bool EXEC_ANALOGUE_INPUT_Stop( void )
{
    if ( analogue_input_state != EXEC_ANALOGUE_INPUT_STATE_STARTED )
    {
        return false;
    }

    if ( !HW_ADC_Stop_DMA_Measurements() )
    {
        return false;
    }

    analogue_input_state = EXEC_ANALOGUE_INPUT_STATE_CONFIGURED;

    return true;
}

bool EXEC_ANALOGUE_INPUT_Is_Configured( void )
{
    return analogue_input_state == EXEC_ANALOGUE_INPUT_STATE_CONFIGURED
           || analogue_input_state == EXEC_ANALOGUE_INPUT_STATE_STARTED;
}

bool EXEC_ANALOGUE_INPUT_Is_Started( void )
{
    return analogue_input_state == EXEC_ANALOGUE_INPUT_STATE_STARTED;
}

/**
 * @brief Reads and processes the latest analogue input measurements.
 *
 * This function reads the most recent ADC DMA measurements from hw_adc,
 * averages a fixed power-of-two number of samples, converts the averaged ADC
 * values into the execution-layer voltage representation, and writes the
 * results to the supplied output destinations.
 *
 * The number of samples averaged is controlled by SAMPLES_TAKEN. Since this is
 * a power of two, the average is calculated using a right shift rather than an
 * integer division to reduce execution cost in the time-critical path.
 *
 * This function is intentionally lightweight and performs no pointer,
 * peripheral, DMA, or configuration validation. The caller must ensure that:
 *
 * - voltage_destination.channel_0_voltage is valid
 * - voltage_destination.channel_1_voltage is valid
 * - ADC DMA sampling has already been configured and started
 * - hw_adc contains recent measurements to read
 *
 * @param voltage_destination
 *      Struct containing pointers to the locations where the processed channel
 *      voltage values should be stored.
 */
inline void
EXEC_ANALOGUE_INPUT_Read_Analogue_Inputs( ExecAnalogueInputVoltages_T voltage_destination )
{
    ADCMeasurement_T results[SAMPLES_TAKEN];
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
