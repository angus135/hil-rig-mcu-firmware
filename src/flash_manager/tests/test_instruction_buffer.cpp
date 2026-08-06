/******************************************************************************
 *  File:       test_instruction_buffer.cpp
 *  Author:     Callum Rafferty
 *  Created:    05-Aug-2026
 *
 *  Description:
 *      Unit tests for instruction-buffer geometry, NAND page-fill ownership,
 *      contiguous instruction views, consumption, and page release.
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
static constexpr uint16_t TEST_FULL_INSTRUCTION_PAGE_PAYLOAD_BYTES = static_cast<uint16_t>(
    TEST_INSTRUCTION_PAGE_SIZE_BYTES - sizeof( FlashManagerInstructionHeader_T ) );

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

    static uint32_t StoreInstruction( uint8_t* destination, uint32_t timestamp,
                                      uint16_t payload_length_bytes, uint8_t payload_seed )
    {
        FlashManagerInstructionHeader_T header = {
            timestamp,
            payload_length_bytes,
            2U,
            3U,
        };

        std::memcpy( destination, &header, sizeof( header ) );

        for ( uint16_t index = 0U; index < payload_length_bytes; index++ )
        {
            destination[sizeof( header ) + index] = static_cast<uint8_t>( payload_seed + index );
        }

        return static_cast<uint32_t>( sizeof( header ) ) + payload_length_bytes;
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

/**-----------------------------------------------------------------------------
 *  Instruction View and Consumption Tests
 *------------------------------------------------------------------------------
 */

TEST_F( InstructionBufferTest, CompletingSlotZeroCopiesItsValidBytesIntoMirror )
{
    Prepare( TEST_INSTRUCTION_PAGE_SIZE_BYTES );
    InstructionBufferPageFillLease_T lease = AcquirePage();

    for ( uint32_t index = 0U; index < lease.read_length_bytes; index++ )
    {
        lease.page_data[index] = static_cast<uint8_t>( 0x40U + index );
    }

    ASSERT_TRUE( INSTRUCTION_BUFFER_CompleteFillPage( &lease, true ) );

    uint32_t mirror_offset_bytes = TEST_INSTRUCTION_PAGE_SIZE_BYTES * INSTRUCTION_BUFFER_PAGE_COUNT;

    EXPECT_EQ( 0, std::memcmp( instruction_buffer_storage,
                               &instruction_buffer_storage[mirror_offset_bytes],
                               TEST_INSTRUCTION_PAGE_SIZE_BYTES ) );
}

TEST_F( InstructionBufferTest, PeekAndConsumeAdvanceWithinPageWithoutRequestingRefill )
{
    constexpr uint16_t first_payload_length_bytes  = 4U;
    constexpr uint16_t second_payload_length_bytes = 3U;
    constexpr uint32_t image_length_bytes          = sizeof( FlashManagerInstructionHeader_T ) * 2U
                                            + first_payload_length_bytes
                                            + second_payload_length_bytes;

    Prepare( image_length_bytes );
    InstructionBufferPageFillLease_T lease = AcquirePage();

    uint32_t second_record_offset =
        StoreInstruction( lease.page_data, 100U, first_payload_length_bytes, 0x10U );
    StoreInstruction( &lease.page_data[second_record_offset], 200U, second_payload_length_bytes,
                      0x20U );
    ASSERT_TRUE( INSTRUCTION_BUFFER_CompleteFillPage( &lease, true ) );

    const FlashManagerInstructionView_T* first_view = nullptr;
    ASSERT_EQ( INSTRUCTION_BUFFER_PEEK_AVAILABLE,
               INSTRUCTION_BUFFER_PeekInstruction( &first_view ) );
    ASSERT_NE( nullptr, first_view );
    EXPECT_EQ( 100U, first_view->header.timestamp );
    EXPECT_EQ( first_payload_length_bytes, first_view->header.payload_length_bytes );
    EXPECT_EQ( 0x10U, first_view->payload[0] );

    EXPECT_EQ( INSTRUCTION_BUFFER_CONSUME_OK, INSTRUCTION_BUFFER_ConsumeInstruction( first_view ) );
    EXPECT_EQ( second_record_offset, instruction_buffer_context.consumer_stream_offset_bytes );
    EXPECT_EQ( second_record_offset, instruction_buffer_context.consumer_page_offset_bytes );
    EXPECT_EQ( INSTRUCTION_BUFFER_PAGE_READY, instruction_buffer_context.page_states[0] );

    const FlashManagerInstructionView_T* second_view = nullptr;
    ASSERT_EQ( INSTRUCTION_BUFFER_PEEK_AVAILABLE,
               INSTRUCTION_BUFFER_PeekInstruction( &second_view ) );
    ASSERT_NE( nullptr, second_view );
    EXPECT_EQ( 200U, second_view->header.timestamp );
    EXPECT_FALSE( INSTRUCTION_BUFFER_IsReadComplete() );
}

TEST_F( InstructionBufferTest, PeekWaitsUntilCompleteCrossPageRecordIsBuffered )
{
    constexpr uint16_t payload_length_bytes      = 20U;
    constexpr uint32_t first_record_length_bytes = sizeof( FlashManagerInstructionHeader_T );
    constexpr uint32_t second_record_length_bytes =
        sizeof( FlashManagerInstructionHeader_T ) + payload_length_bytes;
    constexpr uint32_t image_length_bytes = first_record_length_bytes + second_record_length_bytes;

    uint8_t image[image_length_bytes] = {};
    ASSERT_EQ( first_record_length_bytes, StoreInstruction( image, 50U, 0U, 0U ) );
    ASSERT_EQ( second_record_length_bytes, StoreInstruction( &image[first_record_length_bytes],
                                                             100U, payload_length_bytes, 0x30U ) );

    Prepare( image_length_bytes );
    InstructionBufferPageFillLease_T first_lease = AcquirePage();
    std::memcpy( first_lease.page_data, image, first_lease.read_length_bytes );
    ASSERT_TRUE( INSTRUCTION_BUFFER_CompleteFillPage( &first_lease, true ) );

    const FlashManagerInstructionView_T* view = nullptr;
    ASSERT_EQ( INSTRUCTION_BUFFER_PEEK_AVAILABLE, INSTRUCTION_BUFFER_PeekInstruction( &view ) );
    ASSERT_EQ( INSTRUCTION_BUFFER_CONSUME_OK, INSTRUCTION_BUFFER_ConsumeInstruction( view ) );

    EXPECT_EQ( INSTRUCTION_BUFFER_PEEK_NOT_BUFFERED, INSTRUCTION_BUFFER_PeekInstruction( &view ) );
    EXPECT_EQ( nullptr, view );

    InstructionBufferPageFillLease_T final_lease = AcquirePage();
    std::memcpy( final_lease.page_data, &image[final_lease.instruction_offset_bytes],
                 final_lease.read_length_bytes );
    ASSERT_TRUE( INSTRUCTION_BUFFER_CompleteFillPage( &final_lease, true ) );

    ASSERT_EQ( INSTRUCTION_BUFFER_PEEK_AVAILABLE, INSTRUCTION_BUFFER_PeekInstruction( &view ) );
    ASSERT_NE( nullptr, view );
    EXPECT_EQ( 0, std::memcmp(
                      view->payload,
                      &image[first_record_length_bytes + sizeof( FlashManagerInstructionHeader_T )],
                      payload_length_bytes ) );
}

TEST_F( InstructionBufferTest, ConsumeRejectsCopiedViewWithoutAdvancingActiveInstruction )
{
    constexpr uint16_t payload_length_bytes = 4U;
    constexpr uint32_t record_length_bytes =
        sizeof( FlashManagerInstructionHeader_T ) + payload_length_bytes;

    Prepare( record_length_bytes );
    InstructionBufferPageFillLease_T lease = AcquirePage();
    StoreInstruction( lease.page_data, 100U, payload_length_bytes, 0x50U );
    ASSERT_TRUE( INSTRUCTION_BUFFER_CompleteFillPage( &lease, true ) );

    const FlashManagerInstructionView_T* view = nullptr;
    ASSERT_EQ( INSTRUCTION_BUFFER_PEEK_AVAILABLE, INSTRUCTION_BUFFER_PeekInstruction( &view ) );
    FlashManagerInstructionView_T copied_view = *view;

    EXPECT_EQ( INSTRUCTION_BUFFER_CONSUME_INVALID_VIEW,
               INSTRUCTION_BUFFER_ConsumeInstruction( &copied_view ) );
    EXPECT_EQ( 0U, instruction_buffer_context.consumer_stream_offset_bytes );
    EXPECT_TRUE( instruction_buffer_context.active_instruction_view.is_active );

    EXPECT_EQ( INSTRUCTION_BUFFER_CONSUME_OK, INSTRUCTION_BUFFER_ConsumeInstruction( view ) );
    EXPECT_TRUE( INSTRUCTION_BUFFER_IsReadComplete() );
}

TEST_F( InstructionBufferTest, ConsumingExhaustedPageRequestsRefillWhenNandDataRemains )
{
    Prepare( TEST_INSTRUCTION_PAGE_SIZE_BYTES * ( INSTRUCTION_BUFFER_PAGE_COUNT + 1U ) );

    for ( uint32_t page_index = 0U; page_index < INSTRUCTION_BUFFER_PAGE_COUNT; page_index++ )
    {
        InstructionBufferPageFillLease_T lease = AcquirePage();
        ASSERT_EQ( TEST_INSTRUCTION_PAGE_SIZE_BYTES,
                   StoreInstruction( lease.page_data, page_index,
                                     TEST_FULL_INSTRUCTION_PAGE_PAYLOAD_BYTES,
                                     static_cast<uint8_t>( page_index ) ) );
        ASSERT_TRUE( INSTRUCTION_BUFFER_CompleteFillPage( &lease, true ) );
    }

    const FlashManagerInstructionView_T* view = nullptr;
    ASSERT_EQ( INSTRUCTION_BUFFER_PEEK_AVAILABLE, INSTRUCTION_BUFFER_PeekInstruction( &view ) );

    EXPECT_EQ( INSTRUCTION_BUFFER_CONSUME_REFILL_REQUIRED,
               INSTRUCTION_BUFFER_ConsumeInstruction( view ) );
    EXPECT_EQ( INSTRUCTION_BUFFER_PAGE_EMPTY, instruction_buffer_context.page_states[0] );
    EXPECT_EQ( 1U, instruction_buffer_context.consumer_page_index );
    EXPECT_EQ( 0U, instruction_buffer_context.consumer_page_offset_bytes );

    InstructionBufferPageFillLease_T refill_lease = AcquirePage();
    EXPECT_EQ( TEST_INSTRUCTION_PAGE_SIZE_BYTES * INSTRUCTION_BUFFER_PAGE_COUNT,
               refill_lease.instruction_offset_bytes );
    EXPECT_EQ( instruction_buffer_storage, refill_lease.page_data );
}

TEST_F( InstructionBufferTest, RingMirrorMakesCrossBoundaryInstructionPayloadContiguous )
{
    constexpr uint32_t image_length_bytes        = TEST_INSTRUCTION_PAGE_SIZE_BYTES * 3U + 8U;
    uint8_t            image[image_length_bytes] = {};
    uint32_t           image_offset_bytes        = 0U;

    image_offset_bytes += StoreInstruction( &image[image_offset_bytes], 10U,
                                            TEST_FULL_INSTRUCTION_PAGE_PAYLOAD_BYTES, 0x10U );
    image_offset_bytes += StoreInstruction( &image[image_offset_bytes], 20U,
                                            TEST_FULL_INSTRUCTION_PAGE_PAYLOAD_BYTES, 0x20U );
    image_offset_bytes += StoreInstruction( &image[image_offset_bytes], 30U, 20U, 0x30U );
    image_offset_bytes += StoreInstruction( &image[image_offset_bytes], 40U, 4U, 0x40U );
    ASSERT_EQ( image_length_bytes, image_offset_bytes );

    Prepare( image_length_bytes );

    for ( uint32_t page_index = 0U; page_index < INSTRUCTION_BUFFER_PAGE_COUNT; page_index++ )
    {
        InstructionBufferPageFillLease_T lease = AcquirePage();
        std::memcpy( lease.page_data, &image[lease.instruction_offset_bytes],
                     lease.read_length_bytes );
        ASSERT_TRUE( INSTRUCTION_BUFFER_CompleteFillPage( &lease, true ) );
    }

    const FlashManagerInstructionView_T* view = nullptr;
    ASSERT_EQ( INSTRUCTION_BUFFER_PEEK_AVAILABLE, INSTRUCTION_BUFFER_PeekInstruction( &view ) );
    ASSERT_EQ( INSTRUCTION_BUFFER_CONSUME_REFILL_REQUIRED,
               INSTRUCTION_BUFFER_ConsumeInstruction( view ) );

    InstructionBufferPageFillLease_T refill_lease = AcquirePage();
    std::memcpy( refill_lease.page_data, &image[refill_lease.instruction_offset_bytes],
                 refill_lease.read_length_bytes );
    ASSERT_TRUE( INSTRUCTION_BUFFER_CompleteFillPage( &refill_lease, true ) );

    ASSERT_EQ( INSTRUCTION_BUFFER_PEEK_AVAILABLE, INSTRUCTION_BUFFER_PeekInstruction( &view ) );
    ASSERT_EQ( INSTRUCTION_BUFFER_CONSUME_OK, INSTRUCTION_BUFFER_ConsumeInstruction( view ) );
    ASSERT_EQ( INSTRUCTION_BUFFER_PEEK_AVAILABLE, INSTRUCTION_BUFFER_PeekInstruction( &view ) );
    ASSERT_EQ( INSTRUCTION_BUFFER_CONSUME_OK, INSTRUCTION_BUFFER_ConsumeInstruction( view ) );

    ASSERT_EQ( INSTRUCTION_BUFFER_PEEK_AVAILABLE, INSTRUCTION_BUFFER_PeekInstruction( &view ) );
    ASSERT_NE( nullptr, view );
    EXPECT_EQ( 40U, view->header.timestamp );
    EXPECT_EQ( 4U, view->header.payload_length_bytes );
    EXPECT_EQ( 0, std::memcmp( view->payload, &image[image_length_bytes - 4U], 4U ) );

    EXPECT_EQ( INSTRUCTION_BUFFER_CONSUME_OK, INSTRUCTION_BUFFER_ConsumeInstruction( view ) );
    EXPECT_EQ( INSTRUCTION_BUFFER_PAGE_EMPTY, instruction_buffer_context.page_states[2] );
    EXPECT_EQ( INSTRUCTION_BUFFER_PAGE_EMPTY, instruction_buffer_context.page_states[0] );
    EXPECT_EQ( INSTRUCTION_BUFFER_PEEK_END_OF_STREAM, INSTRUCTION_BUFFER_PeekInstruction( &view ) );
    EXPECT_TRUE( INSTRUCTION_BUFFER_IsReadComplete() );
}
