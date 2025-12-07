# File Structure

## Module Directory Sub-Structure
```
example_module/                     # Module directory
├── CMakeLists.txt                  # Builds the module + its tests
├── README.md                       # Description, usage, design notes
├── example_module.c                # Public API implementation
├── example_module.h                # Public API header
├── example_module_hw.c             # HW-specific implementation
├── example_module_hw.h
├── example_module_other.c          # Other required functionality
├── example_module_other.h
└── tests/                          # Tests for this module
    ├── test_example_module.c
    ├── test_example_module_hw.c
    └── test_example_module_other.c
```

## .c File Layout

```c
/******************************************************************************
 *  File:       <filename>.c
 *  Author:     <your name>
 *  Created:    <DD-MMM-YYYY>
 *
 *  Description:
 *      <Short description of the module's purpose and responsibilities>
 *
 *  Notes:
 *      <Any design notes, dependencies, or assumptions go here>
 ******************************************************************************/

/*------------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include "<filename>.h"
#include <stdint.h>
#include <stdbool.h>
// Add other required includes here


/*------------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */

#define EXAMPLE_CONSTANT        (10U)
// Add compile-time configuration options, flags, masks, and constants here


/*------------------------------------------------------------------------------
 *  Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

typedef struct {
    uint16_t value;
    bool     ready;
} Example_T;
// Add data structures, enum types, and typedefs here


/*------------------------------------------------------------------------------
 *  Private (static) Variables
 *------------------------------------------------------------------------------
 */

static Example_T local_state;
// Add static module variables here (hidden from other translation units)


/*------------------------------------------------------------------------------
 *  Private (static) Function Prototypes
 *------------------------------------------------------------------------------
 */

static void MODULE_Example_Init(void);
static uint16_t MODULE_Example_Process(uint16_t input);


/*------------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

static void MODULE_Example_Init(void)
{
    local_state.value = 0U;
    local_state.ready = false;
}

static uint16_t MODULE_Example_Process(uint16_t input)
{
    local_state.value = input * EXAMPLE_CONSTANT;
    local_state.ready = true;
    return local_state.value;
}

/*------------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */

/**
 * @brief Initialises the module state.
 */
void MODULE_Init(void)
{
    MODULE_Example_Init();
}

/**
 * @brief Main processing function for this module.
 *
 * @param input  Input value to be processed.
 * @return       Processed output value.
 */
uint16_t MODULE_Process(uint16_t input)
{
    return MODULE_Example_Process(input);
}

```

## .h File Layout

```c
/******************************************************************************
 *  File:       <filename>.h
 *  Author:     <your name>
 *  Created:    <DD-MMM-YYYY>
 *
 *  Description:
 *      <Short description of the module, what it exposes, and how it should be used>
 *
 *  Notes:
 *      <Public assumptions, required initialisation order, dependencies, etc.>
 ******************************************************************************/

#ifndef <FILENAME>_H
#define <FILENAME>_H

#ifdef __cplusplus
extern "C" {
#endif

/*------------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include <stdint.h>
#include <stdbool.h>
// Add any needed standard or project-specific includes here


/*------------------------------------------------------------------------------
 *  Public Defines / Macros
 *------------------------------------------------------------------------------
 */

// #define MODULE_FEATURE_FLAG   (1U)
// Add macros intended for use outside this module here


/*------------------------------------------------------------------------------
 *  Public Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

// typedef enum { STATE_IDLE, STATE_BUSY } Module_State_T;
// typedef struct { uint16_t value; bool ready; } Module_Data_T;
// Add types that must be visible to other modules here


/*------------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

/**
 * @brief Initialises the module and internal state.
 *
 * Call this before any other public API functions for this module.
 */
void MODULE_Init(void);

/**
 * @brief Processes an input value and returns the result.
 *
 * @param input  Input value to be processed.
 * @return       Processed output value.
 */
uint16_t MODULE_Process(uint16_t input);

/**
 * @brief Simple test entry point for verifying basic functionality.
 *
 * @param test_value  Test input parameter.
 * @return            Result produced by the module for verification.
 *
 * @test
 * This function is intended for unit testing or debugging. IDEs will show the
 * brief, parameters and return information automatically when hovering.
 */
uint16_t MODULE_Test(uint16_t test_value);

#ifdef __cplusplus
}
#endif

#endif /* <FILENAME>_H */


```

## test_xxx.cpp File Layout

```cpp
/******************************************************************************
 *  File:       test_<module>.cpp
 *  Author:     <your name>
 *  Created:    <DD-MMM-YYYY>
 *
 *  Description:
 *      Unit tests for the <module> module using GoogleTest and GoogleMock.
 *      This file validates the public API and behaviour defined in <module>.h.
 *
 *  Notes:
 *      - Production code is written in C; tests are written in C++.
 *      - C headers must be included inside an extern "C" block.
 *      - GoogleMock may be used to mock external dependencies.
 ******************************************************************************/

/*------------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>

extern "C"
{
    #include "<module>.h"     // C module under test
    #include <stdint.h>
    #include <stdbool.h>
    // Add any other C headers required by the module
}

// Add additional C++ includes here if required


/*------------------------------------------------------------------------------
 *  Test Constants / Macros
 *------------------------------------------------------------------------------
 */

// Test-specific constants (does NOT affect production code)
#define TEST_INPUT_VALUE      (5U)
#define TEST_EXPECTED_OUTPUT  (50U)

// Add additional test-scoped constants and macros here

/*------------------------------------------------------------------------------
 *  Test Doubles / Mocks
 *------------------------------------------------------------------------------
 */

/*
 * If the module calls external interfaces (HAL, drivers, other modules),
 * mock them here using GoogleMock.
 *
 */ Example:

extern "C" {
    void HAL_GPIO_WritePin(uint32_t pin, bool level);
}

class MockHalGpio {
public:
    MOCK_METHOD(void, HAL_GPIO_WritePin, (uint32_t pin, bool level));
};

// Provide a fake C binding that forwards to the mock instance:
MockHalGpio mock_gpio;
extern "C" void HAL_GPIO_WritePin(uint32_t pin, bool level) {
    mock_gpio.HAL_GPIO_WritePin(pin, level);
}

// Add mocks or stubs here as needed


/*------------------------------------------------------------------------------
 *  Test Fixture
 *------------------------------------------------------------------------------
 */

/**
 * @brief Test fixture for <module> tests.
 *
 * Provides a consistent setup/teardown environment for all test cases.
 */
class ModuleTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Runs before each test
        // Reset mocks, clear global state, etc.
    }

    void TearDown() override
    {
        // Runs after each test
        // Verify mock expectations if needed
    }
};


/*------------------------------------------------------------------------------
 *  Test Cases
 *------------------------------------------------------------------------------
 */

/**
 * @test Module initialisation should set the module state to known defaults.
 */
TEST_F(ModuleTest, ModuleInitialisesCorrectly)
{
    MODULE_Init();

    // EXPECT_* and ASSERT_* macros from GoogleTest
    // Example assertions:
    EXPECT_EQ(MODULE_GetState(), 0U);
    EXPECT_FALSE(MODULE_IsReady());
}


/**
 * @test Processing a value should produce the correct output.
 */
TEST_F(ModuleTest, ProcessingProducesCorrectOutput)
{
    MODULE_Init();

    uint16_t result = MODULE_Process(TEST_INPUT_VALUE);

    EXPECT_EQ(result, TEST_EXPECTED_OUTPUT);
}


/**
 * @test Example demonstrating GoogleMock usage for dependencies.
 *
 */
TEST_F(ModuleTest, CallsExternalDependency)
{
    EXPECT_CALL(mock_gpio, HAL_GPIO_WritePin(/*pin=*/3U, /*level=*/true));

    MODULE_DoSomethingThatCallsHal();

    // No additional ASSERTs needed; GoogleMock handles verification.
}

```

## CMakeLists.txt File Layout
```cmake
# src/example_module/CMakeLists.txt

# -----------------------------
# Library sources / headers
# -----------------------------

set(EXAMPLE_SOURCES
    example_module.c
)

set(EXAMPLE_HEADERS
    example_module.h
    example_module_other.h
)

add_library(example_module STATIC
    ${EXAMPLE_MODULE_SOURCES}
    ${EXAMPLE_MODULE_HEADERS}
)

# Make this directory usable as an include path for other targets
target_include_directories(example_module
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(example_module
    PUBLIC
        project_warnings
        cubeide_hal
        rtos
        # include any other relevant modules
)

# -----------------------------
# Tests for this module (gtest/gmock)
# -----------------------------

option(EXAMPLE_MODULE_ENABLE_TESTS "Build tests for example module" ON)

if(EXAMPLE_MODULE_ENABLE_TESTS AND BUILD_TESTING)

    add_executable(example_module_tests
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_example_module.cpp
    )

    target_link_libraries(example_module_tests
        PRIVATE
            project_warnings
            example_module
            # include any other dependent libraries here
            gtest
            gtest_main
            gmock
    )

    target_include_directories(example_module_tests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )

    add_test(NAME example_module_tests COMMAND example_module_tests)

endif()


```

## Module README.md Layout
```
# example_module
## Overview

`example_module` provides a self-contained functional unit within the firmware.  
It exposes a simple public API that can be called by other modules or the main application.

This module is responsible for:

- <Short description of what the module does>
- <What part of the system it interacts with>
- <Any hardware, timing, or configuration assumptions>

The implementation is designed to be portable and avoids direct dependencies unless explicitly stated.

---

## Files

| File                      | Role |
|---------------------------|------|
| `example_module.c`        | Public API implementation |
| `example_module.h`        | Public API header |
| `example_module_other.c`  | Internal helper functions (not intended for external use) |
| `example_module_other.h`  | Header for helpers (optional export) |
| `example_module_hw.c`     | Hardware-specific implementation (if required) |
| `example_module_hw.h`     | Hardware integration header |
| `tests/test_example_module.c` | Unit tests for this module |

---

## Public API

The public API is declared in `example_module.h`. Typical usage:

```c
#include "example_module.h"

void app_init(void)
{
    EXAMPLE_MODULE_Init();
}

void app_run(void)
{
    uint16_t output = EXAMPLE_MODULE_Process(42U);
    // Handle output
}
```