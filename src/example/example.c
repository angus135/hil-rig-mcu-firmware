/******************************************************************************
 *  File:       example.c
 *  Author:     Angus Corr
 *  Created:    6-Dec-2025
 *
 *  Description:
 *      Example module implementation.
 *
 *      This module provides:
 *        - A simple processing function that scales an input value.
 *        - An internal state structure demonstrating the StructName_T pattern.
 *        - A wrapper around a GPIO EXAMPLE_HAL function used as a mock seam in tests.
 *
 *  Notes:
 *      - This code is written in C and is intended to be unit tested from
 *        C++ using GoogleTest and GoogleMock.
 *      - All internal state is kept static to this translation unit; only the
 *        public API declared in example.h is visible externally.
 ******************************************************************************/

/*------------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include "example.h"
#include "example_hal_gpio.h"

#include <stdint.h>
#include <stdbool.h>

/*------------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */

/* Internal constants; public constants are in example.h if needed externally. */
#define EXAMPLE_INTERNAL_INIT_VALUE (0U)

/*------------------------------------------------------------------------------
 *  Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

/**
 * @brief Internal state for the Example module.
 *
 * This demonstrates the StructName_T naming convention for internal types.
 * It is not exposed in the header file.
 */
typedef struct
{
    uint16_t value;
    bool     ready;
} Example_T;

/*------------------------------------------------------------------------------
 *  Private (static) Variables
 *------------------------------------------------------------------------------
 */

/** @brief Local module state, not visible outside this file. */
static Example_T example_state = {EXAMPLE_INTERNAL_INIT_VALUE, false};

/*------------------------------------------------------------------------------
 *  Private (static) Function Prototypes
 *------------------------------------------------------------------------------
 */

static void     EXAMPLE_InitState(void);
static uint16_t EXAMPLE_DoProcess(uint16_t input);

/*------------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

/**
 * @brief Resets the internal module state to a known default.
 */
static void EXAMPLE_InitState(void)
{
    example_state.value = EXAMPLE_INTERNAL_INIT_VALUE;
    example_state.ready = false;
}

/**
 * @brief Internal processing implementation.
 *
 * Multiplies the input by EXAMPLE_SCALE_FACTOR and marks the module as ready.
 */
static uint16_t EXAMPLE_DoProcess(uint16_t input)
{
    example_state.value = (uint16_t)(input * EXAMPLE_SCALE_FACTOR);
    example_state.ready = true;
    return example_state.value;
}

/*------------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */

void EXAMPLE_Init(void)
{
    EXAMPLE_InitState();
}

uint16_t EXAMPLE_Process(uint16_t input)
{
    return EXAMPLE_DoProcess(input);
}

uint16_t EXAMPLE_Test(uint16_t test_value)
{
    /* Simple wrapper around the main processing function.
     * In a real system this could perform a known-good operation for a
     * self-test or built-in test.
     */
    return EXAMPLE_Process(test_value);
}

void EXAMPLE_SetOutput(uint32_t pin, bool level)
{
    /* In production, this calls the real EXAMPLE_HAL implementation. In unit tests,
     * EXAMPLE_HAL_GPIO_WritePin( ... ) is replaced by a GoogleMock-based function.
     */
    EXAMPLE_HAL_GPIO_WritePin(pin, level);
}
