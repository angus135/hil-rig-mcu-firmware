# Run State Manager

## Overview

The Run State Manager will be the RTOS task that owns the global HIL-RIG
lifecycle. It coordinates Flash Manager preparation/finalisation and starts or
stops the Execution Manager timer; it does not execute peripheral instructions
or access Flash Manager-owned buffers directly.

Named console commands currently stand in for the future Host Interface and
Execution Manager requests. Internal completion transitions occur automatically:

```text
IDLE -> TEST_PACKAGE_RECEIVE -> CONFIGURATION -> ARMED -> EXECUTION
     -> RESULT_FINALISATION -> RESULTS_READY -> RESULT_TRANSFER -> IDLE
```

Entering `CONFIGURATION` applies the committed configuration once, then polls
aggregate driver readiness without reapplying it. The manager automatically
enters `ARMED` only after every enabled driver is ready. Driver failure or a
bounded configuration timeout enters `FAULT`.
Similarly, result finalisation remains pending until Flash Manager reports that
the result stream is ready, then automatically enters `RESULTS_READY`.

The current bring-up commands are:

```text
run_state receive
run_state configure
run_state execute
run_state execution_complete
run_state transfer
run_state transfer_complete
run_state repeat
run_state discard
run_state fault
run_state reset
```

`execution_complete` is only an RSM integration seam and console stimulus at
this stage. It does not add an Execution Manager dependency or implementation.
Fault handling retains the first recorded cause until a successful reset.
Fault entry also requests asynchronous Flash session abort after execution and
DUT drivers have stopped. Reset remains rejected until Flash reports `IDLE`,
preventing an RSM/Flash lifecycle mismatch.

Every named event is validated by the Run State Manager task before it can
initiate a transition. A delivered notification is therefore not itself proof
that the lifecycle event was valid. `run_state status` reports the last event
evaluated as accepted, rejected for the current state, rejected while an
asynchronous transition was pending, or failed during its entry action.

| Console event | Required state | Result |
|---|---|---|
| `receive` | `IDLE` | Enter `TEST_PACKAGE_RECEIVE` |
| `configure` | `TEST_PACKAGE_RECEIVE` | Enter `CONFIGURATION`, then automatically `ARMED` on success |
| `execute` | `ARMED` | Start Flash preparation; automatically enter `EXECUTION` when ready |
| `execution_complete` | `EXECUTION` | Stop execution and start Flash finalisation |
| `transfer` | `RESULTS_READY` | Enter `RESULT_TRANSFER` |
| `transfer_complete` | `RESULT_TRANSFER` | Enter `IDLE` |
| `repeat` | `RESULTS_READY` | Discard results, retain configuration/instructions, and enter `ARMED` |
| `discard` | `RESULTS_READY` | Discard results, clear configuration, and enter `IDLE` |
| `fault` | Any state | Enter `FAULT` and retain the first cause |
| `reset` | `FAULT` | Restore idle driver state and enter `IDLE` |

Driver readiness completion from `CONFIGURATION` into `ARMED`, Flash preparation completion into `EXECUTION`, and
Flash finalisation completion into `RESULTS_READY` are internal automatic
transitions. Host-driven requests, future Execution Manager completion, result
transfer completion, fault, and reset remain explicit events.

`ARMED` means test configuration has completed while the DUT drivers and
execution timer remain stopped. An execute request begins Flash Manager
execution preparation; only after Flash reports `EXECUTING` does the manager
start the DUT drivers and execution timer and enter `EXECUTION`.
`RESULTS_READY` means execution has stopped and Flash Manager has completely
finalised a valid result stream. `repeat` deliberately abandons that stream,
retains the active DUT configuration and uploaded instructions, and returns to
`ARMED`. `discard` abandons the stream, clears the active configuration, places
the DUT lifecycle into its idle state, and returns to `IDLE`. Both operations
require Flash Manager to release the result session and return to `IDLE` before
the RSM transition is committed.

The manager uses task notifications for requests and polls only while an
asynchronous Flash Manager transition is pending.

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

Preserving committed diagnostic results uses the normal finalisation and
transfer path. Abandoning them must use the explicit Flash Manager discard API;
the RSM never resets result buffers or assigns Flash state directly.


---

## Files

| File                      | Role |
|---------------------------|------|
| `run_state_manager.c` | RTOS lifecycle policy, Flash sequencing, and execution-clock ownership |
| `run_state_manager.h` | Public lifecycle request and status API |
| `dut_driver_lifecycle.c/.h` | DUT-facing driver configure/start/stop/idle/fault composition |

The Run State Manager decides when a transition is legal and sequences any
asynchronous prerequisites. The DUT Driver Lifecycle module performs only the
synchronous driver operations requested by the manager; it does not own run
state, the execution timer, Flash Manager transitions, or host transport.

The `test_configuration` module owns the committed active DUT configuration.
Application startup publishes a valid all-disabled configuration for inert
bring-up. On entry to `CONFIGURATION`, the Run State Manager first requires the
Logic Expander startup configuration to have physically completed, then copies
the active configuration and passes it to the DUT Driver Lifecycle module. The
lifecycle applies every channel, derives which channels should start, and
records which channels actually started so stop and rollback remain
deterministic. Execution tick reads and writes remain instruction-scheduled
Execution Manager work.

External I2C configuration is temporarily forced disabled in the DUT lifecycle
because of the known I2C hardware fault.

## Integration ownership

The public request API is intentionally independent of the eventual transport
or execution implementation. The future Host Interface owns submission of:

- `RUN_STATE_MANAGER_RequestPackageReceive()` after a new package is accepted.
- `RUN_STATE_MANAGER_RequestConfiguration()` after the active configuration is
  validated and committed.
- `RUN_STATE_MANAGER_RequestExecution()` for a host-originated execute command.
- `RUN_STATE_MANAGER_RequestResultTransfer()` when host result retrieval begins.
- `RUN_STATE_MANAGER_RequestResultTransferComplete()` only after every result
  byte has been consumed and acknowledged.
- `RUN_STATE_MANAGER_RequestRepeat()` or
  `RUN_STATE_MANAGER_RequestDiscardResults()` according to host result policy.

The future Execution Manager owns submission of:

- `RUN_STATE_MANAGER_RequestExecutionComplete()` after normal instruction-stream
  completion.
- `RUN_STATE_MANAGER_RequestFault()` after an execution failure has been handed
  into task context. A separate ISR-safe handoff may notify that task, but the
  RSM request itself remains a task-context API.

A physical or DUT-originated execution trigger may also submit
`RUN_STATE_MANAGER_RequestExecution()` after system-level trigger arbitration.
That arbitration does not belong inside the RSM.

Status consumers should prefer `RUN_STATE_MANAGER_GetStatus()`, which captures
all RSM-owned observable fields in one critical-section snapshot. Individual
legacy getters remain available for simple single-field decisions.

For hardware bring-up, `run_state diagnostic_timer start` and
`run_state diagnostic_timer stop`
request direct execution-timer control through the Run State Manager task.
Timer start deliberately bypasses normal Flash preparation and DUT driver
startup, so it must not be used as the production test-execution path.


---

## Public API

The public API is declared in `run_state_manager.h`.
