# hw_pwm gen
## Overview

`hw_pwm_gen` contains the application for handling pwm generation

This module is responsible for:

- TODO


---

## Files

| File                      | Role |
|---------------------------|------|
| `hw_pwm gen.c`        | Public API implementation |
| `hw_pwm gen.h`        | Public API header |


---

## Public API

The public API is declared in `hw_pwm gen.h`.

## Notes

If we want 0.1% resolution then the ARR register must be >= 999
ARR = timer_hz/((prescaler+1)*pwm_hz)-1
1000 =< timer_hz/((prescaler+1)*pwm_hz)
1<=pwm_hz<=1_000_000    ie worst case when pwm_hz = 1_000_000
1000 =< timer_hz/((prescaler+1)*1_000_000)
1_000_000_000 =< timer_hz/(prescaler+1)
This means 0.1% resolution possible on the STM32 timers, 
but also suggests a prescaler of 0 for highest resolution.
