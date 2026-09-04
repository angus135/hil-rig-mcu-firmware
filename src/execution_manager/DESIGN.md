# Execution Manager Design

## Goals

The Execution Manager runs from a 10 kHz timer ISR, giving each tick 100 microseconds for scheduled outputs, measurements, result storage, and bookkeeping. The MCU Host Interface should validate and calculate as much as possible while translating the external test package. The ISR should only do work that must happen at the scheduled tick.

## Selecting an output function

Each stored instruction must identify an output function. It could contain either an opcode or a function pointer. Both are valid because the MCU Host Interface creates the internal instruction stream. A function pointer avoids one lookup, but is larger, harder to inspect, and tied to the firmware build that created it. An opcode is compact, easy to validate, and remains meaningful when function addresses change.

The recommendation is to store an opcode. Firmware maps it to an approved adapter through a fixed table. The lookup should be small compared with the driver call and its data, even with tens of calls in a tick, but this must be measured on the target at 10 kHz. Storing function pointers remains an option if measurement shows a useful improvement.

## Calling different driver APIs

Driver APIs take different arguments. For example, SPI transmit takes a channel, data, packet sizes, and packet count, while PWM takes `arr`, `ccr`, and `psc`. A small adapter for each opcode gives the ISR one common calling interface:

```c
handler = output_handlers[instruction->header.opcode];
status  = handler( instruction );
```

The adapter reads an Execution Manager payload prepared by the Host Interface, passes its values to the existing driver API, and converts the driver's return value into an Execution Manager result. It must not parse the external host format, inspect configuration, calculate outputs, or build complex inputs in the ISR.

The final payload layout must still guarantee safe access to typed values. In particular, SPI packet sizes must be correctly aligned before they can be passed directly as a `uint32_t` array.

## Instruction records

The current design uses one instruction per useful driver call. During upload, the Host Interface combines compatible changes at the same timestamp when the driver supports batching. This reduces both instruction count and driver-call overhead.

This decision led to Flash Manager's packed `[header][payload]` record design. The header contains fields shared by every instruction: timestamp, opcode, channel or target, and payload length. The payload contains the inputs specific to that call. Several records may share a timestamp when a tick requires several driver calls.

Flash Manager can still be redesigned if another record boundary offers a meaningful improvement. One record per driver call remains the recommendation because it matches the current APIs and their batching support without adding another mixed-operation list inside each record.

### Payloads required by each driver

| Operation | Header supplies | Payload supplies |
|---|---|---|
| Digital set/reset | Timestamp and opcode | Physical pin mask |
| Analogue output batch | Timestamp and opcode | One `ExecutionAnalogueOutputBatchPayload_T` |
| PWM update | Timestamp, opcode, LV/HV channel | `arr`, `ccr`, `psc` |
| CAN transmit | Timestamp, opcode, CAN channel, length | Array of up to 19 `ExecutionCanPacket_T` values |
| SPI transmit | Timestamp, opcode, SPI channel, length | Packet count, packet sizes, packet data |
| UART transmit | Timestamp, opcode, UART channel, length | Raw transmit bytes |

Values already available from the header are not repeated in the payload. UART uses the common payload length as its transmit length. CAN packet count is the payload length divided by `sizeof(EXEC_CAN_Packet_T)`. SPI data length is checked against the sum of its packet sizes rather than stored separately.

Every payload type is owned by the Execution Manager instruction API. The Host Interface depends on this API, not on individual driver headers. Flash Manager treats the payload as opaque bytes, and the adapter is the only code that translates it into driver arguments. This keeps the stored format independent of driver structures and avoids a broad driver refactor.

CAN and analogue output therefore have their own instruction types even though similar types already exist in their drivers. Their adapters must translate those values into the existing driver inputs. That small cost is accepted to preserve the module boundary and should be measured with the rest of the ISR. Driver APIs remain unchanged by this design; they should only be reconsidered later if target measurements identify the translation as a real timing problem.

The MCU Host Interface creates the analogue batch during upload; the external host supplies requested channels and voltages, not DAC frames. The CAN payload stores an explicit zero byte after each packet so its 12-byte layout does not rely on hidden compiler padding.

I2C opcodes are deliberately excluded while the known I2C hardware fault keeps the channels disabled during configuration. Including them now would define instructions that no valid test can execute. They can be added when the hardware and execution path have been validated; the opcode list is designed to be extended.

The draft layouts are in `execution_instruction_payloads.h`. They remain provisional until alignment and validation rules are settled.

## Instruction ownership and copies

Flash Manager gives the Execution Manager a read-only view into its RAM buffer. The view remains valid until the instruction is consumed, so the adapter can use payload pointers without an extra Execution Manager copy:

```text
peek -> confirm timestamp -> look up adapter -> call adapter -> consume
```

Consumption happens after the adapter returns successfully, not after the peripheral finishes its asynchronous work. A successful driver call must mean it has copied or queued everything it needs and will not retain the instruction pointer. The ISR must never wait for peripheral or DMA completion.

If a driver retains the payload for DMA, the instruction cannot be consumed safely. The driver must instead copy the data into storage it owns, or Flash Manager must gain a way to retain the instruction buffer. The current recommendation is for drivers to accept or copy all required data before returning.

A rejected driver call must not consume the instruction as though it succeeded. It should fault the run according to the final execution fault policy.

## Timestamp ownership

Instruction timestamps are Execution Manager ticks. An instruction runs only when its timestamp equals the current tick. A future instruction remains buffered; a late instruction faults and is not executed.

Every measurement collected during one ISR invocation receives that invocation's tick. The counter advances once after all work for the tick is complete. The initial tick value and whether outputs run before or after measurements remain open decisions.

## Remaining decisions

- Final header fields and field widths.
- Exact payload encoding, alignment, and validation rules.
- Ordering of driver calls with the same timestamp.
- Initial tick value and output-versus-measurement order.
- Detailed fault behavior after partial execution of a tick.
- Target timing and feasibility limits for each opcode and complete tick.

The driver paths also require review before ISR integration: analogue output can currently reach a mutex-taking readiness path, SPI multi-packet enqueue is not fully all-or-nothing on failure, and CAN transmit builds and converts a maximum-sized packet array on the caller's stack.
