# Execution Manager Design

## Aim

The Execution Manager owns deterministic work at 100 Hz, 1 kHz, or 10 kHz.
The execution ISR must be bounded, must not wait for asynchronous operations,
and should do as little calculation and copying as possible. The Host Interface
therefore validates downloaded tests and prepares driver inputs before
execution.

## Current instruction model

One variable-length instruction contains all output operations scheduled for
one tick:

```text
[instruction header][encoded operation bytes]
```

`execution_instruction.h` is the implemented instruction contract. Its fixed
header contains:

- the Execution Manager tick;
- the total number of encoded operation bytes;
- the number of operations; and
- a reserved byte.

The complete instruction may be up to
`EXECUTION_INSTRUCTION_MAX_SIZE_BYTES` (4096 bytes). Flash Manager treats the
operation bytes as opaque and returns a read-only view of the complete
instruction. This gives the ISR one peek, one timestamp comparison, and one
consume for an output-bearing tick, rather than repeating those actions for
each driver call.

The operation encoding is not implemented yet. The current payload proposal is
kept separately in `execution_operation_payloads.h` so it cannot be mistaken
for the implemented instruction header.

## Operations, opcodes, and adapters

Each operation will represent one execution-driver call. Its encoding needs to
identify an opcode, a channel or target, and the length of its operation-specific
payload. The exact operation header and alignment rules remain to be decided.

Firmware will map each opcode to an approved adapter. Adapters are required
because the existing driver functions have different signatures. For example,
an SPI adapter must provide channel, packet data, packet sizes, and packet
count, while a PWM adapter provides `arr`, `ccr`, and `psc`. Each adapter will
present one common interface to the output loop and translate its result into a
common Execution Manager result.

An opcode and a stored function pointer are both workable. Function pointers
avoid an indexed lookup, but occupy more space and tie the stored stream to
linked addresses. Opcodes remain preferred because they are compact, easy to
validate, and stable across firmware builds. The lookup cost is expected to be
small compared with the driver call, but must be included in 10 kHz timing
measurements.

The Host Interface creates the operation data using only the Execution Manager
format. It may use the paired peripheral configuration to pre-calculate driver
inputs, but it must not store driver structures or depend on driver headers.
The adapter performs only the small final translation into an existing driver
call.

## Payload shapes

The current proposal in `execution_operation_payloads.h` reflects the existing
driver APIs:

- Digital output, PWM, and analogue output use fixed prepared values.
- CAN uses a variable-length sequence of fixed 12-byte Execution Manager packet
  layouts.
- UART uses its payload length and the raw transmit bytes, so a fixed wrapper
  would add no information.
- SPI uses a packet count, one size per packet, and concatenated packet data.
- I2C remains excluded while its hardware path is unavailable.

Variable-length data is stored at its actual length rather than reserving the
maximum size in every operation. Upload-time validation must check all counts,
lengths, offsets, configured limits, and integer overflow before the instruction
is written to flash. SPI validation must also prove that its packet sizes account
for every data byte.

Typed payload access and SPI's `uint32_t` size array require a defined alignment
rule. This remains unresolved and must be settled as part of the common
operation encoding. The ISR must not make a large copy merely to repair
alignment.

## Instruction timing and lifetime

Instructions are stored in strictly increasing tick order, with at most one
instruction for each output-bearing tick:

- A future instruction remains buffered and unconsumed.
- An instruction matching the current tick has all operations applied in stored
  order, then the complete instruction is consumed once.
- A late instruction is an execution-overrun fault and is not executed or
  consumed.
- End of the instruction stream does not itself end the test because later
  measurements may still be required.

The Flash Manager view remains valid until the instruction is consumed. The
instruction can be consumed after every driver call has accepted its data; the
ISR does not wait for a peripheral or DMA transfer to finish. A successful
asynchronous driver call must copy or queue everything it needs and must not
retain a pointer into Flash Manager instruction storage.

If a call is rejected, the run faults. The policy for an instruction in which
earlier operations succeeded before a later operation failed remains to be
defined.

## Measurements and ticks

The Execution Manager owns the authoritative tick. Every enabled input runs on
every tick, and each stored measurement is paired with that tick. The tick
advances once per completed ISR invocation.

The ordering of measurement collection relative to output calls is not yet
final. Whichever order is selected must be consistent so a timestamp has one
clear meaning.

## Remaining operation decisions

Before implementing the output loop, decide:

- the common operation header fields, widths, and alignment;
- the adapter function type and common result enum;
- validation responsibilities retained in the ISR;
- behavior after a rejected operation or partially applied instruction;
- measurement versus output ordering and the initial tick definition; and
- measured feasibility limits for each opcode and a worst-case complete tick.

Driver ISR-safety concerns are tracked as future driver work. The Execution
Manager design currently assumes the selected execution driver calls are
bounded and ISR safe.
