/******************************************************************************
 *  File:       test_console.cpp
 *  Author:     Angus Corr
 *  Created:    06-Dec-2025
 *
 *  Description:
 *      Unit tests for the Example module using GoogleTest and GoogleMock.
 *
 *      These tests verify:
 *        - Initialisation resets internal state.
 *        - Processing scales input values correctly.
 *        - EXAMPLE_SetOutput( ... ) calls the GPIO EXAMPLE_HAL with the expected
 *          arguments, using GoogleMock to intercept the EXAMPLE_HAL call.
 *
 *  Notes:
 *      - Production code is written in C; tests are written in C++.
 *      - C headers are included inside an extern "C" block.
 *      - GoogleMock is used to mock EXAMPLE_HAL_GPIO_WritePin( ... ).
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>

extern "C"
{
#include "console.h" /* Module under test */
#include <stdint.h>
#include <stdbool.h>
}

/**-----------------------------------------------------------------------------
 *  Test Constants / Macros
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Test Doubles / Mocks
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Test Fixture
 *------------------------------------------------------------------------------
 */

/**
 * @brief Test fixture for module tests.
 *
 * Provides a consistent setup/teardown environment for all test cases.
 */
class ExampleTest : public ::testing::Test
{
protected:
    void SetUp(void) override
    {
    }

    void TearDown(void) override
    {
    }
};

/**-----------------------------------------------------------------------------
 *  Test Cases
 *------------------------------------------------------------------------------
 */
