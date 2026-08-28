# Execution Manager

## Overview

The Execution Manager owns deterministic, timer-driven test execution. Its hot
path runs in the TIM4 ISR and consumes prefetched instructions and produces
timestamped results exclusively through the Flash Manager's ISR-safe RAM APIs.
It never accesses NAND, waits on an RTOS object, or calls task-context APIs.

The current source remains a skeleton. Instruction dispatch, tick tracking,
Flash Manager wake propagation, and execution-fault reporting are not yet
implemented.

## Instruction contract

Instruction records are stored in nondecreasing timestamp order. For each tick:

1. Peek the next instruction through
   `FLASH_MANAGER_PeekNextInstructionFromISR()`.
2. If its timestamp is greater than the current tick, stop without consuming;
   the next tick receives the cached view.
3. If its timestamp equals the current tick, execute it, consume it once, and
   continue with the next instruction.
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


---

## Files

| File                      | Role |
|---------------------------|------|
| `execution_manager.c` | Timer ISR execution loop |
| `execution_manager.h` | Public API and Flash Manager integration contract |


---

## Public API

The public API is declared in `execution_manager.h`.
