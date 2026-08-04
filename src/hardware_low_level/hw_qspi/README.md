# hw_qspi

## Overview

`hw_qspi` is the generic STM32 QSPI peripheral wrapper. It translates project-level command descriptions into STM32 HAL transactions and deliberately contains no NAND geometry, opcode, ECC, bad-block, or storage-allocation policy.

The wrapper provides:

- Command-only transactions.
- Blocking transmit and receive operations.
- DMA transmit and receive operations.
- Wrapper and HAL busy-state checks.
- Transfer completion callbacks and abort support.
- Status mapping from STM32 HAL values to `HW_QSPI_Status_T`.

## Files

| File | Role |
|---|---|
| `hw_qspi.c` | Public API, HAL callbacks, and firmware QSPI/DMA interrupt handlers |
| `hw_qspi.h` | Public interface and command/configuration types |
| `tests/hw_qspi_mocks.h` | Unit-test HAL definitions |
| `tests/test_hw_qspi.cpp` | Unit tests |

## Initialisation

Exactly one of these paths must run before the wrapper is used:

- Call `HW_QSPI_Init()` when this module owns HAL peripheral initialisation.
- In generated firmware, call `MX_QUADSPI_Init()` and then pass the ready `hqspi` handle to `HW_QSPI_AdoptHandle()`.

The firmware currently uses the second path in `Core/Src/main.c`.

GPIOs, clocks, the DMA stream, and NVIC configuration remain owned by CubeMX-generated code. `HW_QSPI_AdoptHandle()` does not configure or reinitialise the peripheral.

## DMA Requirements

DMA transfers complete through STM32 HAL callbacks in `hw_qspi.c`. Because the
project excludes the generated `stm32f4xx_it.c`, this module also owns the two
firmware interrupt handlers that call:

- `HAL_DMA_IRQHandler()` for the QSPI DMA stream.
- `HAL_QSPI_IRQHandler()` from `QUADSPI_IRQHandler()`.

`external_flash` bounds its synchronous DMA wait with `HAL_GetTick()` and calls
`HW_NAND_AbortTransfer()`/`HW_QSPI_Abort()` if that deadline expires.

The source buffer for `HW_QSPI_WriteDma()` and destination buffer for `HW_QSPI_ReadDma()` must remain valid until completion or abort. `HW_QSPI_IsTransferComplete()` returns `true` only after a successful completion callback; it remains `false` while active and after an error.

## Layering

`hw_nand` is the direct consumer of this module. Application code should use `external_flash` rather than constructing raw QSPI commands.
