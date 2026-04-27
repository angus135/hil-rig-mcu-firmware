# exec_digital_input
## Overview

`exec_digital_input` provides execution-layer digital input configuration and sampling.

This module is responsible for:

- Applying per-channel digital input mode configuration for execution use.
- Building/storing an enabled-channel mask from configuration.
- Sampling all digital inputs via low-level GPIO and returning a masked result.

How it works:

- `EXEC_DigitalInput_Configure(...)` converts channel modes into an internal enabled-input pin mask.
- `EXEC_DigitalInput_SampleAll(...)` reads the raw low-level input port state, applies the stored mask, and writes the masked word to the caller.
- The internal mask is retained as module state so sampling can be called repeatedly after configuration.
- In `TEST_BUILD`, pin definitions and seams are provided by test mocks.


---

## Files

| File                      | Role |
|---------------------------|------|
| `exec_digital_input.c`            | Execution-layer digital input implementation |
| `exec_digital_input.h`            | Public execution-layer API and config types |


---

## Public API

The public API is declared in `exec_digital_input.h`.