/******************************************************************************
 *  File:       result_buffer.c
 *  Author:     Callum Rafferty
 *  Created:    25-Mar-2026
 *
 *  Description:
 *      Implementation for the Result Buffer module.
 *
 *  Notes:
 *      This is currently a placeholder; no result-buffer implementation is
 *      provided yet.
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */
#include "result_buffer.h"
#include "external_flash.h"
#include <stdint.h>
#include <stdbool.h>

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
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
    RESULT_BUFFER_PAGE_EMPTY = 0,
    RESULT_BUFFER_PAGE_FILLING,
    RESULT_BUFFER_PAGE_READY,
    RESULT_BUFFER_PAGE_WRITING
} ResultBufferPageState_T;

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

    ResultBufferPageState_T   page_states[RESULT_BUFFER_PAGE_COUNT];
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

/**-----------------------------------------------------------------------------
 *  Private (static) Function Prototypes
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */
