# logic_expander
## Overview

`logic_expander` contains the code for the logic_expander module, containing any logic_expander functionality that does nto require a dedicated task.

This module is responsible for:

- Holding a software shadow-map of control outputs for up to 8x MCP23017 devices.
- Providing a config-stage API for other modules to set individual control bits.
- Sending all staged control bytes to the expanders on the internal FMPI2C1 bus.


---

## Files

| File                      | Role |
|---------------------------|------|
| `logic_expander.c`        | Public API implementation |
| `logic_expander.h`        | Public API header |


---

## Public API

The public API is declared in `logic_expander.h`.

- `expander_self_config()`
	- Initializes all configured MCP23017 devices (`0x20..0x27`).
	- Applies default setup (IOCON, direction, pull-up, interrupt defaults).
	- Seeds the internal A/B output shadow bytes from file-level init tables.

- `expander_load_control_bit(expander_index, port, bit_index, bit_value)`
	- Updates one bit in the local shadow map only (does not immediately write I2C).
	- `expander_index`: `0..7`
	- `port`: `LOGIC_EXPANDER_PORT_A` or `LOGIC_EXPANDER_PORT_B`
	- `bit_index`: `0..7`
	- `bit_value`: `true` sets bit high, `false` clears bit low

- `expander_send_control_bits()`
	- Writes staged `OLATA` and `OLATB` values to all 8 expanders.
	- Intended to be called once after all config modules have loaded their bits.

---

## Required config-stage flow

Use the logic expander API in this order:

1. Call `expander_self_config()` once at the start of project configuration.
2. From each config module, call `expander_load_control_bit(...)` for the bits that module owns.
3. After all modules are done loading bits, call `expander_send_control_bits()` once.

This keeps each module independent while still sending one consolidated output image to the hardware.

---

## Example usage

```c
#include "logic_expander.h"

void CONFIG_MANAGER_Run_Config( void )
{
	( void )expander_self_config();

	/* Example: module A sets Expander 0, Port A, bit 3 high */
	( void )expander_load_control_bit( 0U, LOGIC_EXPANDER_PORT_A, 3U, true );

	/* Example: module B sets Expander 2, Port B, bit 1 low */
	( void )expander_load_control_bit( 2U, LOGIC_EXPANDER_PORT_B, 1U, false );

	( void )expander_send_control_bits();
}
```

---

## Mapping source of truth (important)

To determine the correct target for `expander_load_control_bit(...)`:

- Which expander index (`0..7`)
- Which port (`A` or `B`)
- Which bit (`0..7`)

Use the project hardware documentation as the source of truth:

- Altium project/schematic
- Project interface/control mapping documentation

Do not guess mappings in firmware code. If mapping is unclear, resolve it in hardware/docs first.