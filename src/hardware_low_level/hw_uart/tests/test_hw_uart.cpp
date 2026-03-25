/******************************************************************************
 *  File:       test_uart.cpp
 *  Author:     Angus Corr
 *  Created:    16-Dec-2025
 *
 *  Description:
 *      Unit tests for hw_uart using a HAL/RTOS link seam.
 *
 *      hw_uart.c is compiled as-is (no TEST_BUILD). We mock the HAL entry points
 *      (HAL_UART_Transmit_DMA / HAL_UART_Receive_DMA) and the RTOS delay
 *      (vTaskDelay) by providing test definitions of those symbols. hw_uart.c
 *      will link against these test symbols instead of the real HAL/RTOS.
 *
 *  Notes:
 *
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>

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

extern "C" UART_HandleTypeDef huart3 = { USART3 };

/**-----------------------------------------------------------------------------
 *  Test Doubles / Mocks
 *------------------------------------------------------------------------------
 */

using ::testing::Eq;
using ::testing::Invoke;
using ::testing::Return;

class MockHalUart
{
public:
    MOCK_METHOD( HAL_StatusTypeDef, TxDma,
                 ( UART_HandleTypeDef * huart, uint8_t* data, uint16_t len ), () );
    MOCK_METHOD( HAL_StatusTypeDef, RxDma,
                 ( UART_HandleTypeDef * huart, uint8_t* data, uint16_t len ), () );
    MOCK_METHOD( void, TaskDelay, ( TickType_t ticks ), () );
};

static MockHalUart* g_mock = nullptr;

/**-----------------------------------------------------------------------------
 *  Link seam: mocked functions definitions
 *------------------------------------------------------------------------------
 */
// NOLINTBEGIN
extern "C" HAL_StatusTypeDef HAL_UART_Transmit_DMA( UART_HandleTypeDef* huart, uint8_t* pData,
                                                    uint16_t Size )
{
    if ( !g_mock )
        return HAL_ERROR;
    return g_mock->TxDma( huart, pData, Size );
}

extern "C" HAL_StatusTypeDef HAL_UART_Receive_DMA( UART_HandleTypeDef* huart, uint8_t* pData,
                                                   uint16_t Size )
{
    if ( !g_mock )
        return HAL_ERROR;
    return g_mock->RxDma( huart, pData, Size );
}

extern "C" void vTaskDelay( const TickType_t xTicksToDelay )
{
    if ( !g_mock )
        return;
    g_mock->TaskDelay( xTicksToDelay );
}
// NOLINTEND
/**-----------------------------------------------------------------------------
 *  Test Fixture
 *------------------------------------------------------------------------------
 */

class UartTest : public ::testing::Test
{
protected:
    MockHalUart mock;

    void SetUp() override
    {
        g_mock = &mock;
    }
    void TearDown() override
    {
        g_mock = nullptr;
    }
};

/**-----------------------------------------------------------------------------
 *  Test Cases
 *------------------------------------------------------------------------------
 */

TEST_F( UartTest, WriteByte_CallsTxDmaAndReturnsSuccess_WhenTxCompleteCallbackFires )
{
    // Expect 1-byte DMA transmit on the console handle
    EXPECT_CALL( mock, TxDma( Eq( &huart3 ), testing::_, 1 ) ).WillOnce( Return( HAL_OK ) );

    // The driver loops calling vTaskDelay(pdMS_TO_TICKS(1)) until the status is set.
    // Simulate completion by calling the callback during the first delay.
    EXPECT_CALL( mock, TaskDelay( pdMS_TO_TICKS( 1 ) ) ).WillOnce( Invoke( []( TickType_t ) {
        HAL_UART_TxCpltCallback( &huart3 );
    } ) );

    const UARTStatus_T status = HW_UART_Write_Byte( UART_CONSOLE, 0xA5U );
    EXPECT_EQ( status, UART_SUCCESS );
}

TEST_F( UartTest, ReadByte_CallsRxDmaAndReturnsSuccess_WhenRxCompleteCallbackFires )
{
    uint8_t byte = 0U;

    EXPECT_CALL( mock, RxDma( Eq( &huart3 ), testing::_, 1 ) ).WillOnce( Return( HAL_OK ) );

    EXPECT_CALL( mock, TaskDelay( pdMS_TO_TICKS( 1 ) ) ).WillOnce( Invoke( [&]( TickType_t ) {
        byte = 0x5AU;
        HAL_UART_RxCpltCallback( &huart3 );
    } ) );

    const UARTStatus_T status = HW_UART_Read_Byte( UART_CONSOLE, &byte );
    EXPECT_EQ( status, UART_SUCCESS );
    EXPECT_EQ( byte, 0x5AU );
}

TEST_F( UartTest, WriteByte_MapsHalBusyToUartBusy_AndDoesNotDelay )
{
    EXPECT_CALL( mock, TxDma( Eq( &huart3 ), testing::_, 1 ) ).WillOnce( Return( HAL_BUSY ) );

    // Should return immediately on error status
    EXPECT_CALL( mock, TaskDelay( testing::_ ) ).Times( 0 );

    const UARTStatus_T status = HW_UART_Write_Byte( UART_CONSOLE, 0x11U );
    EXPECT_EQ( status, UART_BUSY );
}

TEST_F( UartTest, ReadByte_MapsHalTimeoutToUartTimeout_AndDoesNotDelay )
{
    uint8_t byte = 0U;

    EXPECT_CALL( mock, RxDma( Eq( &huart3 ), testing::_, 1 ) ).WillOnce( Return( HAL_TIMEOUT ) );

    EXPECT_CALL( mock, TaskDelay( testing::_ ) ).Times( 0 );

    const UARTStatus_T status = HW_UART_Read_Byte( UART_CONSOLE, &byte );
    EXPECT_EQ( status, UART_TIMEOUT );
}
