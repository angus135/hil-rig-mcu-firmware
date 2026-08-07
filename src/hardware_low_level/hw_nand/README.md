# hw_nand
## Overview

`hw_nand` contains the low level driver for the external SPI NAND flash device.

The selected device is `GD5F1GM7UEYIGR` (3.3 V), identified by manufacturer
ID `0xC8` and device ID `0x91`. Its compiled geometry is 1024 blocks, 64 pages
per block, 2048 main-area bytes per page, and 128 physical spare bytes per page.
With internal ECC enabled, the first 64 spare bytes remain user accessible.

This module is responsible for:

- NAND reset and identification
- Device feature configuration
- Ready/busy, ECC, program-fail, and erase-fail status handling
- Complete physical page read and program command sequences
- Block erase operations
- Factory bad-block marker reads
- Bad-block marker programming for retired blocks
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

Call `HW_NAND_Init()` before any API that communicates with the device.
`HW_NAND_GetGeometry()` and `HW_NAND_GetLastEccStatus()` are state-only
accessors and may be called before initialisation; ECC status is `UNKNOWN` until
a checked page read records a result.

The storage-facing operations are deliberately complete and synchronous:

- `HW_NAND_ReadPageBlocking`
- `HW_NAND_ReadPageDma`
- `HW_NAND_ProgramPageBlocking`
- `HW_NAND_ProgramPageDma`
- `HW_NAND_BlockErase`
- `HW_NAND_IsBlockBad`
- `HW_NAND_MarkBlockBad`

Raw feature access, cache access, program-load/program-execute phases, status
waits, and DMA completion handling are private implementation details. This
prevents callers from constructing incomplete or invalid NAND sequences.


---

## Layering

`hw_nand` uses `hw_qspi` for bus transactions.

Higher level storage policy belongs in `external_flash`, not in this module.

`hw_nand` does not know about result records, append-log allocation, logical
page numbers, host transfer framing, or execution-manager timing. It exposes
complete physical NAND operations so `external_flash` can implement those
policies above the hardware layer.


---

## Transfer Model

Small command and feature-register operations use blocking QSPI because they
move one or two bytes and are not throughput-sensitive.

Ready waits use elapsed milliseconds from `HAL_GetTick()`. The timeout values in
`hw_nand.c` are selected above the datasheet maximum reset, page-read, program,
and block-erase times; they do not assume a fixed duration for each poll.
Short reset, page-read, and program waits poll tightly because their device-busy
periods are small and page throughput matters. Block erase is comparatively
long, so its wait delays one RTOS tick between status reads and allows other
ready tasks to execute.

Bulk page transfers use QSPI DMA through `HW_NAND_ReadPageDma()` and
`HW_NAND_ProgramPageDma()`. These calls are synchronous from the caller's
perspective: they return only after the DMA data phase and NAND operation have
completed or failed. No extra page copy is introduced; DMA still reads from or
writes directly into the caller-owned buffer.

`hw_qspi` signals DMA completion through a dedicated binary semaphore. The NAND
call remains synchronous, but the calling task is blocked by FreeRTOS during
the DMA phase rather than occupying the CPU. Other ready service tasks can run.
`hw_qspi` also owns the DMA deadline and aborts an expired transfer.
Consequently, the caller may reuse its buffer as soon as the page function
returns and does not need access to QSPI transfer state.


---

## Bad Blocks

The driver exposes physical bad-block primitives only:

- `HW_NAND_IsBlockBad` checks byte 2048 (spare-area byte 0) on the first page of
  a block. GD5F1GM7xExxG Rev. 1.3 section 12.4 explicitly defines this location
  and does not require checks on the second or last page.
- `HW_NAND_MarkBlockBad` programs the marker in the first page spare area.

Skipping bad blocks, retiring failed blocks, maintaining a bad-block table, and
mapping logical result storage onto physical blocks belong in `external_flash`.


---

## Hardware Validation Status

The driver is currently unit tested against mocked QSPI transactions but has
not yet been exercised on the physical GD5F1GM7UEYIGR device. Initial hardware
bring-up must verify:

- reset, ID, block-unlock, quad-enable, and ECC-enable sequencing;
- page-read ECC decoding for clean, corrected, and uncorrectable data;
- full-page DMA read and program operations;
- DMA completion interrupts and timeout abort recovery;
- erase/program failure-bit handling;
- factory bad-block marker detection and runtime marker programming;
- the configured reset, read, program, and erase deadlines.

The public API intentionally keeps these device-specific details inside
`hw_nand`, so bring-up fixes should not require changes to `external_flash`.
