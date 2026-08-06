# exec_analogue_output

## Overview

`exec_analogue_output` owns execution-level control of the MCP48CVB28T DAC and
the board-level `AO_EN` signal.

The module:

- Configures and starts the dedicated DAC SPI channel.
- Programs the DAC reference, gain, power-down, and initial channel registers.
- Keeps channels 0-5 available and channels 6-7 in open-circuit power-down mode.
- Controls `AO_EN` through port B bit 0 of
  `LOGIC_EXPANDER_DEVICE_I2C_AO`.
- Scales requested 0-20 V outputs into the DAC's 12-bit range.

## Lifecycle

The public lifecycle is deliberately split:

```text
Unconfigured --Configure--> Configured/stopped --Start--> Started
                              ^                         |
                              +----------Stop-----------+
```

`Configure()` holds `AO_EN` low while it configures SPI and queues the DAC
startup frames. `Start()` waits for those frames to complete before asserting
`AO_EN`. `Stop()` first deasserts `AO_EN`, then queues 0 V for channels 0-5.
The DAC and SPI configuration are retained when that sequence succeeds, so
another `Start()` does not require reconfiguration and cannot expose the DAC
until the safe-value frames have completed.

Calling `Configure()` again while stopped first stops the dedicated SPI runtime
and then applies the new configuration. Reconfiguration is rejected while the
output path is started.

Voltage writes are accepted only in the started state.

## Public API

| Function | Behaviour |
|----------|-----------|
| `EXEC_ANALOGUE_OUTPUT_Configure(use_external_vref)` | Disables the external output path, configures the dedicated SPI channel, selects the DAC reference, enables DAC channels 0-5, powers down channels 6-7, and initializes all DAC data registers to zero. |
| `EXEC_ANALOGUE_OUTPUT_Start()` | Requires successful configuration and completed DAC startup transmission, then asserts `AO_EN`. |
| `EXEC_ANALOGUE_OUTPUT_Stop()` | Deasserts `AO_EN`, queues 0 V for channels 0-5, and retains configuration when both operations succeed. |
| `EXEC_ANALOGUE_OUTPUT_Is_Configured()` | Reports whether configuration completed successfully. |
| `EXEC_ANALOGUE_OUTPUT_Is_Started()` | Reports whether the external output enable/disable commit was accepted. |
| `EXEC_ANALOGUE_OUTPUT_Write_Voltage(channel, input_voltage_v)` | Clamps and scales a voltage request and queues the corresponding DAC write. Requires the module to be configured and started. |

| Function | What it does |
|----------|--------------|
| `EXEC_ANALOGUE_OUTPUT_SPI_Channel_Setup()` | Configures and starts the SPI channel used by the DAC. Call this during system startup before attempting any DAC transfers. |
| `EXEC_ANALOGUE_OUTPUT_Config(use_external_vref)` | Queues the DAC's 11 startup commands atomically as separate three-byte, CS-framed SPI packets, triggers once, selects the reference source, enables channels 0-5, and puts channels 6-7 into open-circuit power-down mode. |
| `EXEC_ANALOG_OUTPUT_Is_Configured()` | Returns whether `EXEC_ANALOGUE_OUTPUT_Config()` has completed successfully. Useful for guarding console commands or higher-level control logic. |
| `EXEC_ANALOG_OUTPUT_Write_Voltage(channel, input_voltage_v)` | Validates the requested channel, clamps the input voltage to 0-20 V, scales it to the DAC's 12-bit range, and sends a write frame to the DAC. Returns false if the module is not configured, the channel is out of range, or the SPI transfer cannot be queued. |

- A failed configuration leaves the module unconfigured and stopped.
- A failed start leaves the module stopped.
- A stop failure before the `AO_EN` commit leaves the module marked started
  because the hardware output may still be enabled; the caller may retry.
- If `AO_EN` is disabled but safe DAC frames cannot be queued, the module is
  stopped and marked unconfigured so stale DAC values cannot be re-exposed.
- A successful LogicExpander send means the I2C transaction was initiated. The
  final transfer may still be completing asynchronously.

- Call `EXEC_ANALOGUE_OUTPUT_SPI_Channel_Setup()` before `EXEC_ANALOGUE_OUTPUT_Config()`.
- Only channels 0-5 are intended for active analogue outputs.
- Channels 6-7 are deliberately disabled and should not be written to.
- Voltage writes are clamped to the 0-20 V input range before scaling.
