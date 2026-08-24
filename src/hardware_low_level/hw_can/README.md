# hw_can

## Overview

`hw_can` is the low-level driver for the two classical bxCAN channels. It owns
peripheral timing and filter configuration, lifecycle control, interrupt
handling, software transmit and receive queues, transmission status, and
task-context recovery after terminal CAN errors.

The driver supports standard 11-bit identifiers and payloads of up to eight
bytes.

## Lifecycle

Each channel follows an explicit configure, start, and stop lifecycle:

1. `HW_CAN_Configure1()` or `HW_CAN_Configure2()` applies timing and filter
   configuration and leaves the channel stopped.
2. `HW_CAN_Start1()` or `HW_CAN_Start2()` starts the peripheral and enables its
   runtime receive and error interrupts.
3. `HW_CAN_Stop1()` or `HW_CAN_Stop2()` stops an idle channel while retaining
   its configuration and software queue state.

Starting an unconfigured channel is rejected. Configuration while started is
also rejected. Stop returns busy while a buffered transmission or hardware
mailbox request is active.

The TX mailbox-empty interrupt source is enabled only while a buffered batch
needs service; it is not enabled merely by starting the channel.

## Reset and recovery

Reset and recovery are separate from the normal lifecycle:

- `HW_CAN_Reset1()` and `HW_CAN_Reset2()` clear software queues, transmission
  state, completion state, and receive-drop diagnostics.
- `HW_CAN_Recover1()` and `HW_CAN_Recover2()` operate only on a configured,
  started channel. Recovery discards failed and queued transmit work, stops and
  restarts the peripheral, and returns the transmit state to idle.

If recovery stops the peripheral but cannot restart it, the channel remains
configured but stopped.

## Channel API

The low-level API currently has separate channel 1 and channel 2 functions. A
future cleanup may replace these with channel-parameterized configuration,
lifecycle, and runtime functions.

## Files

| File | Role |
|---|---|
| `hw_can.c` | Driver implementation, queues, lifecycle, and interrupt handlers |
| `hw_can.h` | Public types and API |
| `tests/test_hw_can.cpp` | White-box unit tests |
