## Flash Manager Implementation Notes

These notes are  intended for the person writing the flash_manager and can be replaced once implemented with actual README

The flash manager is the only normal runtime task that should call `external_flash`.

The execution manager should only interact with RAM buffers owned by the flash manager. It must not call `external_flash`, `hw_nand`, or `hw_qspi` directly.

---

## Recommended Buffer Model

Use page sized buffer slots rather than one raw byte circular buffer.

This avoids DMA wraparound problems and keeps ownership simple.

Recommended initial sizing:

```c
#define FLASH_MANAGER_RESULT_PAGE_COUNT       3U
#define FLASH_MANAGER_INSTRUCTION_PAGE_COUNT  3U
```

The page size should be obtained from `external_flash` or the configured NAND geometry. For the current NAND part, the expected main page size is:

```c
#define FLASH_MANAGER_PAGE_SIZE_BYTES         2048U
```

Three slots is a practical starting point because it allows:

- One page being consumed or filled by `execution_manager`.
- One page ready.
- One page active with `external_flash`, or available as timing slack.

Two slots can work, but gives less tolerance to flash latency.

---

## APIs To Use

### Preferred execution time APIs

Use these in the normal execution path:

```c
EXTERNAL_FLASH_ReadInstructionPage(instruction_offset, instruction_page_buffer, length);
EXTERNAL_FLASH_WriteResultPage(result_page_buffer, valid_length);
```

These APIs are page scoped and use DMA internally.

### Package upload APIs

The test package receive path should program instructions before execution using:

```c
EXTERNAL_FLASH_StartInstructionUpload(instruction_length);
EXTERNAL_FLASH_WriteInstructionBytes(chunk, length);
EXTERNAL_FLASH_FinishInstructionUpload();
```

When the package receive path already has page sized instruction spans, it may use:

```c
EXTERNAL_FLASH_WriteInstructionPage(page_buffer, valid_length);
```

Execution must not start until `EXTERNAL_FLASH_FinishInstructionUpload()` succeeds.

---

## Instruction Queue

The instruction queue should use page sized slots.

Each refill should normally call:

```c
EXTERNAL_FLASH_ReadInstructionPage(offset, page_buffer, page_size);
```

For a final partial instruction page:

```c
EXTERNAL_FLASH_ReadInstructionPage(offset, page_buffer, valid_length);
```

Important constraints:

- `offset` must be aligned to the NAND page size.
- `length` must be greater than zero.
- `length` must be no larger than the NAND page size.
- The destination buffer must remain valid and writable until the function returns.

Recommended instruction slot states:

```text
EMPTY
READ_ACTIVE
READY
CONSUMING
REUSABLE
```

Suggested refill policy:

```text
If available instruction bytes <= 1.5 page:
    refill one free instruction page slot
```

Only start a refill when a free page sized slot exists. Do not issue DMA reads into a wrapped circular buffer region.

---

## Result Queue

The result queue should use page sized slots.

During execution, write only full result pages:

```c
EXTERNAL_FLASH_WriteResultPage(page_buffer, page_size);
```

After execution ends, write the final partial page once:

```c
if (final_valid_length > 0U)
{
    EXTERNAL_FLASH_WriteResultPage(final_page_buffer, final_valid_length);
}
```

`external_flash` pads the final partial physical page with `0xFF` internally and commits only `valid_length` logical result bytes.

After a partial page write succeeds, no further result pages should be appended in the same session.

Recommended result slot states:

```text
EMPTY
FILLING
READY
FLASH_ACTIVE
REUSABLE
```

The execution manager may write only to an `EMPTY` or `FILLING` slot. Once a page is passed to `EXTERNAL_FLASH_WriteResultPage()`, it must not be modified until the function returns.

---

## Session Flow

Before execution:

```c
EXTERNAL_FLASH_Init();
EXTERNAL_FLASH_StartSession();
```

Then prime the instruction queue:

```c
EXTERNAL_FLASH_ReadInstructionPage(offset, page_buffer, length);
```

During execution:

```text
execution_manager consumes instruction bytes from flash manager buffers
flash_manager refills instruction page slots as needed
execution_manager appends result bytes into flash manager result slots
flash_manager writes full result pages using EXTERNAL_FLASH_WriteResultPage
```

After execution:

```text
write final partial result page if needed
make committed results available to result_transfer_manager
```

Result transfer should use:

```c
EXTERNAL_FLASH_GetInfo(&info);
EXTERNAL_FLASH_ReadResults(offset, buffer, length);
```

---

## Wear And Erase Policy

The flash manager does not perform wear levelling directly. It must preserve the `external_flash` boundaries so the storage layer can manage wear:

- Program instructions only through `EXTERNAL_FLASH_StartInstructionUpload`, `EXTERNAL_FLASH_WriteInstructionBytes` or `EXTERNAL_FLASH_WriteInstructionPage`, and `EXTERNAL_FLASH_FinishInstructionUpload`.
- Start each execution run with `EXTERNAL_FLASH_StartSession`.
- Write result data only through `EXTERNAL_FLASH_WriteResultPage`.
- Do not call `hw_nand` or `hw_qspi` directly.

Current policy:

- Instruction upload erases only the blocks required for the uploaded instruction image.
- Result session preparation currently prepares the full writable result capacity because the final result length is not known before execution.
- `external_flash` keeps a spare block outside each active map so a program-failed block can be retired and replaced.
- Runtime erase counts are currently RAM only; a metadata partition is reserved for future persistent snapshots.

Future policy:

- Add an erase-ahead or pre-erased result block queue.
- The flash manager can request/maintain erased result blocks outside the hard real-time execution path.
- Result page writes should consume already-erased blocks.
- Direct erase-as-needed during execution should be avoided because block erase latency is too large and non-deterministic.

---

## Error Handling

If any `external_flash` call fails, the flash manager should:

- Stop normal execution flow.
- Preserve the returned error code.
- Report the fault to the system state manager.
- Avoid continuing as if the instruction or result buffers are valid.

Important statuses to handle:

| Status | Meaning |
|---|---|
| `EXTERNAL_FLASH_STATUS_STORAGE_FULL` | Result or instruction partition capacity exhausted. |
| `EXTERNAL_FLASH_STATUS_TIMEOUT` | DMA or NAND operation did not complete. |
| `EXTERNAL_FLASH_STATUS_ECC_ERROR` | Uncorrectable read issue. |
| `EXTERNAL_FLASH_STATUS_PROGRAM_FAIL` | Program failure not recovered. |
| `EXTERNAL_FLASH_STATUS_ERASE_FAIL` | Erase failure not recovered. |
| `EXTERNAL_FLASH_STATUS_NOT_INITIALISED` | Flash stack used before initialisation. |
| `EXTERNAL_FLASH_STATUS_INVALID_ARG` | Buffer, length, or offset contract violation. |

---

## Key Rules

- The flash manager owns the instruction and result RAM buffers.
- The execution manager never calls `external_flash`.
- Use `EXTERNAL_FLASH_ReadInstructionPage()` for instruction queue refills.
- Use `EXTERNAL_FLASH_WriteResultPage()` for result page writes.
- Use page sized slots to avoid DMA wraparound.
- Only write full result pages during execution.
- Write the final partial result page once after execution ends.
- Do not call `hw_nand` or `hw_qspi` directly from the flash manager.
