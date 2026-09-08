# Execution Manager

## Responsibility

The Execution Manager is the deterministic execution-clock boundary engine. It
owns the current tick, configured run length, instruction dispatch, first
execution failure, and the placement of future result production within each
boundary. The Run State Manager owns TIM4, the DUT driver lifecycle, and Flash
Manager session transitions.

`EXECUTION_MANAGER_Prepare(tick_count)` establishes tick zero while TIM4 is
stopped. Tick zero is the configured initial condition at execution time zero;
it is not processed by an interrupt. The first TIM4 interrupt processes tick
one. A run of N ticks therefore processes boundaries 1 through N and completes
at time `N / tick_rate_hz`.

## Tick and I/O semantics

One tick always means one execution-clock boundary. It is not shifted for
different peripheral types:

```text
timestamp X = the driver operation was performed at boundary X
boundary time = X / tick_rate_hz
```

At each boundary the Execution Manager advances the authoritative tick once,
collects measurements, and then applies or queues outputs. The tick remains
unchanged for the rest of that ISR. A tick-X measurement therefore observes
the system immediately before tick-X outputs are requested.

The common timestamp identifies the observation or request boundary. Driver
behaviour determines the relationship between that boundary and physical data:

| Driver operation | Timestamp meaning |
|---|---|
| Digital-input read | Instantaneous GPIO sample at the boundary |
| Analogue-input read | Boundary where the latest rolling DMA sample set was observed |
| UART or SPI receive | Boundary where accumulated unread bytes were copied and consumed |
| CAN receive | Boundary where queued completed frames were drained |
| PWM capture | Boundary where the latest completed capture was consumed |
| I2C receive | Boundary where a completed message was retrieved |
| Digital-output update | Boundary where the GPIO register update was issued |
| PWM update | Boundary where timer register writes were issued |
| DAC, UART, SPI, CAN, or I2C output | Boundary where asynchronous work was accepted or triggered |

Asynchronous result timestamps do not claim exact arrival time, and
asynchronous output timestamps do not claim exact wire or electrical completion
time. Drivers would need separate hardware event timestamps to provide that
information.

Measurement and result production are not implemented yet. When added, they
belong after the tick advance and before the existing output path; no
per-driver tick adjustment belongs in the ISR.

## ISR path

`EXECUTION_MANAGER_ProcessTickFromISR()` currently performs:

1. Return an already-latched terminal outcome without touching Flash Manager.
2. Reject invocation unless a run has been prepared.
3. Advance from the previous boundary to the current boundary exactly once.
4. Peek the next prepared instruction from Flash Manager RAM.
5. Leave a future instruction unconsumed.
6. Treat a past instruction as a timing failure without consuming it.
7. For an instruction due now, apply its operations in encoded order through
   the opcode adapter table, then consume the instruction once.
8. Complete after boundary `tick_count`; otherwise return `CONTINUE`.

End of the instruction stream is not completion because output-free ticks and
future measurement-only ticks continue until the configured run length.

The ISR does not copy complete instructions or fixed output payloads, access
NAND, wait on an RTOS object, calculate timer values, configure a peripheral,
or control TIM4. Flash
Manager exposes operations directly from aligned word storage. The operation
walker relies on the Host Interface to validate the canonical stream before it
is stored.

Flash Manager instruction consumption and terminal lifecycle notification
accumulate a FreeRTOS task-wake request supplied by the TIM4 handler. TIM4
performs one `portYIELD_FROM_ISR()` only after the complete boundary service
returns, allowing Flash Manager task work between ISR services without
interrupting a measurement/output boundary.

## Instruction and operation contract

One variable-length instruction contains all output operations for one
boundary:

```text
[8-byte instruction header][operation 0]...[operation N]
```

Valid instruction timestamps are 1 through the configured run tick count and
are strictly increasing. Timestamp zero is reserved for initial conditions and
must not be uploaded.

The instruction header contains the timestamp, encoded operation length,
operation count, and a zero reserved byte. Each operation starts with one
aligned 32-bit header word, followed by its payload and zero to three padding
bytes.

`DIGITAL_OUTPUT_UPDATE`, `ANALOGUE_OUTPUT_BATCH`, `PWM_UPDATE`,
`CAN_TRANSMIT`, `SPI_TRANSMIT`, and `UART_TRANSMIT` are currently dispatched.
The digital
output's zero-copy payload contains prepared physical HIGH and LOW masks. Its adapter uses the active-low-aware
digital-output driver. The PWM payload holds the precomputed ARR, CCR, and PSC
inputs produced during package processing; its adapter selects LV or HV and
forwards those values directly.

The analogue-output payload is a non-empty sequence of prepared three-byte DAC
wire frames. Its adapter passes the payload pointer and encoded byte length
directly to `EXEC_ANALOGUE_OUTPUT_Submit_Prepared_Batch()`. The analogue-output
driver copies the frames into SPI-DAC-owned DMA storage; the adapter performs no
additional copy, voltage conversion, or lifecycle work.

The CAN payload is a non-empty sequence of fixed 12-byte standard CAN packet
records. Its adapter derives the packet count from the payload length and calls
`EXEC_CAN_Transmit()` directly. The current CAN driver retains its existing
packet validation and driver-owned queue copy, so this path is intentionally
heavier than the other zero-copy adapters until CAN-specific ISR tightening is
completed.

The SPI adapter views the aligned payload as a packet-count prefix, a contiguous
`uint32_t` packet-size array, and the concatenated packet data. It passes those
views directly to `EXEC_SPI_Transmit()` without copying or revalidating the
prevalidated operation. The SPI driver atomically copies the complete batch
into its TX queue before Flash Manager storage can be reused.

The UART adapter passes the raw payload pointer and length directly to
`EXEC_UART_Transmit()`. The UART driver necessarily copies those bytes into its
DMA-owned TX ring before returning, because Flash Manager storage becomes
reusable after instruction consumption. The adapter performs no additional
copy and retains no Flash Manager pointer. A driver refusal maps to operation
rejection and terminates the run; schedule feasibility checks are deferred.

PWM register writes request an update at the instruction boundary without
resetting timer phase or forcing an update event. With ARR/CCR preload enabled
and the prescaler buffered, the three values become active together at the
timer's next natural update event. A PWM timestamp therefore identifies the
register-write boundary, not an exact waveform-edge timestamp.

The complete instruction is consumed only after every encoded operation is
accepted. Earlier physical effects cannot be rolled back if a later operation
rejects, so upload validation and feasibility admission must make runtime
rejection exceptional and run-ending.

For asynchronous drivers, acceptance means the driver has copied or queued all
required data. The ISR never waits for physical completion, and a driver must
not retain a pointer into Flash Manager instruction storage.

## Lifecycle integration

The Run State Manager prepares Flash Manager, prepares the Execution Manager,
starts configured DUT drivers, and finally starts TIM4. On completion or first
failure, the Execution Manager invokes the registered terminal callback once.
That callback immediately inhibits later dispatch and notifies the RSM task.
Task context then stops TIM4 and drivers and requests Flash finalisation or
abort.

`EXECUTION_MANAGER_Abort()` deactivates execution only after TIM4 has stopped.
It deliberately retains the last processed tick and first failure for post-run
diagnostics. The next `EXECUTION_MANAGER_Prepare()` resets those values and all
other run-local state.

## Public interfaces

- `execution_manager.h`: task-context preparation, abort, tick, and failure API.
- `execution_manager_isr.h`: narrow per-boundary ISR API.
- `execution_instruction.h`: canonical per-tick instruction header.
- `execution_operation_payloads.h`: canonical operation and payload layouts.
- `execution_operation_adapters.h`: zero-copy operation dispatch.
