# exec_digital_output

## Overview

`exec_digital_output` owns the aggregate lifecycle and board-level voltage selection for the ten
digital outputs. Firmware channel names follow the one-based board silkscreen; channel 1 maps to
schematic signal `DO_MCU0`, and channel 10 maps to `DO_MCU9`.

The MCU output pins are all on GPIOG. The HW GPIO layer translates logical `DIGITAL_OUTPUT_0` to
`DIGITAL_OUTPUT_9` identities into physical GPIO masks, ensuring unrelated pins such as PG6
(`QSPI_NCS`) are not modified.

## Lifecycle

1. `EXEC_DIGITAL_OUTPUT_Configure()` validates all ten channel requests, drives all output signals
   low, stages their voltage selections, and leaves the subsystem configured and stopped.
2. `EXEC_DIGITAL_OUTPUT_Start()` sets only enabled channels whose retained initial state is high.
3. `EXEC_DIGITAL_OUTPUT_Stop()` drives all ten output signals low while retaining configuration.
4. A subsequent Start reapplies the retained initial states.

Disabled channels ignore their requested mode and use the safe 3.3 V selection. Invalid
configuration does not disturb an existing stopped configuration. A failure after hardware
mutation begins leaves the subsystem disabled so stale configuration cannot be started.

## Voltage Selection

Each output uses `DO_A0_n` and `DO_A1_n` signals distributed across `LOGIC_EXPANDER_DO_1` and
`LOGIC_EXPANDER_DO_2`:

| A0 | A1 | Voltage |
|---:|---:|---:|
| 0 | 0 | 3.3 V |
| 0 | 1 | 5 V |
| 1 | 0 | 12 V |
| 1 | 1 | 24 V |

Configure stages all twenty selector bits and submits one Logic Expander update. The direct send
is temporary until global configuration commits all staged subsystem changes.

## Runtime Path

`EXEC_DIGITAL_OUTPUT_Set_Output()` and `EXEC_DIGITAL_OUTPUT_Reset_Output()` forward physical port
masks directly to the HW layer. They intentionally contain no lifecycle or defensive checks.
Callers are responsible for using a mask produced from the logical GPIO mapping.
