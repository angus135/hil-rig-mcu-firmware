/******************************************************************************
 *  File:       test_hw_spi_tx_master.cpp
 *  Author:     Angus Corr
 *  Created:    05-May-2026
 *
 *  Description:
 *      Unit tests for the master TX portion of the low-level SPI driver using
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
#include <array>
#include <vector>

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

enum class GPIOEventKind
{
    CONFIGURE_OUTPUT,
    CONFIGURE_ALTERNATE,
    SET_HIGH,
    RESET_LOW,
    DMA_ARM,
};

struct GPIOEvent
{
    GPIOEventKind kind;
    GPIOPin_T     pin;
    bool          initial_high;
};

static std::vector<GPIOEvent> gpio_events;
static bool                   gpio_output_configuration_succeeds    = true;
static bool                   gpio_alternate_configuration_succeeds = true;

extern "C" bool HW_GPIO_Is_Valid_Pin( GPIOPin_T pin )
{
    return pin > GPIO_PIN_NONE && pin < GPIO_NUM_PINS;
}

extern "C" bool HW_GPIO_Configure_Pin_As_Output( GPIOPin_T pin, bool initial_high )
{
    gpio_events.push_back( { GPIOEventKind::CONFIGURE_OUTPUT, pin, initial_high } );
    return gpio_output_configuration_succeeds;
}

extern "C" bool HW_GPIO_Configure_Pin_As_Alternate_Function( GPIOPin_T pin )
{
    gpio_events.push_back( { GPIOEventKind::CONFIGURE_ALTERNATE, pin, false } );
    return gpio_alternate_configuration_succeeds;
}

extern "C" void HW_GPIO_Set_Pin( GPIOPin_T pin )
{
    gpio_events.push_back( { GPIOEventKind::SET_HIGH, pin, true } );
}

extern "C" void HW_GPIO_Reset_Pin( GPIOPin_T pin )
{
    gpio_events.push_back( { GPIOEventKind::RESET_LOW, pin, false } );
}
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
    MOCK_METHOD( void, SPIDisableDMAReqRX, ( SPI_TypeDef * spi ), () );
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
    MOCK_METHOD( uint32_t, DMAIsActiveFlagTE4, ( DMA_TypeDef * dma ), () );
    MOCK_METHOD( void, DMAClearFlagTE4, ( DMA_TypeDef * dma ), () );
    MOCK_METHOD( uint32_t, DMAIsActiveFlagTC4, ( DMA_TypeDef * dma ), () );
    MOCK_METHOD( void, DMAClearFlagTC4, ( DMA_TypeDef * dma ), () );

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
    gpio_events.push_back( { GPIOEventKind::DMA_ARM, GPIO_PIN_NONE, false } );
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

extern "C" void LL_SPI_DisableDMAReq_RX( SPI_TypeDef* SPIx )
{
    if ( g_mock )
    {
        g_mock->SPIDisableDMAReqRX( SPIx );
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

extern "C" uint32_t LL_DMA_IsActiveFlag_TE4( DMA_TypeDef* DMAx )
{
    return g_mock ? g_mock->DMAIsActiveFlagTE4( DMAx ) : 0U;
}

extern "C" void LL_DMA_ClearFlag_TE4( DMA_TypeDef* DMAx )
{
    if ( g_mock )
    {
        g_mock->DMAClearFlagTE4( DMAx );
    }
}

extern "C" uint32_t LL_DMA_IsActiveFlag_TC4( DMA_TypeDef* DMAx )
{
    return g_mock ? g_mock->DMAIsActiveFlagTC4( DMAx ) : 0U;
}

extern "C" void LL_DMA_ClearFlag_TC4( DMA_TypeDef* DMAx )
{
    if ( g_mock )
    {
        g_mock->DMAClearFlagTC4( DMAx );
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

/**-----------------------------------------------------------------------------
 *  Test Fixture
 *------------------------------------------------------------------------------
 */

/**
 * @brief Test fixture for module tests.
 *
 * Provides a consistent setup/teardown environment for all test cases.
 */
class HWSpiMasterTxTest : public ::testing::Test
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
                              .cpha      = SPI_CPHA_1_EDGE,
                              .nss_pin   = GPIO_SPI1_NSS };
    }

    static HWSPIConfig_T MakeSlaveConfig( SPIDataSize_T size = SPI_SIZE_8_BIT,
                                          SPIBaudRate_T baud = SPI_BAUD_45MBIT )
    {
        return HWSPIConfig_T{ .spi_mode  = SPI_SLAVE_MODE,
                              .data_size = size,
                              .first_bit = SPI_FIRST_MSB,
                              .baud_rate = baud,
                              .cpol      = SPI_CPOL_LOW,
                              .cpha      = SPI_CPHA_1_EDGE,
                              .nss_pin   = GPIO_SPI1_NSS };
    }

    static void InitialiseState( SPIPeripheralState_T* state, SPIChannel_T logical,
                                 HWSPIConfig_T config, DMA_TypeDef* rx_dma, uint32_t rx_stream,
                                 DMA_TypeDef* tx_dma, uint32_t tx_stream, SPI_TypeDef* spi,
                                 IRQn_Type tx_irqn, Timer_T timer )
    {
        memset( state, 0, sizeof( *state ) );
        state->config                    = config;
        state->logical_peripheral        = logical;
        state->nss_pin                   = config.nss_pin;
        state->is_configured             = true;
        state->is_started                = false;
        state->cs_asserted               = false;
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
        HW_SPI_TX_Configure_Timer( state );
        HW_SPI_TX_Reset_State( state );
    }

    void SetUp( void ) override
    {
        g_mock = &mock;
        memset( &SPI_CHANNEL_0_HANDLE, 0, sizeof( SPI_CHANNEL_0_HANDLE ) );
        gpio_events.clear();
        gpio_output_configuration_succeeds    = true;
        gpio_alternate_configuration_succeeds = true;
        memset( &SPI_CHANNEL_1_HANDLE, 0, sizeof( SPI_CHANNEL_1_HANDLE ) );

        EXPECT_CALL( mock, TimerConfigure( _, _, _ ) ).Times( AnyNumber() );

        InitialiseState( HW_SPI_STATE( SPI_CHANNEL_0 ), SPI_CHANNEL_0, MakeMasterConfig(),
                         SPI_CHANNEL_0_RX_DMA, SPI_CHANNEL_0_RX_DMA_STREAM, SPI_CHANNEL_0_TX_DMA,
                         SPI_CHANNEL_0_TX_DMA_STREAM, SPI_CHANNEL_0_INSTANCE,
                         SPI_CHANNEL_0_TX_DMA_IRQN, SPI_CHANNEL_0_TIMER );
        InitialiseState( HW_SPI_STATE( SPI_CHANNEL_1 ), SPI_CHANNEL_1, MakeMasterConfig(),
                         SPI_CHANNEL_1_RX_DMA, SPI_CHANNEL_1_RX_DMA_STREAM, SPI_CHANNEL_1_TX_DMA,
                         SPI_CHANNEL_1_TX_DMA_STREAM, SPI_CHANNEL_1_INSTANCE,
                         SPI_CHANNEL_1_TX_DMA_IRQN, SPI_CHANNEL_1_TIMER );
        InitialiseState( HW_SPI_STATE( SPI_DAC ), SPI_DAC, MakeMasterConfig(), NULL, 0U,
                         SPI_DAC_TX_DMA, SPI_DAC_TX_DMA_STREAM, SPI_DAC_INSTANCE,
                         SPI_DAC_TX_DMA_IRQN, SPI_DAC_TIMER );
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
        EXPECT_CALL( mock, DMAClearFlagTC4( Eq( SPI_CHANNEL_1_TX_DMA ) ) );
        EXPECT_CALL( mock, DMAClearFlagTE4( Eq( SPI_CHANNEL_1_TX_DMA ) ) );
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

    void ExpectDacDmaProgram( const uint8_t* expected_ptr, uint32_t expected_elements )
    {
        EXPECT_CALL( mock, SPIDisableDMAReqTX( Eq( SPI_DAC_INSTANCE ) ) );
        EXPECT_CALL( mock, DMADisableStream( Eq( SPI_DAC_TX_DMA ), Eq( SPI_DAC_TX_DMA_STREAM ) ) );
        EXPECT_CALL( mock, DMAIsEnabledStream( Eq( SPI_DAC_TX_DMA ), Eq( SPI_DAC_TX_DMA_STREAM ) ) )
            .WillOnce( Return( 0U ) );
        EXPECT_CALL( mock, DMAClearFlagTC1( Eq( SPI_DAC_TX_DMA ) ) );
        EXPECT_CALL( mock, DMAClearFlagTE1( Eq( SPI_DAC_TX_DMA ) ) );
        EXPECT_CALL( mock,
                     DMASetMemoryAddress( Eq( SPI_DAC_TX_DMA ), Eq( SPI_DAC_TX_DMA_STREAM ),
                                          Eq( static_cast<uint32_t>(
                                              reinterpret_cast<uintptr_t>( expected_ptr ) ) ) ) );
        EXPECT_CALL( mock, DMASetDataLength( Eq( SPI_DAC_TX_DMA ), Eq( SPI_DAC_TX_DMA_STREAM ),
                                             Eq( expected_elements ) ) );
        EXPECT_CALL( mock, DMAEnableStream( Eq( SPI_DAC_TX_DMA ), Eq( SPI_DAC_TX_DMA_STREAM ) ) );
        EXPECT_CALL( mock, SPIEnableDMAReqTX( Eq( SPI_DAC_INSTANCE ) ) );
    }

    void ExpectChannel0ConfigurationHardware()
    {
        constexpr uint32_t spi_data_register_address = 0x12345678U;

        EXPECT_CALL( mock, SPIInit( Eq( &SPI_CHANNEL_0_HANDLE ) ) ).WillOnce( Return( HAL_OK ) );

        EXPECT_CALL( mock, DMASetMemorySize( Eq( SPI_CHANNEL_0_RX_DMA ),
                                             Eq( SPI_CHANNEL_0_RX_DMA_STREAM ),
                                             Eq( LL_DMA_MDATAALIGN_BYTE ) ) );
        EXPECT_CALL( mock, DMASetPeriphSize( Eq( SPI_CHANNEL_0_RX_DMA ),
                                             Eq( SPI_CHANNEL_0_RX_DMA_STREAM ),
                                             Eq( LL_DMA_PDATAALIGN_BYTE ) ) );
        EXPECT_CALL( mock, SPIDMAGetRegAddr( Eq( SPI_CHANNEL_0_INSTANCE ) ) )
            .Times( 2 )
            .WillRepeatedly( Return( spi_data_register_address ) );
        EXPECT_CALL( mock, DMASetPeriphAddress( Eq( SPI_CHANNEL_0_RX_DMA ),
                                                Eq( SPI_CHANNEL_0_RX_DMA_STREAM ),
                                                Eq( spi_data_register_address ) ) );

        EXPECT_CALL( mock, DMASetMemorySize( Eq( SPI_CHANNEL_0_TX_DMA ),
                                             Eq( SPI_CHANNEL_0_TX_DMA_STREAM ),
                                             Eq( LL_DMA_MDATAALIGN_BYTE ) ) );
        EXPECT_CALL( mock, DMASetPeriphSize( Eq( SPI_CHANNEL_0_TX_DMA ),
                                             Eq( SPI_CHANNEL_0_TX_DMA_STREAM ),
                                             Eq( LL_DMA_PDATAALIGN_BYTE ) ) );
        EXPECT_CALL( mock, DMASetPeriphAddress( Eq( SPI_CHANNEL_0_TX_DMA ),
                                                Eq( SPI_CHANNEL_0_TX_DMA_STREAM ),
                                                Eq( spi_data_register_address ) ) );
        EXPECT_CALL(
            mock, DMAEnableITTC( Eq( SPI_CHANNEL_0_TX_DMA ), Eq( SPI_CHANNEL_0_TX_DMA_STREAM ) ) );
        EXPECT_CALL(
            mock, DMAEnableITTE( Eq( SPI_CHANNEL_0_TX_DMA ), Eq( SPI_CHANNEL_0_TX_DMA_STREAM ) ) );
    }
};

/**-----------------------------------------------------------------------------
 *  Test Cases
 *------------------------------------------------------------------------------
 */
TEST_F( HWSpiMasterTxTest, LoadTxBuffer_MasterCreatesOnePacketDescriptorPerLoad )
{
    const uint8_t first[2]  = { 0xAAU, 0xBBU };
    const uint8_t second[3] = { 0x01U, 0x02U, 0x03U };

    EXPECT_CALL( mock, NVICDisableIRQ( SPI_CHANNEL_0_TX_DMA_IRQN ) ).Times( 2 );
    EXPECT_CALL( mock, NVICEnableIRQ( SPI_CHANNEL_0_TX_DMA_IRQN ) ).Times( 2 );

    EXPECT_TRUE( HW_SPI_Load_Tx_Buffer( SPI_CHANNEL_0, first, sizeof( first ) ) );
    EXPECT_TRUE( HW_SPI_Load_Tx_Buffer( SPI_CHANNEL_0, second, sizeof( second ) ) );

    EXPECT_EQ( HW_SPI_STATE( SPI_CHANNEL_0 )->tx_num_packets_pending, 2U );
    EXPECT_EQ( HW_SPI_STATE( SPI_CHANNEL_0 )->tx_packet_descriptors[0].start_index, 0U );
    EXPECT_EQ( HW_SPI_STATE( SPI_CHANNEL_0 )->tx_packet_descriptors[0].size_bytes,
               sizeof( first ) );
    EXPECT_EQ( HW_SPI_STATE( SPI_CHANNEL_0 )->tx_packet_descriptors[1].start_index,
               sizeof( first ) );
    EXPECT_EQ( HW_SPI_STATE( SPI_CHANNEL_0 )->tx_packet_descriptors[1].size_bytes,
               sizeof( second ) );
    EXPECT_EQ( HW_SPI_STATE( SPI_CHANNEL_0 )->tx_num_bytes_pending,
               sizeof( first ) + sizeof( second ) );
}

TEST_F( HWSpiMasterTxTest, LoadTxBuffer_MasterWrapsWholePacketRatherThanSplittingPacket )
{
    const uint8_t data[6]                            = { 1U, 2U, 3U, 4U, 5U, 6U };
    HW_SPI_STATE( SPI_CHANNEL_0 )->tx_write_position = TX_BUFFER_SIZE_BYTES - 2U;
    HW_SPI_STATE( SPI_CHANNEL_0 )->tx_read_position  = 20U;

    EXPECT_CALL( mock, NVICDisableIRQ( SPI_CHANNEL_0_TX_DMA_IRQN ) );
    EXPECT_CALL( mock, NVICEnableIRQ( SPI_CHANNEL_0_TX_DMA_IRQN ) );

    EXPECT_TRUE( HW_SPI_Load_Tx_Buffer( SPI_CHANNEL_0, data, sizeof( data ) ) );

    EXPECT_EQ( HW_SPI_STATE( SPI_CHANNEL_0 )->tx_packet_descriptors[0].start_index, 0U );
    EXPECT_EQ( HW_SPI_STATE( SPI_CHANNEL_0 )->tx_packet_descriptors[0].size_bytes, sizeof( data ) );
    EXPECT_EQ( memcmp( &HW_SPI_STATE( SPI_CHANNEL_0 )->tx_buffer[0], data, sizeof( data ) ), 0 );
    EXPECT_EQ( HW_SPI_STATE( SPI_CHANNEL_0 )->tx_write_position, 6U );
}

TEST_F( HWSpiMasterTxTest, LoadTxBuffer_MasterRejectsWhenDescriptorQueueIsFull )
{
    const uint8_t one_byte                                = 0x55U;
    HW_SPI_STATE( SPI_CHANNEL_0 )->tx_num_packets_pending = TX_PACKET_QUEUE_DEPTH;

    EXPECT_CALL( mock, NVICDisableIRQ( SPI_CHANNEL_0_TX_DMA_IRQN ) );
    EXPECT_CALL( mock, NVICEnableIRQ( SPI_CHANNEL_0_TX_DMA_IRQN ) );

    EXPECT_FALSE( HW_SPI_Load_Tx_Buffer( SPI_CHANNEL_0, &one_byte, 1U ) );
}

TEST_F( HWSpiMasterTxTest, LoadTxPackets_QueuesElevenSeparateThreeByteDescriptorsInOrder )
{
    std::array<uint8_t, 33U> startup_bytes = {};
    for ( size_t index = 0U; index < startup_bytes.size(); index++ )
    {
        startup_bytes[index] = static_cast<uint8_t>( index );
    }

    EXPECT_CALL( mock, NVICDisableIRQ( SPI_DAC_TX_DMA_IRQN ) );
    EXPECT_CALL( mock, NVICEnableIRQ( SPI_DAC_TX_DMA_IRQN ) );

    ASSERT_TRUE( HW_SPI_Load_Tx_Packets( SPI_DAC, startup_bytes.data(), 3U, 11U ) );

    EXPECT_EQ( HW_SPI_STATE( SPI_DAC )->tx_num_packets_pending, 11U );
    EXPECT_EQ( HW_SPI_STATE( SPI_DAC )->tx_num_bytes_pending, startup_bytes.size() );
    for ( uint32_t packet_index = 0U; packet_index < 11U; packet_index++ )
    {
        const SPITxPacketDescriptor_T& descriptor =
            HW_SPI_STATE( SPI_DAC )->tx_packet_descriptors[packet_index];
        EXPECT_EQ( descriptor.start_index, packet_index * 3U );
        EXPECT_EQ( descriptor.size_bytes, 3U );
    }
    EXPECT_EQ(
        memcmp( HW_SPI_STATE( SPI_DAC )->tx_buffer, startup_bytes.data(), startup_bytes.size() ),
        0 );
}

TEST_F( HWSpiMasterTxTest, LoadTxPackets_AppendsAfterActivePacketAndTransmitsInOrder )
{
    const std::array<uint8_t, 3U> active_packet  = { 0x10U, 0x11U, 0x12U };
    const std::array<uint8_t, 9U> queued_packets = { 0x20U, 0x21U, 0x22U, 0x30U, 0x31U,
                                                     0x32U, 0x40U, 0x41U, 0x42U };
    SPIPeripheralState_T*         state          = HW_SPI_STATE( SPI_CHANNEL_0 );

    EXPECT_CALL( mock, NVICDisableIRQ( SPI_CHANNEL_0_TX_DMA_IRQN ) );
    EXPECT_CALL( mock, NVICEnableIRQ( SPI_CHANNEL_0_TX_DMA_IRQN ) );
    ASSERT_TRUE(
        HW_SPI_Load_Tx_Buffer( SPI_CHANNEL_0, active_packet.data(), active_packet.size() ) );
    testing::Mock::VerifyAndClearExpectations( &mock );

    {
        InSequence sequence;
        EXPECT_CALL( mock, NVICDisableIRQ( SPI_CHANNEL_0_TX_DMA_IRQN ) );
        ExpectChannel0DmaProgram( &state->tx_buffer[0], active_packet.size() );
        EXPECT_CALL( mock, NVICEnableIRQ( SPI_CHANNEL_0_TX_DMA_IRQN ) );
    }
    HW_SPI_Tx_Trigger( SPI_CHANNEL_0 );
    testing::Mock::VerifyAndClearExpectations( &mock );

    ASSERT_EQ( state->tx_transaction_state, HW_SPI_TX_TRANSACTION_DMA_ACTIVE );
    ASSERT_EQ( state->tx_read_position, active_packet.size() );
    ASSERT_EQ( state->tx_write_position, state->tx_read_position );
    ASSERT_EQ( state->tx_num_bytes_in_transmission, active_packet.size() );

    EXPECT_CALL( mock, NVICDisableIRQ( SPI_CHANNEL_0_TX_DMA_IRQN ) );
    EXPECT_CALL( mock, NVICEnableIRQ( SPI_CHANNEL_0_TX_DMA_IRQN ) );
    ASSERT_TRUE(
        HW_SPI_Load_Tx_Packets( SPI_CHANNEL_0, queued_packets.data(), active_packet.size(), 3U ) );
    testing::Mock::VerifyAndClearExpectations( &mock );

    EXPECT_EQ( memcmp( state->tx_buffer, active_packet.data(), active_packet.size() ), 0 );
    ASSERT_EQ( state->tx_num_packets_pending, 3U );
    ASSERT_EQ( state->tx_num_bytes_pending, queued_packets.size() );
    for ( uint32_t packet_index = 0U; packet_index < 3U; packet_index++ )
    {
        const SPITxPacketDescriptor_T& descriptor = state->tx_packet_descriptors[packet_index + 1U];
        const uint32_t                 expected_start =
            static_cast<uint32_t>( active_packet.size() * ( packet_index + 1U ) );

        EXPECT_EQ( descriptor.start_index, expected_start );
        EXPECT_EQ( descriptor.size_bytes, active_packet.size() );
        EXPECT_EQ( memcmp( &state->tx_buffer[expected_start],
                           &queued_packets[packet_index * active_packet.size()],
                           active_packet.size() ),
                   0 );
    }

    for ( uint32_t completed_packet = 0U; completed_packet < 4U; completed_packet++ )
    {
        InSequence sequence;
        EXPECT_CALL( mock, DMAIsActiveFlagTE5( Eq( SPI_CHANNEL_0_TX_DMA ) ) )
            .WillOnce( Return( 0U ) );
        EXPECT_CALL( mock, DMAIsActiveFlagTC5( Eq( SPI_CHANNEL_0_TX_DMA ) ) )
            .WillOnce( Return( 1U ) );
        EXPECT_CALL( mock, DMAClearFlagTC5( Eq( SPI_CHANNEL_0_TX_DMA ) ) );
        EXPECT_CALL( mock, SPIDisableDMAReqTX( Eq( SPI_CHANNEL_0_INSTANCE ) ) );
        EXPECT_CALL( mock, SPIIsBusy( Eq( SPI_CHANNEL_0_INSTANCE ) ) ).WillOnce( Return( 0U ) );
        if ( completed_packet < 3U )
        {
            const uint32_t next_start =
                static_cast<uint32_t>( active_packet.size() * ( completed_packet + 1U ) );
            ExpectChannel0DmaProgram( &state->tx_buffer[next_start], active_packet.size() );
        }

        SPI_CHANNEL_0_TX_DMA_IRQ();
        testing::Mock::VerifyAndClearExpectations( &mock );
    }

    EXPECT_EQ( state->tx_transaction_state, HW_SPI_TX_TRANSACTION_IDLE );
    EXPECT_EQ( state->tx_num_packets_pending, 0U );
    EXPECT_EQ( state->tx_num_bytes_pending, 0U );
    EXPECT_EQ( state->tx_num_bytes_in_transmission, 0U );

    ASSERT_EQ( gpio_events.size(), 12U );
    for ( uint32_t packet_index = 0U; packet_index < 4U; packet_index++ )
    {
        EXPECT_EQ( gpio_events[packet_index * 3U].kind, GPIOEventKind::RESET_LOW );
        EXPECT_EQ( gpio_events[packet_index * 3U + 1U].kind, GPIOEventKind::DMA_ARM );
        EXPECT_EQ( gpio_events[packet_index * 3U + 2U].kind, GPIOEventKind::SET_HIGH );
    }
}

TEST_F( HWSpiMasterTxTest, LoadTxPackets_InsufficientByteCapacityLeavesQueueUnchanged )
{
    const uint8_t         packets[6] = { 1U, 2U, 3U, 4U, 5U, 6U };
    SPIPeripheralState_T* state      = HW_SPI_STATE( SPI_CHANNEL_0 );
    state->tx_write_position         = TX_BUFFER_SIZE_BYTES - 4U;
    state->tx_num_bytes_pending      = TX_BUFFER_SIZE_BYTES - 4U;
    memset( state->tx_buffer, 0xA5, sizeof( state->tx_buffer ) );

    const SPIPeripheralState_T before = *state;

    EXPECT_CALL( mock, NVICDisableIRQ( SPI_CHANNEL_0_TX_DMA_IRQN ) );
    EXPECT_CALL( mock, NVICEnableIRQ( SPI_CHANNEL_0_TX_DMA_IRQN ) );

    EXPECT_FALSE( HW_SPI_Load_Tx_Packets( SPI_CHANNEL_0, packets, 3U, 2U ) );
    EXPECT_EQ( memcmp( state, &before, sizeof( before ) ), 0 );
}

TEST_F( HWSpiMasterTxTest, LoadTxPackets_InsufficientDescriptorCapacityLeavesQueueUnchanged )
{
    const uint8_t         packets[6] = { 1U, 2U, 3U, 4U, 5U, 6U };
    SPIPeripheralState_T* state      = HW_SPI_STATE( SPI_CHANNEL_0 );
    state->tx_num_packets_pending    = TX_PACKET_QUEUE_DEPTH - 1U;

    const SPIPeripheralState_T before = *state;

    EXPECT_CALL( mock, NVICDisableIRQ( SPI_CHANNEL_0_TX_DMA_IRQN ) );
    EXPECT_CALL( mock, NVICEnableIRQ( SPI_CHANNEL_0_TX_DMA_IRQN ) );

    EXPECT_FALSE( HW_SPI_Load_Tx_Packets( SPI_CHANNEL_0, packets, 3U, 2U ) );
    EXPECT_EQ( memcmp( state, &before, sizeof( before ) ), 0 );
}

TEST_F( HWSpiMasterTxTest, LoadTxPackets_RejectsZeroUnalignedInvalidAndSlaveRequests )
{
    const uint8_t packets[4] = { 1U, 2U, 3U, 4U };
    InitialiseState( HW_SPI_STATE( SPI_CHANNEL_1 ), SPI_CHANNEL_1,
                     MakeMasterConfig( SPI_SIZE_16_BIT ), SPI_CHANNEL_1_RX_DMA,
                     SPI_CHANNEL_1_RX_DMA_STREAM, SPI_CHANNEL_1_TX_DMA, SPI_CHANNEL_1_TX_DMA_STREAM,
                     SPI_CHANNEL_1_INSTANCE, SPI_CHANNEL_1_TX_DMA_IRQN, SPI_CHANNEL_1_TIMER );

    EXPECT_FALSE( HW_SPI_Load_Tx_Packets( SPI_NUM_CHANNELS, packets, 2U, 1U ) );
    EXPECT_FALSE( HW_SPI_Load_Tx_Packets( SPI_CHANNEL_0, nullptr, 1U, 1U ) );
    EXPECT_FALSE( HW_SPI_Load_Tx_Packets( SPI_CHANNEL_0, packets, 0U, 1U ) );
    EXPECT_FALSE( HW_SPI_Load_Tx_Packets( SPI_CHANNEL_0, packets, 1U, 0U ) );

    EXPECT_CALL( mock, NVICDisableIRQ( SPI_CHANNEL_1_TX_DMA_IRQN ) );
    EXPECT_CALL( mock, NVICEnableIRQ( SPI_CHANNEL_1_TX_DMA_IRQN ) );
    EXPECT_FALSE( HW_SPI_Load_Tx_Packets( SPI_CHANNEL_1, packets, 3U, 1U ) );

    HW_SPI_STATE( SPI_CHANNEL_0 )->is_master = false;
    EXPECT_CALL( mock, NVICDisableIRQ( SPI_CHANNEL_0_TX_DMA_IRQN ) );
    EXPECT_CALL( mock, NVICEnableIRQ( SPI_CHANNEL_0_TX_DMA_IRQN ) );
    EXPECT_FALSE( HW_SPI_Load_Tx_Packets( SPI_CHANNEL_0, packets, 2U, 2U ) );
}

TEST_F( HWSpiMasterTxTest, TxFaultQuery_DistinguishesBusyFromError )
{
    HW_SPI_STATE( SPI_CHANNEL_0 )->tx_transaction_state = HW_SPI_TX_TRANSACTION_DMA_ACTIVE;
    EXPECT_FALSE( HW_SPI_Tx_Is_Faulted( SPI_CHANNEL_0 ) );

    HW_SPI_STATE( SPI_CHANNEL_0 )->tx_transaction_state = HW_SPI_TX_TRANSACTION_ERROR;
    EXPECT_TRUE( HW_SPI_Tx_Is_Faulted( SPI_CHANNEL_0 ) );
    EXPECT_FALSE( HW_SPI_Tx_Is_Faulted( SPI_NUM_CHANNELS ) );
}

TEST_F( HWSpiMasterTxTest, TxTrigger_MasterStartsOnlyFirstQueuedPacketAndLeavesRestPending )
{
    const uint8_t first[2]  = { 0x10U, 0x11U };
    const uint8_t second[3] = { 0x20U, 0x21U, 0x22U };

    EXPECT_CALL( mock, NVICDisableIRQ( SPI_CHANNEL_0_TX_DMA_IRQN ) ).Times( 2 );
    EXPECT_CALL( mock, NVICEnableIRQ( SPI_CHANNEL_0_TX_DMA_IRQN ) ).Times( 2 );
    EXPECT_TRUE( HW_SPI_Load_Tx_Buffer( SPI_CHANNEL_0, first, sizeof( first ) ) );
    EXPECT_TRUE( HW_SPI_Load_Tx_Buffer( SPI_CHANNEL_0, second, sizeof( second ) ) );
    testing::Mock::VerifyAndClearExpectations( &mock );

    InSequence seq;
    EXPECT_CALL( mock, NVICDisableIRQ( SPI_CHANNEL_0_TX_DMA_IRQN ) );
    ExpectChannel0DmaProgram( &HW_SPI_STATE( SPI_CHANNEL_0 )->tx_buffer[0], sizeof( first ) );
    EXPECT_CALL( mock, NVICEnableIRQ( SPI_CHANNEL_0_TX_DMA_IRQN ) );

    HW_SPI_Tx_Trigger( SPI_CHANNEL_0 );

    EXPECT_EQ( HW_SPI_STATE( SPI_CHANNEL_0 )->tx_transaction_state,
               HW_SPI_TX_TRANSACTION_DMA_ACTIVE );
    EXPECT_EQ( HW_SPI_STATE( SPI_CHANNEL_0 )->tx_num_packets_pending, 1U );
    EXPECT_EQ( HW_SPI_STATE( SPI_CHANNEL_0 )->tx_num_bytes_pending, sizeof( second ) );
    EXPECT_EQ( HW_SPI_STATE( SPI_CHANNEL_0 )->tx_num_bytes_in_transmission, sizeof( first ) );
}

TEST_F( HWSpiMasterTxTest, TxTrigger_MasterDoesNothingWhenTransactionAlreadyActive )
{
    HW_SPI_STATE( SPI_CHANNEL_0 )->tx_transaction_state         = HW_SPI_TX_TRANSACTION_DMA_ACTIVE;
    HW_SPI_STATE( SPI_CHANNEL_0 )->tx_num_bytes_in_transmission = 1U;
    HW_SPI_STATE( SPI_CHANNEL_0 )->tx_num_packets_pending       = 1U;

    EXPECT_CALL( mock, NVICDisableIRQ( SPI_CHANNEL_0_TX_DMA_IRQN ) );
    EXPECT_CALL( mock, NVICEnableIRQ( SPI_CHANNEL_0_TX_DMA_IRQN ) );

    HW_SPI_Tx_Trigger( SPI_CHANNEL_0 );

    EXPECT_EQ( HW_SPI_STATE( SPI_CHANNEL_0 )->tx_num_packets_pending, 1U );
}

TEST_F( HWSpiMasterTxTest, TxDmaIrq_MasterCompletesFastTransactionWhenBsyAlreadyClear )
{
    const uint8_t data[1] = { 0x5AU };
    EXPECT_CALL( mock, NVICDisableIRQ( SPI_CHANNEL_0_TX_DMA_IRQN ) );
    EXPECT_CALL( mock, NVICEnableIRQ( SPI_CHANNEL_0_TX_DMA_IRQN ) );
    EXPECT_TRUE( HW_SPI_Load_Tx_Buffer( SPI_CHANNEL_0, data, sizeof( data ) ) );
    testing::Mock::VerifyAndClearExpectations( &mock );

    InSequence start_seq;
    EXPECT_CALL( mock, NVICDisableIRQ( SPI_CHANNEL_0_TX_DMA_IRQN ) );
    ExpectChannel0DmaProgram( &HW_SPI_STATE( SPI_CHANNEL_0 )->tx_buffer[0], 1U );
    EXPECT_CALL( mock, NVICEnableIRQ( SPI_CHANNEL_0_TX_DMA_IRQN ) );
    HW_SPI_Tx_Trigger( SPI_CHANNEL_0 );
    testing::Mock::VerifyAndClearExpectations( &mock );

    InSequence irq_seq;
    EXPECT_CALL( mock, DMAIsActiveFlagTE5( Eq( SPI_CHANNEL_0_TX_DMA ) ) ).WillOnce( Return( 0U ) );
    EXPECT_CALL( mock, DMAIsActiveFlagTC5( Eq( SPI_CHANNEL_0_TX_DMA ) ) ).WillOnce( Return( 1U ) );
    EXPECT_CALL( mock, DMAClearFlagTC5( Eq( SPI_CHANNEL_0_TX_DMA ) ) );
    EXPECT_CALL( mock, SPIDisableDMAReqTX( Eq( SPI_CHANNEL_0_INSTANCE ) ) );
    EXPECT_CALL( mock, SPIIsBusy( Eq( SPI_CHANNEL_0_INSTANCE ) ) ).WillOnce( Return( 0U ) );

    SPI_CHANNEL_0_TX_DMA_IRQ();

    EXPECT_EQ( HW_SPI_STATE( SPI_CHANNEL_0 )->tx_transaction_state, HW_SPI_TX_TRANSACTION_IDLE );
    EXPECT_EQ( HW_SPI_STATE( SPI_CHANNEL_0 )->tx_num_bytes_in_transmission, 0U );
    EXPECT_EQ( HW_SPI_STATE( SPI_CHANNEL_0 )->tx_num_packets_pending, 0U );
}

TEST_F( HWSpiMasterTxTest, DacThreeByteFrameProgramsDma2Stream1AndCompletes )
{
    const uint8_t frame[3] = { 0x40U, 0xFFU, 0xFFU };

    EXPECT_CALL( mock, NVICDisableIRQ( SPI_DAC_TX_DMA_IRQN ) );
    EXPECT_CALL( mock, NVICEnableIRQ( SPI_DAC_TX_DMA_IRQN ) );
    ASSERT_TRUE( HW_SPI_Load_Tx_Buffer( SPI_DAC, frame, sizeof( frame ) ) );
    testing::Mock::VerifyAndClearExpectations( &mock );

    InSequence start_sequence;
    EXPECT_CALL( mock, NVICDisableIRQ( SPI_DAC_TX_DMA_IRQN ) );
    ExpectDacDmaProgram( &HW_SPI_STATE( SPI_DAC )->tx_buffer[0], 3U );
    EXPECT_CALL( mock, NVICEnableIRQ( SPI_DAC_TX_DMA_IRQN ) );

    HW_SPI_Tx_Trigger( SPI_DAC );
    testing::Mock::VerifyAndClearExpectations( &mock );

    EXPECT_EQ( HW_SPI_STATE( SPI_DAC )->tx_num_bytes_in_transmission, 3U );
    EXPECT_EQ( HW_SPI_STATE( SPI_DAC )->tx_transaction_state, HW_SPI_TX_TRANSACTION_DMA_ACTIVE );

    InSequence completion_sequence;
    EXPECT_CALL( mock, DMAIsActiveFlagTE1( Eq( SPI_DAC_TX_DMA ) ) ).WillOnce( Return( 0U ) );
    EXPECT_CALL( mock, DMAIsActiveFlagTC1( Eq( SPI_DAC_TX_DMA ) ) ).WillOnce( Return( 1U ) );
    EXPECT_CALL( mock, DMAClearFlagTC1( Eq( SPI_DAC_TX_DMA ) ) );
    EXPECT_CALL( mock, SPIDisableDMAReqTX( Eq( SPI_DAC_INSTANCE ) ) );
    EXPECT_CALL( mock, SPIIsBusy( Eq( SPI_DAC_INSTANCE ) ) ).WillOnce( Return( 0U ) );

    SPI_DAC_TX_DMA_IRQ();

    EXPECT_EQ( HW_SPI_STATE( SPI_DAC )->tx_num_bytes_in_transmission, 0U );
    EXPECT_EQ( HW_SPI_STATE( SPI_DAC )->tx_transaction_state, HW_SPI_TX_TRANSACTION_IDLE );
}

TEST_F( HWSpiMasterTxTest, TxDmaIrq_MasterStartsFinalDrainTimerWhenSlowBsyStillSet )
{
    HW_SPI_STATE( SPI_CHANNEL_0 )->config.baud_rate             = SPI_BAUD_352KBIT;
    HW_SPI_STATE( SPI_CHANNEL_0 )->tx_uses_final_drain_timer    = true;
    HW_SPI_STATE( SPI_CHANNEL_0 )->tx_final_drain_timer         = SPI_CHANNEL_0_TIMER;
    HW_SPI_STATE( SPI_CHANNEL_0 )->tx_transaction_state         = HW_SPI_TX_TRANSACTION_DMA_ACTIVE;
    HW_SPI_STATE( SPI_CHANNEL_0 )->tx_num_bytes_in_transmission = 1U;

    InSequence irq_seq;
    EXPECT_CALL( mock, DMAIsActiveFlagTE5( Eq( SPI_CHANNEL_0_TX_DMA ) ) ).WillOnce( Return( 0U ) );
    EXPECT_CALL( mock, DMAIsActiveFlagTC5( Eq( SPI_CHANNEL_0_TX_DMA ) ) ).WillOnce( Return( 1U ) );
    EXPECT_CALL( mock, DMAClearFlagTC5( Eq( SPI_CHANNEL_0_TX_DMA ) ) );
    EXPECT_CALL( mock, SPIDisableDMAReqTX( Eq( SPI_CHANNEL_0_INSTANCE ) ) );
    EXPECT_CALL( mock, SPIIsBusy( Eq( SPI_CHANNEL_0_INSTANCE ) ) ).WillOnce( Return( 1U ) );
    EXPECT_CALL( mock, TimerStart( SPI_CHANNEL_0_TIMER ) );

    SPI_CHANNEL_0_TX_DMA_IRQ();

    EXPECT_EQ( HW_SPI_STATE( SPI_CHANNEL_0 )->tx_transaction_state,
               HW_SPI_TX_TRANSACTION_WAIT_FINAL_DRAIN );
    EXPECT_EQ( HW_SPI_STATE( SPI_CHANNEL_0 )->tx_num_bytes_in_transmission, 0U );
    EXPECT_EQ( HW_SPI_STATE( SPI_CHANNEL_0 )->tx_final_drain_timer_attempts, 1U );
}

TEST_F( HWSpiMasterTxTest, TimerCallback_CompletesSlowTransactionAndStartsNextQueuedPacket )
{
    const uint8_t next_packet[2] = { 0x22U, 0x23U };
    memcpy( &HW_SPI_STATE( SPI_CHANNEL_0 )->tx_buffer[10], next_packet, sizeof( next_packet ) );
    HW_SPI_STATE( SPI_CHANNEL_0 )->tx_packet_descriptors[0].start_index = 10U;
    HW_SPI_STATE( SPI_CHANNEL_0 )->tx_packet_descriptors[0].size_bytes  = sizeof( next_packet );
    HW_SPI_STATE( SPI_CHANNEL_0 )->tx_packet_read_position              = 0U;
    HW_SPI_STATE( SPI_CHANNEL_0 )->tx_packet_write_position             = 1U;
    HW_SPI_STATE( SPI_CHANNEL_0 )->tx_num_packets_pending               = 1U;
    HW_SPI_STATE( SPI_CHANNEL_0 )->tx_num_bytes_pending                 = sizeof( next_packet );
    HW_SPI_STATE( SPI_CHANNEL_0 )->tx_transaction_state = HW_SPI_TX_TRANSACTION_WAIT_FINAL_DRAIN;
    HW_SPI_STATE( SPI_CHANNEL_0 )->tx_final_drain_timer = SPI_CHANNEL_0_TIMER;

    InSequence seq;
    EXPECT_CALL( mock, SPIIsBusy( Eq( SPI_CHANNEL_0_INSTANCE ) ) ).WillOnce( Return( 0U ) );
    ExpectChannel0DmaProgram( &HW_SPI_STATE( SPI_CHANNEL_0 )->tx_buffer[10],
                              sizeof( next_packet ) );

    HW_SPI_Timer_Callback_From_ISR( SPI_CHANNEL_0 );

    EXPECT_EQ( HW_SPI_STATE( SPI_CHANNEL_0 )->tx_transaction_state,
               HW_SPI_TX_TRANSACTION_DMA_ACTIVE );
    EXPECT_EQ( HW_SPI_STATE( SPI_CHANNEL_0 )->tx_num_packets_pending, 0U );
    EXPECT_EQ( HW_SPI_STATE( SPI_CHANNEL_0 )->tx_num_bytes_in_transmission, sizeof( next_packet ) );
}

TEST_F( HWSpiMasterTxTest, TimerCallback_DacRestartsTimerWhenBsyAtFirstCallback )
{
    HW_SPI_STATE( SPI_DAC )->tx_transaction_state          = HW_SPI_TX_TRANSACTION_WAIT_FINAL_DRAIN;
    HW_SPI_STATE( SPI_DAC )->tx_final_drain_timer          = SPI_DAC_TIMER;
    HW_SPI_STATE( SPI_DAC )->tx_final_drain_timer_attempts = 1U;

    InSequence sequence;
    EXPECT_CALL( mock, SPIIsBusy( Eq( SPI_DAC_INSTANCE ) ) ).WillOnce( Return( 1U ) );
    EXPECT_CALL( mock, TimerStart( SPI_DAC_TIMER ) );

    HW_SPI_Timer_Callback_From_ISR( SPI_DAC );

    EXPECT_EQ( HW_SPI_STATE( SPI_DAC )->tx_transaction_state,
               HW_SPI_TX_TRANSACTION_WAIT_FINAL_DRAIN );
    EXPECT_EQ( HW_SPI_STATE( SPI_DAC )->tx_final_drain_timer_attempts, 2U );
}

TEST_F( HWSpiMasterTxTest, TimerCallback_DacFaultsAfterBoundedDrainWithCsHeld )
{
    HW_SPI_STATE( SPI_DAC )->tx_transaction_state          = HW_SPI_TX_TRANSACTION_WAIT_FINAL_DRAIN;
    HW_SPI_STATE( SPI_DAC )->tx_final_drain_timer_attempts = SPI_DAC_FINAL_DRAIN_TIMER_MAX_ATTEMPTS;

    InSequence sequence;
    EXPECT_CALL( mock, SPIIsBusy( Eq( SPI_DAC_INSTANCE ) ) ).WillOnce( Return( 1U ) );
    EXPECT_CALL( mock, SPIDisableDMAReqTX( Eq( SPI_DAC_INSTANCE ) ) );

    HW_SPI_Timer_Callback_From_ISR( SPI_DAC );

    EXPECT_EQ( HW_SPI_STATE( SPI_DAC )->tx_transaction_state, HW_SPI_TX_TRANSACTION_ERROR );
    EXPECT_EQ( HW_SPI_STATE( SPI_DAC )->tx_final_drain_timer_attempts, 0U );
}

TEST_F( HWSpiMasterTxTest, TimerCallback_IgnoresStaleCallbackWhileDmaActive )
{
    HW_SPI_STATE( SPI_CHANNEL_0 )->tx_transaction_state         = HW_SPI_TX_TRANSACTION_DMA_ACTIVE;
    HW_SPI_STATE( SPI_CHANNEL_0 )->tx_num_bytes_in_transmission = 1U;

    HW_SPI_Timer_Callback_From_ISR( SPI_CHANNEL_0 );

    EXPECT_EQ( HW_SPI_STATE( SPI_CHANNEL_0 )->tx_transaction_state,
               HW_SPI_TX_TRANSACTION_DMA_ACTIVE );
    EXPECT_EQ( HW_SPI_STATE( SPI_CHANNEL_0 )->tx_num_bytes_in_transmission, 1U );
}

TEST_F( HWSpiMasterTxTest, TxDmaIrq_TransferErrorWinsOverTransferComplete )
{
    HW_SPI_STATE( SPI_CHANNEL_0 )->tx_transaction_state         = HW_SPI_TX_TRANSACTION_DMA_ACTIVE;
    HW_SPI_STATE( SPI_CHANNEL_0 )->tx_num_bytes_in_transmission = 4U;

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

    EXPECT_EQ( HW_SPI_STATE( SPI_CHANNEL_0 )->tx_num_bytes_in_transmission, 0U );
    EXPECT_EQ( HW_SPI_STATE( SPI_CHANNEL_0 )->tx_transaction_state, HW_SPI_TX_TRANSACTION_ERROR );
}

TEST_F( HWSpiMasterTxTest, Master16BitTriggerProgramsDmaInHalfwordElements )
{
    InitialiseState( HW_SPI_STATE( SPI_CHANNEL_0 ), SPI_CHANNEL_0,
                     MakeMasterConfig( SPI_SIZE_16_BIT ), SPI_CHANNEL_0_RX_DMA,
                     SPI_CHANNEL_0_RX_DMA_STREAM, SPI_CHANNEL_0_TX_DMA, SPI_CHANNEL_0_TX_DMA_STREAM,
                     SPI_CHANNEL_0_INSTANCE, SPI_CHANNEL_0_TX_DMA_IRQN, SPI_CHANNEL_0_TIMER );
    const uint8_t data[4] = { 0x01U, 0x02U, 0x03U, 0x04U };

    EXPECT_CALL( mock, NVICDisableIRQ( SPI_CHANNEL_0_TX_DMA_IRQN ) );
    EXPECT_CALL( mock, NVICEnableIRQ( SPI_CHANNEL_0_TX_DMA_IRQN ) );
    EXPECT_TRUE( HW_SPI_Load_Tx_Buffer( SPI_CHANNEL_0, data, sizeof( data ) ) );
    testing::Mock::VerifyAndClearExpectations( &mock );

    InSequence seq;
    EXPECT_CALL( mock, NVICDisableIRQ( SPI_CHANNEL_0_TX_DMA_IRQN ) );
    ExpectChannel0DmaProgram( &HW_SPI_STATE( SPI_CHANNEL_0 )->tx_buffer[0], 2U );
    EXPECT_CALL( mock, NVICEnableIRQ( SPI_CHANNEL_0_TX_DMA_IRQN ) );

    HW_SPI_Tx_Trigger( SPI_CHANNEL_0 );

    EXPECT_EQ( HW_SPI_STATE( SPI_CHANNEL_0 )->tx_num_bytes_in_transmission, sizeof( data ) );
}

/**
 * @brief Confirms that the public state lookup follows the SPIChannel_T index
 *        mapping used by the state-array refactor.
 *
 * This catches regressions where a future channel reorder or array definition
 * accidentally maps a logical channel to the wrong state object.
 */
TEST_F( HWSpiMasterTxTest, StateArray_GetStateReturnsMatchingArrayEntry )
{
    EXPECT_EQ( HW_SPI_Get_State( SPI_CHANNEL_0 ), HW_SPI_STATE( SPI_CHANNEL_0 ) );
    EXPECT_EQ( HW_SPI_Get_State( SPI_CHANNEL_1 ), HW_SPI_STATE( SPI_CHANNEL_1 ) );
    EXPECT_EQ( HW_SPI_Get_State( SPI_DAC ), HW_SPI_STATE( SPI_DAC ) );
    EXPECT_EQ( HW_SPI_Get_State( static_cast<SPIChannel_T>( SPI_NUM_CHANNELS ) ), nullptr );
}

/**
 * @brief TX completion is stricter than software-buffer empty.
 *
 * The queue and DMA byte counters can be zero before the master transaction is
 * actually complete. The completion helper must also wait for BSY to clear and
 * for the master final-drain state machine to return to IDLE.
 */
TEST_F( HWSpiMasterTxTest, TxIsComplete_MasterRequiresEmptyCountsClearBsyAndIdleState )
{
    SPIPeripheralState_T* state = HW_SPI_STATE( SPI_CHANNEL_0 );

    state->tx_num_bytes_pending         = 0U;
    state->tx_num_bytes_in_transmission = 0U;
    state->tx_transaction_state         = HW_SPI_TX_TRANSACTION_IDLE;

    EXPECT_CALL( mock, SPIIsBusy( Eq( SPI_CHANNEL_0_INSTANCE ) ) ).WillOnce( Return( 1U ) );
    EXPECT_FALSE( HW_SPI_Tx_Is_Complete( SPI_CHANNEL_0 ) );
    testing::Mock::VerifyAndClearExpectations( &mock );

    state->tx_transaction_state = HW_SPI_TX_TRANSACTION_WAIT_FINAL_DRAIN;
    EXPECT_CALL( mock, SPIIsBusy( Eq( SPI_CHANNEL_0_INSTANCE ) ) ).WillOnce( Return( 0U ) );
    EXPECT_FALSE( HW_SPI_Tx_Is_Complete( SPI_CHANNEL_0 ) );
    testing::Mock::VerifyAndClearExpectations( &mock );

    state->tx_transaction_state = HW_SPI_TX_TRANSACTION_IDLE;
    EXPECT_CALL( mock, SPIIsBusy( Eq( SPI_CHANNEL_0_INSTANCE ) ) ).WillOnce( Return( 0U ) );
    EXPECT_TRUE( HW_SPI_Tx_Is_Complete( SPI_CHANNEL_0 ) );
}

/**
 * @brief Master descriptor queue indices must wrap independently of TX buffer indices.
 *
 * This exercises the descriptor ring boundary without involving DMA. It protects
 * the packet queue ownership model used by the master CS-framed transfer path.
 */
TEST_F( HWSpiMasterTxTest, LoadTxBuffer_MasterDescriptorWriteIndexWrapsAtQueueDepth )
{
    const uint8_t first[1]  = { 0xA1U };
    const uint8_t second[1] = { 0xB2U };

    HW_SPI_STATE( SPI_CHANNEL_0 )->tx_packet_write_position = TX_PACKET_QUEUE_DEPTH - 1U;

    EXPECT_CALL( mock, NVICDisableIRQ( SPI_CHANNEL_0_TX_DMA_IRQN ) ).Times( 2 );
    EXPECT_CALL( mock, NVICEnableIRQ( SPI_CHANNEL_0_TX_DMA_IRQN ) ).Times( 2 );

    EXPECT_TRUE( HW_SPI_Load_Tx_Buffer( SPI_CHANNEL_0, first, sizeof( first ) ) );
    EXPECT_TRUE( HW_SPI_Load_Tx_Buffer( SPI_CHANNEL_0, second, sizeof( second ) ) );

    EXPECT_EQ( HW_SPI_STATE( SPI_CHANNEL_0 )
                   ->tx_packet_descriptors[TX_PACKET_QUEUE_DEPTH - 1U]
                   .start_index,
               0U );
    EXPECT_EQ(
        HW_SPI_STATE( SPI_CHANNEL_0 )->tx_packet_descriptors[TX_PACKET_QUEUE_DEPTH - 1U].size_bytes,
        sizeof( first ) );
    EXPECT_EQ( HW_SPI_STATE( SPI_CHANNEL_0 )->tx_packet_descriptors[0].start_index,
               sizeof( first ) );
    EXPECT_EQ( HW_SPI_STATE( SPI_CHANNEL_0 )->tx_packet_descriptors[0].size_bytes,
               sizeof( second ) );
    EXPECT_EQ( HW_SPI_STATE( SPI_CHANNEL_0 )->tx_packet_write_position, 1U );
    EXPECT_EQ( HW_SPI_STATE( SPI_CHANNEL_0 )->tx_num_packets_pending, 2U );
}

/**
 * @brief Master packets must remain contiguous after wrapping to the buffer head.
 *
 * If the tail cannot fit the packet and the free space before tx_read_position is
 * too small, the load must fail rather than splitting one CS-framed transaction
 * across the ring boundary.
 */
TEST_F( HWSpiMasterTxTest, LoadTxBuffer_MasterRejectsWhenWrappedHeadSpaceIsTooSmall )
{
    const uint8_t data[8] = { 0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U };

    HW_SPI_STATE( SPI_CHANNEL_0 )->tx_write_position = TX_BUFFER_SIZE_BYTES - 2U;
    HW_SPI_STATE( SPI_CHANNEL_0 )->tx_read_position  = 4U;

    EXPECT_CALL( mock, NVICDisableIRQ( SPI_CHANNEL_0_TX_DMA_IRQN ) );
    EXPECT_CALL( mock, NVICEnableIRQ( SPI_CHANNEL_0_TX_DMA_IRQN ) );

    EXPECT_FALSE( HW_SPI_Load_Tx_Buffer( SPI_CHANNEL_0, data, sizeof( data ) ) );
    EXPECT_EQ( HW_SPI_STATE( SPI_CHANNEL_0 )->tx_num_packets_pending, 0U );
    EXPECT_EQ( HW_SPI_STATE( SPI_CHANNEL_0 )->tx_num_bytes_pending, 0U );
}

TEST_F( HWSpiMasterTxTest, MasterChannelsDriveOnlyTheirConfiguredCsPins )
{
    HW_SPI_STATE( SPI_CHANNEL_0 )->nss_pin = GPIO_SPI1_NSS;
    HW_SPI_STATE( SPI_CHANNEL_1 )->nss_pin = GPIO_SPI2_NSS;

    HW_SPI_TX_Master_CS_Assert( HW_SPI_STATE( SPI_CHANNEL_0 ) );
    HW_SPI_TX_Master_CS_Assert( HW_SPI_STATE( SPI_CHANNEL_1 ) );
    HW_SPI_TX_Master_CS_Deassert( HW_SPI_STATE( SPI_CHANNEL_0 ) );
    HW_SPI_TX_Master_CS_Deassert( HW_SPI_STATE( SPI_CHANNEL_1 ) );

    ASSERT_EQ( gpio_events.size(), 4U );
    EXPECT_EQ( gpio_events[0].kind, GPIOEventKind::RESET_LOW );
    EXPECT_EQ( gpio_events[0].pin, GPIO_SPI1_NSS );
    EXPECT_EQ( gpio_events[1].kind, GPIOEventKind::RESET_LOW );
    EXPECT_EQ( gpio_events[1].pin, GPIO_SPI2_NSS );
    EXPECT_EQ( gpio_events[2].kind, GPIOEventKind::SET_HIGH );
    EXPECT_EQ( gpio_events[2].pin, GPIO_SPI1_NSS );
    EXPECT_EQ( gpio_events[3].kind, GPIOEventKind::SET_HIGH );
    EXPECT_EQ( gpio_events[3].pin, GPIO_SPI2_NSS );
}

TEST_F( HWSpiMasterTxTest, MasterConfigurationPreloadsSelectedCsHighAndStoresIt )
{
    HWSPIConfig_T config = MakeMasterConfig();
    config.nss_pin       = GPIO_SPI1_NSS;
    memset( HW_SPI_STATE( SPI_CHANNEL_0 ), 0, sizeof( *HW_SPI_STATE( SPI_CHANNEL_0 ) ) );

    ExpectChannel0ConfigurationHardware();

    ASSERT_TRUE( HW_SPI_Configure_Channel( SPI_CHANNEL_0, config ) );

    ASSERT_EQ( gpio_events.size(), 1U );
    EXPECT_EQ( gpio_events[0].kind, GPIOEventKind::CONFIGURE_OUTPUT );
    EXPECT_EQ( gpio_events[0].pin, GPIO_SPI1_NSS );
    EXPECT_TRUE( gpio_events[0].initial_high );
    EXPECT_EQ( SPI_CHANNEL_0_HANDLE.Init.NSS, SPI_NSS_SOFT );
    EXPECT_EQ( HW_SPI_STATE( SPI_CHANNEL_0 )->nss_pin, GPIO_SPI1_NSS );
    EXPECT_FALSE( HW_SPI_STATE( SPI_CHANNEL_0 )->cs_asserted );
    EXPECT_FALSE( HW_SPI_STATE( SPI_CHANNEL_0 )->is_started );
}

TEST_F( HWSpiMasterTxTest, MasterToSlaveReconfigurationReleasesCsThenRestoresHardwareNss )
{
    HWSPIConfig_T config = MakeSlaveConfig();
    config.nss_pin       = GPIO_SPI1_NSS;

    ExpectChannel0ConfigurationHardware();

    ASSERT_TRUE( HW_SPI_Configure_Channel( SPI_CHANNEL_0, config ) );

    ASSERT_EQ( gpio_events.size(), 2U );
    EXPECT_EQ( gpio_events[0].kind, GPIOEventKind::SET_HIGH );
    EXPECT_EQ( gpio_events[0].pin, GPIO_SPI1_NSS );
    EXPECT_EQ( gpio_events[1].kind, GPIOEventKind::CONFIGURE_ALTERNATE );
    EXPECT_EQ( gpio_events[1].pin, GPIO_SPI1_NSS );
    EXPECT_EQ( SPI_CHANNEL_0_HANDLE.Init.NSS, SPI_NSS_HARD_INPUT );
    EXPECT_FALSE( HW_SPI_STATE( SPI_CHANNEL_0 )->is_master );
}

TEST_F( HWSpiMasterTxTest, SlaveToMasterReconfigurationCreatesInactiveHighCsWithoutLowPulse )
{
    InitialiseState( HW_SPI_STATE( SPI_CHANNEL_0 ), SPI_CHANNEL_0, MakeSlaveConfig(),
                     SPI_CHANNEL_0_RX_DMA, SPI_CHANNEL_0_RX_DMA_STREAM, SPI_CHANNEL_0_TX_DMA,
                     SPI_CHANNEL_0_TX_DMA_STREAM, SPI_CHANNEL_0_INSTANCE, SPI_CHANNEL_0_TX_DMA_IRQN,
                     SPI_CHANNEL_0_TIMER );
    HWSPIConfig_T config = MakeMasterConfig();
    config.nss_pin       = GPIO_SPI1_NSS;

    gpio_events.clear();
    ExpectChannel0ConfigurationHardware();

    ASSERT_TRUE( HW_SPI_Configure_Channel( SPI_CHANNEL_0, config ) );

    ASSERT_EQ( gpio_events.size(), 1U );
    EXPECT_EQ( gpio_events[0].kind, GPIOEventKind::CONFIGURE_OUTPUT );
    EXPECT_EQ( gpio_events[0].pin, GPIO_SPI1_NSS );
    EXPECT_TRUE( gpio_events[0].initial_high );
    EXPECT_FALSE( HW_SPI_STATE( SPI_CHANNEL_0 )->cs_asserted );
}

TEST_F( HWSpiMasterTxTest, InvalidSlavePinCombinationIsRejectedWithoutHardwareChanges )
{
    HWSPIConfig_T config = MakeSlaveConfig();

    config.nss_pin = GPIO_SPI2_NSS;
    EXPECT_FALSE( HW_SPI_Configure_Channel( SPI_CHANNEL_0, config ) );

    EXPECT_TRUE( gpio_events.empty() );
    EXPECT_TRUE( HW_SPI_STATE( SPI_CHANNEL_0 )->is_master );
    EXPECT_EQ( HW_SPI_STATE( SPI_CHANNEL_0 )->nss_pin, GPIO_SPI1_NSS );
}

TEST_F( HWSpiMasterTxTest, ReconfigurationIsRejectedWhileTransactionOwnsCs )
{
    HWSPIConfig_T config = MakeMasterConfig();
    config.nss_pin       = GPIO_SPI1_NSS;

    HW_SPI_STATE( SPI_CHANNEL_0 )->tx_transaction_state = HW_SPI_TX_TRANSACTION_DMA_ACTIVE;
    HW_SPI_STATE( SPI_CHANNEL_0 )->cs_asserted          = true;

    EXPECT_FALSE( HW_SPI_Configure_Channel( SPI_CHANNEL_0, config ) );
    EXPECT_TRUE( gpio_events.empty() );
    EXPECT_EQ( HW_SPI_STATE( SPI_CHANNEL_0 )->nss_pin, GPIO_SPI1_NSS );
}

TEST_F( HWSpiMasterTxTest, SlaveModeNeverUsesSoftwareCsOperations )
{
    InitialiseState( HW_SPI_STATE( SPI_CHANNEL_0 ), SPI_CHANNEL_0, MakeSlaveConfig(),
                     SPI_CHANNEL_0_RX_DMA, SPI_CHANNEL_0_RX_DMA_STREAM, SPI_CHANNEL_0_TX_DMA,
                     SPI_CHANNEL_0_TX_DMA_STREAM, SPI_CHANNEL_0_INSTANCE, SPI_CHANNEL_0_TX_DMA_IRQN,
                     SPI_CHANNEL_0_TIMER );

    gpio_events.clear();
    HW_SPI_TX_Master_CS_Assert( HW_SPI_STATE( SPI_CHANNEL_0 ) );
    HW_SPI_TX_Master_CS_Deassert( HW_SPI_STATE( SPI_CHANNEL_0 ) );

    EXPECT_TRUE( gpio_events.empty() );
    EXPECT_FALSE( HW_SPI_STATE( SPI_CHANNEL_0 )->cs_asserted );
}

TEST_F( HWSpiMasterTxTest, CsStaysAssertedUntilSlowFinalDrainCompletes )
{
    const uint8_t data                                       = 0x5AU;
    HW_SPI_STATE( SPI_CHANNEL_0 )->tx_uses_final_drain_timer = true;

    EXPECT_CALL( mock, NVICDisableIRQ( SPI_CHANNEL_0_TX_DMA_IRQN ) );
    EXPECT_CALL( mock, NVICEnableIRQ( SPI_CHANNEL_0_TX_DMA_IRQN ) );
    ASSERT_TRUE( HW_SPI_Load_Tx_Buffer( SPI_CHANNEL_0, &data, sizeof( data ) ) );
    testing::Mock::VerifyAndClearExpectations( &mock );

    EXPECT_CALL( mock, NVICDisableIRQ( SPI_CHANNEL_0_TX_DMA_IRQN ) );
    ExpectChannel0DmaProgram( &HW_SPI_STATE( SPI_CHANNEL_0 )->tx_buffer[0], 1U );
    EXPECT_CALL( mock, NVICEnableIRQ( SPI_CHANNEL_0_TX_DMA_IRQN ) );

    gpio_events.clear();
    HW_SPI_Tx_Trigger( SPI_CHANNEL_0 );

    ASSERT_EQ( gpio_events.size(), 2U );
    EXPECT_EQ( gpio_events[0].kind, GPIOEventKind::RESET_LOW );
    EXPECT_EQ( gpio_events[0].pin, GPIO_SPI1_NSS );
    EXPECT_EQ( gpio_events[1].kind, GPIOEventKind::DMA_ARM );
    EXPECT_TRUE( HW_SPI_STATE( SPI_CHANNEL_0 )->cs_asserted );
    testing::Mock::VerifyAndClearExpectations( &mock );

    EXPECT_CALL( mock, DMAIsActiveFlagTE5( Eq( SPI_CHANNEL_0_TX_DMA ) ) ).WillOnce( Return( 0U ) );
    EXPECT_CALL( mock, DMAIsActiveFlagTC5( Eq( SPI_CHANNEL_0_TX_DMA ) ) ).WillOnce( Return( 1U ) );
    EXPECT_CALL( mock, DMAClearFlagTC5( Eq( SPI_CHANNEL_0_TX_DMA ) ) );
    EXPECT_CALL( mock, SPIDisableDMAReqTX( Eq( SPI_CHANNEL_0_INSTANCE ) ) );
    EXPECT_CALL( mock, SPIIsBusy( Eq( SPI_CHANNEL_0_INSTANCE ) ) ).WillOnce( Return( 1U ) );
    EXPECT_CALL( mock, TimerStart( SPI_CHANNEL_0_TIMER ) );

    gpio_events.clear();
    SPI_CHANNEL_0_TX_DMA_IRQ();

    EXPECT_TRUE( gpio_events.empty() );
    EXPECT_TRUE( HW_SPI_STATE( SPI_CHANNEL_0 )->cs_asserted );
    EXPECT_EQ( HW_SPI_STATE( SPI_CHANNEL_0 )->tx_transaction_state,
               HW_SPI_TX_TRANSACTION_WAIT_FINAL_DRAIN );
    testing::Mock::VerifyAndClearExpectations( &mock );

    EXPECT_CALL( mock, SPIIsBusy( Eq( SPI_CHANNEL_0_INSTANCE ) ) ).WillOnce( Return( 0U ) );
    HW_SPI_Timer_Callback_From_ISR( SPI_CHANNEL_0 );

    ASSERT_EQ( gpio_events.size(), 1U );
    EXPECT_EQ( gpio_events[0].kind, GPIOEventKind::SET_HIGH );
    EXPECT_EQ( gpio_events[0].pin, GPIO_SPI1_NSS );
    EXPECT_FALSE( HW_SPI_STATE( SPI_CHANNEL_0 )->cs_asserted );
    EXPECT_EQ( HW_SPI_STATE( SPI_CHANNEL_0 )->tx_transaction_state, HW_SPI_TX_TRANSACTION_IDLE );
}

TEST_F( HWSpiMasterTxTest, StopReleasesTheConfiguredMasterCs )
{
    HW_SPI_STATE( SPI_CHANNEL_0 )->nss_pin              = GPIO_SPI4_NSS;
    HW_SPI_STATE( SPI_CHANNEL_0 )->is_started           = true;
    HW_SPI_STATE( SPI_CHANNEL_0 )->cs_asserted          = true;
    HW_SPI_STATE( SPI_CHANNEL_0 )->tx_transaction_state = HW_SPI_TX_TRANSACTION_DMA_ACTIVE;

    EXPECT_CALL( mock, SPIDMAStop( Eq( &SPI_CHANNEL_0_HANDLE ) ) ).WillOnce( Return( HAL_OK ) );

    ASSERT_TRUE( HW_SPI_Stop_Channel( SPI_CHANNEL_0 ) );

    ASSERT_EQ( gpio_events.size(), 1U );
    EXPECT_EQ( gpio_events[0].kind, GPIOEventKind::SET_HIGH );
    EXPECT_EQ( gpio_events[0].pin, GPIO_SPI4_NSS );
    EXPECT_FALSE( HW_SPI_STATE( SPI_CHANNEL_0 )->cs_asserted );
    EXPECT_FALSE( HW_SPI_STATE( SPI_CHANNEL_0 )->is_started );
}

TEST_F( HWSpiMasterTxTest, StopRejectsUnconfiguredChannel )
{
    HW_SPI_STATE( SPI_CHANNEL_0 )->is_configured = false;
    HW_SPI_STATE( SPI_CHANNEL_0 )->is_started    = false;

    EXPECT_FALSE( HW_SPI_Stop_Channel( SPI_CHANNEL_0 ) );
}

TEST_F( HWSpiMasterTxTest, StopRejectsConfiguredButStoppedChannel )
{
    EXPECT_TRUE( HW_SPI_STATE( SPI_CHANNEL_0 )->is_configured );
    EXPECT_FALSE( HW_SPI_STATE( SPI_CHANNEL_0 )->is_started );

    EXPECT_FALSE( HW_SPI_Stop_Channel( SPI_CHANNEL_0 ) );
}

TEST_F( HWSpiMasterTxTest, StopFailureRetainsStartedStateForRetry )
{
    HW_SPI_STATE( SPI_CHANNEL_0 )->is_started  = true;
    HW_SPI_STATE( SPI_CHANNEL_0 )->cs_asserted = true;

    EXPECT_CALL( mock, SPIDMAStop( Eq( &SPI_CHANNEL_0_HANDLE ) ) ).WillOnce( Return( HAL_ERROR ) );

    EXPECT_FALSE( HW_SPI_Stop_Channel( SPI_CHANNEL_0 ) );
    EXPECT_TRUE( HW_SPI_STATE( SPI_CHANNEL_0 )->is_started );
    EXPECT_FALSE( HW_SPI_STATE( SPI_CHANNEL_0 )->cs_asserted );
}

TEST_F( HWSpiMasterTxTest, TxErrorReleasesTheConfiguredMasterCs )
{
    HW_SPI_STATE( SPI_CHANNEL_0 )->nss_pin                      = GPIO_SPI1_NSS;
    HW_SPI_STATE( SPI_CHANNEL_0 )->cs_asserted                  = true;
    HW_SPI_STATE( SPI_CHANNEL_0 )->tx_transaction_state         = HW_SPI_TX_TRANSACTION_DMA_ACTIVE;
    HW_SPI_STATE( SPI_CHANNEL_0 )->tx_num_bytes_in_transmission = 4U;

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

    HW_SPI_TX_Error_Handler( SPI_CHANNEL_0 );

    ASSERT_EQ( gpio_events.size(), 1U );
    EXPECT_EQ( gpio_events[0].kind, GPIOEventKind::SET_HIGH );
    EXPECT_EQ( gpio_events[0].pin, GPIO_SPI1_NSS );
    EXPECT_EQ( HW_SPI_STATE( SPI_CHANNEL_0 )->tx_transaction_state, HW_SPI_TX_TRANSACTION_ERROR );
}
