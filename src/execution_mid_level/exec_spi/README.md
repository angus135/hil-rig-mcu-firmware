# exec_spi

## Overview

`exec_spi` provides the execution-level SPI interface used by the HIL-RIG execution manager.
It sits above `hw_spi` and exposes a small API for configuring SPI channels, transmitting
packetised byte sequences, copying received bytes, and checking whether transmission has completed.

This module is intentionally lightweight. It does not implement protocol validation, message
parsing, device-specific behaviour, or execution scheduling. Those responsibilities belong to the
validation, scheduling, and device/protocol layers above it.

For transmission, this module does carry packet boundary information from the execution layer down
to `hw_spi`. The actual chip-select handling is still owned by the low-level driver, but
`exec_spi` now loads each SPI packet separately so the low-level master TX path can frame each
packet as its own DMA-backed software-chip-select transaction.

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
- In master mode, every low-level TX load is one software-chip-select-framed SPI packet.
- `exec_spi` receives one contiguous data buffer plus an array of packet sizes, then calls the
  low-level load function once per packet.
- TX transmission begins only when the TX path is explicitly triggered.
- `exec_spi` triggers TX once after all packets for the call have been queued.
- TX is complete only when there are no queued bytes and no bytes currently owned by DMA.
- The public SPI data path is byte-oriented, including when the peripheral uses 16-bit frames.

---

## Public API

### `EXEC_SPI_Configure_Channel()`

```c
bool EXEC_SPI_Configure_Channel( SPIChannel_T peripheral,
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
bool EXEC_SPI_Transmit( SPIChannel_T peripheral,
                        const uint8_t* data_src,
                        const uint32_t* packet_sizes_bytes,
                        uint32_t num_packets );
```

Queues one or more SPI packets for transmission and triggers the low-level TX path once after all
packet loads succeed.

`data_src` points to one contiguous byte buffer containing all packets back-to-back.
`packet_sizes_bytes` points to an array of packet sizes. `num_packets` gives the number of entries in
that size array. Each size entry describes the next packet in `data_src`.

The source data is copied into the low-level TX queue, so the caller does not need to keep the source
buffer alive after a successful call. If any low-level packet load fails, this function returns
`false` and does not trigger transmission.

---

### `EXEC_SPI_Receive()`

```c
bool EXEC_SPI_Receive( SPIChannel_T peripheral,
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
bool EXEC_SPI_Is_Transmission_Complete( SPIChannel_T peripheral );
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

The transmit path is a thin wrapper around the low-level TX queue, but it now preserves packet
boundaries:

1. `EXEC_SPI_Transmit()` starts with `data_offset_bytes = 0`.
2. For each entry in `packet_sizes_bytes`, it calls `HW_SPI_Load_Tx_Buffer()` using
   `&data_src[data_offset_bytes]` and the current packet size.
3. After each successful load, it advances `data_offset_bytes` by that packet size.
4. If any packet load fails, the function returns `false` immediately and does not trigger TX.
5. If all packet loads succeed, it calls `HW_SPI_Tx_Trigger()` once.

The trigger-once behaviour is important for the 100 us execution tick. The execution layer may queue
several SPI packets at the start of a tick. Repeatedly triggering after each packet would add
unnecessary low-level IRQ masking and trigger checks. The low-level master TX path is responsible for
draining the queued packets after it has been kicked once.

In master mode, each call to `HW_SPI_Load_Tx_Buffer()` becomes one low-level master packet. The
low-level SPI driver then sends those packets one by one, asserting software CS for the packet,
starting DMA, waiting for final drain, deasserting CS, and then starting the next queued packet if
one is pending.

`exec_spi` does not track transaction state. Call `EXEC_SPI_Is_Transmission_Complete()` to check
whether the low-level TX path has finished sending all queued and in-flight bytes.

---

## Validation and Caller Responsibilities

`exec_spi` intentionally avoids broad validation so it can remain lightweight.

The caller is responsible for ensuring that:

- peripheral selections are valid
- channels are configured before use
- TX packet sizes fit in the low-level TX queue
- `packet_sizes_bytes` contains exactly `num_packets` valid entries
- the sum of all packet sizes does not exceed the actual length of `data_src`
- `num_packets` is non-zero when a transmit operation is expected
- RX destination buffers are sized for the expected receive data
- byte counts are aligned correctly for 16-bit SPI mode
- SPI mode, baud rate, CPOL, CPHA, data size, and bit order are appropriate
- the requested operation is valid for the current execution interval
- message framing and protocol semantics are handled elsewhere

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
const uint8_t tx_data[] =
{
    0x01U, 0x02U,        // Packet 0
    0xAAU, 0xBBU, 0xCCU, // Packet 1
    0x10U                // Packet 2
};

const uint32_t tx_packet_sizes[] =
{
    2U,
    3U,
    1U
};

bool accepted = EXEC_SPI_Transmit( SPI_CHANNEL_0,
                                   tx_data,
                                   tx_packet_sizes,
                                   3U );
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
- transmit failure on any packet does not trigger TX
- transmit success loads each packet separately and triggers TX once after all loads succeed
- packet offsets passed to `HW_SPI_Load_Tx_Buffer()` match the supplied packet-size array
- receive copies one or two RX spans correctly
- receive fails without consuming data if the destination buffer is too small
- TX completion returns the low-level TX-empty state

---

## Summary

`exec_spi` is a small execution-facing wrapper around the low-level SPI transport driver. It lets the
execution manager configure SPI channels, transmit packetised byte sequences, receive bytes, and
check TX completion without depending directly on the lower-level DMA-backed SPI implementation.
