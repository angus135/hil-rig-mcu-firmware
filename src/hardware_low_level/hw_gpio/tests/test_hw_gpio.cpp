/******************************************************************************
 *  File:       test_hw_gpio.cpp
 *  Author:     Callum Rafferty
 *  Created:    25-Mar-2026
 *
 *  Description:
 *      Unit tests for the hw_gpio module using GoogleTest and GoogleMock.
 *      This file validates the public API and behaviour defined in hw_gpio.h.
 *
 *  Notes:
 *      - Production code is written in C; tests are written in C++.
 *      - C headers must be included inside an extern "C" block.
 *      - GoogleMock may be used to mock external dependencies.
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>

extern "C"
{
#include "hw_gpio.h"
#include <stdint.h>
#include <stdbool.h>

// Add any other C headers required by the module

#include "hw_gpio.c" /* Module under test */  // NOLINT
}

// Add additional C++ includes here if required

/**-----------------------------------------------------------------------------
 *  Test Constants / Macros
 *------------------------------------------------------------------------------
 */

// Test-specific constants (does NOT affect production code)
#define TEST_INPUT_VALUE ( 5U )
#define TEST_EXPECTED_OUTPUT ( 50U )

// Add additional test-scoped constants and macros here

/**-----------------------------------------------------------------------------
 *  Test Doubles / Mocks
 *------------------------------------------------------------------------------
 */

/*
 * If the module calls external interfaces (HAL, drivers, other modules),
 * mock them here using GoogleMock.
 *
 */
Example :

    extern "C"
{
    void HAL_GPIO_WritePin( uint32_t pin, bool level );
}

class MockHalGpio
{
public:
    MOCK_METHOD( void, HAL_GPIO_WritePin, ( uint32_t pin, bool level ) );
};

/**-----------------------------------------------------------------------------
 *  Link seam: mocked functions definitions
 *------------------------------------------------------------------------------
 */
// NOLINTBEGIN

// Provide a fake C binding that forwards to the mock instance:
MockHalGpio     mock_gpio;
extern "C" void HAL_GPIO_WritePin( uint32_t pin, bool level )
{
    mock_gpio.HAL_GPIO_WritePin( pin, level );
}

// Add mocks or stubs here as needed

// NOLINTEND

/**-----------------------------------------------------------------------------
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

/**-----------------------------------------------------------------------------
 *  Test Cases
 *------------------------------------------------------------------------------
 */

/**
 * @test Module initialisation should set the module state to known defaults.
 */
TEST_F( ModuleTest, ModuleInitialisesCorrectly )
{
    MODULE_Init();

    // EXPECT_* and ASSERT_* macros from GoogleTest
    // Example assertions:
    EXPECT_EQ( MODULE_GetState(), 0U );
    EXPECT_FALSE( MODULE_IsReady() );
}

/**
 * @test Processing a value should produce the correct output.
 */
TEST_F( ModuleTest, ProcessingProducesCorrectOutput )
{
    MODULE_Init();

    uint16_t result = MODULE_Process( TEST_INPUT_VALUE );

    EXPECT_EQ( result, TEST_EXPECTED_OUTPUT );
}

/**
 * @test Example demonstrating GoogleMock usage for dependencies.
 *
 */
TEST_F( ModuleTest, CallsExternalDependency )
{
    EXPECT_CALL( mock_gpio, HAL_GPIO_WritePin( /*pin=*/3U, /*level=*/true ) );

    MODULE_DoSomethingThatCallsHal();

    // No additional ASSERTs needed; GoogleMock handles verification.
}
