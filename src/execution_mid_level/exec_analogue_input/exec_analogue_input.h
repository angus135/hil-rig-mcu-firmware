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
typedef enum ExecAnalogueInputSampleRate_T
{
    EXEC_ANALOGUE_INPUT_SAMPLE_RATE_100K_HZ = 0U,
    EXEC_ANALOGUE_INPUT_SAMPLE_RATE_50K_HZ,
    EXEC_ANALOGUE_INPUT_SAMPLE_RATE_10K_HZ,
    EXEC_ANALOGUE_INPUT_SAMPLE_RATE_5K_HZ,
    EXEC_ANALOGUE_INPUT_SAMPLE_RATE_1K_HZ,
    EXEC_ANALOGUE_INPUT_SAMPLE_RATE_500_HZ,
} ExecAnalogueInputSampleRate_T;

typedef struct ExecAnalogueInputConfig_T
{
    bool                          is_enabled;
    ExecAnalogueInputSampleRate_T sample_rate;
    bool                          ch_0_is_enabled;
    bool                          ch_1_is_enabled;
} ExecAnalogueInputConfig_T;

// This struct contains pointers to where the Analogue Input voltages should be stored.
// The Execution Manager should set the pointers in this struct to the places where the
// data is to be stored
typedef struct ExecAnalogueInputVoltages_T
{
    uint32_t* channel_0_voltage;
    uint32_t* channel_1_voltage;
} ExecAnalogueInputVoltages_T;

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

/**
 * @brief Configures the execution-layer analogue input path.
 *
 * This function validates the requested analogue input configuration and applies
 * the ADC measurement sample rate required during execution.
 *
 * A disabled configuration stops active acquisition and leaves the module in
 * its disabled state. When enabled, both analogue input channels must currently
 * be selected because dynamic ADC scan selection is not implemented.
 *
 * The ADC hardware setup itself is handled by the hw_adc module. This function
 * only requests the configured measurement frequency and reports whether that
 * configuration succeeded.
 *
 * @param configuration
 *      Analogue input configuration used during execution. This currently
 *      includes the subsystem enable, channel enable flags, and requested ADC
 *      sample rate.
 *
 * @return true
 *      The configuration was accepted and the ADC measurement frequency was
 *      configured successfully.
 *
 * @return false
 *      The configuration is not currently supported, or the ADC measurement
 *      frequency could not be configured.
 */
bool EXEC_ANALOGUE_INPUT_Configure( const ExecAnalogueInputConfig_T* config );

/** @brief Starts ADC DMA acquisition after successful configuration. */
bool EXEC_ANALOGUE_INPUT_Start( void );

/** @brief Stops ADC DMA acquisition while retaining its configuration. */
bool EXEC_ANALOGUE_INPUT_Stop( void );

/** @brief Returns true when the module is configured, including while started. */
bool EXEC_ANALOGUE_INPUT_Is_Configured( void );

/** @brief Returns true only while ADC DMA acquisition is started. */
bool EXEC_ANALOGUE_INPUT_Is_Started( void );

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
void EXEC_ANALOGUE_INPUT_Read_Analogue_Inputs( ExecAnalogueInputVoltages_T voltage_destination );

#ifdef __cplusplus
}
#endif

#endif /* EXEC_ANALOGUE_INPUT_H */
