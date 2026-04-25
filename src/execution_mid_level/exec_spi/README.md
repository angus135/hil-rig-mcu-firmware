# exec_spi

## Overview

`exec_spi` provides the execution-level SPI interface used by the HIL-RIG execution manager.
It sits above `hw_spi` and exposes a small API for configuring SPI channels, transmitting raw bytes,
copying received bytes, and checking whether transmission has completed.

This module is intentionally lightweight. It does not implement protocol framing, transaction
validation, chip-select policy, message parsing, device-specific behaviour, or execution scheduling.
Those responsibilities belong to the validation, scheduling, and device/protocol layers above it.

---

## Files

| File | Role |
|------|------|
| `exec_spi.c` | Execution-level SPI wrapper implementation |
| `exec_spi.h` | Public execution-level SPI API |

---

## Relationship to `hw_spi`

`hw_spi` owns the low-level SPI and DMA behaviour. It handles SPI peripheral setup, passive RX DMA,
TX buffering, DMA-triggered transmission, RX peek/consume handling, and TX completion tracking.

`exec_spi` uses those low-level primitives without duplicating their implementation. The low-level
details that matter to this module are:

- RX data is captured by `hw_spi` into a DMA-backed circular buffer.
- Unread RX data may be exposed as one or two spans because the RX buffer can wrap.
- TX data is copied into the low-level driver's internal TX queue before transmission.
- TX transmission begins only when the TX path is explicitly triggered.
- TX is complete only when there are no queued bytes and no bytes currently owned by DMA.
- The public SPI data path is byte-oriented, including when the peripheral uses 16-bit frames.

---

## Public API

### `EXEC_SPI_Configure_Channel()`

```c
bool EXEC_SPI_Configure_Channel( SPIPeripheral_T peripheral,
                                 HWSPIConfig_T configuration );
```

Configures and starts an SPI channel for execution use.

If the channel is already active, it is stopped before being reconfigured and started again. This is
intended for setup or between test phases, not for the high-frequency execution hot path.

This function performs minimal state handling only. It does not validate whether the configuration is
appropriate for a particular test or protocol.

---

### `EXEC_SPI_Transmit()`

```c
bool EXEC_SPI_Transmit( SPIPeripheral_T peripheral,
                        const uint8_t* data_src,
                        uint32_t size_bytes );
```

Queues raw bytes for transmission and triggers the low-level TX path.

The source data is copied into the low-level TX queue, so the caller does not need to keep the source
buffer alive after a successful call. If the low-level TX load fails, this function returns `false`
and does not trigger transmission.

---

### `EXEC_SPI_Receive()`

```c
bool EXEC_SPI_Receive( SPIPeripheral_T peripheral,
                       uint8_t* data_dst,
                       uint32_t* size_bytes );
```

Copies all currently unread RX bytes into caller-owned storage and consumes those bytes from the
low-level RX buffer.

`*size_bytes` is used as the destination buffer capacity on entry. On success, it is updated to the
number of bytes copied. If the unread RX data is larger than the supplied buffer, the function
returns `false`, copies nothing, and consumes nothing.

The low-level RX buffer may wrap, so this function may copy from two spans internally. The caller
receives the data as one contiguous buffer.

---

### `EXEC_SPI_Is_Transmission_Complete()`

```c
bool EXEC_SPI_Is_Transmission_Complete( SPIPeripheral_T peripheral );
```

Returns whether the low-level TX path is empty.

A return value of `true` means there are no bytes waiting in the low-level TX queue and no bytes
currently owned by an active TX DMA transfer. This does not necessarily mean that a higher-level SPI
transaction or protocol exchange is complete.

---

## Receive Behaviour

The receive path wraps the low-level peek/consume model:

1. `EXEC_SPI_Receive()` calls `HW_SPI_Rx_Peek()`.
2. The unread RX data is returned as one or two spans.
3. If the caller's buffer is large enough, the spans are copied into `data_dst`.
4. `*size_bytes` is updated to the copied byte count.
5. `HW_SPI_Rx_Consume()` is called with the same byte count.

If the destination buffer is too small, no data is consumed. This prevents the caller from losing RX
bytes it did not receive.

---

## Transmit Behaviour

The transmit path is a thin wrapper around the low-level TX queue:

1. `EXEC_SPI_Transmit()` calls `HW_SPI_Load_Tx_Buffer()`.
2. If the load succeeds, it calls `HW_SPI_Tx_Trigger()`.
3. The low-level driver handles DMA transmission and any queued TX continuation.

`exec_spi` does not track transaction state. Call `EXEC_SPI_Is_Transmission_Complete()` to check
whether the low-level TX path has finished sending all queued and in-flight bytes.

---

## Validation and Caller Responsibilities

`exec_spi` intentionally avoids broad validation so it can remain lightweight.

The caller is responsible for ensuring that:

- peripheral selections are valid
- channels are configured before use
- TX sizes fit in the low-level TX queue
- RX destination buffers are sized for the expected receive data
- byte counts are aligned correctly for 16-bit SPI mode
- SPI mode, baud rate, CPOL, CPHA, data size, and bit order are appropriate
- the requested operation is valid for the current execution interval
- message framing, protocol semantics, and chip-select policy are handled elsewhere

`EXEC_SPI_Receive()` still checks the destination capacity before copying. This is a lightweight
safety guard, not a substitute for the validation subsystem.

---

## Example Usage

```c
HWSPIConfig_T configuration =
{
    .spi_mode  = SPI_MASTER_MODE,
    .data_size = SPI_SIZE_8_BIT,
    .first_bit = SPI_FIRST_MSB,
    .baud_rate = SPI_BAUD_352KBIT,
    .cpol      = SPI_CPOL_LOW,
    .cpha      = SPI_CPHA_1_EDGE,
};

bool configured = EXEC_SPI_Configure_Channel( SPI_CHANNEL_0, configuration );
```

```c
const uint8_t message[] = { 0x01U, 0x02U, 0x03U };

bool accepted = EXEC_SPI_Transmit( SPI_CHANNEL_0,
                                   message,
                                   sizeof( message ) );
```

```c
uint8_t  rx_buffer[32];
uint32_t rx_size_bytes = sizeof( rx_buffer );

bool received = EXEC_SPI_Receive( SPI_CHANNEL_0,
                                  rx_buffer,
                                  &rx_size_bytes );
```

```c
if ( EXEC_SPI_Is_Transmission_Complete( SPI_CHANNEL_0 ) )
{
    // The low-level TX queue and active TX DMA transfer are both empty.
}
```

---

## Testing Notes

Unit tests for this module should mock the `hw_spi` API and verify that `exec_spi` performs the
expected low-level calls.

Useful behaviours to test include:

- configuring an unconfigured channel calls configure then start
- reconfiguring an active channel calls stop before configure/start
- failed low-level configuration does not start the channel
- transmit failure does not trigger TX
- transmit success triggers TX after loading the TX buffer
- receive copies one or two RX spans correctly
- receive fails without consuming data if the destination buffer is too small
- TX completion returns the low-level TX-empty state

---

## Summary

`exec_spi` is a small execution-facing wrapper around the low-level SPI transport driver. It lets the
execution manager configure SPI channels, transmit bytes, receive bytes, and check TX completion
without depending directly on the lower-level DMA-backed SPI implementation.
