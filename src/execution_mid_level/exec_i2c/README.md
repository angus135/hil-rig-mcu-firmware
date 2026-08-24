# exec_i2c

## Overview

`exec_i2c` owns the lifecycle and board-level configuration of the two
DUT-facing I2C channels. It delegates peripheral work to `hw_i2c` and controls
the external voltage and pull-up selection through `LOGIC_EXPANDER_I2C_AO`.
FMPI2C1 is internal Logic Expander infrastructure and never passes through this
module.

## Lifecycle and board control

Each external channel follows Configure, Start, and Stop independently.
Configure selects 3.3 V or 5 V and one of 1 kOhm, 2.2 kOhm, 4.7 kOhm, or
10 kOhm while keeping the pull-up disconnected. Start enables the HW peripheral
before connecting the selected pull-up. Stop disconnects the pull-up, restores
the deterministic 3.3 V/1 kOhm selection, and then stops HW while retaining the
configuration.

Channel 1 maps to I2C3 and always uses interrupt transfers. Channel 2 maps to
I2C2 and always uses DMA for TX and RX. Transfer-path selection is deliberately
not part of the execution API.

The Logic Expander port-A mapping is:

- Channel 1: V_SELECT=0, A0=1, A1=2, PULLUP_EN=3
- Channel 2: V_SELECT=4, A0=5, A1=6, PULLUP_EN=7

## Accepted versus complete

`EXEC_I2C_Master_Transmit_External()` and
`EXEC_I2C_Start_Master_Receive_External()` submit one complete request through
the atomic low-level enqueue API for I2C3 or I2C2. Their existing boolean return
value is preserved: `true` means the request and its full payload/expected
length were accepted into driver-owned queue storage.

Acceptance is not physical completion. Call
`EXEC_I2C_Service_Transaction_Queue()` from normal execution context and use
`EXEC_I2C_Is_Transaction_Queue_Complete()` when a tick must wait for all bus
work, including the final STOP and idle peripheral. Later NACK, bus, or DMA
failures are returned and cleared by
`EXEC_I2C_Get_And_Clear_Transfer_Result()`.

If a task-level deadline expires, `EXEC_I2C_Recover_Channel()` forwards the
low-level non-blocking recovery operation. Recovery intentionally discards the
channel's active and queued transactions and completed RX messages, then
reapplies the existing configuration and latches a generic error. The console
loopback uses this path and consumes that error before returning so a later
loopback can enqueue normally.

Slave transmit and receive retain their non-queued semantics.

Master-receive acceptance can return false/`BUSY` even when a transaction slot
exists if completed and already queued receives have reserved all future RX
byte or descriptor capacity. Capacity becomes available after completed
messages are consumed.

## Complete receive messages

`EXEC_I2C_Receive_Message_Copy_And_Consume()` returns exactly one low-level RX
descriptor and its bytes. Polling this API also services deferred queue progress,
so a queued master receive can be started once the bus becomes idle. It returns:

- `EXEC_I2C_STATUS_OK` after copying and consuming one complete message
- `EXEC_I2C_STATUS_NO_DATA` when no complete message is available
- `EXEC_I2C_STATUS_BUFFER_TOO_SMALL` with `required_length` when the destination
  cannot hold the next message

An undersized destination never receives a partial copy and the message remains
unconsumed. Wrapped byte-ring spans are combined only when they belong to that
same descriptor.

`EXEC_I2C_Receive_Copy_And_Consume()` remains for console compatibility. It
still treats no-data polling as successful with zero bytes, but now retrieves at
most one complete message and fails without consuming when storage is too small.

## Configuration constraints

- Addresses are seven-bit.
- Channel 1 is interrupt-backed and channel 2 is DMA-backed.
- Low-level per-message limits apply to the external channels.
