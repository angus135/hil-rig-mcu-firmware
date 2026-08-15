/******************************************************************************
 *  File:       test_instruction_buffer.cpp
 *  Author:     Callum Rafferty
 *  Created:    05-Aug-2026
 *
 *  Description:
 *      Unit tests for instruction-buffer geometry, cached instruction views,
 *      consume/refill behavior, streamed host upload, NAND page ownership, and
 *      lifecycle completion, including atomic publication after preemptible
 *      ring-mirror preparation.
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

#include <array>
#include <cstddef>
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

static uint32_t test_critical_enter_calls           = 0U;
static uint32_t test_critical_exit_calls            = 0U;
static uint32_t test_critical_depth                 = 0U;
static bool     test_mirror_ready_at_critical_entry = false;
static void ( *test_critical_entry_hook )( void )   = nullptr;

static void TEST_EnterCritical( void )
{
    test_critical_enter_calls++;
    test_critical_depth++;

    if ( test_critical_entry_hook != nullptr )
    {
        test_critical_entry_hook();
    }
}

static void TEST_ExitCritical( void )
{
    test_critical_exit_calls++;

    if ( test_critical_depth > 0U )
    {
        test_critical_depth--;
    }
}

/**-----------------------------------------------------------------------------
 *  Module Under Test
 *------------------------------------------------------------------------------
 */

#undef taskENTER_CRITICAL
#undef taskEXIT_CRITICAL
#define taskENTER_CRITICAL() TEST_EnterCritical()
#define taskEXIT_CRITICAL() TEST_ExitCritical()

extern "C"
{
#if defined( __GNUC__ )
/*
 * The production implementation is C11. It is included here as C++ solely to
 * expose private module state, so suppress diagnostics for valid C aggregate
 * syntax that would otherwise require C++20 in this test translation unit.
 */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wc++20-extensions"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wpedantic"
#endif
#include "../instruction_buffer.c" /* Private module under test */  // NOLINT
#if defined( __GNUC__ )
#pragma GCC diagnostic pop
#endif
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

        test_critical_enter_calls           = 0U;
        test_critical_exit_calls            = 0U;
        test_critical_depth                 = 0U;
        test_mirror_ready_at_critical_entry = false;
        test_critical_entry_hook            = nullptr;
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

    void PrepareUpload( uint32_t expected_length_bytes )
    {
        Initialise();
        ASSERT_TRUE( INSTRUCTION_BUFFER_PrepareUpload( expected_length_bytes ) );
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

    static void FillBytes( uint8_t* destination, std::size_t length, uint8_t seed )
    {
        for ( std::size_t index = 0U; index < length; index++ )
        {
            destination[index] = static_cast<uint8_t>( seed + index );
        }
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

    ASSERT_TRUE( INSTRUCTION_BUFFER_PrepareRead( 0U ) );

    EXPECT_TRUE( instruction_buffer_context.is_read_prepared );
    EXPECT_EQ( 0U, instruction_buffer_context.instruction_length_bytes );
    EXPECT_EQ( 0U, instruction_buffer_context.next_nand_read_offset_bytes );
    EXPECT_EQ( 0U, instruction_buffer_context.next_fill_page_index );
    EXPECT_EQ( 0U, instruction_buffer_context.consumer_stream_offset_bytes );
    EXPECT_EQ( 0U, instruction_buffer_context.consumer_page_index );
    EXPECT_EQ( 0U, instruction_buffer_context.consumer_page_offset_bytes );
    EXPECT_EQ( instruction_buffer_storage, instruction_buffer_context.consumer_record_pointer );

    for ( uint32_t page_index = 0U; page_index < INSTRUCTION_BUFFER_PAGE_COUNT; page_index++ )
    {
        EXPECT_EQ( INSTRUCTION_BUFFER_PAGE_EMPTY,
                   instruction_buffer_context.page_states[page_index] );
        EXPECT_EQ( 0U, instruction_buffer_context.page_valid_bytes[page_index] );
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

    test_critical_entry_hook = []() {
        uint32_t mirror_offset_bytes =
            TEST_INSTRUCTION_PAGE_SIZE_BYTES * INSTRUCTION_BUFFER_PAGE_COUNT;

        test_mirror_ready_at_critical_entry =
            std::memcmp( instruction_buffer_storage,
                         &instruction_buffer_storage[mirror_offset_bytes],
                         TEST_INSTRUCTION_PAGE_SIZE_BYTES )
            == 0;
    };

    ASSERT_TRUE( INSTRUCTION_BUFFER_CompleteFillPage( &lease, true ) );

    uint32_t mirror_offset_bytes = TEST_INSTRUCTION_PAGE_SIZE_BYTES * INSTRUCTION_BUFFER_PAGE_COUNT;

    EXPECT_EQ( 0, std::memcmp( instruction_buffer_storage,
                               &instruction_buffer_storage[mirror_offset_bytes],
                               TEST_INSTRUCTION_PAGE_SIZE_BYTES ) );
    EXPECT_TRUE( test_mirror_ready_at_critical_entry );
    EXPECT_EQ( 1U, test_critical_enter_calls );
    EXPECT_EQ( 1U, test_critical_exit_calls );
    EXPECT_EQ( 0U, test_critical_depth );
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

    /* A later execution tick must reuse the prepared view without reparsing RAM. */
    constexpr uint32_t modified_backing_timestamp = 999U;
    std::memcpy( instruction_buffer_storage, &modified_backing_timestamp,
                 sizeof( modified_backing_timestamp ) );

    const FlashManagerInstructionView_T* repeated_view = nullptr;
    ASSERT_EQ( INSTRUCTION_BUFFER_PEEK_AVAILABLE,
               INSTRUCTION_BUFFER_PeekInstruction( &repeated_view ) );
    EXPECT_EQ( first_view, repeated_view );
    EXPECT_EQ( 100U, repeated_view->header.timestamp );
    EXPECT_EQ( 0U, instruction_buffer_context.consumer_stream_offset_bytes );

    EXPECT_EQ( INSTRUCTION_BUFFER_CONSUME_OK, INSTRUCTION_BUFFER_ConsumeInstruction() );
    EXPECT_EQ( 0U, instruction_buffer_context.instruction_cache.record_length_bytes );
    EXPECT_EQ( second_record_offset, instruction_buffer_context.consumer_stream_offset_bytes );
    EXPECT_EQ( second_record_offset, instruction_buffer_context.consumer_page_offset_bytes );
    EXPECT_EQ( &instruction_buffer_storage[second_record_offset],
               instruction_buffer_context.consumer_record_pointer );
    EXPECT_EQ( INSTRUCTION_BUFFER_PAGE_READY, instruction_buffer_context.page_states[0] );

    const FlashManagerInstructionView_T* second_view = nullptr;
    ASSERT_EQ( INSTRUCTION_BUFFER_PEEK_AVAILABLE,
               INSTRUCTION_BUFFER_PeekInstruction( &second_view ) );
    ASSERT_NE( nullptr, second_view );
    EXPECT_EQ( 200U, second_view->header.timestamp );
    EXPECT_LT( instruction_buffer_context.consumer_stream_offset_bytes,
               instruction_buffer_context.instruction_length_bytes );
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
    ASSERT_EQ( INSTRUCTION_BUFFER_CONSUME_OK, INSTRUCTION_BUFFER_ConsumeInstruction() );

    EXPECT_EQ( INSTRUCTION_BUFFER_PEEK_NOT_BUFFERED, INSTRUCTION_BUFFER_PeekInstruction( &view ) );

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

TEST_F( InstructionBufferTest, EndReadClearsCurrentViewWithoutChangingGeometry )
{
    constexpr uint16_t payload_length_bytes = 4U;
    constexpr uint32_t record_length_bytes =
        sizeof( FlashManagerInstructionHeader_T ) + payload_length_bytes;

    Prepare( record_length_bytes );
    InstructionBufferPageFillLease_T lease = AcquirePage();
    StoreInstruction( lease.page_data, 100U, payload_length_bytes, 0x60U );
    ASSERT_TRUE( INSTRUCTION_BUFFER_CompleteFillPage( &lease, true ) );

    const FlashManagerInstructionView_T* view = nullptr;
    ASSERT_EQ( INSTRUCTION_BUFFER_PEEK_AVAILABLE, INSTRUCTION_BUFFER_PeekInstruction( &view ) );
    uint32_t next_fill_lease_id = instruction_buffer_context.next_page_fill_lease_id;

    INSTRUCTION_BUFFER_EndRead();

    EXPECT_TRUE( instruction_buffer_context.is_initialised );
    EXPECT_FALSE( instruction_buffer_context.is_read_prepared );
    EXPECT_EQ( TEST_INSTRUCTION_PAGE_SIZE_BYTES, instruction_buffer_context.page_size_bytes );
    EXPECT_EQ( next_fill_lease_id, instruction_buffer_context.next_page_fill_lease_id );
    EXPECT_EQ( 0U, instruction_buffer_context.instruction_cache.record_length_bytes );
    EXPECT_EQ( instruction_buffer_storage, instruction_buffer_context.consumer_record_pointer );

    for ( uint32_t page_index = 0U; page_index < INSTRUCTION_BUFFER_PAGE_COUNT; page_index++ )
    {
        EXPECT_EQ( INSTRUCTION_BUFFER_PAGE_EMPTY,
                   instruction_buffer_context.page_states[page_index] );
        EXPECT_EQ( 0U, instruction_buffer_context.page_valid_bytes[page_index] );
    }
}

TEST_F( InstructionBufferTest, EndReadInvalidatesActivePageFillLease )
{
    Prepare( TEST_INSTRUCTION_PAGE_SIZE_BYTES );
    InstructionBufferPageFillLease_T lease = AcquirePage();

    INSTRUCTION_BUFFER_EndRead();

    EXPECT_FALSE( instruction_buffer_context.active_page_fill_reservation.is_active );
    EXPECT_EQ( INSTRUCTION_BUFFER_PAGE_EMPTY, instruction_buffer_context.page_states[0] );
    EXPECT_FALSE( INSTRUCTION_BUFFER_CompleteFillPage( &lease, true ) );
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
               INSTRUCTION_BUFFER_ConsumeInstruction() );
    EXPECT_EQ( INSTRUCTION_BUFFER_PAGE_EMPTY, instruction_buffer_context.page_states[0] );
    EXPECT_EQ( 1U, instruction_buffer_context.consumer_page_index );
    EXPECT_EQ( 0U, instruction_buffer_context.consumer_page_offset_bytes );
    EXPECT_EQ( &instruction_buffer_storage[TEST_INSTRUCTION_PAGE_SIZE_BYTES],
               instruction_buffer_context.consumer_record_pointer );

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
               INSTRUCTION_BUFFER_ConsumeInstruction() );

    InstructionBufferPageFillLease_T refill_lease = AcquirePage();
    std::memcpy( refill_lease.page_data, &image[refill_lease.instruction_offset_bytes],
                 refill_lease.read_length_bytes );
    ASSERT_TRUE( INSTRUCTION_BUFFER_CompleteFillPage( &refill_lease, true ) );

    ASSERT_EQ( INSTRUCTION_BUFFER_PEEK_AVAILABLE, INSTRUCTION_BUFFER_PeekInstruction( &view ) );
    ASSERT_EQ( INSTRUCTION_BUFFER_CONSUME_OK, INSTRUCTION_BUFFER_ConsumeInstruction() );
    ASSERT_EQ( INSTRUCTION_BUFFER_PEEK_AVAILABLE, INSTRUCTION_BUFFER_PeekInstruction( &view ) );
    ASSERT_EQ( INSTRUCTION_BUFFER_CONSUME_OK, INSTRUCTION_BUFFER_ConsumeInstruction() );

    ASSERT_EQ( INSTRUCTION_BUFFER_PEEK_AVAILABLE, INSTRUCTION_BUFFER_PeekInstruction( &view ) );
    ASSERT_NE( nullptr, view );
    EXPECT_EQ( 40U, view->header.timestamp );
    EXPECT_EQ( 4U, view->header.payload_length_bytes );
    EXPECT_EQ( 0, std::memcmp( view->payload, &image[image_length_bytes - 4U], 4U ) );

    EXPECT_EQ( INSTRUCTION_BUFFER_CONSUME_OK, INSTRUCTION_BUFFER_ConsumeInstruction() );
    EXPECT_EQ( INSTRUCTION_BUFFER_PAGE_EMPTY, instruction_buffer_context.page_states[2] );
    EXPECT_EQ( INSTRUCTION_BUFFER_PAGE_EMPTY, instruction_buffer_context.page_states[0] );
    EXPECT_EQ( INSTRUCTION_BUFFER_PEEK_END_OF_STREAM, INSTRUCTION_BUFFER_PeekInstruction( &view ) );
    EXPECT_EQ( instruction_buffer_context.instruction_length_bytes,
               instruction_buffer_context.consumer_stream_offset_bytes );
}

/**-----------------------------------------------------------------------------
 *  Instruction Upload Preparation and Producer Tests
 *------------------------------------------------------------------------------
 */

TEST_F( InstructionBufferTest, PrepareUploadValidatesLengthAndExclusiveStorageOwnership )
{
    EXPECT_FALSE( INSTRUCTION_BUFFER_PrepareUpload( 1U ) );

    Initialise();
    EXPECT_FALSE( INSTRUCTION_BUFFER_PrepareUpload( 0U ) );
    EXPECT_FALSE(
        INSTRUCTION_BUFFER_PrepareUpload( TEST_INSTRUCTION_PARTITION_CAPACITY_BYTES + 1U ) );

    ASSERT_TRUE( INSTRUCTION_BUFFER_PrepareRead( TEST_INSTRUCTION_PAGE_SIZE_BYTES ) );
    EXPECT_FALSE( INSTRUCTION_BUFFER_PrepareUpload( TEST_INSTRUCTION_PAGE_SIZE_BYTES ) );

    INSTRUCTION_BUFFER_EndRead();
    ASSERT_TRUE( INSTRUCTION_BUFFER_PrepareUpload( TEST_INSTRUCTION_PAGE_SIZE_BYTES ) );

    EXPECT_TRUE( instruction_buffer_context.is_upload_prepared );
    EXPECT_FALSE( instruction_buffer_context.is_upload_finalised );
    EXPECT_EQ( TEST_INSTRUCTION_PAGE_SIZE_BYTES,
               instruction_buffer_context.upload_expected_length_bytes );
    EXPECT_EQ( 0U, instruction_buffer_context.upload_accepted_length_bytes );
    EXPECT_EQ( 0U, instruction_buffer_context.upload_persisted_length_bytes );
    EXPECT_FALSE( INSTRUCTION_BUFFER_PrepareRead( TEST_INSTRUCTION_PAGE_SIZE_BYTES ) );

    uint32_t expected_length_bytes = 0U;
    EXPECT_TRUE( INSTRUCTION_BUFFER_GetUploadExpectedLength( &expected_length_bytes ) );
    EXPECT_EQ( TEST_INSTRUCTION_PAGE_SIZE_BYTES, expected_length_bytes );
    EXPECT_FALSE( INSTRUCTION_BUFFER_GetUploadExpectedLength( nullptr ) );

    INSTRUCTION_BUFFER_EndRead();
    EXPECT_TRUE( instruction_buffer_context.is_upload_prepared );
}

TEST_F( InstructionBufferTest, WriteUploadBytesRejectsInvalidStateAndArgumentsWithoutMutation )
{
    std::array<uint8_t, TEST_INSTRUCTION_PAGE_SIZE_BYTES + 1U> data = {};

    EXPECT_EQ( INSTRUCTION_BUFFER_UPLOAD_WRITE_INVALID_STATE,
               INSTRUCTION_BUFFER_WriteUploadBytes( data.data(), 1U ) );

    PrepareUpload( TEST_INSTRUCTION_PAGE_SIZE_BYTES );

    EXPECT_EQ( INSTRUCTION_BUFFER_UPLOAD_WRITE_INVALID_ARGUMENT,
               INSTRUCTION_BUFFER_WriteUploadBytes( nullptr, 1U ) );
    EXPECT_EQ( INSTRUCTION_BUFFER_UPLOAD_WRITE_INVALID_ARGUMENT,
               INSTRUCTION_BUFFER_WriteUploadBytes( data.data(), 0U ) );
    EXPECT_EQ( INSTRUCTION_BUFFER_UPLOAD_WRITE_INVALID_ARGUMENT,
               INSTRUCTION_BUFFER_WriteUploadBytes( data.data(), data.size() ) );
    EXPECT_EQ( 0U, instruction_buffer_context.upload_accepted_length_bytes );
    EXPECT_EQ( INSTRUCTION_BUFFER_PAGE_EMPTY, instruction_buffer_context.page_states[0] );
}

TEST_F( InstructionBufferTest, WriteUploadBytesRejectsDataBeyondDeclaredRemainingLength )
{
    std::array<uint8_t, 20U> first_chunk         = {};
    std::array<uint8_t, 13U> oversized_remainder = {};
    PrepareUpload( TEST_INSTRUCTION_PAGE_SIZE_BYTES );

    ASSERT_EQ( INSTRUCTION_BUFFER_UPLOAD_WRITE_ACCEPTED,
               INSTRUCTION_BUFFER_WriteUploadBytes( first_chunk.data(), first_chunk.size() ) );
    std::array<uint8_t, 20U> page_before = {};
    std::memcpy( page_before.data(), instruction_buffer_storage, page_before.size() );

    EXPECT_EQ( INSTRUCTION_BUFFER_UPLOAD_WRITE_INVALID_ARGUMENT,
               INSTRUCTION_BUFFER_WriteUploadBytes( oversized_remainder.data(),
                                                    oversized_remainder.size() ) );
    EXPECT_EQ( first_chunk.size(), instruction_buffer_context.upload_accepted_length_bytes );
    EXPECT_EQ( first_chunk.size(), instruction_buffer_context.page_valid_bytes[0] );
    EXPECT_EQ( 0,
               std::memcmp( page_before.data(), instruction_buffer_storage, page_before.size() ) );
}

TEST_F( InstructionBufferTest, WriteUploadBytesCopiesPartialPageIntoManagerOwnedStorage )
{
    constexpr uint32_t                      chunk_length_bytes = 10U;
    std::array<uint8_t, chunk_length_bytes> data               = {};
    FillBytes( data.data(), data.size(), 0x20U );

    PrepareUpload( chunk_length_bytes + 1U );

    EXPECT_EQ( INSTRUCTION_BUFFER_UPLOAD_WRITE_ACCEPTED,
               INSTRUCTION_BUFFER_WriteUploadBytes( data.data(), data.size() ) );
    EXPECT_EQ( chunk_length_bytes, instruction_buffer_context.upload_accepted_length_bytes );
    EXPECT_EQ( INSTRUCTION_BUFFER_PAGE_FILLING_FROM_HOST,
               instruction_buffer_context.page_states[0] );
    EXPECT_EQ( chunk_length_bytes, instruction_buffer_context.page_valid_bytes[0] );
    EXPECT_EQ( 0, std::memcmp( data.data(), instruction_buffer_storage, data.size() ) );
    EXPECT_FALSE( INSTRUCTION_BUFFER_IsUploadInputComplete() );
}

TEST_F( InstructionBufferTest, WriteUploadBytesCanCompleteOnePageAndContinueIntoTheNext )
{
    constexpr uint32_t                             first_chunk_length_bytes  = 20U;
    constexpr uint32_t                             second_chunk_length_bytes = 20U;
    std::array<uint8_t, first_chunk_length_bytes>  first_chunk               = {};
    std::array<uint8_t, second_chunk_length_bytes> second_chunk              = {};
    FillBytes( first_chunk.data(), first_chunk.size(), 0x10U );
    FillBytes( second_chunk.data(), second_chunk.size(), 0x50U );

    PrepareUpload( first_chunk.size() + second_chunk.size() );
    ASSERT_EQ( INSTRUCTION_BUFFER_UPLOAD_WRITE_ACCEPTED,
               INSTRUCTION_BUFFER_WriteUploadBytes( first_chunk.data(), first_chunk.size() ) );

    EXPECT_EQ( INSTRUCTION_BUFFER_UPLOAD_WRITE_PAGE_READY,
               INSTRUCTION_BUFFER_WriteUploadBytes( second_chunk.data(), second_chunk.size() ) );
    EXPECT_EQ( INSTRUCTION_BUFFER_PAGE_READY_FOR_NAND, instruction_buffer_context.page_states[0] );
    EXPECT_EQ( TEST_INSTRUCTION_PAGE_SIZE_BYTES, instruction_buffer_context.page_valid_bytes[0] );
    EXPECT_EQ( INSTRUCTION_BUFFER_PAGE_FILLING_FROM_HOST,
               instruction_buffer_context.page_states[1] );
    EXPECT_EQ( 8U, instruction_buffer_context.page_valid_bytes[1] );
    EXPECT_EQ( 1U, instruction_buffer_context.upload_write_page_index );
    EXPECT_EQ( 0,
               std::memcmp( first_chunk.data(), instruction_buffer_storage, first_chunk.size() ) );
    EXPECT_EQ( 0, std::memcmp( second_chunk.data(), &instruction_buffer_storage[first_chunk.size()],
                               12U ) );
    EXPECT_EQ( 0,
               std::memcmp( &second_chunk[12U],
                            &instruction_buffer_storage[TEST_INSTRUCTION_PAGE_SIZE_BYTES], 8U ) );
}

TEST_F( InstructionBufferTest, BusyCrossPageWriteCopiesNothingAndCanBeRetriedUnchanged )
{
    std::array<uint8_t, TEST_INSTRUCTION_PAGE_SIZE_BYTES> full_page     = {};
    std::array<uint8_t, 20U>                              partial_page  = {};
    std::array<uint8_t, 20U>                              blocked_chunk = {};
    FillBytes( full_page.data(), full_page.size(), 0x10U );
    FillBytes( partial_page.data(), partial_page.size(), 0x40U );
    FillBytes( blocked_chunk.data(), blocked_chunk.size(), 0x80U );

    PrepareUpload( TEST_INSTRUCTION_PAGE_SIZE_BYTES * 4U );
    ASSERT_EQ( INSTRUCTION_BUFFER_UPLOAD_WRITE_PAGE_READY,
               INSTRUCTION_BUFFER_WriteUploadBytes( full_page.data(), full_page.size() ) );
    ASSERT_EQ( INSTRUCTION_BUFFER_UPLOAD_WRITE_PAGE_READY,
               INSTRUCTION_BUFFER_WriteUploadBytes( full_page.data(), full_page.size() ) );
    ASSERT_EQ( INSTRUCTION_BUFFER_UPLOAD_WRITE_ACCEPTED,
               INSTRUCTION_BUFFER_WriteUploadBytes( partial_page.data(), partial_page.size() ) );

    std::array<uint8_t, 20U> page_tail_before = {};
    std::memcpy( page_tail_before.data(),
                 &instruction_buffer_storage[TEST_INSTRUCTION_PAGE_SIZE_BYTES * 2U],
                 page_tail_before.size() );
    uint32_t accepted_before = instruction_buffer_context.upload_accepted_length_bytes;

    EXPECT_EQ( INSTRUCTION_BUFFER_UPLOAD_WRITE_BUSY,
               INSTRUCTION_BUFFER_WriteUploadBytes( blocked_chunk.data(), blocked_chunk.size() ) );
    EXPECT_EQ( accepted_before, instruction_buffer_context.upload_accepted_length_bytes );
    EXPECT_EQ( 0, std::memcmp( page_tail_before.data(),
                               &instruction_buffer_storage[TEST_INSTRUCTION_PAGE_SIZE_BYTES * 2U],
                               page_tail_before.size() ) );
    EXPECT_EQ( 20U, instruction_buffer_context.page_valid_bytes[2] );

    const uint8_t* drain_data   = nullptr;
    uint32_t       drain_length = 0U;
    ASSERT_TRUE( INSTRUCTION_BUFFER_AcquireUploadDrainPage( &drain_data, &drain_length ) );
    ASSERT_TRUE( INSTRUCTION_BUFFER_CompleteUploadDrain( true ) );

    EXPECT_EQ( INSTRUCTION_BUFFER_UPLOAD_WRITE_PAGE_READY,
               INSTRUCTION_BUFFER_WriteUploadBytes( blocked_chunk.data(), blocked_chunk.size() ) );
    EXPECT_EQ( INSTRUCTION_BUFFER_PAGE_READY_FOR_NAND, instruction_buffer_context.page_states[2] );
    EXPECT_EQ( INSTRUCTION_BUFFER_PAGE_FILLING_FROM_HOST,
               instruction_buffer_context.page_states[0] );
    EXPECT_EQ( 8U, instruction_buffer_context.page_valid_bytes[0] );
}

/**-----------------------------------------------------------------------------
 *  Instruction Upload NAND Drain Tests
 *------------------------------------------------------------------------------
 */

TEST_F( InstructionBufferTest, AcquireUploadDrainPageClearsOutputsWhenNoPageIsReady )
{
    const uint8_t* page_data          = instruction_buffer_storage;
    uint32_t       valid_length_bytes = 7U;

    EXPECT_FALSE( INSTRUCTION_BUFFER_AcquireUploadDrainPage( &page_data, &valid_length_bytes ) );
    EXPECT_EQ( nullptr, page_data );
    EXPECT_EQ( 0U, valid_length_bytes );

    PrepareUpload( TEST_INSTRUCTION_PAGE_SIZE_BYTES );
    page_data          = instruction_buffer_storage;
    valid_length_bytes = 7U;
    EXPECT_FALSE( INSTRUCTION_BUFFER_AcquireUploadDrainPage( &page_data, &valid_length_bytes ) );
    EXPECT_EQ( nullptr, page_data );
    EXPECT_EQ( 0U, valid_length_bytes );
    EXPECT_FALSE( INSTRUCTION_BUFFER_AcquireUploadDrainPage( nullptr, &valid_length_bytes ) );
    EXPECT_FALSE( INSTRUCTION_BUFFER_AcquireUploadDrainPage( &page_data, nullptr ) );
}

TEST_F( InstructionBufferTest, FullRingCanAcquireOldestPageWhenProducerAndDrainIndicesMatch )
{
    std::array<uint8_t, TEST_INSTRUCTION_PAGE_SIZE_BYTES> page = {};
    PrepareUpload( TEST_INSTRUCTION_PAGE_SIZE_BYTES * INSTRUCTION_BUFFER_PAGE_COUNT );

    for ( uint32_t index = 0U; index < INSTRUCTION_BUFFER_PAGE_COUNT; index++ )
    {
        FillBytes( page.data(), page.size(), static_cast<uint8_t>( index * 0x20U ) );
        ASSERT_EQ( INSTRUCTION_BUFFER_UPLOAD_WRITE_PAGE_READY,
                   INSTRUCTION_BUFFER_WriteUploadBytes( page.data(), page.size() ) );
    }

    ASSERT_EQ( instruction_buffer_context.upload_write_page_index,
               instruction_buffer_context.upload_drain_page_index );

    const uint8_t* page_data          = nullptr;
    uint32_t       valid_length_bytes = 0U;
    ASSERT_TRUE( INSTRUCTION_BUFFER_AcquireUploadDrainPage( &page_data, &valid_length_bytes ) );

    EXPECT_EQ( instruction_buffer_storage, page_data );
    EXPECT_EQ( TEST_INSTRUCTION_PAGE_SIZE_BYTES, valid_length_bytes );
    EXPECT_EQ( INSTRUCTION_BUFFER_PAGE_WRITING_TO_NAND, instruction_buffer_context.page_states[0] );

    const uint8_t* second_page_data = instruction_buffer_storage;
    uint32_t       second_length    = 1U;
    EXPECT_FALSE( INSTRUCTION_BUFFER_AcquireUploadDrainPage( &second_page_data, &second_length ) );
    EXPECT_EQ( nullptr, second_page_data );
    EXPECT_EQ( 0U, second_length );
}

TEST_F( InstructionBufferTest, SuccessfulUploadDrainReleasesPageAndAdvancesPersistedPosition )
{
    std::array<uint8_t, TEST_INSTRUCTION_PAGE_SIZE_BYTES> page = {};
    PrepareUpload( page.size() * 2U );
    ASSERT_EQ( INSTRUCTION_BUFFER_UPLOAD_WRITE_PAGE_READY,
               INSTRUCTION_BUFFER_WriteUploadBytes( page.data(), page.size() ) );
    ASSERT_EQ( INSTRUCTION_BUFFER_UPLOAD_WRITE_PAGE_READY,
               INSTRUCTION_BUFFER_WriteUploadBytes( page.data(), page.size() ) );

    const uint8_t* page_data          = nullptr;
    uint32_t       valid_length_bytes = 0U;
    ASSERT_TRUE( INSTRUCTION_BUFFER_AcquireUploadDrainPage( &page_data, &valid_length_bytes ) );
    ASSERT_TRUE( INSTRUCTION_BUFFER_CompleteUploadDrain( true ) );

    EXPECT_EQ( INSTRUCTION_BUFFER_PAGE_EMPTY, instruction_buffer_context.page_states[0] );
    EXPECT_EQ( 0U, instruction_buffer_context.page_valid_bytes[0] );
    EXPECT_EQ( TEST_INSTRUCTION_PAGE_SIZE_BYTES,
               instruction_buffer_context.upload_persisted_length_bytes );
    EXPECT_EQ( 1U, instruction_buffer_context.upload_drain_page_index );
    EXPECT_FALSE( INSTRUCTION_BUFFER_CompleteUploadDrain( true ) );
}

TEST_F( InstructionBufferTest, FailedUploadDrainRestoresPageForIdenticalRetry )
{
    std::array<uint8_t, TEST_INSTRUCTION_PAGE_SIZE_BYTES> page = {};
    FillBytes( page.data(), page.size(), 0x30U );
    PrepareUpload( page.size() );
    ASSERT_EQ( INSTRUCTION_BUFFER_UPLOAD_WRITE_PAGE_READY,
               INSTRUCTION_BUFFER_WriteUploadBytes( page.data(), page.size() ) );

    const uint8_t* first_page_data = nullptr;
    uint32_t       first_length    = 0U;
    ASSERT_TRUE( INSTRUCTION_BUFFER_AcquireUploadDrainPage( &first_page_data, &first_length ) );
    ASSERT_TRUE( INSTRUCTION_BUFFER_CompleteUploadDrain( false ) );

    EXPECT_EQ( INSTRUCTION_BUFFER_PAGE_READY_FOR_NAND, instruction_buffer_context.page_states[0] );
    EXPECT_EQ( 0U, instruction_buffer_context.upload_persisted_length_bytes );
    EXPECT_EQ( 0U, instruction_buffer_context.upload_drain_page_index );

    const uint8_t* retry_page_data = nullptr;
    uint32_t       retry_length    = 0U;
    ASSERT_TRUE( INSTRUCTION_BUFFER_AcquireUploadDrainPage( &retry_page_data, &retry_length ) );
    EXPECT_EQ( first_page_data, retry_page_data );
    EXPECT_EQ( first_length, retry_length );
    EXPECT_EQ( 0, std::memcmp( page.data(), retry_page_data, retry_length ) );
}

TEST_F( InstructionBufferTest, CompleteUploadDrainRejectsMissingOrInconsistentOwnership )
{
    std::array<uint8_t, TEST_INSTRUCTION_PAGE_SIZE_BYTES> page = {};
    PrepareUpload( page.size() );
    EXPECT_FALSE( INSTRUCTION_BUFFER_CompleteUploadDrain( true ) );

    ASSERT_EQ( INSTRUCTION_BUFFER_UPLOAD_WRITE_PAGE_READY,
               INSTRUCTION_BUFFER_WriteUploadBytes( page.data(), page.size() ) );
    const uint8_t* page_data          = nullptr;
    uint32_t       valid_length_bytes = 0U;
    ASSERT_TRUE( INSTRUCTION_BUFFER_AcquireUploadDrainPage( &page_data, &valid_length_bytes ) );

    instruction_buffer_context.upload_accepted_length_bytes = 0U;
    EXPECT_FALSE( INSTRUCTION_BUFFER_CompleteUploadDrain( true ) );
    EXPECT_EQ( INSTRUCTION_BUFFER_PAGE_WRITING_TO_NAND, instruction_buffer_context.page_states[0] );
}

/**-----------------------------------------------------------------------------
 *  Instruction Upload Finalisation Tests
 *------------------------------------------------------------------------------
 */

TEST_F( InstructionBufferTest, FinaliseUploadRejectsIncompleteInputWithoutPublishingPartialPage )
{
    std::array<uint8_t, 5U> partial = {};
    PrepareUpload( partial.size() + 1U );
    ASSERT_EQ( INSTRUCTION_BUFFER_UPLOAD_WRITE_ACCEPTED,
               INSTRUCTION_BUFFER_WriteUploadBytes( partial.data(), partial.size() ) );

    EXPECT_FALSE( INSTRUCTION_BUFFER_FinaliseUpload() );
    EXPECT_FALSE( instruction_buffer_context.is_upload_finalised );
    EXPECT_EQ( INSTRUCTION_BUFFER_PAGE_FILLING_FROM_HOST,
               instruction_buffer_context.page_states[0] );
}

TEST_F( InstructionBufferTest, FinaliseUploadPublishesFinalPartialPageAndStopsProduction )
{
    constexpr uint32_t                                    final_partial_length_bytes = 5U;
    std::array<uint8_t, TEST_INSTRUCTION_PAGE_SIZE_BYTES> full_page                  = {};
    std::array<uint8_t, final_partial_length_bytes>       partial_page               = {};
    PrepareUpload( full_page.size() + partial_page.size() );
    ASSERT_EQ( INSTRUCTION_BUFFER_UPLOAD_WRITE_PAGE_READY,
               INSTRUCTION_BUFFER_WriteUploadBytes( full_page.data(), full_page.size() ) );
    ASSERT_EQ( INSTRUCTION_BUFFER_UPLOAD_WRITE_ACCEPTED,
               INSTRUCTION_BUFFER_WriteUploadBytes( partial_page.data(), partial_page.size() ) );

    EXPECT_TRUE( INSTRUCTION_BUFFER_IsUploadInputComplete() );
    ASSERT_TRUE( INSTRUCTION_BUFFER_FinaliseUpload() );

    EXPECT_TRUE( instruction_buffer_context.is_upload_finalised );
    EXPECT_EQ( INSTRUCTION_BUFFER_PAGE_READY_FOR_NAND, instruction_buffer_context.page_states[1] );
    EXPECT_EQ( final_partial_length_bytes, instruction_buffer_context.page_valid_bytes[1] );
    EXPECT_EQ( INSTRUCTION_BUFFER_UPLOAD_WRITE_INVALID_STATE,
               INSTRUCTION_BUFFER_WriteUploadBytes( partial_page.data(), 1U ) );
    EXPECT_FALSE( INSTRUCTION_BUFFER_FinaliseUpload() );
}

TEST_F( InstructionBufferTest, PageAlignedFullRingFinalisesWithoutRequiringAnEmptyWritePage )
{
    std::array<uint8_t, TEST_INSTRUCTION_PAGE_SIZE_BYTES> page = {};
    PrepareUpload( page.size() * INSTRUCTION_BUFFER_PAGE_COUNT );

    for ( uint32_t index = 0U; index < INSTRUCTION_BUFFER_PAGE_COUNT; index++ )
    {
        ASSERT_EQ( INSTRUCTION_BUFFER_UPLOAD_WRITE_PAGE_READY,
                   INSTRUCTION_BUFFER_WriteUploadBytes( page.data(), page.size() ) );
    }

    ASSERT_EQ( instruction_buffer_context.upload_write_page_index,
               instruction_buffer_context.upload_drain_page_index );
    EXPECT_TRUE( INSTRUCTION_BUFFER_FinaliseUpload() );
    EXPECT_TRUE( instruction_buffer_context.is_upload_finalised );
}

TEST_F( InstructionBufferTest, FinaliseUploadRejectsPageOwnedByActiveNandWrite )
{
    std::array<uint8_t, TEST_INSTRUCTION_PAGE_SIZE_BYTES> page = {};
    PrepareUpload( page.size() );
    ASSERT_EQ( INSTRUCTION_BUFFER_UPLOAD_WRITE_PAGE_READY,
               INSTRUCTION_BUFFER_WriteUploadBytes( page.data(), page.size() ) );

    const uint8_t* page_data          = nullptr;
    uint32_t       valid_length_bytes = 0U;
    ASSERT_TRUE( INSTRUCTION_BUFFER_AcquireUploadDrainPage( &page_data, &valid_length_bytes ) );

    EXPECT_FALSE( INSTRUCTION_BUFFER_FinaliseUpload() );
    EXPECT_FALSE( instruction_buffer_context.is_upload_finalised );

    ASSERT_TRUE( INSTRUCTION_BUFFER_CompleteUploadDrain( true ) );
    EXPECT_TRUE( INSTRUCTION_BUFFER_FinaliseUpload() );
}

TEST_F( InstructionBufferTest, FullyPersistedUploadCanEndAndReleaseAllSharedState )
{
    constexpr uint32_t                                    final_partial_length_bytes = 5U;
    std::array<uint8_t, TEST_INSTRUCTION_PAGE_SIZE_BYTES> full_page                  = {};
    std::array<uint8_t, final_partial_length_bytes>       partial_page               = {};
    PrepareUpload( full_page.size() + partial_page.size() );
    ASSERT_EQ( INSTRUCTION_BUFFER_UPLOAD_WRITE_PAGE_READY,
               INSTRUCTION_BUFFER_WriteUploadBytes( full_page.data(), full_page.size() ) );
    ASSERT_EQ( INSTRUCTION_BUFFER_UPLOAD_WRITE_ACCEPTED,
               INSTRUCTION_BUFFER_WriteUploadBytes( partial_page.data(), partial_page.size() ) );
    ASSERT_TRUE( INSTRUCTION_BUFFER_FinaliseUpload() );
    EXPECT_FALSE( INSTRUCTION_BUFFER_IsUploadPersisted() );
    EXPECT_FALSE( INSTRUCTION_BUFFER_EndUpload() );

    const uint8_t* page_data          = nullptr;
    uint32_t       valid_length_bytes = 0U;
    ASSERT_TRUE( INSTRUCTION_BUFFER_AcquireUploadDrainPage( &page_data, &valid_length_bytes ) );
    ASSERT_EQ( full_page.size(), valid_length_bytes );
    ASSERT_TRUE( INSTRUCTION_BUFFER_CompleteUploadDrain( true ) );
    EXPECT_FALSE( INSTRUCTION_BUFFER_IsUploadPersisted() );

    ASSERT_TRUE( INSTRUCTION_BUFFER_AcquireUploadDrainPage( &page_data, &valid_length_bytes ) );
    ASSERT_EQ( partial_page.size(), valid_length_bytes );
    ASSERT_TRUE( INSTRUCTION_BUFFER_CompleteUploadDrain( true ) );
    ASSERT_TRUE( INSTRUCTION_BUFFER_IsUploadPersisted() );
    ASSERT_TRUE( INSTRUCTION_BUFFER_EndUpload() );

    EXPECT_FALSE( instruction_buffer_context.is_upload_prepared );
    EXPECT_FALSE( instruction_buffer_context.is_upload_finalised );
    EXPECT_EQ( 0U, instruction_buffer_context.upload_expected_length_bytes );
    EXPECT_EQ( 0U, instruction_buffer_context.upload_accepted_length_bytes );
    EXPECT_EQ( 0U, instruction_buffer_context.upload_persisted_length_bytes );

    for ( uint32_t page_index = 0U; page_index < INSTRUCTION_BUFFER_PAGE_COUNT; page_index++ )
    {
        EXPECT_EQ( INSTRUCTION_BUFFER_PAGE_EMPTY,
                   instruction_buffer_context.page_states[page_index] );
        EXPECT_EQ( 0U, instruction_buffer_context.page_valid_bytes[page_index] );
    }
}
