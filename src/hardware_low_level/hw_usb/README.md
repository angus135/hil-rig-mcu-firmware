# hw_usb

## Overview

`hw_usb` contains the low-level application code for handling USB CDC byte transport.

This module is responsible for:

- isolating the application from most CubeMX-generated USB Device code
- copying received USB CDC bytes into a FreeRTOS stream buffer from the CDC receive callback
- providing a non-blocking receive API for higher-level application code
- buffering transmit data in a module-owned ring buffer before passing it to the STM32 CDC driver
- allowing application transmit buffers to be stack/local data, because the USB wrapper copies them internally
- periodically advancing queued USB transmissions when the CDC driver is idle
- tracking basic receive diagnostics such as dropped bytes

`hw_usb` exists to provide a small, controlled boundary between the HIL-RIG application and the
STM32 USB CDC middleware. The USB link is treated as a non-real-time byte transport layer. It is
intended for configuration data, start/stop commands, ACKs, connection or health messages, and
returning non-time-critical results.

This module should not be used as part of the deterministic execution path. It does not parse
protocol frames, validate messages, or implement application-level sequencing. Those behaviours
belong in higher-level protocol or application modules.

---

## Design Summary

`hw_usb` provides two buffered USB access paths:

1. **Receive stream path**
   - used by the generated CDC receive callback
   - copies bytes from the temporary CDC receive buffer into a FreeRTOS stream buffer
   - keeps interrupt-side work short
   - allows higher-level code to read received bytes later from task context
   - counts bytes that could not be stored

2. **Transmit ring-buffer path**
   - used by application code that wants to send bytes over USB CDC
   - copies caller-provided data into a persistent internal transmit buffer
   - removes the need for caller buffers to remain valid until USB transfer completion
   - uses `HW_USB_Monitor_Process()` to pass queued contiguous data to `CDC_Transmit_FS()` when CDC is idle

The receive path is interrupt-facing and should remain minimal. The transmit path is task-facing
and is advanced by periodic calls to the monitor function.

---

## Files

| File       | Role |
|------------|------|
| `hw_usb.c` | Public API implementation and internal USB wrapper state |
| `hw_usb.h` | Public API header |

---

## Key Behaviour

### Receive callback handling

The generated CDC receive callback should only call the hardware USB wrapper and then immediately
re-arm the CDC OUT endpoint. A typical generated-code boundary is:

```c
HW_USB_Receive_From_ISR(Buf, Len);
USBD_CDC_SetRxBuffer(&hUsbDeviceFS, &Buf[0]);
USBD_CDC_ReceivePacket(&hUsbDeviceFS);
```

The `Buf` pointer provided by the STM32 USB stack is temporary. The CDC stack may reuse or
overwrite that memory after the endpoint is re-armed. For that reason,
`HW_USB_Receive_From_ISR()` copies the received bytes into the FreeRTOS stream buffer before
returning.

If the receive stream has not been created, or if there is not enough stream-buffer space, the
unwritten bytes are dropped and counted by the receive dropped-byte diagnostic counter. This is
acceptable for this layer because higher-level protocol code is expected to detect missing or
corrupt data.

### Non-blocking receive reads

`HW_USB_Receive()` reads bytes from the receive stream buffer from normal task context. It is
non-blocking and uses a zero timeout internally, so it only returns bytes already available in the
stream buffer.

This prevents USB reads from blocking a task indefinitely and keeps receive handling predictable.
If no bytes are available, or if the receive stream has not been initialised, the function returns
zero.

### Receive diagnostics

The module provides helper functions for inspecting the receive stream:

- `HW_USB_Get_Receive_Stream_Used_Bytes()` returns the number of bytes currently available to read
- `HW_USB_Get_Receive_Stream_Free_Bytes()` returns the remaining stream-buffer capacity
- `HW_USB_Get_Receive_Stream_Dropped_Bytes()` returns the cumulative number of receive bytes dropped

The dropped-byte count is diagnostic only. It is not a replacement for higher-level packet or
message validation.

### Persistent transmit buffering

The STM32 CDC transmit function does not copy application data into a long-lived application-owned
queue. Once `CDC_Transmit_FS()` is called, the CDC driver may continue using the supplied pointer
until the USB IN transfer completes.

To avoid exposing that lifetime requirement to the rest of the application, `HW_USB_Transmit()`
copies caller data into an internal transmit ring buffer before returning. This means callers may
safely transmit data from local or stack buffers.

If the complete requested transmit payload cannot fit in the internal transmit buffer,
`HW_USB_Transmit()` rejects the request and returns `false`. Partial application-level messages are
not intentionally queued by this API.

### Transmit monitor process

`HW_USB_Monitor_Process()` advances the transmit state machine. It should be called periodically
from a task or application service loop.

The monitor function:

1. checks whether an active CDC transmission has completed
2. removes completed bytes from the internal transmit ring buffer
3. checks whether more bytes are queued
4. starts the next CDC transfer if the CDC driver is idle

Because `CDC_Transmit_FS()` requires a contiguous memory region, the monitor only transmits up to
the end of the physical ring buffer in one call. If queued data wraps around the end of the buffer,
the wrapped section is transmitted by a later monitor call.

### CDC transmit-complete detection

The current implementation polls the STM32 CDC class state rather than using a transmit-complete
callback. Internally, the module checks `TxState` through `hUsbDeviceFS.pClassData`. A non-zero
`TxState` means the CDC driver still owns the active transmit buffer. A zero `TxState` means the
active transfer has completed and the ring buffer can be advanced.

This polling approach is acceptable for the current USB use case because USB is not part of the
hard real-time execution loop. If higher throughput or lower-latency transmit handling is needed
later, this could be replaced with a small transmit-complete hook from the CDC DataIn path.

---

## Public API

The public API is declared in `hw_usb.h`.

### Initialisation

```c
bool HW_USB_Init(void);
```

Creates the receive stream buffer and resets receive dropped-byte diagnostics. This should be
called before USB receive callbacks are allowed to write into the module.

### Transmit

```c
bool HW_USB_Transmit(const uint8_t* data, uint16_t size_bytes);
```

Copies bytes into the internal transmit ring buffer. Returns `true` if the full payload was queued,
or if `size_bytes` is zero. Returns `false` if `data` is `NULL` or if there is not enough free
transmit-buffer space.

### Receive from CDC callback

```c
void HW_USB_Receive_From_ISR(uint8_t* data_received, uint32_t* size_bytes);
```

Copies received CDC bytes into the FreeRTOS stream buffer from interrupt/callback context. This
function should be called from the generated CDC receive callback before the endpoint is re-armed.

### Receive from task context

```c
uint32_t HW_USB_Receive(uint8_t* destination, uint32_t max_size_bytes);
```

Reads available bytes from the receive stream buffer without blocking. Returns the number of bytes
copied into `destination`.

### Receive stream diagnostics

```c
uint32_t HW_USB_Get_Receive_Stream_Used_Bytes(void);
uint32_t HW_USB_Get_Receive_Stream_Free_Bytes(void);
uint32_t HW_USB_Get_Receive_Stream_Dropped_Bytes(void);
```

Provides visibility into receive stream usage and dropped receive bytes.

### Transmit monitor

```c
void HW_USB_Monitor_Process(void);
```

Advances queued USB transmissions. This must be called periodically from task or application
service-loop context.

---

## Usage Notes

- USB CDC is used as a byte transport layer only.
- The receive callback should remain short and should not parse protocol messages.
- Received bytes must be copied before the CDC OUT endpoint is re-armed.
- `HW_USB_Receive()` is non-blocking and returns immediately with currently available bytes.
- `HW_USB_Transmit()` copies transmit data, so caller buffers do not need to remain valid after the function returns.
- `HW_USB_Monitor_Process()` must be called periodically or queued transmit data may not be sent.
- Higher-level protocol code is responsible for framing, validation, sequencing, retries, and recovery from dropped bytes.
- If `HW_USB_Transmit()` and `HW_USB_Monitor_Process()` are called from different tasks, or from mixed ISR/task contexts, access to the internal USB state should be protected with a mutex or critical section.
- The module is not intended to provide deterministic timing guarantees for the HIL-RIG execution loop.
