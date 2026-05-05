# external_flash

## Overview

`external_flash` contains the flash storage service used by the flash manager and the result transfer path.

This module is responsible for:

- Storing execution results as opaque bytes.
- Programming execution instructions as opaque bytes.
- Reading execution instructions as opaque bytes.
- Reading stored execution results for host transfer.
- Managing fixed instruction and result NAND partitions.
- Scanning and skipping factory bad blocks.
- Erasing the result partition at the start of each result session.
- Erasing the instruction partition at the start of each instruction upload.
- Programming instruction data into NAND pages.
- Programming result data into NAND pages.
- Reading instruction queue pages directly into flash manager owned buffers.
- Supporting page scoped zero copy result writes from flash manager owned buffers.
- Padding final partial result pages with `0xFF`.
- Retiring blocks that fail program or erase operations.
- Rotating the logical start block for repeated instruction uploads and result sessions.
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
- `EXTERNAL_FLASH_StartInstructionUpload` prepares the required instruction blocks and starts a new package instruction upload.
- `EXTERNAL_FLASH_WriteInstructionBytes` appends arbitrary instruction bytes during package upload.
- `EXTERNAL_FLASH_WriteInstructionPage` writes one logical instruction page during package upload.
- `EXTERNAL_FLASH_FinishInstructionUpload` commits the final partial instruction page and closes the upload.
- `EXTERNAL_FLASH_StartSession` prepares the result partition for a new test run.
- `EXTERNAL_FLASH_WriteResultPage` writes one logical result page.
- `EXTERNAL_FLASH_ReadInstructionPage` reads one instruction page or partial instruction page using DMA internally.
- `EXTERNAL_FLASH_ReadResults` reads committed result bytes for host transfer.
- `EXTERNAL_FLASH_GetInfo` reports capacity, committed result length, and bad block count.

The preferred execution time write path is `EXTERNAL_FLASH_WriteResultPage`. This lets the flash manager pass page sized result buffers directly to `external_flash`, allowing DMA to read from the flash manager owned buffer without an extra copy.

The preferred execution time instruction refill path is `EXTERNAL_FLASH_ReadInstructionPage`. This lets the flash manager pass an instruction queue page buffer directly to `external_flash`, allowing DMA to write into the flash manager owned buffer without an extra copy.

The preferred package upload path is `EXTERNAL_FLASH_WriteInstructionBytes`. Host transfer chunk sizes do not need to match the NAND page size; the driver stages partial pages and programs full pages internally.

`EXTERNAL_FLASH_WriteInstructionPage` is available when `test_package_recieve` already has page sized instruction chunks. Full page calls DMA directly from the caller supplied buffer. A final partial page is padded internally with `0xFF`.

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

Instruction bytes are stored exactly as supplied by `test_package_recieve`. Execution results are stored exactly as supplied by the flash manager. The driver does not parse instructions, result records, or add per page metadata in this first version.

The current instruction length and result length are tracked in RAM, so instruction and result recovery after reset is not supported yet.

Instruction reads are logical byte offset based and are limited to the committed instruction length from the most recent successful instruction upload.

Instruction upload is sequential and append only. Random instruction patching is intentionally not exposed yet because the host package receive path naturally streams one package image before execution begins.

`external_flash` applies best-practice-lite wear allocation inside each fixed partition. Each new instruction upload and result session builds an active logical-to-physical block map using the lowest known erase counts. Logical offsets remain stable for the active upload or result session.

---

## Instruction Uploads

`test_package_recieve` should use the instruction upload API when the host sends a new test package.

Typical byte stream upload:

```c
EXTERNAL_FLASH_StartInstructionUpload( instruction_length );
EXTERNAL_FLASH_WriteInstructionBytes( chunk_a, len_a );
EXTERNAL_FLASH_WriteInstructionBytes( chunk_b, len_b );
EXTERNAL_FLASH_FinishInstructionUpload();
```

`EXTERNAL_FLASH_StartInstructionUpload` erases only the instruction blocks required for the expected image, resets the volatile committed instruction length, and records the expected instruction byte count.

`EXTERNAL_FLASH_WriteInstructionBytes` accepts arbitrary host chunk sizes. Full staged NAND pages are programmed immediately. A final partial staged page is programmed by `EXTERNAL_FLASH_FinishInstructionUpload`.

`EXTERNAL_FLASH_FinishInstructionUpload` succeeds only when the committed instruction byte count matches the expected length supplied at the start of upload. Execution should not start until this function returns `EXTERNAL_FLASH_STATUS_OK`.

When the package receiver already has page sized instruction spans, it may use:

```c
EXTERNAL_FLASH_StartInstructionUpload( instruction_length );
EXTERNAL_FLASH_WriteInstructionPage( page_a, page_size );
EXTERNAL_FLASH_WriteInstructionPage( final_page, final_valid_length );
EXTERNAL_FLASH_FinishInstructionUpload();
```

For a full page, `external_flash` DMA loads directly from the caller supplied buffer. The caller must keep the buffer valid and unchanged until the function returns.

For a final partial page, `external_flash` copies the valid bytes into its private staging buffer, pads the remainder of the physical NAND page with `0xFF`, and programs one full physical page.

Partial page instruction writes are only valid for the final page of the expected upload length.

---

## Result Sessions

`EXTERNAL_FLASH_StartSession` prepares the full writable result capacity and starts a new volatile result session.

The result partition is append only during a session.

One result write path is supported: page scoped writes with `EXTERNAL_FLASH_WriteResultPage`.

Only bytes successfully committed to NAND are exposed through `EXTERNAL_FLASH_ReadResults`.

The active result session keeps its logical-to-physical rotation stable until the next result session starts. This allows `result_transfer_manager` to read committed bytes in logical order after execution, even though the physical NAND blocks may not start at the first block of the result partition.

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

`EXTERNAL_FLASH_WriteResultPage` is the only result write API. During execution the flash manager should pass full pages. After execution it should pass one final partial page if required.

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

## HIL RIG Upload, Execute, Return Flow

The following flows describe how the managers should use this driver.

### 1. Test Package Upload

`test_package_recieve` is responsible for receiving the test package from the host interface.

It stores instruction bytes into the instruction partition through `external_flash`.

Current behaviour:

- `external_flash` provides `EXTERNAL_FLASH_StartInstructionUpload`.
- `external_flash` provides `EXTERNAL_FLASH_WriteInstructionBytes` for natural host chunk sized writes.
- `external_flash` provides `EXTERNAL_FLASH_WriteInstructionPage` for page sized package chunks.
- `external_flash` provides `EXTERNAL_FLASH_FinishInstructionUpload` to commit the final partial page and validate the expected length.
- `test_package_recieve` must not bypass `external_flash` and call `hw_nand` directly.

Intended upload sequence:

1. Host sends a test package to `test_package_recieve`.
2. `test_package_recieve` validates or fragments the package into instruction byte spans.
3. `test_package_recieve` calls `EXTERNAL_FLASH_StartInstructionUpload` with the expected instruction byte count.
4. `test_package_recieve` appends the package instruction bytes with `EXTERNAL_FLASH_WriteInstructionBytes`, or page spans with `EXTERNAL_FLASH_WriteInstructionPage`.
5. `test_package_recieve` calls `EXTERNAL_FLASH_FinishInstructionUpload`.
6. `flash_manager` later reads those same opaque bytes back into its instruction buffers using `EXTERNAL_FLASH_ReadInstructionPage`.

The instruction byte format is owned by the package and execution format, not by `external_flash`.

### 2. Test Execution

Before execution starts:

1. System startup calls `EXTERNAL_FLASH_Init`.
2. The flash manager calls `EXTERNAL_FLASH_StartSession`.
3. `EXTERNAL_FLASH_StartSession` prepares the full writable result capacity and resets volatile result state.
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

1. The flash manager writes the final partial result page through `EXTERNAL_FLASH_WriteResultPage` if any result bytes remain.
2. `result_transfer_manager` queries committed length with `EXTERNAL_FLASH_GetInfo`.
3. `result_transfer_manager` repeatedly calls `EXTERNAL_FLASH_ReadResults` with byte offsets and host transfer sized buffers.
4. The host interface sends those bytes to the host in order.

Only committed bytes are readable.

---

## Manager Responsibilities

### `flash_manager`

- Owns instruction and result RAM buffers.
- Presents empty, filling, ready, active, and reusable spans to producers and consumers.
- Refills page sized instruction buffers by calling `EXTERNAL_FLASH_ReadInstructionPage`.
- Drains page sized result buffers by calling `EXTERNAL_FLASH_WriteResultPage`.
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
- Uses `external_flash` for instruction partition writes.
- Calls `EXTERNAL_FLASH_StartInstructionUpload` before writing a new package image.
- Writes package instruction bytes with `EXTERNAL_FLASH_WriteInstructionBytes` or page spans with `EXTERNAL_FLASH_WriteInstructionPage`.
- Calls `EXTERNAL_FLASH_FinishInstructionUpload` before allowing execution to start.
- Should not bypass `external_flash` and call `hw_nand` directly.

### `result_transfer_manager`

- Owns result transfer state for host readback.
- Uses `EXTERNAL_FLASH_GetInfo` to determine committed result length.
- Uses `EXTERNAL_FLASH_ReadResults` to stream committed result bytes in order.
- Does not parse NAND pages or inspect bad block state.

---

## Bad Blocks

At initialisation, the module scans the configured instruction, result, and metadata partitions using `HW_NAND_IsBlockBad`.

Bad blocks are skipped when translating logical byte offsets to physical NAND pages.

If erase fails at runtime, the block is marked bad in RAM and `HW_NAND_MarkBlockBad` is called.

If program fails at runtime, the block is marked bad in RAM and `HW_NAND_MarkBlockBad` is called. The same logical result page is retried on the next good physical block.

The same retire-and-retry policy is used for instruction upload page programs.

---

## Wear Rotation

This implementation uses a best-practice-lite wear policy.

The instruction, result, and metadata partitions remain fixed. Within the instruction and result partitions, `external_flash` builds an active logical-to-physical block map using the lowest known erase counts:

- `EXTERNAL_FLASH_StartInstructionUpload` erases and maps only the blocks needed for the expected instruction image, while leaving spare blocks available for program-failure replacement.
- `EXTERNAL_FLASH_StartSession` currently prepares the full writable result capacity because the final result length is not known before execution.
- Program failures retire the failed block and replace it with an already-erased spare selected by the allocator.
- Runtime erase counts are updated whenever `external_flash` successfully erases a block.

The metadata partition is reserved now so persistent snapshots can be added without changing the physical layout. The current erase counts and active maps are RAM only.

A later metadata journal should persist:

- erase counts
- retired block state
- active logical-to-physical maps
- committed instruction/result lengths if recovery after reset is required

Future result storage should move to an erase-ahead or pre-erased block queue. That is better than synchronous erase-as-you-go in the execution path: the flash manager can keep a small supply of erased result blocks ready outside the hard real-time loop, then append into them without blocking on block erase latency. Direct erase-as-needed during result writes should remain a fallback only.

Until that metadata exists, callers must treat instruction and result contents as volatile after reset.

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
- Instruction and result lengths are currently volatile RAM state only.
- Wear erase counts and active block maps are currently volatile RAM state only.
- Instruction recovery after reset is not implemented yet.
- Result recovery after reset is not implemented yet.
- `EXTERNAL_FLASH_WriteResultPage` is the preferred execution time result write API.
- `EXTERNAL_FLASH_ReadInstructionPage` is the preferred execution time instruction refill API.
- `EXTERNAL_FLASH_WriteInstructionBytes` is the preferred test package upload API.
