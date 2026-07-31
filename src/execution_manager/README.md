# execution_manager

## Overview

The Execution Manager will be the single owner of deterministic per-tick execution at 100 Hz,
1 kHz, or 10 kHz. All mutable execution state, including the authoritative zero-based tick,
remains private to `execution_manager.c`.

Task-context code uses `EXECUTION_MANAGER_Start()`, `EXECUTION_MANAGER_Abort()`, and
`EXECUTION_MANAGER_Get_Status()` to control and observe the lifecycle. The execution timer ISR
uses the narrower `execution_manager_isr.h` integration header to call
`EXECUTION_MANAGER_Process_From_ISR()` exactly once per timer interrupt.

## Intended tick sequence

Each execution timer interrupt will eventually:

1. Capture inputs and drain asynchronous measurements for the completed interval.
2. Finalise and publish the completed tick result.
3. Advance the authoritative tick.
4. Fetch the prepared instruction for the new tick.
5. Apply fixed outputs and initiate scheduled communication operations.
6. Check for runtime or timing failure.
7. Complete after the configured number of ticks.

The private functions currently preserve this call order but intentionally remain deterministic
stubs. Instruction and result buffers, measurement capture, peripheral dispatch, result
publication, overrun detection, and peripheral safe-state handling are left as TODO boundaries.

## Files

| File | Role |
|---|---|
| `execution_manager.c` | Private lifecycle state and ordered ISR scaffold |
| `execution_manager.h` | Task-context lifecycle API |
| `execution_manager_isr.h` | Execution timer ISR integration API |
