# exec_pwm_capture

## Overview

`exec_pwm_capture` contains the execution-layer PWM capture driver.

This module consumes raw capture data from the low-level `hw_pwm_capture`
driver, performs minimal validity checks, and exposes copied period/high-time
measurements to the execution manager.

This module is responsible for:

- Starting PWM capture channels through the hardware layer.
- Stopping PWM capture channels through the hardware layer.
- Tracking execution-layer channel started state.
- Detecting newly available hardware capture results.
- Reading raw period and high-time timer values from hardware result pointers.
- Consuming hardware capture flags after values are read.
- Validating captured values for basic logical correctness.
- Returning execution-owned result structures to callers.

This module does not configure timer registers directly, own hardware capture
registers, timestamp measurements, or convert raw ticks into frequency or duty
cycle units.

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

- Analogue front-end selection.
- Timer configuration.
- Timer start/stop control.
- Capture register mapping.
- Capture flag ownership.

The execution layer owns:

- Channel lifecycle state.
- Start/stop sequencing policy.
- Copying raw capture ticks into caller-owned storage.
- Minimal result validation.

This separation keeps hardware register access out of execution-manager-facing
code while still allowing deterministic low-overhead capture reads.

---

## Start/Stop Flow

`EXEC_PWM_Capture_Start_Channel()` starts a capture channel by validating the
request, rejecting duplicate starts, and delegating the enabled configuration to
`HW_PWM_Capture_Configure_Channel()`.

A start request fails if:

- the channel is invalid,
- the configuration pointer is null,
- `config->is_enabled` is false,
- the channel is already started,
- the hardware layer rejects the configuration.

`EXEC_PWM_Capture_Stop_Channel()` stops a previously started channel by applying
a disabled hardware configuration using the default safe mode
`HW_PWM_CAPTURE_LV_3V3`.

A stop request fails if:

- the channel is invalid,
- the channel has not been started,
- the hardware layer rejects the disabled configuration.

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

The execution layer intentionally does not calculate frequency or duty cycle.
Higher layers can derive those values from the raw ticks when needed.

---

## Public API

The public API is declared in `exec_pwm_capture.h`.

| Function | Purpose |
|----------|---------|
| `EXEC_PWM_Capture_Start_Channel()` | Start a PWM capture channel |
| `EXEC_PWM_Capture_Stop_Channel()` | Stop a PWM capture channel |
| `EXEC_PWM_Capture_Consume()` | Consume one new valid PWM capture result |
| `EXEC_PWM_Capture_Test_Reset()` | Reset internal state in test builds only |

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
