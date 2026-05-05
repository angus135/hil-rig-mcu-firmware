# external_flash

## Overview

`external_flash` contains the flash storage service used by the flash manager and the result transfer path.

This module is responsible for:

- Storing execution results as opaque bytes.
- Reading execution instructions as opaque bytes.
- Reading stored execution results for host transfer.
- Managing fixed instruction and result NAND partitions.
- Scanning and skipping factory bad blocks.
- Erasing the result partition at the start of each result session.
- Programming result data into NAND pages.
- Reading instruction queue pages directly into flash manager owned buffers.
- Supporting page scoped zero copy result writes from flash manager owned buffers.
- Padding final partial result pages with `0xFF`.
- Retiring blocks that fail program or erase operations.
- Providing the flash facing API used by the flash manager and result transfer path.

---

## Files

| File | Role |
|---|---|
| `external_flash.c` | Public API implementation |
| `external_flash.h` | Public API header |
| `tests/external_flash_mocks.h` | Unit test mock definitions |
| `tests/test_external_flash.cpp` | Unit tests |

---

## Public API

The public API is declared in `external_flash.h`.

The main API functions are:

- `EXTERNAL_FLASH_Init` prepares the NAND storage service.
- `EXTERNAL_FLASH_StartSession` erases the result partition for a new test run.
- `EXTERNAL_FLASH_WriteResultPage` writes one logical result page.
- `EXTERNAL_FLASH_WriteResultBytes` appends arbitrary result bytes through the byte stream compatibility path.
- `EXTERNAL_FLASH_FlushResults` commits a partial result page staged by the byte stream write path.
- `EXTERNAL_FLASH_ReadInstructionPage` reads one instruction page or partial instruction page using DMA internally.
- `EXTERNAL_FLASH_ReadInstructions` reads arbitrary instruction byte ranges through the byte stream compatibility path.
- `EXTERNAL_FLASH_ReadResults` reads committed result bytes for host transfer.
- `EXTERNAL_FLASH_GetInfo` reports capacity, committed result length, and bad block count.

The preferred execution time write path is `EXTERNAL_FLASH_WriteResultPage`. This lets the flash manager pass page sized result buffers directly to `external_flash`, allowing DMA to read from the flash manager owned buffer without an extra copy.

The preferred execution time instruction refill path is `EXTERNAL_FLASH_ReadInstructionPage`. This lets the flash manager pass an instruction queue page buffer directly to `external_flash`, allowing DMA to write into the flash manager owned buffer without an extra copy.

The byte stream paths, `EXTERNAL_FLASH_WriteResultBytes` and `EXTERNAL_FLASH_ReadInstructions`, remain available for simple use, tests, and non page structured paths.

The driver does not expose physical NAND pages, physical blocks, QSPI commands, ECC fields, or bad block markers to application managers.

---

## Layering

`external_flash` uses `hw_nand` for physical NAND operations. It does not issue QSPI commands directly.

The intended ownership is:

- `execution_manager` produces result bytes and consumes instruction bytes through flash manager owned RAM buffers.
- `flash_manager` owns the RAM instruction and result buffers.
- `flash_manager` is the only normal runtime task that calls `external_flash`.
- `flash_manager` refills page sized instruction buffers by calling `EXTERNAL_FLASH_ReadInstructionPage`.
- `flash_manager` drains page sized result buffers by calling `EXTERNAL_FLASH_WriteResultPage`.
- `external_flash` owns storage policy, logical to physical mapping, bad block skipping, and result length tracking.
- `hw_nand` owns physical NAND command sequencing.
- `hw_qspi` owns generic STM32 QSPI transactions.

This keeps the 10 kHz execution path deterministic. The execution manager never waits for QSPI, NAND page reads, page programs, block erases, DMA completion, ECC handling, or bad block handling.

---

## Storage Model

The first implementation uses fixed compile time partitions:

- Instruction partition starts at block `0`.
- Result partition starts after the instruction partition.

Execution results are stored exactly as supplied by the flash manager. The driver does not parse result records or add per page metadata in this first version.

The current result length is tracked in RAM, so result recovery after reset is not supported yet.

Instruction reads are logical byte offset based. The module assumes instructions have already been programmed into the instruction partition using the same logical bad block skipping policy.

Instruction upload and programming are not implemented in this first version. When `test_package_recieve` grows the host upload path, it should add an instruction write API to `external_flash` rather than writing directly through `hw_nand`.

---

## Result Sessions

`EXTERNAL_FLASH_StartSession` erases all good blocks in the result partition and starts a new volatile result session.

The result partition is append only during a session.

Two result write paths are supported:

1. Page scoped writes with `EXTERNAL_FLASH_WriteResultPage`.
2. Byte stream writes with `EXTERNAL_FLASH_WriteResultBytes` and `EXTERNAL_FLASH_FlushResults`.

Only bytes successfully committed to NAND are exposed through `EXTERNAL_FLASH_ReadResults`.

---

## Page Scoped Result Writes

`EXTERNAL_FLASH_WriteResultPage` is the preferred write API for the flash manager.

It writes one logical result page to the result partition.

For a full page:

```c
EXTERNAL_FLASH_WriteResultPage( page_buffer, page_size );
```

`external_flash` DMA loads directly from `page_buffer`.

The caller must keep the buffer valid and unchanged until the function returns.

For a final partial page:

```c
EXTERNAL_FLASH_WriteResultPage( page_buffer, valid_length );
```

where `valid_length` is less than the NAND page size.

In this case, `external_flash` copies the valid bytes into its private staging buffer, pads the remainder of the physical NAND page with `0xFF`, and programs one full physical page. Only `valid_length` bytes are counted as committed logical result data.

After a partial page write succeeds, no further result page writes should be appended in the same session.

This API must not be mixed with a partially staged `EXTERNAL_FLASH_WriteResultBytes` call.

---

## Page Scoped Instruction Reads

`EXTERNAL_FLASH_ReadInstructionPage` is the preferred read API for the flash manager instruction queue.

It reads one instruction page or partial instruction page from the instruction partition into a caller supplied buffer.

Typical full page refill:

```c
EXTERNAL_FLASH_ReadInstructionPage( instruction_offset, page_buffer, page_size );
```

For a final partial instruction page:

```c
EXTERNAL_FLASH_ReadInstructionPage( instruction_offset, page_buffer, valid_length );
```

where `valid_length` is less than the NAND page size.

The logical offset must be aligned to the NAND page size. The length must be greater than zero and no larger than the NAND page size.

`external_flash` maps the logical instruction offset to a physical NAND page, skips bad block gaps, starts the NAND DMA read path, and waits for DMA completion before returning.

The caller must keep the destination buffer valid and writable until the function returns.

---

## Byte Stream Result Writes

`EXTERNAL_FLASH_WriteResultBytes` appends arbitrary result bytes to the active result session.

This path is intended for simple use, tests, and non page structured code paths.

The byte stream path internally stages data into a private page sized buffer. Full staged pages are programmed immediately. A final partial staged page is programmed by calling `EXTERNAL_FLASH_FlushResults`.

Example:

```c
EXTERNAL_FLASH_WriteResultBytes( data_a, len_a );
EXTERNAL_FLASH_WriteResultBytes( data_b, len_b );
EXTERNAL_FLASH_FlushResults();
```

`EXTERNAL_FLASH_FlushResults` is only required for the byte stream path. It is not required after `EXTERNAL_FLASH_WriteResultPage`, because a partial page passed to that API is programmed immediately.

---

## Byte Stream Instruction Reads

`EXTERNAL_FLASH_ReadInstructions` reads arbitrary instruction byte ranges from the instruction partition.

This path is intended for simple use, tests, and non page structured code paths.

It may read across physical NAND page boundaries and uses blocking NAND reads internally.

For execution time instruction queue refills, prefer `EXTERNAL_FLASH_ReadInstructionPage`.

---

## HIL RIG Upload, Execute, Return Flow

The following flows describe how the managers should use this driver.

### 1. Test Package Upload

`test_package_recieve` is responsible for receiving the test package from the host interface.

In the final design, it should store instruction bytes into the instruction partition through `external_flash`.

Current first version behaviour:

- `external_flash` provides `EXTERNAL_FLASH_ReadInstructionPage`.
- `external_flash` also provides `EXTERNAL_FLASH_ReadInstructions` for arbitrary instruction byte reads.
- Instruction write and upload support are still future work.
- `test_package_recieve` should not call `hw_nand` directly when that write path is added.

Intended upload sequence:

1. Host sends a test package to `test_package_recieve`.
2. `test_package_recieve` validates or fragments the package into instruction byte spans.
3. A future `external_flash` instruction write API stores those bytes in the instruction partition.
4. `flash_manager` later reads those same opaque bytes back into its instruction buffers using `EXTERNAL_FLASH_ReadInstructionPage`.

The instruction byte format is owned by the package and execution format, not by `external_flash`.

### 2. Test Execution

Before execution starts:

1. System startup calls `EXTERNAL_FLASH_Init`.
2. The flash manager calls `EXTERNAL_FLASH_StartSession`.
3. `EXTERNAL_FLASH_StartSession` erases all good result blocks and resets volatile result state.
4. The flash manager primes its instruction queue by calling `EXTERNAL_FLASH_ReadInstructionPage`.

During execution:

1. `execution_manager` consumes instruction bytes from flash manager owned instruction buffers.
2. When the instruction queue occupancy falls below its refill threshold, `flash_manager` refills a free instruction page buffer with `EXTERNAL_FLASH_ReadInstructionPage`.
3. `execution_manager` writes result bytes into flash manager owned result buffers.
4. When a full result page is ready, `flash_manager` calls `EXTERNAL_FLASH_WriteResultPage`.
5. `external_flash` programs the result page, skips bad blocks, and tracks committed length.

The execution manager remains a producer and consumer of RAM buffers only. It does not know whether the bytes come from NAND, DMA, host packets, or any later storage backend.

### 3. Result Return To Host

After execution ends:

1. If using the page scoped API, the flash manager writes the final partial result page through `EXTERNAL_FLASH_WriteResultPage`.
2. If using the byte stream API, the flash manager calls `EXTERNAL_FLASH_FlushResults`.
3. `result_transfer_manager` queries committed length with `EXTERNAL_FLASH_GetInfo`.
4. `result_transfer_manager` repeatedly calls `EXTERNAL_FLASH_ReadResults` with byte offsets and host transfer sized buffers.
5. The host interface sends those bytes to the host in order.

Only committed bytes are readable. Bytes still staged in the byte stream page buffer are not visible to `EXTERNAL_FLASH_ReadResults` until `EXTERNAL_FLASH_FlushResults` succeeds.

---

## Manager Responsibilities

### `flash_manager`

- Owns instruction and result RAM buffers.
- Presents empty, filling, ready, active, and reusable spans to producers and consumers.
- Refills page sized instruction buffers by calling `EXTERNAL_FLASH_ReadInstructionPage`.
- May use `EXTERNAL_FLASH_ReadInstructions` for non page structured instruction reads.
- Drains page sized result buffers by calling `EXTERNAL_FLASH_WriteResultPage`.
- May use `EXTERNAL_FLASH_WriteResultBytes` for non page structured result writes.
- Calls `EXTERNAL_FLASH_StartSession` before execution starts.
- Writes the final partial result page after execution ends.
- Is the only normal runtime task responsible for talking to flash.
- Does not expose NAND pages, blocks, bad block markers, erase timing, or QSPI details to `execution_manager`.

### `execution_manager`

- Consumes instruction bytes from flash manager owned buffers.
- Produces result bytes into flash manager owned buffers.
- Does not call `external_flash`, `hw_nand`, or `hw_qspi`.
- Does not wait for flash operations.

### `test_package_recieve`

- Owns host package reception and validation.
- Should use `external_flash` for instruction partition writes once that API is added.
- Should not bypass `external_flash` and call `hw_nand` directly.

### `result_transfer_manager`

- Owns result transfer state for host readback.
- Uses `EXTERNAL_FLASH_GetInfo` to determine committed result length.
- Uses `EXTERNAL_FLASH_ReadResults` to stream committed result bytes in order.
- Does not parse NAND pages or inspect bad block state.

---

## Bad Blocks

At initialisation, the module scans the configured instruction and result partitions using `HW_NAND_IsBlockBad`.

Bad blocks are skipped when translating logical byte offsets to physical NAND pages.

If erase fails at runtime, the block is marked bad in RAM and `HW_NAND_MarkBlockBad` is called.

If program fails at runtime, the block is marked bad in RAM and `HW_NAND_MarkBlockBad` is called. The same logical result page is retried on the next good physical block.

---

## DMA Buffer Lifetime

For full page writes through `EXTERNAL_FLASH_WriteResultPage`, DMA reads directly from the caller supplied result buffer.

The caller must not modify or reuse that result buffer until `EXTERNAL_FLASH_WriteResultPage` returns.

For instruction page reads through `EXTERNAL_FLASH_ReadInstructionPage`, DMA writes directly into the caller supplied instruction buffer.

The caller must not modify or reuse that instruction buffer until `EXTERNAL_FLASH_ReadInstructionPage` returns.

The current implementation is synchronous from the caller perspective. When `EXTERNAL_FLASH_WriteResultPage` returns `EXTERNAL_FLASH_STATUS_OK`, the DMA load and NAND program execute have completed successfully, and the logical result bytes have been counted as committed.

When `EXTERNAL_FLASH_ReadInstructionPage` returns `EXTERNAL_FLASH_STATUS_OK`, the NAND page read and DMA transfer into the caller supplied buffer have completed successfully.

A future asynchronous version may split these states into separate operations, such as DMA load complete and NAND program complete.

---

## Notes and Limitations

- Hardware validation is still required.
- Instruction upload and programming support is not implemented yet.
- Result recovery after reset is not implemented yet.
- The result length is stored in RAM only.
- `EXTERNAL_FLASH_WriteResultPage` is the preferred execution time result write API.
- `EXTERNAL_FLASH_ReadInstructionPage` is the preferred execution time instruction refill API.
- `EXTERNAL_FLASH_WriteResultBytes` remains available for byte stream compatibility and tests.
- `EXTERNAL_FLASH_ReadInstructions` remains available for byte stream compatibility and tests.