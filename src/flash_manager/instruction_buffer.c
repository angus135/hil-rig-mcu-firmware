/******************************************************************************
 *  File:       instruction_buffer.c
 *  Author:     Callum Rafferty
 *  Created:    25-Mar-2026
 *
 *  Description:
 *      Implementation of the Flash Manager instruction buffer.
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
 *      Timestamp scheduling is intentionally owned by the Execution Manager;
 *      this module only caches, exposes, and advances the ordered byte stream.
 *
 *      Upload reuses the same three page slots in the opposite direction: the
 *      Host Interface copies canonical stream chunks into RAM and the Flash
 *      Manager task drains complete pages to NAND. Retrieval and upload are
 *      mutually exclusive.
 *
 *      The calling layer serialises lifecycle operations. This module makes no
 *      external-flash policy decisions and never blocks; successful NAND-fill
 *      completion uses one short task critical section to publish prepared page
 *      metadata atomically to the execution ISR.
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

/* Keep page-release bookkeeping out of the per-instruction common path. */
#if defined( __GNUC__ ) || defined( __clang__ )
#define INSTRUCTION_BUFFER_COLD_NOINLINE __attribute__( ( cold, noinline ) )
#else
#define INSTRUCTION_BUFFER_COLD_NOINLINE
#endif

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

/* Shared state management types. */

typedef enum
{
    /** Available to either instruction-buffer data flow. */
    INSTRUCTION_BUFFER_PAGE_EMPTY = 0,

    /** A NAND read currently owns and is filling this page. */
    INSTRUCTION_BUFFER_PAGE_FILLING_FROM_NAND,

    /** Valid instruction bytes are available to the execution consumer. */
    INSTRUCTION_BUFFER_PAGE_READY,

    /** The Host Interface is appending upload bytes to this page. */
    INSTRUCTION_BUFFER_PAGE_FILLING_FROM_HOST,

    /** A complete upload page is waiting to be written to NAND. */
    INSTRUCTION_BUFFER_PAGE_READY_FOR_NAND,

    /** The Flash Manager task is currently writing this page to NAND. */
    INSTRUCTION_BUFFER_PAGE_WRITING_TO_NAND
} InstructionBufferPageState_T;

/* Instruction retrieval types. */

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

/** @brief Prepared header, payload view, and length for the current instruction. */
typedef struct
{
    /** Authoritative combined serialized header and payload length. */
    uint32_t record_length_bytes;

    /** Complete logical header-and-payload view exposed to the consumer. */
    FlashManagerInstructionView_T view;
} InstructionBufferInstructionCache_T;

/* Instruction upload types. */

/** @brief Result of checking whether an upload chunk fits without mutation. */
typedef enum
{
    INSTRUCTION_BUFFER_UPLOAD_CAPACITY_AVAILABLE = 0,
    INSTRUCTION_BUFFER_UPLOAD_CAPACITY_BUSY,
    INSTRUCTION_BUFFER_UPLOAD_CAPACITY_INVALID
} InstructionBufferUploadCapacityStatus_T;

/**
 * @brief Shared geometry, page ownership, and directional stream state.
 */
typedef struct
{
    /* Shared state management. */

    /** Geometry is configured by Init and retained across retrieval sessions. */
    bool is_initialised;

    /** Runtime NAND page size used for slot addressing and read lengths. */
    uint32_t page_size_bytes;

    /** Maximum logical instruction image length accepted by PrepareRead. */
    uint32_t instruction_partition_capacity_bytes;

    /* Instruction retrieval: NAND producer position and lease identity. */

    /** Whether PrepareRead successfully configured an instruction image. */
    bool is_read_prepared;

    /** Total logical length of the active canonical instruction image. */
    uint32_t instruction_length_bytes;

    /** Logical offset of the next instruction page that must be read from NAND. */
    uint32_t next_nand_read_offset_bytes;

    /** Slot that will receive the next sequential NAND page. */
    uint8_t next_fill_page_index;

    /** Identifier to assign to the next page-fill reservation. */
    uint32_t next_page_fill_lease_id;

    /* Instruction retrieval: execution consumer position. */

    /** Logical stream offset of the next unconsumed instruction record. */
    uint32_t consumer_stream_offset_bytes;

    /** Slot containing the next unconsumed instruction byte. */
    uint8_t consumer_page_index;

    /** Offset of the next unconsumed instruction byte within its slot. */
    uint32_t consumer_page_offset_bytes;

    /** Direct address of the next unconsumed instruction header. */
    uint8_t* consumer_record_pointer;

    /* Shared page metadata. */

    /** Current ownership state of each page slot. */
    InstructionBufferPageState_T page_states[INSTRUCTION_BUFFER_PAGE_COUNT];

    /** Valid logical instruction bytes held in each page slot. */
    uint32_t page_valid_bytes[INSTRUCTION_BUFFER_PAGE_COUNT];

    /* Instruction retrieval ownership. */

    /** The one NAND page-fill reservation that may currently be outstanding. */
    InstructionBufferPageFillReservation_T active_page_fill_reservation;

    /** Prepared view and serialized length of the current instruction. */
    InstructionBufferInstructionCache_T instruction_cache;

    /* Instruction upload lifecycle and ownership. */

    /** Whether RAM is currently configured for host-to-NAND upload. */
    bool is_upload_prepared;

    /** Whether host input ended and the final partial page was published. */
    bool is_upload_finalised;

    /** Total canonical byte count declared for the active upload. */
    uint32_t upload_expected_length_bytes;

    /** Total host bytes atomically accepted during the active upload. */
    uint32_t upload_accepted_length_bytes;

    /** Total accepted bytes successfully persisted to NAND. */
    uint32_t upload_persisted_length_bytes;

    /** Page into which the next host byte will be appended. */
    uint8_t upload_write_page_index;

    /** Oldest upload page waiting to be drained to NAND. */
    uint8_t upload_drain_page_index;

} InstructionBufferContext_T;

/**-----------------------------------------------------------------------------
 *  Public (global) and Extern Variables
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Private (static) Variables
 *------------------------------------------------------------------------------
 */

/* Shared storage and state for the mutually exclusive data flows. */

/**
 * @brief Circular instruction page storage followed by a slot-zero mirror.
 *
 * The first three runtime pages are shared by retrieval and upload. The final
 * maximum-record region is retrieval-only: it mirrors the valid prefix of slot
 * zero so a slot-two-to-slot-zero record has one contiguous address range.
 */
static uint8_t instruction_buffer_storage[INSTRUCTION_BUFFER_STORAGE_BYTES];

/** Shared geometry plus mutually exclusive retrieval and upload state. */
static InstructionBufferContext_T instruction_buffer_context;

/**-----------------------------------------------------------------------------
 *  Private (static) Function Prototypes
 *------------------------------------------------------------------------------
 */

/* Shared state management. */

/** Returns the circular successor of a valid page-slot index. */
static inline uint8_t INSTRUCTION_BUFFER_NextPageIndex( uint8_t page_index );

/** Returns the start of a valid runtime page slot. */
static inline uint8_t* INSTRUCTION_BUFFER_GetPageData( uint8_t page_index );

/** Releases all page slots and invalidates their stream metadata. */
static void INSTRUCTION_BUFFER_ResetPages( void );

/* Instruction retrieval. */

/** Returns the RAM destination owned by the active page-fill reservation. */
static uint8_t* INSTRUCTION_BUFFER_GetActivePageFillData( void );

/** Validates an external fill lease against the authoritative reservation. */
static bool
INSTRUCTION_BUFFER_PageFillLeaseMatches( const InstructionBufferPageFillLease_T* lease );

/** Invalidates the active page-fill reservation without changing page data. */
static void INSTRUCTION_BUFFER_ClearPageFillReservation( void );

/** Invalidates the prepared instruction cache without advancing the stream. */
static inline void INSTRUCTION_BUFFER_ClearInstructionCache( void );

/** Releases exhausted pages after the current record reaches a page boundary. */
static INSTRUCTION_BUFFER_COLD_NOINLINE InstructionBufferConsumeStatus_T
INSTRUCTION_BUFFER_ConsumeAcrossPageBoundary( uint32_t record_length_bytes,
                                              uint32_t bytes_remaining_in_page );

/* Instruction upload. */

/** Validates the oldest upload page against its expected ownership state. */
static bool
INSTRUCTION_BUFFER_UploadDrainPageIsValid( uint8_t                      page_index,
                                           InstructionBufferPageState_T expected_state );

/** Returns true when every runtime page is empty. */
static bool INSTRUCTION_BUFFER_AreUploadPagesEmpty( void );

/** Checks whether a complete host chunk fits without changing buffer state. */
static InstructionBufferUploadCapacityStatus_T
INSTRUCTION_BUFFER_CheckUploadCapacity( uint32_t length );

/** Copies a preflighted host chunk and reports whether a page became ready. */
static bool INSTRUCTION_BUFFER_CopyUploadBytes( const uint8_t* data, uint32_t length );

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

/* Shared state management. */

static inline uint8_t INSTRUCTION_BUFFER_NextPageIndex( uint8_t page_index )
{
    return ( uint8_t )( ( page_index + 1U ) % INSTRUCTION_BUFFER_PAGE_COUNT );
}

static inline uint8_t* INSTRUCTION_BUFFER_GetPageData( uint8_t page_index )
{
    uint32_t page_offset_bytes =
        ( uint32_t )page_index * instruction_buffer_context.page_size_bytes;

    return &instruction_buffer_storage[page_offset_bytes];
}

static void INSTRUCTION_BUFFER_ResetPages( void )
{
    for ( uint32_t page_index = 0U; page_index < INSTRUCTION_BUFFER_PAGE_COUNT; page_index++ )
    {
        instruction_buffer_context.page_states[page_index]      = INSTRUCTION_BUFFER_PAGE_EMPTY;
        instruction_buffer_context.page_valid_bytes[page_index] = 0U;
    }
}

/* Instruction retrieval. */

static uint8_t* INSTRUCTION_BUFFER_GetActivePageFillData( void )
{
    return INSTRUCTION_BUFFER_GetPageData(
        instruction_buffer_context.active_page_fill_reservation.page_index );
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

static inline void INSTRUCTION_BUFFER_ClearInstructionCache( void )
{
    instruction_buffer_context.instruction_cache = ( InstructionBufferInstructionCache_T ){ 0 };
}

/**
 * @brief Releases pages exhausted by a boundary-reaching instruction.
 */
static INSTRUCTION_BUFFER_COLD_NOINLINE InstructionBufferConsumeStatus_T
INSTRUCTION_BUFFER_ConsumeAcrossPageBoundary( uint32_t record_length_bytes,
                                              uint32_t bytes_remaining_in_page )
{
    uint8_t  current_page_index       = instruction_buffer_context.consumer_page_index;
    uint8_t  successor_page_index     = INSTRUCTION_BUFFER_NextPageIndex( current_page_index );
    uint32_t successor_bytes_consumed = record_length_bytes - bytes_remaining_in_page;

    instruction_buffer_context.page_states[current_page_index]      = INSTRUCTION_BUFFER_PAGE_EMPTY;
    instruction_buffer_context.page_valid_bytes[current_page_index] = 0U;

    instruction_buffer_context.consumer_page_index        = successor_page_index;
    instruction_buffer_context.consumer_page_offset_bytes = successor_bytes_consumed;
    instruction_buffer_context.consumer_record_pointer =
        INSTRUCTION_BUFFER_GetPageData( successor_page_index ) + successor_bytes_consumed;

    /* A crossing record may also exhaust a final partial successor page. */
    if ( ( successor_bytes_consumed > 0U )
         && ( successor_bytes_consumed
              == instruction_buffer_context.page_valid_bytes[successor_page_index] ) )
    {
        instruction_buffer_context.page_states[successor_page_index] =
            INSTRUCTION_BUFFER_PAGE_EMPTY;
        instruction_buffer_context.page_valid_bytes[successor_page_index] = 0U;

        instruction_buffer_context.consumer_page_index =
            INSTRUCTION_BUFFER_NextPageIndex( successor_page_index );
        instruction_buffer_context.consumer_page_offset_bytes = 0U;
        instruction_buffer_context.consumer_record_pointer =
            INSTRUCTION_BUFFER_GetPageData( instruction_buffer_context.consumer_page_index );
    }

    return ( instruction_buffer_context.next_nand_read_offset_bytes
             < instruction_buffer_context.instruction_length_bytes )
               ? INSTRUCTION_BUFFER_CONSUME_REFILL_REQUIRED
               : INSTRUCTION_BUFFER_CONSUME_OK;
}

/* Instruction upload. */

static bool INSTRUCTION_BUFFER_UploadDrainPageIsValid( uint8_t                      page_index,
                                                       InstructionBufferPageState_T expected_state )
{
    if ( ( page_index >= INSTRUCTION_BUFFER_PAGE_COUNT )
         || ( page_index != instruction_buffer_context.upload_drain_page_index )
         || ( instruction_buffer_context.page_states[page_index] != expected_state )
         || ( instruction_buffer_context.upload_persisted_length_bytes
              > instruction_buffer_context.upload_accepted_length_bytes ) )
    {
        return false;
    }

    uint32_t valid_length_bytes   = instruction_buffer_context.page_valid_bytes[page_index];
    uint32_t pending_length_bytes = instruction_buffer_context.upload_accepted_length_bytes
                                    - instruction_buffer_context.upload_persisted_length_bytes;

    return ( valid_length_bytes > 0U )
           && ( valid_length_bytes <= instruction_buffer_context.page_size_bytes )
           && ( valid_length_bytes <= pending_length_bytes );
}

static bool INSTRUCTION_BUFFER_AreUploadPagesEmpty( void )
{
    for ( uint32_t page_index = 0U; page_index < INSTRUCTION_BUFFER_PAGE_COUNT; page_index++ )
    {
        if ( ( instruction_buffer_context.page_states[page_index] != INSTRUCTION_BUFFER_PAGE_EMPTY )
             || ( instruction_buffer_context.page_valid_bytes[page_index] != 0U ) )
        {
            return false;
        }
    }

    return true;
}

static InstructionBufferUploadCapacityStatus_T
INSTRUCTION_BUFFER_CheckUploadCapacity( uint32_t length )
{
    uint8_t                      page_index = instruction_buffer_context.upload_write_page_index;
    InstructionBufferPageState_T page_state = instruction_buffer_context.page_states[page_index];
    uint32_t valid_length_bytes = instruction_buffer_context.page_valid_bytes[page_index];
    uint32_t available_length_bytes;

    if ( page_state == INSTRUCTION_BUFFER_PAGE_EMPTY )
    {
        if ( valid_length_bytes != 0U )
        {
            return INSTRUCTION_BUFFER_UPLOAD_CAPACITY_INVALID;
        }

        available_length_bytes = instruction_buffer_context.page_size_bytes;
    }
    else if ( page_state == INSTRUCTION_BUFFER_PAGE_FILLING_FROM_HOST )
    {
        if ( ( valid_length_bytes >= instruction_buffer_context.page_size_bytes )
             || ( valid_length_bytes > instruction_buffer_context.upload_accepted_length_bytes ) )
        {
            return INSTRUCTION_BUFFER_UPLOAD_CAPACITY_INVALID;
        }

        available_length_bytes = instruction_buffer_context.page_size_bytes - valid_length_bytes;
    }
    else
    {
        return ( page_state == INSTRUCTION_BUFFER_PAGE_READY_FOR_NAND
                 || page_state == INSTRUCTION_BUFFER_PAGE_WRITING_TO_NAND )
                   ? INSTRUCTION_BUFFER_UPLOAD_CAPACITY_BUSY
                   : INSTRUCTION_BUFFER_UPLOAD_CAPACITY_INVALID;
    }

    if ( length <= available_length_bytes )
    {
        return INSTRUCTION_BUFFER_UPLOAD_CAPACITY_AVAILABLE;
    }

    /* A one-page host chunk can cross into at most one successor page. */
    uint8_t successor_page_index = INSTRUCTION_BUFFER_NextPageIndex( page_index );
    InstructionBufferPageState_T successor_state =
        instruction_buffer_context.page_states[successor_page_index];

    if ( successor_state != INSTRUCTION_BUFFER_PAGE_EMPTY )
    {
        return ( successor_state == INSTRUCTION_BUFFER_PAGE_READY_FOR_NAND
                 || successor_state == INSTRUCTION_BUFFER_PAGE_WRITING_TO_NAND )
                   ? INSTRUCTION_BUFFER_UPLOAD_CAPACITY_BUSY
                   : INSTRUCTION_BUFFER_UPLOAD_CAPACITY_INVALID;
    }

    return ( instruction_buffer_context.page_valid_bytes[successor_page_index] == 0U )
               ? INSTRUCTION_BUFFER_UPLOAD_CAPACITY_AVAILABLE
               : INSTRUCTION_BUFFER_UPLOAD_CAPACITY_INVALID;
}

static bool INSTRUCTION_BUFFER_CopyUploadBytes( const uint8_t* data, uint32_t length )
{
    uint32_t source_offset_bytes = 0U;
    bool     page_became_ready   = false;

    while ( source_offset_bytes < length )
    {
        uint8_t page_index = instruction_buffer_context.upload_write_page_index;

        if ( instruction_buffer_context.page_states[page_index] == INSTRUCTION_BUFFER_PAGE_EMPTY )
        {
            instruction_buffer_context.page_states[page_index] =
                INSTRUCTION_BUFFER_PAGE_FILLING_FROM_HOST;
        }

        uint32_t page_valid_bytes = instruction_buffer_context.page_valid_bytes[page_index];
        uint32_t bytes_remaining  = length - source_offset_bytes;
        uint32_t page_available   = instruction_buffer_context.page_size_bytes - page_valid_bytes;
        uint32_t copy_length =
            ( bytes_remaining < page_available ) ? bytes_remaining : page_available;

        memcpy( &INSTRUCTION_BUFFER_GetPageData( page_index )[page_valid_bytes],
                &data[source_offset_bytes], copy_length );

        page_valid_bytes += copy_length;
        source_offset_bytes += copy_length;
        instruction_buffer_context.page_valid_bytes[page_index] = page_valid_bytes;

        if ( page_valid_bytes == instruction_buffer_context.page_size_bytes )
        {
            instruction_buffer_context.page_states[page_index] =
                INSTRUCTION_BUFFER_PAGE_READY_FOR_NAND;
            instruction_buffer_context.upload_write_page_index =
                INSTRUCTION_BUFFER_NextPageIndex( page_index );
            page_became_ready = true;
        }
    }

    instruction_buffer_context.upload_accepted_length_bytes += length;

    return page_became_ready;
}

/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */

/* Shared state management. */

/**
 * @brief Initialises runtime geometry and invalidates all previous buffer state.
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

    instruction_buffer_context.next_page_fill_lease_id = 1U;
    instruction_buffer_context.is_initialised          = true;

    return true;
}

/* Instruction retrieval: lifecycle, NAND fill, and execution serving. */

/**
 * @brief Resets page ownership and cursors for one instruction image.
 */
bool INSTRUCTION_BUFFER_PrepareRead( uint32_t instruction_length_bytes )
{
    if ( !instruction_buffer_context.is_initialised || instruction_buffer_context.is_upload_prepared
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
    instruction_buffer_context.consumer_record_pointer      = instruction_buffer_storage;

    INSTRUCTION_BUFFER_ClearPageFillReservation();
    INSTRUCTION_BUFFER_ClearInstructionCache();
    INSTRUCTION_BUFFER_ResetPages();

    /*
     * Preserve the monotonically changing lease sequence across session resets
     * so a lease from the previous session cannot match newly reused storage.
     */
    if ( instruction_buffer_context.next_page_fill_lease_id == 0U )
    {
        instruction_buffer_context.next_page_fill_lease_id = 1U;
    }

    instruction_buffer_context.is_read_prepared = true;

    return true;
}

/**
 * @brief Invalidates the active instruction stream while retaining geometry.
 */
void INSTRUCTION_BUFFER_EndRead( void )
{
    if ( !instruction_buffer_context.is_initialised
         || instruction_buffer_context.is_upload_prepared )
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
    instruction_buffer_context.consumer_record_pointer      = instruction_buffer_storage;

    INSTRUCTION_BUFFER_ClearPageFillReservation();
    INSTRUCTION_BUFFER_ClearInstructionCache();
    INSTRUCTION_BUFFER_ResetPages();
}

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
    }

    /*
     * The potentially page-sized mirror copy is complete. Mask interrupts only
     * for the small metadata publication that makes the new bytes visible to
     * the execution ISR.
     */
    taskENTER_CRITICAL();

    if ( nand_read_succeeded )
    {
        instruction_buffer_context.page_valid_bytes[page_index] = read_length_bytes;

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
    }

    INSTRUCTION_BUFFER_ClearPageFillReservation();

    taskEXIT_CRITICAL();

    return true;
}

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
    /* A future instruction is re-peeked on later ticks through this cached path. */
    if ( instruction_buffer_context.instruction_cache.record_length_bytes != 0U )
    {
        *instruction = &instruction_buffer_context.instruction_cache.view;
        return INSTRUCTION_BUFFER_PEEK_AVAILABLE;
    }

    uint32_t record_stream_offset_bytes = instruction_buffer_context.consumer_stream_offset_bytes;

    if ( record_stream_offset_bytes == instruction_buffer_context.instruction_length_bytes )
    {
        return INSTRUCTION_BUFFER_PEEK_END_OF_STREAM;
    }

    uint32_t buffered_unread_bytes =
        instruction_buffer_context.next_nand_read_offset_bytes - record_stream_offset_bytes;

    if ( buffered_unread_bytes < sizeof( FlashManagerInstructionHeader_T ) )
    {
        return INSTRUCTION_BUFFER_PEEK_NOT_BUFFERED;
    }

    FlashManagerInstructionHeader_T header = { 0 };

    /*
     * Copy the fixed header into an aligned object for safe field access. The
     * slot-zero mirror makes this a single bounded copy even at the ring end.
     */
    memcpy( &header, instruction_buffer_context.consumer_record_pointer, sizeof( header ) );

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

    instruction_buffer_context.instruction_cache = ( InstructionBufferInstructionCache_T ){
        .record_length_bytes = record_length_bytes,
        .view                = { .header = header,
                                 .payload =
                                     instruction_buffer_context.consumer_record_pointer + sizeof( header ) } };

    *instruction = &instruction_buffer_context.instruction_cache.view;

    return INSTRUCTION_BUFFER_PEEK_AVAILABLE;
}

/**
 * @brief Advances past the active instruction and releases exhausted pages.
 *
 * The common path advances within the current page. Page-state transitions are
 * required only when the record reaches or crosses a page boundary.
 */
InstructionBufferConsumeStatus_T INSTRUCTION_BUFFER_ConsumeInstruction( void )
{
    uint8_t  current_page_index  = instruction_buffer_context.consumer_page_index;
    uint32_t record_length_bytes = instruction_buffer_context.instruction_cache.record_length_bytes;
    uint32_t bytes_remaining_in_page =
        instruction_buffer_context.page_valid_bytes[current_page_index]
        - instruction_buffer_context.consumer_page_offset_bytes;

    instruction_buffer_context.consumer_stream_offset_bytes += record_length_bytes;
    instruction_buffer_context.instruction_cache.record_length_bytes = 0U;

    /* Most instructions remain within their current page. */
    if ( record_length_bytes < bytes_remaining_in_page )
    {
        instruction_buffer_context.consumer_page_offset_bytes += record_length_bytes;
        instruction_buffer_context.consumer_record_pointer += record_length_bytes;

        return INSTRUCTION_BUFFER_CONSUME_OK;
    }

    return INSTRUCTION_BUFFER_ConsumeAcrossPageBoundary( record_length_bytes,
                                                         bytes_remaining_in_page );
}

/* Instruction upload: host production, NAND drain, and lifecycle. */

/**
 * @brief Prepares shared instruction RAM for a host-to-NAND upload.
 */
bool INSTRUCTION_BUFFER_PrepareUpload( uint32_t expected_length_bytes )
{
    if ( !instruction_buffer_context.is_initialised || instruction_buffer_context.is_read_prepared
         || instruction_buffer_context.is_upload_prepared
         || instruction_buffer_context.active_page_fill_reservation.is_active
         || ( expected_length_bytes == 0U )
         || ( expected_length_bytes
              > instruction_buffer_context.instruction_partition_capacity_bytes ) )
    {
        return false;
    }

    /* Keep both data flows unavailable until every shared ownership field is reset. */
    instruction_buffer_context.is_upload_prepared  = false;
    instruction_buffer_context.is_upload_finalised = false;

    instruction_buffer_context.instruction_length_bytes     = 0U;
    instruction_buffer_context.next_nand_read_offset_bytes  = 0U;
    instruction_buffer_context.next_fill_page_index         = 0U;
    instruction_buffer_context.consumer_stream_offset_bytes = 0U;
    instruction_buffer_context.consumer_page_index          = 0U;
    instruction_buffer_context.consumer_page_offset_bytes   = 0U;
    instruction_buffer_context.consumer_record_pointer      = instruction_buffer_storage;

    instruction_buffer_context.upload_expected_length_bytes  = expected_length_bytes;
    instruction_buffer_context.upload_accepted_length_bytes  = 0U;
    instruction_buffer_context.upload_persisted_length_bytes = 0U;
    instruction_buffer_context.upload_write_page_index       = 0U;
    instruction_buffer_context.upload_drain_page_index       = 0U;

    INSTRUCTION_BUFFER_ClearPageFillReservation();
    INSTRUCTION_BUFFER_ClearInstructionCache();
    INSTRUCTION_BUFFER_ResetPages();

    instruction_buffer_context.is_upload_prepared = true;

    return true;
}

/**
 * @brief Returns the declared total length of the prepared upload.
 */
bool INSTRUCTION_BUFFER_GetUploadExpectedLength( uint32_t* expected_length_bytes )
{
    if ( ( expected_length_bytes == NULL ) || !instruction_buffer_context.is_initialised
         || !instruction_buffer_context.is_upload_prepared )
    {
        return false;
    }

    *expected_length_bytes = instruction_buffer_context.upload_expected_length_bytes;

    return true;
}

/**
 * @brief Atomically appends one complete host chunk to upload RAM.
 */
InstructionBufferUploadWriteStatus_T INSTRUCTION_BUFFER_WriteUploadBytes( const uint8_t* data,
                                                                          uint32_t       length )
{
    if ( !instruction_buffer_context.is_initialised
         || !instruction_buffer_context.is_upload_prepared
         || instruction_buffer_context.is_upload_finalised )
    {
        return INSTRUCTION_BUFFER_UPLOAD_WRITE_INVALID_STATE;
    }

    if ( ( data == NULL ) || ( length == 0U )
         || ( length > instruction_buffer_context.page_size_bytes ) )
    {
        return INSTRUCTION_BUFFER_UPLOAD_WRITE_INVALID_ARGUMENT;
    }

    if ( ( instruction_buffer_context.upload_write_page_index >= INSTRUCTION_BUFFER_PAGE_COUNT )
         || ( instruction_buffer_context.upload_accepted_length_bytes
              > instruction_buffer_context.upload_expected_length_bytes ) )
    {
        return INSTRUCTION_BUFFER_UPLOAD_WRITE_INVALID_STATE;
    }

    uint32_t remaining_upload_bytes = instruction_buffer_context.upload_expected_length_bytes
                                      - instruction_buffer_context.upload_accepted_length_bytes;

    if ( length > remaining_upload_bytes )
    {
        return INSTRUCTION_BUFFER_UPLOAD_WRITE_INVALID_ARGUMENT;
    }

    InstructionBufferUploadCapacityStatus_T capacity_status =
        INSTRUCTION_BUFFER_CheckUploadCapacity( length );

    if ( capacity_status == INSTRUCTION_BUFFER_UPLOAD_CAPACITY_BUSY )
    {
        return INSTRUCTION_BUFFER_UPLOAD_WRITE_BUSY;
    }

    if ( capacity_status != INSTRUCTION_BUFFER_UPLOAD_CAPACITY_AVAILABLE )
    {
        return INSTRUCTION_BUFFER_UPLOAD_WRITE_INVALID_STATE;
    }

    return INSTRUCTION_BUFFER_CopyUploadBytes( data, length )
               ? INSTRUCTION_BUFFER_UPLOAD_WRITE_PAGE_READY
               : INSTRUCTION_BUFFER_UPLOAD_WRITE_ACCEPTED;
}

/**
 * @brief Acquires the oldest completed upload page for a NAND write.
 */
bool INSTRUCTION_BUFFER_AcquireUploadDrainPage( const uint8_t** page_data,
                                                uint32_t*       valid_length_bytes )
{
    if ( ( page_data == NULL ) || ( valid_length_bytes == NULL ) )
    {
        return false;
    }

    /* Failed acquisition must never leave stale output values in the caller. */
    *page_data          = NULL;
    *valid_length_bytes = 0U;

    if ( !instruction_buffer_context.is_initialised
         || !instruction_buffer_context.is_upload_prepared )
    {
        return false;
    }

    uint8_t page_index = instruction_buffer_context.upload_drain_page_index;

    if ( !INSTRUCTION_BUFFER_UploadDrainPageIsValid( page_index,
                                                     INSTRUCTION_BUFFER_PAGE_READY_FOR_NAND ) )
    {
        return false;
    }

    /* WRITING_TO_NAND is the authoritative ownership marker. */
    instruction_buffer_context.page_states[page_index] = INSTRUCTION_BUFFER_PAGE_WRITING_TO_NAND;

    *page_data          = INSTRUCTION_BUFFER_GetPageData( page_index );
    *valid_length_bytes = instruction_buffer_context.page_valid_bytes[page_index];

    return true;
}

/**
 * @brief Completes the NAND write owning the oldest upload page.
 */
bool INSTRUCTION_BUFFER_CompleteUploadDrain( bool nand_write_succeeded )
{
    if ( !instruction_buffer_context.is_initialised
         || !instruction_buffer_context.is_upload_prepared )
    {
        return false;
    }

    uint8_t page_index = instruction_buffer_context.upload_drain_page_index;

    if ( !INSTRUCTION_BUFFER_UploadDrainPageIsValid( page_index,
                                                     INSTRUCTION_BUFFER_PAGE_WRITING_TO_NAND ) )
    {
        return false;
    }

    uint32_t valid_length_bytes = instruction_buffer_context.page_valid_bytes[page_index];

    if ( nand_write_succeeded )
    {
        instruction_buffer_context.upload_persisted_length_bytes += valid_length_bytes;
        instruction_buffer_context.page_states[page_index]      = INSTRUCTION_BUFFER_PAGE_EMPTY;
        instruction_buffer_context.page_valid_bytes[page_index] = 0U;
        instruction_buffer_context.upload_drain_page_index =
            INSTRUCTION_BUFFER_NextPageIndex( page_index );
    }
    else
    {
        /* Retain the page and cursor so the identical NAND write can be retried. */
        instruction_buffer_context.page_states[page_index] = INSTRUCTION_BUFFER_PAGE_READY_FOR_NAND;
    }

    return true;
}

/**
 * @brief Stops host production and publishes the final partial upload page.
 */
bool INSTRUCTION_BUFFER_FinaliseUpload( void )
{
    uint8_t drain_page_index = instruction_buffer_context.upload_drain_page_index;

    if ( !instruction_buffer_context.is_initialised
         || !instruction_buffer_context.is_upload_prepared
         || instruction_buffer_context.is_upload_finalised
         || ( drain_page_index >= INSTRUCTION_BUFFER_PAGE_COUNT )
         || ( instruction_buffer_context.page_states[drain_page_index]
              == INSTRUCTION_BUFFER_PAGE_WRITING_TO_NAND )
         || ( instruction_buffer_context.upload_accepted_length_bytes
              != instruction_buffer_context.upload_expected_length_bytes )
         || ( instruction_buffer_context.page_size_bytes == 0U )
         || ( instruction_buffer_context.upload_persisted_length_bytes
              > instruction_buffer_context.upload_accepted_length_bytes ) )
    {
        return false;
    }

    uint32_t partial_length_bytes = instruction_buffer_context.upload_expected_length_bytes
                                    % instruction_buffer_context.page_size_bytes;

    if ( partial_length_bytes > 0U )
    {
        uint8_t page_index = instruction_buffer_context.upload_write_page_index;

        if ( ( page_index >= INSTRUCTION_BUFFER_PAGE_COUNT )
             || ( instruction_buffer_context.page_states[page_index]
                  != INSTRUCTION_BUFFER_PAGE_FILLING_FROM_HOST )
             || ( instruction_buffer_context.page_valid_bytes[page_index]
                  != partial_length_bytes ) )
        {
            return false;
        }

        instruction_buffer_context.page_states[page_index] = INSTRUCTION_BUFFER_PAGE_READY_FOR_NAND;
    }
    else
    {
        /* A page-aligned stream must not leave an unpublished producer page. */
        for ( uint32_t page_index = 0U; page_index < INSTRUCTION_BUFFER_PAGE_COUNT; page_index++ )
        {
            if ( instruction_buffer_context.page_states[page_index]
                 == INSTRUCTION_BUFFER_PAGE_FILLING_FROM_HOST )
            {
                return false;
            }
        }
    }

    instruction_buffer_context.is_upload_finalised = true;

    return true;
}

/**
 * @brief Reports whether every declared host byte has entered upload RAM.
 */
bool INSTRUCTION_BUFFER_IsUploadInputComplete( void )
{
    return instruction_buffer_context.is_initialised
           && instruction_buffer_context.is_upload_prepared
           && ( instruction_buffer_context.upload_accepted_length_bytes
                == instruction_buffer_context.upload_expected_length_bytes );
}

/**
 * @brief Reports whether finalised upload data has completely drained from RAM.
 */
bool INSTRUCTION_BUFFER_IsUploadPersisted( void )
{
    return instruction_buffer_context.is_initialised
           && instruction_buffer_context.is_upload_prepared
           && instruction_buffer_context.is_upload_finalised
           && ( instruction_buffer_context.upload_accepted_length_bytes
                == instruction_buffer_context.upload_expected_length_bytes )
           && ( instruction_buffer_context.upload_persisted_length_bytes
                == instruction_buffer_context.upload_expected_length_bytes )
           && INSTRUCTION_BUFFER_AreUploadPagesEmpty();
}

/**
 * @brief Releases upload state after external-flash finalisation succeeds.
 */
bool INSTRUCTION_BUFFER_EndUpload( void )
{
    if ( !INSTRUCTION_BUFFER_IsUploadPersisted() )
    {
        return false;
    }

    instruction_buffer_context.is_upload_prepared            = false;
    instruction_buffer_context.is_upload_finalised           = false;
    instruction_buffer_context.upload_expected_length_bytes  = 0U;
    instruction_buffer_context.upload_accepted_length_bytes  = 0U;
    instruction_buffer_context.upload_persisted_length_bytes = 0U;
    instruction_buffer_context.upload_write_page_index       = 0U;
    instruction_buffer_context.upload_drain_page_index       = 0U;

    INSTRUCTION_BUFFER_ResetPages();

    return true;
}
