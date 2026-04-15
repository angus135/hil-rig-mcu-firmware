/******************************************************************************
 *  File:       test_hw_uart.cpp
 *  Author:     Callum Rafferty
 *  Created:    14-04-2026
 *
 *  Description:
 *      Minimal unit test harness for hw_uart.
 *
 *      hw_uart.c is compiled directly into this test translation unit. The HAL
 *      and LL entry points required by the current implementation are provided
 *      here as simple link seams so that the test target can build in the host
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
#include "hw_uart_mocks.h"
#include "hw_uart_dut.h"
#include "hw_uart_console.h"
}

/**-----------------------------------------------------------------------------
 *  Test Constants / Macros
 *------------------------------------------------------------------------------
 */

#ifndef DMA_LISR_TCIF3
#define DMA_LISR_TCIF3 ( 1U << 0 )
#endif

#ifndef DMA_LISR_TEIF3
#define DMA_LISR_TEIF3 ( 1U << 1 )
#endif

#ifndef DMA_HISR_TCIF6
#define DMA_HISR_TCIF6 ( 1U << 2 )
#endif

#ifndef DMA_HISR_TEIF6
#define DMA_HISR_TEIF6 ( 1U << 3 )
#endif

/**-----------------------------------------------------------------------------
 *  Test Doubles / Mocks
 *------------------------------------------------------------------------------
 */

extern "C"
{
DMA_TypeDef fake_dma1 = { 0U, 0U, 0U, 0U };
DMA_TypeDef fake_dma2 = { 0U, 0U, 0U, 0U };
}

/**-----------------------------------------------------------------------------
 *  Private Helper Functions
 *------------------------------------------------------------------------------
 */

static DMA_Stream_TypeDef* TEST_HW_UART_Get_Stream( DMA_TypeDef* dma, uint32_t stream )
{
    if ( dma == DMA1 )
    {
        switch ( stream )
        {
            case LL_DMA_STREAM_1:
                return DMA1_Stream1;
            case LL_DMA_STREAM_3:
                return DMA1_Stream3;
            case LL_DMA_STREAM_5:
                return DMA1_Stream5;
            case LL_DMA_STREAM_6:
                return DMA1_Stream6;
            default:
                return nullptr;
        }
    }

    if ( dma == DMA2 )
    {
        switch ( stream )
        {
            case LL_DMA_STREAM_1:
                return DMA2_Stream1;
            case LL_DMA_STREAM_6:
                return DMA2_Stream6;
            default:
                return nullptr;
        }
    }

    return nullptr;
}

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

extern "C" HAL_StatusTypeDef HAL_UART_Receive_DMA( UART_HandleTypeDef* huart, uint8_t* pData,
                                                   uint16_t Size )
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

extern "C" void LL_DMA_DisableStream( DMA_TypeDef* dma, uint32_t stream )
{
    DMA_Stream_TypeDef* dma_stream = TEST_HW_UART_Get_Stream( dma, stream );
    if ( dma_stream != nullptr )
    {
        CLEAR_BIT( dma_stream->CR, DMA_SxCR_EN );
    }
}

extern "C" void LL_DMA_EnableStream( DMA_TypeDef* dma, uint32_t stream )
{
    DMA_Stream_TypeDef* dma_stream = TEST_HW_UART_Get_Stream( dma, stream );
    if ( dma_stream != nullptr )
    {
        SET_BIT( dma_stream->CR, DMA_SxCR_EN );
    }
}

extern "C" uint32_t LL_DMA_IsEnabledStream( DMA_TypeDef* dma, uint32_t stream )
{
    DMA_Stream_TypeDef* dma_stream = TEST_HW_UART_Get_Stream( dma, stream );
    if ( dma_stream == nullptr )
    {
        return 0U;
    }

    return ( ( dma_stream->CR & DMA_SxCR_EN ) != 0U ) ? 1U : 0U;
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

extern "C" uint32_t LL_DMA_IsActiveFlag_TC3( DMA_TypeDef* dma )
{
    if ( dma == nullptr )
    {
        return 0U;
    }

    return ( ( dma->LISR & DMA_LISR_TCIF3 ) != 0U ) ? 1U : 0U;
}

extern "C" uint32_t LL_DMA_IsActiveFlag_TE3( DMA_TypeDef* dma )
{
    if ( dma == nullptr )
    {
        return 0U;
    }

    return ( ( dma->LISR & DMA_LISR_TEIF3 ) != 0U ) ? 1U : 0U;
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

extern "C" void __HAL_UART_ENABLE_IT( UART_HandleTypeDef* huart, uint32_t interrupt )
{
    if ( ( huart != NULL ) && ( huart->Instance != NULL ) )
    {
        huart->Instance->CR1 |= interrupt;
    }
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
        fake_dma1 = {};
        fake_dma2 = {};

        huart6 = {};
        huart2 = {};
        huart3 = {};

        huart6.Instance = USART6;
        huart2.Instance = USART2;
        huart3.Instance = USART3;

        USART2->SR   = 0U;
        USART2->DR   = 0U;
        USART2->BRR  = 0U;
        USART2->CR1  = 0U;
        USART2->CR2  = 0U;
        USART2->CR3  = 0U;
        USART2->GTPR = 0U;

        USART3->SR   = 0U;
        USART3->DR   = 0U;
        USART3->BRR  = 0U;
        USART3->CR1  = 0U;
        USART3->CR2  = 0U;
        USART3->CR3  = 0U;
        USART3->GTPR = 0U;

        USART6->SR   = 0U;
        USART6->DR   = 0U;
        USART6->BRR  = 0U;
        USART6->CR1  = 0U;
        USART6->CR2  = 0U;
        USART6->CR3  = 0U;
        USART6->GTPR = 0U;

        DMA1_Stream1->CR   = 0U;
        DMA1_Stream1->NDTR = 0U;
        DMA1_Stream1->PAR  = 0U;
        DMA1_Stream1->M0AR = 0U;
        DMA1_Stream1->FCR  = 0U;

        DMA1_Stream3->CR   = 0U;
        DMA1_Stream3->NDTR = 0U;
        DMA1_Stream3->PAR  = 0U;
        DMA1_Stream3->M0AR = 0U;
        DMA1_Stream3->FCR  = 0U;

        DMA1_Stream5->CR   = 0U;
        DMA1_Stream5->NDTR = 0U;
        DMA1_Stream5->PAR  = 0U;
        DMA1_Stream5->M0AR = 0U;
        DMA1_Stream5->FCR  = 0U;

        DMA1_Stream6->CR   = 0U;
        DMA1_Stream6->NDTR = 0U;
        DMA1_Stream6->PAR  = 0U;
        DMA1_Stream6->M0AR = 0U;
        DMA1_Stream6->FCR  = 0U;

        DMA2_Stream1->CR   = 0U;
        DMA2_Stream1->NDTR = 0U;
        DMA2_Stream1->PAR  = 0U;
        DMA2_Stream1->M0AR = 0U;
        DMA2_Stream1->FCR  = 0U;

        DMA2_Stream6->CR   = 0U;
        DMA2_Stream6->NDTR = 0U;
        DMA2_Stream6->PAR  = 0U;
        DMA2_Stream6->M0AR = 0U;
        DMA2_Stream6->FCR  = 0U;
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