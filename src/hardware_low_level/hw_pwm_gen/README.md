# hw_pwm_gen

## Overview

`hw_pwm_gen` contains the low-level application code responsible for PWM signal generation.

This module is responsible for:

- configuring, starting, and stopping PWM timer output channels
- computing timer register values required for PWM generation
- updating timer PWM registers during execution with minimal overhead
- abstracting direct timer register access from higher-level execution logic

`hw_pwm_gen` exists to separate PWM hardware access from higher-level execution control logic.
In the HIL-RIG, PWM outputs may need to be updated dynamically during test execution with very
low latency and deterministic timing behaviour. Rather than repeatedly recalculating timer
configuration values during execution, this module provides a split configuration/execution
design where expensive calculations are performed ahead of time and execution-stage functions
perform only direct register updates.

This design supports the HIL-RIG requirement for deterministic execution timing by minimising
software overhead inside execution-critical code paths.

---

## Design Summary

`hw_pwm_gen` separates PWM handling into lifecycle control and execution-time updates:

1. **Lifecycle stage**
   - configures PWM channels without starting their timer outputs
   - starts and stops normal or complementary timer outputs
   - retains channel configuration after Stop so the channel can be restarted
   - computes timer register values required for desired PWM frequency and duty cycle
   - intended for setup-time or pre-execution preparation

2. **Execution stage**
   - used for high-speed PWM updates during execution
   - directly updates timer hardware registers with precomputed values
   - avoids expensive calculations inside execution-critical paths

This separation allows higher-level modules to precompute PWM instructions during configuration
and apply them efficiently during execution.

---

## Files

| File             | Role |
|------------------|------|
| `hw_pwm_gen.c`   | Public API implementation |
| `hw_pwm_gen.h`   | Public API header |

---

## Key Behaviour

### PWM register computation

The module provides helper functions for computing the timer register values required to achieve
a desired PWM frequency and duty cycle.

These include:

- prescaler register (`PSC`) computation
- auto-reload register (`ARR`) computation
- compare register (`CCR`) computation

These functions are intended for configuration-time use so execution-time updates can avoid
runtime arithmetic overhead.

### Direct execution-time PWM updates

For execution-critical PWM updates, the module exposes dedicated direct-update functions for
each PWM channel.

These functions:

- directly write timer hardware registers
- apply updated `ARR`, `PSC`, and `CCR` values
- minimise execution overhead by avoiding abstraction-heavy HAL calls during execution

This approach allows PWM outputs to be updated quickly and deterministically during runtime.

### PWM channel abstraction

The module provides separate interfaces for each PWM output channel while internally managing
the required timer and compare register selection.

Higher-level modules do not need to directly interact with STM32 timer peripherals or register
layouts.

### Channel lifecycle

Each channel has a strict lifecycle:

1. Configure leaves the timer output stopped.
2. Start enables the mapped normal or complementary PWM output.
3. Stop disables the timer output while retaining configuration.

Invalid transitions and HAL failures return false without advancing lifecycle state. Board-level
voltage selection is an execution-layer responsibility and is not performed by `hw_pwm_gen`.

---

## Usage Notes

- Register computation functions should be used during configuration rather than execution.
- Direct PWM update functions are intended for execution-critical runtime use.
- This module assumes timer peripherals and GPIO configuration are performed elsewhere
  (typically via STM32CubeMX-generated initialisation).
- This module is responsible for PWM signal generation only and does not define application-level
  output or voltage-selection semantics.
- Duty cycle values are represented in permille form (`0-1000`) rather than percentages.
- The execution-stage functions are designed to minimise latency and avoid blocking behaviour.
- This module abstracts timer register manipulation but does not manage higher-level waveform
  sequencing or execution scheduling.
