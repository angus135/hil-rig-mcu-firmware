# hw_spi

## Overview

`hw_spi` is the low-level SPI transport driver used by the HIL-RIG firmware. It owns the STM32
SPI/DMA setup, internal RX/TX buffering, interrupt-driven TX continuation, and master-mode
software chip-select framing. Higher-level code should decide what data needs to be sent or parsed;
this module decides how bytes are moved through SPI with minimal runtime overhead.

The public API is intentionally small and byte-oriented:

- configure a logical SPI channel
- start or stop the channel
- inspect unread RX data with a peek/consume model
- queue TX data
- kick the TX engine

The driver supports both master and slave mode. The public TX API is shared, but the internal TX
behaviour is mode-specific:

- **master mode** treats every successful `HW_SPI_Load_Tx_Buffer()` call as one packet, sends that
  packet using one DMA transfer, and automatically frames it with software chip-select
- **slave mode** treats TX as a raw byte stream owned by an external SPI master, with no packet or
  chip-select semantics inside this driver

Runtime TX/RX paths deliberately avoid broad defensive validation. Configuration code performs the
setup-time checks and precomputes values used by hot paths. ISR, trigger, send, receive, and queue
operations assume the selected channel has already been configured correctly.

---

## File layout

| File | Role |
|---|---|
| `hw_spi.h` | Public API, public enums, public configuration structure, and RX span types |
| `hw_spi_internal.h` | Private state, hardware mappings, internal helpers, hot-path inline helpers, buffer sizes, and transaction state |
| `hw_spi_config.c` | Per-channel state storage, configuration, HAL SPI setup, DMA data width setup, and stop behaviour |
| `hw_spi_rx.c` | Passive RX DMA setup, RX peek/consume logic, and RX DMA IRQ placeholders |
| `hw_spi_tx_config.c` | Shared TX trigger logic, TX DMA IRQ handling, final-drain handling, software-CS hooks, timer callback handling, and TX fault handling |
| `hw_spi_tx_master.c` | Master-mode packet queueing and one-packet DMA start logic |
| `hw_spi_tx_slave.c` | Slave-mode byte-stream queueing and contiguous-span DMA start logic |
| `test_hw_spi_rx.cpp` | RX and RX-related unit tests |
| `test_hw_spi_tx_master.cpp` | Master TX, software-CS, final-drain, and queue-draining unit tests |
| `test_hw_spi_tx_slave.cpp` | Slave TX stream and wrapped-DMA-continuation unit tests |
| `hw_spi_mocks.h` | STM32/HAL/LL mock definitions used by the unit tests |

---

## Design goals

The driver is optimised for the HIL-RIG timing model, where multiple SPI-related operations may be
queued in a 100 us execution tick. The main goals are:

- keep the public API simple and stable
- avoid register/DMA manipulation in higher-level modules
- minimise work in ISRs and hot paths
- avoid repeated runtime validation where configuration already guarantees correctness
- support 8-bit and 16-bit SPI while keeping public buffers byte-oriented
- support continuous RX capture without generating unintended master clocks
- support master-mode packet framing using automatic software chip-select
- allow slave-mode TX to remain a raw stream controlled by the external master

The driver is a transport layer. It does not parse messages, define protocol fields, manage
application-level transaction semantics, or decide whether a particular device command is valid.

---

## Public API model

The public data path is intentionally mode-agnostic:

```c
bool HW_SPI_Configure_Channel( SPIChannel_T peripheral, HWSPIConfig_T configuration );
bool HW_SPI_Start_Channel( SPIChannel_T peripheral );
void HW_SPI_Stop_Channel( SPIChannel_T peripheral );

HWSPIRxSpans_T HW_SPI_Rx_Peek( SPIChannel_T peripheral );
void HW_SPI_Rx_Consume( SPIChannel_T peripheral, uint32_t bytes_to_consume );

bool HW_SPI_Load_Tx_Buffer( SPIChannel_T peripheral, const uint8_t* data, uint32_t size );
void HW_SPI_Tx_Trigger( SPIChannel_T peripheral );
```

The same TX load and trigger calls are used for master and slave mode, but the internal behaviour is
different after configuration:

- master mode selects the master packet queue path
- slave mode selects the slave byte-stream path
- configuration precomputes hot-path state so runtime code does not repeatedly derive frame size,
  timer selection, or final-drain strategy

---

## Configuration flow

`HW_SPI_Configure_Channel()` performs all slow setup-time work. It selects the logical channel's HAL
handle, SPI instance, DMA resources, DMA IRQ, and private state block. It then applies the requested
mode, data size, first-bit order, CPOL, CPHA, and baud-rate settings.

Important configuration behaviour:

- master mode uses software NSS because this driver frames master packets using GPIO-controlled
  software chip-select rather than the SPI peripheral's hardware NSS output
- slave mode uses hardware NSS input because the external SPI master owns transaction boundaries
- DMA memory/peripheral data widths are configured to match the selected SPI frame size
- TX state is reset so old queue or transaction state cannot leak across configurations
- final-drain timer configuration is prepared for master-mode channels
- hot-path fields are precomputed, including frame size, frame shift, logical peripheral, final
  drain timer, and whether timer-based final drain is needed

After configuration, runtime paths assume the state is valid. Invalid enum values and unsupported
configuration values are handled here, not repeatedly in the hot paths.

---

## Important state variables

Each logical SPI channel has one `SPIPeripheralState_T`. The most important fields are:

| State field | Meaning |
|---|---|
| `config` | Last public configuration applied to the channel |
| `rx_buffer` | Circular DMA-backed receive buffer |
| `rx_position` | Software read/consume index into `rx_buffer`, in bytes |
| `tx_buffer` | Internal transmit storage used by both master and slave TX paths |
| `tx_write_position` | Next byte index where newly loaded TX data will be copied |
| `tx_read_position` | Next byte index to hand to TX DMA |
| `tx_num_bytes_pending` | Bytes queued in software but not yet handed to DMA |
| `tx_num_bytes_in_transmission` | Bytes currently owned by an active TX DMA transfer |
| `tx_transaction_state` | Master-mode electrical transaction state |
| `tx_packet_descriptors` | Master-mode packet descriptor queue |
| `tx_packet_write_position` | Next descriptor slot to fill in master mode |
| `tx_packet_read_position` | Next descriptor slot to send in master mode |
| `tx_num_packets_pending` | Number of queued master packet descriptors |
| `rx_dma`, `tx_dma` | DMA controller instances for the channel |
| `rx_dma_stream`, `tx_dma_stream` | DMA stream identifiers |
| `spi_peripheral` | SPI register block used by the channel |
| `tx_dma_irqn` | TX DMA IRQ used for load/trigger critical sections |
| `logical_peripheral` | Logical channel enum cached for hot paths and callbacks |
| `frame_size_bytes` | Precomputed frame size: 1 for 8-bit, 2 for 16-bit |
| `frame_shift` | Precomputed byte/element shift: 0 for 8-bit, 1 for 16-bit |
| `is_master` | Precomputed mode flag used by hot paths |
| `final_drain_timer` | Timer assigned to this channel's slow-baud final-drain delay |
| `final_drain_uses_timer` | Whether this baud rate should use the timer path after DMA TC |
| `final_drain_cycles` | Inline wait count used for fast-baud final-drain handling |

The distinction between `tx_num_bytes_pending` and `tx_num_bytes_in_transmission` is important.
Once bytes are handed to DMA, they are no longer pending, but they still occupy `tx_buffer` storage
until the DMA completion path clears the in-flight count. Free-space calculations must include both
pending and in-flight bytes to avoid overwriting DMA-owned data.

---

## RX path

### Purpose

The RX side is a continuous capture path. It is designed to keep SPI receive overhead low by letting
DMA write directly into an internal circular buffer. Higher-level code can inspect unread data
without copying it and then consume only the bytes it has processed.

### Passive RX start

`HW_SPI_Start_Channel()` arms passive RX DMA for channels that support receive. The driver programs
the RX DMA memory address to `rx_buffer`, the peripheral address to `SPIx->DR`, and the DMA length
in DMA elements. It then enables the DMA stream, enables the SPI RX DMA request, and enables the SPI
peripheral.

The driver intentionally does not use a HAL receive call for this path. In master mode, receive-only
HAL flows can generate clocks because an SPI master must drive SCK to receive data. This driver
should not create bus activity when a channel is merely started. RX capture is armed passively, and
traffic only occurs when the caller initiates SPI activity through TX.

### Peek/consume model

`HW_SPI_Rx_Peek()` reads the RX DMA remaining transfer count and converts it into a software DMA
write index. Since the DMA count is expressed in DMA elements, the driver converts it back into
bytes using the configured frame size.

The unread region is calculated from:

```text
software read index = rx_position
DMA write index     = RX_BUFFER_SIZE_BYTES - DMA_remaining_bytes
```

Because the RX buffer is circular, unread data may be one or two spans:

```text
No wrap:
    first span  = rx_position -> DMA write index
    second span = empty

Wrap:
    first span  = rx_position -> end of rx_buffer
    second span = 0 -> DMA write index
```

`HW_SPI_Rx_Consume()` advances `rx_position` after higher-level code has processed data. The driver
does not parse RX data and does not know where protocol messages begin or end.

### RX ownership rules

- The returned RX span pointers are driver-owned memory.
- Callers must not modify RX span memory.
- Callers should copy data if it must persist after future DMA activity.
- The caller must consume data often enough to avoid the DMA write position overtaking unread data.
- In 16-bit mode, RX lengths are still reported in bytes.

---

## TX overview

TX is internally split into two paths:

1. **Master TX packet path**
   - each public load call creates one packet descriptor
   - each packet is sent using exactly one DMA transfer
   - each packet is framed by automatic software chip-select
   - the TX completion chain automatically drains queued packets one at a time

2. **Slave TX stream path**
   - public loads append bytes to a raw TX stream
   - no packet descriptors are created
   - no software chip-select is driven
   - the external master owns SCK/NSS and transaction boundaries

The public trigger function is only a kick. It starts TX if the channel is idle and there is queued
work. It is not required to be called once per packet. In master mode, queued packets continue to
drain automatically through the DMA-completion/final-drain chain.

---

## Master TX path

### Packet queueing

In master mode, every successful `HW_SPI_Load_Tx_Buffer()` call is treated as one logical packet.
The packet bytes are copied into `tx_buffer`, and a descriptor is written into
`tx_packet_descriptors`.

A master packet descriptor contains:

```text
start_index: byte offset in tx_buffer where the packet begins
size_bytes:  number of bytes in the packet
```

Master packets must be contiguous in `tx_buffer` because each packet maps directly to one DMA
transfer and one chip-select pulse. If a packet does not fit in the remaining tail space of
`tx_buffer`, the driver may wrap the entire packet to index 0. It does not split one master packet
across the end of the buffer.

### Master trigger behaviour

`HW_SPI_Tx_Trigger()` is a kick function. For master mode it does this conceptually:

```text
if transaction state is not IDLE:
    return

if no master packets are pending:
    return

start one packet DMA transfer
```

It only starts the first queued packet. If more packets are queued, they are started by the
completion chain, not by repeated trigger calls. This lets a 100 us execution tick load several
packets and call trigger once.

### Starting a master packet DMA transfer

Starting a master packet transfer consumes exactly one packet descriptor:

```text
descriptor queue:
    tx_packet_read_position advances
    tx_num_packets_pending decreases

byte queue:
    tx_read_position advances by packet size
    tx_num_bytes_pending decreases by packet size
    tx_num_bytes_in_transmission becomes packet size

transaction state:
    IDLE -> DMA_ACTIVE

electrical output:
    CS asserted immediately before TX DMA is armed
```

The packet remains physically in `tx_buffer` while DMA owns it. It is not free for overwrite until
the DMA TC path clears `tx_num_bytes_in_transmission`.

### Software chip-select behaviour

Master software chip-select is always enabled. There is no optional chip-select mode in this driver.

The intended electrical framing is:

```text
CS low
DMA transfers exactly one packet into SPI
DMA TC interrupt fires
driver waits for final SPI drain
CS high
next packet may start
```

The current CS assert/deassert functions contain a hardcoded development GPIO write. That is a
temporary integration point and should eventually be replaced with the project GPIO driver. The
timing points must remain the same:

- assert CS immediately before enabling the packet's TX DMA transfer
- deassert CS only after DMA TC and final SPI drain completion

Large multi-packet chip-select windows are intentionally outside this module. If a higher-level
driver needs CS held across hundreds or thousands of bytes spanning multiple logical packets, that
should be handled with a separate GPIO policy above this low-level SPI driver.

---

## DMA TC is not master transaction complete

TX DMA transfer-complete means the DMA stream has finished feeding data to the SPI peripheral. It
does not necessarily mean the last SPI bit has left MOSI/SCK. The SPI peripheral may still be
shifting the final frame. For that reason, the driver does not deassert CS directly on DMA TC.

The master DMA TC path is:

```text
TX DMA TC interrupt
    tx_num_bytes_in_transmission = 0
    disable SPI TX DMA request
    confirm transaction state was DMA_ACTIVE
    if BSY is already clear:
        complete transaction now
    else if slow baud:
        enter WAIT_FINAL_DRAIN and start one-shot timer
    else:
        perform bounded inline final-drain wait
        check BSY again
        complete or fault
```

The final-drain stage exists because SPI bus completion and DMA completion are different events.
The driver must protect the CS rising edge from occurring before the final SPI frame is actually
finished.

---

## Master transaction state machine

Master mode uses `tx_transaction_state` to track the electrical state of the current packet:

| State | Meaning |
|---|---|
| `HW_SPI_TX_TRANSACTION_IDLE` | No master packet is electrically active |
| `HW_SPI_TX_TRANSACTION_DMA_ACTIVE` | One packet has been handed to DMA and CS is asserted |
| `HW_SPI_TX_TRANSACTION_WAIT_FINAL_DRAIN` | DMA TC has occurred, but the driver is waiting before releasing CS |
| `HW_SPI_TX_TRANSACTION_ERROR` | The transaction faulted and higher-level recovery is required |

Typical fast-baud path:

```text
IDLE
  -> DMA_ACTIVE
  -> DMA TC
  -> BSY already clear or short inline final-drain wait
  -> CS deassert
  -> IDLE or start next packet
```

Typical slow-baud path:

```text
IDLE
  -> DMA_ACTIVE
  -> DMA TC
  -> BSY still high
  -> WAIT_FINAL_DRAIN
  -> timer callback
  -> BSY clear
  -> CS deassert
  -> IDLE or start next packet
```

Fault path:

```text
DMA_ACTIVE or WAIT_FINAL_DRAIN
  -> final drain fails or unexpected callback/state occurs
  -> disable TX DMA request
  -> deassert CS according to current safety policy
  -> tx_num_bytes_in_transmission = 0
  -> ERROR
```

Once in `ERROR`, the driver does not automatically continue transmitting. Recovery should be done by
a higher-level reset/reconfiguration path.

---

## Final-drain timing

Fast SPI baud rates use a bounded inline wait after DMA TC. Slower baud rates use a one-shot timer
so the DMA ISR does not block for several microseconds to tens of microseconds while waiting for the
last SPI frame to drain.

Current intended split:

```text
45 Mbit/s
22.5 Mbit/s
11.25 Mbit/s
5.625 Mbit/s
    -> inline bounded wait

2.813 Mbit/s
1.406 Mbit/s
703 kbit/s
352 kbit/s
    -> final-drain timer
```

The timer path is configured so each assigned timer runs at approximately 1 MHz, giving a nominal
1 us tick. The ARR values are chosen to cover approximately one final SPI frame plus a guard margin.

The final-drain timer callback is valid only when the transaction state is
`WAIT_FINAL_DRAIN`. If a timer callback occurs while the channel is still `DMA_ACTIVE`, it is stale
or early and must not complete the transaction.

---

## Automatic master queue draining

The master completion path is responsible for starting the next queued packet. This is an important
behavioural detail.

Correct flow:

```text
caller queues packet A
caller queues packet B
caller queues packet C
caller calls HW_SPI_Tx_Trigger()

driver sends packet A
DMA TC/final-drain completes packet A
driver deasserts CS for packet A
driver immediately starts packet B if pending
DMA TC/final-drain completes packet B
driver deasserts CS for packet B
driver immediately starts packet C if pending
```

This means `HW_SPI_Tx_Trigger()` does not need to be called for every packet. It only starts the
engine when idle. Once active, the TX DMA and final-drain callbacks drain the queue one packet at a
time.

This model supports the HIL-RIG execution tick where multiple transmit requests may be loaded at the
start of a 100 us window.

---

## Slave TX path

Slave mode keeps the original stream-oriented behaviour. It does not use packet descriptors and does
not drive software chip-select. The external SPI master controls SCK and NSS.

### Slave loading

`HW_SPI_Load_Tx_Buffer()` appends bytes to the circular TX buffer. If the write crosses the end of
the buffer, the data is split into a tail copy and a head copy. This is allowed in slave mode because
the stream can be transmitted as multiple DMA spans.

### Slave triggering

`HW_SPI_Tx_Trigger()` starts one contiguous DMA span if TX is idle and bytes are pending.

The span length is:

```text
min(tx_num_bytes_pending, TX_BUFFER_SIZE_BYTES - tx_read_position)
```

After the span is handed to DMA:

```text
tx_read_position advances
tx_num_bytes_pending decreases
tx_num_bytes_in_transmission becomes span size
```

### Slave DMA continuation

When the TX DMA TC IRQ fires in slave mode:

```text
tx_num_bytes_in_transmission = 0

if tx_num_bytes_pending > 0:
    start the next contiguous span
else:
    remain idle
```

This allows wrapped stream data to be transmitted as two DMA transfers without the caller needing to
manually split it.

---

## DMA programming model

TX DMA is programmed as normal/one-shot DMA. Each TX transfer is a finite contiguous memory span.

The common DMA programming sequence is:

```text
disable SPI TX DMA request
disable DMA stream
wait until DMA stream is disabled
clear stale DMA flags
set memory address
set transfer length
enable DMA stream
enable SPI TX DMA request
```

Where possible, channel-invariant DMA setup is done during configuration instead of every transfer.
The hot transfer path focuses on the values that actually change between packets/spans, especially
the memory address and data length.

RX DMA is circular because it is a continuous capture path. TX DMA is normal because each master
packet or slave span should complete once and then be explicitly continued by the driver if required.

---

## Byte counts, frame sizes, and DMA element counts

The public API uses byte counts for both 8-bit and 16-bit SPI modes.

Internally:

```text
8-bit SPI:
    frame size = 1 byte
    DMA elements = bytes

16-bit SPI:
    frame size = 2 bytes
    DMA elements = bytes / 2
```

Configuration precomputes frame-size information so hot paths do not repeatedly derive it. Runtime
TX load sizes and RX consume sizes must remain aligned to whole SPI frames. Higher-level code is
responsible for maintaining this alignment in timing-critical paths.

The driver does not byte-swap or repack 16-bit data. Higher-level code must provide the intended
in-memory byte order.

---

## Buffer and queue behaviour

The driver uses power-of-two RX/TX buffer sizes and a power-of-two master packet queue depth.

This allows hot-path wraparound to use masks rather than division/modulo:

```text
wrapped_index = index & (SIZE - 1)
```

Important queues:

| Queue | Used by | Purpose |
|---|---|---|
| `rx_buffer` | RX | Circular DMA receive storage |
| `tx_buffer` | master and slave TX | Internal TX byte storage |
| `tx_packet_descriptors` | master TX only | Packet boundary preservation for software-CS framing |

Master mode requires each packet to be contiguous. Slave mode allows stream data to wrap and be sent
as multiple DMA spans.

---

## Interrupt and callback flow

### RX DMA IRQ

The RX DMA IRQ handlers currently do not perform work. RX DMA is circular, and the software read
position is derived when `HW_SPI_Rx_Peek()` is called.

### TX DMA IRQ

TX DMA IRQ handlers check transfer-error and transfer-complete flags. Error is handled before TC.
On TC:

- master mode enters the master DMA-completion/final-drain path
- slave mode clears the in-flight count and optionally starts the next stream span

### Final-drain timer IRQ

The hardware timer layer calls back into `HW_SPI_Timer_Callback_From_ISR()` with the logical SPI
peripheral. The SPI callback verifies that the corresponding transaction is actually waiting for
final drain, checks `BSY`, and then completes or faults the transaction.

---

## Performance model

The driver is structured around the expectation that RX/TX operations may be frequent inside a
100 us control/execution window.

Performance-related design choices:

- setup-time validation is preferred over repeated runtime validation
- configuration precomputes values used by hot paths
- hot helpers are implemented as inline helpers where practical
- buffer sizes and queue depth are powers of two
- TX trigger acts as a kick, not as a per-packet send operation
- master queue draining happens from the DMA/final-drain completion chain
- RX peek/consume avoids copying data out of the internal DMA buffer
- error paths are kept separate from normal fast paths

The cost of a TX transaction is dominated by DMA programming, IRQ entry/exit, final-drain handling,
and any GPIO used for software CS. Higher-level code should avoid repeatedly calling trigger after
every load if several packets are queued in the same tick. Load all required packets first, then
trigger once.

---

## Unit test structure

The unit tests are split by behaviour:

- `test_hw_spi_rx.cpp`
  - passive RX DMA start
  - NDTR-to-byte-index conversion
  - empty, contiguous, and wrapped RX spans
  - 8-bit and 16-bit byte accounting
  - consume index wrapping

- `test_hw_spi_tx_slave.cpp`
  - stream loading
  - TX ring wrapping
  - trigger behaviour
  - one-contiguous-span DMA starts
  - DMA TC re-arming for wrapped stream data
  - transfer-error handling
  - 16-bit DMA element counts

- `test_hw_spi_tx_master.cpp`
  - packet descriptor creation
  - whole-packet wrapping
  - descriptor queue capacity
  - trigger-as-kick behaviour
  - software-CS transaction state transitions
  - fast inline final-drain completion
  - slow final-drain timer completion
  - stale timer callback rejection
  - automatic next-packet start after CS release
  - 16-bit packet DMA element counts

The tests deliberately focus less on invalid hot-path inputs because those paths are designed for
speed and assume configuration-time validation. Configuration and non-hot-path checks are still
valid test targets.

---

## DMA and timer configuration assumptions

The generated Cube/HAL/LL configuration must match the driver model:

- RX DMA streams are circular
- TX DMA streams are normal
- RX direction is peripheral-to-memory
- TX direction is memory-to-peripheral
- memory increment is enabled
- peripheral increment is disabled
- DMA memory/peripheral widths match the configured SPI frame size
- TX DMA interrupts are enabled for transfer complete and transfer error
- SPI final-drain timers are configured and routed to the SPI timer callback
- the SPI channel-to-DMA-stream mappings in `hw_spi_internal.h` match the CubeMX project
- the chip-select GPIO currently hardcoded in the master CS hooks matches the test hardware until
  the GPIO-driver integration is completed

If TX DMA is accidentally configured as circular, a master could repeatedly transmit the same bytes
and continue clocking unexpectedly. If the final-drain timer IRQ is not enabled, slow-baud master
transactions can remain stuck in `WAIT_FINAL_DRAIN`.

---

## Caller responsibilities

Higher-level software is responsible for:

- configuring each SPI channel before use
- starting RX DMA before expecting received data
- ensuring runtime calls use valid peripheral values
- ensuring TX load sizes fit in the available queue space
- ensuring byte counts are frame-aligned in 16-bit mode
- consuming RX data before unread data is overwritten
- deciding protocol/message boundaries above this driver
- deciding when the TX engine should be kicked
- handling recovery if a master transaction enters `ERROR`
- using a separate GPIO policy if a device requires chip-select held across a large multi-packet
  transfer

The driver is intentionally not a protocol implementation. It is a deterministic byte/packet
movement layer for the hardware.

---

## Typical usage

### Master packet transmission

```c
HW_SPI_Configure_Channel( SPI_CHANNEL_0, config );
HW_SPI_Start_Channel( SPI_CHANNEL_0 );

HW_SPI_Load_Tx_Buffer( SPI_CHANNEL_0, command_a, command_a_size );
HW_SPI_Load_Tx_Buffer( SPI_CHANNEL_0, command_b, command_b_size );
HW_SPI_Load_Tx_Buffer( SPI_CHANNEL_0, command_c, command_c_size );

HW_SPI_Tx_Trigger( SPI_CHANNEL_0 );
```

The trigger starts `command_a` if the channel is idle. The DMA/final-drain completion chain then
automatically sends `command_b` and `command_c`, each with its own CS pulse.

### Slave stream transmission

```c
HW_SPI_Configure_Channel( SPI_CHANNEL_1, config );
HW_SPI_Start_Channel( SPI_CHANNEL_1 );

HW_SPI_Load_Tx_Buffer( SPI_CHANNEL_1, tx_stream_data, tx_stream_size );
HW_SPI_Tx_Trigger( SPI_CHANNEL_1 );
```

The driver sends the next contiguous stream span when the external master clocks the bus. If queued
data wraps inside the TX ring, the DMA TC path re-arms the next span.

### RX processing

```c
HWSPIRxSpans_T spans = HW_SPI_Rx_Peek( SPI_CHANNEL_0 );

process( spans.first_span.data, spans.first_span.length_bytes );
process( spans.second_span.data, spans.second_span.length_bytes );

HW_SPI_Rx_Consume( SPI_CHANNEL_0, spans.total_length_bytes );
```

The caller decides how much unread RX data is meaningful and how much should be consumed.

---

## Current limitations and TODOs

- The master CS assert/deassert hooks currently use a hardcoded development GPIO. Replace this with
  the separate GPIO driver once the final board-level CS mapping is available.
- `SPI_CHANNEL_1` and `SPI_DAC` should be reviewed if they share physical SPI/DMA resources in the
  final hardware configuration.
- Final timer prescaler/ARR values assume the current expected clock tree. Recheck them once the
  Cube clock tree is locked.
- The driver does not currently provide a public recovery API for `HW_SPI_TX_TRANSACTION_ERROR`.
- The RX path does not protect against unread data being overwritten; higher-level code must consume
  fast enough for the expected traffic pattern.
- Large multi-packet chip-select windows are intentionally outside this module.
