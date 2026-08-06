/******************************************************************************
 *  File:       exec_analogue_output.h
 *  Author:     Angus Corr
 *  Created:    25-Mar-2026
 *
 *  Description:
 *      Execution-layer control for the external analogue-output DAC and its
 *      board-level output-enable signal.
 *
 *  Notes:
 *      Configure, start, and stop are separate lifecycle operations. Voltage
 *      writes are accepted only while the module is configured and started.
 ******************************************************************************/

#ifndef EXEC_ANALOGUE_OUTPUT_H
#define EXEC_ANALOGUE_OUTPUT_H

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
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

/**
 * @brief Configure the DAC module and program initial DAC registers.
 *
 * @param use_external_vref
 *     If true, configure the DAC to use the external buffered VREF pin.
 *     If false, configure the DAC to use VDD as the reference.
 *
 * If the module is already configured but stopped, its dedicated SPI runtime
 * is stopped before the new configuration is applied. Configuration is
 * rejected while the external output path is started.
 *
 * @return true if AO_EN was held disabled and SPI/DAC configuration was queued.
 * @return false if the module is started or any LogicExpander or SPI operation fails.
 */
bool EXEC_ANALOGUE_OUTPUT_Configure( bool use_external_vref );

/**
 * @brief Enable the configured external analogue-output path.
 *
 * @return true if DAC configuration transmission is complete and the AO_EN
 *         LogicExpander commit is accepted.
 * @return false if the module is not configured, is already started, the DAC
 *         transmission is incomplete, or the LogicExpander operation fails.
 */
bool EXEC_ANALOGUE_OUTPUT_Start( void );

/**
 * @brief Disable the external analogue-output path and preload 0 V outputs.
 *
 * AO_EN is disabled before zero-value DAC frames are queued for channels 0-5.
 * On success, the DAC and its SPI channel remain configured so the output path
 * can be started again without reconfiguration. Start waits for these frames
 * to finish before re-enabling AO_EN.
 *
 * @return true if the AO_EN disable commit is accepted.
 * @return false if the module is not configured or started, the LogicExpander
 *         operation fails, or the safe DAC frames cannot be queued. A safe-frame
 *         failure leaves the module unconfigured and requiring reconfiguration.
 */
bool EXEC_ANALOGUE_OUTPUT_Stop( void );

/**
 * @brief Return whether the analogue output module has been configured.
 *
 * Useful for callers to know if EXEC_ANALOGUE_OUTPUT_Configure() has
 * previously been called successfully.
 */
bool EXEC_ANALOGUE_OUTPUT_Is_Configured( void );

/**
 * @brief Return whether the external analogue-output start command was accepted.
 */
bool EXEC_ANALOGUE_OUTPUT_Is_Started( void );

/**
 * @brief Write a voltage to a single DAC output channel.
 *
 * Accepts a voltage in the range 0V to 20V, clamps it to the valid input range,
 * scales it to the DAC's 0-5V output range, converts it to a 12-bit DAC code,
 * and transmits a write command to the MCP48 DAC via SPI4.
 *
 * Input voltage scaling and clamping:
 * - Input range: 0V to 20V (nominally full scale at 20V)
 * - Values below 0V are clamped to 0V
 * - Values above 20V are clamped to 20V
 * - Scaled to DAC output range: 0V to 5V
 * - DAC code formula: code = (clamped_voltage / 20.0) * 4095
 *
 * Only channels 0-5 are functional. Attempts to write to channels 6-7 will
 * fail with false return code because those channels are disabled (configured
 * in open-circuit mode).
 *
 * The module must be configured and started before this function is called.
 *
 * @param channel
 *     The DAC output channel number (0-5 for active channels, 6-7 disabled).
 *
 * @param input_voltage_v
 *     The desired output voltage in volts (0V to 20V, clamped and scaled).
 *
 * @return
 *     true if the voltage write was accepted and queued to SPI for transmission.
 *     false if the module is not initialized, the channel is invalid (>= 6),
 *     or SPI transmission failed.
 */
bool EXEC_ANALOGUE_OUTPUT_Write_Voltage( uint8_t channel, float input_voltage_v );

#ifdef __cplusplus
}
#endif

#endif /* EXEC_ANALOGUE_OUTPUT_H */
