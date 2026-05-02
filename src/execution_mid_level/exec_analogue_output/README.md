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

The public API is declared in `exec_analogue_output.h`.