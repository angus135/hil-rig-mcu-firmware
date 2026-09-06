# Host interface: DEV-138 Transport hardware test

## Purpose

This directory contains the temporary firmware half of the DEV-138 Transport/Application hardware-test harness on branch `test/DEV-138--protocol-test`. It exercises the shared C Transport implementation over STM32 USB CDC and the shared public Application codec for Test Configuration, fixed Test Instruction, and fixed Test Result messages.

This branch is **not intended to merge into `main`**. Reusable USB or host-interface pieces may be extracted into later production PRs, but the `HRTP` ECHO/STATUS diagnostics and `application_test_harness` result oracle are test-only behavior.

The harness does not configure or operate real HIL peripherals. It does not implement variable instruction/result data, execution/global control, System Information, Application Response, or Application Error workflows.

## Shared protocol dependency

The shared protocol repository is a Git submodule at:

```text
src/host_interface/shared_protocol
```

The supplied ZIP contains the protocol working tree but not the parent repository's usable submodule Git metadata, so the exact protocol commit must be recorded from the parent firmware checkout before hardware evidence is captured. The public protocol version in this supplied snapshot is `0.1.0`.

Clone with submodules:

```sh
git clone --recurse-submodules https://github.com/angus135/hil-rig-mcu-firmware.git
```

For an existing checkout:

```sh
git submodule update --init --recursive src/host_interface/shared_protocol
```

Do not copy protocol files into this repository or edit the submodule for firmware-specific behavior.

## Architecture

`HOST_INTERFACE_Task` is the only firmware owner of the Transport context. The task:

1. initializes `hw_usb`;
2. initializes one statically backed Transport context with role `HIL_TRANSPORT_ROLE_RIG`;
3. calls `HW_USB_Monitor_Process()` every service iteration after successful USB initialization;
4. detects USB configured/deconfigured transitions in task context;
5. retains unconsumed USB RX bytes until Transport consumes them;
6. services Transport processing even when no new USB bytes arrive;
7. drains Transport events;
8. holds Transport output pinned until `HW_USB_Transmit()` accepts the complete item;
9. commits accepted Transport output exactly once;
10. reads at most one Transport Application payload while the one-slot response/result buffer is available;
11. dispatches payloads whose first four bytes are exactly `HRTP` to the diagnostic ECHO/STATUS codec;
12. dispatches every other payload directly to the shared `HIL_APPLICATION_*` codec through `application_test_harness`; and
13. retains an encoded HRTP response or Test Result until Transport accepts it.

No Transport API is called from an ISR, the USB receive callback, or the generated CDC code. The Transport library has no USB or FreeRTOS dependency; this integration owns the clock, USB operations, scheduling, link lifecycle, and all static caller buffers.

## Build integration

### Host CMake build

`src/host_interface/CMakeLists.txt` explicitly disables the protocol's host-only build options before adding the submodule:

```text
HIL_RIG_PROTOCOL_BUILD_TESTS=OFF
HIL_RIG_PROTOCOL_BUILD_EXAMPLES=OFF
HIL_RIG_PROTOCOL_BUILD_PYTHON=OFF
```

It then uses:

```cmake
add_subdirectory(shared_protocol EXCLUDE_FROM_ALL)
target_link_libraries(host_interface PRIVATE hil_rig_protocol::hil_rig_protocol)
```

The protocol target is consumed without source filtering. Its Transport, Application, and version sources therefore come from the shared protocol target itself; the submodule remains unchanged.

Normal host-test commands are:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Use the repository's sanitizer/static-analysis configuration as available in the development environment. Do not enable the protocol's own tests, examples, or Python bindings through the firmware build.

### STM32CubeIDE

The CubeIDE project links the repository `src` tree beneath `Core/Src/Application`, so its managed builder can recursively discover the protocol submodule. `.cproject` therefore constrains both Debug and Release configurations as follows:

- adds `../../src/host_interface/shared_protocol/include` to the C compiler include paths;
- excludes `shared_protocol/tests`;
- excludes `shared_protocol/examples`;
- excludes `shared_protocol/bindings` and `shared_protocol/python`;
- excludes `shared_protocol/scripts`;
- must **not** exclude `shared_protocol/src/application`, because the hardware-test harness now uses the real shared Application codec;
- excludes `shared_protocol/src/transport/internal/extended`;
- leaves `src/version.c`, `src/application/*`, and the common/MVP Transport C sources discoverable exactly once.

No `.project` linked-resource change is required because the existing `Core/Src/Application -> ../src` link already exposes the new submodule.

Open `f446ze_cubeide_project` in STM32CubeIDE, select either Debug or Release, clean the configuration, then build. The stored project artifacts identify STM32CubeIDE 1.18.1 with GNU Tools for STM32 13.3.rel1 / GCC 13.3.1, but the actual local toolchain in use should be recorded when hardware validation is performed.

## Firmware flashing and CDC expectations

The target described by the checked-in `.ioc` is NUCLEO-F446ZE / STM32F446ZETx. Build the desired CubeIDE configuration, program the board using the normal ST-LINK Debug/Run workflow, then allow the USB CDC device to enumerate on the host.

The protocol byte stream and debug text must not share the CDC stream. This branch emits only encoded Transport bytes over `hw_usb`.

`HW_USB_Is_Connected()` reports that the USB device is in `USBD_STATE_CONFIGURED` and CDC class data exists. It can detect normal deconfiguration/disconnect and re-enumeration. The STM32 USB stack used here does **not** provide a reliable indication that a desktop process currently has the virtual COM port open, so Transport `CONNECTED` means configured CDC, not “serial application opened”.

On a link transition the firmware clears caller-owned pending RX and harness-response state and asks `hw_usb` to discard stale queued protocol bytes. A still-active CDC-owned transmit region is preserved while the configured link remains up because invalidating a buffer already owned by the USB stack would be unsafe. A disconnected link clears the complete TX ring.

## Effective Transport configuration

The hardware-test configuration starts from `HIL_TRANSPORT_Default_Config()` and overrides named fields in `protocol_test_config.h`:

| Setting | Value |
| --- | ---: |
| Role | RIG |
| Maximum Application message | 512 bytes |
| Maximum encoded Transport output | 640 bytes |
| Session seed | `HIL_TRANSPORT_SESSION_SEED_INVALID` |
| Initial reliable sequence | 0 |
| Connection timeout | 0 ms |
| Retransmit timeout | 100 ms |
| Maximum retries | 5 |
| Operating mode | `HIL_TRANSPORT_OPERATING_MODE_NORMAL` |

With the Transport profile above in the supplied protocol snapshot, `HIL_TRANSPORT_Required_Storage_Size()` reports 3289 bytes and the public workspace alignment is 16 bytes on the host build used during implementation. Firmware reserves an aligned 4096-byte workspace and refuses initialization if the runtime-required size exceeds it or the alignment check fails.

## Buffering and service bounds

| Buffer / cadence | Capacity |
| --- | ---: |
| Transport static workspace | 4096 bytes |
| Application receive buffer | 512 bytes |
| Pending harness response | 512 bytes |
| Transport output copy buffer | 640 bytes |
| Caller-owned pending USB RX | 1024 bytes |
| USB RX stream | 1024 bytes |
| USB TX ring | 1024 bytes |
| USB read chunk | 256 bytes |
| Owner-task service period | 1 ms |

The 1024-byte USB TX ring is intentionally larger than the configured 640-byte maximum encoded Transport output. `HW_USB_Transmit()` remains all-or-nothing: it either copies the complete requested block into USB-owned storage or accepts none of it.

One service iteration is bounded to 4 normal receive operations, 2 zero-byte receive operations, 2 process calls, 4 output operations, and 8 event reads. Continuous traffic or malformed input therefore cannot make `HOST_TRANSPORT_Service()` loop indefinitely.

Unconsumed input is retained byte-for-byte. A partial `HIL_TRANSPORT_Receive_Bytes()` result removes only the consumed prefix. Zero-byte receive is used after partial progress or when draining an Application message, an event, or committed output releases Transport capacity.

## HRTP test envelope

The temporary Application-message envelope has a 16-byte header. Multi-byte fields are little-endian and are encoded/decoded manually rather than by casting packed structs.

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 4 | ASCII `HRTP` |
| 4 | 1 | envelope version, currently `1` |
| 5 | 1 | opcode |
| 6 | 2 | flags, currently must be `0` |
| 8 | 4 | request ID |
| 12 | 4 | payload length |
| 16 | N | payload |

The complete Application message length must equal `16 + payload_length`.

Supported opcodes:

| Opcode | Meaning |
| ---: | --- |
| `0x01` | ECHO request |
| `0x81` | ECHO response |
| `0x02` | STATUS request |
| `0x82` | STATUS response |

### ECHO

ECHO preserves the request ID and returns the request payload byte-for-byte with opcode `0x81`. The maximum payload is the configured 512-byte Application capacity minus the 16-byte envelope header, so the maximum ECHO payload is **496 bytes**.

### STATUS schema version 2

STATUS responses use opcode `0x82`, preserve the request ID, and contain a 128-byte payload of 32 little-endian `uint32_t` values. The complete HRTP response is therefore 144 bytes.

| Index | Field |
| ---: | --- |
| 0 | status schema version (`2`) |
| 1 | current link state |
| 2 | link generation |
| 3 | total Transport event count |
| 4 | USB RX bytes |
| 5 | USB TX bytes |
| 6 | total Transport Application messages received |
| 7 | responses/results submitted to Transport |
| 8 | USB busy retries |
| 9 | invalid HRTP messages |
| 10 | maximum service gap in ms |
| 11 | public Transport session state |
| 12 | compatibility profile ID (`0x41505031`, `APP1`) |
| 13 | protocol version major |
| 14 | protocol version minor |
| 15 | protocol version patch |
| 16 | Application codec initialized |
| 17 | Application initialization status |
| 18 | non-HRTP Application messages received |
| 19 | Application decode failures |
| 20 | Application semantic rejections |
| 21 | Application encode failures |
| 22 | configurations accepted |
| 23 | instructions accepted |
| 24 | results encoded |
| 25 | Application harness state |
| 26 | next expected tick |
| 27 | active expected tick count |
| 28 | last Application status |
| 29 | last successfully decoded Application message type |
| 30 | current/last accepted configuration digest |
| 31 | last accepted instruction digest |

The version fields are sourced from the public protocol version macros. In the supplied protocol snapshot they report `0.1.0`.

A payload is treated as HRTP only when bytes 0 through 3 are exactly `HRTP`. Invalid HRTP version, flags, opcode, or declared length increments the invalid-HRTP diagnostic and produces no response. Any non-HRTP payload, including malformed input, is passed directly to the Application codec and is accounted for as an Application decode/semantic failure rather than an HRTP error.

No additional harness checksum is used. Transport integrity plus the shared Application codec and deterministic test oracle provide the hardware-test checks.

## Backpressure behavior

The harness has one fixed pending-response/result slot. A complete HRTP response or encoded Test Result is built once. If `HIL_TRANSPORT_Submit_Application_Data()` returns `NOT_READY` or `CAPACITY_EXHAUSTED`, the exact bytes remain in that slot and are retried in later service iterations.

While the slot is occupied, the next Transport Application message remains unread. A Test Instruction advances `next_expected_tick` only after its Test Result has been encoded successfully into this caller-owned buffer.

## Debugger-visible diagnostics

`g_host_transport_diagnostics` can be inspected without sending text over CDC. It includes:

- initialization attempt/result, required/available workspace, and alignment;
- USB initialization result;
- current Transport/link/session/failure state and link generation;
- service count and current/maximum service gap;
- host-interface task stack high-water value when sampled on the MCU;
- USB RX/TX byte counts, USB RX stream drops, and TX ring high-water mark;
- pending RX length/high-water, bytes offered/consumed, partial and zero-progress receive counts, and receive status counts;
- USB busy retries and maximum consecutive busy iterations;
- output peek, acceptance, commit, and commit-failure counts;
- total Transport Application request, response/result submission/retry/failure, and invalid-HRTP counts;
- Application codec initialization, decode/semantic/encode counters, accepted configuration/instruction/result counts, workflow state, expected ticks, last status/type, and semantic digests through STATUS v2;
- total and per-type Transport event counts, the most recent event, and an eight-entry recent-event ring; and
- bounded-operation budget exhaustion count.

Diagnostic counters saturate at `UINT32_MAX` rather than wrapping. `link_generation` also uses the saturating diagnostic increment and advances once for each observed disconnected-to-connected transition.

## First USB ECHO hardware check

After the matching Python runner exists:

1. Flash this firmware and allow the USB CDC device to enumerate.
2. Start the Python HOST endpoint and establish a new Transport session with the RIG firmware.
3. Send an empty ECHO and verify an empty `0x81` response with the same request ID.
4. Send a small text ECHO and compare all payload bytes.
5. Send binary bytes containing zero and delimiter-like values and compare all bytes.
6. Send a 496-byte ECHO payload and compare all bytes.
7. Send STATUS and inspect link/session/event/service/USB counters.
8. Reset the board while the Python host remains active.
9. Wait for USB re-enumeration, establish a new Transport session, and complete another ECHO.
10. Inspect `g_host_transport_diagnostics` for unexpected events, commit failures, sustained USB-busy growth, large service gaps, or operation-budget exhaustion.

Successful completion of these steps is hardware validation. A source build or host unit test alone must not be treated as proof of USB or MCU behavior.
