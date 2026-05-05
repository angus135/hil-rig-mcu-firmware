/******************************************************************************
 *  File:       test_external_flash.cpp
 *  Author:     Callum Rafferty
 *  Created:    25-Mar-2026
 *
 *  Description:
 *      Unit tests for the external_flash module using GoogleTest and GoogleMock.
 *      These tests validate the storage policy layer above hw_nand: bad block
 *      scanning, instruction upload, result session erase, page scoped zero
 *      copy result writes, instruction page DMA reads, and committed result
 *      readback.
 *
 *  Notes:
 *      - Production code is written in C; tests are written in C++.
 *      - C headers must be included inside an extern "C" block.
 *      - GoogleMock is used to mock the hw_nand dependency.
 *      - The test file includes external_flash.c directly, matching the local
 *        module test pattern used elsewhere in the repository.
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>

extern "C"
{
#include "external_flash_mocks.h"
#include "external_flash.h"
#include <stdint.h>
#include <stdbool.h>

#include "external_flash.c" /* Module under test */  // NOLINT
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

static constexpr uint32_t TEST_PAGE_SIZE_BYTES  = 2048U;
static constexpr uint32_t TEST_SPARE_SIZE_BYTES = 128U;
static constexpr uint32_t TEST_PAGES_PER_BLOCK  = 64U;
static constexpr uint32_t TEST_BLOCK_COUNT      = 4096U;
static constexpr uint32_t TEST_RESULT_BLOCK     = EXTERNAL_FLASH_RESULT_START_BLOCK;
static constexpr uint32_t TEST_RESULT_PAGE      = TEST_RESULT_BLOCK * TEST_PAGES_PER_BLOCK;
static constexpr uint32_t TEST_INSTRUCTION_PAGE = 0U;
static constexpr uint32_t TEST_BLOCK_DATA_BYTES = TEST_PAGE_SIZE_BYTES * TEST_PAGES_PER_BLOCK;
static constexpr uint32_t TEST_SMALL_LENGTH     = 16U;

/**-----------------------------------------------------------------------------
 *  Test Doubles / Mocks
 *------------------------------------------------------------------------------
 */

class MockExternalFlashNand
{
public:
    MOCK_METHOD( HW_NAND_Status_T, Init, (), () );
    MOCK_METHOD( HW_NAND_Status_T, GetGeometry, ( HW_NAND_Geometry_T * geometry ), () );
    MOCK_METHOD( HW_NAND_Status_T, BlockErase, ( uint32_t block ), () );
    MOCK_METHOD( HW_NAND_Status_T, IsBlockBad, ( uint32_t block, bool* is_bad ), () );
    MOCK_METHOD( HW_NAND_Status_T, MarkBlockBad, ( uint32_t block ), () );
    MOCK_METHOD( HW_NAND_Status_T, ProgramLoadDma,
                 ( uint16_t column, const uint8_t* data, uint32_t length ), () );
    MOCK_METHOD( HW_NAND_Status_T, ReadPageDma,
                 ( uint32_t page, uint16_t column, uint8_t* data, uint32_t length ), () );
    MOCK_METHOD( bool, IsTransferComplete, (), () );
    MOCK_METHOD( HW_NAND_Status_T, StartProgramExecute, ( uint32_t page ), () );
    MOCK_METHOD( HW_NAND_Status_T, WaitProgramComplete, ( uint32_t timeout_ms ), () );
    MOCK_METHOD( HW_NAND_Status_T, ReadPageBlocking,
                 ( uint32_t page, uint16_t column, uint8_t* data, uint32_t length ), () );
};

static MockExternalFlashNand* g_mock = nullptr;

// NOLINTBEGIN
extern "C" HW_NAND_Status_T HW_NAND_Init( void )
{
    if ( g_mock == nullptr )
    {
        return HW_NAND_STATUS_ERROR;
    }

    return g_mock->Init();
}

extern "C" HW_NAND_Status_T HW_NAND_GetGeometry( HW_NAND_Geometry_T* geometry )
{
    if ( g_mock == nullptr )
    {
        return HW_NAND_STATUS_ERROR;
    }

    return g_mock->GetGeometry( geometry );
}

extern "C" HW_NAND_Status_T HW_NAND_BlockErase( uint32_t block )
{
    if ( g_mock == nullptr )
    {
        return HW_NAND_STATUS_ERROR;
    }

    return g_mock->BlockErase( block );
}

extern "C" HW_NAND_Status_T HW_NAND_IsBlockBad( uint32_t block, bool* is_bad )
{
    if ( g_mock == nullptr )
    {
        return HW_NAND_STATUS_ERROR;
    }

    return g_mock->IsBlockBad( block, is_bad );
}

extern "C" HW_NAND_Status_T HW_NAND_MarkBlockBad( uint32_t block )
{
    if ( g_mock == nullptr )
    {
        return HW_NAND_STATUS_ERROR;
    }

    return g_mock->MarkBlockBad( block );
}

extern "C" HW_NAND_Status_T HW_NAND_ProgramLoadDma( uint16_t column, const uint8_t* data,
                                                    uint32_t length )
{
    if ( g_mock == nullptr )
    {
        return HW_NAND_STATUS_ERROR;
    }

    return g_mock->ProgramLoadDma( column, data, length );
}

extern "C" HW_NAND_Status_T HW_NAND_ReadPageDma( uint32_t page, uint16_t column, uint8_t* data,
                                                 uint32_t length )
{
    if ( g_mock == nullptr )
    {
        return HW_NAND_STATUS_ERROR;
    }

    return g_mock->ReadPageDma( page, column, data, length );
}

extern "C" bool HW_NAND_IsTransferComplete( void )
{
    if ( g_mock == nullptr )
    {
        return false;
    }

    return g_mock->IsTransferComplete();
}

extern "C" HW_NAND_Status_T HW_NAND_StartProgramExecute( uint32_t page )
{
    if ( g_mock == nullptr )
    {
        return HW_NAND_STATUS_ERROR;
    }

    return g_mock->StartProgramExecute( page );
}

extern "C" HW_NAND_Status_T HW_NAND_WaitProgramComplete( uint32_t timeout_ms )
{
    if ( g_mock == nullptr )
    {
        return HW_NAND_STATUS_ERROR;
    }

    return g_mock->WaitProgramComplete( timeout_ms );
}

extern "C" HW_NAND_Status_T HW_NAND_ReadPageBlocking( uint32_t page, uint16_t column, uint8_t* data,
                                                      uint32_t length )
{
    if ( g_mock == nullptr )
    {
        return HW_NAND_STATUS_ERROR;
    }

    return g_mock->ReadPageBlocking( page, column, data, length );
}
// NOLINTEND

/**-----------------------------------------------------------------------------
 *  Test Fixture
 *------------------------------------------------------------------------------
 */

/**
 * @brief Test fixture for external_flash tests.
 *
 * Resets private module state and provides common NAND expectations.
 */
class ExternalFlashTest : public ::testing::Test
{
protected:
    NiceMock<MockExternalFlashNand> mock;
    uint8_t                         transfer_data[TEST_PAGE_SIZE_BYTES] = {};

    void SetUp( void ) override
    {
        g_mock = &mock;

        external_flash_initialised                         = false;
        external_flash_session_active                      = false;
        external_flash_instruction_upload_active           = false;
        external_flash_geometry                            = { 0U, 0U, 0U, 0U };
        external_flash_bad_block_count                     = 0U;
        external_flash_instruction_next_start_offset       = 0U;
        external_flash_instruction_block_map_count         = 0U;
        external_flash_result_next_start_offset            = 0U;
        external_flash_result_block_map_count              = 0U;
        external_flash_instruction_expected_length_bytes   = 0U;
        external_flash_instruction_length_bytes            = 0U;
        external_flash_committed_instruction_length_bytes  = 0U;
        external_flash_instruction_page_fill               = 0U;
        external_flash_result_length_bytes                 = 0U;
        external_flash_committed_result_length_bytes       = 0U;
        external_flash_result_session_capacity_bytes       = 0U;

        for ( uint32_t i = 0U; i < TEST_PAGE_SIZE_BYTES; i++ )
        {
            transfer_data[i]                     = static_cast<uint8_t>( i & 0xFFU );
            external_flash_result_page_buffer[i] = 0xFFU;
            external_flash_instruction_page_buffer[i] = 0xFFU;
        }

        EXTERNAL_FLASH_ClearBlockMap( external_flash_instruction_block_map,
                                      EXTERNAL_FLASH_INSTRUCTION_BLOCK_COUNT );
        EXTERNAL_FLASH_ClearBlockMap( external_flash_result_block_map,
                                      EXTERNAL_FLASH_RESULT_BLOCK_COUNT );

        for ( uint32_t i = 0U; i < EXTERNAL_FLASH_MANAGED_BLOCK_COUNT; i++ )
        {
            external_flash_bad_block_table[i] = false;
            external_flash_block_erase_counts[i] = 0U;
        }
    }

    void TearDown( void ) override
    {
        g_mock = nullptr;
    }

    void ExpectGeometry( void )
    {
        EXPECT_CALL( mock, GetGeometry( _ ) ).WillOnce( Invoke( []( HW_NAND_Geometry_T* geometry ) {
            geometry->page_size_bytes  = TEST_PAGE_SIZE_BYTES;
            geometry->spare_size_bytes = TEST_SPARE_SIZE_BYTES;
            geometry->pages_per_block  = TEST_PAGES_PER_BLOCK;
            geometry->block_count      = TEST_BLOCK_COUNT;
            return HW_NAND_STATUS_OK;
        } ) );
    }

    void ExpectBadBlockScanAllGood( void )
    {
        EXPECT_CALL( mock, IsBlockBad( _, _ ) )
            .Times( EXTERNAL_FLASH_MANAGED_BLOCK_COUNT )
            .WillRepeatedly( Invoke( []( uint32_t block, bool* is_bad ) {
                ( void )block;
                *is_bad = false;
                return HW_NAND_STATUS_OK;
            } ) );
    }

    void InitDriverAllGood( void )
    {
        EXPECT_CALL( mock, Init() ).WillOnce( Return( HW_NAND_STATUS_OK ) );
        ExpectGeometry();
        ExpectBadBlockScanAllGood();

        EXPECT_EQ( EXTERNAL_FLASH_STATUS_OK, EXTERNAL_FLASH_Init() );
    }

    void StartSessionAllGood( void )
    {
        EXPECT_CALL( mock, BlockErase( _ ) ).WillRepeatedly( Return( HW_NAND_STATUS_OK ) );
        EXPECT_EQ( EXTERNAL_FLASH_STATUS_OK, EXTERNAL_FLASH_StartSession() );
    }

    void StartInstructionUploadAllGood( uint32_t expected_length )
    {
        EXPECT_CALL( mock, BlockErase( _ ) ).WillRepeatedly( Return( HW_NAND_STATUS_OK ) );
        EXPECT_EQ( EXTERNAL_FLASH_STATUS_OK,
                   EXTERNAL_FLASH_StartInstructionUpload( expected_length ) );
    }

    void ExpectOneSuccessfulProgram( uint32_t expected_page )
    {
        InSequence sequence;

        EXPECT_CALL( mock, ProgramLoadDma( Eq( 0U ), _, Eq( TEST_PAGE_SIZE_BYTES ) ) )
            .WillOnce( Return( HW_NAND_STATUS_OK ) );
        EXPECT_CALL( mock, IsTransferComplete() ).WillOnce( Return( true ) );
        EXPECT_CALL( mock, StartProgramExecute( Eq( expected_page ) ) )
            .WillOnce( Return( HW_NAND_STATUS_OK ) );
        EXPECT_CALL( mock, WaitProgramComplete( Eq( 1U ) ) )
            .WillOnce( Return( HW_NAND_STATUS_OK ) );
    }

    void ExpectOneSuccessfulInstructionPageRead( uint32_t expected_page, uint8_t* expected_buffer,
                                                 uint32_t expected_length )
    {
        InSequence sequence;

        EXPECT_CALL( mock, ReadPageDma( Eq( expected_page ), Eq( 0U ), Eq( expected_buffer ),
                                        Eq( expected_length ) ) )
            .WillOnce( Return( HW_NAND_STATUS_OK ) );
        EXPECT_CALL( mock, IsTransferComplete() ).WillOnce( Return( true ) );
    }
};

/**-----------------------------------------------------------------------------
 *  Test Cases
 *------------------------------------------------------------------------------
 */

TEST_F( ExternalFlashTest, InitInitialisesNandGetsGeometryAndScansBadBlocks )
{
    EXPECT_CALL( mock, Init() ).WillOnce( Return( HW_NAND_STATUS_OK ) );
    ExpectGeometry();
    ExpectBadBlockScanAllGood();

    EXPECT_EQ( EXTERNAL_FLASH_STATUS_OK, EXTERNAL_FLASH_Init() );
    EXPECT_TRUE( EXTERNAL_FLASH_IsInitialised() );

    ExternalFlashInfo_T info = {};
    EXPECT_EQ( EXTERNAL_FLASH_STATUS_OK, EXTERNAL_FLASH_GetInfo( &info ) );
    EXPECT_EQ( info.bad_block_count, 0U );
    EXPECT_EQ( info.instruction_capacity_bytes,
               ( EXTERNAL_FLASH_INSTRUCTION_BLOCK_COUNT - 1U ) * TEST_BLOCK_DATA_BYTES );
    EXPECT_EQ( info.result_capacity_bytes,
               ( EXTERNAL_FLASH_RESULT_BLOCK_COUNT - 1U ) * TEST_BLOCK_DATA_BYTES );
    EXPECT_EQ( info.instruction_length_bytes, 0U );
}

TEST_F( ExternalFlashTest, InitRemovesBadBlocksFromCapacity )
{
    EXPECT_CALL( mock, Init() ).WillOnce( Return( HW_NAND_STATUS_OK ) );
    ExpectGeometry();
    EXPECT_CALL( mock, IsBlockBad( _, _ ) )
        .Times( EXTERNAL_FLASH_MANAGED_BLOCK_COUNT )
        .WillRepeatedly( Invoke( []( uint32_t block, bool* is_bad ) {
            *is_bad = ( block == 0U ) || ( block == EXTERNAL_FLASH_RESULT_START_BLOCK );
            return HW_NAND_STATUS_OK;
        } ) );

    EXPECT_EQ( EXTERNAL_FLASH_STATUS_OK, EXTERNAL_FLASH_Init() );

    ExternalFlashInfo_T info = {};
    EXPECT_EQ( EXTERNAL_FLASH_STATUS_OK, EXTERNAL_FLASH_GetInfo( &info ) );
    EXPECT_EQ( info.bad_block_count, 2U );
    EXPECT_EQ( info.instruction_capacity_bytes,
               ( EXTERNAL_FLASH_INSTRUCTION_BLOCK_COUNT - 2U ) * TEST_BLOCK_DATA_BYTES );
    EXPECT_EQ( info.result_capacity_bytes,
               ( EXTERNAL_FLASH_RESULT_BLOCK_COUNT - 2U ) * TEST_BLOCK_DATA_BYTES );
}

TEST_F( ExternalFlashTest, StartSessionErasesOnlyGoodResultBlocks )
{
    InitDriverAllGood();
    external_flash_bad_block_table[TEST_RESULT_BLOCK + 1U] = true;

    EXPECT_CALL( mock, BlockErase( _ ) )
        .Times( EXTERNAL_FLASH_RESULT_BLOCK_COUNT - 2U )
        .WillRepeatedly( Invoke( []( uint32_t block ) {
            EXPECT_GE( block, EXTERNAL_FLASH_RESULT_START_BLOCK );
            EXPECT_NE( block, TEST_RESULT_BLOCK + 1U );
            return HW_NAND_STATUS_OK;
        } ) );

    EXPECT_EQ( EXTERNAL_FLASH_STATUS_OK, EXTERNAL_FLASH_StartSession() );
}

TEST_F( ExternalFlashTest, StartInstructionUploadErasesOnlyGoodInstructionBlocks )
{
    InitDriverAllGood();
    external_flash_bad_block_table[1U] = true;

    EXPECT_CALL( mock, BlockErase( _ ) )
        .Times( 1U )
        .WillRepeatedly( Invoke( []( uint32_t block ) {
            EXPECT_LT( block, EXTERNAL_FLASH_INSTRUCTION_BLOCK_COUNT );
            EXPECT_NE( block, 1U );
            return HW_NAND_STATUS_OK;
        } ) );

    EXPECT_EQ( EXTERNAL_FLASH_STATUS_OK,
               EXTERNAL_FLASH_StartInstructionUpload( TEST_PAGE_SIZE_BYTES ) );
}

TEST_F( ExternalFlashTest, WriteInstructionBytesStagesPartialPageUntilFinish )
{
    InitDriverAllGood();
    StartInstructionUploadAllGood( TEST_SMALL_LENGTH );

    EXPECT_CALL( mock, ProgramLoadDma( _, _, _ ) ).Times( 0 );

    EXPECT_EQ( EXTERNAL_FLASH_STATUS_OK,
               EXTERNAL_FLASH_WriteInstructionBytes( transfer_data, TEST_SMALL_LENGTH ) );

    ExternalFlashInfo_T info = {};
    EXPECT_EQ( EXTERNAL_FLASH_STATUS_OK, EXTERNAL_FLASH_GetInfo( &info ) );
    EXPECT_EQ( info.instruction_length_bytes, 0U );
}

TEST_F( ExternalFlashTest, FinishInstructionUploadProgramsPartialPageAndReportsLength )
{
    InitDriverAllGood();
    StartInstructionUploadAllGood( TEST_SMALL_LENGTH );

    EXPECT_EQ( EXTERNAL_FLASH_STATUS_OK,
               EXTERNAL_FLASH_WriteInstructionBytes( transfer_data, TEST_SMALL_LENGTH ) );

    InSequence sequence;
    EXPECT_CALL( mock, ProgramLoadDma( Eq( 0U ), _, Eq( TEST_PAGE_SIZE_BYTES ) ) )
        .WillOnce( Invoke( []( uint16_t column, const uint8_t* data, uint32_t length ) {
            EXPECT_EQ( column, 0U );
            EXPECT_EQ( length, TEST_PAGE_SIZE_BYTES );
            EXPECT_EQ( data[0], 0U );
            EXPECT_EQ( data[TEST_SMALL_LENGTH - 1U],
                       static_cast<uint8_t>( TEST_SMALL_LENGTH - 1U ) );
            EXPECT_EQ( data[TEST_SMALL_LENGTH], 0xFFU );
            return HW_NAND_STATUS_OK;
        } ) );
    EXPECT_CALL( mock, IsTransferComplete() ).WillOnce( Return( true ) );
    EXPECT_CALL( mock, StartProgramExecute( Eq( TEST_INSTRUCTION_PAGE ) ) )
        .WillOnce( Return( HW_NAND_STATUS_OK ) );
    EXPECT_CALL( mock, WaitProgramComplete( Eq( 1U ) ) ).WillOnce( Return( HW_NAND_STATUS_OK ) );

    EXPECT_EQ( EXTERNAL_FLASH_STATUS_OK, EXTERNAL_FLASH_FinishInstructionUpload() );

    ExternalFlashInfo_T info = {};
    EXPECT_EQ( EXTERNAL_FLASH_STATUS_OK, EXTERNAL_FLASH_GetInfo( &info ) );
    EXPECT_EQ( info.instruction_length_bytes, TEST_SMALL_LENGTH );
}

TEST_F( ExternalFlashTest, WriteInstructionPageFullPageUsesCallerBufferDirectly )
{
    InitDriverAllGood();
    StartInstructionUploadAllGood( TEST_PAGE_SIZE_BYTES );

    InSequence sequence;
    EXPECT_CALL( mock, ProgramLoadDma( Eq( 0U ), Eq( transfer_data ), Eq( TEST_PAGE_SIZE_BYTES ) ) )
        .WillOnce( Return( HW_NAND_STATUS_OK ) );
    EXPECT_CALL( mock, IsTransferComplete() ).WillOnce( Return( true ) );
    EXPECT_CALL( mock, StartProgramExecute( Eq( TEST_INSTRUCTION_PAGE ) ) )
        .WillOnce( Return( HW_NAND_STATUS_OK ) );
    EXPECT_CALL( mock, WaitProgramComplete( Eq( 1U ) ) ).WillOnce( Return( HW_NAND_STATUS_OK ) );

    EXPECT_EQ( EXTERNAL_FLASH_STATUS_OK,
               EXTERNAL_FLASH_WriteInstructionPage( transfer_data, TEST_PAGE_SIZE_BYTES ) );
    EXPECT_EQ( EXTERNAL_FLASH_STATUS_OK, EXTERNAL_FLASH_FinishInstructionUpload() );

    ExternalFlashInfo_T info = {};
    EXPECT_EQ( EXTERNAL_FLASH_STATUS_OK, EXTERNAL_FLASH_GetInfo( &info ) );
    EXPECT_EQ( info.instruction_length_bytes, TEST_PAGE_SIZE_BYTES );
}

TEST_F( ExternalFlashTest, InstructionUploadUsesWearRotationStartOffset )
{
    InitDriverAllGood();
    external_flash_instruction_next_start_offset = 1U;
    StartInstructionUploadAllGood( TEST_PAGE_SIZE_BYTES );

    InSequence sequence;
    EXPECT_CALL( mock, ProgramLoadDma( Eq( 0U ), Eq( transfer_data ), Eq( TEST_PAGE_SIZE_BYTES ) ) )
        .WillOnce( Return( HW_NAND_STATUS_OK ) );
    EXPECT_CALL( mock, IsTransferComplete() ).WillOnce( Return( true ) );
    EXPECT_CALL( mock, StartProgramExecute( Eq( TEST_PAGES_PER_BLOCK ) ) )
        .WillOnce( Return( HW_NAND_STATUS_OK ) );
    EXPECT_CALL( mock, WaitProgramComplete( Eq( 1U ) ) ).WillOnce( Return( HW_NAND_STATUS_OK ) );

    EXPECT_EQ( EXTERNAL_FLASH_STATUS_OK,
               EXTERNAL_FLASH_WriteInstructionPage( transfer_data, TEST_PAGE_SIZE_BYTES ) );
    EXPECT_EQ( EXTERNAL_FLASH_STATUS_OK, EXTERNAL_FLASH_FinishInstructionUpload() );
    EXPECT_EQ( external_flash_instruction_next_start_offset, 2U );
}

TEST_F( ExternalFlashTest, FinishInstructionUploadRejectsIncompleteUpload )
{
    InitDriverAllGood();
    StartInstructionUploadAllGood( TEST_PAGE_SIZE_BYTES );

    EXPECT_EQ( EXTERNAL_FLASH_STATUS_OK,
               EXTERNAL_FLASH_WriteInstructionBytes( transfer_data, TEST_SMALL_LENGTH ) );

    EXPECT_CALL( mock, ProgramLoadDma( _, _, _ ) ).WillOnce( Return( HW_NAND_STATUS_OK ) );
    EXPECT_CALL( mock, IsTransferComplete() ).WillOnce( Return( true ) );
    EXPECT_CALL( mock, StartProgramExecute( Eq( TEST_INSTRUCTION_PAGE ) ) )
        .WillOnce( Return( HW_NAND_STATUS_OK ) );
    EXPECT_CALL( mock, WaitProgramComplete( Eq( 1U ) ) ).WillOnce( Return( HW_NAND_STATUS_OK ) );

    EXPECT_EQ( EXTERNAL_FLASH_STATUS_ERROR, EXTERNAL_FLASH_FinishInstructionUpload() );
}

TEST_F( ExternalFlashTest, ResultSessionUsesWearRotationStartOffset )
{
    InitDriverAllGood();
    external_flash_result_next_start_offset = 1U;
    StartSessionAllGood();

    uint32_t expected_page = ( TEST_RESULT_BLOCK + 1U ) * TEST_PAGES_PER_BLOCK;

    InSequence sequence;
    EXPECT_CALL( mock, ProgramLoadDma( Eq( 0U ), Eq( transfer_data ), Eq( TEST_PAGE_SIZE_BYTES ) ) )
        .WillOnce( Return( HW_NAND_STATUS_OK ) );
    EXPECT_CALL( mock, IsTransferComplete() ).WillOnce( Return( true ) );
    EXPECT_CALL( mock, StartProgramExecute( Eq( expected_page ) ) )
        .WillOnce( Return( HW_NAND_STATUS_OK ) );
    EXPECT_CALL( mock, WaitProgramComplete( Eq( 1U ) ) ).WillOnce( Return( HW_NAND_STATUS_OK ) );

    EXPECT_EQ( EXTERNAL_FLASH_STATUS_OK,
               EXTERNAL_FLASH_WriteResultPage( transfer_data, TEST_PAGE_SIZE_BYTES ) );
}

TEST_F( ExternalFlashTest, WriteResultPageRejectsInvalidArguments )
{
    InitDriverAllGood();
    StartSessionAllGood();

    EXPECT_EQ( EXTERNAL_FLASH_STATUS_INVALID_ARG, EXTERNAL_FLASH_WriteResultPage( nullptr, 1U ) );
    EXPECT_EQ( EXTERNAL_FLASH_STATUS_INVALID_ARG,
               EXTERNAL_FLASH_WriteResultPage( transfer_data, 0U ) );
    EXPECT_EQ( EXTERNAL_FLASH_STATUS_INVALID_ARG,
               EXTERNAL_FLASH_WriteResultPage( transfer_data, TEST_PAGE_SIZE_BYTES + 1U ) );
}

TEST_F( ExternalFlashTest, WriteResultPageFullPageUsesCallerBufferDirectly )
{
    InitDriverAllGood();
    StartSessionAllGood();

    InSequence sequence;
    EXPECT_CALL( mock, ProgramLoadDma( Eq( 0U ), Eq( transfer_data ), Eq( TEST_PAGE_SIZE_BYTES ) ) )
        .WillOnce( Return( HW_NAND_STATUS_OK ) );
    EXPECT_CALL( mock, IsTransferComplete() ).WillOnce( Return( true ) );
    EXPECT_CALL( mock, StartProgramExecute( Eq( TEST_RESULT_PAGE ) ) )
        .WillOnce( Return( HW_NAND_STATUS_OK ) );
    EXPECT_CALL( mock, WaitProgramComplete( Eq( 1U ) ) ).WillOnce( Return( HW_NAND_STATUS_OK ) );

    EXPECT_EQ( EXTERNAL_FLASH_STATUS_OK,
               EXTERNAL_FLASH_WriteResultPage( transfer_data, TEST_PAGE_SIZE_BYTES ) );

    ExternalFlashInfo_T info = {};
    EXPECT_EQ( EXTERNAL_FLASH_STATUS_OK, EXTERNAL_FLASH_GetInfo( &info ) );
    EXPECT_EQ( info.result_length_bytes, TEST_PAGE_SIZE_BYTES );
}

TEST_F( ExternalFlashTest, WriteResultPagePartialPagePadsWithErasedBytes )
{
    InitDriverAllGood();
    StartSessionAllGood();

    InSequence sequence;
    EXPECT_CALL( mock, ProgramLoadDma( Eq( 0U ), _, Eq( TEST_PAGE_SIZE_BYTES ) ) )
        .WillOnce( Invoke( []( uint16_t column, const uint8_t* data, uint32_t length ) {
            EXPECT_EQ( column, 0U );
            EXPECT_EQ( length, TEST_PAGE_SIZE_BYTES );
            EXPECT_EQ( data[0], 0U );
            EXPECT_EQ( data[TEST_SMALL_LENGTH - 1U],
                       static_cast<uint8_t>( TEST_SMALL_LENGTH - 1U ) );
            EXPECT_EQ( data[TEST_SMALL_LENGTH], 0xFFU );
            return HW_NAND_STATUS_OK;
        } ) );
    EXPECT_CALL( mock, IsTransferComplete() ).WillOnce( Return( true ) );
    EXPECT_CALL( mock, StartProgramExecute( Eq( TEST_RESULT_PAGE ) ) )
        .WillOnce( Return( HW_NAND_STATUS_OK ) );
    EXPECT_CALL( mock, WaitProgramComplete( Eq( 1U ) ) ).WillOnce( Return( HW_NAND_STATUS_OK ) );

    EXPECT_EQ( EXTERNAL_FLASH_STATUS_OK,
               EXTERNAL_FLASH_WriteResultPage( transfer_data, TEST_SMALL_LENGTH ) );

    ExternalFlashInfo_T info = {};
    EXPECT_EQ( EXTERNAL_FLASH_STATUS_OK, EXTERNAL_FLASH_GetInfo( &info ) );
    EXPECT_EQ( info.result_length_bytes, TEST_SMALL_LENGTH );
}

TEST_F( ExternalFlashTest, WriteResultPageRejectsAppendAfterPartialPage )
{
    InitDriverAllGood();
    StartSessionAllGood();

    ExpectOneSuccessfulProgram( TEST_RESULT_PAGE );

    EXPECT_EQ( EXTERNAL_FLASH_STATUS_OK,
               EXTERNAL_FLASH_WriteResultPage( transfer_data, TEST_SMALL_LENGTH ) );

    EXPECT_EQ( EXTERNAL_FLASH_STATUS_ERROR,
               EXTERNAL_FLASH_WriteResultPage( transfer_data, TEST_PAGE_SIZE_BYTES ) );
}

TEST_F( ExternalFlashTest, ProgramFailureRetiresBlockAndRetriesNextGoodBlock )
{
    InitDriverAllGood();
    StartSessionAllGood();

    InSequence sequence;
    EXPECT_CALL( mock, ProgramLoadDma( Eq( 0U ), Eq( transfer_data ), Eq( TEST_PAGE_SIZE_BYTES ) ) )
        .WillOnce( Return( HW_NAND_STATUS_OK ) );
    EXPECT_CALL( mock, IsTransferComplete() ).WillOnce( Return( true ) );
    EXPECT_CALL( mock, StartProgramExecute( Eq( TEST_RESULT_PAGE ) ) )
        .WillOnce( Return( HW_NAND_STATUS_OK ) );
    EXPECT_CALL( mock, WaitProgramComplete( Eq( 1U ) ) )
        .WillOnce( Return( HW_NAND_STATUS_PROGRAM_FAIL ) );
    EXPECT_CALL( mock, MarkBlockBad( Eq( TEST_RESULT_BLOCK ) ) )
        .WillOnce( Return( HW_NAND_STATUS_OK ) );
    EXPECT_CALL( mock,
                 BlockErase( Eq( TEST_RESULT_BLOCK + EXTERNAL_FLASH_RESULT_BLOCK_COUNT - 1U ) ) )
        .WillOnce( Return( HW_NAND_STATUS_OK ) );
    EXPECT_CALL( mock, ProgramLoadDma( Eq( 0U ), Eq( transfer_data ), Eq( TEST_PAGE_SIZE_BYTES ) ) )
        .WillOnce( Return( HW_NAND_STATUS_OK ) );
    EXPECT_CALL( mock, IsTransferComplete() ).WillOnce( Return( true ) );
    EXPECT_CALL( mock,
                 StartProgramExecute( Eq( ( TEST_RESULT_BLOCK
                                            + EXTERNAL_FLASH_RESULT_BLOCK_COUNT - 1U )
                                          * TEST_PAGES_PER_BLOCK ) ) )
        .WillOnce( Return( HW_NAND_STATUS_OK ) );
    EXPECT_CALL( mock, WaitProgramComplete( Eq( 1U ) ) ).WillOnce( Return( HW_NAND_STATUS_OK ) );

    EXPECT_EQ( EXTERNAL_FLASH_STATUS_OK,
               EXTERNAL_FLASH_WriteResultPage( transfer_data, TEST_PAGE_SIZE_BYTES ) );
}

TEST_F( ExternalFlashTest, ReadInstructionPageRejectsInvalidArguments )
{
    InitDriverAllGood();
    external_flash_committed_instruction_length_bytes = TEST_PAGE_SIZE_BYTES;

    uint8_t read_buffer[TEST_PAGE_SIZE_BYTES] = {};

    EXPECT_EQ( EXTERNAL_FLASH_STATUS_INVALID_ARG,
               EXTERNAL_FLASH_ReadInstructionPage( 0U, nullptr, TEST_PAGE_SIZE_BYTES ) );
    EXPECT_EQ( EXTERNAL_FLASH_STATUS_INVALID_ARG,
               EXTERNAL_FLASH_ReadInstructionPage( 0U, read_buffer, 0U ) );
    EXPECT_EQ( EXTERNAL_FLASH_STATUS_INVALID_ARG,
               EXTERNAL_FLASH_ReadInstructionPage( 0U, read_buffer, TEST_PAGE_SIZE_BYTES + 1U ) );
    EXPECT_EQ( EXTERNAL_FLASH_STATUS_INVALID_ARG,
               EXTERNAL_FLASH_ReadInstructionPage( 1U, read_buffer, TEST_PAGE_SIZE_BYTES ) );
}

TEST_F( ExternalFlashTest, ReadInstructionPageUsesDmaIntoCallerBuffer )
{
    InitDriverAllGood();
    external_flash_committed_instruction_length_bytes = TEST_PAGE_SIZE_BYTES;

    uint8_t read_buffer[TEST_PAGE_SIZE_BYTES] = {};

    ExpectOneSuccessfulInstructionPageRead( TEST_INSTRUCTION_PAGE, read_buffer,
                                            TEST_PAGE_SIZE_BYTES );

    EXPECT_EQ( EXTERNAL_FLASH_STATUS_OK,
               EXTERNAL_FLASH_ReadInstructionPage( 0U, read_buffer, TEST_PAGE_SIZE_BYTES ) );
}

TEST_F( ExternalFlashTest, ReadInstructionPageSupportsFinalPartialPage )
{
    InitDriverAllGood();
    external_flash_committed_instruction_length_bytes = TEST_SMALL_LENGTH;

    uint8_t read_buffer[TEST_SMALL_LENGTH] = {};

    ExpectOneSuccessfulInstructionPageRead( TEST_INSTRUCTION_PAGE, read_buffer, TEST_SMALL_LENGTH );

    EXPECT_EQ( EXTERNAL_FLASH_STATUS_OK,
               EXTERNAL_FLASH_ReadInstructionPage( 0U, read_buffer, TEST_SMALL_LENGTH ) );
}

TEST_F( ExternalFlashTest, ReadInstructionPageTimesOutIfDmaDoesNotComplete )
{
    InitDriverAllGood();
    external_flash_committed_instruction_length_bytes = TEST_PAGE_SIZE_BYTES;

    uint8_t read_buffer[TEST_PAGE_SIZE_BYTES] = {};

    EXPECT_CALL( mock, ReadPageDma( Eq( TEST_INSTRUCTION_PAGE ), Eq( 0U ), Eq( read_buffer ),
                                    Eq( TEST_PAGE_SIZE_BYTES ) ) )
        .WillOnce( Return( HW_NAND_STATUS_OK ) );
    EXPECT_CALL( mock, IsTransferComplete() ).WillRepeatedly( Return( false ) );

    EXPECT_EQ( EXTERNAL_FLASH_STATUS_TIMEOUT,
               EXTERNAL_FLASH_ReadInstructionPage( 0U, read_buffer, TEST_PAGE_SIZE_BYTES ) );
}

TEST_F( ExternalFlashTest, ReadResultsReadsCommittedPageWriteBytes )
{
    InitDriverAllGood();
    StartSessionAllGood();

    ExpectOneSuccessfulProgram( TEST_RESULT_PAGE );

    EXPECT_EQ( EXTERNAL_FLASH_STATUS_OK,
               EXTERNAL_FLASH_WriteResultPage( transfer_data, TEST_PAGE_SIZE_BYTES ) );

    uint8_t read_buffer[TEST_SMALL_LENGTH] = {};

    EXPECT_CALL( mock, ReadPageBlocking( Eq( TEST_RESULT_PAGE ), Eq( 0U ), Eq( read_buffer ),
                                         Eq( TEST_SMALL_LENGTH ) ) )
        .WillOnce( Return( HW_NAND_STATUS_OK ) );

    EXPECT_EQ( EXTERNAL_FLASH_STATUS_OK,
               EXTERNAL_FLASH_ReadResults( 0U, read_buffer, TEST_SMALL_LENGTH ) );
}
