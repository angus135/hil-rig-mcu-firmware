/******************************************************************************
 *  File:       test_hw_gpio.cpp
 *  Author:     Callum Rafferty, Tim Vogelsang
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
#include "../hw_gpio.h"
#include <stdint.h>
#include <stdbool.h>

// Add any other C headers required by the module

#include "../hw_gpio.c" /* Module under test */  // NOLINT
}

// Add additional C++ includes here if required

/**-----------------------------------------------------------------------------
 *  Test Constants / Macros
 *------------------------------------------------------------------------------
 */

// Test-specific constants (does NOT affect production code)

// Add additional test-scoped constants and macros here

/**-----------------------------------------------------------------------------
 *  Test Doubles / Mocks
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Link seam: mocked functions definitions
 *------------------------------------------------------------------------------
 */
// NOLINTBEGIN

// NOLINTEND

/**-----------------------------------------------------------------------------
 *  Test Fixture
 *------------------------------------------------------------------------------
 */

/**
 * @brief Test fixture for HW_GPIO tests.
 *
 * Provides a consistent setup/teardown environment for all test cases.
 */
class HWGPIOTest : public ::testing::Test
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
