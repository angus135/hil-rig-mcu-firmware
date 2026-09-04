# Execution Manager

## Overview

The Execution Manager owns deterministic, timer-driven test execution. Its hot
path runs in the TIM4 ISR and consumes prefetched instructions and produces
timestamped results exclusively through the Flash Manager's ISR-safe RAM APIs.
It never accesses NAND, waits on an RTOS object, or calls task-context APIs.

The current source remains a skeleton. Instruction dispatch, tick tracking,
Flash Manager wake propagation, and execution-fault reporting are not yet
implemented.

The current `run_state execution_complete` and `run_state fault` console
commands are temporary stimuli for the integration seam described below.

## Instruction contract

Instructions are variable length and stored in strictly increasing timestamp
order, with at most one instruction for each output-bearing tick. A fixed
`ExecutionInstructionHeader_T` is followed by all packed operations for that
tick, up to `EXECUTION_INSTRUCTION_MAX_SIZE_BYTES`. For each tick:

1. Peek the next instruction through
   `FLASH_MANAGER_PeekNextInstructionFromISR()`.
2. If its timestamp is greater than the current tick, stop without consuming;
   the next tick receives the cached view.
3. If its timestamp equals the current tick, execute all contained operations
   in order and consume the complete instruction once.
4. If its timestamp is less than the current tick, report an execution-overrun
   fault. The test is infeasible and the late instruction is not consumed.

`FLASH_MANAGER_INSTRUCTION_END_OF_STREAM` does not by itself end the test,
because a later measurement may still be required.

## Result contract

Peripheral DMA fills driver-owned buffers asynchronously. During the execution
ISR, the selected driver synchronously copies a stable measurement into a Flash
Manager result lease. The lease is committed or cancelled before ISR return;
DMA and drivers never retain its pointer.

One accumulated `BaseType_t` wake flag covers all consumes and commits in a
tick. The outer timer handler performs the single `portYIELD_FROM_ISR()` after
the complete execution sequence.

## Lifecycle constraints

The Run State Manager exclusively owns TIM4 configuration, start, and stop.
TIM4 may start only after the Run State Manager observes
`FLASH_MANAGER_STATE_EXECUTING`. The Execution Manager owns only the work
performed for each timer tick. Any underrun, corrupt instruction, result-buffer
exhaustion, commit failure, consume failure, or timestamp overrun must be
reported to the Run State Manager through an ISR-safe handoff.

## Run State Manager integration contract

The Execution Manager implementation should separate its ISR hot path from its
task-context lifecycle reporting:

1. The RSM observes Flash Manager `EXECUTING`, starts configured DUT drivers,
   starts TIM4, and only then publishes RSM `EXECUTION`.
2. Each TIM4 interrupt invokes a minimal Execution Manager ISR entry point. Any
   driver measurement/output call made from that entry point remains in ISR
   context and must obey the same bounded, non-blocking restrictions.
3. Normal completion or the first execution failure is latched locally and an
   ISR-safe notification is sent to Execution Manager task context. No RSM
   request function is called directly from the ISR.
4. The Execution Manager task submits
   `RUN_STATE_MANAGER_RequestExecutionComplete()` for normal completion or
   `RUN_STATE_MANAGER_RequestFault(reason)` for failure.
5. The RSM stops TIM4, stops the DUT lifecycle, and coordinates Flash result
   finalisation or abort. The Execution Manager does not perform those actions.

The Execution Manager may own execution-local state such as the current tick,
instruction dispatch bookkeeping, completion detection, and the first local
failure. It must not own global run state, Flash session state, DUT driver
lifecycle state, or the execution-clock lifecycle.

Because RSM requests are task notifications rather than FIFO messages, the
Execution Manager must latch completion/fault exactly once and wait for the RSM
to leave `EXECUTION`; it must not repeatedly submit the same event on every
task iteration.


---

## Files

| File                      | Role |
|---------------------------|------|
| `execution_manager.c` | Timer ISR execution loop |
| `execution_manager.h` | Public API and Flash Manager integration contract |
| `execution_instruction.h` | Prepared instruction format shared with its producers and storage |


---

## Public API

The public API is declared in `execution_manager.h`.
