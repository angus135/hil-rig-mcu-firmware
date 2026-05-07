/******************************************************************************
 *  File:       exec_analogue_output.h
 *  Author:     Angus Corr
 *  Created:    25-Mar-2026
 *
 *  Description:
 *      <Short description of the module, what it exposes, and how it should be used>
 *
 *  Notes:
 *      <Public assumptions, required initialisation order, dependencies, etc.>
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
 * @return
 *     true on success, false on SPI transmission failure.
 */
bool EXEC_ANALOGUE_OUTPUT_Config( bool use_external_vref );

/**
 * @brief Configure and start the SPI4 hardware channel dedicated to DAC communication.
 *
 * Sets up the SPI4 peripheral with the configuration required by the
 * MCP48CVB28T-20E_ST octal DAC: 8-bit data size, 352K baud rate, MSB first,
 * CPOL low, CPHA 1 edge.
 *
 * This function must be called once during system initialization to prepare
 * SPI4 for use before any DAC operations are performed. In the real project,
 * this setup will be performed by the system/board initialization layer.
 *
 * This function is provided as a separate helper for console testing so that
 * test commands can independently set up the SPI channel without integrating
 * into the full system initialization sequence.
 *
 * The SPI channel is activated for immediate use. After this function returns
 * successfully, the channel is ready to transmit frames to the DAC.
 *
 * @return
 *     true if SPI4 configuration and startup completed successfully.
 *     false if hardware configuration or startup failed.
 */
bool EXEC_ANALOGUE_OUTPUT_SPI_Channel_Setup( void );

/**
 * @brief Return whether the analogue output module has been configured.
 *
 * Useful for console commands to know if `EXEC_ANALOGUE_OUTPUT_Config()` has
 * previously been called successfully.
 */
bool EXEC_ANALOG_OUTPUT_Is_Configured( void );

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
 * The module must be initialized via EXEC_ANALOGUE_OUTPUT_Config() before this
 * function is called. Writing to an uninitialized module returns false.
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
bool EXEC_ANALOG_OUTPUT_Write_Voltage( uint8_t channel, float input_voltage_v );

#ifdef __cplusplus
}
#endif

#endif /* EXEC_ANALOGUE_OUTPUT_H */
