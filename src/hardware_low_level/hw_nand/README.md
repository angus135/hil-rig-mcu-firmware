# hw_nand
## Overview

`hw_nand` contains the low level driver for the external SPI NAND flash device.

This module is responsible for:

- NAND reset and identification
- NAND feature register reads and writes
- Ready/busy and status polling
- Page read-to-cache operations
- Cache read operations
- Program-load and program-execute operations
- Block erase operations
- Factory bad-block marker reads
- Bad-block marker programming for retired blocks
- ECC status decode after page reads
- NAND geometry reporting


---

## Files

| File                      | Role |
|---------------------------|------|
| `hw_nand.c`        | Public API implementation |
| `hw_nand.h`        | Public API header |
| `tests/hw_nand_mocks.h` | Unit-test mock definitions |
| `tests/test_hw_nand.cpp` | Unit tests |


---

## Public API

The public API is declared in `hw_nand.h`.


---

## Layering

`hw_nand` uses `hw_qspi` for bus transactions.

Higher level storage policy belongs in `external_flash`, not in this module.

`hw_nand` should not know about result records, append-log allocation,
pre-erase queues, logical page numbers, host transfer framing, or execution
manager timing. It exposes physical NAND operations so `external_flash` can
implement those policies above the hardware layer.


---

## Transfer Model

Small command and feature-register operations are blocking because they move one
or two bytes and are outside the hard real-time execution loop.

Bulk cache transfers expose DMA entry points:

- `HW_NAND_ReadCacheDma`
- `HW_NAND_ReadPageDma`
- `HW_NAND_ProgramLoadDma`

The caller owns buffer lifetime until `HW_NAND_IsTransferComplete()` reports
true. Page read, program execute, and block erase also have start-only entry
points so the flash manager can issue a long NAND operation and decide how to
wait from its own RTOS task context.

Use the matching completion helper for each long operation:

- `HW_NAND_WaitPageReadComplete` checks ECC status.
- `HW_NAND_WaitProgramComplete` checks program-fail status.
- `HW_NAND_WaitBlockEraseComplete` checks erase-fail status.
- `HW_NAND_WaitReady` only waits for OIP to clear.


---

## Bad Blocks

The driver exposes physical bad-block primitives only:

- `HW_NAND_IsBlockBad` checks the marker byte in the spare area of the first,
  second, and last page of a block.
- `HW_NAND_MarkBlockBad` programs the marker in the first page spare area.

Skipping bad blocks, retiring failed blocks, maintaining a bad-block table, and
mapping logical result storage onto physical blocks belong in `external_flash`.
