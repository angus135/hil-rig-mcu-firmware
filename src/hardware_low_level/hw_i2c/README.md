# hw_i2c

## Overview

`hw_i2c` owns I2C3, I2C2, and FMPI2C1 register/DMA state. Master requests are
stored as complete transactions so several messages can be accepted during one
execution tick without sharing a mutable staging payload.

## Channels

- I2C3: external, interrupt transfers
- I2C2: external, interrupt or DMA transfers
- FMPI2C1: internal, interrupt transfers with `AUTOEND`

## Lifecycle

The two external channels follow the configure, start, and stop lifecycle.
`HW_I2C_Configure_Channel()` applies the mode, speed, address, and transfer-path
configuration but leaves the peripheral disabled. `HW_I2C_Start_Channel()`
enables the configured peripheral. Transfer-specific DMA requests and interrupt
sources remain under the existing transaction machinery and are armed only when
a transaction starts.

`HW_I2C_Stop_Channel()` succeeds only when the channel has no active or queued
transaction and the bus is idle. It disables the peripheral while retaining the
channel configuration, completed receive messages, and staged slave transmit
data. Both external channels expose the same lifecycle even though their
supported interrupt and DMA paths differ.

FMPI2C1 is internal infrastructure for the logic expanders. Its dedicated
configuration function initializes and starts it immediately; the public
external-channel start, stop, and state-query functions intentionally reject or
exclude it.

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

A master-receive enqueue also reserves its eventual byte count and one message
descriptor. The reservation includes completed unconsumed messages plus every
active or queued master receive (the active receive is already the queue head).
The call returns `HW_I2C_STATUS_BUSY` without changing the queue when either
future byte or descriptor capacity would be exceeded. Consuming a completed
message releases both forms of capacity.

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

## Timeout recovery

`HW_I2C_Recover_Channel()` is an explicit, non-blocking software recovery path.
It disables the channel I2C/DMA interrupts, stops DMA, requests STOP when a
transfer is active, discards active and queued master work and completed RX
messages, clears transient receive state, and reapplies the saved channel
configuration. A generic transfer error is latched for the caller to consume.
Recovery preserves whether an external channel was started or stopped before
the recovery request. It does not wait for BUSY or perform physical stuck-bus
clock recovery.

## External master-receive tails

The I2C2/I2C3 interrupt path follows the STM32F446 legacy peripheral tail
sequences: one byte NACKs and requests STOP while clearing ADDR, two bytes use
POS and finish from BTF, and longer messages switch from RXNE to the final
three-byte BTF/RXNE sequence. I2C2 DMA receive enables LAST for the final DMA
transfer. ACK, POS, and LAST are cleared when the master receive completes or
is cleaned up so they cannot affect the next transaction.

Manual hardware check:

1. On I2C3 interrupt master receive, request lengths 1, 2, 3, and 16 bytes.
2. Repeat those lengths on I2C2 interrupt master receive.
3. Repeat those lengths on I2C2 DMA master receive.
4. For every case, verify exact data and length, STOP/idle completion, no
   latched transfer error, and a successful immediately following receive.

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
