# exec_can

## Overview

`exec_can` provides the subsystem-facing interface for the two classical CAN
channels. It owns execution-layer lifecycle state, validates public channel and
packet arguments, maps execution channels to the current channel-specific HW
CAN API, and translates HW results into execution-layer results.

The public packet API supports standard 11-bit CAN identifiers and payloads of
up to eight bytes.

## Lifecycle

Each channel follows an explicit configure, start, and stop lifecycle:

1. `EXEC_CAN_Configure_Channel()` applies an enabled channel configuration and
   leaves the channel stopped.
2. `EXEC_CAN_Start_Channel()` starts a successfully configured channel.
3. `EXEC_CAN_Stop_Channel()` stops a running channel while retaining its
   configuration.

Passing `is_enabled = false` to configuration stops the selected channel when
necessary and clears its execution-layer configured state. CAN transceiver
safe-state control remains a hardware-bring-up TODO until the board control
mapping is confirmed.

`EXEC_CAN_Is_Channel_Configured()` and `EXEC_CAN_Is_Channel_Started()` report
cached execution-layer lifecycle state. They do not access hardware during
normal operation.

## Runtime operations

`EXEC_CAN_Transmit()` validates and converts a complete packet batch, loads the
corresponding HW queue, and triggers that batch. `EXEC_CAN_Receive()` converts
received HW packets into execution-layer packet storage. These runtime paths do
not add lifecycle checks.

Transmit status and receive-drop diagnostics are exposed through
`EXEC_CAN_Get_Tx_Status()` and `EXEC_CAN_Get_Rx_Dropped_Count()`.

## Recovery

`EXEC_CAN_Recover()` requires a configured, started channel and delegates the
actual controller recovery to HW CAN. Recovery can discard failed and queued
transmit work. If recovery fails after partially changing the HW lifecycle,
exec CAN synchronizes its cached lifecycle state with HW CAN.

## Files

| File | Role |
|---|---|
| `exec_can.c` | Lifecycle, routing, conversion, and result mapping |
| `exec_can.h` | Public execution-layer types and API |
| `tests/test_exec_can.cpp` | White-box unit tests |
