/******************************************************************************
 *  File:       test_example.cpp
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

/*------------------------------------------------------------------------------
 *  Includes
 *----------------------------------------------------------------------------*/

#include <gtest/gtest.h>
#include <gmock/gmock.h>

extern "C"
{
    #include "example.h"    /* Module under test */
    #include "example_hal_gpio.h"   /* C EXAMPLE_HAL dependency to be mocked */
    #include <stdint.h>
    #include <stdbool.h>
}


/*------------------------------------------------------------------------------
 *  Test Constants / Macros
 *----------------------------------------------------------------------------*/

#define EXAMPLE_TEST_INPUT_VALUE        (5U)
#define EXAMPLE_TEST_EXPECTED_OUTPUT    (EXAMPLE_TEST_INPUT_VALUE * EXAMPLE_SCALE_FACTOR)

#define EXAMPLE_TEST_OUTPUT_PIN         (3U)
#define EXAMPLE_TEST_OUTPUT_LEVEL_HIGH  (true)
#define EXAMPLE_TEST_OUTPUT_LEVEL_LOW   (false)


/*------------------------------------------------------------------------------
 *  Test Doubles / Mocks
 *----------------------------------------------------------------------------*/

/**
 * @brief GoogleMock-based mock for the EXAMPLE_HAL GPIO interface.
 *
 * The real production declaration is:
 *     void EXAMPLE_HAL_GPIO_WritePin( uint32_t pin, bool level );
 *
 * We provide a C++ mock and route the C function symbol to it so that calls
 * from the C module are validated by GoogleMock.
 */
class MockHalGpio
{
public:
    MOCK_METHOD( void, EXAMPLE_HAL_GPIO_WritePin, ( uint32_t pin, bool level ) );
};

/* Global mock pointer used by the C-linkage shim below. */
static MockHalGpio* g_mock_example_hal_gpio = nullptr;

/**
 * @brief C-linkage shim that forwards calls to the C++ mock.
 *
 * This replaces the real EXAMPLE_HAL implementation in the test build. The linker
 * will use this definition instead of any production implementation.
 */
extern "C" void EXAMPLE_HAL_GPIO_WritePin( uint32_t pin, bool level )
{
    ASSERT_NE(g_mock_example_hal_gpio, nullptr)
        << "EXAMPLE_HAL_GPIO_WritePin called without an active mock instance";
    g_mock_example_hal_gpio->EXAMPLE_HAL_GPIO_WritePin(pin, level);
}


/*------------------------------------------------------------------------------
 *  Test Fixture
 *----------------------------------------------------------------------------*/

/**
 * @brief Test fixture for Example module tests.
 *
 * Provides a consistent setup/teardown environment for all test cases.
 */
class ExampleTest : public ::testing::Test
{
protected:
    void SetUp( void ) override
    {
        /* Reset module state before each test. */
        g_mock_example_hal_gpio = new MockHalGpio();
        EXAMPLE_Init();

        /* Clear any previous mock expectations. */
        ::testing::Mock::VerifyAndClearExpectations( g_mock_example_hal_gpio );
    }

    void TearDown( void ) override
    {
        /* Ensure no unexpected calls remain at the end of a test. */
        ::testing::Mock::VerifyAndClearExpectations(g_mock_example_hal_gpio);
        delete g_mock_example_hal_gpio;
        g_mock_example_hal_gpio = nullptr;
    }
};


/*------------------------------------------------------------------------------
 *  Test Cases
 *----------------------------------------------------------------------------*/

/**
 * @test EXAMPLE_Init should complete without interacting with the GPIO EXAMPLE_HAL.
 */
TEST_F( ExampleTest, InitDoesNotCallGpio )
{
    /* SetUp has already called EXAMPLE_Init().
     *
     * If EXAMPLE_Init() were to call EXAMPLE_HAL_GPIO_WritePin( ... ),
     * GoogleMock would report an unexpected call because we have not
     * set any EXPECT_CALL for it in this test.
     */
    SUCCEED();
}

/**
 * @test EXAMPLE_Process scales the input value by EXAMPLE_SCALE_FACTOR.
 */
TEST_F( ExampleTest, ProcessScalesInputByConstant )
{
    uint16_t result = EXAMPLE_Process( EXAMPLE_TEST_INPUT_VALUE );

    EXPECT_EQ( EXAMPLE_TEST_EXPECTED_OUTPUT, result );
}

/**
 * @test EXAMPLE_Test behaves consistently with EXAMPLE_Process.
 */
TEST_F( ExampleTest, TestWrapperMatchesProcessBehaviour )
{
    uint16_t process_result = EXAMPLE_Process( EXAMPLE_TEST_INPUT_VALUE );
    uint16_t test_result    = EXAMPLE_Test( EXAMPLE_TEST_INPUT_VALUE );

    EXPECT_EQ( process_result, test_result );
}

/**
 * @test EXAMPLE_SetOutput drives the GPIO EXAMPLE_HAL with the correct arguments.
 */
TEST_F( ExampleTest, SetOutputCallsHalWithCorrectArguments )
{
    EXPECT_CALL( *g_mock_example_hal_gpio,
                 EXAMPLE_HAL_GPIO_WritePin( EXAMPLE_TEST_OUTPUT_PIN, EXAMPLE_TEST_OUTPUT_LEVEL_HIGH ) )
        .Times( 1 );

    EXAMPLE_SetOutput( EXAMPLE_TEST_OUTPUT_PIN, EXAMPLE_TEST_OUTPUT_LEVEL_HIGH );
}

/**
 * @test Multiple calls to EXAMPLE_SetOutput result in multiple EXAMPLE_HAL calls.
 */
TEST_F( ExampleTest, MultipleSetOutputCallsProduceMultipleHalWrites )
{
    {
        ::testing::InSequence sequence;

        EXPECT_CALL( *g_mock_example_hal_gpio,
                     EXAMPLE_HAL_GPIO_WritePin( EXAMPLE_TEST_OUTPUT_PIN, EXAMPLE_TEST_OUTPUT_LEVEL_HIGH ) );
        EXPECT_CALL( *g_mock_example_hal_gpio,
                     EXAMPLE_HAL_GPIO_WritePin( EXAMPLE_TEST_OUTPUT_PIN, EXAMPLE_TEST_OUTPUT_LEVEL_LOW ) );
    }

    EXAMPLE_SetOutput( EXAMPLE_TEST_OUTPUT_PIN, EXAMPLE_TEST_OUTPUT_LEVEL_HIGH );
    EXAMPLE_SetOutput( EXAMPLE_TEST_OUTPUT_PIN, EXAMPLE_TEST_OUTPUT_LEVEL_LOW );
}