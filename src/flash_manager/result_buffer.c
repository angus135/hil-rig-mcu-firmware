/******************************************************************************
 *  File:       result_buffer.c
 *  Author:     Callum Rafferty
 *  Created:    25-Mar-2026
 *
 *  Description:
 *      Implementation for the Result Buffer module.
 *
 *  Notes:
 *      Result records are packed into a page-backed circular byte buffer. A
 *      single active lease gives the execution path contiguous payload storage
 *      while the flash-manager task owns page-level NAND transfers.
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */
#include "result_buffer.h"
#include "external_flash.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */

/*
 * Three pages allow one page to be filled, one to wait for NAND, and one to be
 * written or retained as latency tolerance.
 */
#define RESULT_BUFFER_PAGE_COUNT ( 3U )

#define RESULT_BUFFER_MAX_CAPACITY_BYTES                                                           \
    ( EXTERNAL_FLASH_MAX_PAGE_SIZE_BYTES * RESULT_BUFFER_PAGE_COUNT )

_Static_assert( sizeof( FlashManagerResultHeader_T ) == 8U, "Unexpected result header layout" );

_Static_assert( EXTERNAL_FLASH_MAX_PAGE_SIZE_BYTES <= UINT16_MAX,
                "Result lease capacity cannot represent the maximum page size" );

/**-----------------------------------------------------------------------------
 *  Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

typedef enum
{
    /* No committed bytes remain in the page; it may be reused by the producer. */
    RESULT_BUFFER_PAGE_EMPTY = 0,

    /* The page contains committed data but has not reached a full NAND page. */
    RESULT_BUFFER_PAGE_FILLING,

    /* Every byte in the page is committed and the page may be written to NAND. */
    RESULT_BUFFER_PAGE_READY_TO_DRAIN,

    /* A NAND operation currently owns the page, so it must remain unchanged. */
    RESULT_BUFFER_PAGE_DRAINING
} ResultBufferPageState_T;

/*
 * Authoritative description of the one record reservation that may be
 * outstanding. The public write lease is validated against this state before
 * commit or cancellation.
 */
typedef struct
{
    bool     is_active;
    bool     uses_wrap_scratch;
    uint16_t payload_capacity_bytes;
    uint16_t reserved_record_length_bytes;
    uint32_t lease_id;
    uint32_t record_offset_bytes;
} ResultBufferRecordReservation_T;

/* Internal state backing a ResultBufferDrainLease_T. */
typedef struct
{
    bool     is_active;
    uint8_t  page_index;
    uint32_t valid_length_bytes;
    uint32_t lease_id;
} ResultBufferDrainReservation_T;

typedef struct
{
    /* Geometry is configured once by Init and preserved across session resets. */
    bool is_initialised;
    bool is_finalised;

    uint32_t page_size_bytes;
    uint32_t capacity_bytes;

    /**
     * End of committed data and start of the next result record.
     */
    uint32_t producer_offset;

    /* Committed bytes that have not been successfully written to NAND. */
    uint32_t pending_nand_bytes;

    /* Oldest page waiting to be drained to NAND. */
    uint8_t drain_page_index;

    uint32_t next_record_lease_id;
    uint32_t next_drain_lease_id;

    /* Page ownership is independent of record boundaries. */
    ResultBufferPageState_T page_states[RESULT_BUFFER_PAGE_COUNT];
    uint32_t                page_valid_bytes[RESULT_BUFFER_PAGE_COUNT];

    ResultBufferRecordReservation_T active_record_reservation;
    ResultBufferDrainReservation_T  active_drain_reservation;
} ResultBufferContext_T;

/**-----------------------------------------------------------------------------
 *  Public (global) and Extern Variables
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Private (static) Variables
 *------------------------------------------------------------------------------
 */

/**
 * Flat storage allows records to cross adjacent page boundaries while
 * remaining contiguous. These bytes are the authoritative RAM copy supplied
 * to the external-flash page-write API.
 */
static uint8_t result_buffer_storage[RESULT_BUFFER_MAX_CAPACITY_BYTES];

/**
 * Used only when a result would cross the physical end of the circular buffer,
 * where the end and beginning are logically adjacent but not contiguous RAM.
 */
static uint8_t result_buffer_wrap_scratch[EXTERNAL_FLASH_MAX_PAGE_SIZE_BYTES];

static ResultBufferContext_T result_buffer_context;

/**-----------------------------------------------------------------------------
 *  Private (static) Function Prototypes
 *------------------------------------------------------------------------------
 */

static uint8_t* RESULT_BUFFER_GetActiveRecordPayload( void );

static bool RESULT_BUFFER_RecordLeaseMatches( const FlashManagerResultWriteLease_T* lease );

static void RESULT_BUFFER_ClearRecordReservation( void );

static bool RESULT_BUFFER_UpdatePagesForCommittedRecord( uint32_t record_offset_bytes,
                                                         uint32_t record_length_bytes );

static uint32_t RESULT_BUFFER_AdvanceProducerOffset( uint32_t offset_bytes, uint32_t length_bytes );

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

static uint8_t* RESULT_BUFFER_GetActiveRecordPayload( void )
{
    /*
     * Scratch preserves the single-pointer driver contract when the logical
     * reservation wraps from the end of the ring back to offset zero.
     */
    if ( result_buffer_context.active_record_reservation.uses_wrap_scratch )
    {
        return &result_buffer_wrap_scratch[sizeof( FlashManagerResultHeader_T )];
    }

    /* Reserve the bytes immediately before the payload for the commit header. */
    uint32_t payload_offset_bytes =
        result_buffer_context.active_record_reservation.record_offset_bytes
        + sizeof( FlashManagerResultHeader_T );

    return &result_buffer_storage[payload_offset_bytes];
}

static bool RESULT_BUFFER_RecordLeaseMatches( const FlashManagerResultWriteLease_T* lease )
{
    /* A write lease is valid only while its record reservation remains active. */
    if ( ( lease == NULL ) || !result_buffer_context.active_record_reservation.is_active )
    {
        return false;
    }

    /* The monotonically changing ID rejects stale leases after storage is reused. */
    if ( lease->lease_id != result_buffer_context.active_record_reservation.lease_id )
    {
        return false;
    }

    /* Capacity and pointer checks detect a modified or unrelated lease. */
    if ( lease->payload_capacity_bytes
         != result_buffer_context.active_record_reservation.payload_capacity_bytes )
    {
        return false;
    }

    return lease->payload == RESULT_BUFFER_GetActiveRecordPayload();
}

static void RESULT_BUFFER_ClearRecordReservation( void )
{
    result_buffer_context.active_record_reservation = ( ResultBufferRecordReservation_T ){ 0 };
}

static uint32_t RESULT_BUFFER_AdvanceProducerOffset( uint32_t offset_bytes, uint32_t length_bytes )
{
    uint32_t advanced_offset_bytes = offset_bytes + length_bytes;

    /* A record is no larger than one page, so at most one ring wrap is possible. */
    if ( advanced_offset_bytes >= result_buffer_context.capacity_bytes )
    {
        advanced_offset_bytes -= result_buffer_context.capacity_bytes;
    }

    return advanced_offset_bytes;
}

static bool RESULT_BUFFER_UpdatePagesForCommittedRecord( uint32_t record_offset_bytes,
                                                         uint32_t record_length_bytes )
{
    uint32_t page_size_bytes   = result_buffer_context.page_size_bytes;
    uint32_t page_index        = record_offset_bytes / page_size_bytes;
    uint32_t page_offset_bytes = record_offset_bytes % page_size_bytes;
    uint32_t page_fill_bytes   = page_offset_bytes + record_length_bytes;

    result_buffer_context.page_states[page_index] = RESULT_BUFFER_PAGE_FILLING;

    /* The current page remains producer-owned until committed data reaches its end. */
    if ( page_fill_bytes < page_size_bytes )
    {
        return false;
    }

    /* A full page is immutable until the flash-manager task writes and releases it. */
    result_buffer_context.page_states[page_index] = RESULT_BUFFER_PAGE_READY_TO_DRAIN;

    /* Any bytes beyond the boundary begin filling the following circular page. */
    if ( page_fill_bytes > page_size_bytes )
    {
        uint32_t next_page = ( page_index + 1U ) % RESULT_BUFFER_PAGE_COUNT;

        result_buffer_context.page_states[next_page] = RESULT_BUFFER_PAGE_FILLING;
    }

    return true;
}

/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */

bool RESULT_BUFFER_Init( void )
{
    ExternalFlashInfo_T info = { 0 };

    /* Obtain geometry through external_flash rather than depending on hw_nand directly. */
    ExternalFlashStatus_T status = EXTERNAL_FLASH_GetInfo( &info );

    if ( status != EXTERNAL_FLASH_STATUS_OK )
    {
        return false;
    }

    /* The active page must hold a header and fit within the static allocation ceiling. */
    if ( ( info.page_size_bytes <= sizeof( FlashManagerResultHeader_T ) )
         || ( info.page_size_bytes > EXTERNAL_FLASH_MAX_PAGE_SIZE_BYTES ) )
    {
        return false;
    }

    uint32_t capacity_bytes = info.page_size_bytes * RESULT_BUFFER_PAGE_COUNT;

    /* Defensively verify that runtime geometry cannot exceed either backing array. */
    if ( ( capacity_bytes > sizeof( result_buffer_storage ) )
         || ( info.page_size_bytes > sizeof( result_buffer_wrap_scratch ) ) )
    {
        return false;
    }

    result_buffer_context.page_size_bytes = info.page_size_bytes;
    result_buffer_context.capacity_bytes  = capacity_bytes;
    result_buffer_context.is_initialised  = true;

    RESULT_BUFFER_Reset();

    return true;
}

void RESULT_BUFFER_Reset( void )
{
    /*
     * Clear only session ownership and cursor state. Geometry remains valid,
     * and stale bytes need not be erased because no lease or page references
     * them until they are overwritten and committed again.
     */
    result_buffer_context.producer_offset      = 0U;
    result_buffer_context.pending_nand_bytes   = 0U;
    result_buffer_context.drain_page_index     = 0U;
    result_buffer_context.next_record_lease_id = 1U;

    result_buffer_context.active_record_reservation = ( ResultBufferRecordReservation_T ){ 0 };

    for ( uint32_t page_index = 0U; page_index < RESULT_BUFFER_PAGE_COUNT; page_index++ )
    {
        result_buffer_context.page_states[page_index] = RESULT_BUFFER_PAGE_EMPTY;
    }
}

/**
 * @brief Reserves contiguous writable storage for one result payload.
 */
bool RESULT_BUFFER_ReserveRecord( uint16_t                        requested_payload_capacity_bytes,
                                  FlashManagerResultWriteLease_T* lease )
{
    if ( lease == NULL )
    {
        return false;
    }

    /*
     * Ensure the caller never retains stale lease information after a failed
     * reservation.
     */
    *lease = ( FlashManagerResultWriteLease_T ){ 0 };

    /*
     * The producer may hold only one lease at a time, which keeps reservation and commit
     * ordering deterministic and removes the need for an allocation queue.
     */
    if ( !result_buffer_context.is_initialised
         || result_buffer_context.active_record_reservation.is_active
         || ( requested_payload_capacity_bytes == 0U ) )
    {
        return false;
    }

    /* Capacity accounting includes the stored header as well as the payload. */
    uint32_t total_record_length_bytes =
        sizeof( FlashManagerResultHeader_T ) + requested_payload_capacity_bytes;

    /*
     * Each record must fit within the wrap scratch buffer. It may still cross
     * one NAND page boundary in the result ring.
     */
    if ( total_record_length_bytes > result_buffer_context.page_size_bytes )
    {
        return false;
    }

    /* Guard the subtraction below against an internal-state underflow. */
    if ( result_buffer_context.pending_nand_bytes > result_buffer_context.capacity_bytes )
    {
        return false;
    }

    uint32_t available_record_bytes =
        result_buffer_context.capacity_bytes - result_buffer_context.pending_nand_bytes;

    /* Reserve for the requested maximum; commit may later consume fewer bytes. */
    if ( total_record_length_bytes > available_record_bytes )
    {
        return false;
    }

    uint32_t contiguous_record_bytes =
        result_buffer_context.capacity_bytes - result_buffer_context.producer_offset;

    /* Ordinary page crossings stay contiguous; only the end of the ring needs scratch. */
    bool uses_wrap_scratch = total_record_length_bytes > contiguous_record_bytes;

    /* The ID prevents a stale lease from operating on later reuse of this address. */
    uint32_t lease_id = result_buffer_context.next_record_lease_id;

    result_buffer_context.next_record_lease_id++;

    /*
     * Zero is reserved for invalid leases.
     */
    if ( result_buffer_context.next_record_lease_id == 0U )
    {
        result_buffer_context.next_record_lease_id = 1U;
    }

    /* Record authoritative internal ownership before publishing the public lease. */
    result_buffer_context.active_record_reservation.is_active         = true;
    result_buffer_context.active_record_reservation.uses_wrap_scratch = uses_wrap_scratch;
    result_buffer_context.active_record_reservation.payload_capacity_bytes =
        requested_payload_capacity_bytes;
    result_buffer_context.active_record_reservation.reserved_record_length_bytes =
        ( uint16_t )total_record_length_bytes;
    result_buffer_context.active_record_reservation.lease_id = lease_id;
    result_buffer_context.active_record_reservation.record_offset_bytes =
        result_buffer_context.producer_offset;

    /* The caller receives temporary write access; ownership remains with this module. */
    lease->payload          = RESULT_BUFFER_GetActiveRecordPayload();
    lease->lease_id         = lease_id;
    lease->payload_capacity_bytes = requested_payload_capacity_bytes;

    return true;
}

/**
 * @brief Cancels the currently active record reservation.
 */
bool RESULT_BUFFER_CancelRecord( const FlashManagerResultWriteLease_T* lease )
{
    /* Validate ID, capacity, and pointer so stale or modified leases cannot cancel. */
    if ( !RESULT_BUFFER_RecordLeaseMatches( lease ) )
    {
        return false;
    }

    /*
     * Reserve does not advance cursors or consume capacity, so clearing the
     * record reservation is a complete rollback. Uncommitted bytes may remain
     * in RAM and will be overwritten by a later reservation.
     */
    RESULT_BUFFER_ClearRecordReservation();

    return true;
}

/**
 * @brief Commits the active lease into the packed result stream.
 */
ResultBufferRecordCommitStatus_T
RESULT_BUFFER_CommitRecord( const FlashManagerResultWriteLease_T* lease, uint32_t timestamp,
                            uint8_t peripheral_type, uint8_t channel,
                            uint16_t actual_payload_length_bytes )
{
    /* Reject stale or modified leases before accessing caller-written bytes. */
    if ( !RESULT_BUFFER_RecordLeaseMatches( lease ) )
    {
        return RESULT_BUFFER_RECORD_COMMIT_INVALID_LEASE;
    }

    /* Keep an overflowed reservation active so the caller can retry or cancel it. */
    if ( actual_payload_length_bytes
         > result_buffer_context.active_record_reservation.payload_capacity_bytes )
    {
        return RESULT_BUFFER_RECORD_COMMIT_OVERFLOW;
    }

    /* Store only the actual payload length; unused reservation capacity is reclaimed. */
    FlashManagerResultHeader_T header = {
        .timestamp       = timestamp,
        .payload_length_bytes  = actual_payload_length_bytes,
        .peripheral_type = peripheral_type,
        .channel         = channel,
    };

    uint32_t record_length_bytes =
        sizeof( FlashManagerResultHeader_T ) + actual_payload_length_bytes;

    if ( result_buffer_context.active_record_reservation.uses_wrap_scratch )
    {
        /*
         * The driver wrote the payload after the reserved header area in scratch.
         * Complete the record by placing the header at the start of scratch.
         */
        memcpy( result_buffer_wrap_scratch, &header, sizeof( header ) );

        uint32_t bytes_until_ring_end =
            result_buffer_context.capacity_bytes
            - result_buffer_context.active_record_reservation.record_offset_bytes;

        /*
         * The actual payload may be smaller than the reserved capacity. Therefore,
         * a reservation that required scratch may commit a record that no longer
         * reaches the physical end of the ring.
         */
        uint32_t first_copy_length_bytes = bytes_until_ring_end;

        if ( first_copy_length_bytes > record_length_bytes )
        {
            first_copy_length_bytes = record_length_bytes;
        }

        uint32_t record_offset_bytes =
            result_buffer_context.active_record_reservation.record_offset_bytes;

        memcpy( &result_buffer_storage[record_offset_bytes], result_buffer_wrap_scratch,
                first_copy_length_bytes );

        uint32_t second_copy_length_bytes = record_length_bytes - first_copy_length_bytes;

        if ( second_copy_length_bytes > 0U )
        {
            memcpy( result_buffer_storage, &result_buffer_wrap_scratch[first_copy_length_bytes],
                    second_copy_length_bytes );
        }
    }
    else
    {
        /*
         * The driver already wrote the payload directly into the ring, so only the
         * header needs to be copied.
         */
        uint32_t record_offset_bytes =
            result_buffer_context.active_record_reservation.record_offset_bytes;

        memcpy( &result_buffer_storage[record_offset_bytes], &header, sizeof( header ) );
    }

    /* Publish page ownership only after the complete record is in its final location. */
    bool page_ready_to_drain = RESULT_BUFFER_UpdatePagesForCommittedRecord(
        result_buffer_context.active_record_reservation.record_offset_bytes, record_length_bytes );

    /* Advance by committed bytes rather than the originally reserved maximum. */
    result_buffer_context.producer_offset = RESULT_BUFFER_AdvanceProducerOffset(
        result_buffer_context.producer_offset, record_length_bytes );
    result_buffer_context.pending_nand_bytes += record_length_bytes;

    /* The caller's lease becomes stale once the committed range is published. */
    RESULT_BUFFER_ClearRecordReservation();

    if ( page_ready_to_drain )
    {
        return RESULT_BUFFER_RECORD_COMMIT_PAGE_READY_TO_DRAIN;
    }
    return RESULT_BUFFER_RECORD_COMMIT_OK;
}
