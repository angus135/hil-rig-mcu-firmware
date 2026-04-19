# hw_gpio  
## Overview

`hw_gpio` contains the low-level application code for controlling digital output pins.

This module is responsible for:

- mapping software-defined GPIO output names to physical MCU ports and pins  
- setting and resetting individual digital outputs  
- efficiently setting and resetting multiple outputs using port-level operations  
- grouping GPIO operations by port to minimise hardware access overhead  
- providing a string-to-enum interface for configurable GPIO control  

`hw_gpio` exists to separate GPIO hardware access from higher-level execution logic. In the
HIL-RIG, digital outputs may need to be updated deterministically and with minimal latency.
Rather than performing repeated per-pin operations, this module allows outputs to be grouped
and written per-port, reducing the number of hardware register accesses.

This design supports efficient execution by leveraging the STM32 GPIO BSR register, allowing
multiple pins on the same port to be modified in a single operation.

---

## Design Summary

`hw_gpio` provides two primary GPIO control paths:

1. **Single-pin control path**
   - used when manipulating individual digital outputs  
   - resolves the software pin name to a hardware port and pin mask  
   - performs a direct set or reset operation  

2. **Multi-pin (batched) control path**
   - used when updating multiple outputs simultaneously  
   - groups pins by GPIO port  
   - combines pin masks per port to minimise the number of hardware writes  
   - performs one write per port instead of one write per pin  

The multi-pin path is the preferred mechanism when multiple outputs must be updated together,
as it significantly reduces execution overhead and improves determinism.

---

## Files

| File        | Role |
|-------------|------|
| `hw_gpio.c` | Public API implementation and internal helpers |
| `hw_gpio.h` | Public API header and GPIO definitions |

---

## Key Behaviour

### Software-to-hardware pin mapping

GPIO outputs are referenced using the `GPIOOutput_T` enum. Internally, these are mapped to
hardware-specific `(port, pin)` pairs using a central mapping function.

This abstraction allows higher-level modules to work with logical pin names without needing
knowledge of MCU-specific definitions.

### Port-based batching

To improve efficiency, `hw_gpio` groups GPIO operations by port before writing to hardware.
Pins that share the same GPIO port are combined into a single pin mask and written in one step.

This reduces the number of register accesses and ensures faster, more deterministic updates.

### Direct BSR register usage

All set and reset operations ultimately write to the STM32 GPIO BSR register via the
LL (Low Layer) driver.

- Lower 16 bits → set pins high  
- Upper 16 bits → reset pins low  

By combining pin masks, multiple outputs can be modified atomically in a single write.

### String-to-enum conversion

The module provides a mapping from string names to `GPIOOutput_T` values. This enables
dynamic configuration or control of GPIO outputs from higher-level systems (e.g. command
interfaces or configuration files).

---

## Usage Notes

- The multi-pin interface (`HW_GPIO_Set_Many_Pins`, `HW_GPIO_Reset_Many_Pins`) should be used
  wherever possible for efficiency.
- All GPIO outputs must be defined in both:
  - the `GPIOOutput_T` enum  
  - the internal mapping function that associates them with hardware pins  
- The module assumes GPIO pins are pre-configured as outputs during system initialisation
  (typically via CubeMX or equivalent).
- This module does not manage GPIO configuration (mode, speed, pull-up/down), only output state.
- This module does not enforce application-level meaning of GPIO signals; it strictly handles
  low-level pin control.