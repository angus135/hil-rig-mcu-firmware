# exec_analogue_input
## Overview

`exec_analogue_input` contains the execution-level application code for handling analogue inputs.

This module is responsible for:

- validating and applying execution-time analogue input configuration
- coordinating with `hw_adc` to configure continuous ADC sampling for a test run
- retrieving recent analogue measurements from the ADC DMA path
- processing those measurements into execution-ready analogue input values
- writing analogue input results into storage owned by the execution manager

`exec_analogue_input` exists to bridge the gap between low-level ADC acquisition and the needs of
the execution manager. In the HIL-RIG, the execution manager must obtain analogue input readings
quickly at each execution timestamp. This module provides that interface by using recent
background-sampled ADC values rather than performing slow blocking conversions during execution.

---

## Design Summary

`exec_analogue_input` is the execution-facing analogue input layer.

It does not interact with ADC hardware directly. Instead, it uses the `hw_adc` module to access
recent ADC measurements that have already been captured through timer-triggered DMA sampling.

This arrangement is important for the HIL-RIG because the execution manager ISR must remain brief
and deterministic. By relying on ADC samples that were gathered in the background, this module can
perform only lightweight work during execution, such as:

- reading recent measurements from `hw_adc`
- combining or averaging those measurements
- storing the resulting analogue input values in the execution results structure

This makes the module suitable for integration into the execution path while keeping low-level ADC
handling separate.

---

## Files

| File                     | Role |
|--------------------------|------|
| `exec_analogue_input.c`  | Public API implementation |
| `exec_analogue_input.h`  | Public API header |

---

## Key Behaviour

### Execution-time configuration

Before a test begins, this module accepts the analogue input configuration for the run. This
includes information such as the ADC sample rate and which analogue channels are expected to be
used.

It then passes the sampling-rate configuration down to `hw_adc`, which in turn relies on the
underlying timer configuration needed for continuous ADC triggering.

### Lightweight execution-time reading

During execution, this module reads a small set of recent ADC measurements from the `hw_adc` DMA
buffer and processes them into the analogue input values used by the execution manager.

The current design uses recent samples rather than initiating new ADC conversions on demand. This
keeps execution-time work brief and avoids blocking behaviour inside the ISR path.

### Result preparation for the execution manager

Rather than owning the final result storage itself, this module writes the processed analogue input
values to destinations provided by the caller. This allows the execution manager to control where
the current timestamp’s analogue input results are stored.

---

## Interaction with Other Modules

### `hw_adc`
`exec_analogue_input` depends on `hw_adc` for access to ADC measurements. It uses `hw_adc` to:

- configure the ADC measurement frequency for the run
- retrieve recent DMA-captured ADC measurements

This lets `exec_analogue_input` avoid direct knowledge of ADC registers, DMA indexing, or timer
details.


### Execution manager
The execution manager is expected to use this module as the analogue-input interface during a test
run.

A typical flow is:

1. configure analogue inputs before execution starts
2. ensure the ADC DMA measurement path is running
3. call this module during execution to obtain current analogue input values
4. store those values in the test results for the relevant execution timestamp

---

## Usage Notes

- This module is intended for execution-time analogue input handling, not general-purpose ADC
  access.
- It relies on `hw_adc` for low-level acquisition and assumes that the continuous ADC measurement
  path is being used for execution-time readings.
- It is designed so that execution-time work remains brief and predictable.
- This module is responsible for execution-facing analogue input handling, not low-level ADC
  control.