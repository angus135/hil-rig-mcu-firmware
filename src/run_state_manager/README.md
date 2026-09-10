# Run State Manager

## Overview

The Run State Manager is the RTOS task that owns the global HIL-RIG
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
enters `ARMED` only after every enabled driver is ready and the identified
Logic Expander configuration batch has physically completed. Driver, expander,
or bounded configuration-timeout failure enters `FAULT`.
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
Fault entry immediately inhibits execution, requests forced DUT-driver cleanup,
and requests asynchronous Flash session abort. Reset remains rejected until
both driver cleanup is acknowledged and Flash reports `IDLE`.

Every named event is validated by the Run State Manager task before it can
initiate a transition. A delivered notification is therefore not itself proof
that the lifecycle event was valid. `run_state status` reports the last event
evaluated as accepted, rejected for the current state, rejected while an
asynchronous transition was pending, or failed during its entry action.

| Console event | Required state | Result |
|---|---|---|
| `receive` | `IDLE` | Enter `TEST_PACKAGE_RECEIVE` |
| `configure` | `TEST_PACKAGE_RECEIVE` | Enter `CONFIGURATION`, then automatically `ARMED` on success |
| `execute` | `ARMED` | Start Flash preparation and DUT startup; enter `EXECUTION` only after both complete |
| `execution_complete` | `EXECUTION` | Stop execution and start Flash finalisation |
| `transfer` | `RESULTS_READY` | Enter `RESULT_TRANSFER` |
| `transfer_complete` | `RESULT_TRANSFER` | Enter `IDLE` |
| `repeat` | `RESULTS_READY` | Discard results, retain configuration/instructions, and enter `ARMED` |
| `discard` | `RESULTS_READY` | Discard results, clear configuration, and enter `IDLE` |
| `fault` | Any state | Enter `FAULT` and retain the first cause |
| `reset` | `FAULT` | Restore idle driver state and enter `IDLE` |

Driver and external-interface readiness complete `CONFIGURATION` into `ARMED`.
Flash preparation is followed by a distinct asynchronous DUT-startup phase;
only successful physical completion of its Logic Expander batch permits TIM4
to start and the RSM to enter `EXECUTION`. Flash finalisation completion into
`RESULTS_READY` is also automatic. Host-driven requests, future Execution
Manager completion, result transfer completion, fault, and reset remain
explicit events.

`ARMED` means test configuration has completed while the DUT drivers and
execution timer remain stopped. An execute request begins Flash Manager
execution preparation. After Flash reports `EXECUTING`, the manager starts the
DUT drivers but remains `ARMED` with a pending transition while external-path
enable writes complete. TIM4 starts and `EXECUTION` is published only after the
startup batch succeeds. Startup failure or timeout enters `FAULT` without
starting TIM4.

Execution completion stops TIM4 first, then waits in an acknowledged
DUT-driver shutdown phase. The RSM remains in `EXECUTION` while graceful SPI,
DAC, UART, and CAN transmissions drain, and does not ask Flash Manager to finalise results
until every driver has stopped and the Logic Expander disable batch has
completed. A shutdown timeout enters `FAULT` and upgrades cleanup to forced
abort so an undrainable transaction, such as an SPI slave transfer with no
master clocks, cannot leave hardware started indefinitely. Reset is rejected
until DUT cleanup is acknowledged and Flash Manager is `IDLE`.

`RESULTS_READY` means execution has stopped and Flash Manager has completely
finalised a valid result stream. `repeat` deliberately abandons that stream,
retains the active DUT configuration and uploaded instructions, reapplies the
complete configuration, waits for asynchronous frontend writes to complete,
and then returns to `ARMED`. This restores driver-owned initial output
conditions such as PWM timer values and DAC codes. `discard` abandons the stream, clears the active configuration, places
the DUT lifecycle into its idle state, and returns to `IDLE`. Both operations
require Flash Manager to release the result session and return to `IDLE` before
the RSM transition is committed.

The manager uses task notifications for requests and polls only while an
asynchronous configuration, driver-start, driver-shutdown, or Flash Manager operation is
pending. Configuration is resumable: if the Logic Expander/I2C queue is full,
the lifecycle retains the copied configuration, leaves the RSM in
`CONFIGURATION`, and retries after the background service drains queued writes.
The RSM configuration timeout bounds both queue backpressure and persistent
driver errors.

Request APIs report only whether their notification was delivered to the RSM
task. They do not report that the request was valid or that its transition has
completed. Notifications are coalescing event bits, not a FIFO command queue.
When several different bits arrive together, the RSM evaluates them in its
documented internal priority order and returns the remaining bits to itself;
submitting dependent lifecycle requests back-to-back is therefore invalid.
Callers must wait for the expected state with `transition_pending == false`
before submitting the next dependent request. The final decision is available
through `last_request` and `last_request_result` in
`RUN_STATE_MANAGER_GetStatus()`.

The same status snapshot provides transition timing diagnostics. While an
accepted lifecycle request is still progressing, `request_timing_active`,
`timed_request`, and `timed_request_elapsed_ms` report its total elapsed time.
After the terminal state is published, `last_completed_request` and
`last_transition_duration_ms` retain the most recent completed measurement.
The timing spans all asynchronous prerequisites. For example, `execute` covers
Flash preparation, driver startup, Logic Expander batch completion, and entry
to `EXECUTION`; `execution_complete` covers graceful driver shutdown and Flash
result finalisation through `RESULTS_READY`. These values are observational and
do not alter transition scheduling or timeout policy.

## Execution lifecycle

Before a run, request Flash Manager execution preparation and wait for
`FLASH_MANAGER_STATE_EXECUTING`. Only then may TIM4 start. At normal completion,
stop TIM4, ensure the ISR has returned and all result leases are resolved, then
request result finalisation and wait for `RESULTS_READY` or `FAULT`.

Instructions are ordered by timestamp. If the Execution Manager observes an
instruction timestamp less than the current tick, the workload has overrun its
deadline and the test is infeasible. The late instruction is not consumed. The
Execution Manager reports the fault through the ISR-safe RSM handoff. This
synchronously latches execution abort, so TIM4 skips later dispatches while the
RSM task stops the timer and records the fault. Future feasibility analysis
should reject such a workload before execution.

Flash Manager registers its task-side and ISR-side fault handoff with the RSM
at startup. Any Flash fault uses the same abort-first path. Notification latency
may delay task-context timer, driver, and Flash cleanup, but cannot permit a
subsequent TIM4 execution callback after the abort latch is set.

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
| `../test_configuration/` | Active validated DUT configuration storage |

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

The RSM does not construct or modify a test configuration. During current
bring-up, `test_config ...` console commands construct and commit it. In the
production path, the Host Interface will translate a validated, versioned test
package and call `TEST_CONFIGURATION_Commit()` before requesting
`CONFIGURATION`. The RSM snapshots that committed value once on configuration
entry. A later commit must not mutate a configured or executing test.

## Integration ownership

The public request API is intentionally independent of the eventual transport
or execution implementation. The future Host Interface owns this sequence:

- Submit `RUN_STATE_MANAGER_RequestPackageReceive()` when package reception is
  accepted, then wait for `TEST_PACKAGE_RECEIVE`.
- Receive and validate the complete package, upload its canonical instruction
  stream through Flash Manager, translate its DUT configuration, and publish
  that configuration using `TEST_CONFIGURATION_Commit()`.
- Submit `RUN_STATE_MANAGER_RequestConfiguration()` only after both package
  products are complete, then wait for `ARMED` with no pending transition.
- `RUN_STATE_MANAGER_RequestExecution()` for a host-originated execute command.
- Submit `RUN_STATE_MANAGER_RequestResultTransfer()` from `RESULTS_READY`, wait
  for `RESULT_TRANSFER`, and retrieve bytes using the Flash Manager result API.
- Submit `RUN_STATE_MANAGER_RequestResultTransferComplete()` only after Flash
  Manager reports end-of-stream and the host protocol has accepted all bytes.
- Alternatively, from `RESULTS_READY`, submit
  `RUN_STATE_MANAGER_RequestRepeat()` to abandon the current results and return
  to `ARMED`, or `RUN_STATE_MANAGER_RequestDiscardResults()` to abandon them
  and return to `IDLE`. These are alternatives to transfer in the current state
  table and are not valid after transfer completion.

Successful result-transfer completion currently returns directly to `IDLE`.
If the host protocol later requires “transfer results, then rerun the same
test,” that needs an explicit state-transition policy change; callers must not
approximate it with a late repeat request.

The future Execution Manager owns this execution-side integration:

- Perform only bounded instruction dispatch and result production from TIM4
  ISR context; the RSM continues to own TIM4 configuration, start, and stop.
- Hand normal completion or failure from the ISR to Execution Manager task
  context using an ISR-safe primitive.
- Submit `RUN_STATE_MANAGER_RequestExecutionComplete()` from task context after
  normal completion.
- Submit `RUN_STATE_MANAGER_RequestFault(reason)` from task context after an
  execution failure. The RSM request API itself is not ISR-safe.

The Execution Manager must not directly change RSM state, start or stop TIM4,
start or finalise Flash sessions, or start and stop DUT drivers. Those remain
consequences of RSM transitions.

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
