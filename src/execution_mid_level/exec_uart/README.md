# exec_uart

## Overview

`exec_uart` contains the mid-level UART driver used by the execution and
configuration layers.

This module sequences UART configuration, deconfiguration, transmit, receive,
and completion checks without directly owning UART hardware, DMA streams, or
hardware buffers. Hardware access remains inside the low-level `hw_uart` driver.

This module is responsible for:

- Applying UART channel configurations through the hardware layer.
- Stopping active RX before reconfiguration when required.
- Starting RX after configuration when RX is enabled.
- Applying a disabled configuration during deconfiguration.
- Queueing execution-layer TX payloads through the low-level TX ring buffer.
- Triggering the low-level TX DMA pump after TX data is queued.
- Copying unread RX data from low-level RX spans into caller-owned buffers.
- Reporting full TX completion through the low-level UART completion check.

---

## Files

| File | Role |
|------|------|
| `exec_uart.c` | Mid-level UART implementation |
| `exec_uart.h` | Public API for execution-layer UART operations |
| `tests/test_exec_uart.cpp` | Unit tests for exec UART sequencing and edge cases |

---

## Layering

`exec_uart` sits between higher-level execution/configuration code and the
low-level UART driver.

Higher layers call `exec_uart` when they need UART behaviour expressed in
execution terms, such as applying a configuration, transmitting a payload,
reading received bytes, or checking whether TX is complete.

`exec_uart` delegates hardware-specific work to `hw_uart`, including:

- UART peripheral setup.
- DMA setup.
- RX circular buffer ownership.
- TX ring buffer ownership.
- DMA interrupt handling.
- Electrical interface selection.

This keeps execution-layer code independent from direct UART register and DMA
management.

---

## TX Behaviour

`EXEC_UART_Transmit()` performs a combined transmit operation:

1. Queue the complete payload into the low-level TX ring buffer.
2. Trigger the low-level TX DMA pump.

Payload queueing is all-or-nothing. If the full payload cannot fit in the
low-level TX ring buffer, no bytes are queued and `EXEC_UART_Transmit()` returns
`false`.

New TX data may be queued while a previous DMA transfer is still active,
provided enough TX buffer space remains.

The maximum single payload size accepted by the exec UART API is:

```c
EXEC_UART_MAX_CHUNK_SIZE
```

This currently maps to:

```c
HW_UART_TX_BUFFER_SIZE
```

Larger logical UART outputs must be split by higher layers before calling
`EXEC_UART_Transmit()`.

---

## RX Behaviour

`EXEC_UART_Read()` copies unread RX data from the low-level driver into
caller-provided storage.

The low-level driver exposes unread RX data as one or two transient spans.
`exec_uart` copies up to the caller-provided destination size and then consumes
exactly the number of bytes copied.

If no unread data is available, `EXEC_UART_Read()` returns `true` and reports
`0` bytes read.

If the destination size is `0`, the function returns `true` without copying or
consuming data.

---

## Configuration Flow

`EXEC_UART_Apply_Configuration()` sequences configuration through the low-level
driver.

Typical behaviour:

1. Validate the channel and configuration pointer.
2. Stop RX if it is already running.
3. Apply the requested low-level UART configuration.
4. Start RX if the supplied configuration enables RX.
5. Store execution-layer lifecycle state.

`EXEC_UART_Deconfigure()` stops active RX if required, then applies a canonical
disabled UART configuration through the low-level driver.

---

## Public API

The public API is declared in `exec_uart.h`.

| Function | Purpose |
|----------|---------|
| `EXEC_UART_Apply_Configuration()` | Apply a UART channel configuration |
| `EXEC_UART_Deconfigure()` | Disable/deconfigure a UART channel |
| `EXEC_UART_Transmit()` | Queue a TX payload and trigger TX DMA |
| `EXEC_UART_Read()` | Copy unread RX data into caller storage |
| `EXEC_UART_Is_Tx_Complete()` | Report whether TX is fully complete |

---

## Completion Semantics

`EXEC_UART_Is_Tx_Complete()` reports full UART TX completion.

TX is considered complete only when the low-level UART driver reports that:

- The TX queue is empty.
- No TX DMA transfer is active.
- The UART has shifted out the final stop bit.

This is stronger than checking only whether DMA has consumed the source buffer.
