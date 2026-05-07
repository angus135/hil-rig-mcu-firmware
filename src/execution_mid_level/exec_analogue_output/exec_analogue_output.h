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

bool EXEC_ANALOGUE_OUTPUT_SPI_Channel_Setup( void );

/**
 * @brief Return whether the analogue output module has been configured.
 *
 * Useful for console commands to know if `EXEC_ANALOGUE_OUTPUT_Config()` has
 * previously been called successfully.
 */
bool EXEC_ANALOG_OUTPUT_Is_Configured( void );

bool EXEC_ANALOG_OUTPUT_Write_Voltage( uint8_t channel, float input_voltage_v );

#ifdef __cplusplus
}
#endif

#endif /* EXEC_ANALOGUE_OUTPUT_H */
