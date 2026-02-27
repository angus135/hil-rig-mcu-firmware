/******************************************************************************
 *  File:       example.h
 *  Author:     Angus Corr
 *  Created:    6-Dec-2025
 *
 *  Description:
 *      Public interface for the Example module.
 *
 *      This module demonstrates the coding style, naming conventions and
 *      testing approach used in this project. It provides a simple processing
 *      function that scales an input value, and a wrapper around a GPIO HAL
 *      function for driving an output pin.
 *
 *  Notes:
 *      - EXAMPLE_Init( void ) must be called before EXAMPLE_Process( ... )
 *        or EXAMPLE_Test( ... ) are used.
 *      - The EXAMPLE_SetOutput( ... ) function depends on an underlying
 *        HAL implementation of HAL_GPIO_WritePin( ... ). In production this
 *        will be provided by the platform HAL; in unit tests it is mocked.
 ******************************************************************************/

#ifndef EXAMPLE_H
#define EXAMPLE_H

#ifdef __cplusplus
extern "C"
{
#endif

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include <stdint.h>
#include <stdbool.h>

/**-----------------------------------------------------------------------------
 *  Public Defines / Macros
 *------------------------------------------------------------------------------
 */

/**
 * @brief Scale factor used by EXAMPLE_Process and EXAMPLE_Test.
 *
 * This is exposed so that other modules or tests can make assertions that
 * remain consistent if the scaling behaviour is changed.
 */
#define EXAMPLE_SCALE_FACTOR ( 10U )

/**-----------------------------------------------------------------------------
 *  Public Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

/**
 * @brief Example status structure.
 *
 * This is a simple example of a public type that follows the StructName_T
 * convention and could be exposed if external modules needed state access.
 * It is not strictly required by the current API but demonstrates style.
 */
typedef struct
{
    uint16_t last_value;
    bool     ready;
} ExampleStatus_T;

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

/**
 * @brief Initialises the Example module and internal state.
 *
 * Call this before any other public API functions for this module.
 */
void EXAMPLE_Init( void );

/**
 * @brief Processes an input value and returns the scaled result.
 *
 * @param input  Input value to be processed.
 * @return       Processed (scaled) output value.
 */
uint16_t EXAMPLE_Process( uint16_t input );

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
uint16_t EXAMPLE_Test( uint16_t test_value );

/**
 * @brief Drives a digital output using the underlying GPIO HAL.
 *
 * @param pin    Logical pin identifier to drive.
 * @param level  Output level (true = high, false = low).
 *
 * This function wraps the HAL_GPIO_WritePin( ... ) function provided by the
 * HAL layer. It is a convenient seam for unit testing where the HAL call is
 * mocked using GoogleMock.
 */
void EXAMPLE_SetOutput( uint32_t pin, bool level );

#ifdef __cplusplus
}
#endif

#endif /* EXAMPLE_H */
