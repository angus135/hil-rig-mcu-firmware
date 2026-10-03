/******************************************************************************
 *  File:       external_flash_allocator.c
 *  Author:     Callum Rafferty
 *  Created:    05-May-2026
 *
 *  Description:
 *      Private wear-aware block allocator for external_flash.
 *
 *  Notes:
 *      The allocator owns volatile bad-block state, erase counts, active
 *      logical-to-physical maps, and low-wear block selection. It deliberately
 *      does not expose NAND pages, columns, or storage formats to callers.
 ******************************************************************************/

/**=============================================================================
 *  Includes
 *==============================================================================
 */

#include "external_flash_allocator.h"

#include "hw_nand.h"

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

/**=============================================================================
 *  Defines / Macros
 *==============================================================================
 */

/** Sentinel used for unused map entries. Fits in uint16_t. */
#define EXTERNAL_FLASH_ALLOCATOR_INVALID_BLOCK ( 0xFFFFU )

/** Number of physical blocks managed by external_flash, including metadata blocks. */
#define EXTERNAL_FLASH_ALLOCATOR_MANAGED_BLOCK_COUNT                                               \
    ( EXTERNAL_FLASH_METADATA_START_BLOCK + EXTERNAL_FLASH_METADATA_BLOCK_COUNT )

/** Number of 32-bit words required to hold one bit per managed NAND block. */
#define EXTERNAL_FLASH_ALLOCATOR_BITMAP_WORDS                                                      \
    ( ( EXTERNAL_FLASH_ALLOCATOR_MANAGED_BLOCK_COUNT + 31U ) / 32U )

/** Good blocks kept outside each active map so failed program blocks can be replaced. */
#define EXTERNAL_FLASH_ALLOCATOR_SPARE_BLOCK_COUNT ( 1U )

/**=============================================================================
 *  Typedefs / Enums / Structures
 *==============================================================================
 */

typedef struct
{
    uint32_t start_block;
    uint32_t block_count;
} ExternalFlashAllocatorPartitionConfig_T;

/**=============================================================================
 *  Private (static) Variables
 *==============================================================================
 */

static uint32_t external_flash_allocator_bad_blocks[EXTERNAL_FLASH_ALLOCATOR_BITMAP_WORDS] = { 0U };

static uint16_t
    external_flash_allocator_erase_counts[EXTERNAL_FLASH_ALLOCATOR_MANAGED_BLOCK_COUNT] = { 0U };

/**
 * Scratch selection state used while preparing a partition.
 *
 * Stored as a bitset to avoid placing large byte arrays on the stack or in RAM.
 */
static uint32_t external_flash_allocator_selected_blocks[EXTERNAL_FLASH_ALLOCATOR_BITMAP_WORDS] = {
    0U };

static uint32_t external_flash_allocator_bad_block_count = 0U;

static uint32_t external_flash_allocator_instruction_next_offset = 0U;
static uint32_t external_flash_allocator_result_next_offset      = 0U;

static uint16_t external_flash_allocator_instruction_map[EXTERNAL_FLASH_INSTRUCTION_BLOCK_COUNT] = {
    EXTERNAL_FLASH_ALLOCATOR_INVALID_BLOCK };

static uint16_t external_flash_allocator_result_map[EXTERNAL_FLASH_RESULT_BLOCK_COUNT] = {
    EXTERNAL_FLASH_ALLOCATOR_INVALID_BLOCK };

static uint32_t external_flash_allocator_instruction_map_count = 0U;
static uint32_t external_flash_allocator_result_map_count      = 0U;

static const ExternalFlashAllocatorPartitionConfig_T
    external_flash_allocator_instruction_partition = {
        EXTERNAL_FLASH_INSTRUCTION_START_BLOCK,
        EXTERNAL_FLASH_INSTRUCTION_BLOCK_COUNT,
};

static const ExternalFlashAllocatorPartitionConfig_T external_flash_allocator_result_partition = {
    EXTERNAL_FLASH_RESULT_START_BLOCK,
    EXTERNAL_FLASH_RESULT_BLOCK_COUNT,
};

static const ExternalFlashAllocatorPartitionConfig_T external_flash_allocator_metadata_partition = {
    EXTERNAL_FLASH_METADATA_START_BLOCK,
    EXTERNAL_FLASH_METADATA_BLOCK_COUNT,
};

/**=============================================================================
 *  Private (static) Function Prototypes
 *==============================================================================
 */

static inline bool EXTERNAL_FLASH_ALLOCATOR_BitmapGet( const uint32_t* bitmap, uint32_t index )
{
    return ( bitmap[index >> 5U] & ( ( uint32_t )1U << ( index & 31U ) ) ) != 0U;
}

static inline void EXTERNAL_FLASH_ALLOCATOR_BitmapSet( uint32_t* bitmap, uint32_t index )
{
    bitmap[index >> 5U] |= ( ( uint32_t )1U << ( index & 31U ) );
}

static inline void EXTERNAL_FLASH_ALLOCATOR_BitmapClear( uint32_t* bitmap, uint32_t index )
{
    bitmap[index >> 5U] &= ~( ( uint32_t )1U << ( index & 31U ) );
}

static inline void EXTERNAL_FLASH_ALLOCATOR_BitmapClearAll( uint32_t* bitmap, uint32_t word_count )
{
    for ( uint32_t i = 0U; i < word_count; i++ )
    {
        bitmap[i] = 0U;
    }
}

static ExternalFlashStatus_T EXTERNAL_FLASH_ALLOCATOR_MapNandStatus( HW_NAND_Status_T nand_status );

static const ExternalFlashAllocatorPartitionConfig_T*
EXTERNAL_FLASH_ALLOCATOR_GetPartitionConfig( ExternalFlashAllocatorPartition_T partition );

static uint16_t*
EXTERNAL_FLASH_ALLOCATOR_GetPartitionMap( ExternalFlashAllocatorPartition_T partition );

static uint32_t*
EXTERNAL_FLASH_ALLOCATOR_GetPartitionMapCount( ExternalFlashAllocatorPartition_T partition );

static uint32_t
EXTERNAL_FLASH_ALLOCATOR_GetPartitionMapCapacity( ExternalFlashAllocatorPartition_T partition );

static uint32_t*
EXTERNAL_FLASH_ALLOCATOR_GetPartitionNextOffset( ExternalFlashAllocatorPartition_T partition );

static bool EXTERNAL_FLASH_ALLOCATOR_IsPhysicalBlockBad( uint32_t block );

static void EXTERNAL_FLASH_ALLOCATOR_SetPhysicalBlockBad( uint32_t block );

static void EXTERNAL_FLASH_ALLOCATOR_ClearMap( uint16_t* block_map, uint32_t block_map_capacity );

static bool EXTERNAL_FLASH_ALLOCATOR_IsBlockInMap( const uint16_t* block_map,
                                                   uint32_t block_map_count, uint32_t block );

static bool EXTERNAL_FLASH_ALLOCATOR_SelectBlock( ExternalFlashAllocatorPartition_T partition,
                                                  const uint16_t*                   block_map,
                                                  uint32_t block_map_count, uint32_t* block );

/**=============================================================================
 *  Private Function Definitions
 *==============================================================================
 */

static ExternalFlashStatus_T EXTERNAL_FLASH_ALLOCATOR_MapNandStatus( HW_NAND_Status_T nand_status )
{
    switch ( nand_status )
    {
        case HW_NAND_STATUS_OK:
            return EXTERNAL_FLASH_STATUS_OK;

        case HW_NAND_STATUS_BUSY:
            return EXTERNAL_FLASH_STATUS_BUSY;

        case HW_NAND_STATUS_TIMEOUT:
            return EXTERNAL_FLASH_STATUS_TIMEOUT;

        case HW_NAND_STATUS_INVALID_ARG:
            return EXTERNAL_FLASH_STATUS_INVALID_ARG;

        case HW_NAND_STATUS_NOT_INITIALISED:
            return EXTERNAL_FLASH_STATUS_NOT_INITIALISED;

        case HW_NAND_STATUS_ECC_ERROR:
            return EXTERNAL_FLASH_STATUS_ECC_ERROR;

        case HW_NAND_STATUS_PROGRAM_FAIL:
            return EXTERNAL_FLASH_STATUS_PROGRAM_FAIL;

        case HW_NAND_STATUS_ERASE_FAIL:
            return EXTERNAL_FLASH_STATUS_ERASE_FAIL;

        case HW_NAND_STATUS_ERROR:
        case HW_NAND_STATUS_UNSUPPORTED_DEVICE:
        default:
            return EXTERNAL_FLASH_STATUS_ERROR;
    }
}

static const ExternalFlashAllocatorPartitionConfig_T*
EXTERNAL_FLASH_ALLOCATOR_GetPartitionConfig( ExternalFlashAllocatorPartition_T partition )
{
    switch ( partition )
    {
        case EXTERNAL_FLASH_ALLOCATOR_PARTITION_INSTRUCTION:
            return &external_flash_allocator_instruction_partition;

        case EXTERNAL_FLASH_ALLOCATOR_PARTITION_RESULT:
            return &external_flash_allocator_result_partition;

        case EXTERNAL_FLASH_ALLOCATOR_PARTITION_METADATA:
            return &external_flash_allocator_metadata_partition;

        default:
            return NULL;
    }
}

static uint16_t*
EXTERNAL_FLASH_ALLOCATOR_GetPartitionMap( ExternalFlashAllocatorPartition_T partition )
{
    switch ( partition )
    {
        case EXTERNAL_FLASH_ALLOCATOR_PARTITION_INSTRUCTION:
            return external_flash_allocator_instruction_map;

        case EXTERNAL_FLASH_ALLOCATOR_PARTITION_RESULT:
            return external_flash_allocator_result_map;

        case EXTERNAL_FLASH_ALLOCATOR_PARTITION_METADATA:
        default:
            return NULL;
    }
}

static uint32_t*
EXTERNAL_FLASH_ALLOCATOR_GetPartitionMapCount( ExternalFlashAllocatorPartition_T partition )
{
    switch ( partition )
    {
        case EXTERNAL_FLASH_ALLOCATOR_PARTITION_INSTRUCTION:
            return &external_flash_allocator_instruction_map_count;

        case EXTERNAL_FLASH_ALLOCATOR_PARTITION_RESULT:
            return &external_flash_allocator_result_map_count;

        case EXTERNAL_FLASH_ALLOCATOR_PARTITION_METADATA:
        default:
            return NULL;
    }
}

static uint32_t
EXTERNAL_FLASH_ALLOCATOR_GetPartitionMapCapacity( ExternalFlashAllocatorPartition_T partition )
{
    switch ( partition )
    {
        case EXTERNAL_FLASH_ALLOCATOR_PARTITION_INSTRUCTION:
            return EXTERNAL_FLASH_INSTRUCTION_BLOCK_COUNT;

        case EXTERNAL_FLASH_ALLOCATOR_PARTITION_RESULT:
            return EXTERNAL_FLASH_RESULT_BLOCK_COUNT;

        case EXTERNAL_FLASH_ALLOCATOR_PARTITION_METADATA:
        default:
            return 0U;
    }
}

static uint32_t*
EXTERNAL_FLASH_ALLOCATOR_GetPartitionNextOffset( ExternalFlashAllocatorPartition_T partition )
{
    switch ( partition )
    {
        case EXTERNAL_FLASH_ALLOCATOR_PARTITION_INSTRUCTION:
            return &external_flash_allocator_instruction_next_offset;

        case EXTERNAL_FLASH_ALLOCATOR_PARTITION_RESULT:
            return &external_flash_allocator_result_next_offset;

        case EXTERNAL_FLASH_ALLOCATOR_PARTITION_METADATA:
        default:
            return NULL;
    }
}

static bool EXTERNAL_FLASH_ALLOCATOR_IsPhysicalBlockBad( uint32_t block )
{
    if ( block >= EXTERNAL_FLASH_ALLOCATOR_MANAGED_BLOCK_COUNT )
    {
        return true;
    }

    return EXTERNAL_FLASH_ALLOCATOR_BitmapGet( external_flash_allocator_bad_blocks, block );
}

static void EXTERNAL_FLASH_ALLOCATOR_SetPhysicalBlockBad( uint32_t block )
{
    if ( block >= EXTERNAL_FLASH_ALLOCATOR_MANAGED_BLOCK_COUNT )
    {
        return;
    }

    if ( !EXTERNAL_FLASH_ALLOCATOR_BitmapGet( external_flash_allocator_bad_blocks, block ) )
    {
        EXTERNAL_FLASH_ALLOCATOR_BitmapSet( external_flash_allocator_bad_blocks, block );
        external_flash_allocator_bad_block_count++;
    }
}

static void EXTERNAL_FLASH_ALLOCATOR_ClearMap( uint16_t* block_map, uint32_t block_map_capacity )
{
    if ( block_map == NULL )
    {
        return;
    }

    for ( uint32_t i = 0U; i < block_map_capacity; i++ )
    {
        block_map[i] = EXTERNAL_FLASH_ALLOCATOR_INVALID_BLOCK;
    }
}

static bool EXTERNAL_FLASH_ALLOCATOR_IsBlockInMap( const uint16_t* block_map,
                                                   uint32_t block_map_count, uint32_t block )
{
    if ( block_map == NULL )
    {
        return false;
    }

    for ( uint32_t i = 0U; i < block_map_count; i++ )
    {
        if ( ( uint32_t )block_map[i] == block )
        {
            return true;
        }
    }

    return false;
}

static bool EXTERNAL_FLASH_ALLOCATOR_SelectBlock( ExternalFlashAllocatorPartition_T partition,
                                                  const uint16_t*                   block_map,
                                                  uint32_t block_map_count, uint32_t* block )
{
    const ExternalFlashAllocatorPartitionConfig_T* config =
        EXTERNAL_FLASH_ALLOCATOR_GetPartitionConfig( partition );
    if ( ( config == NULL ) || ( block == NULL ) || ( config->block_count == 0U ) )
    {
        return false;
    }

    uint32_t* next_offset  = EXTERNAL_FLASH_ALLOCATOR_GetPartitionNextOffset( partition );
    uint32_t  search_start = ( next_offset == NULL ) ? 0U : ( *next_offset % config->block_count );
    uint32_t  best_block   = EXTERNAL_FLASH_ALLOCATOR_INVALID_BLOCK;
    uint32_t  best_erase_count = UINT32_MAX;
    bool      found            = false;

    for ( uint32_t i = 0U; i < config->block_count; i++ )
    {
        uint32_t candidate_offset = ( search_start + i ) % config->block_count;
        uint32_t candidate_block  = config->start_block + candidate_offset;

        if ( EXTERNAL_FLASH_ALLOCATOR_IsPhysicalBlockBad( candidate_block )
             || EXTERNAL_FLASH_ALLOCATOR_IsBlockInMap( block_map, block_map_count,
                                                       candidate_block ) )
        {
            continue;
        }

        uint32_t candidate_erase_count =
            ( uint32_t )external_flash_allocator_erase_counts[candidate_block];
        if ( ( !found ) || ( candidate_erase_count < best_erase_count ) )
        {
            found            = true;
            best_block       = candidate_block;
            best_erase_count = candidate_erase_count;
        }
    }

    if ( found )
    {
        *block = best_block;
    }

    return found;
}

/**=============================================================================
 *  Public Function Definitions
 *==============================================================================
 */

void EXTERNAL_FLASH_ALLOCATOR_Reset( void )
{
    external_flash_allocator_bad_block_count         = 0U;
    external_flash_allocator_instruction_next_offset = 0U;
    external_flash_allocator_result_next_offset      = 0U;
    external_flash_allocator_instruction_map_count   = 0U;
    external_flash_allocator_result_map_count        = 0U;

    EXTERNAL_FLASH_ALLOCATOR_ClearMap( external_flash_allocator_instruction_map,
                                       EXTERNAL_FLASH_INSTRUCTION_BLOCK_COUNT );
    EXTERNAL_FLASH_ALLOCATOR_ClearMap( external_flash_allocator_result_map,
                                       EXTERNAL_FLASH_RESULT_BLOCK_COUNT );

    EXTERNAL_FLASH_ALLOCATOR_BitmapClearAll( external_flash_allocator_bad_blocks,
                                             EXTERNAL_FLASH_ALLOCATOR_BITMAP_WORDS );
    EXTERNAL_FLASH_ALLOCATOR_BitmapClearAll( external_flash_allocator_selected_blocks,
                                             EXTERNAL_FLASH_ALLOCATOR_BITMAP_WORDS );

    for ( uint32_t block = 0U; block < EXTERNAL_FLASH_ALLOCATOR_MANAGED_BLOCK_COUNT; block++ )
    {
        external_flash_allocator_erase_counts[block] = 0U;
    }
}

ExternalFlashStatus_T EXTERNAL_FLASH_ALLOCATOR_ScanBadBlocks( void )
{
    external_flash_allocator_bad_block_count = 0U;
    EXTERNAL_FLASH_ALLOCATOR_BitmapClearAll( external_flash_allocator_bad_blocks,
                                             EXTERNAL_FLASH_ALLOCATOR_BITMAP_WORDS );

    for ( uint32_t block = 0U; block < EXTERNAL_FLASH_ALLOCATOR_MANAGED_BLOCK_COUNT; block++ )
    {
        bool is_bad = false;

        ExternalFlashStatus_T status =
            EXTERNAL_FLASH_ALLOCATOR_MapNandStatus( HW_NAND_IsBlockBad( block, &is_bad ) );
        if ( status != EXTERNAL_FLASH_STATUS_OK )
        {
            return status;
        }

        if ( is_bad )
        {
            EXTERNAL_FLASH_ALLOCATOR_BitmapSet( external_flash_allocator_bad_blocks, block );
            external_flash_allocator_bad_block_count++;
        }
    }

    return EXTERNAL_FLASH_STATUS_OK;
}

uint32_t EXTERNAL_FLASH_ALLOCATOR_GetBadBlockCount( void )
{
    return external_flash_allocator_bad_block_count;
}

uint32_t EXTERNAL_FLASH_ALLOCATOR_GetCapacityBytes( ExternalFlashAllocatorPartition_T partition,
                                                    uint32_t block_data_size )
{
    const ExternalFlashAllocatorPartitionConfig_T* config =
        EXTERNAL_FLASH_ALLOCATOR_GetPartitionConfig( partition );
    if ( ( config == NULL ) || ( block_data_size == 0U ) )
    {
        return 0U;
    }

    uint32_t good_block_count = 0U;

    for ( uint32_t i = 0U; i < config->block_count; i++ )
    {
        uint32_t block = config->start_block + i;
        if ( !EXTERNAL_FLASH_ALLOCATOR_IsPhysicalBlockBad( block ) )
        {
            good_block_count++;
        }
    }

    if ( good_block_count <= EXTERNAL_FLASH_ALLOCATOR_SPARE_BLOCK_COUNT )
    {
        return 0U;
    }

    return ( good_block_count - EXTERNAL_FLASH_ALLOCATOR_SPARE_BLOCK_COUNT ) * block_data_size;
}

ExternalFlashStatus_T
EXTERNAL_FLASH_ALLOCATOR_PreparePartition( ExternalFlashAllocatorPartition_T partition,
                                           uint32_t                          required_length_bytes,
                                           uint32_t                          block_data_size )
{
    if ( ( required_length_bytes == 0U ) || ( block_data_size == 0U ) )
    {
        return EXTERNAL_FLASH_STATUS_INVALID_ARG;
    }

    const ExternalFlashAllocatorPartitionConfig_T* config =
        EXTERNAL_FLASH_ALLOCATOR_GetPartitionConfig( partition );
    uint32_t  required_blocks = ( required_length_bytes + block_data_size - 1U ) / block_data_size;
    uint32_t  map_capacity    = EXTERNAL_FLASH_ALLOCATOR_GetPartitionMapCapacity( partition );
    uint16_t* block_map       = EXTERNAL_FLASH_ALLOCATOR_GetPartitionMap( partition );
    uint32_t* block_count     = EXTERNAL_FLASH_ALLOCATOR_GetPartitionMapCount( partition );
    uint32_t* next_offset     = EXTERNAL_FLASH_ALLOCATOR_GetPartitionNextOffset( partition );

    if ( ( config == NULL ) || ( block_map == NULL ) || ( block_count == NULL )
         || ( required_blocks > map_capacity ) )
    {
        return EXTERNAL_FLASH_STATUS_STORAGE_FULL;
    }

    EXTERNAL_FLASH_ALLOCATOR_ClearMap( block_map, map_capacity );
    *block_count = 0U;

    uint32_t search_start = ( next_offset == NULL ) ? 0U : ( *next_offset % config->block_count );

    EXTERNAL_FLASH_ALLOCATOR_BitmapClearAll( external_flash_allocator_selected_blocks,
                                             EXTERNAL_FLASH_ALLOCATOR_BITMAP_WORDS );

    while ( *block_count < required_blocks )
    {
        bool     found_candidate    = false;
        uint32_t lowest_erase_count = UINT32_MAX;

        for ( uint32_t i = 0U; i < config->block_count; i++ )
        {
            uint32_t candidate_offset = ( search_start + i ) % config->block_count;
            uint32_t candidate_block  = config->start_block + candidate_offset;

            if ( EXTERNAL_FLASH_ALLOCATOR_IsPhysicalBlockBad( candidate_block )
                 || EXTERNAL_FLASH_ALLOCATOR_BitmapGet( external_flash_allocator_selected_blocks,
                                                        candidate_block ) )
            {
                continue;
            }

            uint32_t candidate_erase_count =
                ( uint32_t )external_flash_allocator_erase_counts[candidate_block];
            if ( ( !found_candidate ) || ( candidate_erase_count < lowest_erase_count ) )
            {
                found_candidate    = true;
                lowest_erase_count = candidate_erase_count;
            }
        }

        if ( !found_candidate )
        {
            return EXTERNAL_FLASH_STATUS_STORAGE_FULL;
        }

        bool selected_this_pass = false;

        for ( uint32_t i = 0U; ( i < config->block_count ) && ( *block_count < required_blocks );
              i++ )
        {
            uint32_t candidate_offset = ( search_start + i ) % config->block_count;
            uint32_t candidate_block  = config->start_block + candidate_offset;

            if ( EXTERNAL_FLASH_ALLOCATOR_IsPhysicalBlockBad( candidate_block )
                 || EXTERNAL_FLASH_ALLOCATOR_BitmapGet( external_flash_allocator_selected_blocks,
                                                        candidate_block )
                 || ( ( uint32_t )external_flash_allocator_erase_counts[candidate_block]
                      != lowest_erase_count ) )
            {
                continue;
            }

            EXTERNAL_FLASH_ALLOCATOR_BitmapSet( external_flash_allocator_selected_blocks,
                                                candidate_block );
            selected_this_pass = true;

            ExternalFlashStatus_T status =
                EXTERNAL_FLASH_ALLOCATOR_MapNandStatus( HW_NAND_BlockErase( candidate_block ) );
            if ( status == EXTERNAL_FLASH_STATUS_ERASE_FAIL )
            {
                ExternalFlashStatus_T retire_status =
                    EXTERNAL_FLASH_ALLOCATOR_RetirePhysicalBlock( candidate_block );
                if ( retire_status != EXTERNAL_FLASH_STATUS_OK )
                {
                    return retire_status;
                }

                continue;
            }

            if ( status != EXTERNAL_FLASH_STATUS_OK )
            {
                return status;
            }

            if ( external_flash_allocator_erase_counts[candidate_block] < UINT16_MAX )
            {
                external_flash_allocator_erase_counts[candidate_block]++;
            }
            block_map[*block_count] = ( uint16_t )candidate_block;
            ( *block_count )++;
        }

        if ( !selected_this_pass )
        {
            return EXTERNAL_FLASH_STATUS_STORAGE_FULL;
        }
    }

    return EXTERNAL_FLASH_STATUS_OK;
}

bool EXTERNAL_FLASH_ALLOCATOR_GetPhysicalBlock( ExternalFlashAllocatorPartition_T partition,
                                                uint32_t logical_block, uint32_t* physical_block )
{
    if ( physical_block == NULL )
    {
        return false;
    }

    uint16_t* block_map   = EXTERNAL_FLASH_ALLOCATOR_GetPartitionMap( partition );
    uint32_t* block_count = EXTERNAL_FLASH_ALLOCATOR_GetPartitionMapCount( partition );

    if ( ( block_map != NULL ) && ( block_count != NULL ) && ( logical_block < *block_count )
         && ( block_map[logical_block] != EXTERNAL_FLASH_ALLOCATOR_INVALID_BLOCK ) )
    {
        *physical_block = ( uint32_t )block_map[logical_block];
        return true;
    }

    const ExternalFlashAllocatorPartitionConfig_T* config =
        EXTERNAL_FLASH_ALLOCATOR_GetPartitionConfig( partition );
    if ( config == NULL )
    {
        return false;
    }

    uint32_t good_blocks_seen = 0U;

    for ( uint32_t i = 0U; i < config->block_count; i++ )
    {
        uint32_t candidate_block = config->start_block + i;

        if ( !EXTERNAL_FLASH_ALLOCATOR_IsPhysicalBlockBad( candidate_block ) )
        {
            if ( good_blocks_seen == logical_block )
            {
                *physical_block = candidate_block;
                return true;
            }

            good_blocks_seen++;
        }
    }

    return false;
}

ExternalFlashStatus_T
EXTERNAL_FLASH_ALLOCATOR_AllocateReplacementBlock( ExternalFlashAllocatorPartition_T partition,
                                                   uint32_t* replacement_block )
{
    uint16_t* block_map    = EXTERNAL_FLASH_ALLOCATOR_GetPartitionMap( partition );
    uint32_t* block_count  = EXTERNAL_FLASH_ALLOCATOR_GetPartitionMapCount( partition );
    uint32_t  map_capacity = EXTERNAL_FLASH_ALLOCATOR_GetPartitionMapCapacity( partition );

    if ( ( replacement_block == NULL ) || ( block_map == NULL ) || ( block_count == NULL ) )
    {
        return EXTERNAL_FLASH_STATUS_INVALID_ARG;
    }

    *replacement_block = ( uint32_t )EXTERNAL_FLASH_ALLOCATOR_INVALID_BLOCK;

    for ( uint32_t attempts = 0U; attempts < map_capacity; attempts++ )
    {
        uint32_t candidate_block = ( uint32_t )EXTERNAL_FLASH_ALLOCATOR_INVALID_BLOCK;
        if ( !EXTERNAL_FLASH_ALLOCATOR_SelectBlock( partition, block_map, *block_count,
                                                    &candidate_block ) )
        {
            return EXTERNAL_FLASH_STATUS_STORAGE_FULL;
        }

        ExternalFlashStatus_T status =
            EXTERNAL_FLASH_ALLOCATOR_MapNandStatus( HW_NAND_BlockErase( candidate_block ) );
        if ( status == EXTERNAL_FLASH_STATUS_ERASE_FAIL )
        {
            ExternalFlashStatus_T retire_status =
                EXTERNAL_FLASH_ALLOCATOR_RetirePhysicalBlock( candidate_block );
            if ( retire_status != EXTERNAL_FLASH_STATUS_OK )
            {
                return retire_status;
            }

            continue;
        }

        if ( status != EXTERNAL_FLASH_STATUS_OK )
        {
            return status;
        }

        if ( external_flash_allocator_erase_counts[candidate_block] < UINT16_MAX )
        {
            external_flash_allocator_erase_counts[candidate_block]++;
        }
        *replacement_block = candidate_block;

        return EXTERNAL_FLASH_STATUS_OK;
    }

    return EXTERNAL_FLASH_STATUS_STORAGE_FULL;
}

ExternalFlashStatus_T EXTERNAL_FLASH_ALLOCATOR_RetirePhysicalBlock( uint32_t block )
{
    if ( block >= EXTERNAL_FLASH_ALLOCATOR_MANAGED_BLOCK_COUNT )
    {
        return EXTERNAL_FLASH_STATUS_INVALID_ARG;
    }

    EXTERNAL_FLASH_ALLOCATOR_SetPhysicalBlockBad( block );

    return EXTERNAL_FLASH_ALLOCATOR_MapNandStatus( HW_NAND_MarkBlockBad( block ) );
}

ExternalFlashStatus_T EXTERNAL_FLASH_ALLOCATOR_CommitMappedBlockReplacement(
    ExternalFlashAllocatorPartition_T partition, uint32_t logical_block, uint32_t failed_block,
    uint32_t replacement_block )
{
    const ExternalFlashAllocatorPartitionConfig_T* config =
        EXTERNAL_FLASH_ALLOCATOR_GetPartitionConfig( partition );
    uint16_t* block_map   = EXTERNAL_FLASH_ALLOCATOR_GetPartitionMap( partition );
    uint32_t* block_count = EXTERNAL_FLASH_ALLOCATOR_GetPartitionMapCount( partition );

    if ( ( config == NULL ) || ( block_map == NULL ) || ( block_count == NULL )
         || ( logical_block >= *block_count )
         || ( ( uint32_t )block_map[logical_block] != failed_block )
         || ( replacement_block < config->start_block )
         || ( replacement_block >= ( config->start_block + config->block_count ) )
         || EXTERNAL_FLASH_ALLOCATOR_IsPhysicalBlockBad( replacement_block )
         || EXTERNAL_FLASH_ALLOCATOR_IsBlockInMap( block_map, *block_count, replacement_block ) )
    {
        return EXTERNAL_FLASH_STATUS_INVALID_ARG;
    }

    ExternalFlashStatus_T status = EXTERNAL_FLASH_ALLOCATOR_RetirePhysicalBlock( failed_block );
    if ( status != EXTERNAL_FLASH_STATUS_OK && status != EXTERNAL_FLASH_STATUS_PROGRAM_FAIL )
    {
        return status;
    }

    block_map[logical_block] = ( uint16_t )replacement_block;

    return EXTERNAL_FLASH_STATUS_OK;
}

void EXTERNAL_FLASH_ALLOCATOR_AdvanceCursor( ExternalFlashAllocatorPartition_T partition,
                                             uint32_t committed_length_bytes,
                                             uint32_t block_data_size )
{
    uint32_t* next_offset = EXTERNAL_FLASH_ALLOCATOR_GetPartitionNextOffset( partition );
    const ExternalFlashAllocatorPartitionConfig_T* config =
        EXTERNAL_FLASH_ALLOCATOR_GetPartitionConfig( partition );

    if ( ( next_offset == NULL ) || ( config == NULL ) || ( config->block_count == 0U )
         || ( block_data_size == 0U ) )
    {
        return;
    }

    if ( committed_length_bytes == 0U )
    {
        return;
    }

    uint32_t blocks_used = ( committed_length_bytes + block_data_size - 1U ) / block_data_size;

    *next_offset = ( *next_offset + blocks_used ) % config->block_count;
}
