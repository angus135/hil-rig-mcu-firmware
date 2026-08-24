# hw_pwm_capture

## Overview

`hw_pwm_capture` contains the low-level PWM capture driver.

This module owns the timer-facing PWM capture path, including timer start/stop
sequencing, logical channel mapping, and zero-copy access to raw timer capture
registers.

This module is responsible for:

- Configuring, starting, stopping, and disabling timer-based PWM capture.
- Mapping logical capture channels to timer peripherals and CCR registers.
- Exposing new capture availability through timer status flags.
- Returning direct pointers to raw period and high-time capture registers.
- Clearing consumed capture flags after the execution layer has read a result.

This module does not interpret captured values, convert to frequency or duty
cycle, timestamp measurements, or own execution-layer result storage.

---

## Files

| File | Role |
|------|------|
| `hw_pwm_capture.c` | Low-level PWM capture implementation |
| `hw_pwm_capture.h` | Public API for hardware PWM capture |
| `tests/test_hw_pwm_capture.cpp` | Unit tests for hardware PWM capture behaviour |
| `tests/hw_pwm_capture_mocks.h` | Timer and hardware mocks used by unit tests |

---

## Channel Mapping

The driver supports two logical PWM capture channels:

| Logical Channel | Timer | Period Capture | High-Time Capture |
|-----------------|-------|----------------|-------------------|
| `HW_PWM_CAPTURE_CHANNEL_1` | `TIM2` | `CCR1` | `CCR2` |
| `HW_PWM_CAPTURE_CHANNEL_2` | `TIM5` | `CCR2` | `CCR1` |

The timer PWM input mode is expected to be configured by the IOC. This driver
uses the fixed mapping above to expose the relevant capture registers to higher
layers.

---

## Capture Flow

Typical hardware-layer usage:

1. Configure a stopped channel with `HW_PWM_Capture_Configure_Channel(channel, true)`.
2. Start capture with `HW_PWM_Capture_Start_Channel()`.
3. Poll for new data with `HW_PWM_Capture_Peek_Result()`.
4. If `has_new_data` is true, read `period_ticks` and `high_ticks`.
5. Clear the consumed capture event with `HW_PWM_Capture_Consume_Result()`.
6. Stop capture with `HW_PWM_Capture_Stop_Channel()`.
7. Disable the channel with `HW_PWM_Capture_Configure_Channel(channel, false)` when the
   configuration is no longer required.

Configuration and capture activity are separate lifecycle states. Enabling a
channel configures its timer but leaves it stopped. Stopping a started channel
retains its configuration, so it can be started again without reconfiguration.
Disabling a channel stops it and clears its configured state and cached timer
clock.

`HW_PWM_Capture_Peek_Result()` does not copy captured data. It returns pointers
to the timer capture registers so the execution path can read the latest raw
timer values with minimal overhead.

---

## Public API

The public API is declared in `hw_pwm_capture.h`.

| Function | Purpose |
|----------|---------|
| `HW_PWM_Capture_Configure_Channel()` | Configure a stopped channel, or stop and disable it |
| `HW_PWM_Capture_Start_Channel()` | Start a configured, stopped channel |
| `HW_PWM_Capture_Stop_Channel()` | Stop a started channel while retaining its configuration |
| `HW_PWM_Capture_Peek_Result()` | Inspect whether a new raw capture is available |
| `HW_PWM_Capture_Consume_Result()` | Clear the consumed period capture flag |
| `HW_PWM_Capture_Get_Timer_Clock_Hz()` | Return the cached timer clock for a configured channel |

---

## Result Semantics

A new result is considered available when the configured period capture flag is
set for the channel.

`HwPWMCaptureResult_T` contains:

| Field | Meaning |
|-------|---------|
| `has_new_data` | True when a new complete PWM period has been captured |
| `period_ticks` | Pointer to the timer CCR containing the captured period |
| `high_ticks` | Pointer to the timer CCR containing the captured high time |

If no new capture is available, `has_new_data` is false and the pointer fields
are null.

---

## Execution Path Notes

The peek and consume functions are intended for deterministic execution use.
Callers must provide a valid, configured channel and must only consume a result
after a corresponding successful peek.

Captured values are intentionally left raw. Validation and interpretation are
performed by higher layers.
