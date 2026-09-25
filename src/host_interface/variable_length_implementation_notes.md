# Shared Protocol: Variable-Length, Update-Based Architecture
## Technical Specification & Implementation Reference

**Status**: Implemented & Verified in `hil-rig-protocol` (branch `feature/DEV-163--VariableMsg`)  
**Scope**: Shared C11 Protocol Application Layer & Python CFFI Bindings

---

## 1. Executive Summary & Design Decision Justification

This document specifies the completed protocol extension for **variable-length, update-based instruction and result messages** (`UPDATE_INSTRUCTION` [Type 21] and `VARIABLE_TEST_RESULT` [Type 34]) in the shared protocol library (`hil-rig-protocol`).

### Core Problem Statement
* **Fixed Snapshot Baseline (`TEST_INSTRUCTION` [Type 17] & `TEST_RESULT` [Type 32])**:
  * Carries static 50-byte instruction / 39-byte result payloads containing full state for all 18 channels (10 digital, 6 analog, 2 PWM) on every tick.
  * Cannot accommodate variable-length serial streams (UART, SPI, CAN).
  * Forces the host to construct dense state snapshots and forces the MCU to perform delta checks across 18 channels on every tick.
* **Update-Based Architecture (`UPDATE_INSTRUCTION` [Type 21] & `VARIABLE_TEST_RESULT` [Type 34])**:
  * Transmits only sparse peripheral updates and variable-length serial data that change or transmit at a given tick boundary.
  * Provides native support for streaming serial communications (UART, SPI, CAN).
  * Direct execution on MCU: declared operations are dispatched in $O(1)$ time without delta comparison.
  * Reduces wire transmission volume by over 99% on sparse test sequences.
  * Preserves 100% backward compatibility with Types 17 and 32.

### Design Decisions & Architectural Rationale

1. **Streaming Flags in Payload Header (`flags`), Not Envelope `subtype`**:
   * Envelope `subtype` is reserved for transport/session metadata.
   * Placing `flags: COMPLETE_TICK (0x00)` and `HAS_MORE_CHUNKS (0x01)` in the payload header keeps message streaming semantics isolated to the Application layer.
2. **Zero Dynamic Memory Allocation in C Library**:
   * All decoding and sizing use caller-provided decode storage aligned to `HIL_APPLICATION_DECODE_STORAGE_ALIGNMENT`.
   * Sizing queries calculate exact required buffer space; no internal heap allocations occur.
3. **4-Byte Aligned TLV Records**:
   * Operation and captured records are padded with `0x00` to 4-byte boundaries, enabling direct aligned pointer dereferencing on 32-bit ARM MCUs without unaligned memory faults.
4. **Checked Arithmetic Throughout**:
   * All size computations, alignment operations, and bounds checks use checked arithmetic helper functions (`HIL_APPLICATION_Checked_Add_Size`, `HIL_APPLICATION_Align_Up_Size`, etc.) to guarantee memory safety against integer overflow attacks.
5. **Cross-Platform C11 & MSVC Portability**:
   * `HIL_APPLICATION_DECODE_STORAGE_ALIGNMENT` supports both C11 `_Alignof(max_align_t)` (GCC/Clang) and MSVC's C runtime fallback `__alignof(long double)`.

---

## 2. Wire Format Specification

Every Application message is encapsulated by the standard **23-byte Common Envelope**:

```text
 0       1       2       3                              19      20      21      23
+-------+-------+-------+-------------------------------+-------+-------+-------+------------------+
| major | minor |has_id |          Test ID              | type  |subtype|payload|     payload      |
|  1 B  |  1 B  |  1 B  |           16 B                |  1 B  |  1 B  |len 2B |       N B        |
+-------+-------+-------+-------------------------------+-------+-------+-------+------------------+
|<-------------------------------- 23-byte envelope --------------------------->|<--- N bytes ---->|
```

Both Types 21 and 34 require:
* `has_test_id == 1` (opaque 16-byte `test_id`)
* `subtype == HIL_APPLICATION_MESSAGE_SUBTYPE_NONE (0)`

---

### A. `UPDATE_INSTRUCTION` (Message Type 21)

#### Payload Header (8 bytes, 4-byte aligned)
```text
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                          tick_number                          | (4 bytes LE)
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|operation_count|     flags     |           reserved            | (4 bytes)
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

| Offset | Width | Type | Field | Description |
| :---: | :---: | :---: | :--- | :--- |
| `0` | 4 bytes | `uint32_t` LE | `tick_number` | Execution tick boundary where operations execute ($< \text{max\_expected\_tick\_count}$). |
| `4` | 1 byte | `uint8_t` | `operation_count` | Number of distinct operations packed in this message ($1 \dots 255$). |
| `5` | 1 byte | `uint8_t` | `flags` | Streaming control: `0x00` = `COMPLETE_TICK`, `0x01` = `HAS_MORE_CHUNKS`. |
| `6` | 2 bytes | `uint16_t` LE | `reserved` | Alignment padding; must be encoded as `0x0000` and validated as `0`. |

#### Logical Operation Record Layout (4-byte aligned TLV)
```text
+------------------------------+--------------------+-----------------------+-------------------------+-------------+
| Peripheral Type (1B: uint8_t)| Channel (1B: uint8)| Payload Length (2B LE)| Operation Payload (N B) | Pad (0..3B) |
+------------------------------+--------------------+-----------------------+-------------------------+-------------+
|<--------------------------- 4-byte Logical Header Word ------------------->|<--------- N bytes ----->|
```
$$\text{pad\_bytes} = (4 - (N \pmod 4)) \pmod 4$$

#### Supported Logical Operations:

| `peripheral_type` | Channel | Payload Format | Description |
| :---: | :---: | :--- | :--- |
| `DIGITAL_OUTPUT` (2) | `0` (bank) | 2 bytes LE: `uint16_t` mask | Bits 0..9: logical channels 0..9 (`0`=LOW, `1`=HIGH). Bits 10..15 reserved (0). |
| `ANALOG_OUTPUT` (4) | `0..5` | 4 bytes LE: `uint32_t microvolts` | Voltage in $\mu\text{V}$. |
| `PWM_OUTPUT` (6) | `0` (LV), `1` (HV)| 6 bytes LE: `period_ns` (4B) + `duty_permyriad` (2B) | Physical units ($0 \dots 10000$ duty). |
| `UART` (16) | `0..1` | $N$ bytes: Raw TX bytes | Variable-length UART TX stream ($1 \le N \le 255$). |
| `SPI` (17) | `0..1` | 1B `num_packets` + $P$ bytes `lengths` + $N$ data | Packetized SPI TX with chip-select framing. |
| `CAN` (19) | `0..1` | $12 \times K$ bytes: Sequence of CAN frames | Standard 11-bit ID (2B LE), DLC (1B), Data (8B), Reserved (1B = 0). |

---

### B. `VARIABLE_TEST_RESULT` (Message Type 34)

#### Payload Header (12 bytes, 4-byte aligned)
```text
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                          tick_number                          | (4 bytes LE)
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|  record_count |   condition   |     flags     |    reserved   | (4 bytes)
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                         problem_detail                        | (4 bytes LE)
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

| Offset | Width | Type | Field | Description |
| :---: | :---: | :---: | :--- | :--- |
| `0` | 4 bytes | `uint32_t` LE | `tick_number` | Execution tick boundary where measurements were captured. |
| `4` | 1 byte | `uint8_t` | `record_count` | Number of captured peripheral records ($0 \dots 255$). May be `0` for fault reports. |
| `5` | 1 byte | `uint8_t` | `condition` | Result condition: `0` = `OK`, `1` = `PARTIAL`, `2` = `EXECUTION_PROBLEM`. |
| `6` | 1 byte | `uint8_t` | `flags` | Streaming control: `0x00` = `COMPLETE_TICK`, `0x01` = `HAS_MORE_CHUNKS`. |
| `7` | 1 byte | `uint8_t` | `reserved` | Alignment padding; must be encoded as `0x00` and validated as `0`. |
| `8` | 4 bytes | `uint32_t` LE | `problem_detail` | Hardware/diagnostic error code when `condition != OK` (`0` when `condition == OK`). |

#### Supported Captured Records:

| `peripheral_type` | Channel | Captured Data Format | Description |
| :---: | :---: | :--- | :--- |
| `DIGITAL_INPUT` (1) | `0` (bank) | 2 bytes LE: `uint16_t` bitmask | Captured input pinmask for channels 0..9. Bits 10..15 reserved (0). |
| `ANALOG_INPUT` (3) | `0..1` | 4 bytes LE: `uint32_t microvolts` | Sampled analog input in $\mu\text{V}$. |
| `PWM_INPUT` (5) | `0..1` | 6 bytes LE: `period_ns` (4B) + `duty_permyriad` (2B) | Measured PWM period and duty cycle. |
| `UART` (16) | `0..1` | $N$ bytes: Exact raw RX bytes | Variable-length UART RX stream ($1 \le N \le 255$). |
| `SPI` (17) | `0..1` | $N$ bytes: Exact raw RX bytes | Variable-length SPI RX stream ($1 \le N \le 255$). |
| `CAN` (19) | `0..1` | $12 \times K$ bytes: Received CAN frames | Sequence of 12-byte CAN frames. |

---

## 3. Python and Host Interface Implications

The introduction of `UPDATE_INSTRUCTION` and `VARIABLE_TEST_RESULT` fundamentally enhances how host test suites, runners, and codecs interact with the HIL-RIG compared to the legacy fixed snapshot types (`TEST_INSTRUCTION` [17] and `TEST_RESULT` [32]).

### Detailed Comparison: Instructions

| Feature | Legacy `TEST_INSTRUCTION` [Type 17] | New `UPDATE_INSTRUCTION` [Type 21] |
| :--- | :--- | :--- |
| **Python Type** | `TestInstruction` | `UpdateInstruction` |
| **Data Structure** | Dense, fixed tuples:<br>• `digital_outputs: tuple[DigitalOutputValue, ...] * 10`<br>• `analog_outputs: tuple[AnalogOutputValue, ...] * 6`<br>• `pwm_outputs: tuple[PWMOutputValue, ...] * 2` | Sparse, dynamic tuple:<br>• `operations: tuple[LogicalOperation, ...]`<br>• `flags: int` (`COMPLETE_TICK` or `HAS_MORE_CHUNKS`) |
| **Host Authoring Model** | **Stateful full snapshot**: Host must maintain a full model of all 18 channels and transmit values for all channels at every tick, even if unchanged. | **Event/delta driven**: Host specifies only the channels that transition or transmit data at a given tick. |
| **Serial / Stream Support** | **None**: Cannot transmit UART bytes, SPI transactions, or CAN frames. | **Native**: Full support for raw UART TX, multi-packet SPI transactions, and CAN frame bursts. |
| **Streaming / Multi-Chunk** | Single fixed 50-byte payload per tick. Cannot exceed envelope capacity. | Supports chunked streaming: large transfers (e.g. 1 KB UART) are split across multiple Type 21 messages sharing the same `tick_number` with `HAS_MORE_CHUNKS`. |
| **MCU Ingestion & Dispatch** | **$O(N)$ Delta Calculation**: MCU compares each of the 18 incoming channels against previous tick state to detect transitions. | **$O(1)$ Direct Hardware Dispatch**: MCU directly executes each `LogicalOperation` without delta evaluation. |
| **Payload Overhead** | 50 bytes wire payload on every single tick (fixed). | 8-byte header + $(4 + N + \text{pad})$ per active operation. Zero bytes sent for idle channels. |

### Detailed Comparison: Results

| Feature | Legacy `TEST_RESULT` [Type 32] | New `VARIABLE_TEST_RESULT` [Type 34] |
| :--- | :--- | :--- |
| **Python Type** | `TestResult` | `VariableTestResult` |
| **Data Structure** | Dense, fixed tuples:<br>• `digital_inputs: tuple[DigitalInputValue, ...] * 10`<br>• `analog_inputs: tuple[AnalogInputValue, ...] * 2`<br>• `pwm_inputs: tuple[PWMInputValue, ...] * 2`<br>• `condition: ResultCondition` | Sparse, dynamic tuple:<br>• `records: tuple[CapturedRecord, ...]`<br>• `condition: ResultCondition`<br>• `flags: int`<br>• `problem_detail: int` |
| **Fault Reporting** | Condition indicates status, but all 14 fixed input fields must still be populated with dummy/default data. `problem_detail` is a generic field. | Supports **empty result records** (`records = ()`) when reporting pure execution faults, with explicit 32-bit `problem_detail` hardware error code. |
| **Serial Stream Capture** | **None**: Cannot capture received UART bytes, SPI data, or CAN frames. | **Native**: Returns exact received bytes for UART/SPI and packed 12-byte CAN frames. |
| **Decode Storage Contract** | Fixed statically known decode storage size (0 bytes dynamic storage). | Decoded spans borrow caller-owned storage allocated to `HIL_APPLICATION_DECODE_STORAGE_ALIGNMENT`. Storage queried via `HIL_APPLICATION_Decode_Storage_Size`. |
| **Host Processing** | Host must parse and inspect all channel fields to see if relevant signals changed. | Host iterates only the records received for active peripherals. |

### Code Usage Examples (Python)

#### 1. Generating Sparse Update Instructions
```python
import hil_rig_protocol as hil

# Single digital output change at tick 42
update = hil.UpdateInstruction(
    test_id=hil.TestId(b"0123456789abcdef"),
    tick_number=42,
    flags=0,  # COMPLETE_TICK
    operations=(
        # Drive digital pin 3 HIGH (mask bit 3 = 0x0008)
        hil.LogicalOperation(
            peripheral_type=hil.PeripheralType.DIGITAL_OUTPUT,
            channel=0,
            payload=b"\x08\x00",
        ),
        # Transmit 4 bytes over UART Channel 0
        hil.LogicalOperation(
            peripheral_type=hil.PeripheralType.UART,
            channel=0,
            payload=b"PING",
        ),
    ),
)

codec = hil.ApplicationCodec(hil.ApplicationConfig())
wire_bytes = codec.encode(update)
```

#### 2. Consuming Variable Test Results
```python
result = codec.decode(wire_bytes)
if isinstance(result, hil.VariableTestResult):
    if result.condition != hil.ResultCondition.OK:
        print(f"Hardware fault on tick {result.tick_number}: error 0x{result.problem_detail:08X}")
    for record in result.records:
        if record.peripheral_type == hil.PeripheralType.UART:
            print(f"UART RX (Channel {record.channel}): {record.data.decode('ascii', errors='replace')}")
        elif record.peripheral_type == hil.PeripheralType.CAN:
            print(f"CAN RX: {len(record.data) // 12} frames captured")
```

---

## 4. Summary of Implemented Codebase Changes

### C Public Headers (`include/hil_rig_protocol/application/`)
* **`application_message.h`**: Registered `UPDATE_INSTRUCTION (21)` and `VARIABLE_TEST_RESULT (34)` in `HIL_Application_Message_Type_T` and added `body` union members.
* **`application_instruction.h`**: Defined `HIL_Application_Logical_Operation_T`, `HIL_Application_Update_Instruction_T`, and instruction streaming flags.
* **`application_result.h`**: Defined `HIL_Application_Captured_Record_T`, `HIL_Application_Variable_Test_Result_T`, and result streaming flags.
* **`application_types.h`**: Added `PeripheralType` definitions and MSVC-compatible `HIL_APPLICATION_DECODE_STORAGE_ALIGNMENT`.

### C Codec Implementation (`src/application/`)
* **`application_size.c`**: Implemented sizing logic with 4-byte padding residue computation.
* **`application_validation.c`**: Comprehensive validation for ticks, duplicate channel prevention, duty cycle bounds, SPI packet structures, and CAN frame layouts.
* **`application_encoding.c`**: Serialization for headers, TLV headers, payloads, and pad bytes.
* **`application_decoding.c`**: Implemented `Update_Instruction_Scan`, `Variable_Test_Result_Scan`, `Aligned_Records_Scan`, and zero-copy partition decoders.

### Python Bindings & Package (`python/hil_rig_protocol/`)
* **`bindings/python/cdef.py`**: Added CFFI declarations for all new structs, enums, and prototypes.
* **`python/hil_rig_protocol/application_types.py`**: Added frozen, slotted dataclasses (`LogicalOperation`, `UpdateInstruction`, `CapturedRecord`, `VariableTestResult`) and `PeripheralType`.
* **`python/hil_rig_protocol/_application_conversion.py`**: Implemented bidirectional native C $\leftrightarrow$ Python dataclass conversions with owner retention.
* **`python/hil_rig_protocol/application.py`**: Extended `ApplicationCodec.encode()` and `decode()` with storage expansion bounds checks.

---

## 5. Verification & Test Suite Summary

### C++ GoogleTest Suites (100% Pass)
* **`test_application_update_instruction.cpp` (814 lines)**: Full golden vector comparisons, validation boundaries, duplicate channel rejection, SPI/CAN format checks, and full facade round-trips.
* **`test_application_variable_result.cpp` (666 lines)**: Result golden vectors, empty/fault reports, `problem_detail` validation, and decode storage calculations.
* **`test_application_aligned_records.cpp` (503 lines)**: Exhaustive parametric tests for every payload length (1..255), padding residue, and channel boundary across both message types.
* **`test_application_workflow.cpp`**: Cleaned and validated against full message workflow regressions.

### Python pytest Suites (100% Pass)
* **`test_application_update_instruction.py` (426 lines)**: CFFI parity, GC memory lifecycle preservation, wire truncation checks, and parametric payload tests.
* **`test_public_api.py` & `test_application_types.py`**: Public export surface verification and native enum contract validation.

### Code Quality & Static Analysis (All Green)
* `clang-format --dry-run --Werror`: Zero format violations across all `.c`, `.cpp`, and `.h` files.
* `ruff check .` & `ruff format --check .`: Zero lint or format issues across all Python files.
* `mypy`: Strict typing passed with zero errors across the entire package.
