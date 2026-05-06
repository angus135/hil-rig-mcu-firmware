/******************************************************************************
 *  File:       external_flash_allocator.h
 *  Author:     Callum Rafferty
 *  Created:    05-May-2026
 *
 *  Description:
 *      Private wear-aware block allocator for the external flash service.
 *
 *  Notes:
 *      This is not a public application API. It is owned by external_flash and
 *      exists only to keep allocation, bad-block, and erase-count policy out of
 *      external_flash.c.
 *
 *      The allocator keeps volatile erase counts and active logical-to-physical
 *      block maps for instruction and result storage. A metadata partition is
 *      reserved by external_flash.h for future persistent snapshots.
 ******************************************************************************/

#ifndef EXTERNAL_FLASH_ALLOCATOR_H
#define EXTERNAL_FLASH_ALLOCATOR_H

#ifdef __cplusplus
extern "C"
{
#endif

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include "external_flash.h"

#include <stdbool.h>
#include <stdint.h>

/**-----------------------------------------------------------------------------
 *  Public Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

typedef enum
{
    EXTERNAL_FLASH_ALLOCATOR_PARTITION_INSTRUCTION = 0,
    EXTERNAL_FLASH_ALLOCATOR_PARTITION_RESULT,
    EXTERNAL_FLASH_ALLOCATOR_PARTITION_METADATA
} ExternalFlashAllocatorPartition_T;

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

/**
 * @brief Resets volatile allocator state.
 */
void EXTERNAL_FLASH_ALLOCATOR_Reset( void );

/**
 * @brief Scans managed NAND blocks and caches bad-block state.
 *
 * @return EXTERNAL_FLASH_STATUS_OK on success, otherwise a mapped NAND error.
 */
ExternalFlashStatus_T EXTERNAL_FLASH_ALLOCATOR_ScanBadBlocks( void );

/**
 * @brief Returns the number of bad blocks in managed partitions.
 *
 * @return Factory and runtime bad block count.
 */
uint32_t EXTERNAL_FLASH_ALLOCATOR_GetBadBlockCount( void );

/**
 * @brief Returns usable logical capacity for a partition.
 *
 * @param partition       Partition to inspect.
 * @param block_data_size Main-area bytes per physical NAND block.
 *
 * @return Usable logical bytes after bad blocks and reserved spare blocks.
 */
uint32_t
EXTERNAL_FLASH_ALLOCATOR_GetCapacityBytes( ExternalFlashAllocatorPartition_T partition,
                                           uint32_t block_data_size );

/**
 * @brief Prepares an active logical-to-physical map for a partition image.
 *
 * @param partition             Partition to prepare.
 * @param required_length_bytes Logical byte length to reserve.
 * @param block_data_size       Main-area bytes per physical NAND block.
 *
 * @return EXTERNAL_FLASH_STATUS_OK on success, otherwise an error status.
 */
ExternalFlashStatus_T
EXTERNAL_FLASH_ALLOCATOR_PreparePartition( ExternalFlashAllocatorPartition_T partition,
                                           uint32_t required_length_bytes,
                                           uint32_t block_data_size );

/**
 * @brief Resolves a logical block to a physical NAND block.
 *
 * @param partition      Partition containing the logical block.
 * @param logical_block  Logical block index inside the active image.
 * @param physical_block Destination for the physical NAND block.
 *
 * @return true if the logical block can be resolved, otherwise false.
 */
bool EXTERNAL_FLASH_ALLOCATOR_GetPhysicalBlock( ExternalFlashAllocatorPartition_T partition,
                                                uint32_t logical_block,
                                                uint32_t* physical_block );

/**
 * @brief Replaces a failed mapped block with a newly erased spare block.
 *
 * @param partition     Partition containing the failed block.
 * @param logical_block Logical block whose physical block failed.
 * @param failed_block  Physical block that failed program.
 *
 * @return EXTERNAL_FLASH_STATUS_OK on success, otherwise an error status.
 */
ExternalFlashStatus_T
EXTERNAL_FLASH_ALLOCATOR_ReplaceMappedBlock( ExternalFlashAllocatorPartition_T partition,
                                             uint32_t logical_block, uint32_t failed_block );

/**
 * @brief Advances the partition tie-break cursor after committing an image.
 *
 * @param partition              Partition whose cursor should move.
 * @param committed_length_bytes Number of bytes committed in the image.
 * @param block_data_size        Main-area bytes per physical NAND block.
 */
void EXTERNAL_FLASH_ALLOCATOR_AdvanceCursor( ExternalFlashAllocatorPartition_T partition,
                                             uint32_t committed_length_bytes,
                                             uint32_t block_data_size );

#ifdef __cplusplus
}
#endif

#endif /* EXTERNAL_FLASH_ALLOCATOR_H */
