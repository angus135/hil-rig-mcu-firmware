# hw_adc
## Overview

`hw_adc` contains the low-level application code for handling ADC measurements.

This module is responsible for:

- configuring the ADC measurement trigger frequency used for continuous analogue sampling
- starting and stopping timer-triggered ADC DMA measurements
- providing access to the most recent DMA-captured ADC measurements
- providing a separate polled ADC read path for slower, non-execution-critical measurements

`hw_adc` exists to separate ADC hardware access from higher-level execution logic. In the
HIL-RIG, analogue inputs used during test execution need to be acquired with very low overhead.
Rather than performing blocking ADC reads inside the execution path, this module allows ADC
sampling to run continuously in the background using timer-triggered DMA. Higher-level modules
can then retrieve recent samples quickly when needed.

This design supports the HIL-RIG requirement for deterministic execution timing by moving ADC
conversion and transfer work out of the execution manager ISR and into dedicated hardware
peripherals.

---

## Design Summary

`hw_adc` provides two ADC access paths:

1. **DMA measurement path**
   - used for execution-time analogue input acquisition
   - ADC conversions are triggered by a hardware timer at a configured sample rate
   - results are written continuously into a DMA buffer
   - other modules can retrieve a number of recent measurements without needing to directly
     manage ADC or DMA hardware details

2. **Polled measurement path**
   - used for one-off ADC reads that are not timing-critical
   - suitable for slower monitoring or supervisory reads, such as VIN measurement
   - performs a direct blocking conversion of a selected ADC source

The DMA path is the primary mechanism for analogue measurements that must integrate with the
execution manager. The polled path exists for broader HIL-RIG ADC use cases outside the
time-critical execution loop.

---

## Files

| File       | Role |
|------------|------|
| `hw_adc.c` | Public API implementation |
| `hw_adc.h` | Public API header |

---

## Key Behaviour

### Timer-driven DMA sampling

For continuous analogue acquisition, `hw_adc` configures a timer to generate the ADC trigger at
a selected sample rate. Once started, the ADC and DMA hardware continuously place sampled channel
values into an internal buffer.

This allows the rest of the system to work with recent analogue data without repeatedly starting
and waiting on ADC conversions in software.

### Recent measurement access

The module exposes an interface for reading a specified number of the most recent DMA
measurements. This is intended for higher-level modules that need a fast snapshot of recent ADC
history for processing such as averaging or filtering.

### Separate polled read path

Not all ADC reads in the HIL-RIG need to be continuous or ISR-friendly. For slower measurements,
`hw_adc` also provides a direct polling interface that performs a one-shot conversion for a
selected ADC source.

This keeps the execution-time path efficient while still supporting general-purpose ADC use
elsewhere in the system.

---

## Usage Notes

- The DMA measurement path is intended for execution-time analogue input handling.
- The polled measurement path is intended for slower supervisory or background reads.
- This module does not decide how ADC values are interpreted, filtered, or stored in execution
  results. That responsibility belongs to higher-level modules.
- This module is concerned with ADC acquisition, not application-level analogue input semantics.