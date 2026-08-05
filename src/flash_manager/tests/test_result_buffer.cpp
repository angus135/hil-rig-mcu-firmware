/******************************************************************************
 *  File:       test_result_buffer.cpp
 *  Author:     Callum Rafferty
 *  Created:    05-Aug-2026
 *
 *  Description:
 *      Unit tests for result-buffer initialisation and the result logging
 *      record-production, NAND-drain, and finalisation flows.
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
static constexpr uint32_t TEST_CAPACITY_BYTES  = TEST_PAGE_SIZE_BYTES * 3U;
static constexpr uint16_t TEST_MAX_PAYLOAD_BYTES =
    static_cast<uint16_t>( TEST_PAGE_SIZE_BYTES - sizeof( FlashManagerResultHeader_T ) );

/**-----------------------------------------------------------------------------
 *  Test Doubles / Mocks
 *------------------------------------------------------------------------------
 */

static ExternalFlashStatus_T external_flash_get_info_status = EXTERNAL_FLASH_STATUS_OK;
static ExternalFlashInfo_T   external_flash_info            = {};
static uint32_t              external_flash_get_info_calls  = 0U;

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

        external_flash_get_info_status      = EXTERNAL_FLASH_STATUS_OK;
        external_flash_info                 = {};
        external_flash_info.page_size_bytes = TEST_PAGE_SIZE_BYTES;
        external_flash_get_info_calls       = 0U;
    }

    void Initialise( void )
    {
        ASSERT_TRUE( RESULT_BUFFER_Init() );
    }

    static FlashManagerResultHeader_T ReadHeader( uint32_t offset_bytes )
    {
        FlashManagerResultHeader_T header = {};
        std::memcpy( &header, &result_buffer_storage[offset_bytes], sizeof( header ) );
        return header;
    }

    static void CommitFullPage( uint32_t timestamp = 1U )
    {
        FlashManagerResultWriteLease_T lease = {};
        ASSERT_TRUE( RESULT_BUFFER_ReserveRecord( TEST_MAX_PAYLOAD_BYTES, &lease ) );
        std::memset( lease.payload, 0x5AU, TEST_MAX_PAYLOAD_BYTES );
        ASSERT_EQ( RESULT_BUFFER_RECORD_COMMIT_PAGE_READY_TO_DRAIN,
                   RESULT_BUFFER_CommitRecord( &lease, timestamp, 1U, 1U,
                                               TEST_MAX_PAYLOAD_BYTES ) );
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
    EXPECT_EQ( 0U, result_buffer_context.producer_offset );
    EXPECT_EQ( 0U, result_buffer_context.pending_nand_bytes );
    EXPECT_EQ( 0U, result_buffer_context.drain_page_index );
    EXPECT_EQ( 1U, result_buffer_context.next_record_lease_id );
    EXPECT_EQ( 1U, result_buffer_context.next_drain_lease_id );
    EXPECT_FALSE( result_buffer_context.is_finalised );
    EXPECT_FALSE( result_buffer_context.active_record_reservation.is_active );
    EXPECT_FALSE( result_buffer_context.active_drain_reservation.is_active );

    for ( uint32_t page_index = 0U; page_index < RESULT_BUFFER_PAGE_COUNT; page_index++ )
    {
        EXPECT_EQ( RESULT_BUFFER_PAGE_EMPTY, result_buffer_context.page_states[page_index] );
        EXPECT_EQ( 0U, result_buffer_context.page_valid_bytes[page_index] );
    }
}

TEST_F( ResultBufferTest, InitRejectsUnavailableOrUnsupportedGeometry )
{
    external_flash_get_info_status = EXTERNAL_FLASH_STATUS_NOT_INITIALISED;
    EXPECT_FALSE( RESULT_BUFFER_Init() );
    EXPECT_FALSE( result_buffer_context.is_initialised );

    external_flash_get_info_status      = EXTERNAL_FLASH_STATUS_OK;
    external_flash_info.page_size_bytes = sizeof( FlashManagerResultHeader_T );
    EXPECT_FALSE( RESULT_BUFFER_Init() );

    external_flash_info.page_size_bytes = EXTERNAL_FLASH_MAX_PAGE_SIZE_BYTES + 1U;
    EXPECT_FALSE( RESULT_BUFFER_Init() );
}

TEST_F( ResultBufferTest, FailedReinitialisationInvalidatesEarlierGeometry )
{
    Initialise();
    external_flash_get_info_status = EXTERNAL_FLASH_STATUS_ERROR;

    EXPECT_FALSE( RESULT_BUFFER_Init() );
    EXPECT_FALSE( result_buffer_context.is_initialised );
    EXPECT_EQ( 0U, result_buffer_context.page_size_bytes );
    EXPECT_EQ( 0U, result_buffer_context.capacity_bytes );
}

TEST_F( ResultBufferTest, ResetInvalidatesLeaseAndPreservesGeometry )
{
    Initialise();
    CommitFullPage();

    ResultBufferDrainLease_T drain_lease = {};
    ASSERT_TRUE( RESULT_BUFFER_AcquireDrainPage( &drain_lease ) );

    FlashManagerResultWriteLease_T write_lease = {};
    ASSERT_TRUE( RESULT_BUFFER_ReserveRecord( 4U, &write_lease ) );

    RESULT_BUFFER_Reset();

    EXPECT_FALSE( RESULT_BUFFER_CancelRecord( &write_lease ) );
    EXPECT_FALSE( RESULT_BUFFER_CompleteDrain( &drain_lease, true ) );
    EXPECT_TRUE( result_buffer_context.is_initialised );
    EXPECT_FALSE( result_buffer_context.is_finalised );
    EXPECT_EQ( TEST_PAGE_SIZE_BYTES, result_buffer_context.page_size_bytes );
    EXPECT_EQ( TEST_CAPACITY_BYTES, result_buffer_context.capacity_bytes );
    EXPECT_EQ( 0U, result_buffer_context.producer_offset );
    EXPECT_EQ( 0U, result_buffer_context.pending_nand_bytes );
    EXPECT_EQ( 0U, result_buffer_context.drain_page_index );
    EXPECT_EQ( 1U, result_buffer_context.next_record_lease_id );
    EXPECT_EQ( 1U, result_buffer_context.next_drain_lease_id );
    EXPECT_FALSE( result_buffer_context.active_record_reservation.is_active );
    EXPECT_FALSE( result_buffer_context.active_drain_reservation.is_active );
}

/**-----------------------------------------------------------------------------
 *  Reservation and Cancellation Tests
 *------------------------------------------------------------------------------
 */

TEST_F( ResultBufferTest, ReserveRecordRejectsInvalidRequestsAndClearsLease )
{
    FlashManagerResultWriteLease_T lease = {};
    lease.payload                        = result_buffer_storage;
    lease.lease_id                       = 42U;
    lease.payload_capacity_bytes         = 7U;

    EXPECT_FALSE( RESULT_BUFFER_ReserveRecord( 1U, &lease ) );
    EXPECT_EQ( nullptr, lease.payload );
    EXPECT_EQ( 0U, lease.lease_id );
    EXPECT_EQ( 0U, lease.payload_capacity_bytes );

    Initialise();
    EXPECT_FALSE( RESULT_BUFFER_ReserveRecord( 0U, &lease ) );
    EXPECT_FALSE( RESULT_BUFFER_ReserveRecord( static_cast<uint16_t>( TEST_MAX_PAYLOAD_BYTES + 1U ),
                                               &lease ) );
    EXPECT_FALSE( RESULT_BUFFER_ReserveRecord( 1U, nullptr ) );

    result_buffer_context.is_finalised = true;
    EXPECT_FALSE( RESULT_BUFFER_ReserveRecord( 1U, &lease ) );
}

TEST_F( ResultBufferTest, ReserveRecordRejectsReadyOrDrainingDestinationPage )
{
    Initialise();
    FlashManagerResultWriteLease_T lease = {};

    result_buffer_context.page_states[0]      = RESULT_BUFFER_PAGE_READY_TO_DRAIN;
    result_buffer_context.page_valid_bytes[0] = TEST_PAGE_SIZE_BYTES;
    result_buffer_context.pending_nand_bytes  = TEST_PAGE_SIZE_BYTES;
    EXPECT_FALSE( RESULT_BUFFER_ReserveRecord( 1U, &lease ) );

    result_buffer_context.page_states[0] = RESULT_BUFFER_PAGE_DRAINING;
    EXPECT_FALSE( RESULT_BUFFER_ReserveRecord( 1U, &lease ) );
}

TEST_F( ResultBufferTest, ReserveRecordReturnsDirectPayloadAndAllowsOnlyOneActiveLease )
{
    Initialise();
    FlashManagerResultWriteLease_T first_lease  = {};
    FlashManagerResultWriteLease_T second_lease = {};

    ASSERT_TRUE( RESULT_BUFFER_ReserveRecord( 8U, &first_lease ) );

    EXPECT_EQ( &result_buffer_storage[sizeof( FlashManagerResultHeader_T )], first_lease.payload );
    EXPECT_EQ( 1U, first_lease.lease_id );
    EXPECT_EQ( 8U, first_lease.payload_capacity_bytes );
    EXPECT_TRUE( result_buffer_context.active_record_reservation.is_active );
    EXPECT_FALSE( result_buffer_context.active_record_reservation.uses_wrap_scratch );

    EXPECT_FALSE( RESULT_BUFFER_ReserveRecord( 4U, &second_lease ) );
    EXPECT_EQ( nullptr, second_lease.payload );
}

TEST_F( ResultBufferTest, CancelRecordValidatesLeaseWithoutConsumingBufferSpace )
{
    Initialise();
    FlashManagerResultWriteLease_T lease = {};
    ASSERT_TRUE( RESULT_BUFFER_ReserveRecord( 8U, &lease ) );

    FlashManagerResultWriteLease_T modified_lease = lease;
    modified_lease.lease_id++;
    EXPECT_FALSE( RESULT_BUFFER_CancelRecord( &modified_lease ) );
    EXPECT_TRUE( result_buffer_context.active_record_reservation.is_active );

    EXPECT_TRUE( RESULT_BUFFER_CancelRecord( &lease ) );
    EXPECT_FALSE( RESULT_BUFFER_CancelRecord( &lease ) );
    EXPECT_EQ( 0U, result_buffer_context.producer_offset );
    EXPECT_EQ( 0U, result_buffer_context.pending_nand_bytes );

    FlashManagerResultWriteLease_T next_lease = {};
    ASSERT_TRUE( RESULT_BUFFER_ReserveRecord( 8U, &next_lease ) );
    EXPECT_NE( lease.lease_id, next_lease.lease_id );
}

/**-----------------------------------------------------------------------------
 *  Commit Tests
 *------------------------------------------------------------------------------
 */

TEST_F( ResultBufferTest, CommitRecordRejectsInvalidLeaseAndRetainsOverflowedReservation )
{
    Initialise();
    FlashManagerResultWriteLease_T lease = {};

    EXPECT_EQ( RESULT_BUFFER_RECORD_COMMIT_INVALID_LEASE,
               RESULT_BUFFER_CommitRecord( &lease, 1U, 2U, 3U, 0U ) );

    ASSERT_TRUE( RESULT_BUFFER_ReserveRecord( 4U, &lease ) );
    EXPECT_EQ( RESULT_BUFFER_RECORD_COMMIT_OVERFLOW,
               RESULT_BUFFER_CommitRecord( &lease, 1U, 2U, 3U, 5U ) );
    EXPECT_TRUE( result_buffer_context.active_record_reservation.is_active );
    EXPECT_TRUE( RESULT_BUFFER_CancelRecord( &lease ) );
}

TEST_F( ResultBufferTest, CommitRecordStoresHeaderAndOnlyTheActualPayloadBytes )
{
    Initialise();
    FlashManagerResultWriteLease_T lease = {};
    ASSERT_TRUE( RESULT_BUFFER_ReserveRecord( 8U, &lease ) );

    const std::array<uint8_t, 4U> payload = { 0x11U, 0x22U, 0x33U, 0x44U };
    std::memcpy( lease.payload, payload.data(), payload.size() );

    EXPECT_EQ( RESULT_BUFFER_RECORD_COMMIT_OK,
               RESULT_BUFFER_CommitRecord( &lease, 0x12345678U, 6U, 9U,
                                           static_cast<uint16_t>( payload.size() ) ) );

    const FlashManagerResultHeader_T header = ReadHeader( 0U );
    EXPECT_EQ( 0x12345678U, header.timestamp );
    EXPECT_EQ( payload.size(), header.payload_length_bytes );
    EXPECT_EQ( 6U, header.peripheral_type );
    EXPECT_EQ( 9U, header.channel );
    EXPECT_EQ( 12U, result_buffer_context.producer_offset );
    EXPECT_EQ( 12U, result_buffer_context.pending_nand_bytes );
    EXPECT_EQ( RESULT_BUFFER_PAGE_FILLING, result_buffer_context.page_states[0] );
    EXPECT_EQ( 12U, result_buffer_context.page_valid_bytes[0] );
    EXPECT_FALSE( result_buffer_context.active_record_reservation.is_active );
    EXPECT_EQ( 0, std::memcmp( &result_buffer_storage[sizeof( header )], payload.data(),
                               payload.size() ) );
}

TEST_F( ResultBufferTest, CommitRecordReturnsPageReadyWhenRecordCompletesPage )
{
    Initialise();
    FlashManagerResultWriteLease_T lease = {};
    ASSERT_TRUE( RESULT_BUFFER_ReserveRecord( TEST_MAX_PAYLOAD_BYTES, &lease ) );
    std::memset( lease.payload, 0x5AU, TEST_MAX_PAYLOAD_BYTES );

    EXPECT_EQ( RESULT_BUFFER_RECORD_COMMIT_PAGE_READY_TO_DRAIN,
               RESULT_BUFFER_CommitRecord( &lease, 1U, 2U, 3U, TEST_MAX_PAYLOAD_BYTES ) );
    EXPECT_EQ( TEST_PAGE_SIZE_BYTES, result_buffer_context.producer_offset );
    EXPECT_EQ( TEST_PAGE_SIZE_BYTES, result_buffer_context.pending_nand_bytes );
    EXPECT_EQ( RESULT_BUFFER_PAGE_READY_TO_DRAIN, result_buffer_context.page_states[0] );
    EXPECT_EQ( TEST_PAGE_SIZE_BYTES, result_buffer_context.page_valid_bytes[0] );
}

TEST_F( ResultBufferTest, CommitRecordCanCompleteOnePageAndContinueRecordInNextPage )
{
    Initialise();
    FlashManagerResultWriteLease_T first_lease = {};
    ASSERT_TRUE( RESULT_BUFFER_ReserveRecord( 20U, &first_lease ) );
    ASSERT_EQ( RESULT_BUFFER_RECORD_COMMIT_OK,
               RESULT_BUFFER_CommitRecord( &first_lease, 1U, 2U, 3U, 20U ) );

    FlashManagerResultWriteLease_T crossing_lease = {};
    ASSERT_TRUE( RESULT_BUFFER_ReserveRecord( 8U, &crossing_lease ) );
    EXPECT_EQ( &result_buffer_storage[28U + sizeof( FlashManagerResultHeader_T )],
               crossing_lease.payload );
    std::memset( crossing_lease.payload, 0xA5, 8U );

    EXPECT_EQ( RESULT_BUFFER_RECORD_COMMIT_PAGE_READY_TO_DRAIN,
               RESULT_BUFFER_CommitRecord( &crossing_lease, 4U, 5U, 6U, 8U ) );
    EXPECT_EQ( 44U, result_buffer_context.producer_offset );
    EXPECT_EQ( 44U, result_buffer_context.pending_nand_bytes );
    EXPECT_EQ( RESULT_BUFFER_PAGE_READY_TO_DRAIN, result_buffer_context.page_states[0] );
    EXPECT_EQ( RESULT_BUFFER_PAGE_FILLING, result_buffer_context.page_states[1] );
    EXPECT_EQ( TEST_PAGE_SIZE_BYTES, result_buffer_context.page_valid_bytes[0] );
    EXPECT_EQ( 12U, result_buffer_context.page_valid_bytes[1] );
}

TEST_F( ResultBufferTest, CommitRecordCopiesScratchRecordAcrossPhysicalRingWrap )
{
    Initialise();
    result_buffer_context.producer_offset    = TEST_CAPACITY_BYTES - 4U;
    result_buffer_context.pending_nand_bytes = TEST_PAGE_SIZE_BYTES - 4U;
    result_buffer_context.page_states[2]     = RESULT_BUFFER_PAGE_FILLING;
    result_buffer_context.page_valid_bytes[2] = TEST_PAGE_SIZE_BYTES - 4U;

    FlashManagerResultWriteLease_T lease = {};
    ASSERT_TRUE( RESULT_BUFFER_ReserveRecord( 8U, &lease ) );
    ASSERT_EQ( &result_buffer_wrap_scratch[sizeof( FlashManagerResultHeader_T )], lease.payload );

    const std::array<uint8_t, 8U> payload = { 1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U };
    std::memcpy( lease.payload, payload.data(), payload.size() );

    EXPECT_EQ( RESULT_BUFFER_RECORD_COMMIT_PAGE_READY_TO_DRAIN,
               RESULT_BUFFER_CommitRecord( &lease, 123U, 7U, 8U,
                                           static_cast<uint16_t>( payload.size() ) ) );

    std::array<uint8_t, 16U> record = {};
    std::memcpy( record.data(), &result_buffer_storage[TEST_CAPACITY_BYTES - 4U], 4U );
    std::memcpy( &record[4], result_buffer_storage, record.size() - 4U );

    FlashManagerResultHeader_T header = {};
    std::memcpy( &header, record.data(), sizeof( header ) );
    EXPECT_EQ( 123U, header.timestamp );
    EXPECT_EQ( payload.size(), header.payload_length_bytes );
    EXPECT_EQ( 7U, header.peripheral_type );
    EXPECT_EQ( 8U, header.channel );
    EXPECT_EQ( 0, std::memcmp( &record[sizeof( header )], payload.data(), payload.size() ) );
    EXPECT_EQ( 12U, result_buffer_context.producer_offset );
    EXPECT_EQ( 44U, result_buffer_context.pending_nand_bytes );
    EXPECT_EQ( RESULT_BUFFER_PAGE_READY_TO_DRAIN, result_buffer_context.page_states[2] );
    EXPECT_EQ( RESULT_BUFFER_PAGE_FILLING, result_buffer_context.page_states[0] );
    EXPECT_EQ( TEST_PAGE_SIZE_BYTES, result_buffer_context.page_valid_bytes[2] );
    EXPECT_EQ( 12U, result_buffer_context.page_valid_bytes[0] );
}

TEST_F( ResultBufferTest, ShortScratchRecordCommitDoesNotCopyPastPhysicalEnd )
{
    Initialise();
    result_buffer_context.producer_offset    = TEST_CAPACITY_BYTES - 12U;
    result_buffer_context.pending_nand_bytes = 20U;
    result_buffer_context.page_states[2]     = RESULT_BUFFER_PAGE_FILLING;
    result_buffer_context.page_valid_bytes[2] = 20U;
    result_buffer_storage[0]                 = 0xCCU;

    FlashManagerResultWriteLease_T lease = {};
    ASSERT_TRUE( RESULT_BUFFER_ReserveRecord( 8U, &lease ) );
    ASSERT_EQ( &result_buffer_wrap_scratch[sizeof( FlashManagerResultHeader_T )], lease.payload );
    lease.payload[0] = 0xAAU;
    lease.payload[1] = 0xBBU;

    EXPECT_EQ( RESULT_BUFFER_RECORD_COMMIT_OK,
               RESULT_BUFFER_CommitRecord( &lease, 1U, 2U, 3U, 2U ) );
    EXPECT_EQ( TEST_CAPACITY_BYTES - 2U, result_buffer_context.producer_offset );
    EXPECT_EQ( 30U, result_buffer_context.pending_nand_bytes );
    EXPECT_EQ( 30U, result_buffer_context.page_valid_bytes[2] );
    EXPECT_EQ( 0U, result_buffer_context.page_valid_bytes[0] );
    EXPECT_EQ( 0xCCU, result_buffer_storage[0] );
}

TEST_F( ResultBufferTest, ReserveRecordFailsWhenCommittedDataOccupiesEntireRing )
{
    Initialise();

    for ( uint32_t page = 0U; page < 3U; page++ )
    {
        FlashManagerResultWriteLease_T lease = {};
        ASSERT_TRUE( RESULT_BUFFER_ReserveRecord( TEST_MAX_PAYLOAD_BYTES, &lease ) );
        ASSERT_EQ( RESULT_BUFFER_RECORD_COMMIT_PAGE_READY_TO_DRAIN,
                   RESULT_BUFFER_CommitRecord( &lease, page, 1U, 1U, TEST_MAX_PAYLOAD_BYTES ) );
    }

    EXPECT_EQ( TEST_CAPACITY_BYTES, result_buffer_context.pending_nand_bytes );
    for ( uint32_t page_index = 0U; page_index < RESULT_BUFFER_PAGE_COUNT; page_index++ )
    {
        EXPECT_EQ( TEST_PAGE_SIZE_BYTES, result_buffer_context.page_valid_bytes[page_index] );
    }

    FlashManagerResultWriteLease_T lease = {};
    EXPECT_FALSE( RESULT_BUFFER_ReserveRecord( 1U, &lease ) );
}

/**-----------------------------------------------------------------------------
 *  NAND Drain Tests
 *------------------------------------------------------------------------------
 */

TEST_F( ResultBufferTest, AcquireDrainPageRejectsInvalidStateAndClearsLease )
{
    ResultBufferDrainLease_T lease = {};
    lease.page_data                 = result_buffer_storage;
    lease.valid_length_bytes        = 7U;
    lease.lease_id                  = 42U;

    EXPECT_FALSE( RESULT_BUFFER_AcquireDrainPage( &lease ) );
    EXPECT_EQ( nullptr, lease.page_data );
    EXPECT_EQ( 0U, lease.valid_length_bytes );
    EXPECT_EQ( 0U, lease.lease_id );

    Initialise();
    EXPECT_FALSE( RESULT_BUFFER_AcquireDrainPage( &lease ) );
    EXPECT_FALSE( RESULT_BUFFER_AcquireDrainPage( nullptr ) );
}

TEST_F( ResultBufferTest, AcquireDrainPageReturnsOldestReadyPageAndMarksItDraining )
{
    Initialise();
    CommitFullPage();

    ResultBufferDrainLease_T lease = {};
    ASSERT_TRUE( RESULT_BUFFER_AcquireDrainPage( &lease ) );

    EXPECT_EQ( result_buffer_storage, lease.page_data );
    EXPECT_EQ( TEST_PAGE_SIZE_BYTES, lease.valid_length_bytes );
    EXPECT_EQ( 1U, lease.lease_id );
    EXPECT_EQ( RESULT_BUFFER_PAGE_DRAINING, result_buffer_context.page_states[0] );
    EXPECT_TRUE( result_buffer_context.active_drain_reservation.is_active );
    EXPECT_EQ( 0U, result_buffer_context.active_drain_reservation.page_index );
    EXPECT_EQ( TEST_PAGE_SIZE_BYTES,
               result_buffer_context.active_drain_reservation.valid_length_bytes );

    ResultBufferDrainLease_T second_lease = {};
    EXPECT_FALSE( RESULT_BUFFER_AcquireDrainPage( &second_lease ) );
    EXPECT_EQ( nullptr, second_lease.page_data );
}

TEST_F( ResultBufferTest, RecordWriteAndDrainLeasesCanOwnDifferentPagesConcurrently )
{
    Initialise();
    CommitFullPage();

    ResultBufferDrainLease_T drain_lease = {};
    ASSERT_TRUE( RESULT_BUFFER_AcquireDrainPage( &drain_lease ) );

    FlashManagerResultWriteLease_T write_lease = {};
    ASSERT_TRUE( RESULT_BUFFER_ReserveRecord( 4U, &write_lease ) );

    EXPECT_TRUE( result_buffer_context.active_drain_reservation.is_active );
    EXPECT_TRUE( result_buffer_context.active_record_reservation.is_active );
    EXPECT_EQ( RESULT_BUFFER_PAGE_DRAINING, result_buffer_context.page_states[0] );
    EXPECT_EQ( &result_buffer_storage[TEST_PAGE_SIZE_BYTES
                                     + sizeof( FlashManagerResultHeader_T )],
               write_lease.payload );
    EXPECT_NE( drain_lease.page_data, write_lease.payload );
}

TEST_F( ResultBufferTest, CompleteDrainRejectsNullStaleAndModifiedLeases )
{
    Initialise();
    CommitFullPage();

    ResultBufferDrainLease_T lease = {};
    ASSERT_TRUE( RESULT_BUFFER_AcquireDrainPage( &lease ) );

    EXPECT_FALSE( RESULT_BUFFER_CompleteDrain( nullptr, true ) );

    ResultBufferDrainLease_T modified_lease = lease;
    modified_lease.lease_id++;
    EXPECT_FALSE( RESULT_BUFFER_CompleteDrain( &modified_lease, true ) );

    modified_lease = lease;
    modified_lease.valid_length_bytes--;
    EXPECT_FALSE( RESULT_BUFFER_CompleteDrain( &modified_lease, true ) );

    modified_lease = lease;
    modified_lease.page_data++;
    EXPECT_FALSE( RESULT_BUFFER_CompleteDrain( &modified_lease, true ) );

    EXPECT_TRUE( result_buffer_context.active_drain_reservation.is_active );
    EXPECT_EQ( RESULT_BUFFER_PAGE_DRAINING, result_buffer_context.page_states[0] );
    EXPECT_EQ( TEST_PAGE_SIZE_BYTES, result_buffer_context.pending_nand_bytes );
}

TEST_F( ResultBufferTest, FailedDrainPreservesPageForRetryWithNewLeaseId )
{
    Initialise();
    CommitFullPage();

    ResultBufferDrainLease_T first_lease = {};
    ASSERT_TRUE( RESULT_BUFFER_AcquireDrainPage( &first_lease ) );
    ASSERT_TRUE( RESULT_BUFFER_CompleteDrain( &first_lease, false ) );

    EXPECT_FALSE( result_buffer_context.active_drain_reservation.is_active );
    EXPECT_EQ( RESULT_BUFFER_PAGE_READY_TO_DRAIN, result_buffer_context.page_states[0] );
    EXPECT_EQ( TEST_PAGE_SIZE_BYTES, result_buffer_context.page_valid_bytes[0] );
    EXPECT_EQ( TEST_PAGE_SIZE_BYTES, result_buffer_context.pending_nand_bytes );
    EXPECT_EQ( 0U, result_buffer_context.drain_page_index );

    ResultBufferDrainLease_T retry_lease = {};
    ASSERT_TRUE( RESULT_BUFFER_AcquireDrainPage( &retry_lease ) );
    EXPECT_EQ( first_lease.page_data, retry_lease.page_data );
    EXPECT_EQ( first_lease.valid_length_bytes, retry_lease.valid_length_bytes );
    EXPECT_NE( first_lease.lease_id, retry_lease.lease_id );
    EXPECT_FALSE( RESULT_BUFFER_CompleteDrain( &first_lease, true ) );
}

TEST_F( ResultBufferTest, SuccessfulDrainReleasesPageAndAdvancesInOrder )
{
    Initialise();
    CommitFullPage( 1U );
    CommitFullPage( 2U );

    ResultBufferDrainLease_T first_lease = {};
    ASSERT_TRUE( RESULT_BUFFER_AcquireDrainPage( &first_lease ) );
    ASSERT_TRUE( RESULT_BUFFER_CompleteDrain( &first_lease, true ) );

    EXPECT_EQ( RESULT_BUFFER_PAGE_EMPTY, result_buffer_context.page_states[0] );
    EXPECT_EQ( 0U, result_buffer_context.page_valid_bytes[0] );
    EXPECT_EQ( TEST_PAGE_SIZE_BYTES, result_buffer_context.pending_nand_bytes );
    EXPECT_EQ( 1U, result_buffer_context.drain_page_index );
    EXPECT_FALSE( result_buffer_context.active_drain_reservation.is_active );

    ResultBufferDrainLease_T second_lease = {};
    ASSERT_TRUE( RESULT_BUFFER_AcquireDrainPage( &second_lease ) );
    EXPECT_EQ( &result_buffer_storage[TEST_PAGE_SIZE_BYTES], second_lease.page_data );
}

TEST_F( ResultBufferTest, SuccessfulDrainMakesWrappedProducerPageReusable )
{
    Initialise();
    CommitFullPage( 1U );
    CommitFullPage( 2U );
    CommitFullPage( 3U );

    EXPECT_EQ( 0U, result_buffer_context.producer_offset );
    EXPECT_EQ( TEST_CAPACITY_BYTES, result_buffer_context.pending_nand_bytes );

    ResultBufferDrainLease_T drain_lease = {};
    ASSERT_TRUE( RESULT_BUFFER_AcquireDrainPage( &drain_lease ) );
    ASSERT_TRUE( RESULT_BUFFER_CompleteDrain( &drain_lease, true ) );

    FlashManagerResultWriteLease_T write_lease = {};
    ASSERT_TRUE( RESULT_BUFFER_ReserveRecord( TEST_MAX_PAYLOAD_BYTES, &write_lease ) );
    EXPECT_EQ( &result_buffer_storage[sizeof( FlashManagerResultHeader_T )],
               write_lease.payload );
}

/**-----------------------------------------------------------------------------
 *  Finalisation Tests
 *------------------------------------------------------------------------------
 */

TEST_F( ResultBufferTest, FinaliseRejectsUninitialisedBufferAndActiveRecordLease )
{
    EXPECT_FALSE( RESULT_BUFFER_Finalise() );

    Initialise();
    FlashManagerResultWriteLease_T write_lease = {};
    ASSERT_TRUE( RESULT_BUFFER_ReserveRecord( 4U, &write_lease ) );
    EXPECT_FALSE( RESULT_BUFFER_Finalise() );
    EXPECT_FALSE( result_buffer_context.is_finalised );

    ASSERT_TRUE( RESULT_BUFFER_CancelRecord( &write_lease ) );
    EXPECT_TRUE( RESULT_BUFFER_Finalise() );
    EXPECT_TRUE( RESULT_BUFFER_Finalise() );
    EXPECT_TRUE( RESULT_BUFFER_IsDrainComplete() );
}

TEST_F( ResultBufferTest, FinalisePublishesPartialPageAndPreventsFurtherRecords )
{
    Initialise();
    FlashManagerResultWriteLease_T write_lease = {};
    ASSERT_TRUE( RESULT_BUFFER_ReserveRecord( 8U, &write_lease ) );
    ASSERT_EQ( RESULT_BUFFER_RECORD_COMMIT_OK,
               RESULT_BUFFER_CommitRecord( &write_lease, 1U, 2U, 3U, 4U ) );

    ASSERT_TRUE( RESULT_BUFFER_Finalise() );

    EXPECT_TRUE( result_buffer_context.is_finalised );
    EXPECT_EQ( RESULT_BUFFER_PAGE_READY_TO_DRAIN, result_buffer_context.page_states[0] );
    EXPECT_EQ( 12U, result_buffer_context.page_valid_bytes[0] );
    EXPECT_FALSE( RESULT_BUFFER_ReserveRecord( 1U, &write_lease ) );
    EXPECT_FALSE( RESULT_BUFFER_IsDrainComplete() );

    ResultBufferDrainLease_T drain_lease = {};
    ASSERT_TRUE( RESULT_BUFFER_AcquireDrainPage( &drain_lease ) );
    EXPECT_EQ( 12U, drain_lease.valid_length_bytes );
    ASSERT_TRUE( RESULT_BUFFER_CompleteDrain( &drain_lease, true ) );
    EXPECT_TRUE( RESULT_BUFFER_IsDrainComplete() );
}

TEST_F( ResultBufferTest, ExactPageFinalisationDoesNotCreateAnotherDrainPage )
{
    Initialise();
    CommitFullPage();
    ASSERT_TRUE( RESULT_BUFFER_Finalise() );

    EXPECT_EQ( RESULT_BUFFER_PAGE_READY_TO_DRAIN, result_buffer_context.page_states[0] );
    EXPECT_EQ( RESULT_BUFFER_PAGE_EMPTY, result_buffer_context.page_states[1] );
    EXPECT_EQ( 0U, result_buffer_context.page_valid_bytes[1] );

    ResultBufferDrainLease_T drain_lease = {};
    ASSERT_TRUE( RESULT_BUFFER_AcquireDrainPage( &drain_lease ) );
    ASSERT_TRUE( RESULT_BUFFER_CompleteDrain( &drain_lease, true ) );
    EXPECT_FALSE( RESULT_BUFFER_AcquireDrainPage( &drain_lease ) );
    EXPECT_TRUE( RESULT_BUFFER_IsDrainComplete() );
}

TEST_F( ResultBufferTest, FinaliseCanPublishPartialPageWhileEarlierPageIsDraining )
{
    Initialise();
    CommitFullPage();

    ResultBufferDrainLease_T drain_lease = {};
    ASSERT_TRUE( RESULT_BUFFER_AcquireDrainPage( &drain_lease ) );

    FlashManagerResultWriteLease_T write_lease = {};
    ASSERT_TRUE( RESULT_BUFFER_ReserveRecord( 4U, &write_lease ) );
    ASSERT_EQ( RESULT_BUFFER_RECORD_COMMIT_OK,
               RESULT_BUFFER_CommitRecord( &write_lease, 2U, 3U, 4U, 4U ) );

    ASSERT_TRUE( RESULT_BUFFER_Finalise() );
    EXPECT_EQ( RESULT_BUFFER_PAGE_DRAINING, result_buffer_context.page_states[0] );
    EXPECT_EQ( RESULT_BUFFER_PAGE_READY_TO_DRAIN, result_buffer_context.page_states[1] );
    EXPECT_EQ( 12U, result_buffer_context.page_valid_bytes[1] );
    EXPECT_FALSE( RESULT_BUFFER_IsDrainComplete() );
}

TEST_F( ResultBufferTest, DrainCompleteRequiresFinalisationAndNoRemainingPageOwnership )
{
    Initialise();
    EXPECT_FALSE( RESULT_BUFFER_IsDrainComplete() );

    CommitFullPage();
    ASSERT_TRUE( RESULT_BUFFER_Finalise() );
    EXPECT_FALSE( RESULT_BUFFER_IsDrainComplete() );

    ResultBufferDrainLease_T drain_lease = {};
    ASSERT_TRUE( RESULT_BUFFER_AcquireDrainPage( &drain_lease ) );
    EXPECT_FALSE( RESULT_BUFFER_IsDrainComplete() );

    ASSERT_TRUE( RESULT_BUFFER_CompleteDrain( &drain_lease, false ) );
    EXPECT_FALSE( RESULT_BUFFER_IsDrainComplete() );

    ASSERT_TRUE( RESULT_BUFFER_AcquireDrainPage( &drain_lease ) );
    ASSERT_TRUE( RESULT_BUFFER_CompleteDrain( &drain_lease, true ) );
    EXPECT_TRUE( RESULT_BUFFER_IsDrainComplete() );
}
