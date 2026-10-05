# hw_timer
## Overview

`hw_timer` provides low-level timer configuration and interrupt dispatch.

This module is responsible for:

- Configure, start, and stop the timers selected by higher-level owners.
- Dispatch TIM4 update interrupts to the registered diagnostic callback or,
  when no override is installed, to `EXECUTION_MANAGER_ProcessTickFromISR()`.
- Apply the Run State Manager execution guard before dispatching TIM4 work.
- Record the latest and worst observed TIM4 service time using the Cortex-M DWT
  cycle counter.

Run State Manager owns the TIM4 execution-clock lifecycle. The default TIM4
route invokes the Execution Manager tick engine. On a terminal tick, the
Execution Manager invokes the RSM-registered ISR callback, which closes the
execution guard immediately; RSM task context subsequently stops TIM4.
Diagnostic console tests temporarily install a callback only while TIM4 is
stopped and restore the default route afterward.

TIM4 accumulates any FreeRTOS task-wake request across the complete execution
boundary and calls `portYIELD_FROM_ISR()` once after the selected callback has
returned. Task context can therefore run only between complete execution ISR
services, never partway through measurement or output processing.

TIM4 timing statistics reset whenever the execution timer starts. Measurement
begins once an update interrupt is recognised and ends before the optional
FreeRTOS yield. The cycle delta is wrap-safe for an individual ISR invocation;
higher-priority interrupt preemption is deliberately included because it also
consumes the execution deadline. `run_state status` reports the sample count,
latest service time, and maximum service time in cycles and microseconds.

Physical Blue 2 (`USER_LED_BLUE_1`, PE2) provides a scope marker: direct GPIO
set/reset writes bracket the TIM4 ISR body, including the optional yield request.
Every exit clears the marker, including a rejected execution guard or an IRQ
without an update flag. Probe PE2 relative to board ground. Pulse width includes
higher-priority interrupt preemption and excludes hardware exception entry/return;
rising-edge spacing measures interrupt service intervals, nominally 100 us during
10 kHz execution. The marker has two GPIO writes of instrumentation overhead.


---

## Files

| File                      | Role |
|---------------------------|------|
| `hw_timer.c`        | Public API implementation |
| `hw_timer.h`        | Public API header |


---

## Public API

The public API is declared in `hw_timer.h`.
