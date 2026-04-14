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

/**-----------------------------------------------------------------------------
 *  Link seam: mocked functions definitions
 *------------------------------------------------------------------------------
 */
// NOLINTBEGIN

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
 *  Digital Output Test Cases
 *------------------------------------------------------------------------------
 */

/**
 * @test Processing a value should produce the correct output.
 */
TEST_F( split_about_ports, ProcessingProducesCorrectOutput )
{
    GPIO_OUTPUT_NAMES* my_arr = [ DIGITALOUT0, DIGITALOUT1, DIGITALOUT2 ];
    HW_GPIO_port_pin_association_to_return.gpiox = GPIOB;
    HW_GPIO_port_pin_association_to_return.pin_mask = 2;
    GPIO_PORT_PACKET destination[8];
    split_about_ports(my_arr, 3, destination);

    uint16_t result = split_about_ports( TEST_INPUT_VALUE );

    EXPECT_EQ( result, TEST_EXPECTED_OUTPUT );
}

TEST_F( combine_port_pin_masks, ProcessingProducesCorrectOutput )
{
    MODULE_Init();

    uint16_t result = combine_port_pin_masks( TEST_INPUT_VALUE );

    EXPECT_EQ( result, TEST_EXPECTED_OUTPUT );
}

TEST_F( HW_GPIO_port_pin_association, ProcessingProducesCorrectOutput )
{
    MODULE_Init();

    uint16_t result = MODULE_Process( TEST_INPUT_VALUE );

    EXPECT_EQ( result, TEST_EXPECTED_OUTPUT );
}
