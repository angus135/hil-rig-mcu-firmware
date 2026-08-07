/******************************************************************************
 *  File:       result_buffer.c
 *  Author:     Callum Rafferty
 *  Created:    25-Mar-2026
 *
 *  Description:
 *      Implementation of the Flash Manager result logging buffer.
 *
 *  Notes:
 *      Result records are packed into a page-backed circular byte buffer. A
 *      record write lease gives the execution path contiguous payload storage
 *      while an independent drain lease protects a page-level NAND transfer.
 *      State-transition calls must be serialised by the Flash Manager. This
 *      module does not contain RTOS synchronisation or NAND-access policy.
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */
#include "result_buffer.h"
#include "external_flash.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */

/*
 * Three slots allow result production, ready-data buffering, and a NAND page
 * write to overlap without sharing ownership. The extra slot provides
 * tolerance for NAND and scheduler latency.
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
    /** No committed bytes remain; the producer may reuse this page. */
    RESULT_BUFFER_PAGE_EMPTY = 0,

    /** Committed data is present, but the page is not yet ready for NAND. */
    RESULT_BUFFER_PAGE_FILLING,

    /** The page is published and may be acquired for a NAND write. */
    RESULT_BUFFER_PAGE_READY_TO_DRAIN,

    /** A drain lease owns the page; its bytes must remain immutable. */
    RESULT_BUFFER_PAGE_DRAINING
} ResultBufferPageState_T;

/**
 * @brief Authoritative state for the one active result-record reservation.
 *
 * The public write lease is validated against this state before commit or
 * cancellation.
 */
typedef struct
{
    bool     is_active;
    bool     uses_wrap_scratch;
    uint16_t payload_capacity_bytes;
    uint32_t lease_id;
    uint32_t record_offset_bytes;
} ResultBufferRecordReservation_T;

/** @brief Authoritative state for the one active result-page drain lease. */
typedef struct
{
    bool     is_active;
    uint8_t  page_index;
    uint32_t valid_length_bytes;
    uint32_t lease_id;
} ResultBufferDrainReservation_T;

typedef struct
{
    /** Geometry is configured by Init and preserved across session resets. */
    bool is_initialised;

    /** Prevents further record reservations after the producer is stopped. */
    bool is_finalised;

    /** Runtime NAND page size used by all page and cursor calculations. */
    uint32_t page_size_bytes;

    /** Usable bytes in the circular RAM storage. */
    uint32_t capacity_bytes;

    /** Circular byte offset where the next committed result record begins. */
    uint32_t producer_offset;

    /** Committed bytes not yet successfully persisted to NAND. */
    uint32_t pending_nand_bytes;

    /** Oldest occupied page in sequential NAND-write order. */
    uint8_t drain_page_index;

    /** Identifier assigned to the next result-record write lease. */
    uint32_t next_record_lease_id;

    /** Identifier assigned to the next result-page drain lease. */
    uint32_t next_drain_lease_id;

    /** Page ownership is tracked independently of result-record boundaries. */
    ResultBufferPageState_T page_states[RESULT_BUFFER_PAGE_COUNT];

    /** Number of committed logical result bytes held in each page. */
    uint32_t page_valid_bytes[RESULT_BUFFER_PAGE_COUNT];

    /** The one driver-to-RAM reservation that may remain outstanding. */
    ResultBufferRecordReservation_T active_record_reservation;

    /** The one RAM-to-NAND reservation that may remain outstanding. */
    ResultBufferDrainReservation_T active_drain_reservation;
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
 * @brief Page-partitioned circular storage for the packed result byte stream.
 *
 * Flat storage allows records to cross adjacent page boundaries while
 * remaining contiguous. These bytes are the authoritative RAM copy supplied
 * to the external-flash page-write API.
 */
static uint8_t result_buffer_storage[RESULT_BUFFER_MAX_CAPACITY_BYTES];

/**
 * @brief Contiguous staging for a record that crosses the physical ring end.
 *
 * Used only when a result would cross the physical end of the circular buffer,
 * where the end and beginning are logically adjacent but not contiguous RAM.
 */
static uint8_t result_buffer_wrap_scratch[EXTERNAL_FLASH_MAX_PAGE_SIZE_BYTES];

static ResultBufferContext_T result_buffer_context;

/**-----------------------------------------------------------------------------
 *  Private (static) Function Prototypes
 *------------------------------------------------------------------------------
 */

/** Returns the writable payload pointer owned by the active record reservation. */
static uint8_t* RESULT_BUFFER_GetActiveRecordPayload( void );

/** Validates a public write lease against the active record reservation. */
static bool RESULT_BUFFER_RecordLeaseMatches( const FlashManagerResultWriteLease_T* lease );

/** Invalidates the active record reservation without changing committed data. */
static void RESULT_BUFFER_ClearRecordReservation( void );

/** Reports whether the producer owns every page touched by a record range. */
static bool RESULT_BUFFER_RecordRangeIsWritable( uint32_t record_offset_bytes,
                                                 uint32_t record_length_bytes );

/** Publishes committed record bytes into their affected page states. */
static bool RESULT_BUFFER_UpdatePagesForCommittedRecord( uint32_t record_offset_bytes,
                                                         uint32_t record_length_bytes );

/** Advances a circular producer offset by a validated record length. */
static uint32_t RESULT_BUFFER_AdvanceProducerOffset( uint32_t offset_bytes, uint32_t length_bytes );

/** Returns the immutable page pointer owned by the active drain reservation. */
static const uint8_t* RESULT_BUFFER_GetActiveDrainPageData( void );

/** Validates a drain lease against its reservation and current page state. */
static bool RESULT_BUFFER_DrainLeaseMatches( const ResultBufferDrainLease_T* lease );

/** Invalidates the active drain reservation without changing page ownership. */
static void RESULT_BUFFER_ClearDrainReservation( void );

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

static bool RESULT_BUFFER_RecordRangeIsWritable( uint32_t record_offset_bytes,
                                                 uint32_t record_length_bytes )
{
    uint32_t page_size_bytes   = result_buffer_context.page_size_bytes;
    uint32_t page_index        = record_offset_bytes / page_size_bytes;
    uint32_t page_offset_bytes = record_offset_bytes % page_size_bytes;

    if ( page_index >= RESULT_BUFFER_PAGE_COUNT )
    {
        return false;
    }

    /* A page-aligned producer must begin in a completely released page. */
    if ( page_offset_bytes == 0U )
    {
        if ( ( result_buffer_context.page_states[page_index] != RESULT_BUFFER_PAGE_EMPTY )
             || ( result_buffer_context.page_valid_bytes[page_index] != 0U ) )
        {
            return false;
        }
    }
    /* A partial producer page must end exactly where the next record begins. */
    else if ( ( result_buffer_context.page_states[page_index] != RESULT_BUFFER_PAGE_FILLING )
              || ( result_buffer_context.page_valid_bytes[page_index] != page_offset_bytes ) )
    {
        return false;
    }

    uint32_t available_page_bytes = page_size_bytes - page_offset_bytes;

    if ( record_length_bytes > available_page_bytes )
    {
        uint32_t next_page_index = ( page_index + 1U ) % RESULT_BUFFER_PAGE_COUNT;

        /* A crossing record begins the following page at offset zero. */
        if ( ( result_buffer_context.page_states[next_page_index] != RESULT_BUFFER_PAGE_EMPTY )
             || ( result_buffer_context.page_valid_bytes[next_page_index] != 0U ) )
        {
            return false;
        }
    }

    return true;
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
    uint32_t page_size_bytes = result_buffer_context.page_size_bytes;

    uint32_t page_index = record_offset_bytes / page_size_bytes;

    uint32_t page_offset_bytes = record_offset_bytes % page_size_bytes;

    uint32_t available_page_bytes = page_size_bytes - page_offset_bytes;

    uint32_t bytes_in_current_page = record_length_bytes;

    if ( bytes_in_current_page > available_page_bytes )
    {
        bytes_in_current_page = available_page_bytes;
    }

    uint32_t current_page_valid_bytes = page_offset_bytes + bytes_in_current_page;

    result_buffer_context.page_valid_bytes[page_index] = current_page_valid_bytes;

    bool page_ready_to_drain = current_page_valid_bytes == page_size_bytes;

    if ( page_ready_to_drain )
    {
        result_buffer_context.page_states[page_index] = RESULT_BUFFER_PAGE_READY_TO_DRAIN;
    }
    else
    {
        result_buffer_context.page_states[page_index] = RESULT_BUFFER_PAGE_FILLING;
    }

    uint32_t remaining_record_bytes = record_length_bytes - bytes_in_current_page;

    if ( remaining_record_bytes > 0U )
    {
        uint32_t next_page_index = ( page_index + 1U ) % RESULT_BUFFER_PAGE_COUNT;

        /*
         * A record crossing a page boundary begins the following page at
         * offset zero.
         */
        result_buffer_context.page_valid_bytes[next_page_index] = remaining_record_bytes;

        result_buffer_context.page_states[next_page_index] = RESULT_BUFFER_PAGE_FILLING;
    }

    return page_ready_to_drain;
}

static const uint8_t* RESULT_BUFFER_GetActiveDrainPageData( void )
{
    uint32_t page_offset_bytes =
        ( uint32_t )result_buffer_context.active_drain_reservation.page_index
        * result_buffer_context.page_size_bytes;

    return &result_buffer_storage[page_offset_bytes];
}

static bool RESULT_BUFFER_DrainLeaseMatches( const ResultBufferDrainLease_T* lease )
{
    if ( ( lease == NULL ) || !result_buffer_context.active_drain_reservation.is_active )
    {
        return false;
    }

    uint8_t page_index = result_buffer_context.active_drain_reservation.page_index;

    /*
     * Protect the array and pointer calculations against corrupted internal
     * state.
     */
    if ( page_index >= RESULT_BUFFER_PAGE_COUNT )
    {
        return false;
    }

    if ( result_buffer_context.page_states[page_index] != RESULT_BUFFER_PAGE_DRAINING )
    {
        return false;
    }

    if ( result_buffer_context.page_valid_bytes[page_index]
         != result_buffer_context.active_drain_reservation.valid_length_bytes )
    {
        return false;
    }

    if ( lease->lease_id != result_buffer_context.active_drain_reservation.lease_id )
    {
        return false;
    }

    if ( lease->valid_length_bytes
         != result_buffer_context.active_drain_reservation.valid_length_bytes )
    {
        return false;
    }

    if ( ( lease->valid_length_bytes == 0U )
         || ( lease->valid_length_bytes > result_buffer_context.page_size_bytes ) )
    {
        return false;
    }

    return lease->page_data == RESULT_BUFFER_GetActiveDrainPageData();
}

static void RESULT_BUFFER_ClearDrainReservation( void )
{
    result_buffer_context.active_drain_reservation = ( ResultBufferDrainReservation_T ){ 0 };
}

/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */

/**
 * @brief Initialises result-buffer geometry and resets session ownership.
 */
bool RESULT_BUFFER_Init( void )
{
    /* A failed reinitialisation must not leave an earlier geometry usable. */
    result_buffer_context = ( ResultBufferContext_T ){ 0 };

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

/**
 * @brief Resets session ownership while retaining initialised NAND geometry.
 */
void RESULT_BUFFER_Reset( void )
{
    /*
     * Reset session ownership and cursor state while preserving the NAND
     * geometry configured by RESULT_BUFFER_Init().
     */
    result_buffer_context.is_finalised         = false;
    result_buffer_context.producer_offset      = 0U;
    result_buffer_context.pending_nand_bytes   = 0U;
    result_buffer_context.drain_page_index     = 0U;
    result_buffer_context.next_record_lease_id = 1U;
    result_buffer_context.next_drain_lease_id  = 1U;

    result_buffer_context.active_record_reservation = ( ResultBufferRecordReservation_T ){ 0 };

    result_buffer_context.active_drain_reservation = ( ResultBufferDrainReservation_T ){ 0 };

    for ( uint32_t page_index = 0U; page_index < RESULT_BUFFER_PAGE_COUNT; page_index++ )
    {
        result_buffer_context.page_states[page_index] = RESULT_BUFFER_PAGE_EMPTY;

        result_buffer_context.page_valid_bytes[page_index] = 0U;
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
    if ( !result_buffer_context.is_initialised || result_buffer_context.is_finalised
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

    /* Explicitly prevent producer access to a page owned by the drain path. */
    if ( !RESULT_BUFFER_RecordRangeIsWritable( result_buffer_context.producer_offset,
                                               total_record_length_bytes ) )
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
    result_buffer_context.active_record_reservation.lease_id = lease_id;
    result_buffer_context.active_record_reservation.record_offset_bytes =
        result_buffer_context.producer_offset;

    /* The caller receives temporary write access; ownership remains with this module. */
    lease->payload                = RESULT_BUFFER_GetActiveRecordPayload();
    lease->lease_id               = lease_id;
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
        .timestamp            = timestamp,
        .payload_length_bytes = actual_payload_length_bytes,
        .peripheral_type      = peripheral_type,
        .channel              = channel,
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

/**
 * @brief Acquires the oldest result page that is ready to drain to NAND.
 */
bool RESULT_BUFFER_AcquireDrainPage( ResultBufferDrainLease_T* lease )
{
    if ( lease == NULL )
    {
        return false;
    }

    /*
     * Ensure a failed acquisition never leaves stale ownership information
     * in the caller's lease.
     */
    *lease = ( ResultBufferDrainLease_T ){ 0 };

    if ( !result_buffer_context.is_initialised
         || result_buffer_context.active_drain_reservation.is_active )
    {
        return false;
    }

    uint8_t page_index = result_buffer_context.drain_page_index;

    if ( page_index >= RESULT_BUFFER_PAGE_COUNT )
    {
        return false;
    }

    /*
     * Only the oldest page may be acquired, preserving the logical result
     * ordering written to NAND.
     */
    if ( result_buffer_context.page_states[page_index] != RESULT_BUFFER_PAGE_READY_TO_DRAIN )
    {
        return false;
    }

    uint32_t valid_length_bytes = result_buffer_context.page_valid_bytes[page_index];

    if ( ( valid_length_bytes == 0U )
         || ( valid_length_bytes > result_buffer_context.page_size_bytes ) )
    {
        return false;
    }

    /*
     * The page's valid bytes must still be included in the amount waiting to
     * be committed to NAND.
     */
    if ( result_buffer_context.pending_nand_bytes < valid_length_bytes )
    {
        return false;
    }

    uint32_t lease_id = result_buffer_context.next_drain_lease_id;

    result_buffer_context.next_drain_lease_id++;

    /* Zero is reserved for invalid or cleared leases. */
    if ( result_buffer_context.next_drain_lease_id == 0U )
    {
        result_buffer_context.next_drain_lease_id = 1U;
    }

    /*
     * Record authoritative internal ownership before publishing the caller's
     * lease.
     */
    result_buffer_context.active_drain_reservation.is_active = true;

    result_buffer_context.active_drain_reservation.page_index = page_index;

    result_buffer_context.active_drain_reservation.valid_length_bytes = valid_length_bytes;

    result_buffer_context.active_drain_reservation.lease_id = lease_id;

    /*
     * The page must remain immutable while external flash uses it for the NAND
     * write operation.
     */
    result_buffer_context.page_states[page_index] = RESULT_BUFFER_PAGE_DRAINING;

    lease->page_data = RESULT_BUFFER_GetActiveDrainPageData();

    lease->valid_length_bytes = valid_length_bytes;
    lease->lease_id           = lease_id;

    return true;
}

/**
 * @brief Completes the NAND write associated with an active drain lease.
 */
bool RESULT_BUFFER_CompleteDrain( const ResultBufferDrainLease_T* lease, bool nand_write_succeeded )
{
    if ( !RESULT_BUFFER_DrainLeaseMatches( lease ) )
    {
        return false;
    }

    uint8_t page_index = result_buffer_context.active_drain_reservation.page_index;

    uint32_t valid_length_bytes = result_buffer_context.active_drain_reservation.valid_length_bytes;

    if ( nand_write_succeeded )
    {
        /*
         * Do not release ownership if the committed-byte accounting is
         * inconsistent. The page remains DRAINING for fault handling or reset.
         */
        if ( result_buffer_context.pending_nand_bytes < valid_length_bytes )
        {
            return false;
        }

        /*
         * EXTERNAL_FLASH_WriteResultPage() completed successfully, so these
         * bytes no longer need to remain in RAM.
         */
        result_buffer_context.pending_nand_bytes -= valid_length_bytes;

        result_buffer_context.page_valid_bytes[page_index] = 0U;

        result_buffer_context.page_states[page_index] = RESULT_BUFFER_PAGE_EMPTY;

        result_buffer_context.drain_page_index =
            ( uint8_t )( ( page_index + 1U ) % RESULT_BUFFER_PAGE_COUNT );
    }
    else
    {
        /*
         * Preserve both the data and its capacity accounting so the
         * Flash Manager task can retry the same page.
         */
        result_buffer_context.page_states[page_index] = RESULT_BUFFER_PAGE_READY_TO_DRAIN;
    }

    /*
     * Both success and reported failure complete this ownership interval.
     * The supplied lease is now stale.
     */
    RESULT_BUFFER_ClearDrainReservation();

    return true;
}

/**
 * @brief Stops result production and publishes the final partial NAND page.
 */
bool RESULT_BUFFER_Finalise( void )
{
    if ( !result_buffer_context.is_initialised )
    {
        return false;
    }

    /*
     * A driver may still be writing through an active record lease. Publishing
     * that page now could expose incomplete data to the drain path.
     */
    if ( result_buffer_context.active_record_reservation.is_active )
    {
        return false;
    }

    /* Finalisation is intentionally idempotent. */
    if ( result_buffer_context.is_finalised )
    {
        return true;
    }

    if ( ( result_buffer_context.page_size_bytes == 0U )
         || ( result_buffer_context.producer_offset >= result_buffer_context.capacity_bytes ) )
    {
        return false;
    }

    uint32_t partial_page_valid_bytes =
        result_buffer_context.producer_offset % result_buffer_context.page_size_bytes;

    if ( partial_page_valid_bytes > 0U )
    {
        uint32_t partial_page_index =
            result_buffer_context.producer_offset / result_buffer_context.page_size_bytes;

        if ( partial_page_index >= RESULT_BUFFER_PAGE_COUNT )
        {
            return false;
        }

        /*
         * The partial page must still be producer-owned and its bookkeeping
         * must end exactly at the producer cursor.
         */
        if ( result_buffer_context.page_states[partial_page_index] != RESULT_BUFFER_PAGE_FILLING )
        {
            return false;
        }

        if ( result_buffer_context.page_valid_bytes[partial_page_index]
             != partial_page_valid_bytes )
        {
            return false;
        }

        if ( result_buffer_context.pending_nand_bytes < partial_page_valid_bytes )
        {
            return false;
        }

        /*
         * This is now the final page of the result session. No more records may
         * be appended after it.
         */
        result_buffer_context.page_states[partial_page_index] = RESULT_BUFFER_PAGE_READY_TO_DRAIN;
    }

    /*
     * Exact-page-aligned and empty streams do not need an additional partial
     * page, but result production must still stop.
     */
    result_buffer_context.is_finalised = true;

    return true;
}

bool RESULT_BUFFER_IsDrainComplete( void )
{
    if ( !result_buffer_context.is_initialised || !result_buffer_context.is_finalised )
    {
        return false;
    }

    if ( ( result_buffer_context.pending_nand_bytes != 0U )
         || result_buffer_context.active_record_reservation.is_active
         || result_buffer_context.active_drain_reservation.is_active )
    {
        return false;
    }

    /* Pending-byte accounting and page ownership must agree before reuse. */
    for ( uint32_t page_index = 0U; page_index < RESULT_BUFFER_PAGE_COUNT; page_index++ )
    {
        if ( ( result_buffer_context.page_states[page_index] != RESULT_BUFFER_PAGE_EMPTY )
             || ( result_buffer_context.page_valid_bytes[page_index] != 0U ) )
        {
            return false;
        }
    }

    return true;
}
