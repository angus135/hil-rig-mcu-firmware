# Host Interface

## Overview

The Host Interface owns host transport and application-layer packaging. For
instruction upload it converts incoming protocol data into the canonical packed
instruction stream and submits that stream through the Flash Manager. It does
not access `external_flash`, NAND, QSPI, or Flash Manager-owned RAM directly.

The current task only monitors USB periodically. The instruction-upload flow
below is an implementation contract for the next Host Interface work.

## Canonical instruction stream

Before starting an upload, the Host Interface must know the complete canonical
byte length and guarantee:

- packed `[FlashManagerInstructionHeader_T][payload]...` records with no padding;
- nondecreasing instruction timestamps;
- valid peripheral types, channels, and payload schemas;
- each complete record is no larger than one NAND page; and
- the submitted byte count exactly matches the declared upload length.

Transport chunks may split records and may cross NAND-page boundaries. Each
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
| `test_package_recieve.c/.h` | Future package validation and canonical conversion |
| `result_send.c/.h` | Future stored-result packaging and transmission |

## Public API

The currently implemented public entry point is `HOST_INTERFACE_Task()`. The
instruction-upload application API and package-conversion API remain to be
defined.
