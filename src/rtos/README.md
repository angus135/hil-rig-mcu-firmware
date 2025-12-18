# rtos
## Overview

The `rtos` module provides a thin abstraction layer over FreeRTOS that makes it possible to:

- Build the firmware against the STM32CubeIDE–generated FreeRTOS sources in production builds.

- Build and unit test the same application code in a host or test configuration (e.g. PC, CI) without linking the real FreeRTOS kernel, by using lightweight C stubs.

In a normal (non-test) build, `rtos_config` simply pulls in the STM32Cube FreeRTOS headers and exposes a small helper API (for example `CREATE_TASK`).
In a test build (guarded by `TEST_BUILD`), the same header instead provides:

- Minimal typedefs that mirror the FreeRTOS types actually used in the project (`TaskFunction_t`, `BaseType_t`, `TickType_t`, etc.).

- Function prototypes for the FreeRTOS APIs that application code calls (for example `xTaskCreate`, `vTaskStartScheduler`, `xTaskGetTickCount`).

- C source files (such as `stub_task.c`) that implement deterministic, test-friendly stub behaviour for those APIs.

This design lets application modules compile and link unchanged in both environments, while tests can control and observe RTOS-related behaviour without requiring a real scheduler, tick interrupt, or hardware.

## Responsibilities

The `rtos` module is responsible for:

- Providing a single, consistent include point (`#include "rtos_config.h"`) for any module that uses FreeRTOS functionality.

- Hiding the difference between:

    - Real FreeRTOS types and functions (production / CubeIDE firmware build).

    - Stubbed RTOS types and functions (unit-test / host build).

- Supplying a small helper API that keeps FreeRTOS usage patterns consistent, for example `CREATE_TASK` for task creation.

- Maintaining a growing set of stub implementations that mirror only the FreeRTOS APIs actually used in this project, such as:

    - Task management: `xTaskCreate`, `vTaskStartScheduler`, `vTaskDelayUntil`, `xTaskGetTickCount`.

    - Queues, semaphores, task notifications, etc. as they are introduced into the codebase.

It explicitly does not:

- Provide a complete replacement for the entire FreeRTOS API surface.

- Emulate full RTOS scheduling behaviour, priority rules, or timing guarantees.

- Perform any hardware initialisation, clock configuration, or interrupt setup.

- Own any application-level logic (no business rules, state machines, or hardware drivers).

Instead, its purpose is to act as a controlled seam between FreeRTOS usage in the firmware and the unit-test harness.

## Directory Structure

The intended directory structure for the module is:
```
rtos/
├─ rtos_config # Public RTOS abstraction header
├─ stub_task.c # Stubs for task-related APIs (test build only)
├─ stub_queue.c # Queue API stubs (xQueueCreate, xQueueSend, xQueueReceive, etc.)
├─ stub_semphr.c # Semaphore / mutex API stubs (xSemaphoreTake, xSemaphoreGive, etc.)
├─ stub_notify.c # Task notification stubs (xTaskNotify, xTaskNotifyWait, etc.)
```

As the project adopts more FreeRTOS features, new `stub_*.c` files should be added to model the minimal behaviour required by the tests.

## Files

File: `rtos_config`
Role:

- Public API surface for any module that needs FreeRTOS facilities.

- In production builds (no `TEST_BUILD`), this header:

    - Includes the STM32CubeIDE-generated FreeRTOS headers:

        - `FreeRTOS.h`, `task.h`, `semphr.h`, `timers.h`, `queue.h`, `stream_buffer.h`.

    - Provides a small helper functions:

- In test builds (with `TEST_BUILD` defined), this header:

    - Defines simplified versions of the core FreeRTOS types used in the codebase, such as:

        - `TickType_t`, `TaskFunction_t`, `BaseType_t`, etc.

    - Declares prototypes for the RTOS functions that will be stubbed:

        - `xTaskCreate`, `vTaskDelayUntil`, etc.

- Ensures C and C++ compatibility via `extern "C"` guards.

File: `stub_*.c`
Role (test build only; compiled when `TEST_BUILD` is defined):

- Implements a minimal, deterministic model of the related FreeRTOS APIs declared in `rtos_config`:

- Does not create or schedule real tasks; the behaviour is intentionally simple and deterministic.

- Gives tests a way to reason about timing logic (for example, modules that use `vTaskDelayUntil` or compare tick counts) without relying on hardware timers or interrupt-driven ticks.

## Public API and Usage

The key consumption pattern for this module is:

 - All application modules that use FreeRTOS must include:

```c
#include "rtos_config.h"
```

 - Application code must not include the CubeIDE-generated FreeRTOS headers directly (for example `#include "FreeRTOS.h"`, `#include "task.h"`).
Instead, all such headers are pulled in via `rtos_config` in production builds, and replaced by the stubbed types and prototypes in test builds.

Typical usage example for task creation:

```c
#include "rtos_config.h"

static TaskHandle_t ConsoleTaskHandle = NULL;

void CONSOLE_Task(void *pvParameters);

void APP_MAIN_Application(void)
{
CREATE_TASK(CONSOLE_Task,
"Console Task",
CONSOLE_TASK_MEMORY,
CONSOLE_TASK_PRIORITY,
&ConsoleTaskHandle);

vTaskStartScheduler();


}
```

In a production build:

- `TaskHandle_t`, `vTaskStartScheduler` etc. are the real FreeRTOS implementations from the STM32CubeIDE project.

In a test build:

- `vTaskStartScheduler`, `vTaskDelayUntil`, and `xTaskGetTickCount` use deterministic, single-threaded stub behaviour suitable for unit tests.

## Behaviour and Design Notes

- The `rtos` module is intentionally minimal. It only introduces stubs for the subset of FreeRTOS used by the application code.
When a new FreeRTOS function is used in any module:

    - Its prototype should be added to the `TEST_BUILD` section of `rtos_config`.

    - A corresponding stub implementation should be added to an appropriate `stub_*.c` file.

- The stub implementations are expected to be:

    - Deterministic (no randomness, no hidden global state beyond what tests can reset or control).

    - Single-threaded and synchronous from the test’s perspective.

    - Sufficient to support the logic under test, without re-implementing the full RTOS.

- The abstraction is designed so that:

    - Application modules do not need to be aware of whether they are linking against the real FreeRTOS or the stubbed RTOS.

    - Unit tests remain fast and predictable, without requiring a hardware board or simulator.

## Integration Notes

- The build system (for example CMake) must define `TEST_BUILD` for unit-test targets so that:

    - The stubbed RTOS types and prototypes in `rtos_config` are exposed instead of the real FreeRTOS headers.

    - The `stub_*.c` implementation files are compiled and linked into the test executable.

- For production firmware builds:

    - `TEST_BUILD` should not be defined.

    - The STM32CubeIDE project should provide the real FreeRTOS sources and configuration (heap, port, etc.), and only those should be linked.

- Any module that currently includes FreeRTOS headers directly should be migrated to include `rtos_config` only, to ensure the abstraction boundary is respected.