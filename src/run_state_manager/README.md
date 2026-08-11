# Run State Manager

## Overview

The Run State Manager will be the RTOS task that owns the global HIL-RIG
lifecycle. It coordinates Flash Manager preparation/finalisation and starts or
stops the Execution Manager timer; it does not execute peripheral instructions
or access Flash Manager-owned buffers directly.

The current implementation is a temporary timer controller and still duplicates
Execution Manager frequency constants. It is not the final state machine.

## Execution lifecycle

Before a run, request Flash Manager execution preparation and wait for
`FLASH_MANAGER_STATE_EXECUTING`. Only then may TIM4 start. At normal completion,
stop TIM4, ensure the ISR has returned and all result leases are resolved, then
request result finalisation and wait for `RESULTS_READY` or `FAULT`.

Instructions are ordered by timestamp. If the Execution Manager observes an
instruction timestamp less than the current tick, the workload has overrun its
deadline and the test is infeasible. The late instruction is not consumed. The
Run State Manager must receive this fault through a future ISR-safe handoff,
stop execution, and record the infeasible outcome. Future feasibility analysis
should reject such a workload before execution.

The current Flash Manager exposes normal result finalisation but no session
discard API. Preserving committed diagnostic results therefore uses the normal
finalisation path and may end in `RESULTS_READY` while the global run outcome
remains infeasible. A discard policy would require an explicit future API.


---

## Files

| File                      | Role |
|---------------------------|------|
| `run_state_manager.c` | Temporary timer-controller implementation |
| `run_state_manager.h` | Public API and future lifecycle integration contract |
| `configuration_application.c/.h` | Legacy configuration/timer placeholder |


---

## Public API

The public API is declared in `run_state_manager.h`.
