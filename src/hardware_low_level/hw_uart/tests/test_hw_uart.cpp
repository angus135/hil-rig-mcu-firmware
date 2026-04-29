/******************************************************************************
 *  File:       test_hw_uart.cpp
 *  Author:     Callum Rafferty
 *  Created:    14-04-2026
 *
 *  Description:
 *      Unit test harness for the DUT-facing low-level UART driver and console
 *      UART driver.
 *
 *      The HAL, LL, CMSIS, DMA, and USART entry points required by the current
 *      implementations are provided here as simple link seams so that the test
 *      target can build in the host environment.
 *
 *  Notes:
 *      DUT UART TX hot path tests respect the valid call contract. Invalid
 *      channel, null payload, zero length, and unconfigured TX calls are not
 *      tested for the DUT TX load and trigger path.
 *
 *      Console UART tests include defensive checks because the console public
 *      API explicitly validates arguments and driver state.
 *
 *      This test target includes the implementation files directly. Do not also
 *      compile hw_uart_dut.c or hw_uart_console.c into the same test target.
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
#include <string.h>
#include "hw_uart_mocks.h"
#include "hw_uart_dut.h"
#include "hw_uart_console.h"
}

/**-----------------------------------------------------------------------------
 *  Test Constants / Macros
 *------------------------------------------------------------------------------
 */

#define TEST_HW_UART_RX_BUFFER_SIZE 4096U
#define TEST_HW_UART_CONSOLE_RX_CAPACITY 127U

/**-----------------------------------------------------------------------------
 *  Test Doubles / Mocks
 *------------------------------------------------------------------------------
 */

extern "C"
{
DMA_TypeDef fake_dma1 = { 0U, 0U, 0U, 0U };
DMA_TypeDef fake_dma2 = { 0U, 0U, 0U, 0U };
}

uint32_t mock_primask           = 0U;
uint32_t mock_irq_disable_count = 0U;
uint32_t mock_irq_enable_count  = 0U;

static UART_HandleTypeDef* mock_receive_dma_huart = nullptr;
static uint8_t*            mock_receive_dma_data  = nullptr;
static uint16_t            mock_receive_dma_size  = 0U;

static UART_HandleTypeDef* mock_receive_it_huart = nullptr;
static uint8_t*            mock_receive_it_data  = nullptr;
static uint16_t            mock_receive_it_size  = 0U;
static uint32_t            mock_receive_it_count = 0U;

static UART_HandleTypeDef* mock_transmit_huart   = nullptr;
static uint8_t*            mock_transmit_data    = nullptr;
static uint16_t            mock_transmit_size    = 0U;
static uint32_t            mock_transmit_timeout = 0U;
static uint32_t            mock_transmit_count   = 0U;

static UART_HandleTypeDef* mock_irq_handler_huart = nullptr;
static uint32_t            mock_irq_handler_count = 0U;

static HAL_StatusTypeDef mock_hal_uart_init_result   = HAL_OK;
static HAL_StatusTypeDef mock_hal_receive_dma_result = HAL_OK;
static HAL_StatusTypeDef mock_hal_dma_stop_result    = HAL_OK;
static int               mock_hal_receive_it_result  = HAL_OK;
static int               mock_hal_transmit_result    = HAL_OK;

/**-----------------------------------------------------------------------------
 *  Private Helper Functions
 *------------------------------------------------------------------------------
 */

static HwUartConfig_T TEST_HW_UART_Make_Tx_Rx_Config()
{
    HwUartConfig_T config = {};

    config.interface_mode = HW_UART_MODE_TTL_3V3;
    config.baud_rate      = 115200U;
    config.word_length    = HW_UART_WORD_LENGTH_8_BITS;
    config.stop_bits      = HW_UART_STOP_BITS_1;
    config.parity         = HW_UART_PARITY_NONE;
    config.rx_enabled     = true;
    config.tx_enabled     = true;

    return config;
}

static HwUartConfig_T TEST_HW_UART_Make_Tx_Only_Config()
{
    HwUartConfig_T config = TEST_HW_UART_Make_Tx_Rx_Config();

    config.rx_enabled = false;
    config.tx_enabled = true;

    return config;
}

static HwUartConfig_T TEST_HW_UART_Make_Disabled_Config()
{
    HwUartConfig_T config = {};

    config.interface_mode = HW_UART_MODE_DISABLED;
    config.baud_rate      = 0U;
    config.word_length    = HW_UART_WORD_LENGTH_8_BITS;
    config.stop_bits      = HW_UART_STOP_BITS_1;
    config.parity         = HW_UART_PARITY_NONE;
    config.rx_enabled     = false;
    config.tx_enabled     = false;

    return config;
}

static DMA_Stream_TypeDef* TEST_HW_UART_Get_Tx_Stream( DMA_TypeDef* dma, uint32_t stream )
{
    if ( stream != LL_DMA_STREAM_6 )
    {
        return nullptr;
    }

    if ( dma == DMA1 )
    {
        return DMA1_Stream6;
    }

    if ( dma == DMA2 )
    {
        return DMA2_Stream6;
    }

    return nullptr;
}

static void TEST_HW_UART_Reset_Usart( USART_TypeDef* usart )
{
    usart->SR   = 0U;
    usart->DR   = 0U;
    usart->BRR  = 0U;
    usart->CR1  = 0U;
    usart->CR2  = 0U;
    usart->CR3  = 0U;
    usart->GTPR = 0U;
}

static void TEST_HW_UART_Reset_Dma_Stream( DMA_Stream_TypeDef* stream )
{
    stream->CR   = 0U;
    stream->NDTR = 0U;
    stream->PAR  = 0U;
    stream->M0AR = 0U;
    stream->FCR  = 0U;
}

static void TEST_HW_UART_Reset_Dma_Handle( DMA_HandleTypeDef* handle, DMA_Stream_TypeDef* stream )
{
    handle->Instance                = stream;
    handle->disabled_interrupt_mask = 0U;
}

/**-----------------------------------------------------------------------------
 *  Link seam: mocked functions definitions
 *------------------------------------------------------------------------------
 */
// NOLINTBEGIN

extern "C" uint32_t __get_PRIMASK( void )
{
    return mock_primask;
}

extern "C" void __disable_irq( void )
{
    mock_primask = 1U;
    mock_irq_disable_count++;
}

extern "C" void __enable_irq( void )
{
    mock_primask = 0U;
    mock_irq_enable_count++;
}

extern "C" HAL_StatusTypeDef HAL_UART_Init( UART_HandleTypeDef* huart )
{
    ( void )huart;

    return mock_hal_uart_init_result;
}

extern "C" HAL_StatusTypeDef HAL_UART_Receive_DMA( UART_HandleTypeDef* huart, uint8_t* pData,
                                                   uint16_t Size )
{
    mock_receive_dma_huart = huart;
    mock_receive_dma_data  = pData;
    mock_receive_dma_size  = Size;

    return mock_hal_receive_dma_result;
}

extern "C" HAL_StatusTypeDef HAL_UART_DMAStop( UART_HandleTypeDef* huart )
{
    ( void )huart;

    return mock_hal_dma_stop_result;
}

extern "C" int HAL_UART_Receive_IT( UART_HandleTypeDef* huart, uint8_t* data, uint16_t size )
{
    mock_receive_it_huart = huart;
    mock_receive_it_data  = data;
    mock_receive_it_size  = size;
    mock_receive_it_count++;

    return mock_hal_receive_it_result;
}

extern "C" int HAL_UART_Transmit( UART_HandleTypeDef* huart, uint8_t* data, uint16_t size,
                                  uint32_t timeout )
{
    mock_transmit_huart   = huart;
    mock_transmit_data    = data;
    mock_transmit_size    = size;
    mock_transmit_timeout = timeout;
    mock_transmit_count++;

    return mock_hal_transmit_result;
}

extern "C" void HAL_UART_IRQHandler( UART_HandleTypeDef* huart )
{
    mock_irq_handler_huart = huart;
    mock_irq_handler_count++;
}

extern "C" void LL_DMA_DisableStream( DMA_TypeDef* dma, uint32_t stream )
{
    DMA_Stream_TypeDef* dma_stream = TEST_HW_UART_Get_Tx_Stream( dma, stream );

    if ( dma_stream != nullptr )
    {
        CLEAR_BIT( dma_stream->CR, DMA_SxCR_EN );
    }
}

extern "C" void LL_DMA_EnableStream( DMA_TypeDef* dma, uint32_t stream )
{
    DMA_Stream_TypeDef* dma_stream = TEST_HW_UART_Get_Tx_Stream( dma, stream );

    if ( dma_stream != nullptr )
    {
        SET_BIT( dma_stream->CR, DMA_SxCR_EN );
    }
}

extern "C" uint32_t LL_DMA_IsEnabledStream( DMA_TypeDef* dma, uint32_t stream )
{
    DMA_Stream_TypeDef* dma_stream = TEST_HW_UART_Get_Tx_Stream( dma, stream );

    if ( dma_stream == nullptr )
    {
        return 0U;
    }

    return ( ( dma_stream->CR & DMA_SxCR_EN ) != 0U ) ? 1U : 0U;
}

extern "C" void LL_DMA_SetMemoryAddress( DMA_TypeDef* dma, uint32_t stream, uint32_t address )
{
    DMA_Stream_TypeDef* dma_stream = TEST_HW_UART_Get_Tx_Stream( dma, stream );

    if ( dma_stream != nullptr )
    {
        dma_stream->M0AR = address;
    }
}

extern "C" void LL_DMA_SetPeriphAddress( DMA_TypeDef* dma, uint32_t stream, uint32_t address )
{
    DMA_Stream_TypeDef* dma_stream = TEST_HW_UART_Get_Tx_Stream( dma, stream );

    if ( dma_stream != nullptr )
    {
        dma_stream->PAR = address;
    }
}

extern "C" void LL_DMA_SetDataLength( DMA_TypeDef* dma, uint32_t stream, uint32_t length )
{
    DMA_Stream_TypeDef* dma_stream = TEST_HW_UART_Get_Tx_Stream( dma, stream );

    if ( dma_stream != nullptr )
    {
        dma_stream->NDTR = length;
    }
}

extern "C" void LL_DMA_DisableIT_HT( DMA_TypeDef* dma, uint32_t stream )
{
    DMA_Stream_TypeDef* dma_stream = TEST_HW_UART_Get_Tx_Stream( dma, stream );

    if ( dma_stream != nullptr )
    {
        CLEAR_BIT( dma_stream->CR, DMA_SxCR_HTIE );
    }
}

extern "C" void LL_DMA_EnableIT_TC( DMA_TypeDef* dma, uint32_t stream )
{
    DMA_Stream_TypeDef* dma_stream = TEST_HW_UART_Get_Tx_Stream( dma, stream );

    if ( dma_stream != nullptr )
    {
        SET_BIT( dma_stream->CR, DMA_SxCR_TCIE );
    }
}

extern "C" void LL_DMA_EnableIT_TE( DMA_TypeDef* dma, uint32_t stream )
{
    DMA_Stream_TypeDef* dma_stream = TEST_HW_UART_Get_Tx_Stream( dma, stream );

    if ( dma_stream != nullptr )
    {
        SET_BIT( dma_stream->CR, DMA_SxCR_TEIE );
    }
}

extern "C" uint32_t LL_DMA_IsActiveFlag_TC6( DMA_TypeDef* dma )
{
    if ( dma == nullptr )
    {
        return 0U;
    }

    return ( ( dma->HISR & DMA_HISR_TCIF6 ) != 0U ) ? 1U : 0U;
}

extern "C" uint32_t LL_DMA_IsActiveFlag_TE6( DMA_TypeDef* dma )
{
    if ( dma == nullptr )
    {
        return 0U;
    }

    return ( ( dma->HISR & DMA_HISR_TEIF6 ) != 0U ) ? 1U : 0U;
}

extern "C" void LL_USART_EnableDMAReq_TX( USART_TypeDef* usart )
{
    if ( usart != nullptr )
    {
        SET_BIT( usart->CR3, USART_CR3_DMAT );
    }
}

extern "C" void LL_USART_DisableDMAReq_TX( USART_TypeDef* usart )
{
    if ( usart != nullptr )
    {
        CLEAR_BIT( usart->CR3, USART_CR3_DMAT );
    }
}

extern "C" uint32_t LL_DMA_IsActiveFlag_TC2( DMA_TypeDef* dma )
{
    if ( dma == nullptr )
    {
        return 0U;
    }

    return ( ( dma->LISR & DMA_LISR_TCIF2 ) != 0U ) ? 1U : 0U;
}

extern "C" uint32_t LL_DMA_IsActiveFlag_TE2( DMA_TypeDef* dma )
{
    if ( dma == nullptr )
    {
        return 0U;
    }

    return ( ( dma->LISR & DMA_LISR_TEIF2 ) != 0U ) ? 1U : 0U;
}

extern "C" uint32_t LL_DMA_IsActiveFlag_DME2( DMA_TypeDef* dma )
{
    if ( dma == nullptr )
    {
        return 0U;
    }

    return ( ( dma->LISR & DMA_LISR_DMEIF2 ) != 0U ) ? 1U : 0U;
}

extern "C" uint32_t LL_DMA_IsActiveFlag_FE2( DMA_TypeDef* dma )
{
    if ( dma == nullptr )
    {
        return 0U;
    }

    return ( ( dma->LISR & DMA_LISR_FEIF2 ) != 0U ) ? 1U : 0U;
}

extern "C" uint32_t LL_DMA_IsActiveFlag_HT2( DMA_TypeDef* dma )
{
    if ( dma == nullptr )
    {
        return 0U;
    }

    return ( ( dma->LISR & DMA_LISR_HTIF2 ) != 0U ) ? 1U : 0U;
}

extern "C" uint32_t LL_DMA_IsActiveFlag_TC5( DMA_TypeDef* dma )
{
    if ( dma == nullptr )
    {
        return 0U;
    }

    return ( ( dma->HISR & DMA_HISR_TCIF5 ) != 0U ) ? 1U : 0U;
}

extern "C" uint32_t LL_DMA_IsActiveFlag_TE5( DMA_TypeDef* dma )
{
    if ( dma == nullptr )
    {
        return 0U;
    }

    return ( ( dma->HISR & DMA_HISR_TEIF5 ) != 0U ) ? 1U : 0U;
}

extern "C" uint32_t LL_DMA_IsActiveFlag_DME5( DMA_TypeDef* dma )
{
    if ( dma == nullptr )
    {
        return 0U;
    }

    return ( ( dma->HISR & DMA_HISR_DMEIF5 ) != 0U ) ? 1U : 0U;
}

extern "C" uint32_t LL_DMA_IsActiveFlag_FE5( DMA_TypeDef* dma )
{
    if ( dma == nullptr )
    {
        return 0U;
    }

    return ( ( dma->HISR & DMA_HISR_FEIF5 ) != 0U ) ? 1U : 0U;
}

extern "C" uint32_t LL_DMA_IsActiveFlag_HT5( DMA_TypeDef* dma )
{
    if ( dma == nullptr )
    {
        return 0U;
    }

    return ( ( dma->HISR & DMA_HISR_HTIF5 ) != 0U ) ? 1U : 0U;
}

// NOLINTEND

/**-----------------------------------------------------------------------------
 *  Implementation Under Test
 *------------------------------------------------------------------------------
 */

extern "C"
{
void HAL_UART_RxCpltCallback( UART_HandleTypeDef* huart );
void HAL_UART_ErrorCallback( UART_HandleTypeDef* huart );
void USART3_IRQHandler( void );

#include "hw_uart_dut.c"
#include "hw_uart_console.c"
}

/**-----------------------------------------------------------------------------
 *  Test Fixture
 *------------------------------------------------------------------------------
 */

class UartTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        fake_dma1.LISR  = 0U;
        fake_dma1.HISR  = 0U;
        fake_dma1.LIFCR = 0U;
        fake_dma1.HIFCR = 0U;

        fake_dma2.LISR  = 0U;
        fake_dma2.HISR  = 0U;
        fake_dma2.LIFCR = 0U;
        fake_dma2.HIFCR = 0U;

        TEST_HW_UART_Reset_Dma_Handle( &hdma_usart6_rx, DMA2_Stream2 );
        TEST_HW_UART_Reset_Dma_Handle( &hdma_usart2_rx, DMA1_Stream5 );
        TEST_HW_UART_Reset_Dma_Handle( &hdma_usart3_rx, nullptr );

        huart6.Instance        = USART6;
        huart6.Init.BaudRate   = 0U;
        huart6.Init.WordLength = 0U;
        huart6.Init.StopBits   = 0U;
        huart6.Init.Parity     = 0U;
        huart6.Init.Mode       = 0U;
        huart6.hdmarx          = &hdma_usart6_rx;
        huart6.hdmatx          = nullptr;

        huart2.Instance        = USART2;
        huart2.Init.BaudRate   = 0U;
        huart2.Init.WordLength = 0U;
        huart2.Init.StopBits   = 0U;
        huart2.Init.Parity     = 0U;
        huart2.Init.Mode       = 0U;
        huart2.hdmarx          = &hdma_usart2_rx;
        huart2.hdmatx          = nullptr;

        huart3.Instance        = USART3;
        huart3.Init.BaudRate   = 0U;
        huart3.Init.WordLength = 0U;
        huart3.Init.StopBits   = 0U;
        huart3.Init.Parity     = 0U;
        huart3.Init.Mode       = 0U;
        huart3.hdmarx          = &hdma_usart3_rx;
        huart3.hdmatx          = nullptr;

        TEST_HW_UART_Reset_Usart( USART6 );
        TEST_HW_UART_Reset_Usart( USART2 );
        TEST_HW_UART_Reset_Usart( USART3 );

        TEST_HW_UART_Reset_Dma_Stream( DMA2_Stream2 );
        TEST_HW_UART_Reset_Dma_Stream( DMA2_Stream6 );
        TEST_HW_UART_Reset_Dma_Stream( DMA1_Stream5 );
        TEST_HW_UART_Reset_Dma_Stream( DMA1_Stream6 );

        mock_primask           = 0U;
        mock_irq_disable_count = 0U;
        mock_irq_enable_count  = 0U;

        mock_receive_dma_huart = nullptr;
        mock_receive_dma_data  = nullptr;
        mock_receive_dma_size  = 0U;

        mock_receive_it_huart = nullptr;
        mock_receive_it_data  = nullptr;
        mock_receive_it_size  = 0U;
        mock_receive_it_count = 0U;

        mock_transmit_huart   = nullptr;
        mock_transmit_data    = nullptr;
        mock_transmit_size    = 0U;
        mock_transmit_timeout = 0U;
        mock_transmit_count   = 0U;

        mock_irq_handler_huart = nullptr;
        mock_irq_handler_count = 0U;

        mock_hal_uart_init_result   = HAL_OK;
        mock_hal_receive_dma_result = HAL_OK;
        mock_hal_dma_stop_result    = HAL_OK;
        mock_hal_receive_it_result  = HAL_OK;
        mock_hal_transmit_result    = HAL_OK;

        memset( hw_uart_channel_states, 0, sizeof( hw_uart_channel_states ) );
        memset( &uart_console_state, 0, sizeof( uart_console_state ) );
        memset( uart_console_rx_buffer, 0, sizeof( uart_console_rx_buffer ) );

        uart_console_rx_byte = 0U;
        s_rx_overflow_count  = 0U;
    }

    void TearDown() override
    {
    }
};

/**-----------------------------------------------------------------------------
 *  Test Cases
 *------------------------------------------------------------------------------
 */

TEST_F( UartTest, DutPrivateUnreadBytesCountHandlesNonWrappedIndices )
{
    EXPECT_EQ( HW_UART_Unread_Bytes_Count_Helper( 3U, 8U ), 5U );
}

TEST_F( UartTest, DutPrivateUnreadBytesCountHandlesWrappedIndices )
{
    EXPECT_EQ( HW_UART_Unread_Bytes_Count_Helper( TEST_HW_UART_RX_BUFFER_SIZE - 2U, 3U ), 5U );
}

TEST_F( UartTest, DutPrivateAdvanceIndexWrapsAtBufferBoundary )
{
    EXPECT_EQ( HW_UART_Advance_Index_Helper( TEST_HW_UART_RX_BUFFER_SIZE - 2U, 5U ), 3U );
}

TEST_F( UartTest, DutPrivateConfigurationAcceptsCanonicalDisabledConfig )
{
    HwUartConfig_T config = TEST_HW_UART_Make_Disabled_Config();

    EXPECT_TRUE( HW_UART_Configuration_Is_Valid( &config ) );
}

TEST_F( UartTest, DutPrivateConfigurationRejectsActiveModeWithRxAndTxDisabled )
{
    HwUartConfig_T config = TEST_HW_UART_Make_Tx_Rx_Config();

    config.rx_enabled = false;
    config.tx_enabled = false;

    EXPECT_FALSE( HW_UART_Configuration_Is_Valid( &config ) );
}

TEST_F( UartTest, DutPrivateConfigurationRejectsUnsupportedTtlBaudRate )
{
    HwUartConfig_T config = TEST_HW_UART_Make_Tx_Rx_Config();

    config.baud_rate = 2000001U;

    EXPECT_FALSE( HW_UART_Configuration_Is_Valid( &config ) );
}

TEST_F( UartTest, DutPrivateConfigurationRejectsUnsupportedRs232BaudRate )
{
    HwUartConfig_T config = TEST_HW_UART_Make_Tx_Rx_Config();

    config.interface_mode = HW_UART_MODE_RS232;
    config.baud_rate      = 1000001U;

    EXPECT_FALSE( HW_UART_Configuration_Is_Valid( &config ) );
}

TEST_F( UartTest, DutConfigureChannel1AppliesUartSettings )
{
    HwUartConfig_T config = TEST_HW_UART_Make_Tx_Rx_Config();

    ASSERT_TRUE( HW_UART_Configure_Channel( HW_UART_CHANNEL_1, &config ) );

    EXPECT_EQ( huart6.Init.BaudRate, 115200U );
    EXPECT_EQ( huart6.Init.WordLength, UART_WORDLENGTH_8B );
    EXPECT_EQ( huart6.Init.StopBits, UART_STOPBITS_1 );
    EXPECT_EQ( huart6.Init.Parity, UART_PARITY_NONE );
    EXPECT_EQ( huart6.Init.Mode, UART_MODE_TX_RX );
}

TEST_F( UartTest, DutConfigureChannel2AppliesUartSettings )
{
    HwUartConfig_T config = TEST_HW_UART_Make_Tx_Rx_Config();

    ASSERT_TRUE( HW_UART_Configure_Channel( HW_UART_CHANNEL_2, &config ) );

    EXPECT_EQ( huart2.Init.BaudRate, 115200U );
    EXPECT_EQ( huart2.Init.WordLength, UART_WORDLENGTH_8B );
    EXPECT_EQ( huart2.Init.StopBits, UART_STOPBITS_1 );
    EXPECT_EQ( huart2.Init.Parity, UART_PARITY_NONE );
    EXPECT_EQ( huart2.Init.Mode, UART_MODE_TX_RX );
}

TEST_F( UartTest, DutConfigureRejectsNullConfig )
{
    EXPECT_FALSE( HW_UART_Configure_Channel( HW_UART_CHANNEL_1, nullptr ) );
}

TEST_F( UartTest, DutConfigureRejectsInvalidBaudRate )
{
    HwUartConfig_T config = TEST_HW_UART_Make_Tx_Rx_Config();

    config.baud_rate = 0U;

    EXPECT_FALSE( HW_UART_Configure_Channel( HW_UART_CHANNEL_1, &config ) );
}

TEST_F( UartTest, DutConfigureAcceptsDisabledConfig )
{
    HwUartConfig_T config = TEST_HW_UART_Make_Disabled_Config();

    EXPECT_TRUE( HW_UART_Configure_Channel( HW_UART_CHANNEL_1, &config ) );
}

TEST_F( UartTest, DutConfigureReturnsFalseWhenHalInitFails )
{
    HwUartConfig_T config = TEST_HW_UART_Make_Tx_Rx_Config();

    mock_hal_uart_init_result = HAL_ERROR;

    EXPECT_FALSE( HW_UART_Configure_Channel( HW_UART_CHANNEL_1, &config ) );
}

TEST_F( UartTest, DutRxStartStartsDmaAndReportsRunning )
{
    HwUartConfig_T config = TEST_HW_UART_Make_Tx_Rx_Config();

    ASSERT_TRUE( HW_UART_Configure_Channel( HW_UART_CHANNEL_1, &config ) );
    ASSERT_TRUE( HW_UART_Rx_Start( HW_UART_CHANNEL_1 ) );

    EXPECT_TRUE( HW_UART_Rx_Is_Running( HW_UART_CHANNEL_1 ) );
    EXPECT_EQ( mock_receive_dma_huart, &huart6 );
    EXPECT_NE( mock_receive_dma_data, nullptr );
    EXPECT_EQ( mock_receive_dma_size, TEST_HW_UART_RX_BUFFER_SIZE );
}

TEST_F( UartTest, DutRxStartRejectsWhenRxDisabled )
{
    HwUartConfig_T config = TEST_HW_UART_Make_Tx_Only_Config();

    ASSERT_TRUE( HW_UART_Configure_Channel( HW_UART_CHANNEL_1, &config ) );

    EXPECT_FALSE( HW_UART_Rx_Start( HW_UART_CHANNEL_1 ) );
}

TEST_F( UartTest, DutRxStartRejectsSecondStart )
{
    HwUartConfig_T config = TEST_HW_UART_Make_Tx_Rx_Config();

    ASSERT_TRUE( HW_UART_Configure_Channel( HW_UART_CHANNEL_1, &config ) );
    ASSERT_TRUE( HW_UART_Rx_Start( HW_UART_CHANNEL_1 ) );

    EXPECT_FALSE( HW_UART_Rx_Start( HW_UART_CHANNEL_1 ) );
}

TEST_F( UartTest, DutRxStartReturnsFalseWhenHalReceiveDmaFails )
{
    HwUartConfig_T config = TEST_HW_UART_Make_Tx_Rx_Config();

    ASSERT_TRUE( HW_UART_Configure_Channel( HW_UART_CHANNEL_1, &config ) );

    mock_hal_receive_dma_result = HAL_ERROR;

    EXPECT_FALSE( HW_UART_Rx_Start( HW_UART_CHANNEL_1 ) );
    EXPECT_FALSE( HW_UART_Rx_Is_Running( HW_UART_CHANNEL_1 ) );
}

TEST_F( UartTest, DutRxStopStopsRunningRx )
{
    HwUartConfig_T config = TEST_HW_UART_Make_Tx_Rx_Config();

    ASSERT_TRUE( HW_UART_Configure_Channel( HW_UART_CHANNEL_1, &config ) );
    ASSERT_TRUE( HW_UART_Rx_Start( HW_UART_CHANNEL_1 ) );

    EXPECT_TRUE( HW_UART_Rx_Stop( HW_UART_CHANNEL_1 ) );
    EXPECT_FALSE( HW_UART_Rx_Is_Running( HW_UART_CHANNEL_1 ) );
}

TEST_F( UartTest, DutRxStopReturnsFalseWhenNotRunning )
{
    HwUartConfig_T config = TEST_HW_UART_Make_Tx_Rx_Config();

    ASSERT_TRUE( HW_UART_Configure_Channel( HW_UART_CHANNEL_1, &config ) );

    EXPECT_FALSE( HW_UART_Rx_Stop( HW_UART_CHANNEL_1 ) );
}

TEST_F( UartTest, DutRxStopReturnsFalseWhenHalDmaStopFails )
{
    HwUartConfig_T config = TEST_HW_UART_Make_Tx_Rx_Config();

    ASSERT_TRUE( HW_UART_Configure_Channel( HW_UART_CHANNEL_1, &config ) );
    ASSERT_TRUE( HW_UART_Rx_Start( HW_UART_CHANNEL_1 ) );

    mock_hal_dma_stop_result = HAL_ERROR;

    EXPECT_FALSE( HW_UART_Rx_Stop( HW_UART_CHANNEL_1 ) );
}

TEST_F( UartTest, DutRxPeekReturnsZeroWhenNoUnreadBytes )
{
    HwUartConfig_T config = TEST_HW_UART_Make_Tx_Rx_Config();

    ASSERT_TRUE( HW_UART_Configure_Channel( HW_UART_CHANNEL_1, &config ) );
    ASSERT_TRUE( HW_UART_Rx_Start( HW_UART_CHANNEL_1 ) );

    DMA2_Stream2->NDTR = TEST_HW_UART_RX_BUFFER_SIZE;

    HwUartRxSpans_T spans = HW_UART_Rx_Peek( HW_UART_CHANNEL_1 );

    EXPECT_EQ( spans.total_length_bytes, 0U );
    EXPECT_EQ( spans.first_span.length_bytes, 0U );
    EXPECT_EQ( spans.second_span.length_bytes, 0U );
}

TEST_F( UartTest, DutRxPeekReportsSingleContiguousSpan )
{
    HwUartConfig_T config = TEST_HW_UART_Make_Tx_Rx_Config();

    ASSERT_TRUE( HW_UART_Configure_Channel( HW_UART_CHANNEL_1, &config ) );
    ASSERT_TRUE( HW_UART_Rx_Start( HW_UART_CHANNEL_1 ) );

    DMA2_Stream2->NDTR = TEST_HW_UART_RX_BUFFER_SIZE - 5U;

    HwUartRxSpans_T spans = HW_UART_Rx_Peek( HW_UART_CHANNEL_1 );

    EXPECT_EQ( spans.total_length_bytes, 5U );
    EXPECT_EQ( spans.first_span.length_bytes, 5U );
    EXPECT_EQ( spans.second_span.length_bytes, 0U );
}

TEST_F( UartTest, DutRxPeekReportsWrappedSpan )
{
    HwUartConfig_T config = TEST_HW_UART_Make_Tx_Rx_Config();

    ASSERT_TRUE( HW_UART_Configure_Channel( HW_UART_CHANNEL_1, &config ) );
    ASSERT_TRUE( HW_UART_Rx_Start( HW_UART_CHANNEL_1 ) );

    HW_UART_Rx_Consume( HW_UART_CHANNEL_1, TEST_HW_UART_RX_BUFFER_SIZE - 3U );
    DMA2_Stream2->NDTR = TEST_HW_UART_RX_BUFFER_SIZE - 2U;

    HwUartRxSpans_T spans = HW_UART_Rx_Peek( HW_UART_CHANNEL_1 );

    EXPECT_EQ( spans.total_length_bytes, 5U );
    EXPECT_EQ( spans.first_span.length_bytes, 3U );
    EXPECT_EQ( spans.second_span.length_bytes, 2U );
}

TEST_F( UartTest, DutRxConsumeAdvancesReadIndex )
{
    HwUartConfig_T config = TEST_HW_UART_Make_Tx_Rx_Config();

    ASSERT_TRUE( HW_UART_Configure_Channel( HW_UART_CHANNEL_1, &config ) );
    ASSERT_TRUE( HW_UART_Rx_Start( HW_UART_CHANNEL_1 ) );

    DMA2_Stream2->NDTR = TEST_HW_UART_RX_BUFFER_SIZE - 8U;

    HwUartRxSpans_T initial_spans = HW_UART_Rx_Peek( HW_UART_CHANNEL_1 );
    ASSERT_EQ( initial_spans.total_length_bytes, 8U );

    HW_UART_Rx_Consume( HW_UART_CHANNEL_1, 3U );

    HwUartRxSpans_T later_spans = HW_UART_Rx_Peek( HW_UART_CHANNEL_1 );
    EXPECT_EQ( later_spans.total_length_bytes, 5U );
}

TEST_F( UartTest, DutTxLoadAcceptsPayloadWhenSpaceAvailable )
{
    HwUartConfig_T config     = TEST_HW_UART_Make_Tx_Only_Config();
    uint8_t        payload[4] = { 1U, 2U, 3U, 4U };

    ASSERT_TRUE( HW_UART_Configure_Channel( HW_UART_CHANNEL_1, &config ) );

    EXPECT_TRUE( HW_UART_Tx_Load_Buffer( HW_UART_CHANNEL_1, payload, sizeof( payload ) ) );
    EXPECT_TRUE( HW_UART_Is_Tx_Busy( HW_UART_CHANNEL_1 ) );
    EXPECT_EQ( mock_irq_disable_count, 2U );
    EXPECT_EQ( mock_irq_enable_count, 2U );
}

TEST_F( UartTest, DutTxLoadCopiesPayloadDirectlyIntoDmaSourceRingBuffer )
{
    HwUartConfig_T config     = TEST_HW_UART_Make_Tx_Only_Config();
    uint8_t        payload[4] = { 0x10U, 0x20U, 0x30U, 0x40U };

    ASSERT_TRUE( HW_UART_Configure_Channel( HW_UART_CHANNEL_1, &config ) );

    ASSERT_TRUE( HW_UART_Tx_Load_Buffer( HW_UART_CHANNEL_1, payload, sizeof( payload ) ) );

    EXPECT_EQ(
        memcmp( hw_uart_channel_states[HW_UART_CHANNEL_1].tx_buffer, payload, sizeof( payload ) ),
        0 );
    EXPECT_EQ( hw_uart_channel_states[HW_UART_CHANNEL_1].runtime.tx_head, sizeof( payload ) );
    EXPECT_EQ( hw_uart_channel_states[HW_UART_CHANNEL_1].runtime.tx_tail, 0U );
    EXPECT_EQ( hw_uart_channel_states[HW_UART_CHANNEL_1].runtime.tx_count, sizeof( payload ) );
    EXPECT_FALSE( hw_uart_channel_states[HW_UART_CHANNEL_1].runtime.tx_dma_active );
}

TEST_F( UartTest, DutTxLoadAcceptsSequentialPayloadsUntilFull )
{
    HwUartConfig_T config                                    = TEST_HW_UART_Make_Tx_Only_Config();
    uint8_t        half_payload[HW_UART_TX_BUFFER_SIZE / 2U] = {};

    ASSERT_TRUE( HW_UART_Configure_Channel( HW_UART_CHANNEL_1, &config ) );

    EXPECT_TRUE(
        HW_UART_Tx_Load_Buffer( HW_UART_CHANNEL_1, half_payload, sizeof( half_payload ) ) );

    EXPECT_TRUE(
        HW_UART_Tx_Load_Buffer( HW_UART_CHANNEL_1, half_payload, sizeof( half_payload ) ) );

    EXPECT_TRUE( HW_UART_Is_Tx_Busy( HW_UART_CHANNEL_1 ) );
    EXPECT_EQ( hw_uart_channel_states[HW_UART_CHANNEL_1].runtime.tx_count, HW_UART_TX_BUFFER_SIZE );
}

TEST_F( UartTest, DutTxLoadRejectsPayloadWhenInsufficientSpace )
{
    HwUartConfig_T config                               = TEST_HW_UART_Make_Tx_Only_Config();
    uint8_t        full_payload[HW_UART_TX_BUFFER_SIZE] = {};
    uint8_t        extra_payload[1]                     = { 0xAAU };

    ASSERT_TRUE( HW_UART_Configure_Channel( HW_UART_CHANNEL_1, &config ) );

    ASSERT_TRUE(
        HW_UART_Tx_Load_Buffer( HW_UART_CHANNEL_1, full_payload, HW_UART_TX_BUFFER_SIZE ) );

    EXPECT_FALSE(
        HW_UART_Tx_Load_Buffer( HW_UART_CHANNEL_1, extra_payload, sizeof( extra_payload ) ) );
}

TEST_F( UartTest, DutTxLoadPreservesExistingPrimaskState )
{
    HwUartConfig_T config     = TEST_HW_UART_Make_Tx_Only_Config();
    uint8_t        payload[2] = { 1U, 2U };

    ASSERT_TRUE( HW_UART_Configure_Channel( HW_UART_CHANNEL_1, &config ) );

    mock_primask = 1U;

    ASSERT_TRUE( HW_UART_Tx_Load_Buffer( HW_UART_CHANNEL_1, payload, sizeof( payload ) ) );

    EXPECT_EQ( mock_primask, 1U );
    EXPECT_EQ( mock_irq_disable_count, 2U );
    EXPECT_EQ( mock_irq_enable_count, 0U );
}

TEST_F( UartTest, DutTxTriggerDoesNothingWhenNoDataQueued )
{
    HwUartConfig_T config = TEST_HW_UART_Make_Tx_Only_Config();

    ASSERT_TRUE( HW_UART_Configure_Channel( HW_UART_CHANNEL_1, &config ) );

    EXPECT_TRUE( HW_UART_Tx_Trigger( HW_UART_CHANNEL_1 ) );
    EXPECT_EQ( DMA2_Stream6->CR & DMA_SxCR_EN, 0U );
    EXPECT_EQ( USART6->CR3 & USART_CR3_DMAT, 0U );
}

TEST_F( UartTest, DutTxTriggerStartsDmaForChannel1 )
{
    HwUartConfig_T config     = TEST_HW_UART_Make_Tx_Only_Config();
    uint8_t        payload[3] = { 0x11U, 0x22U, 0x33U };

    ASSERT_TRUE( HW_UART_Configure_Channel( HW_UART_CHANNEL_1, &config ) );
    ASSERT_TRUE( HW_UART_Tx_Load_Buffer( HW_UART_CHANNEL_1, payload, sizeof( payload ) ) );

    ASSERT_TRUE( HW_UART_Tx_Trigger( HW_UART_CHANNEL_1 ) );

    EXPECT_NE( DMA2_Stream6->CR & DMA_SxCR_EN, 0U );
    EXPECT_NE( DMA2_Stream6->CR & DMA_SxCR_TCIE, 0U );
    EXPECT_NE( DMA2_Stream6->CR & DMA_SxCR_TEIE, 0U );
    EXPECT_EQ( DMA2_Stream6->NDTR, sizeof( payload ) );
    EXPECT_NE( USART6->CR3 & USART_CR3_DMAT, 0U );
    EXPECT_EQ( DMA2_Stream6->PAR, ( uint32_t )( uintptr_t )( &( USART6->DR ) ) );
}

TEST_F( UartTest, DutTxTriggerStartsDmaFromTxRingBufferTail )
{
    HwUartConfig_T config     = TEST_HW_UART_Make_Tx_Only_Config();
    uint8_t        payload[3] = { 0x11U, 0x22U, 0x33U };

    ASSERT_TRUE( HW_UART_Configure_Channel( HW_UART_CHANNEL_1, &config ) );
    ASSERT_TRUE( HW_UART_Tx_Load_Buffer( HW_UART_CHANNEL_1, payload, sizeof( payload ) ) );

    ASSERT_TRUE( HW_UART_Tx_Trigger( HW_UART_CHANNEL_1 ) );

    EXPECT_EQ( DMA2_Stream6->M0AR,
               ( uint32_t )( uintptr_t )&hw_uart_channel_states[HW_UART_CHANNEL_1].tx_buffer[0] );
    EXPECT_EQ( DMA2_Stream6->NDTR, sizeof( payload ) );
    EXPECT_EQ( hw_uart_channel_states[HW_UART_CHANNEL_1].runtime.tx_tail, 0U );
    EXPECT_EQ( hw_uart_channel_states[HW_UART_CHANNEL_1].runtime.tx_count, sizeof( payload ) );
    EXPECT_TRUE( hw_uart_channel_states[HW_UART_CHANNEL_1].runtime.tx_dma_active );
}

TEST_F( UartTest, DutTxTriggerStartsDmaForChannel2 )
{
    HwUartConfig_T config     = TEST_HW_UART_Make_Tx_Only_Config();
    uint8_t        payload[2] = { 0xAAU, 0xBBU };

    ASSERT_TRUE( HW_UART_Configure_Channel( HW_UART_CHANNEL_2, &config ) );
    ASSERT_TRUE( HW_UART_Tx_Load_Buffer( HW_UART_CHANNEL_2, payload, sizeof( payload ) ) );

    ASSERT_TRUE( HW_UART_Tx_Trigger( HW_UART_CHANNEL_2 ) );

    EXPECT_NE( DMA1_Stream6->CR & DMA_SxCR_EN, 0U );
    EXPECT_NE( DMA1_Stream6->CR & DMA_SxCR_TCIE, 0U );
    EXPECT_NE( DMA1_Stream6->CR & DMA_SxCR_TEIE, 0U );
    EXPECT_EQ( DMA1_Stream6->NDTR, sizeof( payload ) );
    EXPECT_NE( USART2->CR3 & USART_CR3_DMAT, 0U );
    EXPECT_EQ( DMA1_Stream6->PAR, ( uint32_t )( uintptr_t )( &( USART2->DR ) ) );
    EXPECT_EQ( DMA1_Stream6->M0AR,
               ( uint32_t )( uintptr_t )&hw_uart_channel_states[HW_UART_CHANNEL_2].tx_buffer[0] );
}

TEST_F( UartTest, DutTxTriggerDoesNotConsumeBufferUntilDmaCompletes )
{
    HwUartConfig_T config     = TEST_HW_UART_Make_Tx_Only_Config();
    uint8_t        payload[3] = { 0x11U, 0x22U, 0x33U };

    ASSERT_TRUE( HW_UART_Configure_Channel( HW_UART_CHANNEL_1, &config ) );
    ASSERT_TRUE( HW_UART_Tx_Load_Buffer( HW_UART_CHANNEL_1, payload, sizeof( payload ) ) );

    ASSERT_TRUE( HW_UART_Tx_Trigger( HW_UART_CHANNEL_1 ) );

    EXPECT_EQ( hw_uart_channel_states[HW_UART_CHANNEL_1].runtime.tx_tail, 0U );
    EXPECT_EQ( hw_uart_channel_states[HW_UART_CHANNEL_1].runtime.tx_count, sizeof( payload ) );
    EXPECT_TRUE( hw_uart_channel_states[HW_UART_CHANNEL_1].runtime.tx_dma_active );
    EXPECT_TRUE( HW_UART_Is_Tx_Busy( HW_UART_CHANNEL_1 ) );

    fake_dma2.HISR |= DMA_HISR_TCIF6;
    DMA2_Stream6_IRQHandler();

    EXPECT_EQ( hw_uart_channel_states[HW_UART_CHANNEL_1].runtime.tx_tail, sizeof( payload ) );
    EXPECT_EQ( hw_uart_channel_states[HW_UART_CHANNEL_1].runtime.tx_count, 0U );
    EXPECT_FALSE( hw_uart_channel_states[HW_UART_CHANNEL_1].runtime.tx_dma_active );
    EXPECT_FALSE( HW_UART_Is_Tx_Busy( HW_UART_CHANNEL_1 ) );
}

TEST_F( UartTest, DutTxTriggerDoesNothingWhenAlreadyRunning )
{
    HwUartConfig_T config     = TEST_HW_UART_Make_Tx_Only_Config();
    uint8_t        payload[3] = { 0x11U, 0x22U, 0x33U };

    ASSERT_TRUE( HW_UART_Configure_Channel( HW_UART_CHANNEL_1, &config ) );
    ASSERT_TRUE( HW_UART_Tx_Load_Buffer( HW_UART_CHANNEL_1, payload, sizeof( payload ) ) );
    ASSERT_TRUE( HW_UART_Tx_Trigger( HW_UART_CHANNEL_1 ) );

    uint32_t first_ndtr = DMA2_Stream6->NDTR;
    uint32_t first_m0ar = DMA2_Stream6->M0AR;

    ASSERT_TRUE( HW_UART_Tx_Trigger( HW_UART_CHANNEL_1 ) );

    EXPECT_EQ( DMA2_Stream6->NDTR, first_ndtr );
    EXPECT_EQ( DMA2_Stream6->M0AR, first_m0ar );
}

TEST_F( UartTest, DutTxLoadWhileDmaActiveDoesNotModifyActiveDmaSpan )
{
    HwUartConfig_T config            = TEST_HW_UART_Make_Tx_Only_Config();
    uint8_t        first_payload[3]  = { 0x11U, 0x22U, 0x33U };
    uint8_t        second_payload[2] = { 0x44U, 0x55U };

    ASSERT_TRUE( HW_UART_Configure_Channel( HW_UART_CHANNEL_1, &config ) );

    ASSERT_TRUE(
        HW_UART_Tx_Load_Buffer( HW_UART_CHANNEL_1, first_payload, sizeof( first_payload ) ) );

    ASSERT_TRUE( HW_UART_Tx_Trigger( HW_UART_CHANNEL_1 ) );

    uint32_t active_m0ar = DMA2_Stream6->M0AR;
    uint32_t active_ndtr = DMA2_Stream6->NDTR;

    ASSERT_TRUE(
        HW_UART_Tx_Load_Buffer( HW_UART_CHANNEL_1, second_payload, sizeof( second_payload ) ) );

    EXPECT_EQ( DMA2_Stream6->M0AR, active_m0ar );
    EXPECT_EQ( DMA2_Stream6->NDTR, active_ndtr );
    EXPECT_EQ( hw_uart_channel_states[HW_UART_CHANNEL_1].runtime.tx_count,
               sizeof( first_payload ) + sizeof( second_payload ) );
}

TEST_F( UartTest, DutTxCompleteInterruptDrainsBusyState )
{
    HwUartConfig_T config     = TEST_HW_UART_Make_Tx_Only_Config();
    uint8_t        payload[3] = { 0x11U, 0x22U, 0x33U };

    ASSERT_TRUE( HW_UART_Configure_Channel( HW_UART_CHANNEL_1, &config ) );
    ASSERT_TRUE( HW_UART_Tx_Load_Buffer( HW_UART_CHANNEL_1, payload, sizeof( payload ) ) );
    ASSERT_TRUE( HW_UART_Tx_Trigger( HW_UART_CHANNEL_1 ) );

    ASSERT_TRUE( HW_UART_Is_Tx_Busy( HW_UART_CHANNEL_1 ) );

    fake_dma2.HISR |= DMA_HISR_TCIF6;
    DMA2_Stream6_IRQHandler();

    EXPECT_FALSE( HW_UART_Is_Tx_Busy( HW_UART_CHANNEL_1 ) );
    EXPECT_EQ( USART6->CR3 & USART_CR3_DMAT, 0U );
    EXPECT_EQ( fake_dma2.HIFCR, DMA_HIFCR_CTCIF6 | DMA_HIFCR_CTEIF6 | DMA_HIFCR_CFEIF6
                                    | DMA_HIFCR_CDMEIF6 | DMA_HIFCR_CHTIF6 );
}

TEST_F( UartTest, DutTxCompleteRestartsPumpWhenQueuedDataRemains )
{
    HwUartConfig_T config            = TEST_HW_UART_Make_Tx_Only_Config();
    uint8_t        first_payload[3]  = { 0x11U, 0x22U, 0x33U };
    uint8_t        second_payload[2] = { 0x44U, 0x55U };

    ASSERT_TRUE( HW_UART_Configure_Channel( HW_UART_CHANNEL_1, &config ) );

    ASSERT_TRUE(
        HW_UART_Tx_Load_Buffer( HW_UART_CHANNEL_1, first_payload, sizeof( first_payload ) ) );

    ASSERT_TRUE( HW_UART_Tx_Trigger( HW_UART_CHANNEL_1 ) );

    ASSERT_TRUE(
        HW_UART_Tx_Load_Buffer( HW_UART_CHANNEL_1, second_payload, sizeof( second_payload ) ) );

    fake_dma2.HISR |= DMA_HISR_TCIF6;
    DMA2_Stream6_IRQHandler();

    EXPECT_TRUE( HW_UART_Is_Tx_Busy( HW_UART_CHANNEL_1 ) );
    EXPECT_EQ( DMA2_Stream6->NDTR, sizeof( second_payload ) );
    EXPECT_NE( DMA2_Stream6->CR & DMA_SxCR_EN, 0U );
    EXPECT_EQ( DMA2_Stream6->M0AR,
               ( uint32_t )( uintptr_t )&hw_uart_channel_states[HW_UART_CHANNEL_1]
                   .tx_buffer[sizeof( first_payload )] );
}

TEST_F( UartTest, DutTxErrorInterruptClearsBusyState )
{
    HwUartConfig_T config     = TEST_HW_UART_Make_Tx_Only_Config();
    uint8_t        payload[3] = { 0x11U, 0x22U, 0x33U };

    ASSERT_TRUE( HW_UART_Configure_Channel( HW_UART_CHANNEL_1, &config ) );
    ASSERT_TRUE( HW_UART_Tx_Load_Buffer( HW_UART_CHANNEL_1, payload, sizeof( payload ) ) );
    ASSERT_TRUE( HW_UART_Tx_Trigger( HW_UART_CHANNEL_1 ) );

    ASSERT_TRUE( HW_UART_Is_Tx_Busy( HW_UART_CHANNEL_1 ) );

    fake_dma2.HISR |= DMA_HISR_TEIF6;
    DMA2_Stream6_IRQHandler();

    EXPECT_FALSE( HW_UART_Is_Tx_Busy( HW_UART_CHANNEL_1 ) );
    EXPECT_EQ( USART6->CR3 & USART_CR3_DMAT, 0U );
    EXPECT_EQ( hw_uart_channel_states[HW_UART_CHANNEL_1].runtime.tx_head, 0U );
    EXPECT_EQ( hw_uart_channel_states[HW_UART_CHANNEL_1].runtime.tx_tail, 0U );
    EXPECT_EQ( hw_uart_channel_states[HW_UART_CHANNEL_1].runtime.tx_count, 0U );
    EXPECT_FALSE( hw_uart_channel_states[HW_UART_CHANNEL_1].runtime.tx_dma_active );
}

TEST_F( UartTest, DutTxWrapLoadCopiesPayloadAcrossRingBoundary )
{
    HwUartConfig_T config = TEST_HW_UART_Make_Tx_Only_Config();

    uint8_t near_full_payload[HW_UART_TX_BUFFER_SIZE - 2U] = {};
    uint8_t wrap_payload[4]                                = { 1U, 2U, 3U, 4U };

    ASSERT_TRUE( HW_UART_Configure_Channel( HW_UART_CHANNEL_1, &config ) );

    ASSERT_TRUE( HW_UART_Tx_Load_Buffer( HW_UART_CHANNEL_1, near_full_payload,
                                         sizeof( near_full_payload ) ) );

    ASSERT_TRUE( HW_UART_Tx_Trigger( HW_UART_CHANNEL_1 ) );

    fake_dma2.HISR |= DMA_HISR_TCIF6;
    DMA2_Stream6_IRQHandler();
    fake_dma2.HISR = 0U;

    ASSERT_FALSE( HW_UART_Is_Tx_Busy( HW_UART_CHANNEL_1 ) );

    ASSERT_TRUE(
        HW_UART_Tx_Load_Buffer( HW_UART_CHANNEL_1, wrap_payload, sizeof( wrap_payload ) ) );

    EXPECT_EQ( hw_uart_channel_states[HW_UART_CHANNEL_1].tx_buffer[HW_UART_TX_BUFFER_SIZE - 2U],
               wrap_payload[0] );
    EXPECT_EQ( hw_uart_channel_states[HW_UART_CHANNEL_1].tx_buffer[HW_UART_TX_BUFFER_SIZE - 1U],
               wrap_payload[1] );
    EXPECT_EQ( hw_uart_channel_states[HW_UART_CHANNEL_1].tx_buffer[0U], wrap_payload[2] );
    EXPECT_EQ( hw_uart_channel_states[HW_UART_CHANNEL_1].tx_buffer[1U], wrap_payload[3] );
}

TEST_F( UartTest, DutTxWrapIsSentAsTwoLinearDmaTransfers )
{
    HwUartConfig_T config = TEST_HW_UART_Make_Tx_Only_Config();

    uint8_t near_full_payload[HW_UART_TX_BUFFER_SIZE - 2U] = {};
    uint8_t wrap_payload[4]                                = { 1U, 2U, 3U, 4U };

    ASSERT_TRUE( HW_UART_Configure_Channel( HW_UART_CHANNEL_1, &config ) );

    ASSERT_TRUE( HW_UART_Tx_Load_Buffer( HW_UART_CHANNEL_1, near_full_payload,
                                         sizeof( near_full_payload ) ) );

    ASSERT_TRUE( HW_UART_Tx_Trigger( HW_UART_CHANNEL_1 ) );

    fake_dma2.HISR |= DMA_HISR_TCIF6;
    DMA2_Stream6_IRQHandler();
    fake_dma2.HISR = 0U;

    ASSERT_FALSE( HW_UART_Is_Tx_Busy( HW_UART_CHANNEL_1 ) );

    ASSERT_TRUE(
        HW_UART_Tx_Load_Buffer( HW_UART_CHANNEL_1, wrap_payload, sizeof( wrap_payload ) ) );

    ASSERT_TRUE( HW_UART_Tx_Trigger( HW_UART_CHANNEL_1 ) );

    EXPECT_EQ( DMA2_Stream6->M0AR,
               ( uint32_t )( uintptr_t )&hw_uart_channel_states[HW_UART_CHANNEL_1]
                   .tx_buffer[HW_UART_TX_BUFFER_SIZE - 2U] );
    EXPECT_EQ( DMA2_Stream6->NDTR, 2U );

    fake_dma2.HISR |= DMA_HISR_TCIF6;
    DMA2_Stream6_IRQHandler();
    fake_dma2.HISR = 0U;

    EXPECT_TRUE( HW_UART_Is_Tx_Busy( HW_UART_CHANNEL_1 ) );
    EXPECT_EQ( DMA2_Stream6->M0AR,
               ( uint32_t )( uintptr_t )&hw_uart_channel_states[HW_UART_CHANNEL_1].tx_buffer[0] );
    EXPECT_EQ( DMA2_Stream6->NDTR, 2U );

    fake_dma2.HISR |= DMA_HISR_TCIF6;
    DMA2_Stream6_IRQHandler();

    EXPECT_FALSE( HW_UART_Is_Tx_Busy( HW_UART_CHANNEL_1 ) );
}

TEST_F( UartTest, ConsoleInitConfiguresUsart3AndArmsRxInterrupt )
{
    ASSERT_TRUE( HW_UART_CONSOLE_Init( 115200U ) );

    EXPECT_EQ( huart3.Instance, USART3 );
    EXPECT_EQ( huart3.Init.BaudRate, 115200U );
    EXPECT_EQ( huart3.Init.WordLength, UART_WORDLENGTH_8B );
    EXPECT_EQ( huart3.Init.StopBits, UART_STOPBITS_1 );
    EXPECT_EQ( huart3.Init.Parity, UART_PARITY_NONE );
    EXPECT_EQ( huart3.Init.Mode, UART_MODE_TX_RX );

    EXPECT_EQ( mock_receive_it_huart, &huart3 );
    EXPECT_NE( mock_receive_it_data, nullptr );
    EXPECT_EQ( mock_receive_it_size, 1U );
    EXPECT_EQ( mock_receive_it_count, 1U );
}

TEST_F( UartTest, ConsoleInitFailsIfHalInitFails )
{
    mock_hal_uart_init_result = HAL_ERROR;

    EXPECT_FALSE( HW_UART_CONSOLE_Init( 115200U ) );
}

TEST_F( UartTest, ConsoleInitFailsIfReceiveItFails )
{
    mock_hal_receive_it_result = HAL_ERROR;

    EXPECT_FALSE( HW_UART_CONSOLE_Init( 115200U ) );
}

TEST_F( UartTest, ConsoleReadRejectsNullDestination )
{
    ASSERT_TRUE( HW_UART_CONSOLE_Init( 115200U ) );

    uint32_t bytes_read = 123U;

    EXPECT_FALSE( HW_UART_CONSOLE_Read( nullptr, 10U, &bytes_read ) );
}

TEST_F( UartTest, ConsoleReadRejectsNullBytesRead )
{
    ASSERT_TRUE( HW_UART_CONSOLE_Init( 115200U ) );

    uint8_t buffer[4] = {};

    EXPECT_FALSE( HW_UART_CONSOLE_Read( buffer, sizeof( buffer ), nullptr ) );
}

TEST_F( UartTest, ConsoleReadBeforeInitReturnsZeroBytes )
{
    uint8_t  buffer[4]  = {};
    uint32_t bytes_read = 123U;

    ASSERT_TRUE( HW_UART_CONSOLE_Read( buffer, sizeof( buffer ), &bytes_read ) );

    EXPECT_EQ( bytes_read, 0U );
}

TEST_F( UartTest, ConsoleReadWithZeroSizeReturnsZeroBytes )
{
    uint8_t  buffer[4]  = {};
    uint32_t bytes_read = 123U;

    ASSERT_TRUE( HW_UART_CONSOLE_Init( 115200U ) );

    ASSERT_TRUE( HW_UART_CONSOLE_Read( buffer, 0U, &bytes_read ) );

    EXPECT_EQ( bytes_read, 0U );
}

TEST_F( UartTest, ConsoleRxCallbackStoresByteAndReadReturnsIt )
{
    uint8_t  buffer[4]  = {};
    uint32_t bytes_read = 0U;

    ASSERT_TRUE( HW_UART_CONSOLE_Init( 115200U ) );
    ASSERT_NE( mock_receive_it_data, nullptr );

    *mock_receive_it_data = 'A';

    HAL_UART_RxCpltCallback( &huart3 );

    ASSERT_TRUE( HW_UART_CONSOLE_Read( buffer, sizeof( buffer ), &bytes_read ) );

    EXPECT_EQ( bytes_read, 1U );
    EXPECT_EQ( buffer[0], 'A' );
    EXPECT_EQ( mock_receive_it_count, 2U );
}

TEST_F( UartTest, ConsoleRxCallbackStoresMultipleBytesInOrder )
{
    uint8_t  buffer[4]  = {};
    uint32_t bytes_read = 0U;

    ASSERT_TRUE( HW_UART_CONSOLE_Init( 115200U ) );
    ASSERT_NE( mock_receive_it_data, nullptr );

    *mock_receive_it_data = 'A';
    HAL_UART_RxCpltCallback( &huart3 );

    *mock_receive_it_data = 'B';
    HAL_UART_RxCpltCallback( &huart3 );

    *mock_receive_it_data = 'C';
    HAL_UART_RxCpltCallback( &huart3 );

    ASSERT_TRUE( HW_UART_CONSOLE_Read( buffer, sizeof( buffer ), &bytes_read ) );

    EXPECT_EQ( bytes_read, 3U );
    EXPECT_EQ( buffer[0], 'A' );
    EXPECT_EQ( buffer[1], 'B' );
    EXPECT_EQ( buffer[2], 'C' );
}

TEST_F( UartTest, ConsoleRxCallbackDropsByteWhenRingBufferFull )
{
    uint8_t  buffer[HW_UART_CONSOLE_RX_BUFFER_SIZE] = {};
    uint32_t bytes_read                             = 0U;

    ASSERT_TRUE( HW_UART_CONSOLE_Init( 115200U ) );
    ASSERT_NE( mock_receive_it_data, nullptr );

    for ( uint32_t i = 0U; i < TEST_HW_UART_CONSOLE_RX_CAPACITY; i++ )
    {
        *mock_receive_it_data = ( uint8_t )i;
        HAL_UART_RxCpltCallback( &huart3 );
    }

    *mock_receive_it_data = 0xFFU;
    HAL_UART_RxCpltCallback( &huart3 );

    ASSERT_TRUE( HW_UART_CONSOLE_Read( buffer, sizeof( buffer ), &bytes_read ) );

    EXPECT_EQ( bytes_read, TEST_HW_UART_CONSOLE_RX_CAPACITY );
    EXPECT_EQ( s_rx_overflow_count, 1U );
}

TEST_F( UartTest, ConsoleRxCallbackIgnoresOtherUart )
{
    uint8_t  buffer[4]  = {};
    uint32_t bytes_read = 0U;

    ASSERT_TRUE( HW_UART_CONSOLE_Init( 115200U ) );
    ASSERT_NE( mock_receive_it_data, nullptr );

    *mock_receive_it_data = 'A';

    HAL_UART_RxCpltCallback( &huart6 );

    ASSERT_TRUE( HW_UART_CONSOLE_Read( buffer, sizeof( buffer ), &bytes_read ) );

    EXPECT_EQ( bytes_read, 0U );
    EXPECT_EQ( mock_receive_it_count, 1U );
}

TEST_F( UartTest, ConsoleErrorCallbackRearmsRxForConsoleUart )
{
    ASSERT_TRUE( HW_UART_CONSOLE_Init( 115200U ) );

    uint32_t count_before = mock_receive_it_count;

    HAL_UART_ErrorCallback( &huart3 );

    EXPECT_EQ( mock_receive_it_count, count_before + 1U );
    EXPECT_EQ( mock_receive_it_huart, &huart3 );
    EXPECT_EQ( mock_receive_it_size, 1U );
}

TEST_F( UartTest, ConsoleErrorCallbackIgnoresOtherUart )
{
    ASSERT_TRUE( HW_UART_CONSOLE_Init( 115200U ) );

    uint32_t count_before = mock_receive_it_count;

    HAL_UART_ErrorCallback( &huart6 );

    EXPECT_EQ( mock_receive_it_count, count_before );
}

TEST_F( UartTest, ConsoleWriteRejectsNullData )
{
    ASSERT_TRUE( HW_UART_CONSOLE_Init( 115200U ) );

    EXPECT_FALSE( HW_UART_CONSOLE_Write_Blocking( nullptr, 1U, 100U ) );
}

TEST_F( UartTest, ConsoleWriteRejectsBeforeInit )
{
    uint8_t payload[2] = { 'O', 'K' };

    EXPECT_FALSE( HW_UART_CONSOLE_Write_Blocking( payload, sizeof( payload ), 100U ) );
}

TEST_F( UartTest, ConsoleWriteZeroLengthSucceedsAfterInitWithoutTransmit )
{
    uint8_t payload[2] = { 'O', 'K' };

    ASSERT_TRUE( HW_UART_CONSOLE_Init( 115200U ) );

    EXPECT_TRUE( HW_UART_CONSOLE_Write_Blocking( payload, 0U, 100U ) );
    EXPECT_EQ( mock_transmit_count, 0U );
}

TEST_F( UartTest, ConsoleWriteRejectsZeroTimeout )
{
    uint8_t payload[2] = { 'O', 'K' };

    ASSERT_TRUE( HW_UART_CONSOLE_Init( 115200U ) );

    EXPECT_FALSE( HW_UART_CONSOLE_Write_Blocking( payload, sizeof( payload ), 0U ) );
    EXPECT_EQ( mock_transmit_count, 0U );
}

TEST_F( UartTest, ConsoleWriteCallsBlockingHalTransmit )
{
    uint8_t payload[2] = { 'O', 'K' };

    ASSERT_TRUE( HW_UART_CONSOLE_Init( 115200U ) );

    EXPECT_TRUE( HW_UART_CONSOLE_Write_Blocking( payload, sizeof( payload ), 100U ) );

    EXPECT_EQ( mock_transmit_huart, &huart3 );
    EXPECT_EQ( mock_transmit_data, payload );
    EXPECT_EQ( mock_transmit_size, sizeof( payload ) );
    EXPECT_EQ( mock_transmit_timeout, 100U );
    EXPECT_EQ( mock_transmit_count, 1U );
}

TEST_F( UartTest, ConsoleWriteFailsIfHalTransmitFails )
{
    uint8_t payload[2] = { 'O', 'K' };

    ASSERT_TRUE( HW_UART_CONSOLE_Init( 115200U ) );

    mock_hal_transmit_result = HAL_ERROR;

    EXPECT_FALSE( HW_UART_CONSOLE_Write_Blocking( payload, sizeof( payload ), 100U ) );
}

TEST_F( UartTest, ConsoleIrqHandlerDispatchesToHal )
{
    USART3_IRQHandler();

    EXPECT_EQ( mock_irq_handler_huart, &huart3 );
    EXPECT_EQ( mock_irq_handler_count, 1U );
}

TEST_F( UartTest, DutRxDmaChannel1TransferErrorClearsRxStreamFlags )
{
    fake_dma2.LISR |= DMA_LISR_TEIF2;

    DMA2_Stream2_IRQHandler();

    EXPECT_EQ( fake_dma2.LIFCR, HW_UART_CH1_DMA_RX_IFCR_MASK );
}

TEST_F( UartTest, DutRxDmaChannel1DirectModeErrorClearsRxStreamFlags )
{
    fake_dma2.LISR |= DMA_LISR_DMEIF2;

    DMA2_Stream2_IRQHandler();

    EXPECT_EQ( fake_dma2.LIFCR, HW_UART_CH1_DMA_RX_IFCR_MASK );
}

TEST_F( UartTest, DutRxDmaChannel1FifoErrorClearsRxStreamFlags )
{
    fake_dma2.LISR |= DMA_LISR_FEIF2;

    DMA2_Stream2_IRQHandler();

    EXPECT_EQ( fake_dma2.LIFCR, HW_UART_CH1_DMA_RX_IFCR_MASK );
}

TEST_F( UartTest, DutRxDmaChannel1UnexpectedHalfTransferClearsRxStreamFlags )
{
    fake_dma2.LISR |= DMA_LISR_HTIF2;

    DMA2_Stream2_IRQHandler();

    EXPECT_EQ( fake_dma2.LIFCR, HW_UART_CH1_DMA_RX_IFCR_MASK );
}

TEST_F( UartTest, DutRxDmaChannel1UnexpectedTransferCompleteClearsRxStreamFlags )
{
    fake_dma2.LISR |= DMA_LISR_TCIF2;

    DMA2_Stream2_IRQHandler();

    EXPECT_EQ( fake_dma2.LIFCR, HW_UART_CH1_DMA_RX_IFCR_MASK );
}

TEST_F( UartTest, DutRxDmaChannel2TransferErrorClearsRxStreamFlags )
{
    fake_dma1.HISR |= DMA_HISR_TEIF5;

    DMA1_Stream5_IRQHandler();

    EXPECT_EQ( fake_dma1.HIFCR, HW_UART_CH2_DMA_RX_IFCR_MASK );
}

TEST_F( UartTest, DutRxDmaChannel2DirectModeErrorClearsRxStreamFlags )
{
    fake_dma1.HISR |= DMA_HISR_DMEIF5;

    DMA1_Stream5_IRQHandler();

    EXPECT_EQ( fake_dma1.HIFCR, HW_UART_CH2_DMA_RX_IFCR_MASK );
}

TEST_F( UartTest, DutRxDmaChannel2FifoErrorClearsRxStreamFlags )
{
    fake_dma1.HISR |= DMA_HISR_FEIF5;

    DMA1_Stream5_IRQHandler();

    EXPECT_EQ( fake_dma1.HIFCR, HW_UART_CH2_DMA_RX_IFCR_MASK );
}

TEST_F( UartTest, DutRxDmaChannel2UnexpectedHalfTransferClearsRxStreamFlags )
{
    fake_dma1.HISR |= DMA_HISR_HTIF5;

    DMA1_Stream5_IRQHandler();

    EXPECT_EQ( fake_dma1.HIFCR, HW_UART_CH2_DMA_RX_IFCR_MASK );
}

TEST_F( UartTest, DutRxDmaChannel2UnexpectedTransferCompleteClearsRxStreamFlags )
{
    fake_dma1.HISR |= DMA_HISR_TCIF5;

    DMA1_Stream5_IRQHandler();

    EXPECT_EQ( fake_dma1.HIFCR, HW_UART_CH2_DMA_RX_IFCR_MASK );
}
TEST_F( UartTest, DutRxDmaErrorDoesNotChangeRxRunningStateBeforeFaultPolicyExists )
{
    HwUartConfig_T config = TEST_HW_UART_Make_Tx_Rx_Config();

    ASSERT_TRUE( HW_UART_Configure_Channel( HW_UART_CHANNEL_1, &config ) );
    ASSERT_TRUE( HW_UART_Rx_Start( HW_UART_CHANNEL_1 ) );
    ASSERT_TRUE( HW_UART_Rx_Is_Running( HW_UART_CHANNEL_1 ) );

    fake_dma2.LISR |= DMA_LISR_TEIF2;

    DMA2_Stream2_IRQHandler();

    EXPECT_TRUE( HW_UART_Rx_Is_Running( HW_UART_CHANNEL_1 ) );
    EXPECT_EQ( fake_dma2.LIFCR, HW_UART_CH1_DMA_RX_IFCR_MASK );
}

TEST_F( UartTest, DutRxStartChannel1DisablesUnusedRxDmaHalfAndCompleteInterrupts )
{
    HwUartConfig_T config = TEST_HW_UART_Make_Tx_Rx_Config();

    ASSERT_TRUE( HW_UART_Configure_Channel( HW_UART_CHANNEL_1, &config ) );
    ASSERT_TRUE( HW_UART_Rx_Start( HW_UART_CHANNEL_1 ) );

    ASSERT_NE( huart6.hdmarx, nullptr );
    EXPECT_NE( huart6.hdmarx->disabled_interrupt_mask & DMA_IT_HT, 0U );
    EXPECT_NE( huart6.hdmarx->disabled_interrupt_mask & DMA_IT_TC, 0U );
}

TEST_F( UartTest, DutRxStartChannel2DisablesUnusedRxDmaHalfAndCompleteInterrupts )
{
    HwUartConfig_T config = TEST_HW_UART_Make_Tx_Rx_Config();

    ASSERT_TRUE( HW_UART_Configure_Channel( HW_UART_CHANNEL_2, &config ) );
    ASSERT_TRUE( HW_UART_Rx_Start( HW_UART_CHANNEL_2 ) );

    ASSERT_NE( huart2.hdmarx, nullptr );
    EXPECT_NE( huart2.hdmarx->disabled_interrupt_mask & DMA_IT_HT, 0U );
    EXPECT_NE( huart2.hdmarx->disabled_interrupt_mask & DMA_IT_TC, 0U );
}
