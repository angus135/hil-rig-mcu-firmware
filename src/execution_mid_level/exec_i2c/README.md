# exec_i2c

## Overview

`exec_i2c` is the mid-level execution module for I2C communication.

This module is responsible for:

- Validating higher-layer I2C channel configuration requests for the two external buses.
- Translating execution-layer enums/types into low-level hardware-layer enums/types.
- Calling low-level `hw_i2c` configuration APIs for:
	- `I2C1` external channel
	- `I2C2` external channel
	- `FMPI2C1` internal channel
- Handling transmit orchestration using the stage-buffer pattern:
	- load stage buffer in low level
	- trigger transfer in low level
- Handling receive orchestration using the peek/copy/consume pattern:
	- peek zero-copy spans from low level
	- copy to execution-manager-owned storage
	- consume copied bytes in low level


---

## Files

| File                      | Role |
|---------------------------|------|
| `exec_i2c.c`        | Public API implementation |
| `exec_i2c.h`        | Public API header |


---

## Architecture and data flow

`exec_i2c` does not directly manipulate hardware registers.
It is a coordination layer between higher-level logic and `hw_i2c`.

- Higher layers call `exec_i2c` APIs.
- `exec_i2c` validates parameters and maps types.
- `exec_i2c` forwards operations to `hw_i2c`.
- `hw_i2c` owns hardware-facing resources:
  - receive buffers
  - stage/transmit buffers
  - interrupt/DMA servicing

### TX flow

1. Higher layer calls `EXEC_I2C_Master_Send(...)` or `EXEC_I2C_Slave_Send(...)`.
2. `exec_i2c` calls `HW_I2C_Load_Stage_Buffer(...)`.
3. `exec_i2c` calls `HW_I2C_Trigger_*_Transmit(...)`.

### RX flow

1. Higher layer calls `EXEC_I2C_Start_Master_Receive(...)` or `EXEC_I2C_Start_Slave_Receive(...)`.
2. Higher layer later calls `EXEC_I2C_Receive_Copy_And_Consume(...)`.
3. `exec_i2c`:
	- peeks low-level spans with `HW_I2C_Peek_Received(...)`
	- copies into caller-provided result storage
	- consumes copied bytes via `HW_I2C_Consume_Received(...)`


---

## Configuration model

`EXEC_I2C_Configuration(...)` configures all three channels in one call.

- External `I2C1`: configurable from higher layer.
- External `I2C2`: configurable from higher layer.
- Internal `FMPI2C1`: configured through dedicated low-level internal configuration call.

For each external channel, higher layer provides:

- Mode: `EXEC_I2C_MODE_MASTER` or `EXEC_I2C_MODE_SLAVE`
- Speed: `EXEC_I2C_SPEED_100KHZ` or `EXEC_I2C_SPEED_400KHZ`
- Transfer path:
	- `I2C1`: `EXEC_I2C_TRANSFER_INTERRUPT` only
	- `I2C2`: `EXEC_I2C_TRANSFER_INTERRUPT` or `EXEC_I2C_TRANSFER_DMA`
- Own address: 7-bit value (`0x00` to `0x7F`)

Input validation rejects invalid enum/address values with `EXEC_I2C_STATUS_INVALID_PARAM`.


---

## Public API summary

Configuration:

- `EXEC_I2C_Configuration(...)`

Transmit:

- `EXEC_I2C_Master_Send(...)`
- `EXEC_I2C_Slave_Send(...)`

Receive start:

- `EXEC_I2C_Start_Master_Receive(...)`
- `EXEC_I2C_Start_Slave_Receive(...)`

Receive extraction:

- `EXEC_I2C_Receive_Copy_And_Consume(...)`


---

## Typical usage sequence

1. Build two `EXECI2CChannelConfig_T` objects for external channels.
2. Call `EXEC_I2C_Configuration(&i2c1_cfg, &i2c2_cfg, internal_fmpi2c1_addr)` once at startup.
3. At runtime:
	- Use `EXEC_I2C_Master_Send`/`EXEC_I2C_Slave_Send` for transmit.
	- Use `EXEC_I2C_Start_*_Receive` to arm receive.
	- Use `EXEC_I2C_Receive_Copy_And_Consume` to fetch received bytes into execution-manager storage.

---

## Notes

- This module expects low-level IRQ/DMA service routines to be integrated in the platform interrupt handlers.
- Status codes from low level are mapped into `EXEC_I2C_STATUS_*` for upper-layer consistency.