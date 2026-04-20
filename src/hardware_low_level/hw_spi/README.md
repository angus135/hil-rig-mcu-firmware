# hw_spi
## Overview

`hw_spi` contains the low-level application code for handling SPI communication using DMA-backed
receive and transmit paths.

This module is responsible for:

- configuring supported SPI peripherals with the requested mode, clock settings, bit order, baud
  rate, and data size
- starting and stopping the runtime SPI/DMA behaviour used by the driver
- capturing received SPI data continuously into an internal RX buffer
- exposing unread received data through a peek/consume interface
- buffering outgoing TX data into an internal queue
- starting DMA-based transmission of queued TX data and continuing queued transmission through the
  TX DMA completion IRQ path

`hw_spi` exists to separate raw SPI hardware movement from higher-level protocol logic. In the
HIL-RIG, higher-level modules may need to communicate with different SPI devices or subsystems
without each one directly managing SPI registers, DMA streams, buffer ownership, or IRQ-driven
continuation logic. This module provides a generic transport-style SPI layer so higher-level code
can focus on what data should be exchanged and when, rather than how bytes are moved.

The driver is intentionally mode-agnostic at the public data-path level. It does not define
messages, packet boundaries, application transactions, logging semantics, or device-specific chip
select behaviour beyond the configured peripheral mode. Those responsibilities belong to the layer
above this module.

---

## Design Summary

`hw_spi` provides two complementary data paths:

1. **Continuous RX path**
   - used to capture incoming SPI data into an internal DMA-backed circular buffer
   - RX data can be inspected without copying through a peek interface
   - higher-level software later advances the consume position once data has been processed
   - this keeps the receive side lightweight and avoids unnecessary copying in time-sensitive paths

2. **Queued TX path**
   - used to buffer outgoing TX data into an internal software queue
   - queued data is transmitted using DMA when explicitly triggered
   - if more TX data is appended while a transfer is already in progress, the TX DMA completion IRQ
     handler can continue transmitting the remaining queued data
   - this allows higher-level code to append raw data without directly managing SPI TX DMA state

This split lets the module support generic SPI byte movement while remaining simple from the
perspective of the software above it.

---

## Files

| File       | Role |
|------------|------|
| `hw_spi.c` | Public API implementation and low-level buffer/DMA handling |
| `hw_spi.h` | Public API header |

---

## Key Behaviour

### DMA-backed receive model

The RX side of the driver is designed as a continuous stream capture path. Once a channel is
started, incoming SPI data is written by DMA into an internal circular RX buffer.

Higher-level software does not directly read from the SPI peripheral or manage DMA descriptors.
Instead, it asks the driver for the unread region of the RX buffer. Because the unread region may
wrap around the end of the circular buffer, the module exposes it as one or two spans.

This approach keeps RX overhead low and avoids forcing higher-level code to manage a second copy of
the received data unless it actually wants to.

### Peek/consume receive interface

The receive API follows a peek/consume model:

- **peek** gives higher-level code a view of the unread RX data currently present in the internal
  buffer
- **consume** advances the driver's software read position once that unread data has been processed

This pattern makes the driver suitable for use in systems where the higher-level logic wants to
parse or inspect incoming data incrementally while leaving the low-level buffer ownership inside the
driver.

### Queued transmit model

The TX side of the driver uses an internal linear software queue. Higher-level code copies outgoing
data into this queue using the load function. Transmission does not begin immediately when data is
loaded. Instead, a separate trigger function starts the TX DMA transfer when appropriate.

If additional TX data is appended while a DMA transfer is already active, the current transfer is
allowed to complete first. The TX DMA completion IRQ handler then checks whether more queued data
remains and, if so, re-arms DMA to continue from the next unread portion of the queue.

This design keeps the TX interface simple while still allowing queued back-to-back transfers without
requiring the higher layer to manually restart DMA after every chunk.

### Byte-oriented public interface with frame-aware internals

The public RX and TX interfaces are byte-oriented. Higher-level software interacts with the driver
using byte buffers and byte counts regardless of whether the underlying SPI channel is configured
for 8-bit or 16-bit transfers.

Internally, the driver must still respect the configured SPI frame size when programming DMA
transfer lengths and interpreting DMA progress. In 16-bit mode, the software-facing API remains
byte-based, but the data passed into or consumed from the module must remain aligned to whole SPI
frames.

This allows the API to remain generic while still supporting both common SPI data-size
configurations.

---

## How it fits into the system

`hw_spi` is intended to be the low-level SPI transport layer for the HIL-RIG firmware.

It is **not** responsible for:

- deciding what constitutes a complete message
- parsing received bytes into protocol fields
- structuring transmitted bytes into device-level commands
- deciding when a channel should transmit as part of a larger execution schedule
- implementing device-specific semantics on top of SPI

Those concerns belong to the mid-level or higher-level driver that sits above this module.

This separation keeps the low-level SPI implementation reusable and focused on deterministic data
movement, while allowing higher-level modules to specialise behaviour for particular DUT interfaces
or application protocols.

---

## Usage Notes

- A channel should be configured before it is started or used.
- RX is intended to operate as a continuously running DMA-backed capture path.
- The caller must consume RX data often enough to avoid unread data being overwritten by continued
  DMA activity.
- TX data is copied into an internal queue before transmission, so the caller does not need to keep
  the original source buffer alive after a successful load.
- The TX queue is linear rather than circular and is reset back to empty once all queued data has
  been transmitted.
- In 16-bit mode, queued TX sizes and RX consume sizes must remain aligned to whole SPI frames.
- This module does not perform byte swapping, message framing, or protocol interpretation.
- This module assumes the configured DMA stream mappings and IRQ flag handling match the actual
  hardware configuration.