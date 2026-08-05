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
    bool     is_initialised;
    uint32_t page_size_bytes;
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

/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */
