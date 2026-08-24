# hw_uart

## Overview

`hw_uart` contains the low-level UART drivers for both console UART and DUT-facing UART channels.

The DUT-facing UART driver owns STM32 UART configuration, DMA setup, RX DMA circular buffers, and TX DMA source ring buffers. External electrical-interface selection is owned by `exec_uart`.

This module is responsible for:

- Configuring and deconfiguring DUT-facing STM32 UART peripherals.
- Starting and stopping configured DUT UART channels.
- Providing zero-copy RX span views into the driver-owned RX circular buffer.
- Tracking RX consumption through an internal read index.
- Queueing TX payloads into a driver-owned TX ring buffer.
- Pumping queued TX data through normal-mode DMA transfers.
- Handling TX DMA completion/error interrupt paths.
- Reporting full TX completion once queued data is drained and the UART wire is idle.
- Providing the low-level console UART driver used by the console subsystem.

---

## Files

| File | Role |
|------|------|
| `hw_uart_dut.c` | Low-level DUT-facing UART implementation |
| `hw_uart_dut.h` | Public API for DUT-facing UART channels |
| `hw_uart_console.c` | Low-level console UART implementation |
| `hw_uart_console.h` | Public API for console UART |
| `tests/test_hw_uart.cpp` | Unit tests for DUT-facing UART behaviour |
| `tests/hw_uart_mocks.h` | Hardware/HAL/LL mocks used by UART unit tests |

---

## DUT UART Design

The DUT UART driver supports two logical DUT-facing channels:

- `HW_UART_CHANNEL_1`
- `HW_UART_CHANNEL_2`

Each channel has independent configuration and runtime state.

RX uses a DMA-backed circular buffer owned by the low-level driver. Higher layers inspect unread RX data using `HW_UART_Rx_Peek()`, which returns one or two transient spans depending on whether unread data wraps around the end of the circular buffer. Once data has been processed, the caller advances the driver read index using `HW_UART_Rx_Consume()`.

TX uses a driver-owned ring buffer that also acts as the DMA source buffer. Payloads are copied into this ring buffer by `HW_UART_Tx_Load_Buffer()`. The DMA stream is operated in normal mode and transmits one contiguous span at a time. If queued TX data wraps around the end of the ring buffer, the completion handler launches the next contiguous span after the first transfer completes.

TX completion is reported by `HW_UART_Is_Tx_Complete()`. TX is complete only when:

- the TX ring buffer has no queued bytes,
- no TX DMA transfer is active,
- the UART transmission-complete flag indicates the final stop bit has left the wire.

---

## DUT UART Lifecycle

Configuration and activity are separate states:

1. `HW_UART_Configure_Channel()` configures the peripheral and leaves it stopped.
2. `HW_UART_Start_Channel()` starts the channel and RX DMA when RX is enabled.
3. `HW_UART_Stop_Channel()` stops RX after TX is complete and retains configuration.
4. `HW_UART_Deconfigure_Channel()` removes a stopped channel configuration.

TX-only channels still enter the started state, but no TX transfer begins until
data is queued and the TX DMA pump is triggered. Stop is rejected while TX data
is queued, DMA is active, or the UART is still shifting the final frame.

---

## Typical DUT RX Flow

1. Configure the channel with `HW_UART_Configure_Channel()`.
2. Start the channel with `HW_UART_Start_Channel()`.
3. Inspect unread data with `HW_UART_Rx_Peek()`.
4. Copy data into caller-owned storage if it must persist.
5. Report consumed bytes with `HW_UART_Rx_Consume()`.
6. Stop the channel with `HW_UART_Stop_Channel()` when required.

---

## Typical DUT TX Flow

1. Configure the channel with `HW_UART_Configure_Channel()`.
2. Start the channel with `HW_UART_Start_Channel()`.
3. Queue a complete payload with `HW_UART_Tx_Load_Buffer()`.
4. Start the DMA pump with `HW_UART_Tx_Trigger()`.
5. Continue queueing additional payloads while TX buffer space remains.
6. Poll `HW_UART_Is_Tx_Complete()` when the caller must know that transmission has fully drained.
7. Stop the channel with `HW_UART_Stop_Channel()`.

Payload queueing is all-or-nothing. If there is not enough free TX ring-buffer space for the full payload, no bytes are copied and `HW_UART_Tx_Load_Buffer()` returns `false`.

---

## Public API

The DUT-facing UART API is declared in `hw_uart_dut.h`.

Key functions:

| Function | Purpose |
|----------|---------|
| `HW_UART_Configure_Channel()` | Configure a DUT UART channel |
| `HW_UART_Start_Channel()` | Start a configured DUT UART channel |
| `HW_UART_Stop_Channel()` | Stop a channel while retaining configuration |
| `HW_UART_Deconfigure_Channel()` | Deconfigure a DUT UART channel |
| `HW_UART_Rx_Peek()` | Return transient unread RX spans |
| `HW_UART_Rx_Consume()` | Advance the RX read index |
| `HW_UART_Tx_Load_Buffer()` | Queue a TX payload into the TX ring buffer |
| `HW_UART_Tx_Trigger()` | Start or continue the TX DMA pump |
| `HW_UART_Is_Tx_Complete()` | Report full TX completion |

The console UART API is declared in `hw_uart_console.h`.

Key functions:

| Function | Purpose |
|----------|---------|
| `HW_UART_CONSOLE_Init()` | Initialise console UART |
| `HW_UART_CONSOLE_Read()` | Read buffered console RX bytes |
| `HW_UART_CONSOLE_Write_Blocking()` | Write console TX bytes using blocking UART transmit |

---

## Execution Path Notes

Execution-path DUT UART functions assume valid caller input unless otherwise documented. The caller is responsible for providing a valid channel, configuring the channel for the required direction, and respecting buffer ownership rules.

This keeps the hot path lightweight and avoids repeated defensive checks in low-level RX/TX operations.
