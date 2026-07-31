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

## Non-blocking configuration

`LOGIC_EXPANDER_Self_Config()` configures FMPI2C1 and begins enqueueing the eight
MCP23017 setup writes for each active device. It can return `BUSY` after writes
were accepted because queue acceptance is not bus completion.

The existing 5 ms console task calls `LOGIC_EXPANDER_Process()` in normal
context. It resumes partial queue submission, services deferred I2C progress,
and waits for physical queue completion. The module becomes ready only after
the final STOP and an `OK` latched transfer result. A later asynchronous error
leaves configuration not ready. There is no CPU busy-retry loop. If expander
control is moved out of the console in a future build, that application context
must take ownership of this periodic call.

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
