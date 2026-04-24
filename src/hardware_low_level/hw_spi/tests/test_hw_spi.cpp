/******************************************************************************
 *  File:       test_hw_spi.cpp
 *  Author:     Angus Corr
 *  Created:    21-Apr-2026
 *
 *  Description:
 *      Unit tests for the <module> module using GoogleTest and GoogleMock.
 *      This file validates the public API and behaviour defined in <module>.h.
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
#include "hw_spi_mocks.h"
#include "hw_spi.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "../hw_spi.c" /* Module under test */  // NOLINT
}

// Add additional C++ includes here if required

using ::testing::_;
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
};

static MockHWSPI* g_mock = nullptr;

// NOLINTBEGIN

extern "C" HAL_StatusTypeDef HAL_SPI_Init( SPI_HandleTypeDef* hspi )
{
    if ( !g_mock )
    {
        return HAL_ERROR;
    }
    return g_mock->SPIInit( hspi );
}

extern "C" HAL_StatusTypeDef HAL_SPI_Receive_DMA( SPI_HandleTypeDef* hspi, uint8_t* pData,
                                                  uint16_t Size )
{
    if ( !g_mock )
    {
        return HAL_ERROR;
    }
    return g_mock->SPIReceiveDMA( hspi, pData, Size );
}

extern "C" HAL_StatusTypeDef HAL_SPI_DMAStop( SPI_HandleTypeDef* hspi )
{
    if ( !g_mock )
    {
        return HAL_ERROR;
    }
    return g_mock->SPIDMAStop( hspi );
}

extern "C" uint32_t LL_DMA_GetDataLength( void* DMAx, uint32_t Stream )
{
    if ( !g_mock )
    {
        return 0U;
    }
    return g_mock->DMAGetDataLength( DMAx, Stream );
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
    if ( !g_mock )
    {
        return 0U;
    }
    return g_mock->DMAIsEnabledStream( DMAx, Stream );
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
    if ( !g_mock )
    {
        return 0U;
    }
    return g_mock->SPIDMAGetRegAddr( SPIx );
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

extern "C" uint32_t LL_DMA_IsActiveFlag_TE5( DMA_TypeDef* DMAx )
{
    if ( !g_mock )
    {
        return 0U;
    }
    return g_mock->DMAIsActiveFlagTE5( DMAx );
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
    if ( !g_mock )
    {
        return 0U;
    }
    return g_mock->DMAIsActiveFlagTC5( DMAx );
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
    if ( !g_mock )
    {
        return 0U;
    }
    return g_mock->DMAIsActiveFlagTE1( DMAx );
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
    if ( !g_mock )
    {
        return 0U;
    }
    return g_mock->DMAIsActiveFlagTC1( DMAx );
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

// NOLINTEND

/**-----------------------------------------------------------------------------
 *  Test Fixture
 *------------------------------------------------------------------------------
 */

/**
 * @brief Test fixture for module tests.
 *
 * Provides a consistent setup/teardown environment for all test cases.
 */
class HWSPITest : public ::testing::Test
{
protected:
    StrictMock<MockHWSPI> mock;

    static HWSPIConfig_T MakeValidConfig8BitMaster( void )
    {
        return HWSPIConfig_T{ .spi_mode  = SPI_MASTER_MODE,
                              .data_size = SPI_SIZE_8_BIT,
                              .first_bit = SPI_FIRST_MSB,
                              .baud_rate = SPI_BAUD_45MBIT,
                              .cpol      = SPI_CPOL_LOW,
                              .cpha      = SPI_CPHA_1_EDGE };
    }

    static HWSPIConfig_T MakeValidConfig16BitSlave( void )
    {
        return HWSPIConfig_T{ .spi_mode  = SPI_SLAVE_MODE,
                              .data_size = SPI_SIZE_16_BIT,
                              .first_bit = SPI_FIRST_LSB,
                              .baud_rate = SPI_BAUD_352KBIT,
                              .cpol      = SPI_CPOL_HIGH,
                              .cpha      = SPI_CPHA_2_EDGE };
    }

    void SetUp( void ) override
    {
        g_mock = &mock;

        memset( &SPI_CHANNEL_0_HANDLE, 0, sizeof( SPI_CHANNEL_0_HANDLE ) );
        memset( &SPI_CHANNEL_1_HANDLE, 0, sizeof( SPI_CHANNEL_1_HANDLE ) );
        memset( &channel_0_state_struct, 0, sizeof( channel_0_state_struct ) );
        memset( &channel_1_state_struct, 0, sizeof( channel_1_state_struct ) );
        memset( &dac_state_struct, 0, sizeof( dac_state_struct ) );

        channel_0_state = &channel_0_state_struct;
        channel_1_state = &channel_1_state_struct;
        dac_state       = &dac_state_struct;

        channel_0_state->config = MakeValidConfig8BitMaster();
        channel_1_state->config = MakeValidConfig8BitMaster();
        dac_state->config       = MakeValidConfig8BitMaster();

        channel_0_state->rx_dma         = SPI_CHANNEL_0_RX_DMA;
        channel_0_state->rx_dma_stream  = SPI_CHANNEL_0_RX_DMA_STREAM;
        channel_0_state->tx_dma         = SPI_CHANNEL_0_TX_DMA;
        channel_0_state->tx_dma_stream  = SPI_CHANNEL_0_TX_DMA_STREAM;
        channel_0_state->spi_peripheral = SPI_CHANNEL_0_INSTANCE;
        channel_0_state->tx_dma_irqn    = SPI_CHANNEL_0_TX_DMA_IRQN;

        channel_1_state->rx_dma         = SPI_CHANNEL_1_RX_DMA;
        channel_1_state->rx_dma_stream  = SPI_CHANNEL_1_RX_DMA_STREAM;
        channel_1_state->tx_dma         = SPI_CHANNEL_1_TX_DMA;
        channel_1_state->tx_dma_stream  = SPI_CHANNEL_1_TX_DMA_STREAM;
        channel_1_state->spi_peripheral = SPI_CHANNEL_1_INSTANCE;
        channel_1_state->tx_dma_irqn    = SPI_CHANNEL_1_TX_DMA_IRQN;

        dac_state->tx_dma         = SPI_DAC_TX_DMA;
        dac_state->tx_dma_stream  = SPI_DAC_TX_DMA_STREAM;
        dac_state->spi_peripheral = SPI_DAC_INSTANCE;
        dac_state->tx_dma_irqn    = SPI_DAC_TX_DMA_IRQN;
    }

    void TearDown( void ) override
    {
        g_mock = nullptr;
    }
};

/**-----------------------------------------------------------------------------
 *  Test Cases
 *------------------------------------------------------------------------------
 */
TEST_F( HWSPITest, ConfigureChannel_ReturnsFalseForInvalidPeripheral )
{
    EXPECT_FALSE( HW_SPI_Configure_Channel( static_cast<SPIPeripheral_T>( 999 ),
                                            MakeValidConfig8BitMaster() ) );
}

TEST_F( HWSPITest, ConfigureChannel_ReturnsFalseForInvalidMode )
{
    HWSPIConfig_T config = MakeValidConfig8BitMaster();
    config.spi_mode      = static_cast<SPIMode_T>( 999 );

    EXPECT_FALSE( HW_SPI_Configure_Channel( SPI_CHANNEL_0, config ) );
}

TEST_F( HWSPITest, ConfigureChannel_ReturnsFalseForInvalidDataSize )
{
    HWSPIConfig_T config = MakeValidConfig8BitMaster();
    config.data_size     = static_cast<SPIDataSize_T>( 999 );

    EXPECT_FALSE( HW_SPI_Configure_Channel( SPI_CHANNEL_0, config ) );
}

TEST_F( HWSPITest, ConfigureChannel_ReturnsFalseForInvalidCPOL )
{
    HWSPIConfig_T config = MakeValidConfig8BitMaster();
    config.cpol          = static_cast<SPICPOL_T>( 999 );

    EXPECT_FALSE( HW_SPI_Configure_Channel( SPI_CHANNEL_0, config ) );
}

TEST_F( HWSPITest, ConfigureChannel_ReturnsFalseForInvalidCPHA )
{
    HWSPIConfig_T config = MakeValidConfig8BitMaster();
    config.cpha          = static_cast<SPICPHA_T>( 999 );

    EXPECT_FALSE( HW_SPI_Configure_Channel( SPI_CHANNEL_0, config ) );
}

TEST_F( HWSPITest, ConfigureChannel_ReturnsFalseForInvalidBaudRate )
{
    HWSPIConfig_T config = MakeValidConfig8BitMaster();
    config.baud_rate     = static_cast<SPIBaudRate_T>( 999 );

    EXPECT_FALSE( HW_SPI_Configure_Channel( SPI_CHANNEL_0, config ) );
}

TEST_F( HWSPITest, ConfigureChannel_ReturnsFalseForInvalidFirstBit )
{
    HWSPIConfig_T config = MakeValidConfig8BitMaster();
    config.first_bit     = static_cast<SPIFirstBit_T>( 999 );

    EXPECT_FALSE( HW_SPI_Configure_Channel( SPI_CHANNEL_0, config ) );
}

TEST_F( HWSPITest, ConfigureChannel_ReturnsFalseWhenHALInitFails )
{
    EXPECT_CALL( mock, SPIInit( Eq( &SPI_CHANNEL_0_HANDLE ) ) ).WillOnce( Return( HAL_ERROR ) );

    EXPECT_FALSE( HW_SPI_Configure_Channel( SPI_CHANNEL_0, MakeValidConfig8BitMaster() ) );
}

TEST_F( HWSPITest, ConfigureChannel_ConfiguresChannel0Master8BitAndByteDMAWidths )
{
    const HWSPIConfig_T config = MakeValidConfig8BitMaster();

    EXPECT_CALL( mock, SPIInit( Eq( &SPI_CHANNEL_0_HANDLE ) ) )
        .WillOnce( Invoke( [&]( SPI_HandleTypeDef* hspi ) {
            EXPECT_EQ( hspi->Instance, SPI_CHANNEL_0_INSTANCE );
            EXPECT_EQ( hspi->Init.Mode, SPI_MODE_MASTER );
            EXPECT_EQ( hspi->Init.NSS, SPI_NSS_HARD_OUTPUT );
            EXPECT_EQ( hspi->Init.Direction, SPI_DIRECTION_2LINES );
            EXPECT_EQ( hspi->Init.DataSize, SPI_DATASIZE_8BIT );
            EXPECT_EQ( hspi->Init.CLKPolarity, SPI_POLARITY_LOW );
            EXPECT_EQ( hspi->Init.CLKPhase, SPI_PHASE_1EDGE );
            EXPECT_EQ( hspi->Init.BaudRatePrescaler, SPI_BAUDRATEPRESCALER_2 );
            EXPECT_EQ( hspi->Init.FirstBit, SPI_FIRSTBIT_MSB );
            EXPECT_EQ( hspi->Init.TIMode, SPI_TIMODE_DISABLE );
            EXPECT_EQ( hspi->Init.CRCCalculation, SPI_CRCCALCULATION_DISABLE );
            EXPECT_EQ( hspi->Init.CRCPolynomial, 10U );
            return HAL_OK;
        } ) );

    EXPECT_CALL( mock,
                 DMASetMemorySize( Eq( SPI_CHANNEL_0_RX_DMA ), Eq( SPI_CHANNEL_0_RX_DMA_STREAM ),
                                   Eq( LL_DMA_MDATAALIGN_BYTE ) ) );
    EXPECT_CALL( mock,
                 DMASetPeriphSize( Eq( SPI_CHANNEL_0_RX_DMA ), Eq( SPI_CHANNEL_0_RX_DMA_STREAM ),
                                   Eq( LL_DMA_PDATAALIGN_BYTE ) ) );
    EXPECT_CALL( mock,
                 DMASetMemorySize( Eq( SPI_CHANNEL_0_TX_DMA ), Eq( SPI_CHANNEL_0_TX_DMA_STREAM ),
                                   Eq( LL_DMA_MDATAALIGN_BYTE ) ) );
    EXPECT_CALL( mock,
                 DMASetPeriphSize( Eq( SPI_CHANNEL_0_TX_DMA ), Eq( SPI_CHANNEL_0_TX_DMA_STREAM ),
                                   Eq( LL_DMA_PDATAALIGN_BYTE ) ) );

    EXPECT_TRUE( HW_SPI_Configure_Channel( SPI_CHANNEL_0, config ) );
    EXPECT_EQ( channel_0_state->config.spi_mode, SPI_MASTER_MODE );
    EXPECT_EQ( channel_0_state->config.data_size, SPI_SIZE_8_BIT );
    EXPECT_EQ( channel_0_state->tx_dma_irqn, SPI_CHANNEL_0_TX_DMA_IRQN );
}

TEST_F( HWSPITest, ConfigureChannel_ConfiguresChannel1Slave16BitAndHalfwordDMAWidths )
{
    const HWSPIConfig_T config = MakeValidConfig16BitSlave();

    EXPECT_CALL( mock, SPIInit( Eq( &SPI_CHANNEL_1_HANDLE ) ) )
        .WillOnce( Invoke( [&]( SPI_HandleTypeDef* hspi ) {
            EXPECT_EQ( hspi->Instance, SPI_CHANNEL_1_INSTANCE );
            EXPECT_EQ( hspi->Init.Mode, SPI_MODE_SLAVE );
            EXPECT_EQ( hspi->Init.NSS, SPI_NSS_HARD_INPUT );
            EXPECT_EQ( hspi->Init.Direction, SPI_DIRECTION_2LINES );
            EXPECT_EQ( hspi->Init.DataSize, SPI_DATASIZE_16BIT );
            EXPECT_EQ( hspi->Init.CLKPolarity, SPI_POLARITY_HIGH );
            EXPECT_EQ( hspi->Init.CLKPhase, SPI_PHASE_2EDGE );
            EXPECT_EQ( hspi->Init.BaudRatePrescaler, SPI_BAUDRATEPRESCALER_256 );
            EXPECT_EQ( hspi->Init.FirstBit, SPI_FIRSTBIT_LSB );
            return HAL_OK;
        } ) );

    EXPECT_CALL( mock,
                 DMASetMemorySize( Eq( SPI_CHANNEL_1_RX_DMA ), Eq( SPI_CHANNEL_1_RX_DMA_STREAM ),
                                   Eq( LL_DMA_MDATAALIGN_HALFWORD ) ) );
    EXPECT_CALL( mock,
                 DMASetPeriphSize( Eq( SPI_CHANNEL_1_RX_DMA ), Eq( SPI_CHANNEL_1_RX_DMA_STREAM ),
                                   Eq( LL_DMA_PDATAALIGN_HALFWORD ) ) );
    EXPECT_CALL( mock,
                 DMASetMemorySize( Eq( SPI_CHANNEL_1_TX_DMA ), Eq( SPI_CHANNEL_1_TX_DMA_STREAM ),
                                   Eq( LL_DMA_MDATAALIGN_HALFWORD ) ) );
    EXPECT_CALL( mock,
                 DMASetPeriphSize( Eq( SPI_CHANNEL_1_TX_DMA ), Eq( SPI_CHANNEL_1_TX_DMA_STREAM ),
                                   Eq( LL_DMA_PDATAALIGN_HALFWORD ) ) );

    EXPECT_TRUE( HW_SPI_Configure_Channel( SPI_CHANNEL_1, config ) );
    EXPECT_EQ( channel_1_state->config.data_size, SPI_SIZE_16_BIT );
    EXPECT_EQ( channel_1_state->tx_dma_irqn, SPI_CHANNEL_1_TX_DMA_IRQN );
}

TEST_F( HWSPITest, ConfigureChannel_ConfiguresDACAndOnlyTouchesTxDMAWidth )
{
    const HWSPIConfig_T config = MakeValidConfig8BitMaster();

    EXPECT_CALL( mock, SPIInit( Eq( &SPI_DAC_HANDLE ) ) ).WillOnce( Return( HAL_OK ) );
    EXPECT_CALL( mock, DMASetMemorySize( Eq( SPI_DAC_TX_DMA ), Eq( SPI_DAC_TX_DMA_STREAM ),
                                         Eq( LL_DMA_MDATAALIGN_BYTE ) ) );
    EXPECT_CALL( mock, DMASetPeriphSize( Eq( SPI_DAC_TX_DMA ), Eq( SPI_DAC_TX_DMA_STREAM ),
                                         Eq( LL_DMA_PDATAALIGN_BYTE ) ) );

    EXPECT_TRUE( HW_SPI_Configure_Channel( SPI_DAC, config ) );
    EXPECT_EQ( dac_state->tx_dma_irqn, SPI_DAC_TX_DMA_IRQN );
    EXPECT_EQ( dac_state->rx_dma, nullptr );
}

TEST_F( HWSPITest, StartChannel_Channel0StartsReceiveDMAWith1024ElementsIn8BitMode )
{
    channel_0_state->config.data_size = SPI_SIZE_8_BIT;

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

TEST_F( HWSPITest, StartChannel_Channel1StartsReceiveDMAWith512ElementsIn16BitMode )
{
    channel_1_state->config.data_size = SPI_SIZE_16_BIT;

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
    EXPECT_EQ( channel_1_state->rx_position, 0U );
}

TEST_F( HWSPITest, StartChannel_DACDoesNothing )
{
    HW_SPI_Start_Channel( SPI_DAC );
}

TEST_F( HWSPITest, StopChannel_Channel0StopsDMA )
{
    EXPECT_CALL( mock, SPIDMAStop( Eq( &SPI_CHANNEL_0_HANDLE ) ) ).WillOnce( Return( HAL_OK ) );
    HW_SPI_Stop_Channel( SPI_CHANNEL_0 );
}

TEST_F( HWSPITest, StopChannel_Channel1StopsDMA )
{
    EXPECT_CALL( mock, SPIDMAStop( Eq( &SPI_CHANNEL_1_HANDLE ) ) ).WillOnce( Return( HAL_OK ) );
    HW_SPI_Stop_Channel( SPI_CHANNEL_1 );
}

TEST_F( HWSPITest, StopChannel_DACReturnsWithoutStopping )
{
    HW_SPI_Stop_Channel( SPI_DAC );
}

TEST_F( HWSPITest, RxPeek_ReturnsEmptyForInvalidPeripheral )
{
    HWSPIRxSpans_T spans = HW_SPI_Rx_Peek( static_cast<SPIPeripheral_T>( 999 ) );

    EXPECT_EQ( spans.first_span.data, nullptr );
    EXPECT_EQ( spans.first_span.length_bytes, 0U );
    EXPECT_EQ( spans.second_span.data, nullptr );
    EXPECT_EQ( spans.second_span.length_bytes, 0U );
    EXPECT_EQ( spans.total_length_bytes, 0U );
}

TEST_F( HWSPITest, RxPeek_ReturnsEmptyWhenNoUnreadDataExists )
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

TEST_F( HWSPITest, RxPeek_ReturnsSingleContiguousSpanWhenUnreadDoesNotWrap )
{
    channel_0_state->rx_position = 100U;
    EXPECT_CALL( mock,
                 DMAGetDataLength( Eq( SPI_CHANNEL_0_RX_DMA ), Eq( SPI_CHANNEL_0_RX_DMA_STREAM ) ) )
        .WillOnce( Return( 824U ) );

    HWSPIRxSpans_T spans = HW_SPI_Rx_Peek( SPI_CHANNEL_0 );

    EXPECT_EQ( spans.first_span.data, &channel_0_state->rx_buffer[100] );
    EXPECT_EQ( spans.first_span.length_bytes, 100U );
    EXPECT_EQ( spans.second_span.data, &channel_0_state->rx_buffer[0] );
    EXPECT_EQ( spans.second_span.length_bytes, 0U );
    EXPECT_EQ( spans.total_length_bytes, 100U );
}

TEST_F( HWSPITest, RxPeek_ReturnsWrappedSpansWhenUnreadWrapsAroundBufferEnd )
{
    channel_0_state->rx_position = 1000U;
    EXPECT_CALL( mock,
                 DMAGetDataLength( Eq( SPI_CHANNEL_0_RX_DMA ), Eq( SPI_CHANNEL_0_RX_DMA_STREAM ) ) )
        .WillOnce( Return( 974U ) );

    HWSPIRxSpans_T spans = HW_SPI_Rx_Peek( SPI_CHANNEL_0 );

    EXPECT_EQ( spans.first_span.data, &channel_0_state->rx_buffer[1000] );
    EXPECT_EQ( spans.first_span.length_bytes, 24U );
    EXPECT_EQ( spans.second_span.data, &channel_0_state->rx_buffer[0] );
    EXPECT_EQ( spans.second_span.length_bytes, 50U );
    EXPECT_EQ( spans.total_length_bytes, 74U );
}

TEST_F( HWSPITest, RxPeek_UsesByteLengthsEvenWhenConfiguredFor16BitMode )
{
    channel_1_state->config.data_size = SPI_SIZE_16_BIT;
    channel_1_state->rx_position      = 8U;

    EXPECT_CALL( mock,
                 DMAGetDataLength( Eq( SPI_CHANNEL_1_RX_DMA ), Eq( SPI_CHANNEL_1_RX_DMA_STREAM ) ) )
        .WillOnce( Return( 507U ) );

    HWSPIRxSpans_T spans = HW_SPI_Rx_Peek( SPI_CHANNEL_1 );

    EXPECT_EQ( spans.first_span.data, &channel_1_state->rx_buffer[8] );
    EXPECT_EQ( spans.first_span.length_bytes, 2U );
    EXPECT_EQ( spans.total_length_bytes, 2U );
}

TEST_F( HWSPITest, RxConsume_AdvancesReadPositionIn8BitMode )
{
    channel_0_state->rx_position = 10U;
    HW_SPI_Rx_Consume( SPI_CHANNEL_0, 25U );
    EXPECT_EQ( channel_0_state->rx_position, 35U );
}

TEST_F( HWSPITest, RxConsume_WrapsReadPositionAtEndOfBuffer )
{
    channel_0_state->rx_position = 1000U;
    HW_SPI_Rx_Consume( SPI_CHANNEL_0, 50U );
    EXPECT_EQ( channel_0_state->rx_position, 26U );
}

TEST_F( HWSPITest, RxConsume_DoesNothingForInvalidPeripheral )
{
    channel_0_state->rx_position = 77U;
    HW_SPI_Rx_Consume( static_cast<SPIPeripheral_T>( 999 ), 10U );
    EXPECT_EQ( channel_0_state->rx_position, 77U );
}

TEST_F( HWSPITest, RxConsume_RejectsMisalignedByteCountIn16BitMode )
{
    channel_1_state->config.data_size = SPI_SIZE_16_BIT;
    channel_1_state->rx_position      = 40U;

    HW_SPI_Rx_Consume( SPI_CHANNEL_1, 3U );

    EXPECT_EQ( channel_1_state->rx_position, 40U );
}

TEST_F( HWSPITest, RxConsume_AcceptsAlignedByteCountIn16BitMode )
{
    channel_1_state->config.data_size = SPI_SIZE_16_BIT;
    channel_1_state->rx_position      = 40U;

    HW_SPI_Rx_Consume( SPI_CHANNEL_1, 4U );

    EXPECT_EQ( channel_1_state->rx_position, 44U );
}

TEST_F( HWSPITest, LoadTxBuffer_CopiesDataAndAdvancesWritePosition )
{
    const uint8_t data[4] = { 0x11U, 0x22U, 0x33U, 0x44U };

    EXPECT_CALL( mock, NVICDisableIRQ( SPI_CHANNEL_0_TX_DMA_IRQN ) );
    EXPECT_CALL( mock, NVICEnableIRQ( SPI_CHANNEL_0_TX_DMA_IRQN ) );

    EXPECT_TRUE( HW_SPI_Load_Tx_Buffer( SPI_CHANNEL_0, data, sizeof( data ) ) );
    EXPECT_EQ( channel_0_state->tx_write_position, 4U );
    EXPECT_EQ( channel_0_state->tx_num_bytes_pending, 4U );
    EXPECT_EQ( channel_0_state->tx_buffer[0], 0x11U );
    EXPECT_EQ( channel_0_state->tx_buffer[1], 0x22U );
    EXPECT_EQ( channel_0_state->tx_buffer[2], 0x33U );
    EXPECT_EQ( channel_0_state->tx_buffer[3], 0x44U );
}

TEST_F( HWSPITest, LoadTxBuffer_AppendsToExistingQueueContents )
{
    const uint8_t first[2]  = { 0xAAU, 0xBBU };
    const uint8_t second[3] = { 0x01U, 0x02U, 0x03U };

    EXPECT_CALL( mock, NVICDisableIRQ( SPI_CHANNEL_0_TX_DMA_IRQN ) ).Times( 2 );
    EXPECT_CALL( mock, NVICEnableIRQ( SPI_CHANNEL_0_TX_DMA_IRQN ) ).Times( 2 );

    EXPECT_TRUE( HW_SPI_Load_Tx_Buffer( SPI_CHANNEL_0, first, sizeof( first ) ) );
    EXPECT_TRUE( HW_SPI_Load_Tx_Buffer( SPI_CHANNEL_0, second, sizeof( second ) ) );

    EXPECT_EQ( channel_0_state->tx_write_position, 5U );
    EXPECT_EQ( channel_0_state->tx_num_bytes_pending, 5U );
    EXPECT_EQ( channel_0_state->tx_buffer[0], 0xAAU );
    EXPECT_EQ( channel_0_state->tx_buffer[1], 0xBBU );
    EXPECT_EQ( channel_0_state->tx_buffer[2], 0x01U );
    EXPECT_EQ( channel_0_state->tx_buffer[3], 0x02U );
    EXPECT_EQ( channel_0_state->tx_buffer[4], 0x03U );
}

TEST_F( HWSPITest, LoadTxBuffer_ReturnsFalseWhenQueueWouldOverflow )
{
    const uint8_t data[8] = { 0 };

    channel_0_state->tx_write_position            = 100U;
    channel_0_state->tx_num_bytes_pending         = TX_BUFFER_SIZE_BYTES - 4U;
    channel_0_state->tx_num_bytes_in_transmission = 0U;

    EXPECT_CALL( mock, NVICDisableIRQ( SPI_CHANNEL_0_TX_DMA_IRQN ) );
    EXPECT_CALL( mock, NVICEnableIRQ( SPI_CHANNEL_0_TX_DMA_IRQN ) );

    EXPECT_FALSE( HW_SPI_Load_Tx_Buffer( SPI_CHANNEL_0, data, sizeof( data ) ) );
    EXPECT_EQ( channel_0_state->tx_write_position, 100U );
    EXPECT_EQ( channel_0_state->tx_num_bytes_pending, TX_BUFFER_SIZE_BYTES - 4U );
}

TEST_F( HWSPITest, LoadTxBuffer_WrapsWriteAtEndOfCircularBuffer )
{
    const uint8_t data[4] = { 0x10U, 0x20U, 0x30U, 0x40U };

    channel_0_state->tx_write_position    = TX_BUFFER_SIZE_BYTES - 2U;
    channel_0_state->tx_num_bytes_pending = 2U;

    EXPECT_CALL( mock, NVICDisableIRQ( SPI_CHANNEL_0_TX_DMA_IRQN ) );
    EXPECT_CALL( mock, NVICEnableIRQ( SPI_CHANNEL_0_TX_DMA_IRQN ) );

    EXPECT_TRUE( HW_SPI_Load_Tx_Buffer( SPI_CHANNEL_0, data, sizeof( data ) ) );

    EXPECT_EQ( channel_0_state->tx_buffer[TX_BUFFER_SIZE_BYTES - 2U], 0x10U );
    EXPECT_EQ( channel_0_state->tx_buffer[TX_BUFFER_SIZE_BYTES - 1U], 0x20U );
    EXPECT_EQ( channel_0_state->tx_buffer[0], 0x30U );
    EXPECT_EQ( channel_0_state->tx_buffer[1], 0x40U );
    EXPECT_EQ( channel_0_state->tx_write_position, 2U );
    EXPECT_EQ( channel_0_state->tx_num_bytes_pending, 6U );
}

TEST_F( HWSPITest, LoadTxBuffer_FreeSpaceIncludesBytesCurrentlyOwnedByDMA )
{
    const uint8_t data[3] = { 0x01U, 0x02U, 0x03U };

    channel_0_state->tx_write_position            = 50U;
    channel_0_state->tx_num_bytes_pending         = TX_BUFFER_SIZE_BYTES - 5U;
    channel_0_state->tx_num_bytes_in_transmission = 4U;

    EXPECT_CALL( mock, NVICDisableIRQ( SPI_CHANNEL_0_TX_DMA_IRQN ) );
    EXPECT_CALL( mock, NVICEnableIRQ( SPI_CHANNEL_0_TX_DMA_IRQN ) );

    EXPECT_FALSE( HW_SPI_Load_Tx_Buffer( SPI_CHANNEL_0, data, sizeof( data ) ) );
    EXPECT_EQ( channel_0_state->tx_write_position, 50U );
    EXPECT_EQ( channel_0_state->tx_num_bytes_pending, TX_BUFFER_SIZE_BYTES - 5U );
}

TEST_F( HWSPITest, LoadTxBuffer_RejectsMisalignedSizeIn16BitMode )
{
    channel_1_state->config.data_size = SPI_SIZE_16_BIT;
    const uint8_t data[3]             = { 1U, 2U, 3U };

    EXPECT_FALSE( HW_SPI_Load_Tx_Buffer( SPI_CHANNEL_1, data, sizeof( data ) ) );
    EXPECT_EQ( channel_1_state->tx_write_position, 0U );
}

TEST_F( HWSPITest, LoadTxBuffer_AcceptsAlignedSizeIn16BitMode )
{
    channel_1_state->config.data_size = SPI_SIZE_16_BIT;
    const uint8_t data[4]             = { 1U, 2U, 3U, 4U };

    EXPECT_CALL( mock, NVICDisableIRQ( SPI_CHANNEL_1_TX_DMA_IRQN ) );
    EXPECT_CALL( mock, NVICEnableIRQ( SPI_CHANNEL_1_TX_DMA_IRQN ) );

    EXPECT_TRUE( HW_SPI_Load_Tx_Buffer( SPI_CHANNEL_1, data, sizeof( data ) ) );
    EXPECT_EQ( channel_1_state->tx_write_position, 4U );
    EXPECT_EQ( channel_1_state->tx_num_bytes_pending, 4U );
}

TEST_F( HWSPITest, TxTrigger_DoesNothingWhenQueueIsEmpty )
{
    channel_0_state->tx_write_position            = 0U;
    channel_0_state->tx_num_bytes_pending         = 0U;
    channel_0_state->tx_num_bytes_in_transmission = 0U;

    EXPECT_CALL( mock, NVICDisableIRQ( SPI_CHANNEL_0_TX_DMA_IRQN ) );
    EXPECT_CALL( mock, NVICEnableIRQ( SPI_CHANNEL_0_TX_DMA_IRQN ) );

    HW_SPI_Tx_Trigger( SPI_CHANNEL_0 );
}

TEST_F( HWSPITest, TxTrigger_DoesNothingWhenTransferIsAlreadyInProgress )
{
    channel_0_state->tx_write_position            = 12U;
    channel_0_state->tx_num_bytes_pending         = 8U;
    channel_0_state->tx_num_bytes_in_transmission = 4U;

    EXPECT_CALL( mock, NVICDisableIRQ( SPI_CHANNEL_0_TX_DMA_IRQN ) );
    EXPECT_CALL( mock, NVICEnableIRQ( SPI_CHANNEL_0_TX_DMA_IRQN ) );

    HW_SPI_Tx_Trigger( SPI_CHANNEL_0 );
}

TEST_F( HWSPITest, TxTrigger_Channel0StartsDmaTransferIn8BitMode )
{
    channel_0_state->tx_write_position    = 5U;
    channel_0_state->tx_read_position     = 0U;
    channel_0_state->tx_num_bytes_pending = 5U;
    memset( channel_0_state->tx_buffer, 0xA5, 5U );

    InSequence seq;
    EXPECT_CALL( mock, NVICDisableIRQ( SPI_CHANNEL_0_TX_DMA_IRQN ) );
    EXPECT_CALL( mock, DMAClearFlagTC5( Eq( SPI_CHANNEL_0_TX_DMA ) ) );
    EXPECT_CALL( mock, DMAClearFlagTE5( Eq( SPI_CHANNEL_0_TX_DMA ) ) );
    EXPECT_CALL(
        mock, DMADisableStream( Eq( SPI_CHANNEL_0_TX_DMA ), Eq( SPI_CHANNEL_0_TX_DMA_STREAM ) ) );
    EXPECT_CALL(
        mock, DMAIsEnabledStream( Eq( SPI_CHANNEL_0_TX_DMA ), Eq( SPI_CHANNEL_0_TX_DMA_STREAM ) ) )
        .WillOnce( Return( 0U ) );
    EXPECT_CALL( mock,
                 DMASetMemoryAddress( Eq( SPI_CHANNEL_0_TX_DMA ), Eq( SPI_CHANNEL_0_TX_DMA_STREAM ),
                                      Eq( static_cast<uint32_t>( reinterpret_cast<uintptr_t>(
                                          &channel_0_state->tx_buffer[0] ) ) ) ) );
    EXPECT_CALL( mock, SPIDMAGetRegAddr( Eq( SPI_CHANNEL_0_INSTANCE ) ) )
        .WillOnce( Return( 0x12345678U ) );
    EXPECT_CALL( mock,
                 DMASetPeriphAddress( Eq( SPI_CHANNEL_0_TX_DMA ), Eq( SPI_CHANNEL_0_TX_DMA_STREAM ),
                                      Eq( 0x12345678U ) ) );
    EXPECT_CALL( mock, DMASetDataLength( Eq( SPI_CHANNEL_0_TX_DMA ),
                                         Eq( SPI_CHANNEL_0_TX_DMA_STREAM ), Eq( 5U ) ) );
    EXPECT_CALL( mock, SPIEnableDMAReqTX( Eq( SPI_CHANNEL_0_INSTANCE ) ) );
    EXPECT_CALL( mock,
                 DMAEnableITTC( Eq( SPI_CHANNEL_0_TX_DMA ), Eq( SPI_CHANNEL_0_TX_DMA_STREAM ) ) );
    EXPECT_CALL( mock,
                 DMAEnableITTE( Eq( SPI_CHANNEL_0_TX_DMA ), Eq( SPI_CHANNEL_0_TX_DMA_STREAM ) ) );
    EXPECT_CALL( mock,
                 DMAEnableStream( Eq( SPI_CHANNEL_0_TX_DMA ), Eq( SPI_CHANNEL_0_TX_DMA_STREAM ) ) );
    EXPECT_CALL( mock, NVICEnableIRQ( SPI_CHANNEL_0_TX_DMA_IRQN ) );

    HW_SPI_Tx_Trigger( SPI_CHANNEL_0 );

    EXPECT_EQ( channel_0_state->tx_read_position, 5U );
    EXPECT_EQ( channel_0_state->tx_num_bytes_pending, 0U );
    EXPECT_EQ( channel_0_state->tx_num_bytes_in_transmission, 5U );
}

TEST_F( HWSPITest, TxTrigger_StartsOnlyContiguousTailWhenRingReadWraps )
{
    channel_0_state->tx_read_position     = TX_BUFFER_SIZE_BYTES - 3U;
    channel_0_state->tx_write_position    = 2U;
    channel_0_state->tx_num_bytes_pending = 5U;

    InSequence seq;
    EXPECT_CALL( mock, NVICDisableIRQ( SPI_CHANNEL_0_TX_DMA_IRQN ) );
    EXPECT_CALL( mock, DMAClearFlagTC5( Eq( SPI_CHANNEL_0_TX_DMA ) ) );
    EXPECT_CALL( mock, DMAClearFlagTE5( Eq( SPI_CHANNEL_0_TX_DMA ) ) );
    EXPECT_CALL(
        mock, DMADisableStream( Eq( SPI_CHANNEL_0_TX_DMA ), Eq( SPI_CHANNEL_0_TX_DMA_STREAM ) ) );
    EXPECT_CALL(
        mock, DMAIsEnabledStream( Eq( SPI_CHANNEL_0_TX_DMA ), Eq( SPI_CHANNEL_0_TX_DMA_STREAM ) ) )
        .WillOnce( Return( 0U ) );
    EXPECT_CALL( mock, DMASetMemoryAddress(
                           Eq( SPI_CHANNEL_0_TX_DMA ), Eq( SPI_CHANNEL_0_TX_DMA_STREAM ),
                           Eq( static_cast<uint32_t>( reinterpret_cast<uintptr_t>(
                               &channel_0_state->tx_buffer[TX_BUFFER_SIZE_BYTES - 3U] ) ) ) ) );
    EXPECT_CALL( mock, SPIDMAGetRegAddr( Eq( SPI_CHANNEL_0_INSTANCE ) ) )
        .WillOnce( Return( 0x11112222U ) );
    EXPECT_CALL( mock,
                 DMASetPeriphAddress( Eq( SPI_CHANNEL_0_TX_DMA ), Eq( SPI_CHANNEL_0_TX_DMA_STREAM ),
                                      Eq( 0x11112222U ) ) );
    EXPECT_CALL( mock, DMASetDataLength( Eq( SPI_CHANNEL_0_TX_DMA ),
                                         Eq( SPI_CHANNEL_0_TX_DMA_STREAM ), Eq( 3U ) ) );
    EXPECT_CALL( mock, SPIEnableDMAReqTX( Eq( SPI_CHANNEL_0_INSTANCE ) ) );
    EXPECT_CALL( mock,
                 DMAEnableITTC( Eq( SPI_CHANNEL_0_TX_DMA ), Eq( SPI_CHANNEL_0_TX_DMA_STREAM ) ) );
    EXPECT_CALL( mock,
                 DMAEnableITTE( Eq( SPI_CHANNEL_0_TX_DMA ), Eq( SPI_CHANNEL_0_TX_DMA_STREAM ) ) );
    EXPECT_CALL( mock,
                 DMAEnableStream( Eq( SPI_CHANNEL_0_TX_DMA ), Eq( SPI_CHANNEL_0_TX_DMA_STREAM ) ) );
    EXPECT_CALL( mock, NVICEnableIRQ( SPI_CHANNEL_0_TX_DMA_IRQN ) );

    HW_SPI_Tx_Trigger( SPI_CHANNEL_0 );

    EXPECT_EQ( channel_0_state->tx_read_position, 0U );
    EXPECT_EQ( channel_0_state->tx_num_bytes_pending, 2U );
    EXPECT_EQ( channel_0_state->tx_num_bytes_in_transmission, 3U );
}

TEST_F( HWSPITest, TxTrigger_Channel1StartsDmaTransferIn16BitModeUsingElementCount )
{
    channel_1_state->config.data_size     = SPI_SIZE_16_BIT;
    channel_1_state->tx_write_position    = 6U;
    channel_1_state->tx_read_position     = 0U;
    channel_1_state->tx_num_bytes_pending = 6U;

    EXPECT_CALL( mock, NVICDisableIRQ( SPI_CHANNEL_1_TX_DMA_IRQN ) );
    EXPECT_CALL( mock, DMAClearFlagTC1( Eq( SPI_CHANNEL_1_TX_DMA ) ) );
    EXPECT_CALL( mock, DMAClearFlagTE1( Eq( SPI_CHANNEL_1_TX_DMA ) ) );
    EXPECT_CALL(
        mock, DMADisableStream( Eq( SPI_CHANNEL_1_TX_DMA ), Eq( SPI_CHANNEL_1_TX_DMA_STREAM ) ) );
    EXPECT_CALL(
        mock, DMAIsEnabledStream( Eq( SPI_CHANNEL_1_TX_DMA ), Eq( SPI_CHANNEL_1_TX_DMA_STREAM ) ) )
        .WillOnce( Return( 0U ) );
    EXPECT_CALL( mock, DMASetMemoryAddress( Eq( SPI_CHANNEL_1_TX_DMA ),
                                            Eq( SPI_CHANNEL_1_TX_DMA_STREAM ), _ ) );
    EXPECT_CALL( mock, SPIDMAGetRegAddr( Eq( SPI_CHANNEL_1_INSTANCE ) ) )
        .WillOnce( Return( 0xCAFEBABEU ) );
    EXPECT_CALL( mock,
                 DMASetPeriphAddress( Eq( SPI_CHANNEL_1_TX_DMA ), Eq( SPI_CHANNEL_1_TX_DMA_STREAM ),
                                      Eq( 0xCAFEBABEU ) ) );
    EXPECT_CALL( mock, DMASetDataLength( Eq( SPI_CHANNEL_1_TX_DMA ),
                                         Eq( SPI_CHANNEL_1_TX_DMA_STREAM ), Eq( 3U ) ) );
    EXPECT_CALL( mock, SPIEnableDMAReqTX( Eq( SPI_CHANNEL_1_INSTANCE ) ) );
    EXPECT_CALL( mock,
                 DMAEnableITTC( Eq( SPI_CHANNEL_1_TX_DMA ), Eq( SPI_CHANNEL_1_TX_DMA_STREAM ) ) );
    EXPECT_CALL( mock,
                 DMAEnableITTE( Eq( SPI_CHANNEL_1_TX_DMA ), Eq( SPI_CHANNEL_1_TX_DMA_STREAM ) ) );
    EXPECT_CALL( mock,
                 DMAEnableStream( Eq( SPI_CHANNEL_1_TX_DMA ), Eq( SPI_CHANNEL_1_TX_DMA_STREAM ) ) );
    EXPECT_CALL( mock, NVICEnableIRQ( SPI_CHANNEL_1_TX_DMA_IRQN ) );

    HW_SPI_Tx_Trigger( SPI_CHANNEL_1 );

    EXPECT_EQ( channel_1_state->tx_read_position, 6U );
    EXPECT_EQ( channel_1_state->tx_num_bytes_pending, 0U );
    EXPECT_EQ( channel_1_state->tx_num_bytes_in_transmission, 6U );
}

TEST_F( HWSPITest, TxTrigger_InvokesErrorHandlerWhenMisalignedQueuedDataExistsIn16BitMode )
{
    channel_1_state->config.data_size             = SPI_SIZE_16_BIT;
    channel_1_state->tx_write_position            = 3U;
    channel_1_state->tx_read_position             = 0U;
    channel_1_state->tx_num_bytes_pending         = 3U;
    channel_1_state->tx_num_bytes_in_transmission = 0U;

    InSequence seq;
    EXPECT_CALL( mock, NVICDisableIRQ( SPI_CHANNEL_1_TX_DMA_IRQN ) );
    EXPECT_CALL( mock, DMAClearFlagTC1( Eq( SPI_CHANNEL_1_TX_DMA ) ) );
    EXPECT_CALL( mock, DMAClearFlagTE1( Eq( SPI_CHANNEL_1_TX_DMA ) ) );
    EXPECT_CALL( mock,
                 DMADisableITTC( Eq( SPI_CHANNEL_1_TX_DMA ), Eq( SPI_CHANNEL_1_TX_DMA_STREAM ) ) );
    EXPECT_CALL( mock,
                 DMADisableITTE( Eq( SPI_CHANNEL_1_TX_DMA ), Eq( SPI_CHANNEL_1_TX_DMA_STREAM ) ) );
    EXPECT_CALL( mock, SPIDisableDMAReqTX( Eq( SPI_CHANNEL_1_INSTANCE ) ) );
    EXPECT_CALL(
        mock, DMADisableStream( Eq( SPI_CHANNEL_1_TX_DMA ), Eq( SPI_CHANNEL_1_TX_DMA_STREAM ) ) );
    EXPECT_CALL(
        mock, DMAIsEnabledStream( Eq( SPI_CHANNEL_1_TX_DMA ), Eq( SPI_CHANNEL_1_TX_DMA_STREAM ) ) )
        .WillOnce( Return( 0U ) );
    EXPECT_CALL( mock, NVICEnableIRQ( SPI_CHANNEL_1_TX_DMA_IRQN ) );

    HW_SPI_Tx_Trigger( SPI_CHANNEL_1 );

    EXPECT_EQ( channel_1_state->tx_num_bytes_in_transmission, 0U );
}

TEST_F( HWSPITest, Channel0TxIRQ_ErrorFlagTakesPriorityOverTransferComplete )
{
    channel_0_state->tx_write_position            = 10U;
    channel_0_state->tx_read_position             = 2U;
    channel_0_state->tx_num_bytes_pending         = 6U;
    channel_0_state->tx_num_bytes_in_transmission = 4U;

    InSequence seq;
    EXPECT_CALL( mock, DMAIsActiveFlagTE5( Eq( SPI_CHANNEL_0_TX_DMA ) ) ).WillOnce( Return( 1U ) );
    EXPECT_CALL( mock, DMAClearFlagTE5( Eq( SPI_CHANNEL_0_TX_DMA ) ) );
    EXPECT_CALL( mock,
                 DMADisableITTC( Eq( SPI_CHANNEL_0_TX_DMA ), Eq( SPI_CHANNEL_0_TX_DMA_STREAM ) ) );
    EXPECT_CALL( mock,
                 DMADisableITTE( Eq( SPI_CHANNEL_0_TX_DMA ), Eq( SPI_CHANNEL_0_TX_DMA_STREAM ) ) );
    EXPECT_CALL( mock, SPIDisableDMAReqTX( Eq( SPI_CHANNEL_0_INSTANCE ) ) );
    EXPECT_CALL(
        mock, DMADisableStream( Eq( SPI_CHANNEL_0_TX_DMA ), Eq( SPI_CHANNEL_0_TX_DMA_STREAM ) ) );
    EXPECT_CALL(
        mock, DMAIsEnabledStream( Eq( SPI_CHANNEL_0_TX_DMA ), Eq( SPI_CHANNEL_0_TX_DMA_STREAM ) ) )
        .WillOnce( Return( 0U ) );

    SPI_CHANNEL_0_TX_DMA_IRQ();

    EXPECT_EQ( channel_0_state->tx_num_bytes_in_transmission, 0U );
    EXPECT_EQ( channel_0_state->tx_num_bytes_pending, 6U );
    EXPECT_EQ( channel_0_state->tx_read_position, 2U );
    EXPECT_EQ( channel_0_state->tx_write_position, 10U );
}

TEST_F( HWSPITest, Channel0TxIRQ_TransferCompleteClearsInFlightWhenNoPendingData )
{
    channel_0_state->tx_read_position             = 5U;
    channel_0_state->tx_write_position            = 5U;
    channel_0_state->tx_num_bytes_pending         = 0U;
    channel_0_state->tx_num_bytes_in_transmission = 5U;

    EXPECT_CALL( mock, DMAIsActiveFlagTE5( Eq( SPI_CHANNEL_0_TX_DMA ) ) ).WillOnce( Return( 0U ) );
    EXPECT_CALL( mock, DMAIsActiveFlagTC5( Eq( SPI_CHANNEL_0_TX_DMA ) ) ).WillOnce( Return( 1U ) );
    EXPECT_CALL( mock, DMAClearFlagTC5( Eq( SPI_CHANNEL_0_TX_DMA ) ) );

    SPI_CHANNEL_0_TX_DMA_IRQ();

    EXPECT_EQ( channel_0_state->tx_num_bytes_in_transmission, 0U );
    EXPECT_EQ( channel_0_state->tx_num_bytes_pending, 0U );
    EXPECT_EQ( channel_0_state->tx_read_position, 5U );
    EXPECT_EQ( channel_0_state->tx_write_position, 5U );
}

TEST_F( HWSPITest, Channel0TxIRQ_TransferCompleteRearmsDmaWhenMoreQueuedDataExists )
{
    channel_0_state->tx_read_position             = 4U;
    channel_0_state->tx_write_position            = 8U;
    channel_0_state->tx_num_bytes_pending         = 4U;
    channel_0_state->tx_num_bytes_in_transmission = 4U;

    InSequence seq;
    EXPECT_CALL( mock, DMAIsActiveFlagTE5( Eq( SPI_CHANNEL_0_TX_DMA ) ) ).WillOnce( Return( 0U ) );
    EXPECT_CALL( mock, DMAIsActiveFlagTC5( Eq( SPI_CHANNEL_0_TX_DMA ) ) ).WillOnce( Return( 1U ) );
    EXPECT_CALL( mock, DMAClearFlagTC5( Eq( SPI_CHANNEL_0_TX_DMA ) ) );
    EXPECT_CALL(
        mock, DMADisableStream( Eq( SPI_CHANNEL_0_TX_DMA ), Eq( SPI_CHANNEL_0_TX_DMA_STREAM ) ) );
    EXPECT_CALL(
        mock, DMAIsEnabledStream( Eq( SPI_CHANNEL_0_TX_DMA ), Eq( SPI_CHANNEL_0_TX_DMA_STREAM ) ) )
        .WillOnce( Return( 0U ) );
    EXPECT_CALL( mock,
                 DMASetMemoryAddress( Eq( SPI_CHANNEL_0_TX_DMA ), Eq( SPI_CHANNEL_0_TX_DMA_STREAM ),
                                      Eq( static_cast<uint32_t>( reinterpret_cast<uintptr_t>(
                                          &channel_0_state->tx_buffer[4] ) ) ) ) );
    EXPECT_CALL( mock, SPIDMAGetRegAddr( Eq( SPI_CHANNEL_0_INSTANCE ) ) )
        .WillOnce( Return( 0x89ABCD01U ) );
    EXPECT_CALL( mock,
                 DMASetPeriphAddress( Eq( SPI_CHANNEL_0_TX_DMA ), Eq( SPI_CHANNEL_0_TX_DMA_STREAM ),
                                      Eq( 0x89ABCD01U ) ) );
    EXPECT_CALL( mock, DMASetDataLength( Eq( SPI_CHANNEL_0_TX_DMA ),
                                         Eq( SPI_CHANNEL_0_TX_DMA_STREAM ), Eq( 4U ) ) );
    EXPECT_CALL( mock, SPIEnableDMAReqTX( Eq( SPI_CHANNEL_0_INSTANCE ) ) );
    EXPECT_CALL( mock,
                 DMAEnableITTC( Eq( SPI_CHANNEL_0_TX_DMA ), Eq( SPI_CHANNEL_0_TX_DMA_STREAM ) ) );
    EXPECT_CALL( mock,
                 DMAEnableITTE( Eq( SPI_CHANNEL_0_TX_DMA ), Eq( SPI_CHANNEL_0_TX_DMA_STREAM ) ) );
    EXPECT_CALL( mock,
                 DMAEnableStream( Eq( SPI_CHANNEL_0_TX_DMA ), Eq( SPI_CHANNEL_0_TX_DMA_STREAM ) ) );

    SPI_CHANNEL_0_TX_DMA_IRQ();

    EXPECT_EQ( channel_0_state->tx_read_position, 8U );
    EXPECT_EQ( channel_0_state->tx_write_position, 8U );
    EXPECT_EQ( channel_0_state->tx_num_bytes_pending, 0U );
    EXPECT_EQ( channel_0_state->tx_num_bytes_in_transmission, 4U );
}

TEST_F( HWSPITest, Channel1TxIRQ_ErrorPathClearsInFlightButLeavesQueuePositions )
{
    channel_1_state->tx_read_position             = 6U;
    channel_1_state->tx_write_position            = 14U;
    channel_1_state->tx_num_bytes_pending         = 8U;
    channel_1_state->tx_num_bytes_in_transmission = 4U;

    EXPECT_CALL( mock, DMAIsActiveFlagTE1( Eq( SPI_CHANNEL_1_TX_DMA ) ) ).WillOnce( Return( 1U ) );
    EXPECT_CALL( mock, DMAClearFlagTE1( Eq( SPI_CHANNEL_1_TX_DMA ) ) );
    EXPECT_CALL( mock,
                 DMADisableITTC( Eq( SPI_CHANNEL_1_TX_DMA ), Eq( SPI_CHANNEL_1_TX_DMA_STREAM ) ) );
    EXPECT_CALL( mock,
                 DMADisableITTE( Eq( SPI_CHANNEL_1_TX_DMA ), Eq( SPI_CHANNEL_1_TX_DMA_STREAM ) ) );
    EXPECT_CALL( mock, SPIDisableDMAReqTX( Eq( SPI_CHANNEL_1_INSTANCE ) ) );
    EXPECT_CALL(
        mock, DMADisableStream( Eq( SPI_CHANNEL_1_TX_DMA ), Eq( SPI_CHANNEL_1_TX_DMA_STREAM ) ) );
    EXPECT_CALL(
        mock, DMAIsEnabledStream( Eq( SPI_CHANNEL_1_TX_DMA ), Eq( SPI_CHANNEL_1_TX_DMA_STREAM ) ) )
        .WillOnce( Return( 0U ) );

    SPI_CHANNEL_1_TX_DMA_IRQ();

    EXPECT_EQ( channel_1_state->tx_num_bytes_in_transmission, 0U );
    EXPECT_EQ( channel_1_state->tx_num_bytes_pending, 8U );
    EXPECT_EQ( channel_1_state->tx_read_position, 6U );
    EXPECT_EQ( channel_1_state->tx_write_position, 14U );
}

TEST_F( HWSPITest, Channel1TxIRQ_TransferCompleteClearsInFlightWhenDone )
{
    channel_1_state->tx_read_position             = 6U;
    channel_1_state->tx_write_position            = 6U;
    channel_1_state->tx_num_bytes_pending         = 0U;
    channel_1_state->tx_num_bytes_in_transmission = 6U;

    EXPECT_CALL( mock, DMAIsActiveFlagTE1( Eq( SPI_CHANNEL_1_TX_DMA ) ) ).WillOnce( Return( 0U ) );
    EXPECT_CALL( mock, DMAIsActiveFlagTC1( Eq( SPI_CHANNEL_1_TX_DMA ) ) ).WillOnce( Return( 1U ) );
    EXPECT_CALL( mock, DMAClearFlagTC1( Eq( SPI_CHANNEL_1_TX_DMA ) ) );

    SPI_CHANNEL_1_TX_DMA_IRQ();

    EXPECT_EQ( channel_1_state->tx_num_bytes_in_transmission, 0U );
    EXPECT_EQ( channel_1_state->tx_num_bytes_pending, 0U );
    EXPECT_EQ( channel_1_state->tx_read_position, 6U );
    EXPECT_EQ( channel_1_state->tx_write_position, 6U );
}

TEST_F( HWSPITest, Channel0TxIRQ_DoesNothingWhenNoFlagsAreActive )
{
    channel_0_state->tx_read_position             = 1U;
    channel_0_state->tx_write_position            = 5U;
    channel_0_state->tx_num_bytes_pending         = 4U;
    channel_0_state->tx_num_bytes_in_transmission = 2U;

    EXPECT_CALL( mock, DMAIsActiveFlagTE5( Eq( SPI_CHANNEL_0_TX_DMA ) ) ).WillOnce( Return( 0U ) );
    EXPECT_CALL( mock, DMAIsActiveFlagTC5( Eq( SPI_CHANNEL_0_TX_DMA ) ) ).WillOnce( Return( 0U ) );

    SPI_CHANNEL_0_TX_DMA_IRQ();

    EXPECT_EQ( channel_0_state->tx_read_position, 1U );
    EXPECT_EQ( channel_0_state->tx_write_position, 5U );
    EXPECT_EQ( channel_0_state->tx_num_bytes_pending, 4U );
    EXPECT_EQ( channel_0_state->tx_num_bytes_in_transmission, 2U );
}