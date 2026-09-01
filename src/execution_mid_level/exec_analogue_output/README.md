# exec_analogue_output

## Overview

`exec_analogue_output` owns execution-level control of the MCP48CVB28T DAC and
the board-level `AO_EN` signal. It configures the dedicated DAC SPI channel,
programs the DAC reference and channel registers, controls `AO_EN` through port
B bit 0 of `LOGIC_EXPANDER_I2C_AO`, and prepares 0-20 V requests for SPI.

Channels 0-5 are available for analogue outputs. Channels 6-7 are placed in
open-circuit power-down mode during configuration.

## Lifecycle

```text
Disabled --Configure(enabled)--> Configuring --> Configured --Start--> Started
   ^                                              ^                    |
   +-------------Configure(disabled)--------------+-------Stop---------+
```

- `Configure(enabled)` holds `AO_EN` low, configures and temporarily starts
  SPI, then sends the DAC startup packet. The packet initializes every DAC data
  register to 0 V. SPI is stopped when transmission completes.
- `Start()` starts SPI and then asserts `AO_EN`.
- `Stop()` waits for pending transmission to complete, deasserts `AO_EN`, and
  stops SPI. It retains the DAC configuration and last programmed values.
- `Configure(disabled)` applies the safe `AO_EN` state. Until the SPI layer has
  a deconfigure operation, the SPI peripheral remains configured but stopped.

Therefore, configure then start begins at 0 V. Stop then start restores the
previous DAC values; reconfigure before restarting when 0 V is required.
Runtime writes and prepared-batch submissions are accepted only while started.

## Public API

| Function | Behaviour |
|----------|-----------|
| `EXEC_ANALOGUE_OUTPUT_Configure(config)` | Enables and configures the module, or applies its disabled safe state according to `config->is_enabled`. |
| `EXEC_ANALOGUE_OUTPUT_Start()` | Starts SPI and enables the external analogue-output path. |
| `EXEC_ANALOGUE_OUTPUT_Stop()` | Disables the external path and stops SPI while retaining DAC values and configuration. |
| `EXEC_ANALOGUE_OUTPUT_Is_Configured()` | Reports whether configuration has completed. |
| `EXEC_ANALOGUE_OUTPUT_Is_Started()` | Reports whether the output path is started. |
| `EXEC_ANALOGUE_OUTPUT_Prepare_Frame()` | Validates, clamps, and converts one voltage request outside the execution hot path. |
| `EXEC_ANALOGUE_OUTPUT_Submit_Prepared_Batch()` | Submits a prepared batch while started. |
| `EXEC_ANALOGUE_OUTPUT_Write_Voltage()` | Compatibility interface for manual and console writes while started. |

A successful LogicExpander send means the I2C transaction was initiated; its
electrical completion may still be asynchronous. The global LogicExpander
active mask must include `LOGIC_EXPANDER_I2C_AO` during board bring-up for
`AO_EN` changes to reach the physical device.
