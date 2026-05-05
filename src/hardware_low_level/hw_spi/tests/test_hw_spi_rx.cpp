/******************************************************************************
 *  File:       test_hw_spi_rx.cpp
 *  Author:     Angus Corr
 *  Created:    05-May-2026
 *
 *  Description:
 *      Unit tests for the RX portion of the low-level SPI driver using
 *      GoogleTest and GoogleMock.
 *
 *  Notes:
 *      - Production code is written in C; tests are written in C++.
 *      - C headers and implementation files are included inside extern "C".
 *      - This file follows the existing test_hw_spi.cpp style, but targets the
 *        split SPI implementation files.
 *      - This file is intended to be built as its own test target. If all three
 *        generated test files are linked into one executable, compile the SPI C
 *        sources separately instead of including them in each test file.
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */
#define HW_SPI_INTERNAL
#include <gtest/gtest.h>
#include <gmock/gmock.h>

extern "C"
{
#ifndef TEST_BUILD
#define TEST_BUILD
#endif
#include "hw_spi_mocks.h"
#include "hw_spi.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "../hw_spi_config.c"     // NOLINT
#include "../hw_spi_rx.c"         // NOLINT
#include "../hw_spi_tx_config.c"  // NOLINT
#include "../hw_spi_tx_master.c"  // NOLINT
#include "../hw_spi_tx_slave.c"   // NOLINT
}

using ::testing::_;
using ::testing::AnyNumber;
using ::testing::AtLeast;
using ::testing::Eq;
using ::testing::InSequence;
using ::testing::Invoke;
using ::testing::Return;
using ::testing::StrictMock;

/**-----------------------------------------------------------------------------
 *  Test Constants / Macros
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Test Doubles / Mocks
 *------------------------------------------------------------------------------
 */

class MockHWSPI
{
public:
    MOCK_METHOD( HAL_StatusTypeDef, SPIInit, ( SPI_HandleTypeDef * hspi ), () );
    MOCK_METHOD( HAL_StatusTypeDef, SPIReceiveDMA,
                 ( SPI_HandleTypeDef * hspi, uint8_t* pData, uint16_t size ), () );
    MOCK_METHOD( HAL_StatusTypeDef, SPIDMAStop, ( SPI_HandleTypeDef * hspi ), () );

    MOCK_METHOD( uint32_t, DMAGetDataLength, ( void* dma, uint32_t stream ), () );
    MOCK_METHOD( void, DMADisableStream, ( DMA_TypeDef * dma, uint32_t stream ), () );
    MOCK_METHOD( uint32_t, DMAIsEnabledStream, ( DMA_TypeDef * dma, uint32_t stream ), () );
    MOCK_METHOD( void, DMASetMemoryAddress,
                 ( DMA_TypeDef * dma, uint32_t stream, uint32_t address ), () );
    MOCK_METHOD( void, DMASetPeriphAddress,
                 ( DMA_TypeDef * dma, uint32_t stream, uint32_t address ), () );
    MOCK_METHOD( uint32_t, SPIDMAGetRegAddr, ( const SPI_TypeDef* spi ), () );
    MOCK_METHOD( void, DMASetDataLength, ( DMA_TypeDef * dma, uint32_t stream, uint32_t length ),
                 () );
    MOCK_METHOD( void, SPIEnableDMAReqTX, ( SPI_TypeDef * spi ), () );
    MOCK_METHOD( void, DMAEnableITTC, ( DMA_TypeDef * dma, uint32_t stream ), () );
    MOCK_METHOD( void, DMAEnableITTE, ( DMA_TypeDef * dma, uint32_t stream ), () );
    MOCK_METHOD( void, DMAEnableStream, ( DMA_TypeDef * dma, uint32_t stream ), () );
    MOCK_METHOD( void, DMADisableITTC, ( DMA_TypeDef * dma, uint32_t stream ), () );
    MOCK_METHOD( void, DMADisableITTE, ( DMA_TypeDef * dma, uint32_t stream ), () );
    MOCK_METHOD( void, SPIDisableDMAReqTX, ( SPI_TypeDef * spi ), () );
    MOCK_METHOD( void, SPIEnableDMAReqRX, ( SPI_TypeDef * spi ), () );
    MOCK_METHOD( void, SPIEnable, ( SPI_TypeDef * spi ), () );
    MOCK_METHOD( uint32_t, SPIIsBusy, ( const SPI_TypeDef* spi ), () );

    MOCK_METHOD( uint32_t, DMAIsActiveFlagTE5, ( DMA_TypeDef * dma ), () );
    MOCK_METHOD( void, DMAClearFlagTE5, ( DMA_TypeDef * dma ), () );
    MOCK_METHOD( uint32_t, DMAIsActiveFlagTC5, ( DMA_TypeDef * dma ), () );
    MOCK_METHOD( void, DMAClearFlagTC5, ( DMA_TypeDef * dma ), () );
    MOCK_METHOD( uint32_t, DMAIsActiveFlagTE1, ( DMA_TypeDef * dma ), () );
    MOCK_METHOD( void, DMAClearFlagTE1, ( DMA_TypeDef * dma ), () );
    MOCK_METHOD( uint32_t, DMAIsActiveFlagTC1, ( DMA_TypeDef * dma ), () );
    MOCK_METHOD( void, DMAClearFlagTC1, ( DMA_TypeDef * dma ), () );

    MOCK_METHOD( void, DMASetMemorySize, ( DMA_TypeDef * dma, uint32_t stream, uint32_t size ),
                 () );
    MOCK_METHOD( void, DMASetPeriphSize, ( DMA_TypeDef * dma, uint32_t stream, uint32_t size ),
                 () );

    MOCK_METHOD( void, NVICDisableIRQ, ( IRQn_Type irqn ), () );
    MOCK_METHOD( void, NVICEnableIRQ, ( IRQn_Type irqn ), () );

    MOCK_METHOD( void, TimerConfigure, ( Timer_T timer, uint32_t psc, uint32_t arr ), () );
    MOCK_METHOD( void, TimerStart, ( Timer_T timer ), () );
    MOCK_METHOD( void, TimerStop, ( Timer_T timer ), () );
};

static MockHWSPI* g_mock = nullptr;

extern "C" HAL_StatusTypeDef HAL_SPI_Init( SPI_HandleTypeDef* hspi )
{
    return g_mock ? g_mock->SPIInit( hspi ) : HAL_ERROR;
}

extern "C" HAL_StatusTypeDef HAL_SPI_Receive_DMA( SPI_HandleTypeDef* hspi, uint8_t* pData,
                                                  uint16_t Size )
{
    return g_mock ? g_mock->SPIReceiveDMA( hspi, pData, Size ) : HAL_ERROR;
}

extern "C" HAL_StatusTypeDef HAL_SPI_DMAStop( SPI_HandleTypeDef* hspi )
{
    return g_mock ? g_mock->SPIDMAStop( hspi ) : HAL_ERROR;
}

extern "C" uint32_t LL_DMA_GetDataLength( void* DMAx, uint32_t Stream )
{
    return g_mock ? g_mock->DMAGetDataLength( DMAx, Stream ) : 0U;
}

extern "C" void LL_DMA_DisableStream( DMA_TypeDef* DMAx, uint32_t Stream )
{
    if ( g_mock )
    {
        g_mock->DMADisableStream( DMAx, Stream );
    }
}

extern "C" uint32_t LL_DMA_IsEnabledStream( DMA_TypeDef* DMAx, uint32_t Stream )
{
    return g_mock ? g_mock->DMAIsEnabledStream( DMAx, Stream ) : 0U;
}

extern "C" void LL_DMA_SetMemoryAddress( DMA_TypeDef* DMAx, uint32_t Stream,
                                         uint32_t MemoryAddress )
{
    if ( g_mock )
    {
        g_mock->DMASetMemoryAddress( DMAx, Stream, MemoryAddress );
    }
}

extern "C" void LL_DMA_SetPeriphAddress( DMA_TypeDef* DMAx, uint32_t Stream,
                                         uint32_t PeriphAddress )
{
    if ( g_mock )
    {
        g_mock->DMASetPeriphAddress( DMAx, Stream, PeriphAddress );
    }
}

extern "C" uint32_t LL_SPI_DMA_GetRegAddr( const SPI_TypeDef* SPIx )
{
    return g_mock ? g_mock->SPIDMAGetRegAddr( SPIx ) : 0U;
}

extern "C" void LL_DMA_SetDataLength( DMA_TypeDef* DMAx, uint32_t Stream, uint32_t NbData )
{
    if ( g_mock )
    {
        g_mock->DMASetDataLength( DMAx, Stream, NbData );
    }
}

extern "C" void LL_SPI_EnableDMAReq_TX( SPI_TypeDef* SPIx )
{
    if ( g_mock )
    {
        g_mock->SPIEnableDMAReqTX( SPIx );
    }
}

extern "C" void LL_SPI_EnableDMAReq_RX( SPI_TypeDef* SPIx )
{
    if ( g_mock )
    {
        g_mock->SPIEnableDMAReqRX( SPIx );
    }
}

extern "C" void LL_SPI_Enable( SPI_TypeDef* SPIx )
{
    if ( g_mock )
    {
        g_mock->SPIEnable( SPIx );
    }
}

extern "C" void LL_DMA_EnableIT_TC( DMA_TypeDef* DMAx, uint32_t Stream )
{
    if ( g_mock )
    {
        g_mock->DMAEnableITTC( DMAx, Stream );
    }
}

extern "C" void LL_DMA_EnableIT_TE( DMA_TypeDef* DMAx, uint32_t Stream )
{
    if ( g_mock )
    {
        g_mock->DMAEnableITTE( DMAx, Stream );
    }
}

extern "C" void LL_DMA_EnableStream( DMA_TypeDef* DMAx, uint32_t Stream )
{
    if ( g_mock )
    {
        g_mock->DMAEnableStream( DMAx, Stream );
    }
}

extern "C" void LL_DMA_DisableIT_TC( DMA_TypeDef* DMAx, uint32_t Stream )
{
    if ( g_mock )
    {
        g_mock->DMADisableITTC( DMAx, Stream );
    }
}

extern "C" void LL_DMA_DisableIT_TE( DMA_TypeDef* DMAx, uint32_t Stream )
{
    if ( g_mock )
    {
        g_mock->DMADisableITTE( DMAx, Stream );
    }
}

extern "C" void LL_SPI_DisableDMAReq_TX( SPI_TypeDef* SPIx )
{
    if ( g_mock )
    {
        g_mock->SPIDisableDMAReqTX( SPIx );
    }
}

extern "C" uint32_t LL_SPI_IsActiveFlag_BSY( const SPI_TypeDef* SPIx )
{
    return g_mock ? g_mock->SPIIsBusy( SPIx ) : 0U;
}

extern "C" uint32_t LL_DMA_IsActiveFlag_TE5( DMA_TypeDef* DMAx )
{
    return g_mock ? g_mock->DMAIsActiveFlagTE5( DMAx ) : 0U;
}

extern "C" void LL_DMA_ClearFlag_TE5( DMA_TypeDef* DMAx )
{
    if ( g_mock )
    {
        g_mock->DMAClearFlagTE5( DMAx );
    }
}

extern "C" uint32_t LL_DMA_IsActiveFlag_TC5( DMA_TypeDef* DMAx )
{
    return g_mock ? g_mock->DMAIsActiveFlagTC5( DMAx ) : 0U;
}

extern "C" void LL_DMA_ClearFlag_TC5( DMA_TypeDef* DMAx )
{
    if ( g_mock )
    {
        g_mock->DMAClearFlagTC5( DMAx );
    }
}

extern "C" uint32_t LL_DMA_IsActiveFlag_TE1( DMA_TypeDef* DMAx )
{
    return g_mock ? g_mock->DMAIsActiveFlagTE1( DMAx ) : 0U;
}

extern "C" void LL_DMA_ClearFlag_TE1( DMA_TypeDef* DMAx )
{
    if ( g_mock )
    {
        g_mock->DMAClearFlagTE1( DMAx );
    }
}

extern "C" uint32_t LL_DMA_IsActiveFlag_TC1( DMA_TypeDef* DMAx )
{
    return g_mock ? g_mock->DMAIsActiveFlagTC1( DMAx ) : 0U;
}

extern "C" void LL_DMA_ClearFlag_TC1( DMA_TypeDef* DMAx )
{
    if ( g_mock )
    {
        g_mock->DMAClearFlagTC1( DMAx );
    }
}

extern "C" void LL_DMA_SetMemorySize( DMA_TypeDef* DMAx, uint32_t Stream, uint32_t Size )
{
    if ( g_mock )
    {
        g_mock->DMASetMemorySize( DMAx, Stream, Size );
    }
}

extern "C" void LL_DMA_SetPeriphSize( DMA_TypeDef* DMAx, uint32_t Stream, uint32_t Size )
{
    if ( g_mock )
    {
        g_mock->DMASetPeriphSize( DMAx, Stream, Size );
    }
}

extern "C" void NVIC_DisableIRQ( IRQn_Type IRQn )
{
    if ( g_mock )
    {
        g_mock->NVICDisableIRQ( IRQn );
    }
}

extern "C" void NVIC_EnableIRQ( IRQn_Type IRQn )
{
    if ( g_mock )
    {
        g_mock->NVICEnableIRQ( IRQn );
    }
}

extern "C" void HW_TIMER_Configure_Timer( Timer_T timer, uint32_t psc, uint32_t arr )
{
    if ( g_mock )
    {
        g_mock->TimerConfigure( timer, psc, arr );
    }
}

extern "C" void HW_TIMER_Start_Timer( Timer_T timer )
{
    if ( g_mock )
    {
        g_mock->TimerStart( timer );
    }
}

extern "C" void HW_TIMER_Stop_Timer( Timer_T timer )
{
    if ( g_mock )
    {
        g_mock->TimerStop( timer );
    }
}

class HWSPIRxTest : public ::testing::Test
{
protected:
    StrictMock<MockHWSPI> mock;

    static HWSPIConfig_T MakeMasterConfig( SPIDataSize_T size = SPI_SIZE_8_BIT,
                                           SPIBaudRate_T baud = SPI_BAUD_45MBIT )
    {
        return HWSPIConfig_T{ .spi_mode  = SPI_MASTER_MODE,
                              .data_size = size,
                              .first_bit = SPI_FIRST_MSB,
                              .baud_rate = baud,
                              .cpol      = SPI_CPOL_LOW,
                              .cpha      = SPI_CPHA_1_EDGE };
    }

    static HWSPIConfig_T MakeSlaveConfig( SPIDataSize_T size = SPI_SIZE_8_BIT,
                                          SPIBaudRate_T baud = SPI_BAUD_45MBIT )
    {
        return HWSPIConfig_T{ .spi_mode  = SPI_SLAVE_MODE,
                              .data_size = size,
                              .first_bit = SPI_FIRST_MSB,
                              .baud_rate = baud,
                              .cpol      = SPI_CPOL_LOW,
                              .cpha      = SPI_CPHA_1_EDGE };
    }

    static void InitialiseState( SPIPeripheralState_T* state, SPIPeripheral_T logical,
                                 HWSPIConfig_T config, DMA_TypeDef* rx_dma, uint32_t rx_stream,
                                 DMA_TypeDef* tx_dma, uint32_t tx_stream, SPI_TypeDef* spi,
                                 IRQn_Type tx_irqn, Timer_T timer )
    {
        memset( state, 0, sizeof( *state ) );
        state->config                    = config;
        state->logical_peripheral        = logical;
        state->is_master                 = config.spi_mode == SPI_MASTER_MODE;
        state->frame_size_bytes          = config.data_size == SPI_SIZE_16_BIT ? 2U : 1U;
        state->frame_shift               = config.data_size == SPI_SIZE_16_BIT ? 1U : 0U;
        state->tx_uses_final_drain_timer = config.baud_rate > SPI_BAUD_5M625BIT;
        state->tx_final_drain_cycles     = 0U;
        state->tx_final_drain_timer      = timer;
        state->rx_dma                    = rx_dma;
        state->rx_dma_stream             = rx_stream;
        state->tx_dma                    = tx_dma;
        state->tx_dma_stream             = tx_stream;
        state->spi_peripheral            = spi;
        state->tx_dma_irqn               = tx_irqn;
        state->tx_transaction_state      = HW_SPI_TX_TRANSACTION_IDLE;
        HW_SPI_TX_Configure_Operations( state );
        HW_SPI_TX_Reset_State( state );
    }

    void SetUp( void ) override
    {
        g_mock = &mock;
        memset( &SPI_CHANNEL_0_HANDLE, 0, sizeof( SPI_CHANNEL_0_HANDLE ) );
        memset( &SPI_CHANNEL_1_HANDLE, 0, sizeof( SPI_CHANNEL_1_HANDLE ) );

        channel_0_state = &channel_0_state_struct;
        channel_1_state = &channel_1_state_struct;
        dac_state       = &dac_state_struct;

        EXPECT_CALL( mock, TimerConfigure( _, _, _ ) ).Times( AnyNumber() );

        InitialiseState( channel_0_state, SPI_CHANNEL_0, MakeMasterConfig(), SPI_CHANNEL_0_RX_DMA,
                         SPI_CHANNEL_0_RX_DMA_STREAM, SPI_CHANNEL_0_TX_DMA,
                         SPI_CHANNEL_0_TX_DMA_STREAM, SPI_CHANNEL_0_INSTANCE,
                         SPI_CHANNEL_0_TX_DMA_IRQN, SPI_CHANNEL_0_TIMER );
        InitialiseState( channel_1_state, SPI_CHANNEL_1, MakeMasterConfig(), SPI_CHANNEL_1_RX_DMA,
                         SPI_CHANNEL_1_RX_DMA_STREAM, SPI_CHANNEL_1_TX_DMA,
                         SPI_CHANNEL_1_TX_DMA_STREAM, SPI_CHANNEL_1_INSTANCE,
                         SPI_CHANNEL_1_TX_DMA_IRQN, SPI_CHANNEL_1_TIMER );
        InitialiseState( dac_state, SPI_DAC, MakeMasterConfig(), NULL, 0U, SPI_DAC_TX_DMA,
                         SPI_DAC_TX_DMA_STREAM, SPI_DAC_INSTANCE, SPI_DAC_TX_DMA_IRQN,
                         SPI_DAC_TIMER );
    }

    void TearDown( void ) override
    {
        g_mock = nullptr;
    }

    void ExpectChannel0DmaProgram( const uint8_t* expected_ptr, uint32_t expected_elements )
    {
        EXPECT_CALL( mock, SPIDisableDMAReqTX( Eq( SPI_CHANNEL_0_INSTANCE ) ) );
        EXPECT_CALL( mock, DMADisableStream( Eq( SPI_CHANNEL_0_TX_DMA ),
                                             Eq( SPI_CHANNEL_0_TX_DMA_STREAM ) ) );
        EXPECT_CALL( mock, DMAIsEnabledStream( Eq( SPI_CHANNEL_0_TX_DMA ),
                                               Eq( SPI_CHANNEL_0_TX_DMA_STREAM ) ) )
            .WillOnce( Return( 0U ) );
        EXPECT_CALL( mock, DMAClearFlagTC5( Eq( SPI_CHANNEL_0_TX_DMA ) ) );
        EXPECT_CALL( mock, DMAClearFlagTE5( Eq( SPI_CHANNEL_0_TX_DMA ) ) );
        EXPECT_CALL(
            mock,
            DMASetMemoryAddress(
                Eq( SPI_CHANNEL_0_TX_DMA ), Eq( SPI_CHANNEL_0_TX_DMA_STREAM ),
                Eq( static_cast<uint32_t>( reinterpret_cast<uintptr_t>( expected_ptr ) ) ) ) );
        EXPECT_CALL( mock, DMASetDataLength( Eq( SPI_CHANNEL_0_TX_DMA ),
                                             Eq( SPI_CHANNEL_0_TX_DMA_STREAM ),
                                             Eq( expected_elements ) ) );
        EXPECT_CALL( mock, DMAEnableStream( Eq( SPI_CHANNEL_0_TX_DMA ),
                                            Eq( SPI_CHANNEL_0_TX_DMA_STREAM ) ) );
        EXPECT_CALL( mock, SPIEnableDMAReqTX( Eq( SPI_CHANNEL_0_INSTANCE ) ) );
    }

    void ExpectChannel1DmaProgram( const uint8_t* expected_ptr, uint32_t expected_elements )
    {
        EXPECT_CALL( mock, SPIDisableDMAReqTX( Eq( SPI_CHANNEL_1_INSTANCE ) ) );
        EXPECT_CALL( mock, DMADisableStream( Eq( SPI_CHANNEL_1_TX_DMA ),
                                             Eq( SPI_CHANNEL_1_TX_DMA_STREAM ) ) );
        EXPECT_CALL( mock, DMAIsEnabledStream( Eq( SPI_CHANNEL_1_TX_DMA ),
                                               Eq( SPI_CHANNEL_1_TX_DMA_STREAM ) ) )
            .WillOnce( Return( 0U ) );
        EXPECT_CALL( mock, DMAClearFlagTC1( Eq( SPI_CHANNEL_1_TX_DMA ) ) );
        EXPECT_CALL( mock, DMAClearFlagTE1( Eq( SPI_CHANNEL_1_TX_DMA ) ) );
        EXPECT_CALL(
            mock,
            DMASetMemoryAddress(
                Eq( SPI_CHANNEL_1_TX_DMA ), Eq( SPI_CHANNEL_1_TX_DMA_STREAM ),
                Eq( static_cast<uint32_t>( reinterpret_cast<uintptr_t>( expected_ptr ) ) ) ) );
        EXPECT_CALL( mock, DMASetDataLength( Eq( SPI_CHANNEL_1_TX_DMA ),
                                             Eq( SPI_CHANNEL_1_TX_DMA_STREAM ),
                                             Eq( expected_elements ) ) );
        EXPECT_CALL( mock, DMAEnableStream( Eq( SPI_CHANNEL_1_TX_DMA ),
                                            Eq( SPI_CHANNEL_1_TX_DMA_STREAM ) ) );
        EXPECT_CALL( mock, SPIEnableDMAReqTX( Eq( SPI_CHANNEL_1_INSTANCE ) ) );
    }
};

/**-----------------------------------------------------------------------------
 *  Test Fixture
 *------------------------------------------------------------------------------
 */

/**
 * @brief Test fixture for module tests.
 *
 * Provides a consistent setup/teardown environment for all test cases.
 */

TEST_F( HWSPIRxTest, StartChannel_Channel0ArmsPassiveRxDmaIn8BitMode )
{
    channel_0_state->config.data_size = SPI_SIZE_8_BIT;
    channel_0_state->frame_size_bytes = 1U;
    channel_0_state->frame_shift      = 0U;

    InSequence seq;
    EXPECT_CALL(
        mock, DMADisableStream( Eq( SPI_CHANNEL_0_RX_DMA ), Eq( SPI_CHANNEL_0_RX_DMA_STREAM ) ) );
    EXPECT_CALL(
        mock, DMAIsEnabledStream( Eq( SPI_CHANNEL_0_RX_DMA ), Eq( SPI_CHANNEL_0_RX_DMA_STREAM ) ) )
        .WillOnce( Return( 0U ) );
    EXPECT_CALL( mock,
                 DMASetMemoryAddress( Eq( SPI_CHANNEL_0_RX_DMA ), Eq( SPI_CHANNEL_0_RX_DMA_STREAM ),
                                      Eq( static_cast<uint32_t>( reinterpret_cast<uintptr_t>(
                                          channel_0_state->rx_buffer ) ) ) ) );
    EXPECT_CALL( mock, SPIDMAGetRegAddr( Eq( SPI_CHANNEL_0_INSTANCE ) ) )
        .WillOnce( Return( 0x12345678U ) );
    EXPECT_CALL( mock,
                 DMASetPeriphAddress( Eq( SPI_CHANNEL_0_RX_DMA ), Eq( SPI_CHANNEL_0_RX_DMA_STREAM ),
                                      Eq( 0x12345678U ) ) );
    EXPECT_CALL( mock,
                 DMASetDataLength( Eq( SPI_CHANNEL_0_RX_DMA ), Eq( SPI_CHANNEL_0_RX_DMA_STREAM ),
                                   Eq( RX_BUFFER_SIZE_BYTES ) ) );
    EXPECT_CALL( mock,
                 DMAEnableStream( Eq( SPI_CHANNEL_0_RX_DMA ), Eq( SPI_CHANNEL_0_RX_DMA_STREAM ) ) );
    EXPECT_CALL( mock, SPIEnableDMAReqRX( Eq( SPI_CHANNEL_0_INSTANCE ) ) );
    EXPECT_CALL( mock, SPIEnable( Eq( SPI_CHANNEL_0_INSTANCE ) ) );

    HW_SPI_Start_Channel( SPI_CHANNEL_0 );

    EXPECT_EQ( channel_0_state->rx_position, 0U );
}

TEST_F( HWSPIRxTest, StartChannel_Channel1UsesHalfAsManyDmaElementsIn16BitMode )
{
    channel_1_state->config.data_size = SPI_SIZE_16_BIT;
    channel_1_state->frame_size_bytes = 2U;
    channel_1_state->frame_shift      = 1U;

    InSequence seq;
    EXPECT_CALL(
        mock, DMADisableStream( Eq( SPI_CHANNEL_1_RX_DMA ), Eq( SPI_CHANNEL_1_RX_DMA_STREAM ) ) );
    EXPECT_CALL(
        mock, DMAIsEnabledStream( Eq( SPI_CHANNEL_1_RX_DMA ), Eq( SPI_CHANNEL_1_RX_DMA_STREAM ) ) )
        .WillOnce( Return( 0U ) );
    EXPECT_CALL( mock,
                 DMASetMemoryAddress( Eq( SPI_CHANNEL_1_RX_DMA ), Eq( SPI_CHANNEL_1_RX_DMA_STREAM ),
                                      Eq( static_cast<uint32_t>( reinterpret_cast<uintptr_t>(
                                          channel_1_state->rx_buffer ) ) ) ) );
    EXPECT_CALL( mock, SPIDMAGetRegAddr( Eq( SPI_CHANNEL_1_INSTANCE ) ) )
        .WillOnce( Return( 0xCAFEBABEU ) );
    EXPECT_CALL( mock,
                 DMASetPeriphAddress( Eq( SPI_CHANNEL_1_RX_DMA ), Eq( SPI_CHANNEL_1_RX_DMA_STREAM ),
                                      Eq( 0xCAFEBABEU ) ) );
    EXPECT_CALL( mock,
                 DMASetDataLength( Eq( SPI_CHANNEL_1_RX_DMA ), Eq( SPI_CHANNEL_1_RX_DMA_STREAM ),
                                   Eq( RX_BUFFER_SIZE_BYTES / 2U ) ) );
    EXPECT_CALL( mock,
                 DMAEnableStream( Eq( SPI_CHANNEL_1_RX_DMA ), Eq( SPI_CHANNEL_1_RX_DMA_STREAM ) ) );
    EXPECT_CALL( mock, SPIEnableDMAReqRX( Eq( SPI_CHANNEL_1_INSTANCE ) ) );
    EXPECT_CALL( mock, SPIEnable( Eq( SPI_CHANNEL_1_INSTANCE ) ) );

    HW_SPI_Start_Channel( SPI_CHANNEL_1 );
}

TEST_F( HWSPIRxTest, RxPeek_ReturnsEmptySpansWhenDmaWriteEqualsRead )
{
    channel_0_state->rx_position = 0U;
    EXPECT_CALL( mock,
                 DMAGetDataLength( Eq( SPI_CHANNEL_0_RX_DMA ), Eq( SPI_CHANNEL_0_RX_DMA_STREAM ) ) )
        .WillOnce( Return( RX_BUFFER_SIZE_BYTES ) );

    HWSPIRxSpans_T spans = HW_SPI_Rx_Peek( SPI_CHANNEL_0 );

    EXPECT_EQ( spans.first_span.data, &channel_0_state->rx_buffer[0] );
    EXPECT_EQ( spans.first_span.length_bytes, 0U );
    EXPECT_EQ( spans.second_span.data, &channel_0_state->rx_buffer[0] );
    EXPECT_EQ( spans.second_span.length_bytes, 0U );
    EXPECT_EQ( spans.total_length_bytes, 0U );
}

TEST_F( HWSPIRxTest, RxPeek_ReturnsSingleSpanWhenUnreadDataDoesNotWrap )
{
    channel_0_state->rx_position = 100U;
    EXPECT_CALL( mock,
                 DMAGetDataLength( Eq( SPI_CHANNEL_0_RX_DMA ), Eq( SPI_CHANNEL_0_RX_DMA_STREAM ) ) )
        .WillOnce( Return( 824U ) );  // write index = 200

    HWSPIRxSpans_T spans = HW_SPI_Rx_Peek( SPI_CHANNEL_0 );

    EXPECT_EQ( spans.first_span.data, &channel_0_state->rx_buffer[100] );
    EXPECT_EQ( spans.first_span.length_bytes, 100U );
    EXPECT_EQ( spans.second_span.data, &channel_0_state->rx_buffer[0] );
    EXPECT_EQ( spans.second_span.length_bytes, 0U );
    EXPECT_EQ( spans.total_length_bytes, 100U );
}

TEST_F( HWSPIRxTest, RxPeek_ReturnsTwoSpansWhenUnreadDataWraps )
{
    channel_0_state->rx_position = 1000U;
    EXPECT_CALL( mock,
                 DMAGetDataLength( Eq( SPI_CHANNEL_0_RX_DMA ), Eq( SPI_CHANNEL_0_RX_DMA_STREAM ) ) )
        .WillOnce( Return( 974U ) );  // write index = 50

    HWSPIRxSpans_T spans = HW_SPI_Rx_Peek( SPI_CHANNEL_0 );

    EXPECT_EQ( spans.first_span.data, &channel_0_state->rx_buffer[1000] );
    EXPECT_EQ( spans.first_span.length_bytes, 24U );
    EXPECT_EQ( spans.second_span.data, &channel_0_state->rx_buffer[0] );
    EXPECT_EQ( spans.second_span.length_bytes, 50U );
    EXPECT_EQ( spans.total_length_bytes, 74U );
}

TEST_F( HWSPIRxTest, RxPeek_ConvertsDmaElementsBackToBytesIn16BitMode )
{
    channel_1_state->config.data_size = SPI_SIZE_16_BIT;
    channel_1_state->frame_size_bytes = 2U;
    channel_1_state->frame_shift      = 1U;
    channel_1_state->rx_position      = 8U;

    EXPECT_CALL( mock,
                 DMAGetDataLength( Eq( SPI_CHANNEL_1_RX_DMA ), Eq( SPI_CHANNEL_1_RX_DMA_STREAM ) ) )
        .WillOnce( Return( 507U ) );  // remaining bytes = 1014, write index = 10

    HWSPIRxSpans_T spans = HW_SPI_Rx_Peek( SPI_CHANNEL_1 );

    EXPECT_EQ( spans.first_span.data, &channel_1_state->rx_buffer[8] );
    EXPECT_EQ( spans.first_span.length_bytes, 2U );
    EXPECT_EQ( spans.total_length_bytes, 2U );
}

TEST_F( HWSPIRxTest, RxPeek_TreatsNdtrReloadToZeroAsWriteIndexZero )
{
    channel_0_state->rx_position = 100U;
    EXPECT_CALL( mock,
                 DMAGetDataLength( Eq( SPI_CHANNEL_0_RX_DMA ), Eq( SPI_CHANNEL_0_RX_DMA_STREAM ) ) )
        .WillOnce( Return( 0U ) );

    HWSPIRxSpans_T spans = HW_SPI_Rx_Peek( SPI_CHANNEL_0 );

    EXPECT_EQ( spans.first_span.data, &channel_0_state->rx_buffer[100] );
    EXPECT_EQ( spans.first_span.length_bytes, RX_BUFFER_SIZE_BYTES - 100U );
    EXPECT_EQ( spans.second_span.length_bytes, 0U );
    EXPECT_EQ( spans.total_length_bytes, RX_BUFFER_SIZE_BYTES - 100U );
}

TEST_F( HWSPIRxTest, RxConsume_UsesMaskBasedWrapAtEndOfBuffer )
{
    channel_0_state->rx_position = 1000U;

    HW_SPI_Rx_Consume( SPI_CHANNEL_0, 50U );

    EXPECT_EQ( channel_0_state->rx_position, 26U );
}

TEST_F( HWSPIRxTest, RxConsume_HandlesLargePowerOfTwoDistanceWithoutDivision )
{
    channel_0_state->rx_position = 17U;

    HW_SPI_Rx_Consume( SPI_CHANNEL_0, RX_BUFFER_SIZE_BYTES * 3U + 9U );

    EXPECT_EQ( channel_0_state->rx_position, 26U );
}
