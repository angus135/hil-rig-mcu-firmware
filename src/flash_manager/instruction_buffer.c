/******************************************************************************
 *  File:       instruction_buffer.c
 *  Author:     Callum Rafferty
 *  Created:    25-Mar-2026
 *
 *  Description:
 *      Implementation of the Flash Manager instruction retrieval buffer.
 *
 *  Notes:
 *      The Flash Manager task reads sequential NAND pages directly into this
 *      module's page slots. The future execution-facing path will expose
 *      complete timestamped instructions from READY pages without accessing
 *      NAND in the execution ISR.
 *
 *      State-changing calls must be serialised by the Flash Manager. This
 *      module does not own RTOS synchronisation or external-flash policy.
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
 * @brief Geometry, page ownership, and sequential retrieval cursors.
 */
typedef struct
{
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

    /** Logical offset of the next instruction page that must be read from NAND. */
    uint32_t next_nand_read_offset_bytes;

    /** Slot that will receive the next sequential NAND page. */
    uint8_t next_fill_page_index;

    /** Logical stream offset of the next unconsumed instruction record. */
    uint32_t consumer_stream_offset_bytes;

    /** Slot containing the next unconsumed instruction byte. */
    uint8_t consumer_page_index;

    /** Offset of the next unconsumed instruction byte within its slot. */
    uint32_t consumer_page_offset_bytes;

    /** Identifier to assign to the next page-fill reservation. */
    uint32_t next_page_fill_lease_id;

    /** Current ownership state of each page slot. */
    InstructionBufferPageState_T page_states[INSTRUCTION_BUFFER_PAGE_COUNT];

    /** Valid logical instruction bytes held in each page slot. */
    uint32_t page_valid_bytes[INSTRUCTION_BUFFER_PAGE_COUNT];

    /** Logical instruction-stream offset represented by byte zero of each slot. */
    uint32_t page_stream_offsets_bytes[INSTRUCTION_BUFFER_PAGE_COUNT];

    /** The one NAND page-fill reservation that may currently be outstanding. */
    InstructionBufferPageFillReservation_T active_page_fill_reservation;

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

static InstructionBufferContext_T instruction_buffer_context;

/**-----------------------------------------------------------------------------
 *  Private (static) Function Prototypes
 *------------------------------------------------------------------------------
 */

/** Returns the RAM destination owned by the active page-fill reservation. */
static uint8_t* INSTRUCTION_BUFFER_GetActivePageFillData( void );

/** Validates an external fill lease against the authoritative reservation. */
static bool
INSTRUCTION_BUFFER_PageFillLeaseMatches( const InstructionBufferPageFillLease_T* lease );

/** Invalidates the active page-fill reservation without changing page data. */
static void INSTRUCTION_BUFFER_ClearPageFillReservation( void );

/** Returns the circular successor of a valid page-slot index. */
static uint8_t INSTRUCTION_BUFFER_NextPageIndex( uint8_t page_index );

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

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

static uint8_t INSTRUCTION_BUFFER_NextPageIndex( uint8_t page_index )
{
    return ( uint8_t )( ( page_index + 1U ) % INSTRUCTION_BUFFER_PAGE_COUNT );
}

/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */

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

    instruction_buffer_context.next_page_fill_lease_id = 1U;
    instruction_buffer_context.is_initialised          = true;

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

    instruction_buffer_context.is_read_prepared = true;

    return true;
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
