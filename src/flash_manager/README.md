# Flash Manager Design Notes

`result_buffer.c` implements packed result-record production and page draining.
The runtime task in `flash_manager.c`, instruction buffering, public wrappers,
and result retrieval remain to be integrated.

The flash manager is the only normal runtime task that should call `external_flash`.

The execution manager should only interact with RAM buffers owned by the flash
manager. It must not call `external_flash`, `hw_nand`, or `hw_qspi` directly.

---

## Recommended Buffer Model

The result buffer is a flat circular byte array backed by three page-sized
regions. Records may cross a page boundary, while NAND drain leases always
expose one page-aligned region. A one-page scratch buffer preserves a contiguous
driver payload pointer only when a record crosses the physical end of the ring.

The future instruction buffer should use page-sized slots because instruction
DMA reads do not require packed variable-length records.

Recommended initial sizing:

```c
#define FLASH_MANAGER_RESULT_PAGE_COUNT       3U
#define FLASH_MANAGER_INSTRUCTION_PAGE_COUNT  3U
```

`RESULT_BUFFER_Init()` obtains the selected NAND page size from
`EXTERNAL_FLASH_GetInfo()`. Static storage is sized using
`EXTERNAL_FLASH_MAX_PAGE_SIZE_BYTES`, while all active page calculations use the
reported runtime geometry.

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

Only start a refill when a free page-sized slot exists. Do not issue DMA reads
into a wrapped circular buffer region.

---

## Result Queue

The execution path reserves one packed record at a time:

```c
RESULT_BUFFER_ReserveRecord(payload_capacity_bytes, &write_lease);
RESULT_BUFFER_CommitRecord(&write_lease, timestamp, peripheral_type,
                           channel, actual_payload_length_bytes);
```

Each stored record contains `FlashManagerResultHeader_T` followed immediately
by the actual payload bytes. Unused reservation capacity is not committed.

During execution, the flash-manager task acquires and writes only full pages:

```c
RESULT_BUFFER_AcquireDrainPage(&drain_lease);
EXTERNAL_FLASH_WriteResultPage(drain_lease.page_data,
                               drain_lease.valid_length_bytes);
RESULT_BUFFER_CompleteDrain(&drain_lease, nand_write_succeeded);
```

After execution ends, finalisation publishes at most one partial page:

```c
RESULT_BUFFER_Finalise();
```

`external_flash` pads the final partial physical page with `0xFF` internally and
commits only `valid_length` logical result bytes.

After a partial page write succeeds, no further result pages should be appended in the same session.

Recommended result slot states:

```text
EMPTY
FILLING
READY_TO_DRAIN
DRAINING
```

The execution manager may write only to an `EMPTY` or `FILLING` page. Once a
page is passed to `EXTERNAL_FLASH_WriteResultPage()`, it must not be modified
until the function returns.

### Concurrency contract

One record write lease and one drain lease may exist simultaneously because
they own different regions. Result-buffer functions do not contain an RTOS
mutex; the future public flash-manager wrappers and task must serialise calls
that mutate or inspect result-buffer state. Payload filling occurs outside that
short bookkeeping lock, protected by the active record reservation. A
`DRAINING` page remains immutable until `RESULT_BUFFER_CompleteDrain()`.

---

## Initialisation and Session Flow

Once during system startup, after the generated QSPI handle has been adopted:

```c
EXTERNAL_FLASH_Init();
```

Firmware startup makes this call after adopting the generated QSPI handle.
Integration of the upload, session, task-driven result drain, instruction
refill, and result transfer calls is still required in the placeholder manager.

Before each execution:

```c
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
wait until RESULT_BUFFER_IsDrainComplete() reports true
make committed results available to the future result-transfer path
```

If the final result length is exactly page aligned, there is no separate
external-flash finalize call. Leave the session readable for result transfer;
`external_flash` advances its result wear-rotation cursor when the next
`EXTERNAL_FLASH_StartSession()` begins.

The future result-transfer path should use:

```c
EXTERNAL_FLASH_GetInfo(&info);
EXTERNAL_FLASH_ReadResults(offset, buffer, length);
```

---

## Wear And Erase Policy

The flash manager does not perform wear levelling directly. It must preserve
the `external_flash` boundaries so the storage layer can manage wear:

- Program instructions only through `EXTERNAL_FLASH_StartInstructionUpload`,
  `EXTERNAL_FLASH_WriteInstructionBytes`, or
  `EXTERNAL_FLASH_WriteInstructionPage`, followed by
  `EXTERNAL_FLASH_FinishInstructionUpload`.
- Start each execution run with `EXTERNAL_FLASH_StartSession`.
- Write result data only through `EXTERNAL_FLASH_WriteResultPage`.
- Do not call `hw_nand` or `hw_qspi` directly.

Current policy:

- Instruction upload erases only the blocks required for the uploaded instruction image.
- Result session preparation currently prepares the full writable result
  capacity because the final result length is not known before execution.
- `external_flash` keeps a spare block outside each active map so a
  program-failed block can be retired and replaced.
- Exact-page result sessions do not need a flush/finalize call; the next
  `EXTERNAL_FLASH_StartSession()` advances the wear cursor for the previous
  committed result length.
- Runtime erase counts are currently RAM only; a metadata partition is reserved
  for future persistent snapshots.

Future policy:

- Add an erase-ahead or pre-erased result block queue.
- The flash manager can request or maintain erased result blocks outside the
  hard real-time execution path.
- Result page writes should consume already-erased blocks.
- Direct erase-as-needed during execution should be avoided because block erase
  latency is too large and non-deterministic.

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
