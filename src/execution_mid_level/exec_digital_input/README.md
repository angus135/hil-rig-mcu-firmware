# exec_digital_input

## Overview

`exec_digital_input` owns the aggregate lifecycle and voltage selection for ten digital inputs.
Firmware channel names follow the one-based board silkscreen. The underlying array indexes remain
zero-based.

## Lifecycle

1. `EXEC_DIGITAL_INPUT_Configure()` validates all ten modes, stages the two selector bits for every
   channel, and builds a retained physical GPIOD sampling mask.
2. `EXEC_DIGITAL_INPUT_Start()` activates the retained mask.
3. `EXEC_DIGITAL_INPUT_Stop()` clears the active mask while retaining configuration.
4. `EXEC_DIGITAL_INPUT_Sample_All()` performs one GPIO port read and one mask operation.

Disabled channels use the safe 3.3 V selector state but are excluded from the sampling mask. A
configuration failure after expander staging begins leaves the subsystem disabled and non-startable.

## Voltage Selection

The DI selectors use `LOGIC_EXPANDER_DI_1` and `LOGIC_EXPANDER_DI_2`. The schematic truth table is
written in `(S1, S0)` order:

| Mode | S1 | S0 |
|---|---:|---:|
| Disabled / 3.3 V | 0 | 0 |
| 5 V | 0 | 1 |
| 12 V | 1 | 0 |
| 24 V | 1 | 1 |

## Sample Representation

Samples are currently `uint32_t` physical GPIOD masks. Digital-input data occupies bits 0, 1, 2,
3, 8, 9, 10, 11, 14, and 15. The result is not packed into logical channel bits 0 through 9.
The execution layer currently preserves the raw GPIO polarity and does not invert comparator
signals.

TODO: Evaluate changing the public sample and downstream capture buffers to `uint16_t` to reduce
buffer and transfer size. The HW GPIO port-read API should remain `uint32_t`.
