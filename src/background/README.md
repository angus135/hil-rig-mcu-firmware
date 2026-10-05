# background

## Overview

The background task owns periodic task-context servicing that does not justify a
dedicated task. It runs at a 10 ms base period so the non-blocking logic-expander
transaction queue progresses promptly.

The status LEDs have explicit ownership:

- Blue 1 is the one-second heartbeat.
- Blue 2 (`USER_LED_BLUE_1`, PE2) is high during the TIM4 Execution Manager
  ISR body and low on every exit, including execution-guard rejection.
- Blue 3 through Blue 6 display `RunState_T + 1` in binary, with Blue 3 as the
  most-significant bit and rightmost Blue 6 as the least-significant bit. Adding
  one keeps `IDLE` visibly distinct from all LEDs off.
- All six red LEDs are reserved for faults and flash together while the Run
  State Manager is in `RUN_STATE_FAULT`.

The background task also clears Blue 2 whenever the execution timer is stopped.

Application startup initializes the Logic Expander and starts its asynchronous
self-configuration before the scheduler runs. Each background cycle then loops
through a function-pointer array containing the logic-expander and status-LED
processes. Additional periodic work can be added to this array without changing
the task loop. The expander mutex allows Run State Manager, driver, and console
calls to overlap this periodic servicing safely.
