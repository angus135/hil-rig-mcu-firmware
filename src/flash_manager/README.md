# Flash Manager Design Notes

`result_buffer.c` implements packed result-record production and page draining.
`instruction_buffer.c` implements prefetched instruction views and page release.
`flash_manager.c` connects both buffers to the execution ISR and performs NAND
refill/drain work from its RTOS task. The public instruction-upload lifecycle
and task-side page drain are implemented; result retrieval and application
startup remain to be integrated.

The flash manager is the only normal runtime task that should call `external_flash`.

The execution manager should only interact with RAM buffers owned by the flash
manager. It must not call `external_flash`, `hw_nand`, or `hw_qspi` directly.

---

## Buffer Model

The result buffer is a flat circular byte array backed by three page-sized
regions. Records may cross a page boundary, while NAND drain leases always
expose one page-aligned region. A one-page scratch buffer preserves a contiguous
driver payload pointer only when a record crosses the physical end of the ring.

The instruction buffer uses three page-sized circular slots followed by one
page-sized mirror of slot zero. NAND DMA fills only the three circular slots.
The Flash Manager task updates the mirror whenever slot zero is filled, making a
record that crosses the physical ring end contiguous without an ISR-time payload
copy.

Implemented sizing:

```c
#define RESULT_BUFFER_PAGE_COUNT       3U
#define INSTRUCTION_BUFFER_PAGE_COUNT  3U
```

Both constants are private implementation policy. They may be increased after
worst-case execution bursts and NAND latency are measured. Increasing RAM depth
adds burst tolerance; it does not correct sustained instruction consumption or
result production that exceeds NAND throughput.

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

### Flash-manager storage APIs

The Flash Manager task uses these page-scoped external-flash APIs. The execution
manager does not call them directly:

```c
EXTERNAL_FLASH_ReadInstructionPage(instruction_offset, instruction_page_buffer, length);
EXTERNAL_FLASH_WriteInstructionPage(instruction_page_buffer, valid_length);
EXTERNAL_FLASH_WriteResultPage(result_page_buffer, valid_length);
```

These APIs are page scoped and use DMA internally. They are synchronous from
the Flash Manager's perspective, so page ownership remains simple: the lease is
completed only after the call returns. While DMA is active, however, the Flash
Manager task blocks on the QSPI completion semaphore rather than occupying the
CPU, allowing other ready service tasks to run.

### External-flash instruction upload APIs

The Flash Manager implements instruction upload using:

```c
EXTERNAL_FLASH_StartInstructionUpload(instruction_length);
EXTERNAL_FLASH_WriteInstructionPage(page_buffer, valid_length);
EXTERNAL_FLASH_FinishInstructionUpload();
```

Execution must not start until `EXTERNAL_FLASH_FinishInstructionUpload()` succeeds.

### Flash Manager instruction-upload contract

The Host Interface uses these non-blocking Flash Manager APIs rather than
calling `external_flash` directly:

```c
FLASH_MANAGER_RequestInstructionUploadStart(expected_length_bytes);
FLASH_MANAGER_SubmitInstructionUploadBytes(data, length);
FLASH_MANAGER_RequestInstructionUploadFinish();
```

The intended lifecycle is:

```text
IDLE
  -> PREPARING_INSTRUCTION_UPLOAD
  -> INSTRUCTION_UPLOAD
  -> FINALISING_INSTRUCTION_UPLOAD
  -> IDLE
```

The start request prepares instruction storage asynchronously. The Host
Interface must wait for `FLASH_MANAGER_STATE_INSTRUCTION_UPLOAD` before sending
canonical instruction bytes. Each accepted chunk is copied into Flash
Manager-owned storage before submission returns, allowing the Host Interface to
reuse its receive buffer immediately. Each submission is all-or-nothing and may
contain at most one NAND page. The three-page ring allows host production and
task-context NAND writes to overlap; `BUSY` asks the Host Interface to retry the
identical chunk when insufficient ring capacity is currently available.

Finalisation is accepted only after the declared instruction length has been
accepted. The finish request publishes any final partial RAM page. The Flash
Manager task drains all remaining pages through
`EXTERNAL_FLASH_WriteInstructionPage()`, closes the image through
`EXTERNAL_FLASH_FinishInstructionUpload()`, and releases the upload buffer.
Successful completion returns the manager to `IDLE`; any asynchronous storage
or ownership failure enters `FAULT`.

The Host Interface application layer is responsible for translating and
validating incoming package data into the canonical packed
`[instruction header][payload]...` stream before submission. The Flash Manager
preserves byte order and controls storage lifecycle; it does not interpret
peripheral-specific instruction payloads.

Canonical validation must establish valid peripheral types, channels and
payload schemas, a complete packed stream with no padding, records no larger
than one NAND page, and nondecreasing instruction timestamps. Host transport
chunks do not need to align with records or NAND pages; they are merely ordered
pieces of the declared canonical byte stream.

---

## Instruction Queue

The instruction queue uses page-sized slots and serves packed variable-length
records directly from those slots.

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

Instruction slot states:

```text
EMPTY
FILLING_FROM_NAND
READY
```

Refill policy:

```text
Preparation notification:
    fill sequential empty slots until the buffer is full or the image is loaded

Execution consumes the last byte held by a page:
    mark that page EMPTY
    notify the Flash Manager task from the ISR

Refill notification:
    fill sequential empty slots until backpressure or end of image
```

Notification bits may coalesce, so the task processes every currently available
slot on each wake. `xTaskNotifyFromISR()` is called only when a page is released,
not for every instruction. The outer timer ISR defers `portYIELD_FROM_ISR()`
until its complete execution sequence has finished.

The Flash Manager task cannot refill RAM until that ISR returns. The three-page
preload must therefore cover the maximum instruction bytes that one timer
iteration can consume, and sustained NAND refill throughput must exceed
sustained execution consumption. Event-driven notification removes polling
latency but cannot compensate for insufficient buffer depth or NAND throughput.

Only start a refill when the next sequential page slot is empty. Do not issue a
DMA read into a slot still referenced by the execution manager.

---

## Result Queue

The execution timer ISR reserves one packed record at a time through the public
Flash Manager API:

```c
FLASH_MANAGER_ReserveResultRecordFromISR(payload_capacity_bytes, &write_lease);
FLASH_MANAGER_CommitResultRecordFromISR(&write_lease, timestamp,
                                        peripheral_type, channel,
                                        actual_payload_length_bytes,
                                        &higher_priority_task_woken);
```

The outer timer ISR accumulates `higher_priority_task_woken` and calls
`portYIELD_FROM_ISR()` only after the complete execution sequence finishes. A
ready task can therefore run after ISR return, never partway through execution.

Each stored record contains `FlashManagerResultHeader_T` followed immediately
by the actual payload bytes. Unused reservation capacity is not committed.

Peripheral DMA populates driver-owned buffers asynchronously. When a result is
required, the execution ISR reserves Flash Manager storage and the selected
driver synchronously copies one stable measurement into `write_lease.payload`.
That copy must finish before commit, and neither DMA nor the driver may retain
or write through the lease after commit, cancellation, or ISR return.

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

After a partial-page write succeeds, no further result pages may be appended in
the same session.

Implemented result slot states:

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
they own different regions. The execution ISR is the only record producer and
the Flash Manager task is the only NAND drain consumer. ISR-facing reserve,
cancel, and commit calls never take a mutex or block. Task-side drain acquire
and completion operations use short critical sections so the execution ISR
cannot interrupt buffer metadata changes. NAND programming occurs outside the
critical section with interrupts enabled. Payload filling is protected by the
active record reservation, and a `DRAINING` page remains immutable until
`RESULT_BUFFER_CompleteDrain()`.

---

## Initialisation and Session Flow

Once during system startup, after the generated QSPI handle has been adopted:

```c
EXTERNAL_FLASH_Init();
```

Firmware startup makes this call after adopting the generated QSPI handle.
`FLASH_MANAGER_Init()` must then initialise the manager mutex and both buffers
before the Flash Manager task is allowed to run. The task creation remains
commented out in `app_main.c` until that startup order is connected.

Before each execution, the Run State Manager requests asynchronous preparation:

```c
FLASH_MANAGER_RequestExecutionPreparation();
```

The Flash Manager task calls `EXTERNAL_FLASH_StartSession()`, resets the result
buffer, reads the committed instruction length, prepares the instruction buffer,
and preloads every available instruction slot. It changes to
`FLASH_MANAGER_STATE_EXECUTING` only after all preparation operations succeed.
The Run State Manager must observe that state before starting the execution
timer.

During execution:

```text
execution_manager consumes instruction bytes from flash manager buffers
flash_manager refills instruction page slots as needed
execution_manager appends result bytes into flash manager result slots
flash_manager writes full result pages using EXTERNAL_FLASH_WriteResultPage
```

The instruction API uses a cached hot-path contract. The first peek copies the
fixed eight-byte header into an aligned view while leaving the payload zero-copy
in Flash Manager-owned RAM. If its timestamp belongs to a future execution
tick, later peeks return the same prepared view through a short cached branch;
they do not copy, reparse, or advance it. Consume advances a cached record
pointer and two offsets exactly once. Page release and refill notification occur
only on the less frequent boundary path.

The Execution Manager processes the ordered stream from its head:

```text
instruction timestamp > current tick: retain the cached view and stop this tick
instruction timestamp == current tick: execute, consume, and peek the next record
instruction timestamp < current tick: declare an execution-overrun fault
```

A late instruction is not consumed. It means the configured work could not be
completed within its real-time deadline and the test is infeasible. Bring-up
detects this at runtime; future feasibility validation should reject such a
test before execution begins.

The instruction stream is trusted to have been canonicalised before it reaches
NAND. The execution path retains only the length bounds needed to prevent a
stored record from exceeding one NAND page or the declared instruction image.
Lifecycle and call-order validation belongs outside the per-instruction path.

The common peek and consume paths contain no private helper calls. Tiny shared
addressing helpers are declared inline, while page-boundary bookkeeping is
explicitly cold and out of line. The generated CubeIDE Release configuration
currently uses `-Os` without an explicit LTO option, so cross-translation-unit
inlining of the public Flash Manager wrapper should be evaluated from target
Release assembly before changing the generated project settings.

Instruction-stream exhaustion is independent of execution-session completion.
After the final stored instruction is consumed, instruction peek returns
`FLASH_MANAGER_INSTRUCTION_END_OF_STREAM` while the Flash Manager remains
`EXECUTING`. Measurements and result logging may continue. A final-measurement
instruction or another future mechanism is interpreted outside the Flash
Manager and eventually asks the Run State Manager to stop the test.

If execution reaches a record whose bytes have not been loaded,
`FLASH_MANAGER_INSTRUCTION_NOT_BUFFERED` represents a real-time underrun and the
Flash Manager latches `FAULT`. A corrupt stored record and a NAND refill failure
also latch `FAULT`.

After execution, the Run State Manager stops the execution timer, ensures the
active ISR has returned, and requests asynchronous finalisation:

```c
FLASH_MANAGER_RequestResultFinalisation();
```

The Flash Manager task then:

```text
publish the final partial page if needed
drain every remaining full or partial page
verify RESULT_BUFFER_IsDrainComplete()
invalidate the instruction read session and any outstanding instruction view
enter FLASH_MANAGER_STATE_RESULTS_READY
```

Finalisation fails if an execution write lease remains active. The execution
path must therefore commit or cancel every lease before its timer ISR returns.
Instruction exhaustion is not required: explicit finalisation closes whatever
instruction position remains after the execution ISR has stopped.

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

If a result-page write or drain completion fails, the current implementation:

- Preserves buffer ownership for diagnosis or explicit recovery.
- Stops the active drain pass instead of retrying automatically.
- Enters `FLASH_MANAGER_STATE_FAULT`.
- Ignores queued drain work while faulted.

Lifecycle integration must also:

- Stop normal execution flow.
- Preserve the returned error code.
- Report the fault to the system state manager.
- Avoid continuing as if the instruction or result buffers are valid.

An execution overrun (`instruction timestamp < current tick`) is detected by
the Execution Manager rather than the Flash Manager. It ends the test as an
infeasibility fault. The Run State Manager must stop the timer before deciding
whether to preserve already committed results through normal finalisation. No
discard/abort lifecycle is currently exposed by the Flash Manager.

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
- The execution ISR uses only the non-blocking Flash Manager instruction and
  result APIs.
- Repeated peeks before consumption return the same cached instruction view;
  consume each instruction exactly once when its timestamp equals the current
  tick.
- A future instruction ends processing for the current tick; a late instruction
  is an execution-overrun fault and is not consumed.
- The execution manager never calls `external_flash` or takes an RTOS mutex.
- Use `EXTERNAL_FLASH_ReadInstructionPage()` for instruction queue refills.
- Use `EXTERNAL_FLASH_WriteResultPage()` for result page writes.
- Use page sized slots to avoid DMA wraparound.
- Notify instruction refill only when consumption releases a page.
- Defer any ISR-requested task yield until the complete timer execution sequence
  has finished.
- Only write full result pages during execution.
- Write the final partial result page once after execution ends.
- Do not call `hw_nand` or `hw_qspi` directly from the flash manager.
