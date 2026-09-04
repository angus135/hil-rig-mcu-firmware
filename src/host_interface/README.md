# Host Interface

## Overview

The Host Interface owns host transport and application-layer packaging. For
instruction upload it converts incoming protocol data into the canonical packed
instruction stream and submits that stream through the Flash Manager. It does
not access `external_flash`, NAND, QSPI, or Flash Manager-owned RAM directly.

The current task only monitors USB periodically. The instruction-upload flow
below is an implementation contract for the next Host Interface work.

The current console commands are temporary integration stimuli. The Host
Interface does not yet populate `TEST_CONFIGURATION`, submit RSM lifecycle
requests, or transmit stored results.

## Run State Manager integration contract

The Host Interface will own the host-originated side of one lifecycle:

1. Accept a new package and submit
   `RUN_STATE_MANAGER_RequestPackageReceive()`; wait for
   `TEST_PACKAGE_RECEIVE` with no pending RSM transition.
2. Receive and validate the complete package. Upload the canonical instruction
   stream using the Flash Manager flow below.
3. Translate the package's versioned wire representation into a fully populated
   `DutDriverConfiguration_T`, validate every leaf driver configuration, and
   call `TEST_CONFIGURATION_Commit()`.
4. Submit `RUN_STATE_MANAGER_RequestConfiguration()` and wait for `ARMED`.
5. Submit `RUN_STATE_MANAGER_RequestExecution()` only when host policy owns the
   execution trigger. Physical or DUT trigger arbitration belongs in a
   separate system-level component.
6. After the RSM reports `RESULTS_READY`, choose exactly one current result
   disposition:
   - submit `RUN_STATE_MANAGER_RequestRepeat()` to discard these results and
     return to `ARMED` with the same configuration and instructions;
   - submit `RUN_STATE_MANAGER_RequestDiscardResults()` to discard these results,
     clear the configuration, and return to `IDLE`; or
   - submit `RUN_STATE_MANAGER_RequestResultTransfer()` and wait for
     `RESULT_TRANSFER`.
7. For transfer, read the complete result stream through
   `FLASH_MANAGER_ReadResultBytes()`. Handle `BUSY` by retrying without losing
   position and continue until `END_OF_STREAM`.
8. After all bytes are acknowledged by the host protocol, submit
   `RUN_STATE_MANAGER_RequestResultTransferComplete()`, which currently returns
   the lifecycle directly to `IDLE`.

The current state table does not support repeat after a completed transfer. If
that host behaviour is required, add an explicit RSM policy transition rather
than submitting `repeat` after the lifecycle has returned to `IDLE`.

RSM request return values indicate notification delivery only. They do not mean
the event was accepted or completed. Requests use coalescing notification bits,
not a FIFO; the Host Interface must wait for the expected state and
`transition_pending == false` between dependent requests. It should use the
coherent `RUN_STATE_MANAGER_GetStatus()` snapshot and inspect
`last_request_result` when a requested transition does not occur.

The Host Interface must never write RSM state directly, call DUT driver
lifecycle functions, control TIM4, or access external Flash directly.

## Canonical instruction stream

Before starting an upload, the Host Interface must know the complete canonical
byte length and guarantee:

- packed `[ExecutionInstructionHeader_T][operations...]` instructions;
- strictly increasing instruction timestamps;
- exactly one instruction for each output-bearing tick;
- valid operation headers, opcodes, channels, payload layouts, and alignment;
- each complete instruction is no larger than
  `EXECUTION_INSTRUCTION_MAX_SIZE_BYTES`; and
- the submitted byte count exactly matches the declared upload length.

Transport chunks may split instructions or operations and may cross NAND-page boundaries. Each
submission itself must be non-empty and no larger than one NAND page. Because
the current Flash Manager has no upload-cancel API, bring-up should not start an
upload until the Host Interface can guarantee that the complete valid stream
will be supplied.

## Instruction-upload flow

1. Call `FLASH_MANAGER_RequestInstructionUploadStart(total_length)` from task
   context while the manager is `IDLE`.
2. After an accepted request, poll or otherwise observe
   `FLASH_MANAGER_GetState()` until it reports `INSTRUCTION_UPLOAD` or `FAULT`.
3. Submit ordered chunks using
   `FLASH_MANAGER_SubmitInstructionUploadBytes(data, length)`.
4. On `ACCEPTED`, the source buffer may be reused immediately. On `BUSY`, retain
   the exact bytes and retry the same chunk unchanged; no partial copy occurred.
5. After every declared byte is accepted, call
   `FLASH_MANAGER_RequestInstructionUploadFinish()`. Retry unchanged on `BUSY`.
6. After acceptance, wait for `IDLE` to report success or `FAULT` to report
   failure to the host.

Every request status must be handled explicitly. `INVALID_STATE` indicates a
lifecycle error, `INVALID_ARGUMENT` indicates malformed integration input,
`TASK_NOT_READY` indicates incomplete startup, and `NOTIFY_FAILED` places the
Flash Manager in `FAULT`.

The current one-second Host Interface task period is a monitoring placeholder;
upload reception and `BUSY` retry should eventually be event-driven or use a
cadence appropriate to transport and NAND throughput.


---

## Files

| File | Role |
|---|---|
| `host_communications.c/.h` | Host RTOS task and upload lifecycle coordination |
| `test_package_recieve.c/.h` | Future package validation, configuration translation, and canonical conversion |
| `result_send.c/.h` | Future stored-result packaging and transmission |

## Public API

The currently implemented public entry point is `HOST_INTERFACE_Task()`. The
instruction-upload application API and package-conversion API remain to be
defined.
