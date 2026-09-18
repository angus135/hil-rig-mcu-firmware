# AGENTS.md

This file defines the default expectations for AI coding agents working on embedded C/C++ repositories.

The priorities are, in order:

1. Functional correctness.
2. Deterministic embedded behaviour.
3. Concurrency, ISR, ownership, and lifetime safety.
4. Minimal, targeted changes.
5. Clear, testable APIs and state transitions.
6. Maintainability without unnecessary abstraction.

Preserve established project architecture and conventions unless the task specifically requires changing them.

Do not retrofit untouched code merely to satisfy this guide. Apply these requirements to code added or materially changed by the task, and use them when reviewing code.
## 1. Working Style

### Keep changes tightly scoped

- Implement the requested behaviour with the smallest sensible change set.
- Do not perform unrelated refactors, clean-ups, renames, formatting changes, or optimisations.
- Do not redesign an API or subsystem simply because another design is possible.
- Preserve existing public APIs, wire formats, task topology, ownership rules, and behaviour unless changing them is explicitly part of the task.
- Preserve unrelated local changes already present in the working tree.
- Do not modify generated code, vendor code, submodules, reference implementations, or compatibility copies unless explicitly instructed.
- Do not introduce new tasks, queues, timers, threads, callbacks, buffers, or architectural layers merely to make an implementation easier.
- Prefer local fixes over repository-wide abstractions when the abstraction would only be used once.
- Only create a shared helper or constant when it removes genuine duplication or establishes an important invariant.

### Understand before editing

Before changing code:

- Read the relevant implementation, header, tests, and call sites.
- Trace the complete data and control flow involved in the requested behaviour.
- Identify ownership, lifetime, concurrency, ISR context, buffer capacity, and state-machine implications.
- Check whether similar functionality already exists elsewhere in the repository.
- Treat existing tests and externally visible behaviour as part of the contract.
- Do not infer protocol or hardware behaviour from names alone. Verify it in the implementation and relevant specifications.
- If behaviour, intent or use is unclear or contradictory based on context, then investigate first, then ask one consolidated question only when unresolved ambiguity could materially alter behavior, architecture, safety, or API compatibility (as many questions should be asked at once rather than asking one at a time)

### Do not hide uncertainty with code

- Do not add speculative checks, casts, retries, sleeps, state resets, or fallback behaviour without a concrete reason.
- Do not silently change semantics to make a test pass.
- Do not modify production code solely to make testing easier unless the new seam is also a valid production design.
- If a failure is unrelated to the requested work, report it rather than changing unrelated code.

## 2. C and C++ Style

Follow the repository formatter when one exists. Avoid formatting churn outside edited lines.

### Avoid unnecessary casts

Do not add casts unless they are actually required for:

- correctness;
- compilation;
- API compatibility; or
- an intentional representation/type conversion.

In particular:

- Do not cast merely to silence a warning.
- Do not cast a function to a compatible function-pointer type.
- Do not use casts as defensive decoration.
- Do not cast ignored return values to `void`.
- In C++, do not use C-style casts unless explicitly required.

Prefer allowing valid implicit C conversions when the source and destination types are already compatible. A redundant cast can hide a real type mismatch later.

Example:

```c
config->handler = handler_fn;
```

Prefer this over:

```c
config->handler = (handler_fn_t)handler_fn;
```

### Passing state

- Pass mutable state structures by pointer/reference.
- Do not make unnecessary copies of state or large records.
- Keep ownership explicit.
- Avoid arbitrary untyped pointers when a concrete typed pointer, ID, or statically owned object can express the relationship.

### Types and sizes

- Use types that represent the actual data being stored.
- Do not narrow sequence numbers, lengths, identifiers, counters, or protocol values without a defined reason.
- At hardware and wire-format boundaries, use explicit-width integer types.
- Keep length/capacity arithmetic in types capable of representing the full valid range.
- Be especially careful with signed/unsigned comparisons and arithmetic.
- Do not rely on casts to suppress those problems.

## 3. API and Architecture Preferences

### Prefer simple, explicit APIs

- Keep APIs small and direct.
- Avoid unnecessary wrappers, indirection, class-like layers, registries, and generic frameworks in C.
- Do not introduce abstractions that obscure ownership or control flow.
- Separate policy from mechanism where it materially improves testability or portability.

### Preserve invariants at the correct layer

Do not push responsibility to callers when the module can enforce its own invariant cheaply and reliably.

Examples include:

- bounds;
- valid configuration;
- legal state transitions;
- queue capacity;
- frame size;
- sequence advancement;
- ownership state;
- retry accounting.

Validate before mutating state whenever possible.

A failed operation should not partially consume ownership, sequence numbers, retry counts, queue entries, or other state unless that partial transition is explicitly part of the design.

### Programmer errors versus runtime errors

Use assertions for internal/programmer invariants such as:

- invalid static configuration;
- impossible internal states;
- required pointers supplied by trusted code;
- compile-time or startup assumptions that must always hold.

Use normal error handling for:

- malformed external input;
- queue/full conditions;
- timeouts;
- unavailable hardware;
- disconnected peers;
- protocol errors;
- other conditions expected during normal operation.

Do not fabricate or collapse meaningful status codes if the underlying layer already exposes the distinction required by the caller.

## 4. Determinism and Memory

Embedded code should be deterministic by default.

Prefer:

- static storage;
- caller-owned buffers;
- caller-supplied workspaces;
- bounded queues;
- fixed-capacity structures;
- explicit ownership.

Do not introduce heap allocation into embedded runtime paths unless the repository already uses it for that purpose or the task explicitly requires it.

For every buffer or queue change, reason about:

- maximum payload;
- framing overhead;
- encryption/authentication overhead;
- alignment/padding;
- delimiter ownership;
- DMA requirements;
- producer/consumer concurrency;
- full/empty behaviour;
- what happens on failure after partial progress.

Do not reserve bytes for framing elements that are not actually stored inside the bounded object.

Do not clear large buffers unnecessarily when resetting metadata or lengths is sufficient and safe.

## 5. Concurrency, FreeRTOS, and ISR Safety

Concurrency bugs are correctness bugs, not style issues.

### Shared state

For every variable shared between tasks, threads, and/or ISRs, establish:

- who owns it;
- who may read it;
- who may write it;
- what synchronisation protects it;
- whether an operation must be atomic;
- what lifetime guarantees apply.

Prefer a clear single owner plus message passing where that fits the architecture.

Keep critical sections as short as possible.

Do not perform expensive work such as:

- COBS decoding;
- CRC calculation;
- parsing;
- large copies;
- logging;
- blocking operations;

inside a critical section unless it is demonstrably necessary.

### ISR rules

Inside an ISR:

- Use only APIs documented as ISR-safe.
- Use the FreeRTOS `...FromISR` variants where required.
- Use `BaseType_t xHigherPriorityTaskWoken` correctly.
- Request a context switch with the appropriate `portYIELD_FROM_ISR(...)` mechanism when a higher-priority task is unblocked.
- Do not block.
- Do not allocate dynamically.
- Do not perform large or unbounded computations.
- Keep hardware acknowledgement and data capture minimal, then defer work to task context where practical.

Do not assume `volatile` provides mutual exclusion or makes a compound operation atomic.

### RTOS topology

Do not create a new task or timer as a workaround for sequencing, ownership, or testing unless the task itself is justified by the system architecture.

Do not change priorities, stack sizes, scheduler timing, or task lifetime without considering the full system effect.

## 6. Hardware-Facing Code

Treat hardware side effects as part of the correctness model.

When modifying peripheral code, consider:

- peripheral enable/disable ordering;
- interrupt enable/disable ordering;
- pending interrupt state;
- DMA ownership and completion;
- chip-select timing;
- final-byte/frame drain;
- stale FIFO/register state;
- peripheral reset behaviour;
- power-up and power-down ordering;
- error recovery;
- interaction with boot/reset/watchdog behaviour.

Do not assume a HAL call is atomic or that returning from it means the physical transaction has fully completed.

Avoid changing hardware configuration merely to make a software test easier.

## 7. Comments and Documentation

Documentation should explain contracts and non-obvious reasoning, not narrate obvious code.

Documentation should always aim to be as concise as possible while relaying all required information. 
Usage of visual aids such as mermaid diagrams in README or other .md files should be used where appropriate.

Use Doxygen-style documentation consistently for newly added or materially changed:

- public functions;
- externally visible hooks;
- non-trivial private functions;
- structs and important fields;
- active configuration macros;
- state-machine or protocol contracts.
- unit tests (just limited to explaining test does not need full return type and parameter documentation)

Inline comments are useful for:

- subtle ordering requirements;
- concurrency reasoning;
- hardware constraints;
- protocol invariants;
- why an apparently simpler implementation would be wrong.

Do not add comments that merely restate the next line of code.

Do not spend task scope on documentation-only clean-up unless documentation is part of the request or is required to prevent misuse of changed behaviour.

Do not invent new jargon that does not exist outside of the codebase unless it is referencing something that did not previously exist.
This also applies to names for functions, variables, files and anything else

## 8. Code Review Priorities

When reviewing embedded C/C++, prioritise concrete functional defects over stylistic commentary.

Review deeply for:

- race conditions;
- ISR-unsafety;
- incorrect critical-section boundaries;
- ownership/lifetime errors;
- use-after-reset or stale state;
- queue and buffer overflows;
- incorrect capacity calculations;
- partial state mutation on failure;
- sequence/retry accounting bugs;
- incorrect task/ISR API use;
- deadlocks and blocking behaviour;
- timing assumptions;
- watchdog interactions;
- DMA/peripheral completion ordering;
- reset/reconnect recovery;
- integer width/sign/overflow issues;
- packing/alignment/endianness;
- error-path correctness;
- mismatched assumptions between modules;
- invalid hardware-state transitions.

De-prioritise the following (even if they contradict this style guide):

- naming preferences;
- speculative architecture improvements;
- broad refactors;
- documentation polish;
- minor style differences;
- test-count criticism without a concrete coverage gap;
- suggestions that cannot be tied to a plausible failure.

### Review findings must be demonstrable

For each significant finding, explain:

1. The relevant code path.
2. The exact preconditions.
3. The sequence of events that triggers the problem.
4. The resulting incorrect behaviour.
5. Why existing protection does not prevent it.
6. A minimal direction for fixing it.
7. A focused regression test that would reproduce it.

Do not label something as a race, overflow, deadlock, or protocol violation without tracing how it can actually occur.
