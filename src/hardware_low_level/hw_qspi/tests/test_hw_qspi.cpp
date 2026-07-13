/******************************************************************************
 *  File:       test_<module>.cpp
 *  Author:     <your name>
 *  Created:    <DD-MMM-YYYY>
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
#include "hw_qspi_mocks.h"
#include <stdint.h>
#include <stdbool.h>
#include "hw_qspi.h"

#include "hw_qspi.c" /* Module under test */  // NOLINT
}

/**-----------------------------------------------------------------------------
 *  Test Constants / Macros
 *------------------------------------------------------------------------------
 */

using ::testing::_;
using ::testing::Eq;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;

static constexpr uint32_t TEST_TRANSFER_DATA_LENGTH = 16U;

/**-----------------------------------------------------------------------------
 *  Test Doubles / Mocks
 *------------------------------------------------------------------------------
 */

class MockHWQSPI
{
public:
    MOCK_METHOD( HAL_StatusTypeDef, Init, ( QSPI_HandleTypeDef * hqspi ), () );
    MOCK_METHOD( HAL_StatusTypeDef, Command,
                 ( QSPI_HandleTypeDef * hqspi, QSPI_CommandTypeDef* cmd, uint32_t timeout ), () );
    MOCK_METHOD( HAL_StatusTypeDef, Transmit,
                 ( QSPI_HandleTypeDef * hqspi, uint8_t* p_data, uint32_t timeout ), () );
    MOCK_METHOD( HAL_StatusTypeDef, Receive,
                 ( QSPI_HandleTypeDef * hqspi, uint8_t* p_data, uint32_t timeout ), () );
    MOCK_METHOD( HAL_StatusTypeDef, TransmitDma, ( QSPI_HandleTypeDef * hqspi, uint8_t* p_data ),
                 () );
    MOCK_METHOD( HAL_StatusTypeDef, ReceiveDma, ( QSPI_HandleTypeDef * hqspi, uint8_t* p_data ),
                 () );
    MOCK_METHOD( HAL_QSPI_StateTypeDef, GetState, ( const QSPI_HandleTypeDef* hqspi ), () );
    MOCK_METHOD( HAL_StatusTypeDef, Abort, ( QSPI_HandleTypeDef * hqspi ), () );
};

static MockHWQSPI* g_mock = nullptr;

// NOLINTBEGIN
extern "C" HAL_StatusTypeDef HAL_QSPI_Init( QSPI_HandleTypeDef* hqspi )
{
    if ( g_mock == nullptr )
    {
        return HAL_ERROR;
    }

    return g_mock->Init( hqspi );
}

extern "C" HAL_StatusTypeDef HAL_QSPI_Command( QSPI_HandleTypeDef* hqspi, QSPI_CommandTypeDef* cmd,
                                               uint32_t timeout )
{
    if ( g_mock == nullptr )
    {
        return HAL_ERROR;
    }

    return g_mock->Command( hqspi, cmd, timeout );
}

extern "C" HAL_StatusTypeDef HAL_QSPI_Transmit( QSPI_HandleTypeDef* hqspi, uint8_t* p_data,
                                                uint32_t timeout )
{
    if ( g_mock == nullptr )
    {
        return HAL_ERROR;
    }

    return g_mock->Transmit( hqspi, p_data, timeout );
}

extern "C" HAL_StatusTypeDef HAL_QSPI_Receive( QSPI_HandleTypeDef* hqspi, uint8_t* p_data,
                                               uint32_t timeout )
{
    if ( g_mock == nullptr )
    {
        return HAL_ERROR;
    }

    return g_mock->Receive( hqspi, p_data, timeout );
}

extern "C" HAL_StatusTypeDef HAL_QSPI_Transmit_DMA( QSPI_HandleTypeDef* hqspi, uint8_t* p_data )
{
    if ( g_mock == nullptr )
    {
        return HAL_ERROR;
    }

    return g_mock->TransmitDma( hqspi, p_data );
}

extern "C" HAL_StatusTypeDef HAL_QSPI_Receive_DMA( QSPI_HandleTypeDef* hqspi, uint8_t* p_data )
{
    if ( g_mock == nullptr )
    {
        return HAL_ERROR;
    }

    return g_mock->ReceiveDma( hqspi, p_data );
}

extern "C" HAL_QSPI_StateTypeDef HAL_QSPI_GetState( const QSPI_HandleTypeDef* hqspi )
{
    if ( g_mock == nullptr )
    {
        return HAL_QSPI_STATE_ERROR;
    }

    return g_mock->GetState( hqspi );
}

extern "C" HAL_StatusTypeDef HAL_QSPI_Abort( QSPI_HandleTypeDef* hqspi )
{
    if ( g_mock == nullptr )
    {
        return HAL_ERROR;
    }

    return g_mock->Abort( hqspi );
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
class HWQSPITest : public ::testing::Test
{
protected:
    NiceMock<MockHWQSPI> mock;

    QSPI_HandleTypeDef qspi_hal_handle                          = {};
    HW_QSPI_Config_T   qspi_config                              = {};
    HW_QSPI_Command_T  qspi_command                             = {};
    uint8_t            transfer_data[TEST_TRANSFER_DATA_LENGTH] = {};

    void SetUp( void ) override
    {
        g_mock = &mock;

        qspi_handle             = nullptr;
        qspi_default_timeout_ms = 0U;
        qspi_transfer_state     = HW_QSPI_TRANSFER_IDLE;

        qspi_hal_handle = {};
        qspi_config     = {};
        qspi_command    = {};

        qspi_config.hal_handle            = &qspi_hal_handle;
        qspi_config.clock_prescaler       = 17U;
        qspi_config.fifo_threshold        = 4U;
        qspi_config.sample_shifting       = 0U;
        qspi_config.flash_size            = 28U;
        qspi_config.chip_select_high_time = 4U;
        qspi_config.clock_mode            = 0U;
        qspi_config.default_timeout_ms    = 50U;

        qspi_command.instruction           = 0x6BU;
        qspi_command.instruction_lines     = HW_QSPI_LINES_1;
        qspi_command.address               = 0x00123456U;
        qspi_command.address_size          = HW_QSPI_ADDR_24_BITS;
        qspi_command.address_lines         = HW_QSPI_LINES_1;
        qspi_command.alternate_bytes       = 0xA5U;
        qspi_command.alternate_bytes_size  = HW_QSPI_ALT_BYTES_8_BITS;
        qspi_command.alternate_bytes_lines = HW_QSPI_LINES_4;
        qspi_command.dummy_cycles          = 8U;
        qspi_command.data_lines            = HW_QSPI_LINES_4;
        qspi_command.timeout_ms            = 25U;

        for ( uint32_t i = 0U; i < TEST_TRANSFER_DATA_LENGTH; i++ )
        {
            transfer_data[i] = static_cast<uint8_t>( i );
        }

        ON_CALL( mock, GetState( _ ) ).WillByDefault( Return( HAL_QSPI_STATE_READY ) );
    }

    void TearDown( void ) override
    {
        g_mock = nullptr;
    }

    void InitDriver( void )
    {
        EXPECT_CALL( mock, Init( Eq( &qspi_hal_handle ) ) ).WillOnce( Return( HAL_OK ) );

        EXPECT_EQ( HW_QSPI_STATUS_OK, HW_QSPI_Init( &qspi_config ) );
    }
};

/**-----------------------------------------------------------------------------
 *  Test Cases
 *------------------------------------------------------------------------------
 */

TEST_F( HWQSPITest, InitRejectsNullConfig )
{
    EXPECT_CALL( mock, Init( _ ) ).Times( 0 );

    EXPECT_EQ( HW_QSPI_STATUS_INVALID_ARG, HW_QSPI_Init( nullptr ) );
}

TEST_F( HWQSPITest, InitRejectsNullHalHandle )
{
    qspi_config.hal_handle = nullptr;

    EXPECT_CALL( mock, Init( _ ) ).Times( 0 );

    EXPECT_EQ( HW_QSPI_STATUS_INVALID_ARG, HW_QSPI_Init( &qspi_config ) );
}

TEST_F( HWQSPITest, InitAppliesConfigToHalHandle )
{
    EXPECT_CALL( mock, Init( Eq( &qspi_hal_handle ) ) ).WillOnce( Return( HAL_OK ) );

    EXPECT_EQ( HW_QSPI_STATUS_OK, HW_QSPI_Init( &qspi_config ) );

    EXPECT_EQ( qspi_hal_handle.Init.ClockPrescaler, qspi_config.clock_prescaler );
    EXPECT_EQ( qspi_hal_handle.Init.FifoThreshold, qspi_config.fifo_threshold );
    EXPECT_EQ( qspi_hal_handle.Init.SampleShifting, qspi_config.sample_shifting );
    EXPECT_EQ( qspi_hal_handle.Init.FlashSize, qspi_config.flash_size );
    EXPECT_EQ( qspi_hal_handle.Init.ChipSelectHighTime, qspi_config.chip_select_high_time );
    EXPECT_EQ( qspi_hal_handle.Init.ClockMode, qspi_config.clock_mode );
    EXPECT_EQ( qspi_hal_handle.Init.FlashID, QSPI_FLASH_ID_1 );
    EXPECT_EQ( qspi_hal_handle.Init.DualFlash, QSPI_DUALFLASH_DISABLE );
}

TEST_F( HWQSPITest, CommandRejectsCallBeforeInit )
{
    EXPECT_CALL( mock, Command( _, _, _ ) ).Times( 0 );

    EXPECT_EQ( HW_QSPI_STATUS_NOT_INITIALISED, HW_QSPI_Command( &qspi_command ) );
}

TEST_F( HWQSPITest, TransfersRejectInvalidArguments )
{
    InitDriver();

    EXPECT_CALL( mock, Command( _, _, _ ) ).Times( 0 );
    EXPECT_CALL( mock, Transmit( _, _, _ ) ).Times( 0 );
    EXPECT_CALL( mock, Receive( _, _, _ ) ).Times( 0 );
    EXPECT_CALL( mock, TransmitDma( _, _ ) ).Times( 0 );
    EXPECT_CALL( mock, ReceiveDma( _, _ ) ).Times( 0 );

    EXPECT_EQ( HW_QSPI_STATUS_INVALID_ARG,
               HW_QSPI_WriteBlocking( nullptr, transfer_data, TEST_TRANSFER_DATA_LENGTH ) );
    EXPECT_EQ( HW_QSPI_STATUS_INVALID_ARG,
               HW_QSPI_WriteBlocking( &qspi_command, nullptr, TEST_TRANSFER_DATA_LENGTH ) );
    EXPECT_EQ( HW_QSPI_STATUS_INVALID_ARG,
               HW_QSPI_WriteBlocking( &qspi_command, transfer_data, 0U ) );

    EXPECT_EQ( HW_QSPI_STATUS_INVALID_ARG,
               HW_QSPI_ReadBlocking( nullptr, transfer_data, TEST_TRANSFER_DATA_LENGTH ) );
    EXPECT_EQ( HW_QSPI_STATUS_INVALID_ARG,
               HW_QSPI_ReadBlocking( &qspi_command, nullptr, TEST_TRANSFER_DATA_LENGTH ) );
    EXPECT_EQ( HW_QSPI_STATUS_INVALID_ARG,
               HW_QSPI_ReadBlocking( &qspi_command, transfer_data, 0U ) );

    EXPECT_EQ( HW_QSPI_STATUS_INVALID_ARG,
               HW_QSPI_WriteDma( nullptr, transfer_data, TEST_TRANSFER_DATA_LENGTH ) );
    EXPECT_EQ( HW_QSPI_STATUS_INVALID_ARG,
               HW_QSPI_WriteDma( &qspi_command, nullptr, TEST_TRANSFER_DATA_LENGTH ) );
    EXPECT_EQ( HW_QSPI_STATUS_INVALID_ARG, HW_QSPI_WriteDma( &qspi_command, transfer_data, 0U ) );

    EXPECT_EQ( HW_QSPI_STATUS_INVALID_ARG,
               HW_QSPI_ReadDma( nullptr, transfer_data, TEST_TRANSFER_DATA_LENGTH ) );
    EXPECT_EQ( HW_QSPI_STATUS_INVALID_ARG,
               HW_QSPI_ReadDma( &qspi_command, nullptr, TEST_TRANSFER_DATA_LENGTH ) );
    EXPECT_EQ( HW_QSPI_STATUS_INVALID_ARG, HW_QSPI_ReadDma( &qspi_command, transfer_data, 0U ) );
}

TEST_F( HWQSPITest, TransferReturnsBusyWhenHalIsBusy )
{
    InitDriver();

    EXPECT_CALL( mock, GetState( Eq( &qspi_hal_handle ) ) )
        .WillOnce( Return( HAL_QSPI_STATE_BUSY_INDIRECT_TX ) );
    EXPECT_CALL( mock, Command( _, _, _ ) ).Times( 0 );

    EXPECT_EQ( HW_QSPI_STATUS_BUSY,
               HW_QSPI_ReadDma( &qspi_command, transfer_data, TEST_TRANSFER_DATA_LENGTH ) );
}

TEST_F( HWQSPITest, CommandMapsHalTimeoutStatus )
{
    InitDriver();

    EXPECT_CALL( mock, Command( Eq( &qspi_hal_handle ), _, Eq( qspi_command.timeout_ms ) ) )
        .WillOnce( Return( HAL_TIMEOUT ) );

    EXPECT_EQ( HW_QSPI_STATUS_TIMEOUT, HW_QSPI_Command( &qspi_command ) );
}

TEST_F( HWQSPITest, CommandBuildsExpectedHalCommand )
{
    InitDriver();

    EXPECT_CALL( mock, Command( Eq( &qspi_hal_handle ), _, Eq( qspi_command.timeout_ms ) ) )
        .WillOnce( Invoke(
            [this]( QSPI_HandleTypeDef* hqspi, QSPI_CommandTypeDef* cmd, uint32_t timeout ) {
                ( void )hqspi;
                ( void )timeout;

                EXPECT_EQ( cmd->Instruction, qspi_command.instruction );
                EXPECT_EQ( cmd->InstructionMode, QSPI_INSTRUCTION_1_LINE );
                EXPECT_EQ( cmd->Address, qspi_command.address );
                EXPECT_EQ( cmd->AddressMode, QSPI_ADDRESS_1_LINE );
                EXPECT_EQ( cmd->AddressSize, QSPI_ADDRESS_24_BITS );
                EXPECT_EQ( cmd->AlternateBytes, qspi_command.alternate_bytes );
                EXPECT_EQ( cmd->AlternateByteMode, QSPI_ALTERNATE_BYTES_4_LINES );
                EXPECT_EQ( cmd->AlternateBytesSize, QSPI_ALTERNATE_BYTES_8_BITS );
                EXPECT_EQ( cmd->DummyCycles, qspi_command.dummy_cycles );
                EXPECT_EQ( cmd->DataMode, QSPI_DATA_NONE );
                EXPECT_EQ( cmd->NbData, 0U );
                EXPECT_EQ( cmd->DdrMode, QSPI_DDR_MODE_DISABLE );
                EXPECT_EQ( cmd->DdrHoldHalfCycle, QSPI_DDR_HHC_ANALOG_DELAY );
                EXPECT_EQ( cmd->SIOOMode, QSPI_SIOO_INST_EVERY_CMD );
                return HAL_OK;
            } ) );

    EXPECT_EQ( HW_QSPI_STATUS_OK, HW_QSPI_Command( &qspi_command ) );
}

TEST_F( HWQSPITest, WriteBlockingIssuesCommandThenTransmit )
{
    InitDriver();

    constexpr uint32_t length = 8U;

    EXPECT_CALL( mock, Command( Eq( &qspi_hal_handle ), _, Eq( qspi_command.timeout_ms ) ) )
        .WillOnce( Invoke(
            [length]( QSPI_HandleTypeDef* hqspi, QSPI_CommandTypeDef* cmd, uint32_t timeout ) {
                ( void )hqspi;
                ( void )timeout;

                EXPECT_EQ( cmd->DataMode, QSPI_DATA_4_LINES );
                EXPECT_EQ( cmd->NbData, length );
                return HAL_OK;
            } ) );

    EXPECT_CALL( mock, Transmit( Eq( &qspi_hal_handle ), Eq( transfer_data ),
                                 Eq( qspi_command.timeout_ms ) ) )
        .WillOnce( Return( HAL_OK ) );

    EXPECT_EQ( HW_QSPI_STATUS_OK, HW_QSPI_WriteBlocking( &qspi_command, transfer_data, length ) );
    EXPECT_FALSE( HW_QSPI_IsTransferComplete() );
}

TEST_F( HWQSPITest, ReadBlockingIssuesCommandThenReceive )
{
    InitDriver();

    constexpr uint32_t length = 8U;

    EXPECT_CALL( mock, Command( Eq( &qspi_hal_handle ), _, Eq( qspi_command.timeout_ms ) ) )
        .WillOnce( Return( HAL_OK ) );
    EXPECT_CALL( mock, Receive( Eq( &qspi_hal_handle ), Eq( transfer_data ),
                                Eq( qspi_command.timeout_ms ) ) )
        .WillOnce( Return( HAL_OK ) );

    EXPECT_EQ( HW_QSPI_STATUS_OK, HW_QSPI_ReadBlocking( &qspi_command, transfer_data, length ) );
}

TEST_F( HWQSPITest, WriteDmaStartsTransferAndCompletesOnTxCallback )
{
    InitDriver();

    constexpr uint32_t length = 8U;

    EXPECT_CALL( mock, Command( Eq( &qspi_hal_handle ), _, Eq( qspi_command.timeout_ms ) ) )
        .WillOnce( Return( HAL_OK ) );
    EXPECT_CALL( mock, TransmitDma( Eq( &qspi_hal_handle ), Eq( transfer_data ) ) )
        .WillOnce( Return( HAL_OK ) );

    EXPECT_EQ( HW_QSPI_STATUS_OK, HW_QSPI_WriteDma( &qspi_command, transfer_data, length ) );
    EXPECT_TRUE( HW_QSPI_IsBusy() );
    EXPECT_FALSE( HW_QSPI_IsTransferComplete() );

    HAL_QSPI_TxCpltCallback( &qspi_hal_handle );

    EXPECT_TRUE( HW_QSPI_IsTransferComplete() );
    EXPECT_FALSE( HW_QSPI_IsBusy() );
}

TEST_F( HWQSPITest, ReadDmaStartsTransferAndCompletesOnRxCallback )
{
    InitDriver();

    constexpr uint32_t length = 8U;

    EXPECT_CALL( mock, Command( Eq( &qspi_hal_handle ), _, Eq( qspi_command.timeout_ms ) ) )
        .WillOnce( Return( HAL_OK ) );
    EXPECT_CALL( mock, ReceiveDma( Eq( &qspi_hal_handle ), Eq( transfer_data ) ) )
        .WillOnce( Return( HAL_OK ) );

    EXPECT_EQ( HW_QSPI_STATUS_OK, HW_QSPI_ReadDma( &qspi_command, transfer_data, length ) );
    EXPECT_TRUE( HW_QSPI_IsBusy() );
    EXPECT_FALSE( HW_QSPI_IsTransferComplete() );

    HAL_QSPI_RxCpltCallback( &qspi_hal_handle );

    EXPECT_TRUE( HW_QSPI_IsTransferComplete() );
    EXPECT_FALSE( HW_QSPI_IsBusy() );
}

TEST_F( HWQSPITest, DmaErrorCallbackClearsBusyWithoutCompletingTransfer )
{
    InitDriver();

    constexpr uint32_t length = 8U;

    EXPECT_CALL( mock, Command( Eq( &qspi_hal_handle ), _, Eq( qspi_command.timeout_ms ) ) )
        .WillOnce( Return( HAL_OK ) );
    EXPECT_CALL( mock, TransmitDma( Eq( &qspi_hal_handle ), Eq( transfer_data ) ) )
        .WillOnce( Return( HAL_OK ) );

    EXPECT_EQ( HW_QSPI_STATUS_OK, HW_QSPI_WriteDma( &qspi_command, transfer_data, length ) );

    HAL_QSPI_ErrorCallback( &qspi_hal_handle );

    EXPECT_FALSE( HW_QSPI_IsTransferComplete() );
    EXPECT_FALSE( HW_QSPI_IsBusy() );
}

TEST_F( HWQSPITest, IsBusyReportsHalBusyState )
{
    InitDriver();

    EXPECT_CALL( mock, GetState( Eq( &qspi_hal_handle ) ) )
        .WillOnce( Return( HAL_QSPI_STATE_BUSY_INDIRECT_RX ) );

    EXPECT_TRUE( HW_QSPI_IsBusy() );
}

TEST_F( HWQSPITest, AbortCallsHalAbort )
{
    InitDriver();

    EXPECT_CALL( mock, Abort( Eq( &qspi_hal_handle ) ) ).WillOnce( Return( HAL_OK ) );

    EXPECT_EQ( HW_QSPI_STATUS_OK, HW_QSPI_Abort() );
    EXPECT_FALSE( HW_QSPI_IsTransferComplete() );
}
