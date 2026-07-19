# hw_can

## Overview

`hw_can` drives the two STM32F446 bxCAN controllers used by the HIL-RIG DUT
interfaces. It supports classic CAN frames with an 11-bit standard or 29-bit
extended identifier, a DLC from zero to eight, and data or remote-frame type.

CAN is broadcast. An identifier is not a point-to-point address: it labels a
message, participates in arbitration, and is matched by receive filters. Each
transmitted `HwCanFrame_T` therefore carries its own identifier and format.

## Ownership and configuration

Cube-generated code owns the clocks, GPIO alternate functions, HAL handles, and
NVIC setup. This includes CAN2's dependency on the CAN1 clock. The driver owns
runtime timing/mode, fixed-allocation queues, filters, bounded direct-register
ISR service, and diagnostics.

CAN1 owns shared filter banks 0-13 and CAN2 owns 14-27. Configuration is
serialized cold-path work and must not run in the 100 us execution ISR. The
driver reads the real APB1 clock and searches legal bxCAN timing combinations.
A stopped channel also receives a bounded cleanup of its three-entry hardware
FIFO, write-one-to-clear causes, and pending NVIC requests so traffic accepted
under an old filter cannot appear after restart. Recovery is enabled only after
a complete timing/filter configuration has been stored.

The filter registers and their FINIT control are shared by CAN1 and CAN2. The
STM32 HAL enters and leaves FINIT for each programmed bank, and bxCAN reception
is disabled while FINIT is set. Configure, deconfigure, and recover are therefore
coordinated cold-path operations. Stop both channels first if losing a peer
frame during that short filter-programming interval is unacceptable.

Filter policy is explicit:

- `HW_CAN_FILTER_ACCEPT_NONE` disables every assigned bank.
- `HW_CAN_FILTER_ACCEPT_ALL` installs a deliberate all-zero mask.
- `HW_CAN_FILTER_CONFIGURED` installs mask or two-ID list filters.

Only FIFO0 is used because it has a bounded RX0 service path.

## Runtime architecture

TX is single-producer/single-consumer. A complete batch is validated and checked
for capacity before any slot is published. Queue entries are pre-encoded, so
each TX interrupt only classifies at most three completions and fills at most
three mailboxes. A slot is consumed only after TXRQ transfers the complete frame
to hardware. Loading remains valid during bus-off; the following trigger reports
bus-off while the complete batch remains queued for recovery. Completion flags
are classified from one TSR snapshot before one direct write-one-to-clear
acknowledgement.

RX is also single-producer/single-consumer. The ISR handles only the FIFO depth
captured at entry, at most three records. Each record retains ID, format, type,
DLC, payload, timestamp, and filter-match index. Consumers use
`HW_CAN_Rx_Peek()` and then `HW_CAN_Rx_Consume()` for exactly the number
processed.

`HW_CAN_Tx_Trigger()` pends the TX IRQ rather than calling the vector. With the
existing equal CAN/scheduler priority, this work cannot nest inside an active
scheduler ISR. No HAL function or dynamic allocation is used in a CAN ISR.

Queueing success is not wire success. Status separately reports idle state,
successful frames, arbitration losses, errors, aborts, overflow, warning,
passive state, bus-off, and protocol errors. Recovery reapplies stored cold-path
configuration and preserves only frames still owned by the software queue.

## Files

| File | Role |
|---|---|
| `hw_can.h` | Public configuration, frame, span, lifecycle, and status contracts |
| `hw_can.c` | Timing/filter setup, queues, ISR services, and interrupt vectors |
| `tests/hw_can_mocks.h` | HAL/CMSIS model with command and W1C semantics |
| `tests/test_hw_can.cpp` | Focused configuration, queue, ISR, and status tests |
