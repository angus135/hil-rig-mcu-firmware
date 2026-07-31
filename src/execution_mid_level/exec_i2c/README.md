# exec_i2c

## Overview

`exec_i2c` validates the two external-channel configurations and provides the
execution-facing wrapper around `hw_i2c` queues and complete receive messages.
It does not manipulate peripheral registers.

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

- I2C3 is interrupt-only.
- I2C2 supports interrupt and DMA paths.
- Addresses are seven-bit.
- Low-level per-message and FMPI2C1 length limits apply.
