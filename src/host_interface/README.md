# Host interface: DEV-138 Transport hardware test

## Purpose

This directory contains the temporary firmware half of the DEV-138 Transport hardware-test harness on branch `test/DEV-138--protocol-test`. The branch exists to exercise the shared C Transport implementation over the STM32 USB CDC link before the production HIL-RIG Application layer is connected.

This branch is **not intended to merge into `main`**. Reusable USB or host-interface pieces may be extracted into later production PRs, but the `HRTP` test envelope and its ECHO/STATUS behavior are test-harness code only.

The firmware does not define production IDC messages, test instructions, result messages, storage interactions, or execution-manager behavior.

## Shared protocol dependency

The shared protocol repository is a Git submodule at:

```text
src/host_interface/shared_protocol
```

Hardware-test compatibility is against protocol commit:

```text
a24fccc403007cbf6268ff7d0d21f50566a6b2de
```

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
10. reads at most one Application message while the one-slot response buffer is available; and
11. decodes the temporary `HRTP` envelope and submits ECHO or STATUS responses with Transport backpressure.

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

At the pinned protocol revision the base C target also attaches its separate Application sources unconditionally. This transport-only branch filters only `src/application/*` from that target after the submodule is added. The submodule itself is unchanged. Transport sources and `src/version.c` continue to come from the protocol target's own source selection.

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
- excludes `shared_protocol/src/application`;
- excludes `shared_protocol/src/transport/internal/extended`;
- leaves `src/version.c` plus the common and MVP Transport C sources discoverable exactly once.

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

At protocol commit `a24fccc403007cbf6268ff7d0d21f50566a6b2de`, `HIL_TRANSPORT_Required_Storage_Size()` reports 3289 bytes and the public workspace alignment is 16 bytes on the host build used during implementation. Firmware reserves an aligned 4096-byte workspace and refuses initialization if the runtime-required size exceeds it or the alignment check fails.

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

### STATUS schema version 1

STATUS responses use opcode `0x82`, preserve the request ID, and contain a 48-byte payload of twelve little-endian `uint32_t` values:

| Payload offset | Field |
| ---: | --- |
| 0 | status schema version (`1`) |
| 4 | current link state |
| 8 | link generation |
| 12 | total Transport event count |
| 16 | USB RX bytes read by the integration |
| 20 | USB TX bytes accepted into USB-owned storage |
| 24 | Application requests received |
| 28 | responses submitted to Transport |
| 32 | USB TX busy/full retries |
| 36 | invalid harness-message count |
| 40 | maximum service gap in ms |
| 44 | public Transport session state |

Invalid magic, envelope version, non-zero flags, opcode, or declared length is a harness error rather than a Transport error. Version 1 does not define an error response opcode: the message is rejected, `invalid_harness_messages` is incremented, and no response is submitted.

No additional harness checksum is used. Transport integrity plus byte-for-byte ECHO comparison is the test oracle.

## Backpressure behavior

The harness has one fixed pending-response slot. A complete decoded response is built once. If `HIL_TRANSPORT_Submit_Application_Data()` returns a temporary not-ready/capacity status, the exact response bytes remain in that slot and are retried in later service iterations.

While the response slot is occupied, the next Application message remains unread in Transport. It is not consumed and discarded. This matches the intended initial Python runner behavior of one request at a time and keeps the test handler replaceable by a production Application layer later.

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
- Application request, response submission/retry/failure, and invalid-envelope counts;
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
