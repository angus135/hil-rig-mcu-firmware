# logic_expander

## Overview

`logic_expander` holds OLATA/OLATB shadow state for seven MCP23017 devices on
FMPI2C1. State and lookup tables are indexed by `LogicExpanderIndex_T`.

| Device | Address |
|---|---:|
| `LOGIC_EXPANDER_DI_1` | `0x20` |
| `LOGIC_EXPANDER_DI_2` | `0x21` |
| `LOGIC_EXPANDER_DO_1` | `0x22` |
| `LOGIC_EXPANDER_DO_2` | `0x23` |
| `LOGIC_EXPANDER_PWM_SPI` | `0x24` |
| `LOGIC_EXPANDER_UART_PWR` | `0x25` |
| `LOGIC_EXPANDER_I2C_AO` | `0x26` |

Address and initial-state tables use designated initializers so the physical
role-to-address mapping is visible in code. `LOGIC_EXPANDER_COUNT` is the final
enum value and therefore also the state-array size.

All seven role-mapped devices are enabled in
`LOGIC_EXPANDER_DEFAULT_ACTIVE_BITMASK` (`0x7F`). The module does not probe for
devices at runtime; the bitmask determines which role-indexed entries are
configured. Individual drivers own their expander, port, and bit mappings; this
module owns addressing, shadow state, and I2C submission.

## Task ownership and non-blocking configuration

Application startup calls `LOGIC_EXPANDER_Init()` and
`LOGIC_EXPANDER_Self_Config()` before the scheduler starts. Initialization
creates a module-owned mutex that serializes all public task-context APIs used
by background processing, configuration management, and console commands.
Self-configuration begins asynchronously; the background task completes it
through periodic processing after the scheduler starts. These APIs must not be
called from an ISR. The mutex is held only while inspecting state or making
non-blocking queue calls; no API waits for physical I2C completion.

`LOGIC_EXPANDER_Is_Ready()` has one deliberately narrow meaning: initial
MCP23017 register self-configuration completed successfully. It does not mean
that later OLAT control writes are idle, successful, or physically visible.
Lifecycle callers must track those writes with the control-batch API.

`LOGIC_EXPANDER_Self_Config()` configures FMPI2C1 and begins enqueueing the eight
MCP23017 setup writes for each active device. The final setup write applies the
role-specific safe-default table to OLATA and OLATB: connected controls select
their disabled state where available, otherwise their lowest-voltage state;
power/enable outputs are off; and unconnected bits are low. These states all
currently encode as zero, but the table may contain set bits where a peripheral
truth table requires them. The call can return `BUSY` after writes were accepted
because queue acceptance is not bus completion.

Self-configuration is idempotent once the module is ready: repeated calls
return `OK` without resetting FMPI2C1, replacing shadow state, clearing dirty
bits, or enqueueing the setup writes again. Calls made while configuration is
in progress advance the existing operation. A failed operation can be retried;
the retry explicitly recovers the internal I2C channel before reinitializing
and queueing a fresh configuration sequence.

The background task calls `LOGIC_EXPANDER_Process()` every 10 ms. It resumes
partial queue submission, services deferred I2C progress, and observes physical
queue completion. The module becomes ready only after the final STOP and an `OK`
latched transfer result. Configuration and control-write batches have a 100 ms no-progress
deadline. Every newly accepted transaction refreshes the deadline, so a large
multi-device batch may span multiple queue-service cycles without being treated
as stalled. If no further transaction can be accepted and the in-flight queue
does not complete for 100 ms, FMPI2C1 is recovered instead of waiting
indefinitely. A timed-out configuration remains not
ready and can be restarted with `LOGIC_EXPANDER_Self_Config()`. There is no CPU
busy-retry loop. Explicit console calls are harmless because they use the same
mutex.

## Dirty shadow writes

`LOGIC_EXPANDER_Load_Control_Bit()` marks an expander dirty only when its shadow
byte actually changes. `LOGIC_EXPANDER_Send_Control_Bits()` submits OLATA/B only
for dirty active devices.

After a device's complete write is accepted, its dirty bit is cleared and a
pending bit tracks it until the I2C queue physically completes. The submitted
OLATA/OLATB values are also retained separately from the live shadow state. If
the queue fills partway through a pass, accepted devices remain pending and the
current and remaining devices stay dirty, so a later explicit send resumes
without duplicating accepted messages.

When physical completion succeeds, `LOGIC_EXPANDER_Process()` clears the pending
devices. If completion reports an asynchronous error, only pending output writes
move to retry state. A later background tick resubmits their retained snapshots;
`BUSY` leaves the retry scheduled for another tick. A newer shadow change remains
dirty and is not sent by this automatic path. If a newer value is explicitly
accepted while an older write is pending, its snapshot supersedes the older one
for any later retry. A timed-out control-write batch follows the same retry path
after channel recovery. A successful send return therefore means all dirty
writes were accepted, not that they have physically completed.

## Tracked control batches

The DUT Driver Lifecycle brackets configuration and startup writes with
`LOGIC_EXPANDER_Begin_Control_Batch()` and
`LOGIC_EXPANDER_End_Control_Batch()`. Begin returns an opaque non-zero identity.
Only that identity can query or cancel the batch, preventing an old lifecycle
operation from mistaking a later batch for its own completion.

After End, the batch remains `PENDING` until background processing observes the
final successful I2C completion. It becomes `FAILED` if an accepted transaction
later fails or times out, and `COMPLETE` only when dirty, pending, and retry
work belonging to the sealed operation has drained. `Is_Ready()` may remain
true throughout this process because subsystem initialization and control-batch
completion are separate contracts.

Only one batch can be open or pending. Beginning another operation returns
`BUSY` until prior queued work drains. Cancelling releases the identity but does
not cancel transactions already owned by the I2C queue.

## Typical flow

1. Call `LOGIC_EXPANDER_Self_Config()`.
2. Call `LOGIC_EXPANDER_Process()` on subsequent ticks until it returns `OK`.
3. Confirm initial readiness with `LOGIC_EXPANDER_Is_Ready()`.
4. Begin a tracked control batch.
5. Load output changes and call `LOGIC_EXPANDER_Send_Control_Bits()`.
6. Seal the batch after all writes have been submitted.
7. Continue calling `LOGIC_EXPANDER_Process()` and poll the matching batch
   identity until it reports `COMPLETE` or `FAILED`.
