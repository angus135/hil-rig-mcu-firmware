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
 *      exposed directly from the contiguous page storage; a record crossing
 *      the physical end of the circular storage is assembled in a bounded
 *      scratch buffer so consumers still receive one contiguous payload.
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

/**
 * @brief Authoritative state for the instruction currently exposed to the
 *        Execution Manager.
 *
 * Only one instruction view may be active. The reservation keeps the record
 * storage immutable until the caller consumes it or the read session resets.
 */
typedef struct
{
    bool is_active;

    /** Whether the exposed payload resides in the wrap scratch buffer. */
    bool uses_wrap_scratch;

    /** Logical stream offset of the record header. */
    uint32_t record_stream_offset_bytes;

    /** Combined serialized header and payload length. */
    uint32_t record_length_bytes;

    /** Public view returned to the caller and used for consume validation. */
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
 * @brief Page-partitioned instruction storage filled directly by NAND DMA.
 *
 * Logical NAND pages are placed into these slots sequentially. Slots are reused
 * only after the Execution Manager has consumed every valid byte they contain.
 */
static uint8_t instruction_buffer_storage[INSTRUCTION_BUFFER_MAX_CAPACITY_BYTES];

/**
 * @brief Contiguous representation of a record crossing the physical end of
 *        the circular page-slot storage.
 *
 * A canonical instruction record is limited to one NAND page, so this buffer
 * can hold the complete header and payload.
 */
static uint8_t instruction_buffer_wrap_scratch[EXTERNAL_FLASH_MAX_PAGE_SIZE_BYTES];

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

/** Invalidates the active instruction view without advancing the stream. */
static void INSTRUCTION_BUFFER_ClearInstructionView( void );

/** Returns the next non-zero identifier for a published instruction view. */
static uint32_t INSTRUCTION_BUFFER_AllocateInstructionViewId( void );

/* Logical stream access across circular page slots. */

/** Locates a buffered logical stream offset in a READY page. */
static bool INSTRUCTION_BUFFER_FindReadyPage( uint32_t stream_offset_bytes, uint8_t* page_index,
                                              uint32_t* page_offset_bytes );

/** Copies a logical byte range from one or more READY page slots. */
static bool INSTRUCTION_BUFFER_CopyBufferedBytes( uint32_t stream_offset_bytes,
                                                  uint32_t length_bytes, uint8_t* destination );

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

/* Logical stream access across circular page slots. */

static bool INSTRUCTION_BUFFER_FindReadyPage( uint32_t stream_offset_bytes, uint8_t* page_index,
                                              uint32_t* page_offset_bytes )
{
    if ( ( page_index == NULL ) || ( page_offset_bytes == NULL ) )
    {
        return false;
    }

    for ( uint8_t index = 0U; index < INSTRUCTION_BUFFER_PAGE_COUNT; index++ )
    {
        if ( instruction_buffer_context.page_states[index] != INSTRUCTION_BUFFER_PAGE_READY )
        {
            continue;
        }

        uint32_t page_stream_offset = instruction_buffer_context.page_stream_offsets_bytes[index];

        uint32_t page_valid_bytes = instruction_buffer_context.page_valid_bytes[index];

        /*
         * Subtraction after the lower-bound check avoids overflow from
         * calculating page_stream_offset + page_valid_bytes.
         */
        if ( ( stream_offset_bytes >= page_stream_offset )
             && ( ( stream_offset_bytes - page_stream_offset ) < page_valid_bytes ) )
        {
            *page_index        = index;
            *page_offset_bytes = stream_offset_bytes - page_stream_offset;

            return true;
        }
    }

    return false;
}

static bool INSTRUCTION_BUFFER_CopyBufferedBytes( uint32_t stream_offset_bytes,
                                                  uint32_t length_bytes, uint8_t* destination )
{
    /* An empty range requires no backing page and is always available. */
    if ( length_bytes == 0U )
    {
        return true;
    }

    if ( destination == NULL )
    {
        return false;
    }

    uint32_t copied_bytes = 0U;

    /* Resolve each successive range against READY slots in logical order. */
    while ( copied_bytes < length_bytes )
    {
        uint8_t  page_index        = 0U;
        uint32_t page_offset_bytes = 0U;

        if ( !INSTRUCTION_BUFFER_FindReadyPage( stream_offset_bytes, &page_index,
                                                &page_offset_bytes ) )
        {
            return false;
        }

        uint32_t available_bytes =
            instruction_buffer_context.page_valid_bytes[page_index] - page_offset_bytes;

        uint32_t remaining_bytes   = length_bytes - copied_bytes;
        uint32_t copy_length_bytes = available_bytes;

        if ( copy_length_bytes > remaining_bytes )
        {
            copy_length_bytes = remaining_bytes;
        }

        uint32_t storage_offset_bytes =
            ( ( uint32_t )page_index * instruction_buffer_context.page_size_bytes )
            + page_offset_bytes;

        /* Copy only as far as this page or the requested range permits. */
        memcpy( &destination[copied_bytes], &instruction_buffer_storage[storage_offset_bytes],
                copy_length_bytes );

        copied_bytes += copy_length_bytes;
        stream_offset_bytes += copy_length_bytes;
    }

    return true;
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

    if ( buffer_capacity_bytes > sizeof( instruction_buffer_storage ) )
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
 * @note The record parsing and publication path is intentionally completed in
 *       a later implementation step. Until then, no instruction is exposed.
 */
InstructionBufferPeekStatus_T
INSTRUCTION_BUFFER_PeekInstruction( FlashManagerInstructionView_T* instruction )
{
    if ( instruction == NULL )
    {
        return INSTRUCTION_BUFFER_PEEK_INVALID_ARGUMENT;
    }

    /* Preserve the public contract while record parsing remains incomplete. */
    *instruction = ( FlashManagerInstructionView_T ){ 0 };

    return INSTRUCTION_BUFFER_PEEK_NOT_BUFFERED;
}
