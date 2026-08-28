# Test Configuration

## Current implementation

`test_configuration` owns the module-private, active DUT-driver configuration
for one test. `DutDriverConfiguration_T` is the system source of truth for the
in-memory configuration consumed by the DUT Driver Lifecycle module. Each
execution-driver header remains the source of truth for its leaf configuration
type, channel enumeration, and validation rules.

The aggregate contains every DUT-facing channel, including disabled channels.
Applying a complete aggregate prevents configuration from a previous test from
remaining active accidentally. It is an internal runtime representation, not a
packed host protocol or persistent wire format.

Application startup calls `TEST_CONFIGURATION_Init()`, which creates the module
mutex and publishes a valid all-zero, all-disabled configuration. This supports
inert RSM bring-up before package decoding exists. Current `test_config ...`
console commands replace that active value for hardware diagnostics.

`TEST_CONFIGURATION_Commit()` copies a complete candidate into module-owned
storage. `TEST_CONFIGURATION_GetActive()` returns a caller-owned snapshot.
`TEST_CONFIGURATION_Clear()` invalidates and zeroes the stored value when the
RSM discards a completed test. The mutex makes these task-context copies atomic;
it does not provide lifecycle policy or validate individual driver settings.

## Ownership and lifecycle

| Responsibility | Owner |
|---|---|
| Host package schema and versioning | Host Interface |
| Package parsing and semantic validation | Host Interface plus relevant driver validators |
| Active internal aggregate storage | Test Configuration |
| Deciding when configuration may be applied | Run State Manager |
| Applying every driver/channel configuration | DUT Driver Lifecycle |
| Per-tick instruction execution | Execution Manager |

The intended production sequence is:

1. The Host Interface enters RSM `TEST_PACKAGE_RECEIVE`.
2. It receives and validates the complete versioned package.
3. It uploads the canonical instruction stream through Flash Manager.
4. It translates the package configuration into a fully populated
   `DutDriverConfiguration_T` and calls `TEST_CONFIGURATION_Commit()`.
5. It requests RSM configuration.
6. The RSM snapshots the active aggregate and passes it to
   `DUT_DRIVER_LIFECYCLE_Configure()`.
7. The configuration remains immutable through execution and any repeat. A new
   package may replace it only after the lifecycle has returned to package
   reception; discard explicitly clears it.

The current API relies on callers to obey that lifecycle. The Host Interface
may commit during `TEST_PACKAGE_RECEIVE`, before requesting configuration, and
must not commit a replacement while the RSM is configuring, armed, executing,
finalising, results-ready, or transferring results. If cross-module enforcement
becomes necessary, it should be added as an explicit configuration transaction
or RSM authorization mechanism rather than inferred from transport state.

## Current hardware limitation

Requested I2C values may exist in the active aggregate, but the DUT Driver
Lifecycle deliberately applies an all-zero disabled I2C configuration because
of the known I2C hardware fault. Remove that policy only after the hardware and
I2C driver path have been validated.

## Public API

The public structure and task-context API are declared in
`test_configuration.h`.
