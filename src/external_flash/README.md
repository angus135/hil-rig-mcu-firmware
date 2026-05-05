# external_flash
## Overview

`external_flash` contains the flash storage service used by the flash manager.

This module is responsible for:

- Storing execution results as opaque bytes
- Reading execution instructions as opaque bytes
- Reading stored execution results for host transfer
- Managing fixed instruction and result NAND partitions
- Scanning and skipping factory bad blocks
- Erasing the result partition at the start of each result session
- Packing result bytes into NAND pages
- Retiring blocks that fail program or erase operations
- Providing the flash-facing API used by the flash manager and result transfer path


---

## Files

| File                      | Role |
|---------------------------|------|
| `external_flash.c`        | Public API implementation |
| `external_flash.h`        | Public API header |
| `tests/external_flash_mocks.h` | Unit-test mock definitions |
| `tests/test_external_flash.cpp` | Unit tests |


---

## Public API

The public API is declared in `external_flash.h`.

The API is intentionally byte-oriented:

- `EXTERNAL_FLASH_Init` prepares the NAND storage service.
- `EXTERNAL_FLASH_StartSession` erases the result partition for a new test run.
- `EXTERNAL_FLASH_WriteResults` appends opaque result bytes.
- `EXTERNAL_FLASH_FlushResults` commits the final partial result page.
- `EXTERNAL_FLASH_ReadInstructions` reads opaque instruction bytes.
- `EXTERNAL_FLASH_ReadResults` reads committed result bytes for host transfer.
- `EXTERNAL_FLASH_GetInfo` reports capacity, committed result length, and bad
  block count.

The driver does not expose pages, blocks, QSPI, ECC fields, or bad-block markers
to application managers.


---

## Layering

`external_flash` uses `hw_nand` for physical NAND operations. It does not issue
QSPI commands directly.

The execution manager should not call this module. The intended ownership is:

- `execution_manager` produces result bytes and consumes instruction bytes through flash-manager-owned RAM buffers.
- `flash_manager` owns the RAM instruction and result buffers.
- `flash_manager` is the only normal runtime task that calls `external_flash`.
- `flash_manager` moves byte spans between RAM buffers and NAND by calling `external_flash`.
- `external_flash` owns storage policy.
- `hw_nand` owns physical NAND command sequencing.

This keeps the 10 kHz execution path deterministic. The execution manager never
waits for QSPI, NAND page reads, page programs, block erases, DMA completion, or
bad-block handling.


---

## Storage Model

The first implementation uses fixed compile-time partitions:

- Instruction partition: starts at block 0.
- Result partition: starts after the instruction partition.

Execution results are stored exactly as drained from flash-manager-owned result buffers. The
driver does not parse result records or add page metadata in this first version.
The current result length is tracked in RAM, so result recovery after reset is
not supported yet.

Instruction reads are also byte-oriented. The module assumes instructions have
already been programmed into the instruction partition using the same logical
bad-block skipping policy.

Instruction upload/programming is not implemented in this first version. When
`test_package_recieve` grows the host-upload path, it should add an
instruction-write API to `external_flash` rather than writing directly through
`hw_nand`.


---

## Result Sessions

`EXTERNAL_FLASH_StartSession` erases all good blocks in the result partition and
starts a new volatile result session.

Result bytes are staged into one NAND page buffer. Full pages are programmed
immediately. Partial pages are programmed by `EXTERNAL_FLASH_FlushResults`.

Only bytes successfully committed to NAND are exposed through
`EXTERNAL_FLASH_ReadResults`.


---

## HIL-RIG Upload / Execute / Return Flow

The following flows describe how the managers should use this driver.

### 1. Test Package Upload

`test_package_recieve` is responsible for receiving the test package from the
host interface. In the final design, it should store instruction bytes into the
instruction partition through `external_flash`.

Current first-version behaviour:

- `external_flash` provides `EXTERNAL_FLASH_ReadInstructions`.
- Instruction write/upload support is still future work.
- `test_package_recieve` should not call `hw_nand` directly when that write path
  is added.

Intended upload sequence:

1. Host sends a test package to `test_package_recieve`.
2. `test_package_recieve` validates/fragments the package into instruction byte
   spans.
3. A future `external_flash` instruction-write API stores those bytes in the
   instruction partition.
4. `flash_manager` later reads those same opaque bytes back into its instruction
   buffers using `EXTERNAL_FLASH_ReadInstructions`.

The instruction byte format is owned by the package/execution format, not by
`external_flash`.

### 2. Test Execution

Before execution starts:

1. System startup calls `EXTERNAL_FLASH_Init`.
2. The flash manager calls `EXTERNAL_FLASH_StartSession`.
3. `EXTERNAL_FLASH_StartSession` erases all good result blocks and resets the
   volatile result length.

During execution:

1. `execution_manager` consumes instruction bytes from flash-manager-owned
   instruction buffers.
2. `flash_manager` refills those instruction buffers with
   `EXTERNAL_FLASH_ReadInstructions`.
3. `execution_manager` writes result bytes into flash-manager-owned result
   buffers.
4. `flash_manager` drains full/ready result spans by calling
   `EXTERNAL_FLASH_WriteResults`.
5. `external_flash` packs those bytes into NAND pages, starts DMA program-load
   transfers, executes NAND page programs, skips bad blocks, and tracks committed
   length.

The execution manager remains a producer/consumer of RAM buffers only. It does
not know whether the bytes come from NAND, DMA, host packets, or any later
storage backend.

### 3. Result Return To Host

After execution ends:

1. The flash manager calls `EXTERNAL_FLASH_FlushResults` to commit the final
   partial result page.
2. `result_transfer_manager` queries committed length with
   `EXTERNAL_FLASH_GetInfo`.
3. `result_transfer_manager` repeatedly calls `EXTERNAL_FLASH_ReadResults` with
   byte offsets and host-transfer-sized buffers.
4. The host interface sends those bytes to the host in order.

Only committed bytes are readable. Bytes still staged in the page buffer are not
visible to `EXTERNAL_FLASH_ReadResults` until `EXTERNAL_FLASH_FlushResults`
succeeds.


---

## Manager Responsibilities

`flash_manager`

- Owns instruction and result RAM buffers.
- Presents full/empty/ready spans to producers and consumers.
- Refills instruction buffers by calling `EXTERNAL_FLASH_ReadInstructions`.
- Drains result buffers by calling `EXTERNAL_FLASH_WriteResults`.
- Calls `EXTERNAL_FLASH_StartSession` before execution starts.
- Calls `EXTERNAL_FLASH_FlushResults` before committed results are transferred.
- Is the only normal runtime task responsible for talking to flash.
- Does not expose NAND pages, blocks, bad-block markers, erase timing, or QSPI details to `execution_manager`.

`test_package_recieve`

- Owns host package reception and validation.
- Should use `external_flash` for instruction partition writes once that API is
  added.
- Should not bypass `external_flash` and call `hw_nand` directly.

`result_transfer_manager`

- Owns result transfer state for host readback.
- Calls `EXTERNAL_FLASH_FlushResults` before transfer starts.
- Uses `EXTERNAL_FLASH_GetInfo` to determine committed result length.
- Uses `EXTERNAL_FLASH_ReadResults` to stream committed result bytes in order.
- Does not parse NAND pages or inspect bad-block state.


---

## Bad Blocks

At initialisation, the module scans the configured instruction and result
partitions using `HW_NAND_IsBlockBad`.

Bad blocks are skipped when translating logical byte offsets to physical NAND
pages. If erase or program fails at runtime, the block is marked bad in RAM and
`HW_NAND_MarkBlockBad` is called. Program failures are retried on the next good
physical block for the same logical result page.
