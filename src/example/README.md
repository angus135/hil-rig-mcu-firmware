# example
## Overview

The `example` module provides a minimal, self-contained, production-ready embedded C component used to demonstrate the project’s coding standards, naming conventions, and testing methodology.

It implements:

 - A public API for initialising the module and processing input values
 - An internal state structure, isolated from external access
 - A simple wrapper that interacts with a GPIO HAL function, providing a controllable seam for unit testing
 - A test entry point used for functional validation and debugging

This module shows how a real subsystem should be designed:

 - Internal details hidden behind a clean API
 - No global variables leaked externally
 - No assumptions about execution order beyond explicit initialisation
 - Full unit-test coverage with mocks replacing hardware dependencies

It is intentionally simple, but the structure and design patterns scale to more complex modules such as ADC sampling, CAN routing, etc.

---

## Responsibilities
The module is responsible for:

Maintaining internal state representing its latest processed value
 - Scaling an input by a module-defined constant
 - Providing an interface for driving a logical digital output pin via the HAL layer
 - Demonstrating how production HAL functions can be replaced using GoogleMock during testing

It does not:
 - Configure clocks or hardware registers
 - Allocate memory dynamically
 - Assume any RTOS or scheduler environment

These constraints ensure the module remains deterministic, testable, and portable.

---
## Directory Structure
The module uses the following structure:

```
example/
├─ example.c # Module implementation
├─ example.h # Public API
├─ hal_gpio.h # HAL dependency (mockable interface)
└─ tests/
├── example_mocks.h # Declarations of functions used in example module that are defined in test_example.cpp 
└── test_example.cpp # GoogleTest + GoogleMock unit tests
```

All functionality is scoped to this directory. Other modules may consume the API, but cannot access private internals.
---

## Files

| File                      | Role |
|---------------------------|------|
| `example.c`        | Implements the module logic and internal state |
| `example.h`        | Defines the public API, constants, typedefs and usage notes |
| `tests/example_mocks.h`        | Declares the mocked hardware abstraction function used by this module |
| `tests/test_example.c` | Complete unit test suite using GoogleTest/GoogleMock |


---

## Public API
The public API declared in example.h offers:

 - **EXAMPLE_Init( void )** - Must be called before using any other API functions
 - **EXAMPLE_Process( uint16_t input )** - Scales the input value using EXAMPLE_SCALE_FACTOR and updates internal state
 - **EXAMPLE_Test( uint16_t test_value )** - Provides a controlled entry point for triggering known-good behaviour
 - **EXAMPLE_SetOutput( uint32_t pin, bool level )** - Forwards output state to HAL_GPIO_WritePin, which is mocked in unit tests

 ---
## Testing

The module is fully covered by automated unit tests located in:
```
example/tests/test_example.cpp
```

The test suite uses:
 - GoogleTest for assertions and fixtures
 - GoogleMock to intercept calls to HAL_GPIO_WritePin
 - A linker substitution pattern to override real hardware behaviour

This demonstrates the preferred testing architecture for embedded modules in this project:
 - Hardware is abstracted via C functions
 - Tests are written in C++ but link against C modules
 - No hardware, RTOS, or peripheral drivers are required