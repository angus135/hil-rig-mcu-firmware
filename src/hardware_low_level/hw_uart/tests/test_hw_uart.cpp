/******************************************************************************
 *  File:       test_hw_uart.cpp
 *  Author:     Angus Corr
 *  Created:    16-Dec-2025
 *
 *  Description:
 *      Minimal unit test harness for hw_uart.
 *
 *      hw_uart.c is compiled directly into this test translation unit. The HAL
 *      entry points required by the current implementation are provided here as
 *      simple link seams so that the test target can build in the host
 *      environment.
 *
 *  Notes:
 *      This is a temporary placeholder while the hw_uart test suite is being
 *      rewritten for the DMA span-based RX API.
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include <gtest/gtest.h>

extern "C"
{
#include <stdint.h>
#include <stdbool.h>
#include "hw_uart.c" /* Module under test */  // NOLINT
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
 *  Link seam: mocked functions definitions
 *------------------------------------------------------------------------------
 */
// NOLINTBEGIN
extern "C" HAL_StatusTypeDef HAL_UART_Init( UART_HandleTypeDef* huart )
{
    ( void )huart;
    return HAL_OK;
}

extern "C" HAL_StatusTypeDef HAL_UART_Receive_DMA( UART_HandleTypeDef* huart, uint8_t* pData, uint16_t Size )
{
    ( void )huart;
    ( void )pData;
    ( void )Size;
    return HAL_OK;
}

extern "C" HAL_StatusTypeDef HAL_UART_DMAStop( UART_HandleTypeDef* huart )
{
    ( void )huart;
    return HAL_OK;
}
// NOLINTEND

/**-----------------------------------------------------------------------------
 *  Test Fixture
 *------------------------------------------------------------------------------
 */

class UartTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
    }
    void TearDown() override
    {
    }
};

/**-----------------------------------------------------------------------------
 *  Test Cases
 *------------------------------------------------------------------------------
 */

TEST_F( UartTest, Placeholder )
{
    SUCCEED();
}