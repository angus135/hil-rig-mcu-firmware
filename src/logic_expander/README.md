# logic_expander

## Overview

`logic_expander` holds OLATA/OLATB shadow state for the MCP23017 devices on
FMPI2C1. State and lookup tables are indexed by `LogicExpanderIndex_T`.
`LOGIC_EXPANDER_DIGITAL_OUTPUT_SELECT` is the known role at index/address
0/`0x20`; the remaining explicit indices are named unassigned until their
hardware roles are established.

Address and initial-state tables use designated initializers so the physical
role-to-address mapping is visible in code. `LOGIC_EXPANDER_COUNT` is the final
enum value and therefore also the state-array size.

## Task ownership and non-blocking configuration

The background task calls `LOGIC_EXPANDER_Init()` once before periodic
processing begins. This creates a module-owned mutex that serializes all public
task-context APIs used by background processing, configuration management, and
console commands. These APIs must not be called from an ISR. The mutex is held
only while inspecting state or making non-blocking queue calls; no API waits for
physical I2C completion.

`LOGIC_EXPANDER_Self_Config()` configures FMPI2C1 and begins enqueueing the eight
MCP23017 setup writes for each active device. It can return `BUSY` after writes
were accepted because queue acceptance is not bus completion.

Self-configuration is idempotent once the module is ready: repeated calls
return `OK` without resetting FMPI2C1, replacing shadow state, clearing dirty
bits, or enqueueing the setup writes again. Calls made while configuration is
in progress advance the existing operation. A failed operation can be retried;
the retry explicitly recovers the internal I2C channel before reinitializing
and queueing a fresh configuration sequence.

The background task calls `LOGIC_EXPANDER_Process()` every 10 ms. It resumes
partial queue submission, services deferred I2C progress, and observes physical
queue completion. The module becomes ready only after the final STOP and an `OK`
latched transfer result. A later asynchronous error leaves configuration not
ready. There is no CPU busy-retry loop. Explicit console calls are harmless
because they use the same mutex.

## Dirty shadow writes

`LOGIC_EXPANDER_Load_Control_Bit()` marks an expander dirty only when its shadow
byte actually changes. `LOGIC_EXPANDER_Send_Control_Bits()` submits OLATA/B only
for dirty active devices.

After a device's complete write is accepted, its dirty bit is cleared. If the
queue fills partway through a pass, accepted devices remain clean and the
current and remaining devices stay dirty, so a later call resumes without
duplicating accepted messages. A successful return means all dirty writes were
accepted, not that they have physically completed.

## Typical flow

1. Call `LOGIC_EXPANDER_Self_Config()`.
2. Call `LOGIC_EXPANDER_Process()` on subsequent ticks until it returns `OK`.
3. Load output changes with `LOGIC_EXPANDER_Load_Control_Bit()`.
4. Call `LOGIC_EXPANDER_Send_Control_Bits()`; retry later if it returns `BUSY`.
5. Use the low-level completion/result APIs, or the module process path, when
   physical completion matters.
