# exec_can

## Overview

`exec_can` is the execution-level facade for the HIL-RIG classic-CAN driver.
It follows the configuration, load/trigger, and peek/consume composition used by
the SPI and UART execution modules while preserving complete CAN frames.

Configuration and recovery are cold-path operations. Normal transmit and receive
calls are non-blocking and use only fixed-allocation storage.

## Lifecycle

`EXEC_CAN_Configure_Channel()` stops an active or faulted channel, applies
timing, mode, retransmission, and filter configuration, then starts runtime
notifications. Execution state becomes active only after every stage succeeds.
Configure/start failure triggers best-effort low-level deconfiguration without
replacing the original failure result. Successful cleanup leaves the execution
channel unconfigured; failed cleanup is retained as a fault so a later
configure or recovery attempt can retry the physical shutdown.

Deconfiguration discards queued TX frames, unread RX frames, stored
configuration, and diagnostics. Recovery reapplies stored low-level
configuration after a bus fault and becomes active only when restart succeeds.

## Transmit and receive

`EXEC_CAN_Transmit()` passes the complete frame array to one all-or-nothing
`HW_CAN_Load_Tx_Buffer()` call, followed by exactly one trigger. It does not
loop through public low-level functions frame by frame. Every frame independently
selects its standard/extended ID, data/remote type, DLC, and payload.

The operation is asynchronous. Success means the batch was queued and service
was requested, not that arbitration or acknowledgement has completed. Callers
use completion plus the diagnostic status to distinguish idle from wire success.
Loading transfers ownership before triggering. If the trigger reports bus-off,
the complete batch remains in the software queue; callers recover the channel
and must not submit a duplicate copy.

`EXEC_CAN_Receive()` performs one low-level peek, copies no more than the
caller's frame capacity from the first and optional wrapped span, and consumes
exactly the copied count. A small destination capacity gives a direct work bound
when receive is called near the 100 us scheduler.

## Performance contract

The execution layer allocates no memory and performs no blocking waits.
Transmit is one batch load plus one deferred trigger. Receive is bounded by the
caller-provided capacity. HAL work remains confined to low-level cold paths;
CAN interrupt work remains in the bounded direct-register services in `hw_can`.

## Files

| File | Role |
|---|---|
| `exec_can.h` | Documented lifecycle, frame transfer, and status API |
| `exec_can.c` | Lifecycle and load/trigger plus peek/copy/consume composition |
| `tests/test_exec_can.cpp` | Focused low-level-seam lifecycle and transfer tests |
