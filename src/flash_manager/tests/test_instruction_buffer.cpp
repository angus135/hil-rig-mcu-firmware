/******************************************************************************
 *  File:       test_instruction_buffer.cpp
 *  Author:     Callum Rafferty
 *  Created:    05-Aug-2026
 *
 *  Description:
 *      Unit tests for instruction-buffer geometry, read-session preparation,
 *      and sequential NAND page-fill ownership.
 *
 *  Notes:
 *      Production code is included directly so tests can verify private page
 *      state, following the existing Flash Manager test convention.
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>

#include "flash_manager_mocks.h"

extern "C"
{
#include "external_flash.h"
#include "instruction_buffer.h"
}

/**-----------------------------------------------------------------------------
 *  Test Constants / Macros
 *------------------------------------------------------------------------------
 */

static constexpr uint32_t TEST_INSTRUCTION_PAGE_SIZE_BYTES = 32U;
static constexpr uint32_t TEST_INSTRUCTION_PARTITION_CAPACITY_BYTES =
    TEST_INSTRUCTION_PAGE_SIZE_BYTES * 8U;

/**-----------------------------------------------------------------------------
 *  Module Under Test
 *------------------------------------------------------------------------------
 */

extern "C"
{
#include "../instruction_buffer.c" /* Private module under test */  // NOLINT
}

/**-----------------------------------------------------------------------------
 *  Test Fixture
 *------------------------------------------------------------------------------
 */

class InstructionBufferTest : public ::testing::Test
{
protected:
    void SetUp( void ) override
    {
        std::memset( &instruction_buffer_context, 0, sizeof( instruction_buffer_context ) );
        std::memset( instruction_buffer_storage, 0, sizeof( instruction_buffer_storage ) );

        FLASH_MANAGER_TEST_ConfigureInstructionFlashInfo(
            EXTERNAL_FLASH_STATUS_OK, TEST_INSTRUCTION_PAGE_SIZE_BYTES,
            TEST_INSTRUCTION_PARTITION_CAPACITY_BYTES );
    }

    void Initialise( void )
    {
        ASSERT_TRUE( INSTRUCTION_BUFFER_Init() );
    }

    void Prepare( uint32_t instruction_length_bytes )
    {
        Initialise();
        ASSERT_TRUE( INSTRUCTION_BUFFER_PrepareRead( instruction_length_bytes ) );
    }

    static InstructionBufferPageFillLease_T AcquirePage( void )
    {
        InstructionBufferPageFillLease_T lease = {};
        EXPECT_TRUE( INSTRUCTION_BUFFER_AcquireFillPage( &lease ) );
        return lease;
    }
};

/**-----------------------------------------------------------------------------
 *  Initialisation and Preparation Tests
 *------------------------------------------------------------------------------
 */

TEST_F( InstructionBufferTest, InitUsesExternalFlashGeometryAndLeavesReadUnprepared )
{
    ASSERT_TRUE( INSTRUCTION_BUFFER_Init() );

    EXPECT_TRUE( instruction_buffer_context.is_initialised );
    EXPECT_FALSE( instruction_buffer_context.is_read_prepared );
    EXPECT_EQ( TEST_INSTRUCTION_PAGE_SIZE_BYTES, instruction_buffer_context.page_size_bytes );
    EXPECT_EQ( TEST_INSTRUCTION_PARTITION_CAPACITY_BYTES,
               instruction_buffer_context.instruction_partition_capacity_bytes );
    EXPECT_EQ( 1U, instruction_buffer_context.next_page_fill_lease_id );
    EXPECT_FALSE( instruction_buffer_context.active_page_fill_reservation.is_active );
}

TEST_F( InstructionBufferTest, InitRejectsUnavailableOrUnsupportedGeometry )
{
    FLASH_MANAGER_TEST_ConfigureInstructionFlashInfo( EXTERNAL_FLASH_STATUS_NOT_INITIALISED,
                                                      TEST_INSTRUCTION_PAGE_SIZE_BYTES,
                                                      TEST_INSTRUCTION_PARTITION_CAPACITY_BYTES );
    EXPECT_FALSE( INSTRUCTION_BUFFER_Init() );

    FLASH_MANAGER_TEST_ConfigureInstructionFlashInfo( EXTERNAL_FLASH_STATUS_OK, 0U,
                                                      TEST_INSTRUCTION_PARTITION_CAPACITY_BYTES );
    EXPECT_FALSE( INSTRUCTION_BUFFER_Init() );

    FLASH_MANAGER_TEST_ConfigureInstructionFlashInfo( EXTERNAL_FLASH_STATUS_OK,
                                                      EXTERNAL_FLASH_MAX_PAGE_SIZE_BYTES + 1U,
                                                      TEST_INSTRUCTION_PARTITION_CAPACITY_BYTES );
    EXPECT_FALSE( INSTRUCTION_BUFFER_Init() );

    FLASH_MANAGER_TEST_ConfigureInstructionFlashInfo( EXTERNAL_FLASH_STATUS_OK,
                                                      TEST_INSTRUCTION_PAGE_SIZE_BYTES, 0U );
    EXPECT_FALSE( INSTRUCTION_BUFFER_Init() );

    EXPECT_FALSE( instruction_buffer_context.is_initialised );
}

TEST_F( InstructionBufferTest, FailedReinitialisationInvalidatesEarlierGeometry )
{
    Initialise();

    FLASH_MANAGER_TEST_ConfigureInstructionFlashInfo( EXTERNAL_FLASH_STATUS_ERROR,
                                                      TEST_INSTRUCTION_PAGE_SIZE_BYTES,
                                                      TEST_INSTRUCTION_PARTITION_CAPACITY_BYTES );

    EXPECT_FALSE( INSTRUCTION_BUFFER_Init() );
    EXPECT_FALSE( instruction_buffer_context.is_initialised );
    EXPECT_EQ( 0U, instruction_buffer_context.page_size_bytes );
    EXPECT_EQ( 0U, instruction_buffer_context.instruction_partition_capacity_bytes );
}

TEST_F( InstructionBufferTest, PrepareReadRejectsUnavailableOrOversizedImage )
{
    EXPECT_FALSE( INSTRUCTION_BUFFER_PrepareRead( 1U ) );

    Initialise();
    EXPECT_FALSE(
        INSTRUCTION_BUFFER_PrepareRead( TEST_INSTRUCTION_PARTITION_CAPACITY_BYTES + 1U ) );
    EXPECT_FALSE( instruction_buffer_context.is_read_prepared );
}

TEST_F( InstructionBufferTest, PrepareReadAcceptsEmptyImageAndResetsAllCursors )
{
    Initialise();

    instruction_buffer_context.next_nand_read_offset_bytes  = 7U;
    instruction_buffer_context.next_fill_page_index         = 2U;
    instruction_buffer_context.consumer_stream_offset_bytes = 6U;
    instruction_buffer_context.consumer_page_index          = 1U;
    instruction_buffer_context.consumer_page_offset_bytes   = 5U;
    instruction_buffer_context.page_states[1]               = INSTRUCTION_BUFFER_PAGE_READY;
    instruction_buffer_context.page_valid_bytes[1]          = 4U;
    instruction_buffer_context.page_stream_offsets_bytes[1] = 32U;

    ASSERT_TRUE( INSTRUCTION_BUFFER_PrepareRead( 0U ) );

    EXPECT_TRUE( instruction_buffer_context.is_read_prepared );
    EXPECT_EQ( 0U, instruction_buffer_context.instruction_length_bytes );
    EXPECT_EQ( 0U, instruction_buffer_context.next_nand_read_offset_bytes );
    EXPECT_EQ( 0U, instruction_buffer_context.next_fill_page_index );
    EXPECT_EQ( 0U, instruction_buffer_context.consumer_stream_offset_bytes );
    EXPECT_EQ( 0U, instruction_buffer_context.consumer_page_index );
    EXPECT_EQ( 0U, instruction_buffer_context.consumer_page_offset_bytes );

    for ( uint32_t page_index = 0U; page_index < INSTRUCTION_BUFFER_PAGE_COUNT; page_index++ )
    {
        EXPECT_EQ( INSTRUCTION_BUFFER_PAGE_EMPTY,
                   instruction_buffer_context.page_states[page_index] );
        EXPECT_EQ( 0U, instruction_buffer_context.page_valid_bytes[page_index] );
        EXPECT_EQ( 0U, instruction_buffer_context.page_stream_offsets_bytes[page_index] );
    }

    InstructionBufferPageFillLease_T lease = {};
    EXPECT_FALSE( INSTRUCTION_BUFFER_AcquireFillPage( &lease ) );
}

TEST_F( InstructionBufferTest, PrepareReadInvalidatesActiveLeaseAndPreservesLeaseSequence )
{
    Prepare( TEST_INSTRUCTION_PAGE_SIZE_BYTES );
    InstructionBufferPageFillLease_T stale_lease = AcquirePage();
    uint32_t next_lease_id = instruction_buffer_context.next_page_fill_lease_id;

    ASSERT_TRUE( INSTRUCTION_BUFFER_PrepareRead( TEST_INSTRUCTION_PAGE_SIZE_BYTES ) );

    EXPECT_FALSE( instruction_buffer_context.active_page_fill_reservation.is_active );
    EXPECT_EQ( next_lease_id, instruction_buffer_context.next_page_fill_lease_id );
    EXPECT_FALSE( INSTRUCTION_BUFFER_CompleteFillPage( &stale_lease, true ) );
}

/**-----------------------------------------------------------------------------
 *  Page Acquisition Tests
 *------------------------------------------------------------------------------
 */

TEST_F( InstructionBufferTest, AcquireFillPageRejectsInvalidStateAndClearsOutput )
{
    InstructionBufferPageFillLease_T lease = {
        instruction_buffer_storage,
        1U,
        2U,
        3U,
    };

    EXPECT_FALSE( INSTRUCTION_BUFFER_AcquireFillPage( &lease ) );
    EXPECT_EQ( nullptr, lease.page_data );
    EXPECT_EQ( 0U, lease.instruction_offset_bytes );
    EXPECT_EQ( 0U, lease.read_length_bytes );
    EXPECT_EQ( 0U, lease.lease_id );
    EXPECT_FALSE( INSTRUCTION_BUFFER_AcquireFillPage( nullptr ) );
}

TEST_F( InstructionBufferTest, AcquireFillPageReservesFirstFullPage )
{
    Prepare( TEST_INSTRUCTION_PAGE_SIZE_BYTES * 2U );

    InstructionBufferPageFillLease_T lease = AcquirePage();

    EXPECT_EQ( instruction_buffer_storage, lease.page_data );
    EXPECT_EQ( 0U, lease.instruction_offset_bytes );
    EXPECT_EQ( TEST_INSTRUCTION_PAGE_SIZE_BYTES, lease.read_length_bytes );
    EXPECT_EQ( 1U, lease.lease_id );
    EXPECT_TRUE( instruction_buffer_context.active_page_fill_reservation.is_active );
    EXPECT_EQ( INSTRUCTION_BUFFER_PAGE_FILLING_FROM_NAND,
               instruction_buffer_context.page_states[0] );
    EXPECT_EQ( 0U, instruction_buffer_context.next_nand_read_offset_bytes );
}

TEST_F( InstructionBufferTest, AcquireFillPageAllowsOnlyOneActiveLease )
{
    Prepare( TEST_INSTRUCTION_PAGE_SIZE_BYTES * 2U );
    InstructionBufferPageFillLease_T first_lease  = AcquirePage();
    InstructionBufferPageFillLease_T second_lease = {};

    EXPECT_FALSE( INSTRUCTION_BUFFER_AcquireFillPage( &second_lease ) );
    EXPECT_EQ( nullptr, second_lease.page_data );
    EXPECT_TRUE( INSTRUCTION_BUFFER_CompleteFillPage( &first_lease, true ) );
}

TEST_F( InstructionBufferTest, AcquireFillPageUsesPartialLengthForFinalPage )
{
    Prepare( TEST_INSTRUCTION_PAGE_SIZE_BYTES + 7U );

    InstructionBufferPageFillLease_T first_lease = AcquirePage();
    ASSERT_TRUE( INSTRUCTION_BUFFER_CompleteFillPage( &first_lease, true ) );

    InstructionBufferPageFillLease_T final_lease = AcquirePage();
    EXPECT_EQ( &instruction_buffer_storage[TEST_INSTRUCTION_PAGE_SIZE_BYTES],
               final_lease.page_data );
    EXPECT_EQ( TEST_INSTRUCTION_PAGE_SIZE_BYTES, final_lease.instruction_offset_bytes );
    EXPECT_EQ( 7U, final_lease.read_length_bytes );
}

TEST_F( InstructionBufferTest, AcquireFillPageSkipsZeroWhenLeaseIdentifierWraps )
{
    Prepare( TEST_INSTRUCTION_PAGE_SIZE_BYTES );
    instruction_buffer_context.next_page_fill_lease_id = UINT32_MAX;

    InstructionBufferPageFillLease_T lease = AcquirePage();

    EXPECT_EQ( UINT32_MAX, lease.lease_id );
    EXPECT_EQ( 1U, instruction_buffer_context.next_page_fill_lease_id );
}

/**-----------------------------------------------------------------------------
 *  Page Completion and Backpressure Tests
 *------------------------------------------------------------------------------
 */

TEST_F( InstructionBufferTest, CompleteFillPagePublishesPageAndAdvancesReadPosition )
{
    Prepare( TEST_INSTRUCTION_PAGE_SIZE_BYTES * 2U );
    InstructionBufferPageFillLease_T lease = AcquirePage();

    ASSERT_TRUE( INSTRUCTION_BUFFER_CompleteFillPage( &lease, true ) );

    EXPECT_FALSE( instruction_buffer_context.active_page_fill_reservation.is_active );
    EXPECT_EQ( INSTRUCTION_BUFFER_PAGE_READY, instruction_buffer_context.page_states[0] );
    EXPECT_EQ( TEST_INSTRUCTION_PAGE_SIZE_BYTES, instruction_buffer_context.page_valid_bytes[0] );
    EXPECT_EQ( 0U, instruction_buffer_context.page_stream_offsets_bytes[0] );
    EXPECT_EQ( TEST_INSTRUCTION_PAGE_SIZE_BYTES,
               instruction_buffer_context.next_nand_read_offset_bytes );
    EXPECT_EQ( 1U, instruction_buffer_context.next_fill_page_index );
    EXPECT_FALSE( INSTRUCTION_BUFFER_CompleteFillPage( &lease, true ) );
}

TEST_F( InstructionBufferTest, CompletedFinalPageLeavesNoFurtherReadToAcquire )
{
    Prepare( TEST_INSTRUCTION_PAGE_SIZE_BYTES );
    InstructionBufferPageFillLease_T completed_lease = AcquirePage();
    ASSERT_TRUE( INSTRUCTION_BUFFER_CompleteFillPage( &completed_lease, true ) );

    InstructionBufferPageFillLease_T unavailable_lease = {
        instruction_buffer_storage,
        1U,
        2U,
        3U,
    };

    EXPECT_FALSE( INSTRUCTION_BUFFER_AcquireFillPage( &unavailable_lease ) );
    EXPECT_EQ( nullptr, unavailable_lease.page_data );
    EXPECT_EQ( 0U, unavailable_lease.instruction_offset_bytes );
    EXPECT_EQ( 0U, unavailable_lease.read_length_bytes );
    EXPECT_EQ( 0U, unavailable_lease.lease_id );
}

TEST_F( InstructionBufferTest, FailedFillReleasesSlotWithoutAdvancingAndCanRetry )
{
    Prepare( TEST_INSTRUCTION_PAGE_SIZE_BYTES );
    InstructionBufferPageFillLease_T failed_lease = AcquirePage();

    ASSERT_TRUE( INSTRUCTION_BUFFER_CompleteFillPage( &failed_lease, false ) );

    EXPECT_EQ( INSTRUCTION_BUFFER_PAGE_EMPTY, instruction_buffer_context.page_states[0] );
    EXPECT_EQ( 0U, instruction_buffer_context.page_valid_bytes[0] );
    EXPECT_EQ( 0U, instruction_buffer_context.page_stream_offsets_bytes[0] );
    EXPECT_EQ( 0U, instruction_buffer_context.next_nand_read_offset_bytes );
    EXPECT_EQ( 0U, instruction_buffer_context.next_fill_page_index );
    EXPECT_FALSE( INSTRUCTION_BUFFER_CompleteFillPage( &failed_lease, false ) );

    InstructionBufferPageFillLease_T retry_lease = AcquirePage();
    EXPECT_EQ( 0U, retry_lease.instruction_offset_bytes );
    EXPECT_NE( failed_lease.lease_id, retry_lease.lease_id );
}

TEST_F( InstructionBufferTest, CompleteFillPageRejectsModifiedLeaseAndPreservesOwnership )
{
    Prepare( TEST_INSTRUCTION_PAGE_SIZE_BYTES );
    InstructionBufferPageFillLease_T lease          = AcquirePage();
    InstructionBufferPageFillLease_T modified_lease = lease;
    modified_lease.read_length_bytes--;

    EXPECT_FALSE( INSTRUCTION_BUFFER_CompleteFillPage( &modified_lease, true ) );
    EXPECT_TRUE( instruction_buffer_context.active_page_fill_reservation.is_active );
    EXPECT_EQ( INSTRUCTION_BUFFER_PAGE_FILLING_FROM_NAND,
               instruction_buffer_context.page_states[0] );
    EXPECT_FALSE( INSTRUCTION_BUFFER_CompleteFillPage( nullptr, true ) );
}

TEST_F( InstructionBufferTest, CompleteFillPageRejectsCorruptLengthWithoutPublishing )
{
    Prepare( TEST_INSTRUCTION_PAGE_SIZE_BYTES );
    InstructionBufferPageFillLease_T lease = AcquirePage();

    instruction_buffer_context.instruction_length_bytes = 0U;

    EXPECT_FALSE( INSTRUCTION_BUFFER_CompleteFillPage( &lease, true ) );
    EXPECT_TRUE( instruction_buffer_context.active_page_fill_reservation.is_active );
    EXPECT_EQ( INSTRUCTION_BUFFER_PAGE_FILLING_FROM_NAND,
               instruction_buffer_context.page_states[0] );
}

TEST_F( InstructionBufferTest, ThreeReadyPagesApplyBackpressureWithoutOverwritingData )
{
    Prepare( TEST_INSTRUCTION_PAGE_SIZE_BYTES * INSTRUCTION_BUFFER_PAGE_COUNT + 1U );

    for ( uint32_t page_index = 0U; page_index < INSTRUCTION_BUFFER_PAGE_COUNT; page_index++ )
    {
        InstructionBufferPageFillLease_T lease = AcquirePage();
        ASSERT_TRUE( INSTRUCTION_BUFFER_CompleteFillPage( &lease, true ) );
    }

    InstructionBufferPageFillLease_T blocked_lease = {};
    EXPECT_FALSE( INSTRUCTION_BUFFER_AcquireFillPage( &blocked_lease ) );
    EXPECT_EQ( nullptr, blocked_lease.page_data );
    EXPECT_EQ( 0U, instruction_buffer_context.next_fill_page_index );
    EXPECT_EQ( TEST_INSTRUCTION_PAGE_SIZE_BYTES * INSTRUCTION_BUFFER_PAGE_COUNT,
               instruction_buffer_context.next_nand_read_offset_bytes );

    for ( uint32_t page_index = 0U; page_index < INSTRUCTION_BUFFER_PAGE_COUNT; page_index++ )
    {
        EXPECT_EQ( INSTRUCTION_BUFFER_PAGE_READY,
                   instruction_buffer_context.page_states[page_index] );
        EXPECT_EQ( TEST_INSTRUCTION_PAGE_SIZE_BYTES,
                   instruction_buffer_context.page_valid_bytes[page_index] );
        EXPECT_EQ( TEST_INSTRUCTION_PAGE_SIZE_BYTES * page_index,
                   instruction_buffer_context.page_stream_offsets_bytes[page_index] );
    }
}
