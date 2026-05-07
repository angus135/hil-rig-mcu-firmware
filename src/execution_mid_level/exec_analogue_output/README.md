# exec_analogue_output
## Overview

`exec_analogue_output` provides the mid-level driver for the MCP48CVB28T-20E_ST DAC.

This module is responsible for:

- Configuring the DAC over SPI using the low-level `hw_spi` driver.
- Initializing channels 0-5 for active outputs.
- Putting channels 6-7 into power-down open-circuit mode.
- Scaling requested 0-20 V input values down to the DAC's 0-5 V output range.


---

## Files

| File                      | Role |
|---------------------------|------|
| `exec_analogue_output.c`        | Public API implementation |
| `exec_analogue_output.h`        | Public API header |


---

## Public API

The public API is declared in `exec_analogue_output.h`. The functions below
describe the intended call order and the responsibilities of each entry point.

| Function | What it does |
|----------|--------------|
| `EXEC_ANALOGUE_OUTPUT_SPI_Channel_Setup()` | Configures and starts the SPI channel used by the DAC. Call this during system startup before attempting any DAC transfers. |
| `EXEC_ANALOGUE_OUTPUT_Config(use_external_vref)` | Programs the DAC's startup registers, selects the reference source, enables channels 0-5, and puts channels 6-7 into open-circuit power-down mode. Marks the module as configured if the SPI transfer succeeds. |
| `EXEC_ANALOG_OUTPUT_Is_Configured()` | Returns whether `EXEC_ANALOGUE_OUTPUT_Config()` has completed successfully. Useful for guarding console commands or higher-level control logic. |
| `EXEC_ANALOG_OUTPUT_Write_Voltage(channel, input_voltage_v)` | Validates the requested channel, clamps the input voltage to 0-20 V, scales it to the DAC's 12-bit range, and sends a write frame to the DAC. Returns false if the module is not configured, the channel is out of range, or the SPI transfer cannot be queued. |

## Usage Notes

- Call `EXEC_ANALOGUE_OUTPUT_SPI_Channel_Setup()` before `EXEC_ANALOGUE_OUTPUT_Config()`.
- Only channels 0-5 are intended for active analogue outputs.
- Channels 6-7 are deliberately disabled and should not be written to.
- Voltage writes are clamped to the 0-20 V input range before scaling.