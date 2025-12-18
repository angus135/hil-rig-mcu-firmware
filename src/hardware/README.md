# Hardware Modules
## Overview

The Hardware Modules form a thin interface layer between application logic
and the STM32 HAL library. Their purpose is to isolate direct HAL usage to a
small, well-defined set of files, making the codebase easier to unit test,
reason about, and port to other platforms or MCUs in the future.

Application-level code must never call STM32 HAL functions directly.
All interaction with hardware peripherals should go through these modules.

---

## Naming and Subdirectories

Each hardware module is prefixed with `hw_` and represents a single hardware
domain or peripheral group.

Examples:
- hw_gpio
- hw_uart
- hw_adc
- hw_can

Each module must contain, at a minimum:

1. A public header file:
    hw_<module>.h

    This header defines the public interface of the module, including:
    - enums representing logical hardware resources (LEDs, pins, channels)
    - typedefs used by the application
    - function declarations

    The header must not expose STM32 HAL headers unless absolutely required.
    Where possible, HAL types should be hidden behind module-specific enums
    or typedefs.

2. A source file:
    hw_<module>.c

    This file contains the implementation of the hardware interface and is
    the only place where STM32 HAL calls should appear.

3. Other requirements of any module: tests directory, README and CMakeLists.txt etc.


---

## TEST_BUILD vs Embedded Behaviour

All hardware-facing functions must be structured so they compile and link
cleanly in a unit test environment where the STM32 HAL is unavailable.

This is done using the TEST_BUILD compile definition.

Example pattern:
```c
void HW_GPIO_Toggle(GPIO_T gpio)
{
#ifdef TEST_BUILD
    // Unit test path:
    // No hardware access. Parameters are typically unused.
    (void)gpio;
#else
    // Embedded path:
    // Real hardware mapping using STM32 HAL.
    switch (gpio)
    {
        case GPIO_GREEN_LED_INDICATOR:
            HAL_GPIO_TogglePin(LD1_GPIO_Port, LD1_Pin);
            break;

        default:
            break;
    }
#endif
}
```

In this pattern:

- The enum (`GPIO_T`) is defined in `hw_gpio.h` and represents logical GPIOs
    known to the application.
- The embedded path maps those logical values to concrete HAL pins, ports,
    and peripherals.
- The unit test path compiles without requiring HAL headers or symbols.


---

## Unit Testing Expectations

In unit tests:

- Hardware modules may be linked against stub or fake implementations.
- The TEST_BUILD path ensures that no STM32 HAL headers or symbols are
    required.
- Behaviour verification should be done by inspecting side effects via
    mocks, fakes, or test instrumentation as appropriate.

The intent is not to test STM32 HAL behaviour, but to test application logic
that depends on hardware interactions.

---

## Design Guidelines
- Keep hardware modules small and focused.
- Expose only what the application needs.
- Avoid leaking HAL types into application code where possible.
- Prefer enums and logical identifiers over raw pins, ports, or channels.
- Do not access peripheral registers directly from application code.
- All STM32-specific knowledge should live inside hw_* modules.


---
