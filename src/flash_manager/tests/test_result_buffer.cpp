/******************************************************************************
 *  File:       test_result_buffer.cpp
 *  Author:     Callum Rafferty
 *  Created:    05-Aug-2026
 *
 *  Description:
 *      Unit tests for result-buffer initialisation and the result logging
 *      reserve, cancel, and commit flow.
 *
 *  Notes:
 *      Production code is included directly so the tests can verify private
 *      ring-buffer state, following the existing project test convention.
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>

extern "C"
{
#include "external_flash.h"
#include "result_buffer.h"
}

/**-----------------------------------------------------------------------------
 *  Test Constants / Macros
 *------------------------------------------------------------------------------
 */

static constexpr uint32_t TEST_PAGE_SIZE_BYTES = 32U;
static constexpr uint32_t TEST_CAPACITY_BYTES = TEST_PAGE_SIZE_BYTES * 3U;
static constexpr uint16_t TEST_MAX_PAYLOAD_BYTES =
    static_cast<uint16_t>( TEST_PAGE_SIZE_BYTES - sizeof( FlashManagerResultHeader_T ) );

/**-----------------------------------------------------------------------------
 *  Test Doubles / Mocks
 *------------------------------------------------------------------------------
 */

static ExternalFlashStatus_T external_flash_get_info_status = EXTERNAL_FLASH_STATUS_OK;
static ExternalFlashInfo_T   external_flash_info             = {};
static uint32_t              external_flash_get_info_calls   = 0U;

extern "C" ExternalFlashStatus_T EXTERNAL_FLASH_GetInfo( ExternalFlashInfo_T* info )
{
    external_flash_get_info_calls++;

    if ( external_flash_get_info_status != EXTERNAL_FLASH_STATUS_OK )
    {
        return external_flash_get_info_status;
    }

    if ( info == nullptr )
    {
        return EXTERNAL_FLASH_STATUS_INVALID_ARG;
    }

    *info = external_flash_info;
    return EXTERNAL_FLASH_STATUS_OK;
}

/* C11 static assertions are written using the corresponding C++ keyword. */
#define _Static_assert( condition, message ) static_assert( condition, message )
extern "C"
{
#include "../result_buffer.c" /* Private module under test */  // NOLINT
}
#undef _Static_assert

/**-----------------------------------------------------------------------------
 *  Test Fixture
 *------------------------------------------------------------------------------
 */

class ResultBufferTest : public ::testing::Test
{
protected:
    void SetUp( void ) override
    {
        std::memset( &result_buffer_context, 0, sizeof( result_buffer_context ) );
        std::memset( result_buffer_storage, 0, sizeof( result_buffer_storage ) );
        std::memset( result_buffer_wrap_scratch, 0, sizeof( result_buffer_wrap_scratch ) );

        external_flash_get_info_status = EXTERNAL_FLASH_STATUS_OK;
        external_flash_info            = {};
        external_flash_info.page_size_bytes = TEST_PAGE_SIZE_BYTES;
        external_flash_get_info_calls       = 0U;
    }

    void Initialise( void )
    {
        ASSERT_TRUE( RESULT_BUFFER_Init() );
    }

    static FlashManagerResultHeader_T ReadHeader( uint32_t offset )
    {
        FlashManagerResultHeader_T header = {};
        std::memcpy( &header, &result_buffer_storage[offset], sizeof( header ) );
        return header;
    }
};

/**-----------------------------------------------------------------------------
 *  Initialisation and Reset Tests
 *------------------------------------------------------------------------------
 */

TEST_F( ResultBufferTest, InitUsesExternalFlashGeometryAndResetsState )
{
    EXPECT_TRUE( RESULT_BUFFER_Init() );

    EXPECT_EQ( 1U, external_flash_get_info_calls );
    EXPECT_TRUE( result_buffer_context.is_initialised );
    EXPECT_EQ( TEST_PAGE_SIZE_BYTES, result_buffer_context.page_size_bytes );
    EXPECT_EQ( TEST_CAPACITY_BYTES, result_buffer_context.capacity_bytes );
    EXPECT_EQ( 0U, result_buffer_context.write_offset );
    EXPECT_EQ( 0U, result_buffer_context.occupied_bytes );
    EXPECT_EQ( 1U, result_buffer_context.next_reservation_id );

    for ( ResultBufferPageState_T state : result_buffer_context.page_states )
    {
        EXPECT_EQ( RESULT_BUFFER_PAGE_EMPTY, state );
    }
}

TEST_F( ResultBufferTest, InitRejectsUnavailableOrUnsupportedGeometry )
{
    external_flash_get_info_status = EXTERNAL_FLASH_STATUS_NOT_INITIALISED;
    EXPECT_FALSE( RESULT_BUFFER_Init() );
    EXPECT_FALSE( result_buffer_context.is_initialised );

    external_flash_get_info_status = EXTERNAL_FLASH_STATUS_OK;
    external_flash_info.page_size_bytes = sizeof( FlashManagerResultHeader_T );
    EXPECT_FALSE( RESULT_BUFFER_Init() );

    external_flash_info.page_size_bytes = EXTERNAL_FLASH_MAX_PAGE_SIZE_BYTES + 1U;
    EXPECT_FALSE( RESULT_BUFFER_Init() );
}

TEST_F( ResultBufferTest, ResetInvalidatesLeaseAndPreservesGeometry )
{
    Initialise();
    FlashManagerResultLease_T lease = {};
    ASSERT_TRUE( RESULT_BUFFER_Reserve( 4U, &lease ) );

    RESULT_BUFFER_Reset();

    EXPECT_FALSE( RESULT_BUFFER_Cancel( &lease ) );
    EXPECT_TRUE( result_buffer_context.is_initialised );
    EXPECT_EQ( TEST_PAGE_SIZE_BYTES, result_buffer_context.page_size_bytes );
    EXPECT_EQ( TEST_CAPACITY_BYTES, result_buffer_context.capacity_bytes );
    EXPECT_EQ( 0U, result_buffer_context.write_offset );
    EXPECT_EQ( 0U, result_buffer_context.occupied_bytes );
}

/**-----------------------------------------------------------------------------
 *  Reservation and Cancellation Tests
 *------------------------------------------------------------------------------
 */

TEST_F( ResultBufferTest, ReserveRejectsInvalidRequestsAndClearsLease )
{
    FlashManagerResultLease_T lease = {};
    lease.payload                    = result_buffer_storage;
    lease.reservation_id             = 42U;
    lease.payload_capacity           = 7U;

    EXPECT_FALSE( RESULT_BUFFER_Reserve( 1U, &lease ) );
    EXPECT_EQ( nullptr, lease.payload );
    EXPECT_EQ( 0U, lease.reservation_id );
    EXPECT_EQ( 0U, lease.payload_capacity );

    Initialise();
    EXPECT_FALSE( RESULT_BUFFER_Reserve( 0U, &lease ) );
    EXPECT_FALSE( RESULT_BUFFER_Reserve( static_cast<uint16_t>( TEST_MAX_PAYLOAD_BYTES + 1U ),
                                         &lease ) );
    EXPECT_FALSE( RESULT_BUFFER_Reserve( 1U, nullptr ) );
}

TEST_F( ResultBufferTest, ReserveReturnsDirectPayloadAndAllowsOnlyOneActiveLease )
{
    Initialise();
    FlashManagerResultLease_T first_lease  = {};
    FlashManagerResultLease_T second_lease = {};

    ASSERT_TRUE( RESULT_BUFFER_Reserve( 8U, &first_lease ) );

    EXPECT_EQ( &result_buffer_storage[sizeof( FlashManagerResultHeader_T )],
               first_lease.payload );
    EXPECT_EQ( 1U, first_lease.reservation_id );
    EXPECT_EQ( 8U, first_lease.payload_capacity );
    EXPECT_TRUE( result_buffer_context.active_reservation.is_active );
    EXPECT_FALSE( result_buffer_context.active_reservation.uses_wrap_scratch );

    EXPECT_FALSE( RESULT_BUFFER_Reserve( 4U, &second_lease ) );
    EXPECT_EQ( nullptr, second_lease.payload );
}

TEST_F( ResultBufferTest, CancelValidatesLeaseWithoutConsumingBufferSpace )
{
    Initialise();
    FlashManagerResultLease_T lease = {};
    ASSERT_TRUE( RESULT_BUFFER_Reserve( 8U, &lease ) );

    FlashManagerResultLease_T modified_lease = lease;
    modified_lease.reservation_id++;
    EXPECT_FALSE( RESULT_BUFFER_Cancel( &modified_lease ) );
    EXPECT_TRUE( result_buffer_context.active_reservation.is_active );

    EXPECT_TRUE( RESULT_BUFFER_Cancel( &lease ) );
    EXPECT_FALSE( RESULT_BUFFER_Cancel( &lease ) );
    EXPECT_EQ( 0U, result_buffer_context.write_offset );
    EXPECT_EQ( 0U, result_buffer_context.occupied_bytes );

    FlashManagerResultLease_T next_lease = {};
    ASSERT_TRUE( RESULT_BUFFER_Reserve( 8U, &next_lease ) );
    EXPECT_NE( lease.reservation_id, next_lease.reservation_id );
}

/**-----------------------------------------------------------------------------
 *  Commit Tests
 *------------------------------------------------------------------------------
 */

TEST_F( ResultBufferTest, CommitRejectsInvalidLeaseAndRetainsOverflowedReservation )
{
    Initialise();
    FlashManagerResultLease_T lease = {};

    EXPECT_EQ( RESULT_BUFFER_COMMIT_INVALID_LEASE,
               RESULT_BUFFER_Commit( &lease, 1U, 2U, 3U, 0U ) );

    ASSERT_TRUE( RESULT_BUFFER_Reserve( 4U, &lease ) );
    EXPECT_EQ( RESULT_BUFFER_COMMIT_OVERFLOW,
               RESULT_BUFFER_Commit( &lease, 1U, 2U, 3U, 5U ) );
    EXPECT_TRUE( result_buffer_context.active_reservation.is_active );
    EXPECT_TRUE( RESULT_BUFFER_Cancel( &lease ) );
}

TEST_F( ResultBufferTest, CommitStoresHeaderAndOnlyTheActualPayloadBytes )
{
    Initialise();
    FlashManagerResultLease_T lease = {};
    ASSERT_TRUE( RESULT_BUFFER_Reserve( 8U, &lease ) );

    const std::array<uint8_t, 4U> payload = { 0x11U, 0x22U, 0x33U, 0x44U };
    std::memcpy( lease.payload, payload.data(), payload.size() );

    EXPECT_EQ( RESULT_BUFFER_COMMIT_OK,
               RESULT_BUFFER_Commit( &lease, 0x12345678U, 6U, 9U,
                                     static_cast<uint16_t>( payload.size() ) ) );

    const FlashManagerResultHeader_T header = ReadHeader( 0U );
    EXPECT_EQ( 0x12345678U, header.timestamp );
    EXPECT_EQ( payload.size(), header.payload_length );
    EXPECT_EQ( 6U, header.peripheral_type );
    EXPECT_EQ( 9U, header.channel );
    EXPECT_EQ( 12U, result_buffer_context.write_offset );
    EXPECT_EQ( 12U, result_buffer_context.occupied_bytes );
    EXPECT_EQ( RESULT_BUFFER_PAGE_FILLING, result_buffer_context.page_states[0] );
    EXPECT_FALSE( result_buffer_context.active_reservation.is_active );
    EXPECT_EQ( 0, std::memcmp( &result_buffer_storage[sizeof( header )], payload.data(),
                               payload.size() ) );
}

TEST_F( ResultBufferTest, CommitReturnsPageReadyWhenRecordCompletesPage )
{
    Initialise();
    FlashManagerResultLease_T lease = {};
    ASSERT_TRUE( RESULT_BUFFER_Reserve( TEST_MAX_PAYLOAD_BYTES, &lease ) );
    std::memset( lease.payload, 0x5AU, TEST_MAX_PAYLOAD_BYTES );

    EXPECT_EQ( RESULT_BUFFER_COMMIT_PAGE_READY,
               RESULT_BUFFER_Commit( &lease, 1U, 2U, 3U, TEST_MAX_PAYLOAD_BYTES ) );
    EXPECT_EQ( TEST_PAGE_SIZE_BYTES, result_buffer_context.write_offset );
    EXPECT_EQ( TEST_PAGE_SIZE_BYTES, result_buffer_context.occupied_bytes );
    EXPECT_EQ( RESULT_BUFFER_PAGE_READY, result_buffer_context.page_states[0] );
}

TEST_F( ResultBufferTest, CommitCanCompleteOnePageAndContinueRecordInNextPage )
{
    Initialise();
    FlashManagerResultLease_T first_lease = {};
    ASSERT_TRUE( RESULT_BUFFER_Reserve( 20U, &first_lease ) );
    ASSERT_EQ( RESULT_BUFFER_COMMIT_OK,
               RESULT_BUFFER_Commit( &first_lease, 1U, 2U, 3U, 20U ) );

    FlashManagerResultLease_T crossing_lease = {};
    ASSERT_TRUE( RESULT_BUFFER_Reserve( 8U, &crossing_lease ) );
    EXPECT_EQ( &result_buffer_storage[28U + sizeof( FlashManagerResultHeader_T )],
               crossing_lease.payload );
    std::memset( crossing_lease.payload, 0xA5, 8U );

    EXPECT_EQ( RESULT_BUFFER_COMMIT_PAGE_READY,
               RESULT_BUFFER_Commit( &crossing_lease, 4U, 5U, 6U, 8U ) );
    EXPECT_EQ( 44U, result_buffer_context.write_offset );
    EXPECT_EQ( 44U, result_buffer_context.occupied_bytes );
    EXPECT_EQ( RESULT_BUFFER_PAGE_READY, result_buffer_context.page_states[0] );
    EXPECT_EQ( RESULT_BUFFER_PAGE_FILLING, result_buffer_context.page_states[1] );
}

TEST_F( ResultBufferTest, CommitCopiesScratchRecordAcrossPhysicalRingWrap )
{
    Initialise();
    result_buffer_context.write_offset   = TEST_CAPACITY_BYTES - 4U;
    result_buffer_context.occupied_bytes = TEST_PAGE_SIZE_BYTES - 4U;
    result_buffer_context.page_states[2] = RESULT_BUFFER_PAGE_FILLING;

    FlashManagerResultLease_T lease = {};
    ASSERT_TRUE( RESULT_BUFFER_Reserve( 8U, &lease ) );
    ASSERT_EQ( &result_buffer_wrap_scratch[sizeof( FlashManagerResultHeader_T )], lease.payload );

    const std::array<uint8_t, 8U> payload = { 1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U };
    std::memcpy( lease.payload, payload.data(), payload.size() );

    EXPECT_EQ( RESULT_BUFFER_COMMIT_PAGE_READY,
               RESULT_BUFFER_Commit( &lease, 123U, 7U, 8U,
                                     static_cast<uint16_t>( payload.size() ) ) );

    std::array<uint8_t, 16U> record = {};
    std::memcpy( record.data(), &result_buffer_storage[TEST_CAPACITY_BYTES - 4U], 4U );
    std::memcpy( &record[4], result_buffer_storage, record.size() - 4U );

    FlashManagerResultHeader_T header = {};
    std::memcpy( &header, record.data(), sizeof( header ) );
    EXPECT_EQ( 123U, header.timestamp );
    EXPECT_EQ( payload.size(), header.payload_length );
    EXPECT_EQ( 7U, header.peripheral_type );
    EXPECT_EQ( 8U, header.channel );
    EXPECT_EQ( 0, std::memcmp( &record[sizeof( header )], payload.data(), payload.size() ) );
    EXPECT_EQ( 12U, result_buffer_context.write_offset );
    EXPECT_EQ( 44U, result_buffer_context.occupied_bytes );
    EXPECT_EQ( RESULT_BUFFER_PAGE_READY, result_buffer_context.page_states[2] );
    EXPECT_EQ( RESULT_BUFFER_PAGE_FILLING, result_buffer_context.page_states[0] );
}

TEST_F( ResultBufferTest, ShortScratchCommitDoesNotCopyPastPhysicalEnd )
{
    Initialise();
    result_buffer_context.write_offset   = TEST_CAPACITY_BYTES - 12U;
    result_buffer_context.occupied_bytes = 20U;
    result_buffer_context.page_states[2] = RESULT_BUFFER_PAGE_FILLING;
    result_buffer_storage[0]             = 0xCCU;

    FlashManagerResultLease_T lease = {};
    ASSERT_TRUE( RESULT_BUFFER_Reserve( 8U, &lease ) );
    ASSERT_EQ( &result_buffer_wrap_scratch[sizeof( FlashManagerResultHeader_T )], lease.payload );
    lease.payload[0] = 0xAAU;
    lease.payload[1] = 0xBBU;

    EXPECT_EQ( RESULT_BUFFER_COMMIT_OK, RESULT_BUFFER_Commit( &lease, 1U, 2U, 3U, 2U ) );
    EXPECT_EQ( TEST_CAPACITY_BYTES - 2U, result_buffer_context.write_offset );
    EXPECT_EQ( 30U, result_buffer_context.occupied_bytes );
    EXPECT_EQ( 0xCCU, result_buffer_storage[0] );
}

TEST_F( ResultBufferTest, ReserveFailsWhenCommittedDataOccupiesEntireRing )
{
    Initialise();

    for ( uint32_t page = 0U; page < 3U; page++ )
    {
        FlashManagerResultLease_T lease = {};
        ASSERT_TRUE( RESULT_BUFFER_Reserve( TEST_MAX_PAYLOAD_BYTES, &lease ) );
        ASSERT_EQ( RESULT_BUFFER_COMMIT_PAGE_READY,
                   RESULT_BUFFER_Commit( &lease, page, 1U, 1U, TEST_MAX_PAYLOAD_BYTES ) );
    }

    EXPECT_EQ( TEST_CAPACITY_BYTES, result_buffer_context.occupied_bytes );
    FlashManagerResultLease_T lease = {};
    EXPECT_FALSE( RESULT_BUFFER_Reserve( 1U, &lease ) );
}
