# Execution Manager Design

## Aim

The Execution Manager runs from a 10 kHz timer ISR, so each tick is 100 microseconds. The MCU Host Interface should validate the incoming test and calculate driver inputs during upload. The ISR should only select and run calls scheduled for the current tick, collect measurements, and store their results.

## Current direction

- One flash record represents one execution-driver call.
- The record header contains the timestamp, opcode, channel or target, and payload length.
- The payload contains the values needed for that call.
- Payload layouts belong to the Execution Manager instruction API, not to individual drivers.
- The Host Interface creates valid payloads using only that instruction API.
- An Execution Manager adapter translates each payload into the existing driver function arguments.
- The adapter uses the payload directly from Flash Manager RAM where safe, then the Execution Manager consumes the record.

The draft payload API is in `execution_instruction_payloads.h`.

## Instruction record

```text
┌──────────────────────────────────────────────────┐
│ timestamp │ opcode │ channel │ payload length    │  common header
├──────────────────────────────────────────────────┤
│ operation-specific values                        │  payload
└──────────────────────────────────────────────────┘
```

Records are stored in timestamp order. Several records may have the same timestamp when a tick requires several driver calls. The current Flash Manager was designed around this `[header][payload]` stream, although it can still be changed if this investigation finds a better record boundary.

## Opcode and adapter

The opcode identifies the requested call. Firmware maps it to an approved adapter through a fixed table:

```c
handler = output_handlers[instruction->header.opcode];
status  = handler( instruction );
```

Adapters are needed because driver functions have different signatures. An SPI adapter passes channel, data, packet sizes, and packet count to `EXEC_SPI_Transmit()`. A PWM adapter passes `arr`, `ccr`, and `psc` to the selected PWM function. Both adapters present the same interface to the ISR and convert driver-specific return values into one Execution Manager result.

The Host Interface has already parsed the external format, checked the paired configuration, and calculated the payload values. The adapter only reads that payload and calls the driver.

We could store function pointers instead of opcodes because the MCU creates the flash stream for the current firmware. An opcode remains preferred because it is smaller, easier to validate, and not tied to linked function addresses. Its lookup cost is expected to be small compared with the driver work, but this must be measured at 10 kHz.

## Payload shapes

Payloads have different shapes because the driver calls need different inputs.

### Fixed-size payload

Digital output, PWM, and analogue output have a known maximum layout:

```text
DIGITAL_SET:   [pin mask]
PWM_UPDATE:    [arr][ccr][psc]
ANALOGUE_OUT:  [prepared bytes][byte count]
```

Analogue output uses an Execution Manager-owned payload even though the driver has a similar type. This keeps the Host Interface independent of driver headers. Its adapter translates the payload to the existing driver input.

### Repeated fixed-size items

Each CAN packet has a fixed 12-byte instruction layout containing its ID, DLC, eight data bytes, and an explicit zero byte. A CAN payload is simply one or more of these packets:

```text
CAN_TRANSMIT: [packet][packet][packet]...
```

The adapter calculates packet count from the common payload length. CAN has its own Execution Manager packet type so driver structure padding cannot silently change the stored format.

### Raw variable-length data

UART needs only a byte range:

```text
UART_TRANSMIT: [byte][byte][byte]...
```

Channel and length are already in the common header, so a UART payload structure would add no information.

### Metadata followed by variable-length data

SPI needs a packet count, one size for each packet, and the combined packet data:

```text
SPI_TRANSMIT: [packet count][size 0]...[size n][packet data...]
```

`ExecutionSpiTransmitPayloadPrefix_T` represents the fixed packet-count field. The size array and data are still part of the same payload; they follow the prefix because their lengths vary. A normal C structure cannot contain both variable-length arrays without reserving their maximum sizes in every instruction.

SPI packet-size alignment remains unresolved. The final record layout must allow the adapter to pass a valid `uint32_t` size array to the driver without making a large ISR copy.

### How variable-length payloads are read

The Host Interface builds only the bytes needed for that call and writes their exact total into the common payload-length field. Flash Manager treats those bytes as opaque and uses the length to find the next record. No maximum-sized UART, CAN, or SPI object is stored when the call contains less data.

Each opcode defines how its adapter divides the payload:

```text
UART: data length = payload length

CAN:  packet count = payload length / packet size

SPI:  packet count = first payload field
      sizes start  = after packet count
      data start   = after packet_count size entries
      data length  = payload length - bytes before data
```

Upload validation checks these calculations, their limits, and integer overflow before the record reaches flash. For SPI it also checks that the sum of the packet sizes equals the data length. At execution time, the adapter uses the already-validated count and offsets to create pointers into the Flash Manager payload, calls the driver, and then allows the record to be consumed. The variable data is not copied merely to place it in another Execution Manager structure.

The final layout must ensure that any typed region starts at a suitable address. Byte data has no special alignment requirement, but SPI's `uint32_t` size array and the fields in CAN packets do. This may require aligned record starts or explicit padding defined by the instruction format; it must not depend on accidental compiler padding.

## Instruction lifetime

Flash Manager returns a read-only pointer into its instruction RAM. That pointer remains valid until the record is consumed:

```text
peek -> check timestamp -> select adapter -> call driver -> consume
```

The record is consumed after the driver returns successfully, not after the peripheral finishes transmitting. A successful asynchronous call must mean the driver has copied or queued everything it needs and will not retain the instruction pointer. The ISR must not wait for peripheral or DMA completion.

If the driver rejects the call, the instruction is not consumed as though it succeeded. The run should fault according to the final fault policy.

## Timestamp rules

Timestamps are Execution Manager ticks. An instruction runs only when its timestamp equals the current tick. A future instruction remains buffered; a late instruction faults and is not executed.

Every measurement collected in one ISR invocation receives that invocation's tick. The tick advances once after all work for it is complete.

## Scope and remaining decisions

I2C instructions are excluded while the known hardware fault keeps I2C disabled. They can be added after the hardware and execution path are validated.

The remaining decisions are:

- Final header fields and widths.
- Payload alignment and validation rules.
- Ordering of calls with the same timestamp.
- Initial tick value.
- Whether outputs run before or after measurements.
- Fault handling after part of a tick has already executed.
- Timing and feasibility limits for each opcode and complete tick.

Before ISR integration, the driver paths also need review: analogue output can reach a mutex-taking readiness path, SPI multi-packet enqueue is not fully all-or-nothing on failure, and CAN transmit builds and converts a maximum-sized packet array on the caller's stack.
