# hw_i2c

## Overview

`hw_i2c` owns I2C3, I2C2, and FMPI2C1 register/DMA state. Master requests are
stored as complete transactions so several messages can be accepted during one
execution tick without sharing a mutable staging payload.

## Channels

- I2C3: external, interrupt transfers
- I2C2: external, interrupt or DMA transfers
- FMPI2C1: internal, interrupt transfers with `AUTOEND`

## Master transaction queue

Each channel has `HW_I2C_MASTER_TRANSACTION_QUEUE_DEPTH` (currently 8) fixed
slots. A slot contains the transfer kind, seven-bit target address, length, and
`HW_I2C_TX_MAX_MESSAGE_SIZE` (256) bytes of driver-owned TX storage. On the
current ABI a slot is 264 bytes, so the slot array costs 2,112 bytes per channel,
plus queue indices and state flags.

`HW_I2C_Enqueue_Master_Transmit()` and
`HW_I2C_Enqueue_Master_Receive()` validate and publish a complete request under
the channel's I2C/DMA IRQ critical section. TX payload bytes are copied before
the tail and count are advanced. `HW_I2C_STATUS_OK` means accepted into the
queue; it does not mean the bus transaction is complete. Master TX payloads
must contain at least one byte; address-only writes are not implemented.

The active transaction remains the queue head until bus completion:

- DMA transfer-complete only marks the data movement complete.
- I2C2/I2C3 master TX also requires BTF, STOP generation, and an idle bus.
- FMPI2C1 `AUTOEND` requires STOPF and an idle bus.
- `HW_I2C_Service_Transaction_Queue()` performs deferred normal-context head
  removal and starts the next entry without polling BUSY in an ISR.

`HW_I2C_Is_Transaction_Queue_Complete()` is true only when there is no queued or
active transfer, the peripheral is idle, and the final completion condition was
observed. NACK, arbitration loss, bus error, timeout, overrun, or DMA error
latches a failure, aborts the active entry, flushes the remaining queue, and
returns the peripheral to idle. Read and clear that result with
`HW_I2C_Get_And_Clear_Transfer_Result()`.

For external slave transmit, the master's terminal NACK is normal transaction
completion rather than a transfer error. The response remains active until the
following STOP is observed.

FMPI2C1 has an eight-bit `NBYTES` field, so one internal transaction is limited
to 255 bytes. Reload/TCR and repeated-start grouping are not implemented.

## Receive messages

The 512-byte RX ring has a parallel fixed-depth descriptor queue. A descriptor
records transfer kind, complete length, master target address (zero for slave
RX), and status. Interrupt and DMA receive paths use a private linear staging
buffer; partial data is never published.

On STOP/completion, the driver atomically checks space, copies the complete
message into the byte ring, and publishes its descriptor. Early STOP on a DMA
slave receive uses configured length minus the DMA remaining count. If either
the byte ring or descriptor queue lacks room, the complete message is rejected
and overflow is latched.

Use `HW_I2C_Peek_Received_Message()` and
`HW_I2C_Consume_Received_Message()` to retrieve one complete transaction. The
legacy byte-oriented peek/consume names remain as compatibility wrappers, but
they expose and consume only the next complete message.

## Compatibility and deferred work

The stage-buffer/trigger API remains for non-queued slave responses and old
callers. Slave TX/RX stays single-active and returns busy rather than replacing
an active transfer. Queued slave responses, repeated starts, grouped
transactions, and configurable STOP behavior are deliberately deferred.
