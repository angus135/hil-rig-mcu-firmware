# hw_i2c

## Overview

`hw_i2c` is the low-level hardware I2C driver module for the STM32F446ZE microcontroller.

This module is responsible for:

- Configuring I2C channels (I2C1, I2C2, and internal FMPI2C1) in master or slave mode
- Managing I2C transfers at the hardware level using interrupt and DMA pathways
- Implementing ring-buffered receive mechanisms for efficient data handling
- Orchestrating master and slave transmit operations using stage-buffer patterns
- Servicing hardware interrupts and DMA completion events
- Supporting both standard I2C (100 kHz, 400 kHz) and fast I2C modes
- Handling I2C bus errors and protocol violations

---

## Files

| File                      | Role |
|---------------------------|------|
| `hw_i2c.c`        | Public API implementation |
| `hw_i2c.h`        | Public API header |

---

## Architecture and Data Flow

`hw_i2c` directly manages hardware I2C peripherals and buffers. It provides low-level primitives that are coordinated by higher-layer modules like `exec_i2c`.

### Channel Support

- **I2C1**: External channel (interrupt-based transfers only, no DMA)
- **I2C2**: External channel (interrupt or DMA-based transfers)
- **FMPI2C1**: Internal high-speed channel (interrupt-based transfers only)

### Transfer Mechanisms

- **Interrupt Path**: Suitable for small transfers; CPU-driven byte-by-byte handling
- **DMA Path**: Suitable for bulk transfers; hardware-accelerated, available only on I2C2

### RX Flow

1. Caller loads stage buffer with `HW_I2C_Load_Stage_Buffer()`
2. Caller triggers transfer with `HW_I2C_Trigger_Master_Receive()` or `HW_I2C_Trigger_Slave_Receive_External()`
3. Hardware/DMA receives data into internal ring buffer
4. Interrupt handler services I2C events and manages state machine
5. Caller peeks received data with `HW_I2C_Peek_Received()` (zero-copy)
6. Caller consumes read bytes with `HW_I2C_Consume_Received()`

### TX Flow

1. Caller loads stage buffer with `HW_I2C_Load_Stage_Buffer()`
2. Caller triggers transfer with `HW_I2C_Trigger_Master_Transmit()` or `HW_I2C_Trigger_Slave_Transmit()`
3. Hardware/DMA transmits buffered data over I2C bus
4. Interrupt/DMA handler manages transmit progress and detects completion
5. Caller detects completion via interrupt callbacks in higher layers

---

## Public API

The public API is declared in `hw_i2c.h`. Key functions include:

- `HW_I2C_Configure_Channel()` - Configure an external I2C channel
- `HW_I2C_Configure_Internal_FMPI2C1()` - Configure the internal FMPI2C1 channel
- `HW_I2C_Load_Stage_Buffer()` - Load data into transmit stage buffer
- `HW_I2C_Trigger_Master_Transmit()` / `HW_I2C_Trigger_Master_Receive()` - Master operations
- `HW_I2C_Trigger_Slave_Transmit()` / `HW_I2C_Trigger_Slave_Receive_External()` - Slave operations
- `HW_I2C_Peek_Received()` / `HW_I2C_Consume_Received()` - Receive management
- `HW_I2C_Service_*_IRQ()` - Interrupt handlers called by application ISRs
