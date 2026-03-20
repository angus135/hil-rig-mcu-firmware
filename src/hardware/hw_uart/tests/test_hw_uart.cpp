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

        uart_port_tx_dma_status[UART_CONSOLE] = false;

        s_uart_rx_head[UART_CONSOLE] = 0U;
        s_uart_rx_tail[UART_CONSOLE] = 0U;
        s_uart_rx_dma_byte[UART_CONSOLE] = 0U;
        s_uart_rx_active[UART_CONSOLE] = false;
        s_uart_rx_overflow[UART_CONSOLE] = false;
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
TEST_F( UartTest, StartRxService_CallsRxDmaAndReturnsSuccess )
{
    EXPECT_CALL( mock, RxDma( Eq( &huart3 ), testing::_, 1U ) ).WillOnce( Return( HAL_OK ) );

    const UARTStatus_T status = HW_UART_Start_Rx_Service( UART_CONSOLE );
    EXPECT_EQ( status, UART_SUCCESS );
}

TEST_F( UartTest, StartRxService_MapsHalBusyToUartBusy )
{
    EXPECT_CALL( mock, RxDma( Eq( &huart3 ), testing::_, 1U ) ).WillOnce( Return( HAL_BUSY ) );

    const UARTStatus_T status = HW_UART_Start_Rx_Service( UART_CONSOLE );
    EXPECT_EQ( status, UART_BUSY );
}

TEST_F( UartTest, RxCallback_StoresByteInFifo_AndTryReadByteReturnsIt )
{
    uint8_t byte = 0U;

    EXPECT_CALL( mock, RxDma( Eq( &huart3 ), testing::_, 1U ) )
        .WillOnce( Return( HAL_OK ) )   // initial HW_UART_Start_Rx_Service call
        .WillOnce( Return( HAL_OK ) );  // rearm inside HAL_UART_RxCpltCallback

    const UARTStatus_T start_status = HW_UART_Start_Rx_Service( UART_CONSOLE );
    EXPECT_EQ( start_status, UART_SUCCESS );

    s_uart_rx_dma_byte[UART_CONSOLE] = 0x5AU;
    HAL_UART_RxCpltCallback( &huart3 );

    const bool result = HW_UART_Try_Read_Byte( UART_CONSOLE, &byte );
    EXPECT_TRUE( result );
    EXPECT_EQ( byte, 0x5AU );
}
TEST_F( UartTest, TryReadByte_ReturnsFalseAfterFifoIsDrained )
{
    uint8_t byte = 0U;

    EXPECT_CALL( mock, RxDma( Eq( &huart3 ), testing::_, 1U ) )
        .WillOnce( Return( HAL_OK ) )
        .WillOnce( Return( HAL_OK ) );

    EXPECT_EQ( HW_UART_Start_Rx_Service( UART_CONSOLE ), UART_SUCCESS );

    s_uart_rx_dma_byte[UART_CONSOLE] = 0x33U;
    HAL_UART_RxCpltCallback( &huart3 );

    EXPECT_TRUE( HW_UART_Try_Read_Byte( UART_CONSOLE, &byte ) );
    EXPECT_EQ( byte, 0x33U );

    EXPECT_FALSE( HW_UART_Try_Read_Byte( UART_CONSOLE, &byte ) );
}

TEST_F( UartTest, TryReadByte_ReturnsFalseWhenFifoEmpty )
{
    uint8_t byte = 0U;

    const bool result = HW_UART_Try_Read_Byte( UART_CONSOLE, &byte );
    EXPECT_FALSE( result );
}

TEST_F( UartTest, WriteByte_CallsTxDmaAndReturnsSuccess_WhenTxCompleteCallbackFires )
{
    // Expect 1-byte DMA transmit on the console handle
    EXPECT_CALL( mock, TxDma( Eq( &huart3 ), testing::_, 1U ) ).WillOnce( Return( HAL_OK ) );

    // The driver loops calling vTaskDelay(pdMS_TO_TICKS(1)) until the status is set.
    // Simulate completion by calling the callback during the first delay.
    EXPECT_CALL( mock, TaskDelay( pdMS_TO_TICKS( 1 ) ) ).WillOnce( Invoke( []( TickType_t ) {
        HAL_UART_TxCpltCallback( &huart3 );
    } ) );

    const UARTStatus_T status = HW_UART_Write_Byte( UART_CONSOLE, 0xA5U );
    EXPECT_EQ( status, UART_SUCCESS );
}



TEST_F( UartTest, WriteByte_MapsHalBusyToUartBusy_AndDoesNotDelay )
{
    EXPECT_CALL( mock, TxDma( Eq( &huart3 ), testing::_, 1U ) ).WillOnce( Return( HAL_BUSY ) );

    // Should return immediately on error status
    EXPECT_CALL( mock, TaskDelay( testing::_ ) ).Times( 0 );

    const UARTStatus_T status = HW_UART_Write_Byte( UART_CONSOLE, 0x11U );
    EXPECT_EQ( status, UART_BUSY );
}


