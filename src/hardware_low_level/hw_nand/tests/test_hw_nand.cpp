/******************************************************************************
 *  File:       test_hw_nand.cpp
 *  Author:     Callum Rafferty
 *  Created:    5-May-2026
 *
 *  Description:
 *      Unit tests for the hw_nand module using GoogleTest and GoogleMock.
 *      This file validates the public API and NAND command sequencing.
 *
 *  Notes:
 *      - Production code is written in C; tests are written in C++.
 *      - C headers must be included inside an extern "C" block.
 *      - GoogleMock is used to mock the hw_qspi dependency.
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>

extern "C"
{
#include "hw_nand_mocks.h"
#include "hw_nand.h"
#include <stdbool.h>
#include <stdint.h>

#include "hw_nand.c" /* Module under test */  // NOLINT
}

/**-----------------------------------------------------------------------------
 *  Test Constants / Macros
 *------------------------------------------------------------------------------
 */

using ::testing::_;
using ::testing::Eq;
using ::testing::InSequence;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;

static constexpr uint8_t  TEST_READY_STATUS             = 0x00U;
static constexpr uint8_t  TEST_BUSY_STATUS              = 0x01U;
static constexpr uint8_t  TEST_PROGRAM_FAIL_STATUS      = 0x08U;
static constexpr uint8_t  TEST_ERASE_FAIL_STATUS        = 0x04U;
static constexpr uint8_t  TEST_ECC_CORRECTED_STATUS     = 0x10U;
static constexpr uint8_t  TEST_ECC_UNCORRECTABLE_STATUS = 0x30U;
static constexpr uint32_t TEST_PAGE                     = 1234U;
static constexpr uint32_t TEST_BLOCK                    = 7U;
static constexpr uint16_t TEST_COLUMN                   = 32U;
static constexpr uint32_t TEST_LENGTH                   = 16U;

/**-----------------------------------------------------------------------------
 *  Test Doubles / Mocks
 *------------------------------------------------------------------------------
 */

class MockHWNANDQspi
{
public:
    MOCK_METHOD( HW_QSPI_Status_T, Init, ( const HW_QSPI_Config_T* config ), () );
    MOCK_METHOD( HW_QSPI_Status_T, Command, ( const HW_QSPI_Command_T* command ), () );
    MOCK_METHOD( HW_QSPI_Status_T, WriteBlocking,
                 ( const HW_QSPI_Command_T* command, const uint8_t* data, uint32_t length ), () );
    MOCK_METHOD( HW_QSPI_Status_T, ReadBlocking,
                 ( const HW_QSPI_Command_T* command, uint8_t* data, uint32_t length ), () );
    MOCK_METHOD( HW_QSPI_Status_T, WriteDma,
                 ( const HW_QSPI_Command_T* command, const uint8_t* data, uint32_t length ), () );
    MOCK_METHOD( HW_QSPI_Status_T, ReadDma,
                 ( const HW_QSPI_Command_T* command, uint8_t* data, uint32_t length ), () );
    MOCK_METHOD( bool, IsTransferComplete, (), () );
    MOCK_METHOD( bool, IsBusy, (), () );
    MOCK_METHOD( HW_QSPI_Status_T, Abort, (), () );
};

static MockHWNANDQspi* g_mock = nullptr;

// NOLINTBEGIN
extern "C" HW_QSPI_Status_T HW_QSPI_Init( const HW_QSPI_Config_T* config )
{
    if ( g_mock == nullptr )
    {
        return HW_QSPI_STATUS_ERROR;
    }

    return g_mock->Init( config );
}

extern "C" HW_QSPI_Status_T HW_QSPI_Command( const HW_QSPI_Command_T* command )
{
    if ( g_mock == nullptr )
    {
        return HW_QSPI_STATUS_ERROR;
    }

    return g_mock->Command( command );
}

extern "C" HW_QSPI_Status_T HW_QSPI_WriteBlocking( const HW_QSPI_Command_T* command,
                                                   const uint8_t* data, uint32_t length )
{
    if ( g_mock == nullptr )
    {
        return HW_QSPI_STATUS_ERROR;
    }

    return g_mock->WriteBlocking( command, data, length );
}

extern "C" HW_QSPI_Status_T HW_QSPI_ReadBlocking( const HW_QSPI_Command_T* command, uint8_t* data,
                                                  uint32_t length )
{
    if ( g_mock == nullptr )
    {
        return HW_QSPI_STATUS_ERROR;
    }

    return g_mock->ReadBlocking( command, data, length );
}

extern "C" HW_QSPI_Status_T HW_QSPI_WriteDma( const HW_QSPI_Command_T* command, const uint8_t* data,
                                              uint32_t length )
{
    if ( g_mock == nullptr )
    {
        return HW_QSPI_STATUS_ERROR;
    }

    return g_mock->WriteDma( command, data, length );
}

extern "C" HW_QSPI_Status_T HW_QSPI_ReadDma( const HW_QSPI_Command_T* command, uint8_t* data,
                                             uint32_t length )
{
    if ( g_mock == nullptr )
    {
        return HW_QSPI_STATUS_ERROR;
    }

    return g_mock->ReadDma( command, data, length );
}

extern "C" bool HW_QSPI_IsTransferComplete( void )
{
    if ( g_mock == nullptr )
    {
        return false;
    }

    return g_mock->IsTransferComplete();
}

extern "C" bool HW_QSPI_IsBusy( void )
{
    if ( g_mock == nullptr )
    {
        return false;
    }

    return g_mock->IsBusy();
}

extern "C" HW_QSPI_Status_T HW_QSPI_Abort( void )
{
    if ( g_mock == nullptr )
    {
        return HW_QSPI_STATUS_ERROR;
    }

    return g_mock->Abort();
}
// NOLINTEND

/**-----------------------------------------------------------------------------
 *  Test Fixture
 *------------------------------------------------------------------------------
 */

/**
 * @brief Test fixture for the hw_nand module.
 *
 * Resets the private driver state and provides helpers for validating the QSPI
 * commands generated by the NAND command layer.
 */
class HWNANDTest : public ::testing::Test
{
protected:
    NiceMock<MockHWNANDQspi> mock;
    uint8_t                  transfer_data[TEST_LENGTH] = {};

    void SetUp( void ) override
    {
        g_mock = &mock;

        nand_initialised     = false;
        nand_last_ecc_status = HW_NAND_ECC_STATUS_UNKNOWN;

        for ( uint32_t i = 0U; i < TEST_LENGTH; i++ )
        {
            transfer_data[i] = static_cast<uint8_t>( i );
        }
    }

    void TearDown( void ) override
    {
        g_mock = nullptr;
    }

    void SetInitialised( void )
    {
        nand_initialised = true;
    }

    void ExpectOpcodeCommand( uint8_t opcode )
    {
        EXPECT_CALL( mock, Command( _ ) )
            .WillOnce( Invoke( [opcode]( const HW_QSPI_Command_T* command ) {
                EXPECT_EQ( command->instruction, opcode );
                EXPECT_EQ( command->instruction_lines, HW_QSPI_LINES_1 );
                EXPECT_EQ( command->address_lines, HW_QSPI_LINES_NONE );
                EXPECT_EQ( command->address_size, HW_QSPI_ADDR_NONE );
                EXPECT_EQ( command->data_lines, HW_QSPI_LINES_NONE );
                return HW_QSPI_STATUS_OK;
            } ) );
    }

    void ExpectAddressCommand( uint8_t opcode, uint32_t address,
                               HW_QSPI_AddressSize_T address_size )
    {
        EXPECT_CALL( mock, Command( _ ) )
            .WillOnce( Invoke( [opcode, address, address_size]( const HW_QSPI_Command_T* command ) {
                EXPECT_EQ( command->instruction, opcode );
                EXPECT_EQ( command->instruction_lines, HW_QSPI_LINES_1 );
                EXPECT_EQ( command->address, address );
                EXPECT_EQ( command->address_lines, HW_QSPI_LINES_1 );
                EXPECT_EQ( command->address_size, address_size );
                EXPECT_EQ( command->data_lines, HW_QSPI_LINES_NONE );
                return HW_QSPI_STATUS_OK;
            } ) );
    }

    void ExpectStatusRead( uint8_t status_register )
    {
        EXPECT_CALL( mock, ReadBlocking( _, _, Eq( 1U ) ) )
            .WillOnce( Invoke( [status_register]( const HW_QSPI_Command_T* command, uint8_t* data,
                                                  uint32_t length ) {
                EXPECT_EQ( length, 1U );
                EXPECT_EQ( command->instruction, HW_NAND_OPCODE_GET_FEATURE );
                EXPECT_EQ( command->address, HW_NAND_FEATURE_STATUS );
                EXPECT_EQ( command->address_size, HW_QSPI_ADDR_8_BITS );
                EXPECT_EQ( command->address_lines, HW_QSPI_LINES_1 );
                EXPECT_EQ( command->data_lines, HW_QSPI_LINES_1 );
                data[0] = status_register;
                return HW_QSPI_STATUS_OK;
            } ) );
    }

    void ExpectReadId( uint8_t manufacturer_id, uint8_t device_id )
    {
        EXPECT_CALL( mock, ReadBlocking( _, _, Eq( HW_NAND_ID_LENGTH_BYTES ) ) )
            .WillOnce( Invoke( [manufacturer_id, device_id]( const HW_QSPI_Command_T* command,
                                                             uint8_t* data, uint32_t length ) {
                EXPECT_EQ( length, HW_NAND_ID_LENGTH_BYTES );
                EXPECT_EQ( command->instruction, HW_NAND_OPCODE_READ_ID );
                EXPECT_EQ( command->address_lines, HW_QSPI_LINES_NONE );
                EXPECT_EQ( command->address_size, HW_QSPI_ADDR_NONE );
                EXPECT_EQ( command->dummy_cycles, 8U );
                EXPECT_EQ( command->data_lines, HW_QSPI_LINES_1 );
                data[0] = manufacturer_id;
                data[1] = device_id;
                return HW_QSPI_STATUS_OK;
            } ) );
    }

    void ExpectGetFeature( uint8_t feature_address, uint8_t value )
    {
        EXPECT_CALL( mock, ReadBlocking( _, _, Eq( 1U ) ) )
            .WillOnce( Invoke( [feature_address, value]( const HW_QSPI_Command_T* command,
                                                         uint8_t* data, uint32_t length ) {
                EXPECT_EQ( length, 1U );
                EXPECT_EQ( command->instruction, HW_NAND_OPCODE_GET_FEATURE );
                EXPECT_EQ( command->address, feature_address );
                EXPECT_EQ( command->address_size, HW_QSPI_ADDR_8_BITS );
                EXPECT_EQ( command->address_lines, HW_QSPI_LINES_1 );
                EXPECT_EQ( command->data_lines, HW_QSPI_LINES_1 );
                data[0] = value;
                return HW_QSPI_STATUS_OK;
            } ) );
    }

    void ExpectSetFeature( uint8_t feature_address, uint8_t value )
    {
        EXPECT_CALL( mock, WriteBlocking( _, _, Eq( 1U ) ) )
            .WillOnce( Invoke( [feature_address, value]( const HW_QSPI_Command_T* command,
                                                         const uint8_t* data, uint32_t length ) {
                EXPECT_EQ( length, 1U );
                EXPECT_EQ( command->instruction, HW_NAND_OPCODE_SET_FEATURE );
                EXPECT_EQ( command->address, feature_address );
                EXPECT_EQ( command->address_size, HW_QSPI_ADDR_8_BITS );
                EXPECT_EQ( command->address_lines, HW_QSPI_LINES_1 );
                EXPECT_EQ( command->data_lines, HW_QSPI_LINES_1 );
                EXPECT_EQ( data[0], value );
                return HW_QSPI_STATUS_OK;
            } ) );
    }

    void ExpectReadCacheBlocking( uint16_t column, uint8_t marker )
    {
        EXPECT_CALL( mock, ReadBlocking( _, _, Eq( 1U ) ) )
            .WillOnce( Invoke( [column, marker]( const HW_QSPI_Command_T* command, uint8_t* data,
                                                 uint32_t length ) {
                EXPECT_EQ( length, 1U );
                EXPECT_EQ( command->instruction, HW_NAND_OPCODE_READ_CACHE_QUAD );
                EXPECT_EQ( command->address, column );
                EXPECT_EQ( command->address_size, HW_QSPI_ADDR_16_BITS );
                EXPECT_EQ( command->address_lines, HW_QSPI_LINES_1 );
                EXPECT_EQ( command->data_lines, HW_QSPI_LINES_4 );
                EXPECT_EQ( command->dummy_cycles, 8U );
                data[0] = marker;
                return HW_QSPI_STATUS_OK;
            } ) );
    }
};

/**-----------------------------------------------------------------------------
 *  Test Cases
 *------------------------------------------------------------------------------
 */

TEST_F( HWNANDTest, InitResetsVerifiesIdUnlocksBlocksAndEnablesEcc )
{
    InSequence sequence;

    ExpectOpcodeCommand( HW_NAND_OPCODE_RESET );
    ExpectStatusRead( TEST_READY_STATUS );
    ExpectReadId( HW_NAND_EXPECTED_MANUFACTURER_ID, HW_NAND_EXPECTED_DEVICE_ID );
    ExpectSetFeature( HW_NAND_FEATURE_BLOCK_LOCK, HW_NAND_BLOCK_LOCK_UNLOCK_ALL );
    ExpectOpcodeCommand( HW_NAND_OPCODE_WRITE_DISABLE );
    ExpectGetFeature( HW_NAND_FEATURE_BLOCK_LOCK, HW_NAND_BLOCK_LOCK_UNLOCK_ALL );
    ExpectSetFeature( HW_NAND_FEATURE_CONFIGURATION, HW_NAND_CONFIGURATION_ECC_ENABLE_NORMAL_MODE );
    ExpectOpcodeCommand( HW_NAND_OPCODE_WRITE_DISABLE );
    ExpectGetFeature( HW_NAND_FEATURE_CONFIGURATION, HW_NAND_CONFIGURATION_ECC_ENABLE_NORMAL_MODE );

    EXPECT_EQ( HW_NAND_STATUS_OK, HW_NAND_Init() );
}

TEST_F( HWNANDTest, InitRejectsUnsupportedDeviceId )
{
    InSequence sequence;

    ExpectOpcodeCommand( HW_NAND_OPCODE_RESET );
    ExpectStatusRead( TEST_READY_STATUS );
    ExpectReadId( HW_NAND_EXPECTED_MANUFACTURER_ID, 0x14U );

    EXPECT_EQ( HW_NAND_STATUS_UNSUPPORTED_DEVICE, HW_NAND_Init() );
    EXPECT_FALSE( nand_initialised );
}

TEST_F( HWNANDTest, InitRejectsConfigurationWithoutNormalMode )
{
    InSequence sequence;

    ExpectOpcodeCommand( HW_NAND_OPCODE_RESET );
    ExpectStatusRead( TEST_READY_STATUS );
    ExpectReadId( HW_NAND_EXPECTED_MANUFACTURER_ID, HW_NAND_EXPECTED_DEVICE_ID );
    ExpectSetFeature( HW_NAND_FEATURE_BLOCK_LOCK, HW_NAND_BLOCK_LOCK_UNLOCK_ALL );
    ExpectOpcodeCommand( HW_NAND_OPCODE_WRITE_DISABLE );
    ExpectGetFeature( HW_NAND_FEATURE_BLOCK_LOCK, HW_NAND_BLOCK_LOCK_UNLOCK_ALL );
    ExpectSetFeature( HW_NAND_FEATURE_CONFIGURATION, HW_NAND_CONFIGURATION_ECC_ENABLE_NORMAL_MODE );
    ExpectOpcodeCommand( HW_NAND_OPCODE_WRITE_DISABLE );
    ExpectGetFeature( HW_NAND_FEATURE_CONFIGURATION, 0x12U );

    EXPECT_EQ( HW_NAND_STATUS_ERROR, HW_NAND_Init() );
    EXPECT_FALSE( nand_initialised );
}

TEST_F( HWNANDTest, PublicTransactionsRejectCallsBeforeInit )
{
    EXPECT_CALL( mock, Command( _ ) ).Times( 0 );
    EXPECT_CALL( mock, ReadBlocking( _, _, _ ) ).Times( 0 );
    EXPECT_CALL( mock, WriteBlocking( _, _, _ ) ).Times( 0 );

    HW_NAND_Id_T id = {};

    EXPECT_EQ( HW_NAND_STATUS_NOT_INITIALISED, HW_NAND_Reset() );
    EXPECT_EQ( HW_NAND_STATUS_NOT_INITIALISED, HW_NAND_ReadId( &id ) );
    EXPECT_EQ( HW_NAND_STATUS_NOT_INITIALISED, HW_NAND_ReadPageToCache( TEST_PAGE ) );
    EXPECT_EQ( HW_NAND_STATUS_NOT_INITIALISED,
               HW_NAND_ReadCacheBlocking( TEST_COLUMN, transfer_data, TEST_LENGTH ) );
    EXPECT_EQ( HW_NAND_STATUS_NOT_INITIALISED,
               HW_NAND_ProgramLoadBlocking( TEST_COLUMN, transfer_data, TEST_LENGTH ) );
}

TEST_F( HWNANDTest, GetGeometryReportsSelectedDeviceGeometry )
{
    HW_NAND_Geometry_T geometry = {};

    EXPECT_EQ( HW_NAND_STATUS_OK, HW_NAND_GetGeometry( &geometry ) );
    EXPECT_EQ( geometry.page_size_bytes, 2048U );
    EXPECT_EQ( geometry.spare_size_bytes, 128U );
    EXPECT_EQ( geometry.pages_per_block, 64U );
    EXPECT_EQ( geometry.block_count, 4096U );
}

TEST_F( HWNANDTest, StartPageReadToCacheIssuesPageReadWithoutPolling )
{
    SetInitialised();

    ExpectAddressCommand( HW_NAND_OPCODE_PAGE_READ, TEST_PAGE, HW_QSPI_ADDR_24_BITS );
    EXPECT_CALL( mock, ReadBlocking( _, _, _ ) ).Times( 0 );

    EXPECT_EQ( HW_NAND_STATUS_OK, HW_NAND_StartPageReadToCache( TEST_PAGE ) );
}

TEST_F( HWNANDTest, ReadPageToCacheRecordsCorrectedEccStatus )
{
    SetInitialised();
    InSequence sequence;

    ExpectAddressCommand( HW_NAND_OPCODE_PAGE_READ, TEST_PAGE, HW_QSPI_ADDR_24_BITS );
    ExpectStatusRead( TEST_ECC_CORRECTED_STATUS );

    EXPECT_EQ( HW_NAND_STATUS_OK, HW_NAND_ReadPageToCache( TEST_PAGE ) );

    HW_NAND_EccStatus_T ecc_status = HW_NAND_ECC_STATUS_UNKNOWN;
    EXPECT_EQ( HW_NAND_STATUS_OK, HW_NAND_GetLastEccStatus( &ecc_status ) );
    EXPECT_EQ( ecc_status, HW_NAND_ECC_STATUS_CORRECTED_1_TO_2 );
}

TEST_F( HWNANDTest, ReadPageToCacheReportsUncorrectableEcc )
{
    SetInitialised();
    InSequence sequence;

    ExpectAddressCommand( HW_NAND_OPCODE_PAGE_READ, TEST_PAGE, HW_QSPI_ADDR_24_BITS );
    ExpectStatusRead( TEST_ECC_UNCORRECTABLE_STATUS );

    EXPECT_EQ( HW_NAND_STATUS_ECC_ERROR, HW_NAND_ReadPageToCache( TEST_PAGE ) );
}

TEST_F( HWNANDTest, WaitReadyTimesOutWhenOipNeverClears )
{
    SetInitialised();

    EXPECT_CALL( mock, ReadBlocking( _, _, Eq( 1U ) ) )
        .Times( HW_NAND_READY_POLLS_PER_TIMEOUT_MS )
        .WillRepeatedly(
            Invoke( []( const HW_QSPI_Command_T* command, uint8_t* data, uint32_t length ) {
                EXPECT_EQ( length, 1U );
                EXPECT_EQ( command->instruction, HW_NAND_OPCODE_GET_FEATURE );
                EXPECT_EQ( command->address, HW_NAND_FEATURE_STATUS );
                EXPECT_EQ( command->address_size, HW_QSPI_ADDR_8_BITS );
                EXPECT_EQ( command->address_lines, HW_QSPI_LINES_1 );
                EXPECT_EQ( command->data_lines, HW_QSPI_LINES_1 );
                data[0] = TEST_BUSY_STATUS;
                return HW_QSPI_STATUS_OK;
            } ) );

    EXPECT_EQ( HW_NAND_STATUS_TIMEOUT, HW_NAND_WaitReady( 1U ) );
}

TEST_F( HWNANDTest, ReadCacheDmaUsesQuadReadOutputCommand )
{
    SetInitialised();

    EXPECT_CALL( mock, ReadDma( _, Eq( transfer_data ), Eq( TEST_LENGTH ) ) )
        .WillOnce( Invoke( []( const HW_QSPI_Command_T* command, uint8_t* data, uint32_t length ) {
            ( void )data;
            ( void )length;

            EXPECT_EQ( command->instruction, HW_NAND_OPCODE_READ_CACHE_QUAD );
            EXPECT_EQ( command->address, TEST_COLUMN );
            EXPECT_EQ( command->address_size, HW_QSPI_ADDR_16_BITS );
            EXPECT_EQ( command->address_lines, HW_QSPI_LINES_1 );
            EXPECT_EQ( command->data_lines, HW_QSPI_LINES_4 );
            EXPECT_EQ( command->dummy_cycles, 8U );
            return HW_QSPI_STATUS_OK;
        } ) );

    EXPECT_EQ( HW_NAND_STATUS_OK, HW_NAND_ReadCacheDma( TEST_COLUMN, transfer_data, TEST_LENGTH ) );
}

TEST_F( HWNANDTest, ProgramLoadDmaWriteEnablesThenStartsQuadProgramLoad )
{
    SetInitialised();
    InSequence sequence;

    ExpectOpcodeCommand( HW_NAND_OPCODE_WRITE_ENABLE );
    EXPECT_CALL( mock, WriteDma( _, Eq( transfer_data ), Eq( TEST_LENGTH ) ) )
        .WillOnce(
            Invoke( []( const HW_QSPI_Command_T* command, const uint8_t* data, uint32_t length ) {
                ( void )data;
                ( void )length;

                EXPECT_EQ( command->instruction, HW_NAND_OPCODE_PROGRAM_LOAD_QUAD );
                EXPECT_EQ( command->address, TEST_COLUMN );
                EXPECT_EQ( command->address_size, HW_QSPI_ADDR_16_BITS );
                EXPECT_EQ( command->address_lines, HW_QSPI_LINES_1 );
                EXPECT_EQ( command->data_lines, HW_QSPI_LINES_4 );
                EXPECT_EQ( command->dummy_cycles, 0U );
                return HW_QSPI_STATUS_OK;
            } ) );

    EXPECT_EQ( HW_NAND_STATUS_OK,
               HW_NAND_ProgramLoadDma( TEST_COLUMN, transfer_data, TEST_LENGTH ) );
}

TEST_F( HWNANDTest, ProgramExecuteReportsProgramFailure )
{
    SetInitialised();
    InSequence sequence;

    ExpectAddressCommand( HW_NAND_OPCODE_PROGRAM_EXECUTE, TEST_PAGE, HW_QSPI_ADDR_24_BITS );
    ExpectStatusRead( TEST_PROGRAM_FAIL_STATUS );

    EXPECT_EQ( HW_NAND_STATUS_PROGRAM_FAIL, HW_NAND_ProgramExecute( TEST_PAGE ) );
}

TEST_F( HWNANDTest, BlockEraseWriteEnablesAndReportsEraseFailure )
{
    SetInitialised();
    InSequence sequence;

    ExpectOpcodeCommand( HW_NAND_OPCODE_WRITE_ENABLE );
    ExpectAddressCommand( HW_NAND_OPCODE_BLOCK_ERASE, TEST_BLOCK * 64U, HW_QSPI_ADDR_24_BITS );
    ExpectStatusRead( TEST_ERASE_FAIL_STATUS );

    EXPECT_EQ( HW_NAND_STATUS_ERASE_FAIL, HW_NAND_BlockErase( TEST_BLOCK ) );
}

TEST_F( HWNANDTest, IsBlockBadChecksFirstSecondAndLastPageMarkers )
{
    SetInitialised();
    InSequence sequence;

    const uint32_t first_page  = TEST_BLOCK * 64U;
    const uint32_t second_page = first_page + 1U;

    ExpectAddressCommand( HW_NAND_OPCODE_PAGE_READ, first_page, HW_QSPI_ADDR_24_BITS );
    ExpectStatusRead( TEST_READY_STATUS );
    ExpectReadCacheBlocking( HW_NAND_BAD_BLOCK_MARKER_COLUMN, 0xFFU );
    ExpectAddressCommand( HW_NAND_OPCODE_PAGE_READ, second_page, HW_QSPI_ADDR_24_BITS );
    ExpectStatusRead( TEST_READY_STATUS );
    ExpectReadCacheBlocking( HW_NAND_BAD_BLOCK_MARKER_COLUMN, 0x00U );

    bool is_bad = false;
    EXPECT_EQ( HW_NAND_STATUS_OK, HW_NAND_IsBlockBad( TEST_BLOCK, &is_bad ) );
    EXPECT_TRUE( is_bad );
}

TEST_F( HWNANDTest, MarkBlockBadProgramsMarkerInFirstPageSpareArea )
{
    SetInitialised();
    InSequence sequence;

    ExpectOpcodeCommand( HW_NAND_OPCODE_WRITE_ENABLE );
    EXPECT_CALL( mock, WriteBlocking( _, _, Eq( 1U ) ) )
        .WillOnce(
            Invoke( []( const HW_QSPI_Command_T* command, const uint8_t* data, uint32_t length ) {
                EXPECT_EQ( length, 1U );
                EXPECT_EQ( command->instruction, HW_NAND_OPCODE_PROGRAM_LOAD_QUAD );
                EXPECT_EQ( command->address, HW_NAND_BAD_BLOCK_MARKER_COLUMN );
                EXPECT_EQ( command->address_size, HW_QSPI_ADDR_16_BITS );
                EXPECT_EQ( command->data_lines, HW_QSPI_LINES_4 );
                EXPECT_EQ( data[0], HW_NAND_BAD_BLOCK_MARKER_VALUE );
                return HW_QSPI_STATUS_OK;
            } ) );
    ExpectAddressCommand( HW_NAND_OPCODE_PROGRAM_EXECUTE, TEST_BLOCK * 64U, HW_QSPI_ADDR_24_BITS );
    ExpectStatusRead( TEST_READY_STATUS );

    EXPECT_EQ( HW_NAND_STATUS_OK, HW_NAND_MarkBlockBad( TEST_BLOCK ) );
}

TEST_F( HWNANDTest, TransferStateQueriesForwardToQspi )
{
    EXPECT_CALL( mock, IsTransferComplete() ).WillOnce( Return( true ) );
    EXPECT_CALL( mock, IsBusy() ).WillOnce( Return( false ) );

    EXPECT_TRUE( HW_NAND_IsTransferComplete() );
    EXPECT_FALSE( HW_NAND_IsBusy() );
}
