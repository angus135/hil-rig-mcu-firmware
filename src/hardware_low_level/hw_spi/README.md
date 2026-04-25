# hw_spi

## Overview

`hw_spi` contains the low-level application code for handling SPI communication using DMA-backed
receive and transmit paths.

This module is responsible for:

- configuring supported SPI peripherals with the requested mode, clock settings, bit order, baud
  rate, and data size
- starting and stopping the runtime SPI/DMA behaviour used by the driver
- arming RX DMA so received SPI frames are captured into an internal circular RX buffer
- exposing unread received data through a peek/consume interface
- buffering outgoing TX data into an internal circular software queue
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

The low-level driver performs only limited validation in runtime paths. Callers are expected to pass
valid peripherals, correctly aligned lengths, correctly configured channels, and buffers that fit
within the available queue space. This keeps the low-level code fast and avoids duplicating policy
checks that should normally be performed by the mid-level driver.

---

## Design Summary

`hw_spi` provides two complementary data paths:

1. **Continuous RX path**
   - arms DMA to capture incoming SPI frames into an internal circular buffer
   - RX data can be inspected without copying through a peek interface
   - higher-level software later advances the consume position once data has been processed
   - this keeps the receive side lightweight and avoids unnecessary copying in time-sensitive paths

2. **Queued TX path**
   - buffers outgoing TX data into an internal circular software queue
   - queued data is transmitted using DMA when explicitly triggered
   - each DMA transfer sends one contiguous span of the TX ring
   - if queued data wraps around the end of the TX ring, the TX DMA completion IRQ starts another
     DMA transfer from the wrapped span at the start of the buffer
   - if more TX data is appended while a transfer is already in progress, the TX DMA completion IRQ
     can continue transmitting the remaining queued data

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

### Passive RX DMA start

`HW_SPI_Start_Channel()` arms the RX DMA stream directly rather than using `HAL_SPI_Receive_DMA()`.
This is intentional.

For a 2-line SPI master, a HAL receive call can generate clocks because the master must drive SCK in
order to receive data. That is not the behaviour wanted by this low-level driver. In this design,
starting a channel should arm RX capture only. It should not create SPI traffic by itself.

The RX DMA path therefore:

- sets the RX DMA memory address to the internal RX buffer
- sets the RX DMA peripheral address to the SPI data register
- sets the DMA transfer length in DMA elements, not bytes
- enables the RX DMA stream
- enables the SPI RX DMA request
- enables the SPI peripheral

After this, RX DMA passively captures frames whenever SPI traffic occurs. The caller is responsible
for causing traffic at the correct time, usually by loading TX data and calling `HW_SPI_Tx_Trigger()`.

### Peek/consume receive interface

The receive API follows a peek/consume model:

- **peek** gives higher-level code a view of the unread RX data currently present in the internal
  buffer
- **consume** advances the driver's software read position once that unread data has been processed

The driver determines the DMA write position from the RX DMA stream's remaining transfer count. The
DMA count is expressed in DMA elements, so the driver converts it back to bytes before calculating
the unread region.

Because the RX buffer is circular, unread data can be split into two spans:

- a first span from the software read position to the end of the buffer
- a second span from the start of the buffer to the DMA write position

This pattern makes the driver suitable for systems where higher-level logic wants to parse or
inspect incoming data incrementally while leaving low-level buffer ownership inside the driver.

### Queued transmit model

The TX side of the driver uses an internal circular software queue. Higher-level code copies
outgoing data into this queue using `HW_SPI_Load_Tx_Buffer()`. Transmission does not begin
immediately when data is loaded. Instead, `HW_SPI_Tx_Trigger()` starts the TX DMA transfer when the
caller decides it is appropriate.

The TX ring has two byte counts:

- `tx_num_bytes_pending`: bytes waiting in the software ring that have not yet been handed to DMA
- `tx_num_bytes_in_transmission`: bytes currently owned by the active DMA transfer

This distinction is important. Once a contiguous TX span is handed to DMA, it is removed from the
pending software queue and tracked as in-flight. The memory occupied by the in-flight span is still
considered used until the DMA completion IRQ clears `tx_num_bytes_in_transmission`.

This means free space is calculated from both pending bytes and in-flight bytes. The driver must not
overwrite bytes currently owned by DMA, even though those bytes are no longer pending in the
software queue.

### TX ring wrapping

Normal STM32 DMA transfers operate on one contiguous memory region. They cannot transmit two
separate spans of a ring buffer in one transfer.

For that reason, when TX data wraps around the end of the circular buffer, the driver sends it in
multiple DMA transfers:

1. Start DMA from the current TX read position to the end of the TX buffer.
2. The TX DMA completion IRQ clears the in-flight count.
3. If more pending bytes remain, the IRQ starts another DMA transfer from index 0.

This keeps the TX buffer circular while still using normal one-shot DMA transfers.

### TX trigger and DMA completion

`HW_SPI_Tx_Trigger()` starts TX only if there are pending bytes and no TX DMA transfer is already
active. It also clears stale terminal-complete and transfer-error flags before enabling the next DMA
transfer.

Once a DMA transfer completes, the TX DMA IRQ handler:

- clears the active in-flight byte count
- checks whether more pending TX data remains
- starts the next contiguous TX span if required

The IRQ handler does not define message boundaries. It only continues moving raw bytes that have
already been queued by the higher-level driver.

### Byte-oriented public interface with frame-aware internals

The public RX and TX interfaces are byte-oriented. Higher-level software interacts with the driver
using byte buffers and byte counts regardless of whether the underlying SPI channel is configured
for 8-bit or 16-bit transfers.

Internally, the driver must still respect the configured SPI frame size when programming DMA
transfer lengths and interpreting DMA progress. In 16-bit mode, byte counts must remain aligned to
whole SPI frames. The driver converts between byte counts and DMA element counts when setting DMA
lengths or interpreting DMA progress.

This allows the API to remain generic while still supporting both common SPI data-size
configurations.

---

## DMA Configuration Assumptions

The driver assumes the generated hardware configuration matches the low-level driver model:

- RX DMA streams are configured for circular mode.
- TX DMA streams are configured for normal mode.
- RX DMA direction is peripheral-to-memory.
- TX DMA direction is memory-to-peripheral.
- Memory increment is enabled.
- Peripheral increment is disabled.
- DMA memory/peripheral data widths match the configured SPI data size.

RX is circular because it is a continuous capture path. TX is normal because each triggered TX DMA
transfer should send a finite contiguous span once. If TX DMA is configured as circular, the SPI
master can repeatedly transmit the same bytes and continue generating clocks.

---

## Validation and Caller Responsibilities

This is a low-level performance-oriented driver. It intentionally avoids extensive validation in
some runtime paths.

Higher-level software is responsible for ensuring that:

- channels are configured before being started or used
- invalid peripheral values are not passed into hot-path functions
- TX loads fit in the available queue space
- byte counts are aligned to the configured SPI frame size
- RX data is consumed often enough to avoid unread data being overwritten
- message framing and transaction boundaries are handled above this driver
- chip-select timing and device-specific semantics are handled above this driver

The low-level driver may still reject some invalid operations, especially where doing so protects
internal state, but it should not be treated as the primary validation layer for application-level
logic.

---

## How it fits into the system

`hw_spi` is intended to be the low-level SPI transport layer for the HIL-RIG firmware.

It is **not** responsible for:

- deciding what constitutes a complete message
- parsing received bytes into protocol fields
- structuring transmitted bytes into device-level commands
- deciding when a channel should transmit as part of a larger execution schedule
- implementing device-specific semantics on top of SPI
- performing broad runtime validation of higher-level driver decisions

Those concerns belong to the mid-level or higher-level driver that sits above this module.

This separation keeps the low-level SPI implementation reusable and focused on deterministic data
movement, while allowing higher-level modules to specialise behaviour for particular DUT interfaces
or application protocols.

---

## Usage Notes

- A channel should be configured before it is started or used.
- RX is intended to operate as a continuously running DMA-backed capture path.
- Starting a channel arms RX DMA but does not intentionally create SPI traffic.
- The caller must consume RX data often enough to avoid unread data being overwritten by continued
  DMA activity.
- TX data is copied into an internal circular queue before transmission, so the caller does not need
  to keep the original source buffer alive after a successful load.
- TX transmission begins only when `HW_SPI_Tx_Trigger()` is called and there is no active TX DMA
  transfer already in progress.
- Wrapped TX data is transmitted as multiple contiguous DMA spans.
- In 16-bit mode, queued TX sizes and RX consume sizes must remain aligned to whole SPI frames.
- This module does not perform byte swapping, message framing, or protocol interpretation.
- This module assumes the configured DMA stream mappings and IRQ flag handling match the actual
  hardware configuration.
