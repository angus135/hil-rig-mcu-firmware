# DEV-138 Transport hardware-test compatibility manifest

This manifest describes the temporary firmware-side Transport hardware-test integration for PR #63.

The reviewed implementation baseline is immutable and recorded below. The exact final Git commit used for physical hardware testing must be recorded after this change set is committed, before hardware evidence is captured. The Python-side compatibility record must reference that same final firmware commit.

| Item | Value |
| --- | --- |
| Firmware branch | `test/DEV-138--protocol-test` |
| Firmware branch-point/base commit | `7987ca6cb78106530c9362b03a1addc624bede9b` |
| Reviewed PR #63 implementation baseline | `c6c0af2af586108949c30fb4a12df54eb9dd2fda` |
| Final firmware commit used for hardware testing | `TO BE RECORDED AFTER COMMITTING THIS CHANGE SET` |
| Protocol submodule path | `src/host_interface/shared_protocol` |
| Protocol compatibility commit | `a24fccc403007cbf6268ff7d0d21f50566a6b2de` |
| Board | NUCLEO-F446ZE |
| MCU | STM32F446ZETx / STM32F446Z(C-E)Tx |
| STM32CubeIDE version represented by checked-in build artifacts | 1.18.1 |
| STM32 GNU toolchain represented by checked-in build artifacts | GNU Tools for STM32 13.3.rel1, `arm-none-eabi-gcc` 13.3.1 |
| FreeRTOS tick rate | 1000 Hz |
| Service cadence | 1 ms |


## Commit compatibility gate

Before physical testing results are recorded:

1. Commit the firmware changes represented by this compatibility manifest.
2. Replace `TO BE RECORDED AFTER COMMITTING THIS CHANGE SET` with the immutable firmware implementation commit selected for testing. A documentation-only follow-up commit may record that implementation commit without changing the tested firmware code.
3. Record the same firmware implementation commit in the Python hardware-test compatibility evidence.
4. Do not combine hardware results obtained from different firmware implementation commits under one validation record.

## Effective Transport configuration

| Field | Value |
| --- | ---: |
| Role | `HIL_TRANSPORT_ROLE_RIG` |
| `max_application_message_size` | 512 bytes |
| `max_encoded_frame_size` | 640 bytes |
| `session_seed` | `HIL_TRANSPORT_SESSION_SEED_INVALID` |
| `initial_reliable_sequence` | 0 |
| `connection_timeout_ms` | 0 |
| `retransmit_timeout_ms` | 100 |
| `max_retries` | 5 |
| Operating mode | `HIL_TRANSPORT_OPERATING_MODE_NORMAL` |
| Required workspace measured from pinned protocol | 3289 bytes |
| Reserved workspace | 4096 bytes |
| Workspace alignment measured during implementation | 16 bytes |

The runtime results of `HIL_TRANSPORT_Default_Config()`, `HIL_TRANSPORT_Required_Storage_Size()`, and `HIL_TRANSPORT_Init()` remain authoritative. Initialization fails visibly if the required workspace no longer fits the reserved capacity.

## Firmware and USB capacities

| Buffer | Capacity |
| --- | ---: |
| USB RX stream | 1024 bytes |
| USB TX ring | 1024 bytes |
| Caller-owned pending RX | 1024 bytes |
| Per-read USB chunk | 256 bytes |
| Application receive | 512 bytes |
| Pending response | 512 bytes |
| Transport output copy | 640 bytes |
| Maximum ECHO payload | 496 bytes |

## MCU validation record

The following values must be updated after running the hardware test on the actual board:

| Item | Hardware result |
| --- | --- |
| Exact local STM32CubeIDE executable/version used | NOT YET RUN |
| Exact local compiler version used for final flash image | NOT YET RUN |
| Clean Debug MCU build | NOT YET RUN |
| Clean Release MCU build | NOT YET RUN |
| Observed `HOST_INTERFACE_Task` stack high-water | NOT YET MEASURED |
| Empty/small/binary/maximum ECHO | NOT YET RUN |
| STATUS query | NOT YET RUN |
| Reset/re-enumeration/re-session ECHO | NOT YET RUN |
| Long soak result | NOT YET RUN |
