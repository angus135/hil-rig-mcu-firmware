/******************************************************************************
 *  File:       instruction_buffer.c
 *  Author:     Callum Rafferty
 *  Created:    25-Mar-2026
 *
 *  Description:
 *      Implementation of the Flash Manager instruction retrieval buffer.
 *
 *  Notes:
 *      The buffer presents one canonical instruction stream using three
 *      circular page slots:
 *
 *      - The Flash Manager task reserves EMPTY slots, reads sequential NAND
 *        pages directly into them, and publishes them as READY.
 *      - The Execution Manager peeks at and consumes complete instruction
 *        records from READY slots without accessing NAND.
 *      - Consuming all bytes in a slot returns it to EMPTY so the Flash Manager
 *        can refill it with the next NAND page.
 *
 *      A record consists of FlashManagerInstructionHeader_T followed by its
 *      payload. Records may cross a NAND page boundary. Most records can be
 *      exposed directly from the three circular page slots. A fourth region
 *      mirrors slot zero immediately after slot two, making records that cross
 *      the physical ring end contiguous without copying in the execution ISR.
 *
 *      The calling layer must serialise shared-state transitions between the
 *      Flash Manager task and execution ISR. This module deliberately owns no
 *      RTOS primitives and makes no external-flash policy decisions.
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */
#include "instruction_buffer.h"
#include "external_flash.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */

/*
 * Three slots allow instruction consumption, ready-data buffering, and a NAND
 * page fill to overlap without sharing ownership of a slot. The extra slot also
 * provides tolerance for NAND and scheduler latency.
 */
#define INSTRUCTION_BUFFER_PAGE_COUNT ( 3U )

#define INSTRUCTION_BUFFER_MAX_CAPACITY_BYTES                                                      \
    ( EXTERNAL_FLASH_MAX_PAGE_SIZE_BYTES * INSTRUCTION_BUFFER_PAGE_COUNT )

/* Temporary policy: one instruction record may occupy at most one NAND page. */
#define INSTRUCTION_BUFFER_MAX_RECORD_BYTES ( EXTERNAL_FLASH_MAX_PAGE_SIZE_BYTES )

#define INSTRUCTION_BUFFER_STORAGE_BYTES                                                           \
    ( INSTRUCTION_BUFFER_MAX_CAPACITY_BYTES + INSTRUCTION_BUFFER_MAX_RECORD_BYTES )

/* The serialized NAND layout depends on this fixed header width. */
#if defined( __cplusplus )
static_assert( sizeof( FlashManagerInstructionHeader_T ) == 8U,
               "Unexpected instruction header layout" );
#else
_Static_assert( sizeof( FlashManagerInstructionHeader_T ) == 8U,
                "Unexpected instruction header layout" );
#endif

/**-----------------------------------------------------------------------------
 *  Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

typedef enum
{
    /** Available as the destination of a NAND page read. */
    INSTRUCTION_BUFFER_PAGE_EMPTY = 0,

    /** A NAND read currently owns and is filling this page. */
    INSTRUCTION_BUFFER_PAGE_FILLING_FROM_NAND,

    /** Valid instruction bytes are immutable and available to the consumer. */
    INSTRUCTION_BUFFER_PAGE_READY
} InstructionBufferPageState_T;

/**
 * @brief Authoritative state for the one active NAND page-fill reservation.
 *
 * A caller-provided InstructionBufferPageFillLease_T must match every field in
 * this structure before the reservation can be completed.
 */
typedef struct
{
    bool     is_active;
    uint8_t  page_index;
    uint32_t instruction_offset_bytes;
    uint32_t read_length_bytes;
    uint32_t lease_id;
} InstructionBufferPageFillReservation_T;

/** @brief Authoritative ownership of the instruction currently being viewed. */
typedef struct
{
    /** Whether a view has been published and not yet consumed. */
    bool is_active;

    /** Logical stream offset of the viewed record header. */
    uint32_t record_stream_offset_bytes;

    /** Authoritative combined serialized header and payload length. */
    uint32_t record_length_bytes;

    /** Complete logical header-and-payload view exposed to the consumer. */
    FlashManagerInstructionView_T view;
} InstructionBufferViewReservation_T;

/**
 * @brief Geometry, page ownership, and sequential retrieval cursors.
 */
typedef struct
{
    /* Module lifecycle and external-flash geometry. */

    /** Geometry is configured by Init and retained across retrieval sessions. */
    bool is_initialised;

    /** Whether PrepareRead successfully configured an instruction image. */
    bool is_read_prepared;

    /** Runtime NAND page size used for slot addressing and read lengths. */
    uint32_t page_size_bytes;

    /** Maximum logical instruction image length accepted by PrepareRead. */
    uint32_t instruction_partition_capacity_bytes;

    /** Total logical length of the active canonical instruction image. */
    uint32_t instruction_length_bytes;

    /* NAND producer position and lease identity. */

    /** Logical offset of the next instruction page that must be read from NAND. */
    uint32_t next_nand_read_offset_bytes;

    /** Slot that will receive the next sequential NAND page. */
    uint8_t next_fill_page_index;

    /** Identifier to assign to the next page-fill reservation. */
    uint32_t next_page_fill_lease_id;

    /* Execution consumer position and view identity. */

    /** Logical stream offset of the next unconsumed instruction record. */
    uint32_t consumer_stream_offset_bytes;

    /** Slot containing the next unconsumed instruction byte. */
    uint8_t consumer_page_index;

    /** Offset of the next unconsumed instruction byte within its slot. */
    uint32_t consumer_page_offset_bytes;

    /** Identifier assigned to the next published instruction view. */
    uint32_t next_instruction_view_id;

    /* Per-page metadata and active ownership reservations. */

    /** Current ownership state of each page slot. */
    InstructionBufferPageState_T page_states[INSTRUCTION_BUFFER_PAGE_COUNT];

    /** Valid logical instruction bytes held in each page slot. */
    uint32_t page_valid_bytes[INSTRUCTION_BUFFER_PAGE_COUNT];

    /** Logical instruction-stream offset represented by byte zero of each slot. */
    uint32_t page_stream_offsets_bytes[INSTRUCTION_BUFFER_PAGE_COUNT];

    /** The one NAND page-fill reservation that may currently be outstanding. */
    InstructionBufferPageFillReservation_T active_page_fill_reservation;

    /** The one instruction view that may currently be outstanding. */
    InstructionBufferViewReservation_T active_instruction_view;

} InstructionBufferContext_T;

/**-----------------------------------------------------------------------------
 *  Public (global) and Extern Variables
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Private (static) Variables
 *------------------------------------------------------------------------------
 */

/**
 * @brief Circular instruction page storage followed by a slot-zero mirror.
 *
 * The first three runtime pages are filled sequentially from NAND and reused
 * only after their bytes are consumed. The following maximum-record region
 * mirrors the valid prefix of slot zero so a slot-two-to-slot-zero record has
 * one physically contiguous address range.
 */
static uint8_t instruction_buffer_storage[INSTRUCTION_BUFFER_STORAGE_BYTES];

/** Geometry, ownership, and cursor state for the instruction stream. */
static InstructionBufferContext_T instruction_buffer_context;

/**-----------------------------------------------------------------------------
 *  Private (static) Function Prototypes
 *------------------------------------------------------------------------------
 */

/* Page indexing and NAND-fill ownership. */

/** Returns the circular successor of a valid page-slot index. */
static uint8_t INSTRUCTION_BUFFER_NextPageIndex( uint8_t page_index );

/** Returns the RAM destination owned by the active page-fill reservation. */
static uint8_t* INSTRUCTION_BUFFER_GetActivePageFillData( void );

/** Validates an external fill lease against the authoritative reservation. */
static bool
INSTRUCTION_BUFFER_PageFillLeaseMatches( const InstructionBufferPageFillLease_T* lease );

/** Invalidates the active page-fill reservation without changing page data. */
static void INSTRUCTION_BUFFER_ClearPageFillReservation( void );

/* Execution-facing view ownership. */

/** Invalidates the current instruction view without advancing the stream. */
static void INSTRUCTION_BUFFER_ClearInstructionView( void );

/** Returns the next non-zero identifier for a published instruction view. */
static uint32_t INSTRUCTION_BUFFER_AllocateInstructionViewId( void );

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

/* Page indexing and NAND-fill ownership. */

static uint8_t INSTRUCTION_BUFFER_NextPageIndex( uint8_t page_index )
{
    return ( uint8_t )( ( page_index + 1U ) % INSTRUCTION_BUFFER_PAGE_COUNT );
}

static uint8_t* INSTRUCTION_BUFFER_GetActivePageFillData( void )
{
    /* Page slots are contiguous even though their ownership cycles logically. */
    uint32_t page_offset_bytes =
        ( uint32_t )instruction_buffer_context.active_page_fill_reservation.page_index
        * instruction_buffer_context.page_size_bytes;

    return &instruction_buffer_storage[page_offset_bytes];
}

static bool INSTRUCTION_BUFFER_PageFillLeaseMatches( const InstructionBufferPageFillLease_T* lease )
{
    /* A fill lease is meaningful only within an active prepared read session. */
    if ( ( lease == NULL ) || !instruction_buffer_context.is_initialised
         || !instruction_buffer_context.is_read_prepared
         || !instruction_buffer_context.active_page_fill_reservation.is_active )
    {
        return false;
    }

    uint8_t page_index = instruction_buffer_context.active_page_fill_reservation.page_index;

    if ( page_index >= INSTRUCTION_BUFFER_PAGE_COUNT )
    {
        return false;
    }

    if ( instruction_buffer_context.page_states[page_index]
         != INSTRUCTION_BUFFER_PAGE_FILLING_FROM_NAND )
    {
        return false;
    }

    if ( instruction_buffer_context.page_valid_bytes[page_index] != 0U )
    {
        return false;
    }

    if ( instruction_buffer_context.active_page_fill_reservation.instruction_offset_bytes
         != instruction_buffer_context.next_nand_read_offset_bytes )
    {
        return false;
    }

    /* Validate every published field so modified and stale leases are rejected. */
    return ( lease->page_data == INSTRUCTION_BUFFER_GetActivePageFillData() )
           && ( lease->instruction_offset_bytes
                == instruction_buffer_context.active_page_fill_reservation
                       .instruction_offset_bytes )
           && ( lease->read_length_bytes
                == instruction_buffer_context.active_page_fill_reservation.read_length_bytes )
           && ( lease->lease_id
                == instruction_buffer_context.active_page_fill_reservation.lease_id );
}

static void INSTRUCTION_BUFFER_ClearPageFillReservation( void )
{
    instruction_buffer_context.active_page_fill_reservation =
        ( InstructionBufferPageFillReservation_T ){ 0 };
}

/* Execution-facing view ownership. */

static void INSTRUCTION_BUFFER_ClearInstructionView( void )
{
    instruction_buffer_context.active_instruction_view =
        ( InstructionBufferViewReservation_T ){ 0 };
}

static uint32_t INSTRUCTION_BUFFER_AllocateInstructionViewId( void )
{
    uint32_t view_id = instruction_buffer_context.next_instruction_view_id++;

    if ( instruction_buffer_context.next_instruction_view_id == 0U )
    {
        instruction_buffer_context.next_instruction_view_id = 1U;
    }

    return view_id;
}

/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */

/* Lifecycle and retrieval-session configuration. */

/**
 * @brief Initialises runtime geometry and invalidates any previous read state.
 */
bool INSTRUCTION_BUFFER_Init( void )
{
    /* Failed reinitialisation must not leave earlier geometry or leases usable. */
    instruction_buffer_context = ( InstructionBufferContext_T ){ 0 };

    ExternalFlashInfo_T external_flash_info = { 0 };

    if ( EXTERNAL_FLASH_GetInfo( &external_flash_info ) != EXTERNAL_FLASH_STATUS_OK )
    {
        return false;
    }

    /* Runtime geometry must fit the compile-time backing-storage ceiling. */
    if ( ( external_flash_info.page_size_bytes == 0U )
         || ( external_flash_info.page_size_bytes > EXTERNAL_FLASH_MAX_PAGE_SIZE_BYTES )
         || ( external_flash_info.instruction_capacity_bytes == 0U ) )
    {
        return false;
    }

    uint32_t buffer_capacity_bytes =
        external_flash_info.page_size_bytes * INSTRUCTION_BUFFER_PAGE_COUNT;

    if ( buffer_capacity_bytes > INSTRUCTION_BUFFER_MAX_CAPACITY_BYTES )
    {
        return false;
    }

    instruction_buffer_context.page_size_bytes = external_flash_info.page_size_bytes;

    instruction_buffer_context.instruction_partition_capacity_bytes =
        external_flash_info.instruction_capacity_bytes;

    instruction_buffer_context.next_page_fill_lease_id  = 1U;
    instruction_buffer_context.next_instruction_view_id = 1U;
    instruction_buffer_context.is_initialised           = true;

    return true;
}

/**
 * @brief Resets page ownership and cursors for one instruction image.
 */
bool INSTRUCTION_BUFFER_PrepareRead( uint32_t instruction_length_bytes )
{
    if ( !instruction_buffer_context.is_initialised
         || ( instruction_length_bytes
              > instruction_buffer_context.instruction_partition_capacity_bytes ) )
    {
        return false;
    }

    /* Keep the session unavailable until every ownership field is reset. */
    instruction_buffer_context.is_read_prepared             = false;
    instruction_buffer_context.instruction_length_bytes     = instruction_length_bytes;
    instruction_buffer_context.next_nand_read_offset_bytes  = 0U;
    instruction_buffer_context.next_fill_page_index         = 0U;
    instruction_buffer_context.consumer_stream_offset_bytes = 0U;
    instruction_buffer_context.consumer_page_index          = 0U;
    instruction_buffer_context.consumer_page_offset_bytes   = 0U;

    INSTRUCTION_BUFFER_ClearPageFillReservation();
    INSTRUCTION_BUFFER_ClearInstructionView();

    for ( uint32_t page_index = 0U; page_index < INSTRUCTION_BUFFER_PAGE_COUNT; page_index++ )
    {
        instruction_buffer_context.page_states[page_index] = INSTRUCTION_BUFFER_PAGE_EMPTY;

        instruction_buffer_context.page_valid_bytes[page_index] = 0U;

        instruction_buffer_context.page_stream_offsets_bytes[page_index] = 0U;
    }

    /*
     * Preserve the monotonically changing lease sequence across session resets
     * so a lease from the previous session cannot match newly reused storage.
     */
    if ( instruction_buffer_context.next_page_fill_lease_id == 0U )
    {
        instruction_buffer_context.next_page_fill_lease_id = 1U;
    }

    if ( instruction_buffer_context.next_instruction_view_id == 0U )
    {
        instruction_buffer_context.next_instruction_view_id = 1U;
    }

    instruction_buffer_context.is_read_prepared = true;

    return true;
}

/**
 * @brief Invalidates the active instruction stream while retaining geometry.
 */
void INSTRUCTION_BUFFER_EndRead( void )
{
    if ( !instruction_buffer_context.is_initialised )
    {
        return;
    }

    instruction_buffer_context.is_read_prepared             = false;
    instruction_buffer_context.instruction_length_bytes     = 0U;
    instruction_buffer_context.next_nand_read_offset_bytes  = 0U;
    instruction_buffer_context.next_fill_page_index         = 0U;
    instruction_buffer_context.consumer_stream_offset_bytes = 0U;
    instruction_buffer_context.consumer_page_index          = 0U;
    instruction_buffer_context.consumer_page_offset_bytes   = 0U;

    INSTRUCTION_BUFFER_ClearPageFillReservation();
    INSTRUCTION_BUFFER_ClearInstructionView();

    for ( uint32_t page_index = 0U; page_index < INSTRUCTION_BUFFER_PAGE_COUNT; page_index++ )
    {
        instruction_buffer_context.page_states[page_index]      = INSTRUCTION_BUFFER_PAGE_EMPTY;
        instruction_buffer_context.page_valid_bytes[page_index] = 0U;
        instruction_buffer_context.page_stream_offsets_bytes[page_index] = 0U;
    }
}

/* NAND-to-RAM page producer interface. */

/**
 * @brief Reserves the next sequential empty page slot for a NAND read.
 */
bool INSTRUCTION_BUFFER_AcquireFillPage( InstructionBufferPageFillLease_T* lease )
{
    if ( lease == NULL )
    {
        return false;
    }

    *lease = ( InstructionBufferPageFillLease_T ){ 0 };

    if ( !instruction_buffer_context.is_initialised || !instruction_buffer_context.is_read_prepared
         || instruction_buffer_context.active_page_fill_reservation.is_active
         || ( instruction_buffer_context.next_nand_read_offset_bytes
              >= instruction_buffer_context.instruction_length_bytes ) )
    {
        return false;
    }

    uint8_t page_index = instruction_buffer_context.next_fill_page_index;

    if ( page_index >= INSTRUCTION_BUFFER_PAGE_COUNT )
    {
        return false;
    }

    if ( ( instruction_buffer_context.page_states[page_index] != INSTRUCTION_BUFFER_PAGE_EMPTY )
         || ( instruction_buffer_context.page_valid_bytes[page_index] != 0U ) )
    {
        return false;
    }

    uint32_t remaining_instruction_bytes = instruction_buffer_context.instruction_length_bytes
                                           - instruction_buffer_context.next_nand_read_offset_bytes;

    uint32_t read_length_bytes = remaining_instruction_bytes;

    if ( read_length_bytes > instruction_buffer_context.page_size_bytes )
    {
        read_length_bytes = instruction_buffer_context.page_size_bytes;
    }

    uint32_t lease_id = instruction_buffer_context.next_page_fill_lease_id++;

    if ( instruction_buffer_context.next_page_fill_lease_id == 0U )
    {
        instruction_buffer_context.next_page_fill_lease_id = 1U;
    }

    /* Record authoritative ownership before publishing the external lease. */
    instruction_buffer_context.active_page_fill_reservation.is_active  = true;
    instruction_buffer_context.active_page_fill_reservation.page_index = page_index;
    instruction_buffer_context.active_page_fill_reservation.instruction_offset_bytes =
        instruction_buffer_context.next_nand_read_offset_bytes;
    instruction_buffer_context.active_page_fill_reservation.read_length_bytes = read_length_bytes;
    instruction_buffer_context.active_page_fill_reservation.lease_id          = lease_id;

    instruction_buffer_context.page_states[page_index] = INSTRUCTION_BUFFER_PAGE_FILLING_FROM_NAND;

    lease->page_data                = INSTRUCTION_BUFFER_GetActivePageFillData();
    lease->instruction_offset_bytes = instruction_buffer_context.next_nand_read_offset_bytes;
    lease->read_length_bytes        = read_length_bytes;
    lease->lease_id                 = lease_id;

    return true;
}

/**
 * @brief Publishes or releases the page owned by an active NAND-read lease.
 */
bool INSTRUCTION_BUFFER_CompleteFillPage( const InstructionBufferPageFillLease_T* lease,
                                          bool nand_read_succeeded )
{
    if ( !INSTRUCTION_BUFFER_PageFillLeaseMatches( lease ) )
    {
        return false;
    }

    uint8_t page_index = instruction_buffer_context.active_page_fill_reservation.page_index;

    uint32_t instruction_offset_bytes =
        instruction_buffer_context.active_page_fill_reservation.instruction_offset_bytes;

    uint32_t read_length_bytes =
        instruction_buffer_context.active_page_fill_reservation.read_length_bytes;

    if ( nand_read_succeeded )
    {
        /* Guard the subtraction and ensure the read fits the configured image. */
        if ( ( instruction_offset_bytes > instruction_buffer_context.instruction_length_bytes )
             || ( read_length_bytes > ( instruction_buffer_context.instruction_length_bytes
                                        - instruction_offset_bytes ) ) )
        {
            return false;
        }

        /*
         * Mirror slot zero immediately after the circular storage. This makes
         * a record crossing from slot two into slot zero physically contiguous
         * for the execution ISR. Keep the slot unpublished until the copy is
         * complete so the ISR cannot observe a partially updated mirror.
         */
        if ( page_index == 0U )
        {
            uint32_t mirror_offset_bytes =
                instruction_buffer_context.page_size_bytes * INSTRUCTION_BUFFER_PAGE_COUNT;

            memcpy( &instruction_buffer_storage[mirror_offset_bytes], instruction_buffer_storage,
                    read_length_bytes );
        }

        instruction_buffer_context.page_valid_bytes[page_index] = read_length_bytes;

        instruction_buffer_context.page_stream_offsets_bytes[page_index] = instruction_offset_bytes;

        instruction_buffer_context.page_states[page_index] = INSTRUCTION_BUFFER_PAGE_READY;

        instruction_buffer_context.next_nand_read_offset_bytes += read_length_bytes;

        instruction_buffer_context.next_fill_page_index =
            INSTRUCTION_BUFFER_NextPageIndex( page_index );
    }
    else
    {
        /* Preserve the NAND offset so the same logical page can be retried. */
        instruction_buffer_context.page_states[page_index] = INSTRUCTION_BUFFER_PAGE_EMPTY;

        instruction_buffer_context.page_valid_bytes[page_index] = 0U;

        instruction_buffer_context.page_stream_offsets_bytes[page_index] = 0U;
    }

    INSTRUCTION_BUFFER_ClearPageFillReservation();

    return true;
}

/* Execution-facing instruction consumer interface. */

/**
 * @brief Returns the current instruction view without advancing the stream.
 *
 * The fixed header is copied into an aligned public view and the payload is
 * exposed directly from storage. The slot-zero mirror keeps a record crossing
 * the physical ring end contiguous, so this path performs no payload copy and
 * no page search.
 */
InstructionBufferPeekStatus_T
INSTRUCTION_BUFFER_PeekInstruction( const FlashManagerInstructionView_T** instruction )
{
    if ( instruction == NULL )
    {
        return INSTRUCTION_BUFFER_PEEK_INVALID_ARGUMENT;
    }

    *instruction = NULL;

    if ( !instruction_buffer_context.is_initialised
         || !instruction_buffer_context.is_read_prepared )
    {
        return INSTRUCTION_BUFFER_PEEK_NOT_BUFFERED;
    }

    /*
     * The reservation also owns the backing bytes until consume. Returning its
     * address avoids copying the complete public view into ISR-owned storage.
     */
    if ( instruction_buffer_context.active_instruction_view.is_active )
    {
        *instruction = &instruction_buffer_context.active_instruction_view.view;

        return INSTRUCTION_BUFFER_PEEK_AVAILABLE;
    }

    uint32_t record_stream_offset_bytes = instruction_buffer_context.consumer_stream_offset_bytes;

    if ( record_stream_offset_bytes == instruction_buffer_context.instruction_length_bytes )
    {
        return INSTRUCTION_BUFFER_PEEK_END_OF_STREAM;
    }

    /*
     * Upload preprocessing guarantees a packed [header][payload] stream. The
     * producer offset is therefore sufficient to determine whether the next
     * complete record has reached RAM; no page search is required here.
     */
    if ( ( record_stream_offset_bytes > instruction_buffer_context.instruction_length_bytes )
         || ( instruction_buffer_context.next_nand_read_offset_bytes
              < record_stream_offset_bytes ) )
    {
        return INSTRUCTION_BUFFER_PEEK_CORRUPT;
    }

    uint32_t buffered_unread_bytes =
        instruction_buffer_context.next_nand_read_offset_bytes - record_stream_offset_bytes;

    if ( buffered_unread_bytes < sizeof( FlashManagerInstructionHeader_T ) )
    {
        return INSTRUCTION_BUFFER_PEEK_NOT_BUFFERED;
    }

    uint8_t current_page_index = instruction_buffer_context.consumer_page_index;

    if ( current_page_index >= INSTRUCTION_BUFFER_PAGE_COUNT )
    {
        return INSTRUCTION_BUFFER_PEEK_CORRUPT;
    }

    uint32_t record_storage_offset_bytes =
        ( ( uint32_t )current_page_index * instruction_buffer_context.page_size_bytes )
        + instruction_buffer_context.consumer_page_offset_bytes;

    uint32_t runtime_storage_size_bytes =
        instruction_buffer_context.page_size_bytes * INSTRUCTION_BUFFER_PAGE_COUNT;

    if ( record_storage_offset_bytes >= runtime_storage_size_bytes )
    {
        return INSTRUCTION_BUFFER_PEEK_CORRUPT;
    }

    FlashManagerInstructionHeader_T header = { 0 };

    /*
     * Copy the fixed header into an aligned object for safe field access. The
     * slot-zero mirror makes this a single bounded copy even at the ring end.
     */
    memcpy( &header, &instruction_buffer_storage[record_storage_offset_bytes], sizeof( header ) );

    uint32_t record_length_bytes = sizeof( header ) + ( uint32_t )header.payload_length_bytes;

    uint32_t remaining_image_bytes =
        instruction_buffer_context.instruction_length_bytes - record_stream_offset_bytes;

    /*
     * These two bounds are the runtime fault barrier for a trusted canonical
     * stream. Semantic instruction validation belongs to upload preprocessing
     * and the Execution Manager.
     */
    if ( ( record_length_bytes > instruction_buffer_context.page_size_bytes )
         || ( record_length_bytes > remaining_image_bytes ) )
    {
        return INSTRUCTION_BUFFER_PEEK_CORRUPT;
    }

    if ( buffered_unread_bytes < record_length_bytes )
    {
        return INSTRUCTION_BUFFER_PEEK_NOT_BUFFERED;
    }

    instruction_buffer_context.active_instruction_view = ( InstructionBufferViewReservation_T ){
        .is_active                  = true,
        .record_stream_offset_bytes = record_stream_offset_bytes,
        .record_length_bytes        = record_length_bytes,
        .view                       = { .header = header,
                                        .payload =
                                            &instruction_buffer_storage[record_storage_offset_bytes + sizeof( header )],
                                        .view_id = INSTRUCTION_BUFFER_AllocateInstructionViewId() } };

    *instruction = &instruction_buffer_context.active_instruction_view.view;

    return INSTRUCTION_BUFFER_PEEK_AVAILABLE;
}

/**
 * @brief Advances past the active instruction and releases exhausted pages.
 *
 * The common path advances within the current page. Page-state transitions are
 * required only when the record reaches or crosses a page boundary.
 */
InstructionBufferConsumeStatus_T
INSTRUCTION_BUFFER_ConsumeInstruction( const FlashManagerInstructionView_T* instruction )
{
    InstructionBufferViewReservation_T* active_view =
        &instruction_buffer_context.active_instruction_view;

    if ( ( instruction == NULL ) || !instruction_buffer_context.is_initialised
         || !instruction_buffer_context.is_read_prepared || !active_view->is_active
         || ( instruction != &active_view->view ) || ( instruction->view_id == 0U )
         || ( instruction->view_id != active_view->view.view_id ) )
    {
        return INSTRUCTION_BUFFER_CONSUME_INVALID_VIEW;
    }

    uint8_t current_page_index = instruction_buffer_context.consumer_page_index;

    if ( ( current_page_index >= INSTRUCTION_BUFFER_PAGE_COUNT )
         || ( active_view->record_stream_offset_bytes
              != instruction_buffer_context.consumer_stream_offset_bytes )
         || ( active_view->record_length_bytes < sizeof( FlashManagerInstructionHeader_T ) )
         || ( instruction_buffer_context.page_states[current_page_index]
              != INSTRUCTION_BUFFER_PAGE_READY ) )
    {
        return INSTRUCTION_BUFFER_CONSUME_INTERNAL_ERROR;
    }

    uint32_t current_page_valid_bytes =
        instruction_buffer_context.page_valid_bytes[current_page_index];

    uint32_t current_page_offset_bytes = instruction_buffer_context.consumer_page_offset_bytes;

    if ( ( current_page_offset_bytes >= current_page_valid_bytes )
         || ( instruction_buffer_context.consumer_stream_offset_bytes
              > instruction_buffer_context.instruction_length_bytes )
         || ( instruction_buffer_context.page_stream_offsets_bytes[current_page_index]
              > instruction_buffer_context.consumer_stream_offset_bytes )
         || ( instruction_buffer_context.consumer_stream_offset_bytes
                  - instruction_buffer_context.page_stream_offsets_bytes[current_page_index]
              != current_page_offset_bytes )
         || ( active_view->record_length_bytes
              > ( instruction_buffer_context.instruction_length_bytes
                  - instruction_buffer_context.consumer_stream_offset_bytes ) ) )
    {
        return INSTRUCTION_BUFFER_CONSUME_INTERNAL_ERROR;
    }

    uint32_t bytes_remaining_in_page = current_page_valid_bytes - current_page_offset_bytes;

    /* Most instructions remain within their current page. */
    if ( active_view->record_length_bytes < bytes_remaining_in_page )
    {
        instruction_buffer_context.consumer_stream_offset_bytes += active_view->record_length_bytes;
        instruction_buffer_context.consumer_page_offset_bytes += active_view->record_length_bytes;

        INSTRUCTION_BUFFER_ClearInstructionView();

        return INSTRUCTION_BUFFER_CONSUME_OK;
    }

    uint32_t next_consumer_stream_offset_bytes =
        instruction_buffer_context.consumer_stream_offset_bytes + active_view->record_length_bytes;

    uint32_t successor_bytes_consumed = active_view->record_length_bytes - bytes_remaining_in_page;

    uint8_t successor_page_index = INSTRUCTION_BUFFER_NextPageIndex( current_page_index );

    /* A crossing record must be backed by the next sequential READY page. */
    if ( successor_bytes_consumed > 0U )
    {
        if ( ( instruction_buffer_context.page_states[successor_page_index]
               != INSTRUCTION_BUFFER_PAGE_READY )
             || ( instruction_buffer_context.page_stream_offsets_bytes[successor_page_index]
                  != ( instruction_buffer_context.consumer_stream_offset_bytes
                       + bytes_remaining_in_page ) )
             || ( successor_bytes_consumed
                  > instruction_buffer_context.page_valid_bytes[successor_page_index] ) )
        {
            return INSTRUCTION_BUFFER_CONSUME_INTERNAL_ERROR;
        }
    }

    /* The current page contains no bytes at or beyond the new consumer cursor. */
    instruction_buffer_context.page_states[current_page_index]      = INSTRUCTION_BUFFER_PAGE_EMPTY;
    instruction_buffer_context.page_valid_bytes[current_page_index] = 0U;
    instruction_buffer_context.page_stream_offsets_bytes[current_page_index] = 0U;

    instruction_buffer_context.consumer_stream_offset_bytes = next_consumer_stream_offset_bytes;

    instruction_buffer_context.consumer_page_index        = successor_page_index;
    instruction_buffer_context.consumer_page_offset_bytes = successor_bytes_consumed;

    /* A final partial successor page may also be exhausted by this record. */
    if ( ( successor_bytes_consumed > 0U )
         && ( successor_bytes_consumed
              == instruction_buffer_context.page_valid_bytes[successor_page_index] ) )
    {
        instruction_buffer_context.page_states[successor_page_index] =
            INSTRUCTION_BUFFER_PAGE_EMPTY;
        instruction_buffer_context.page_valid_bytes[successor_page_index]          = 0U;
        instruction_buffer_context.page_stream_offsets_bytes[successor_page_index] = 0U;

        instruction_buffer_context.consumer_page_index =
            INSTRUCTION_BUFFER_NextPageIndex( successor_page_index );
        instruction_buffer_context.consumer_page_offset_bytes = 0U;
    }

    INSTRUCTION_BUFFER_ClearInstructionView();

    if ( instruction_buffer_context.next_nand_read_offset_bytes
         < instruction_buffer_context.instruction_length_bytes )
    {
        return INSTRUCTION_BUFFER_CONSUME_REFILL_REQUIRED;
    }

    return INSTRUCTION_BUFFER_CONSUME_OK;
}

/**
 * @brief Reports whether the active instruction image was consumed exactly.
 */
bool INSTRUCTION_BUFFER_IsReadComplete( void )
{
    return instruction_buffer_context.is_initialised && instruction_buffer_context.is_read_prepared
           && ( instruction_buffer_context.consumer_stream_offset_bytes
                == instruction_buffer_context.instruction_length_bytes )
           && !instruction_buffer_context.active_instruction_view.is_active;
}
