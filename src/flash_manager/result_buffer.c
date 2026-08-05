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
    RESULT_BUFFER_PAGE_READY,

    /* A NAND operation currently owns the page, so it must remain unchanged. */
    RESULT_BUFFER_PAGE_WRITING
} ResultBufferPageState_T;

/*
 * Authoritative description of the one reservation that may be outstanding.
 * The public lease is validated against this state before commit or cancel.
 */
typedef struct
{
    bool     is_active;
    bool     uses_wrap_scratch;
    uint16_t payload_capacity;
    uint16_t reserved_record_length;
    uint32_t reservation_id;
    uint32_t record_offset;
} ResultBufferReservation_T;

typedef struct
{
    /* Geometry is configured once by Init and preserved across session resets. */
    bool     is_initialised;
    uint32_t page_size_bytes;
    uint32_t capacity_bytes;

    /**
     * End of committed data and start of the next result record.
     */
    uint32_t write_offset;

    /**
     * Committed bytes not yet released after a successful NAND write.
     */
    uint32_t occupied_bytes;

    /**
     * Oldest page awaiting a NAND write.
     */
    uint8_t flash_page_index;

    /**
     * Zero is invalid, so valid reservation IDs begin at one.
     */
    uint32_t next_reservation_id;

    /* Page ownership is independent of record boundaries. */
    ResultBufferPageState_T page_states[RESULT_BUFFER_PAGE_COUNT];

    /* Only the execution producer may own this reservation. */
    ResultBufferReservation_T active_reservation;
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

static uint8_t* RESULT_BUFFER_GetActivePayload( void );

static bool RESULT_BUFFER_LeaseMatches( const FlashManagerResultLease_T* lease );

static void RESULT_BUFFER_ClearActiveReservation( void );

static bool RESULT_BUFFER_UpdatePageStates( uint32_t record_offset, uint32_t record_length );

static uint32_t RESULT_BUFFER_AdvanceOffset( uint32_t offset, uint32_t length );

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

static uint8_t* RESULT_BUFFER_GetActivePayload( void )
{
    /*
     * Scratch preserves the single-pointer driver contract when the logical
     * reservation wraps from the end of the ring back to offset zero.
     */
    if ( result_buffer_context.active_reservation.uses_wrap_scratch )
    {
        return &result_buffer_wrap_scratch[sizeof( FlashManagerResultHeader_T )];
    }

    /* Reserve the bytes immediately before the payload for the commit header. */
    return &result_buffer_storage[result_buffer_context.active_reservation.record_offset
                                  + sizeof( FlashManagerResultHeader_T )];
}

static bool RESULT_BUFFER_LeaseMatches( const FlashManagerResultLease_T* lease )
{
    /* No caller-provided lease is valid unless the buffer owns an active reservation. */
    if ( ( lease == NULL ) || !result_buffer_context.active_reservation.is_active )
    {
        return false;
    }

    /* The monotonically changing ID rejects stale leases after storage is reused. */
    if ( lease->reservation_id != result_buffer_context.active_reservation.reservation_id )
    {
        return false;
    }

    /* Capacity and pointer checks detect a modified or unrelated lease. */
    if ( lease->payload_capacity != result_buffer_context.active_reservation.payload_capacity )
    {
        return false;
    }

    return lease->payload == RESULT_BUFFER_GetActivePayload();
}

static void RESULT_BUFFER_ClearActiveReservation( void )
{
    result_buffer_context.active_reservation = ( ResultBufferReservation_T ){ 0 };
}

static uint32_t RESULT_BUFFER_AdvanceOffset( uint32_t offset, uint32_t length )
{
    uint32_t advanced_offset = offset + length;

    /* A record is no larger than one page, so at most one ring wrap is possible. */
    if ( advanced_offset >= result_buffer_context.capacity_bytes )
    {
        advanced_offset -= result_buffer_context.capacity_bytes;
    }

    return advanced_offset;
}

static bool RESULT_BUFFER_UpdatePageStates( uint32_t record_offset, uint32_t record_length )
{
    uint32_t page_size   = result_buffer_context.page_size_bytes;
    uint32_t page_index  = record_offset / page_size;
    uint32_t page_offset = record_offset % page_size;
    uint32_t page_fill   = page_offset + record_length;

    result_buffer_context.page_states[page_index] = RESULT_BUFFER_PAGE_FILLING;

    /* The current page remains producer-owned until committed data reaches its end. */
    if ( page_fill < page_size )
    {
        return false;
    }

    /* A full page is immutable until the flash-manager task writes and releases it. */
    result_buffer_context.page_states[page_index] = RESULT_BUFFER_PAGE_READY;

    /* Any bytes beyond the boundary begin filling the following circular page. */
    if ( page_fill > page_size )
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
    result_buffer_context.write_offset        = 0U;
    result_buffer_context.occupied_bytes      = 0U;
    result_buffer_context.flash_page_index    = 0U;
    result_buffer_context.next_reservation_id = 1U;

    result_buffer_context.active_reservation = ( ResultBufferReservation_T ){ 0 };

    for ( uint32_t page = 0U; page < RESULT_BUFFER_PAGE_COUNT; page++ )
    {
        result_buffer_context.page_states[page] = RESULT_BUFFER_PAGE_EMPTY;
    }
}

/**
 * @brief Reserves contiguous writable storage for one result payload.
 */
bool RESULT_BUFFER_Reserve( uint16_t requested_payload_capacity, FlashManagerResultLease_T* lease )
{
    if ( lease == NULL )
    {
        return false;
    }

    /*
     * Ensure the caller never retains stale lease information after a failed
     * reservation.
     */
    *lease = ( FlashManagerResultLease_T ){ 0 };

    /*
     * The producer may hold only one lease at a time, which keeps reservation and commit
     * ordering deterministic and removes the need for an allocation queue.
     */
    if ( !result_buffer_context.is_initialised || result_buffer_context.active_reservation.is_active
         || ( requested_payload_capacity == 0U ) )
    {
        return false;
    }

    /* Capacity accounting includes the stored header as well as the payload. */
    uint32_t total_record_length =
        sizeof( FlashManagerResultHeader_T ) + requested_payload_capacity;

    /*
     * Each record must fit within the wrap scratch buffer. It may still cross
     * one NAND page boundary in the result ring.
     */
    if ( total_record_length > result_buffer_context.page_size_bytes )
    {
        return false;
    }

    /* Guard the subtraction below against an internal-state underflow. */
    if ( result_buffer_context.occupied_bytes > result_buffer_context.capacity_bytes )
    {
        return false;
    }

    uint32_t available_bytes =
        result_buffer_context.capacity_bytes - result_buffer_context.occupied_bytes;

    /* Reserve for the requested maximum; commit may later consume fewer bytes. */
    if ( total_record_length > available_bytes )
    {
        return false;
    }

    uint32_t contiguous_bytes =
        result_buffer_context.capacity_bytes - result_buffer_context.write_offset;

    /* Ordinary page crossings stay contiguous; only the end of the ring needs scratch. */
    bool uses_wrap_scratch = total_record_length > contiguous_bytes;

    /* The ID prevents a stale lease from operating on later reuse of this address. */
    uint32_t reservation_id = result_buffer_context.next_reservation_id;

    result_buffer_context.next_reservation_id++;

    /*
     * Zero is reserved for invalid leases.
     */
    if ( result_buffer_context.next_reservation_id == 0U )
    {
        result_buffer_context.next_reservation_id = 1U;
    }

    /* Record authoritative internal ownership before publishing the public lease. */
    result_buffer_context.active_reservation.is_active         = true;
    result_buffer_context.active_reservation.uses_wrap_scratch = uses_wrap_scratch;
    result_buffer_context.active_reservation.payload_capacity  = requested_payload_capacity;
    result_buffer_context.active_reservation.reserved_record_length =
        ( uint16_t )total_record_length;
    result_buffer_context.active_reservation.reservation_id = reservation_id;
    result_buffer_context.active_reservation.record_offset  = result_buffer_context.write_offset;

    /* The caller receives temporary write access; ownership remains with this module. */
    lease->payload          = RESULT_BUFFER_GetActivePayload();
    lease->reservation_id   = reservation_id;
    lease->payload_capacity = requested_payload_capacity;

    return true;
}

/**
 * @brief Cancels the currently active result reservation.
 */
bool RESULT_BUFFER_Cancel( const FlashManagerResultLease_T* lease )
{
    /* Validate ID, capacity, and pointer so stale or modified leases cannot cancel. */
    if ( !RESULT_BUFFER_LeaseMatches( lease ) )
    {
        return false;
    }

    /*
     * Reserve does not advance cursors or consume capacity, so clearing the
     * active reservation is a complete rollback. Uncommitted bytes may remain
     * in RAM and will be overwritten by a later reservation.
     */
    RESULT_BUFFER_ClearActiveReservation();

    return true;
}

/**
 * @brief Commits the active lease into the packed result stream.
 */
ResultBufferCommitStatus_T RESULT_BUFFER_Commit( const FlashManagerResultLease_T* lease,
                                                 uint32_t timestamp, uint8_t peripheral_type,
                                                 uint8_t channel, uint16_t actual_payload_length )
{
    /* Reject stale or modified leases before accessing caller-written bytes. */
    if ( !RESULT_BUFFER_LeaseMatches( lease ) )
    {
        return RESULT_BUFFER_COMMIT_INVALID_LEASE;
    }

    /* Keep an overflowed reservation active so the caller can retry or cancel it. */
    if ( actual_payload_length > result_buffer_context.active_reservation.payload_capacity )
    {
        return RESULT_BUFFER_COMMIT_OVERFLOW;
    }

    /* Store only the actual payload length; unused reservation capacity is reclaimed. */
    FlashManagerResultHeader_T header = {
        .timestamp       = timestamp,
        .payload_length  = actual_payload_length,
        .peripheral_type = peripheral_type,
        .channel         = channel,
    };

    uint32_t record_length = sizeof( FlashManagerResultHeader_T ) + actual_payload_length;

    if ( result_buffer_context.active_reservation.uses_wrap_scratch )
    {
        /*
         * The driver wrote the payload after the reserved header area in scratch.
         * Complete the record by placing the header at the start of scratch.
         */
        memcpy( result_buffer_wrap_scratch, &header, sizeof( header ) );

        uint32_t bytes_until_end = result_buffer_context.capacity_bytes
                                   - result_buffer_context.active_reservation.record_offset;

        /*
         * The actual payload may be smaller than the reserved capacity. Therefore,
         * a reservation that required scratch may commit a record that no longer
         * reaches the physical end of the ring.
         */
        uint32_t first_copy_length = bytes_until_end;

        if ( first_copy_length > record_length )
        {
            first_copy_length = record_length;
        }

        memcpy( &result_buffer_storage[result_buffer_context.active_reservation.record_offset],
                result_buffer_wrap_scratch, first_copy_length );

        uint32_t second_copy_length = record_length - first_copy_length;

        if ( second_copy_length > 0U )
        {
            memcpy( result_buffer_storage, &result_buffer_wrap_scratch[first_copy_length],
                    second_copy_length );
        }
    }
    else
    {
        /*
         * The driver already wrote the payload directly into the ring, so only the
         * header needs to be copied.
         */
        memcpy( &result_buffer_storage[result_buffer_context.active_reservation.record_offset],
                &header, sizeof( header ) );
    }

    /* Publish page ownership only after the complete record is in its final location. */
    bool page_became_ready = RESULT_BUFFER_UpdatePageStates(
        result_buffer_context.active_reservation.record_offset, record_length );

    /* Advance by committed bytes rather than the originally reserved maximum. */
    result_buffer_context.write_offset =
        RESULT_BUFFER_AdvanceOffset( result_buffer_context.write_offset, record_length );
    result_buffer_context.occupied_bytes += record_length;

    /* The caller's lease becomes stale once the committed range is published. */
    RESULT_BUFFER_ClearActiveReservation();

    if ( page_became_ready )
    {
        return RESULT_BUFFER_COMMIT_PAGE_READY;
    }
    return RESULT_BUFFER_COMMIT_OK;
}
