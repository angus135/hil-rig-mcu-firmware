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
