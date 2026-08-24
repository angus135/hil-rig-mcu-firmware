# exec_pwm_capture

## Overview

`exec_pwm_capture` contains the execution-layer PWM capture driver.

This module consumes raw capture data from the low-level `hw_pwm_capture`
driver, performs minimal validity checks, and exposes copied period/high-time
measurements to the execution manager.

This module is responsible for:

- Selecting the PWM capture analogue front-end mode through the LogicExpander.
- Configuring or disabling PWM capture channels.
- Starting PWM capture channels through the hardware layer.
- Stopping PWM capture channels through the hardware layer.
- Tracking disabled, configured, and started channel state.
- Detecting newly available hardware capture results.
- Reading raw period and high-time timer values from hardware result pointers.
- Consuming hardware capture flags after values are read.
- Validating captured values for basic logical correctness.
- Converting validated ticks to frequency and duty cycle.
- Returning execution-owned result structures to callers.

This module does not configure timer registers directly, own hardware capture
registers, or timestamp measurements.

---

## Files

| File | Role |
|------|------|
| `exec_pwm_capture.c` | Execution-layer PWM capture implementation |
| `exec_pwm_capture.h` | Public API for execution-layer PWM capture |
| `tests/test_exec_pwm_capture.cpp` | Unit tests for execution-layer PWM capture behaviour |

---

## Layering

`exec_pwm_capture` sits between higher-level execution code and the low-level
PWM capture driver.

The hardware layer owns:

- Timer configuration.
- Timer start/stop control.
- Capture register mapping.
- Capture flag ownership.

The execution layer owns:

- Analogue front-end mode selection and its LogicExpander mapping.
- Channel lifecycle state.
- Start/stop sequencing policy.
- Copying raw capture ticks into caller-owned storage.
- Minimal result validation.

This separation keeps hardware register access out of execution-manager-facing
code while still allowing deterministic low-overhead capture reads.

---

## Configuration and Start/Stop Flow

`EXEC_PWM_Capture_Configure_Channel()` applies the requested analogue front-end
mode and configures the mapped hardware channel without starting capture. A
disabled configuration disables the hardware channel and applies the safe
3.3 V frontend mode. Direct LogicExpander submission is temporary until global
configuration commits all staged subsystem changes.

`EXEC_PWM_Capture_Start_Channel()` starts only a channel in configured state.
It does not repeat subsystem configuration.

`EXEC_PWM_Capture_Stop_Channel()` stops a started channel while retaining its
peripheral configuration and frontend mode, allowing a later restart. To fully
disable a channel and apply its safe frontend state, call configure with
`is_enabled` set to false.

A lifecycle request fails if:

- the channel is invalid,
- the request is not valid for the channel's current state,
- the LogicExpander rejects a frontend update,
- the hardware layer rejects configure, start, or stop.

---

## Consume Flow

`EXEC_PWM_Capture_Consume()` consumes one newly captured PWM measurement.

Typical behaviour:

1. Peek the hardware result with `HW_PWM_Capture_Peek_Result()`.
2. If no new data is available, mark the output result invalid and return false.
3. Read raw `period_ticks` and `high_ticks` from the hardware result pointers.
4. Consume the hardware capture flag with `HW_PWM_Capture_Consume_Result()`.
5. Validate the copied raw values.
6. Populate `ExecPwmCaptureResult_T` and return true if the measurement is valid.

The hardware result is consumed after the raw CCR values are read. This avoids
clearing the capture flag before the execution layer has copied the measurement.

---

## Validation

The execution layer performs only minimal validation required to reject
obviously invalid PWM measurements.

A result is valid when:

- `period_ticks > 0`
- `high_ticks <= period_ticks`

Callers may retain the raw ticks or use `EXEC_PWM_Capture_Convert()` to derive
frequency and duty cycle from the cached hardware timer clock.

---

## Public API

The public API is declared in `exec_pwm_capture.h`.

| Function | Purpose |
|----------|---------|
| `EXEC_PWM_Capture_Configure_Channel()` | Configure or disable a PWM capture channel |
| `EXEC_PWM_Capture_Start_Channel()` | Start a PWM capture channel |
| `EXEC_PWM_Capture_Stop_Channel()` | Stop capture while retaining configuration |
| `EXEC_PWM_Capture_Consume()` | Consume one new valid PWM capture result |
| `EXEC_PWM_Capture_Convert()` | Convert raw ticks to frequency and duty cycle |

---

## Result Semantics

`ExecPwmCaptureResult_T` contains execution-owned copies of one PWM capture
measurement.

| Field | Meaning |
|-------|---------|
| `is_valid` | True when the result contains a new valid measurement |
| `period_ticks` | Raw captured PWM period in timer ticks |
| `high_ticks` | Raw captured PWM high time in timer ticks |

If no new capture is available, or if the raw capture values are invalid,
`EXEC_PWM_Capture_Consume()` returns false and sets `is_valid` to false.

---

## Execution Path Notes

`EXEC_PWM_Capture_Consume()` is designed for deterministic execution use. The
caller is expected to provide a valid channel, a non-null result pointer, and a
channel that has already been started.

The function returns true only when a new valid measurement has been consumed.
