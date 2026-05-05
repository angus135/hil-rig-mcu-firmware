/******************************************************************************
 *  File:       external_flash.c
 *  Author:     Callum Rafferty
 *  Created:    25-Mar-2026
 *
 *  Description:
 *      External flash storage service implementation.
 *
 *  Notes:
 *      This module stores execution results as opaque bytes in a fixed NAND
 *      result partition and stores or reads instruction bytes in a fixed
 *      instruction partition. Physical NAND command sequencing remains in
 *      hw_nand.
 *
 *      The intended runtime path is:
 *      1. test_package_recieve receives a host test package and programs
 *         instruction bytes through EXTERNAL_FLASH_StartInstructionUpload,
 *         EXTERNAL_FLASH_WriteInstructionBytes or EXTERNAL_FLASH_WriteInstructionPage,
 *         and EXTERNAL_FLASH_FinishInstructionUpload.
 *      2. The flash manager calls EXTERNAL_FLASH_StartSession before execution.
 *         This erases the result partition so result writes never block on
 *         just in time erases during the execution run.
 *      3. flash_manager drains page sized execution result buffers and calls
 *         EXTERNAL_FLASH_WriteResultPage.
 *      4. flash_manager refills page sized instruction buffers by calling
 *         EXTERNAL_FLASH_ReadInstructionPage with logical byte offsets.
 *      5. result_transfer_manager calls EXTERNAL_FLASH_ReadResults to stream
 *         committed result bytes to the host interface.
 *
 *      Design boundaries:
 *      - execution_manager never calls this module directly.
 *      - flash_manager owns RAM instruction and result buffer lifetime and
 *        producer or consumer state.
 *      - flash_manager is the only normal runtime task responsible for flash
 *        access.
 *      - external_flash owns NAND partitioning, page level result programming,
 *        instruction upload programming, page level instruction reading, bad
 *        block skipping, erase at session start, committed instruction length,
 *        and committed result length.
 ******************************************************************************/

/**=============================================================================
 *  Includes
 *==============================================================================
 */

#include "external_flash.h"

#include "hw_nand.h"

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/**=============================================================================
 *  Defines / Macros
 *==============================================================================
 */

/** Maximum number of polls used while waiting for a NAND DMA transfer to complete. */
#define EXTERNAL_FLASH_DMA_POLL_LIMIT ( 1000000U )

/** Sentinel value used when no valid physical block has been selected. */
#define EXTERNAL_FLASH_INVALID_BLOCK ( 0xFFFFFFFFU )

/**
 * Maximum supported main page size for the private staging buffer.
 *
 * This value must be at least as large as the selected NAND device page size.
 * EXTERNAL_FLASH_Init checks the runtime geometry returned by hw_nand against
 * this buffer size.
 */
#define EXTERNAL_FLASH_MAX_PAGE_SIZE_BYTES ( 2048U )

/**=============================================================================
 *  Typedefs / Enums / Structures
 *==============================================================================
 */

/**
 * @brief Fixed physical block range assigned to one logical storage partition.
 */
typedef struct
{
    /** First physical block in the partition. */
    uint32_t start_block;

    /** Number of physical blocks assigned to the partition. */
    uint32_t block_count;
} ExternalFlashPartition_T;

/**=============================================================================
 *  Public (global) and Extern Variables
 *==============================================================================
 */

/**=============================================================================
 *  Private (static) Variables
 *==============================================================================
 */

/** Tracks whether the NAND device and partition scan completed successfully. */
static bool external_flash_initialised = false;

/** Tracks whether the current result partition has been erased for a new session. */
static bool external_flash_session_active = false;

/** Tracks whether a host package instruction upload is currently active. */
static bool external_flash_instruction_upload_active = false;

/** Geometry cached from hw_nand during initialisation. */
static HW_NAND_Geometry_T external_flash_geometry = { 0U, 0U, 0U, 0U };

/** Bad block bitmap indexed by physical block number. */
static bool external_flash_bad_block_table[EXTERNAL_FLASH_INSTRUCTION_BLOCK_COUNT
                                           + EXTERNAL_FLASH_RESULT_BLOCK_COUNT] = { false };

/** Number of bad blocks found across the configured instruction and result partitions. */
static uint32_t external_flash_bad_block_count = 0U;

/** Total instruction bytes expected from the active host package upload. */
static uint32_t external_flash_instruction_expected_length_bytes = 0U;

/**
 * Total logical instruction bytes accepted in the active upload.
 *
 * This includes bytes still staged in RAM by EXTERNAL_FLASH_WriteInstructionBytes.
 * It is used to choose the logical destination offset for the next instruction
 * page program.
 */
static uint32_t external_flash_instruction_length_bytes = 0U;

/**
 * Total logical instruction bytes successfully committed to NAND.
 *
 * Instruction reads are limited to this value so execution cannot read beyond
 * the uploaded package image.
 */
static uint32_t external_flash_committed_instruction_length_bytes = 0U;

/** Number of instruction bytes currently staged in the private instruction page buffer. */
static uint32_t external_flash_instruction_page_fill = 0U;

/**
 * Total logical result bytes accepted in the current volatile session.
 *
 * For EXTERNAL_FLASH_WriteResultBytes, this includes bytes still staged in RAM.
 * For EXTERNAL_FLASH_WriteResultPage, this advances only after the page write
 * succeeds.
 *
 * This value is used to choose the logical destination offset for the next
 * result page program.
 */
static uint32_t external_flash_result_length_bytes = 0U;

/**
 * Total logical result bytes successfully committed to NAND.
 *
 * Result transfer is limited to this value so the host never receives bytes
 * that are still only staged in RAM.
 */
static uint32_t external_flash_committed_result_length_bytes = 0U;

/** Number of bytes currently staged in the private result page buffer. */
static uint32_t external_flash_result_page_fill = 0U;

/**
 * Page sized staging buffer used by the byte write path and by the final
 * partial page path in the page scoped write API.
 *
 * Full page calls to EXTERNAL_FLASH_WriteResultPage do not use this buffer.
 * Those calls DMA directly from the caller supplied flash manager buffer.
 */
static uint8_t external_flash_result_page_buffer[EXTERNAL_FLASH_MAX_PAGE_SIZE_BYTES] = { 0xFFU };

/**
 * Page sized staging buffer used by the instruction byte upload path and by
 * final partial page writes through EXTERNAL_FLASH_WriteInstructionPage.
 */
static uint8_t external_flash_instruction_page_buffer[EXTERNAL_FLASH_MAX_PAGE_SIZE_BYTES] = {
    0xFFU
};

/**
 * Fixed partition used for uploaded execution instructions.
 */
static const ExternalFlashPartition_T external_flash_instruction_partition = {
    EXTERNAL_FLASH_INSTRUCTION_START_BLOCK,
    EXTERNAL_FLASH_INSTRUCTION_BLOCK_COUNT,
};

/**
 * Fixed partition used for volatile execution result storage.
 *
 * The partition is erased at session start and is append only while the test is
 * running.
 */
static const ExternalFlashPartition_T external_flash_result_partition = {
    EXTERNAL_FLASH_RESULT_START_BLOCK,
    EXTERNAL_FLASH_RESULT_BLOCK_COUNT,
};

/**=============================================================================
 *  Private (static) Function Prototypes
 *==============================================================================
 */

/** Maps physical NAND status values into the public external_flash status enum. */
static ExternalFlashStatus_T EXTERNAL_FLASH_MapNandStatus( HW_NAND_Status_T nand_status );

/**
 * Waits for the active NAND DMA transfer to complete.
 *
 * This helper is used by both page scoped result writes and page scoped
 * instruction reads. It keeps the current public APIs synchronous while still
 * using DMA internally.
 */
static ExternalFlashStatus_T EXTERNAL_FLASH_WaitDmaTransferComplete( void );

/** Returns the number of main area bytes in one physical block. */
static uint32_t EXTERNAL_FLASH_BlockDataSizeBytes( void );

/** Checks whether a physical block index is marked bad in the cached bad block table. */
static bool EXTERNAL_FLASH_IsPhysicalBlockBad( uint32_t block );

/**
 * Converts a partition local logical block index to a usable physical block.
 *
 * Logical block indexes count only good blocks. This is the central bad block
 * skipping decision for instruction reads, result writes, and result readback.
 */
static bool
EXTERNAL_FLASH_GetPhysicalBlockForLogicalBlock( const ExternalFlashPartition_T* partition,
                                                uint32_t logical_block, uint32_t* physical_block );

/**
 * Converts a partition byte offset to a usable physical NAND page and column.
 *
 * Public read and write APIs use logical byte offsets. This helper hides NAND
 * pages, blocks, and bad block gaps from flash_manager and transfer managers.
 */
static bool EXTERNAL_FLASH_GetPhysicalAddress( const ExternalFlashPartition_T* partition,
                                               uint32_t offset, uint32_t* page, uint16_t* column );

/** Returns the logical byte capacity of a partition after bad block removal. */
static uint32_t
EXTERNAL_FLASH_GetPartitionCapacityBytes( const ExternalFlashPartition_T* partition );

/** Scans both configured partitions and caches factory bad block markers. */
static ExternalFlashStatus_T EXTERNAL_FLASH_ScanBadBlocks( void );

/** Erases all good blocks in a partition. */
static ExternalFlashStatus_T
EXTERNAL_FLASH_ErasePartition( const ExternalFlashPartition_T* partition );

/** Erases all good blocks in the instruction partition. */
static ExternalFlashStatus_T EXTERNAL_FLASH_EraseInstructionPartition( void );

/** Erases all good blocks in the result partition. */
static ExternalFlashStatus_T EXTERNAL_FLASH_EraseResultPartition( void );

/**
 * Clears the private result page staging buffer to erased NAND state.
 *
 * The buffer is filled with 0xFF so that partial final pages are padded as
 * erased NAND bytes before programming.
 */
static void EXTERNAL_FLASH_ClearResultPageBuffer( void );

/**
 * Clears the private instruction page staging buffer to erased NAND state.
 *
 * The buffer is filled with 0xFF so that partial final pages are padded as
 * erased NAND bytes before programming.
 */
static void EXTERNAL_FLASH_ClearInstructionPageBuffer( void );

/**
 * Programs one physical NAND page into a partition.
 *
 * page_buffer must point to a full NAND page worth of data. bytes_to_commit is
 * the number of valid logical bytes represented by that physical page.
 *
 * The NAND program operation still writes one full physical page.
 */
static ExternalFlashStatus_T
EXTERNAL_FLASH_ProgramPartitionPageBuffer( const ExternalFlashPartition_T* partition,
                                           const uint8_t* page_buffer,
                                           uint32_t page_start_offset, uint32_t bytes_to_commit,
                                           uint32_t* committed_length_bytes );

/** Programs one physical NAND page into the instruction partition. */
static ExternalFlashStatus_T
EXTERNAL_FLASH_ProgramInstructionPageBuffer( const uint8_t* page_buffer,
                                             uint32_t page_start_offset,
                                             uint32_t bytes_to_commit );

/** Programs one physical NAND page into the result partition. */
static ExternalFlashStatus_T EXTERNAL_FLASH_ProgramResultPageBuffer( const uint8_t* page_buffer,
                                                                     uint32_t page_start_offset,
                                                                     uint32_t bytes_to_commit );

/** Programs the currently staged instruction page into the next instruction partition page. */
static ExternalFlashStatus_T EXTERNAL_FLASH_ProgramStagedInstructionPage( void );

/**
 * Programs the currently staged result page into the next result partition page.
 *
 * Full staged pages are programmed during EXTERNAL_FLASH_WriteResultBytes.
 * Partial staged pages are programmed by EXTERNAL_FLASH_FlushResults before host
 * result transfer.
 */
static ExternalFlashStatus_T EXTERNAL_FLASH_ProgramStagedResultPage( void );

/**
 * Reads one page or partial page from a partition using NAND DMA internally.
 *
 * offset must be aligned to the NAND page size. length must be no larger than
 * the NAND page size. The destination buffer must remain valid and writable
 * until this function returns.
 */
static ExternalFlashStatus_T
EXTERNAL_FLASH_ReadPartitionPageDma( const ExternalFlashPartition_T* partition,
                                     uint32_t readable_length, uint32_t offset, uint8_t* data,
                                     uint32_t length );

/**
 * Reads opaque bytes from a partition by logical byte offset.
 *
 * This helper handles reads that cross physical page boundaries and relies on
 * EXTERNAL_FLASH_GetPhysicalAddress to skip bad block gaps.
 */
static ExternalFlashStatus_T
EXTERNAL_FLASH_ReadFromPartition( const ExternalFlashPartition_T* partition,
                                  uint32_t readable_length, uint32_t offset, uint8_t* data,
                                  uint32_t length );

/**=============================================================================
 *  Private Function Definitions
 *==============================================================================
 */

/**
 * @brief Maps an hw_nand status into the corresponding external_flash status.
 *
 * @param nand_status Status returned by the lower level NAND driver.
 *
 * @return Equivalent public ExternalFlashStatus_T value.
 */
static ExternalFlashStatus_T EXTERNAL_FLASH_MapNandStatus( HW_NAND_Status_T nand_status )
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

/**
 * @brief Polls the NAND transfer state until the active DMA operation completes.
 *
 * @return EXTERNAL_FLASH_STATUS_OK when the transfer completes, otherwise
 *         EXTERNAL_FLASH_STATUS_TIMEOUT.
 *
 * @note This is intentionally a bounded polling helper. A future RTOS aware
 *       version may replace this with a notification or semaphore wait.
 */
static ExternalFlashStatus_T EXTERNAL_FLASH_WaitDmaTransferComplete( void )
{
    for ( uint32_t poll_count = 0U; poll_count < EXTERNAL_FLASH_DMA_POLL_LIMIT; poll_count++ )
    {
        if ( HW_NAND_IsTransferComplete() )
        {
            return EXTERNAL_FLASH_STATUS_OK;
        }
    }

    return EXTERNAL_FLASH_STATUS_TIMEOUT;
}

/**
 * @brief Returns the amount of main area data stored in one physical block.
 *
 * @return Number of usable main area bytes per block.
 */
static uint32_t EXTERNAL_FLASH_BlockDataSizeBytes( void )
{
    return external_flash_geometry.page_size_bytes * external_flash_geometry.pages_per_block;
}

/**
 * @brief Checks whether a physical block is unusable.
 *
 * @param block Physical NAND block index.
 *
 * @return true if the block is outside the managed partitions or is marked bad,
 *         otherwise false.
 */
static bool EXTERNAL_FLASH_IsPhysicalBlockBad( uint32_t block )
{
    if ( block >= ( EXTERNAL_FLASH_INSTRUCTION_BLOCK_COUNT + EXTERNAL_FLASH_RESULT_BLOCK_COUNT ) )
    {
        return true;
    }

    return external_flash_bad_block_table[block];
}

/**
 * @brief Maps a logical block index inside a partition to a good physical block.
 *
 * @param partition      Partition to search.
 * @param logical_block  Partition local logical block index, counting only good blocks.
 * @param physical_block Destination for the selected physical block.
 *
 * @return true if a matching good physical block was found, otherwise false.
 */
static bool
EXTERNAL_FLASH_GetPhysicalBlockForLogicalBlock( const ExternalFlashPartition_T* partition,
                                                uint32_t logical_block, uint32_t* physical_block )
{
    if ( ( partition == NULL ) || ( physical_block == NULL ) )
    {
        return false;
    }

    uint32_t good_blocks_seen = 0U;

    for ( uint32_t i = 0U; i < partition->block_count; i++ )
    {
        uint32_t candidate_block = partition->start_block + i;

        if ( !EXTERNAL_FLASH_IsPhysicalBlockBad( candidate_block ) )
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

/**
 * @brief Converts a logical partition byte offset to a physical NAND address.
 *
 * @param partition Partition containing the logical offset.
 * @param offset    Logical byte offset within the partition.
 * @param page      Destination for the physical NAND page row address.
 * @param column    Destination for the cache column address.
 *
 * @return true if the address can be mapped to a good physical block, otherwise false.
 */
static bool EXTERNAL_FLASH_GetPhysicalAddress( const ExternalFlashPartition_T* partition,
                                               uint32_t offset, uint32_t* page, uint16_t* column )
{
    if ( ( partition == NULL ) || ( page == NULL ) || ( column == NULL )
         || ( external_flash_geometry.page_size_bytes == 0U ) )
    {
        return false;
    }

    uint32_t block_data_size = EXTERNAL_FLASH_BlockDataSizeBytes();
    if ( block_data_size == 0U )
    {
        return false;
    }

    uint32_t logical_block  = offset / block_data_size;
    uint32_t block_offset   = offset % block_data_size;
    uint32_t page_in_block  = block_offset / external_flash_geometry.page_size_bytes;
    uint32_t page_offset    = block_offset % external_flash_geometry.page_size_bytes;
    uint32_t physical_block = EXTERNAL_FLASH_INVALID_BLOCK;

    if ( !EXTERNAL_FLASH_GetPhysicalBlockForLogicalBlock( partition, logical_block,
                                                          &physical_block ) )
    {
        return false;
    }

    *page   = ( physical_block * external_flash_geometry.pages_per_block ) + page_in_block;
    *column = ( uint16_t )page_offset;

    return true;
}

/**
 * @brief Computes the usable logical capacity of a partition.
 *
 * @param partition Partition whose good block capacity should be calculated.
 *
 * @return Usable logical capacity in bytes after excluding bad blocks.
 */
static uint32_t
EXTERNAL_FLASH_GetPartitionCapacityBytes( const ExternalFlashPartition_T* partition )
{
    if ( partition == NULL )
    {
        return 0U;
    }

    uint32_t good_block_count = 0U;

    for ( uint32_t i = 0U; i < partition->block_count; i++ )
    {
        uint32_t block = partition->start_block + i;

        if ( !EXTERNAL_FLASH_IsPhysicalBlockBad( block ) )
        {
            good_block_count++;
        }
    }

    return good_block_count * EXTERNAL_FLASH_BlockDataSizeBytes();
}

/**
 * @brief Scans the configured instruction and result partitions for bad blocks.
 *
 * @return EXTERNAL_FLASH_STATUS_OK on success, otherwise a mapped NAND error.
 *
 * @note Factory bad block markers are read through hw_nand. The cached table is
 *       then used for logical to physical address translation.
 */
static ExternalFlashStatus_T EXTERNAL_FLASH_ScanBadBlocks( void )
{
    external_flash_bad_block_count = 0U;

    for ( uint32_t block = 0U;
          block < ( EXTERNAL_FLASH_INSTRUCTION_BLOCK_COUNT + EXTERNAL_FLASH_RESULT_BLOCK_COUNT );
          block++ )
    {
        bool is_bad = false;

        ExternalFlashStatus_T status =
            EXTERNAL_FLASH_MapNandStatus( HW_NAND_IsBlockBad( block, &is_bad ) );
        if ( status != EXTERNAL_FLASH_STATUS_OK )
        {
            return status;
        }

        external_flash_bad_block_table[block] = is_bad;
        if ( is_bad )
        {
            external_flash_bad_block_count++;
        }
    }

    return EXTERNAL_FLASH_STATUS_OK;
}

/**
 * @brief Erases all good blocks in a partition.
 *
 * @param partition Partition whose good physical blocks should be erased.
 *
 * @return EXTERNAL_FLASH_STATUS_OK on success, otherwise an error status.
 *
 * @note Blocks that fail erase are retired in RAM and marked bad through hw_nand.
 */
static ExternalFlashStatus_T
EXTERNAL_FLASH_ErasePartition( const ExternalFlashPartition_T* partition )
{
    if ( partition == NULL )
    {
        return EXTERNAL_FLASH_STATUS_INVALID_ARG;
    }

    for ( uint32_t i = 0U; i < partition->block_count; i++ )
    {
        uint32_t block = partition->start_block + i;

        if ( !EXTERNAL_FLASH_IsPhysicalBlockBad( block ) )
        {
            ExternalFlashStatus_T status =
                EXTERNAL_FLASH_MapNandStatus( HW_NAND_BlockErase( block ) );

            if ( status == EXTERNAL_FLASH_STATUS_ERASE_FAIL )
            {
                external_flash_bad_block_table[block] = true;
                external_flash_bad_block_count++;
                ( void )HW_NAND_MarkBlockBad( block );
            }
            else if ( status != EXTERNAL_FLASH_STATUS_OK )
            {
                return status;
            }
        }
    }

    return EXTERNAL_FLASH_STATUS_OK;
}

/**
 * @brief Erases all good blocks in the instruction partition.
 */
static ExternalFlashStatus_T EXTERNAL_FLASH_EraseInstructionPartition( void )
{
    return EXTERNAL_FLASH_ErasePartition( &external_flash_instruction_partition );
}

/**
 * @brief Erases all good blocks in the result partition.
 */
static ExternalFlashStatus_T EXTERNAL_FLASH_EraseResultPartition( void )
{
    return EXTERNAL_FLASH_ErasePartition( &external_flash_result_partition );
}

/**
 * @brief Resets the private result page staging buffer to erased NAND state.
 */
static void EXTERNAL_FLASH_ClearResultPageBuffer( void )
{
    ( void )memset( external_flash_result_page_buffer, 0xFF,
                    external_flash_geometry.page_size_bytes );
    external_flash_result_page_fill = 0U;
}

/**
 * @brief Resets the private instruction page staging buffer to erased NAND state.
 */
static void EXTERNAL_FLASH_ClearInstructionPageBuffer( void )
{
    ( void )memset( external_flash_instruction_page_buffer, 0xFF,
                    external_flash_geometry.page_size_bytes );
    external_flash_instruction_page_fill = 0U;
}

/**
 * @brief Programs one physical NAND page in a logical partition.
 *
 * @param partition                Destination partition.
 * @param page_buffer              Full physical page buffer to program.
 * @param page_start_offset        Logical byte offset for the start of this page.
 * @param bytes_to_commit          Number of valid logical bytes represented by the page.
 * @param committed_length_bytes   Destination committed byte counter to advance.
 *
 * @return EXTERNAL_FLASH_STATUS_OK on success, otherwise an error status.
 *
 * @note The NAND receives one full physical page from page_buffer, but committed
 *       length is advanced only by bytes_to_commit.
 * @note If program fails, the failed block is retired and the same logical page
 *       is retried on the next good physical block in the same partition.
 */
static ExternalFlashStatus_T
EXTERNAL_FLASH_ProgramPartitionPageBuffer( const ExternalFlashPartition_T* partition,
                                           const uint8_t* page_buffer,
                                           uint32_t page_start_offset, uint32_t bytes_to_commit,
                                           uint32_t* committed_length_bytes )
{
    if ( ( partition == NULL ) || ( page_buffer == NULL ) || ( committed_length_bytes == NULL ) )
    {
        return EXTERNAL_FLASH_STATUS_INVALID_ARG;
    }

    if ( bytes_to_commit == 0U )
    {
        return EXTERNAL_FLASH_STATUS_OK;
    }

    if ( bytes_to_commit > external_flash_geometry.page_size_bytes )
    {
        return EXTERNAL_FLASH_STATUS_INVALID_ARG;
    }

    for ( uint32_t attempts = 0U; attempts < partition->block_count; attempts++ )
    {
        uint32_t page   = 0U;
        uint16_t column = 0U;

        if ( !EXTERNAL_FLASH_GetPhysicalAddress( partition, page_start_offset, &page, &column )
             || ( column != 0U ) )
        {
            return EXTERNAL_FLASH_STATUS_STORAGE_FULL;
        }

        ExternalFlashStatus_T status = EXTERNAL_FLASH_MapNandStatus(
            HW_NAND_ProgramLoadDma( 0U, page_buffer, external_flash_geometry.page_size_bytes ) );
        if ( status != EXTERNAL_FLASH_STATUS_OK )
        {
            return status;
        }

        status = EXTERNAL_FLASH_WaitDmaTransferComplete();
        if ( status != EXTERNAL_FLASH_STATUS_OK )
        {
            return status;
        }

        status = EXTERNAL_FLASH_MapNandStatus( HW_NAND_StartProgramExecute( page ) );
        if ( status != EXTERNAL_FLASH_STATUS_OK )
        {
            return status;
        }

        status = EXTERNAL_FLASH_MapNandStatus( HW_NAND_WaitProgramComplete( 1U ) );
        if ( status == EXTERNAL_FLASH_STATUS_PROGRAM_FAIL )
        {
            uint32_t failed_block = page / external_flash_geometry.pages_per_block;

            if ( !external_flash_bad_block_table[failed_block] )
            {
                external_flash_bad_block_table[failed_block] = true;
                external_flash_bad_block_count++;
            }

            ( void )HW_NAND_MarkBlockBad( failed_block );
        }
        else if ( status != EXTERNAL_FLASH_STATUS_OK )
        {
            return status;
        }
        else
        {
            *committed_length_bytes += bytes_to_commit;
            return EXTERNAL_FLASH_STATUS_OK;
        }
    }

    return EXTERNAL_FLASH_STATUS_STORAGE_FULL;
}

/**
 * @brief Programs one physical NAND instruction page.
 */
static ExternalFlashStatus_T
EXTERNAL_FLASH_ProgramInstructionPageBuffer( const uint8_t* page_buffer,
                                             uint32_t page_start_offset,
                                             uint32_t bytes_to_commit )
{
    return EXTERNAL_FLASH_ProgramPartitionPageBuffer(
        &external_flash_instruction_partition, page_buffer, page_start_offset, bytes_to_commit,
        &external_flash_committed_instruction_length_bytes );
}

/**
 * @brief Programs one physical NAND result page.
 */
static ExternalFlashStatus_T EXTERNAL_FLASH_ProgramResultPageBuffer( const uint8_t* page_buffer,
                                                                     uint32_t page_start_offset,
                                                                     uint32_t bytes_to_commit )
{
    return EXTERNAL_FLASH_ProgramPartitionPageBuffer(
        &external_flash_result_partition, page_buffer, page_start_offset, bytes_to_commit,
        &external_flash_committed_result_length_bytes );
}

/**
 * @brief Programs the current private staged instruction page.
 *
 * @return EXTERNAL_FLASH_STATUS_OK on success, otherwise an error status.
 */
static ExternalFlashStatus_T EXTERNAL_FLASH_ProgramStagedInstructionPage( void )
{
    if ( external_flash_instruction_page_fill == 0U )
    {
        return EXTERNAL_FLASH_STATUS_OK;
    }

    uint32_t page_start_offset =
        external_flash_instruction_length_bytes - external_flash_instruction_page_fill;
    uint32_t bytes_to_commit = external_flash_instruction_page_fill;

    ExternalFlashStatus_T status = EXTERNAL_FLASH_ProgramInstructionPageBuffer(
        external_flash_instruction_page_buffer, page_start_offset, bytes_to_commit );

    if ( status == EXTERNAL_FLASH_STATUS_OK )
    {
        EXTERNAL_FLASH_ClearInstructionPageBuffer();
    }

    return status;
}

/**
 * @brief Programs the current private staged result page.
 *
 * @return EXTERNAL_FLASH_STATUS_OK on success, otherwise an error status.
 *
 * @note This helper is used only by the byte stream write path and its flush
 *       operation. Page scoped writes bypass the private staging buffer for full
 *       pages.
 */
static ExternalFlashStatus_T EXTERNAL_FLASH_ProgramStagedResultPage( void )
{
    if ( external_flash_result_page_fill == 0U )
    {
        return EXTERNAL_FLASH_STATUS_OK;
    }

    uint32_t page_start_offset =
        external_flash_result_length_bytes - external_flash_result_page_fill;
    uint32_t bytes_to_commit = external_flash_result_page_fill;

    ExternalFlashStatus_T status = EXTERNAL_FLASH_ProgramResultPageBuffer(
        external_flash_result_page_buffer, page_start_offset, bytes_to_commit );

    if ( status == EXTERNAL_FLASH_STATUS_OK )
    {
        EXTERNAL_FLASH_ClearResultPageBuffer();
    }

    return status;
}

/**
 * @brief Reads one logical page or partial logical page from a partition using DMA.
 *
 * @param partition       Partition to read from.
 * @param readable_length Maximum readable logical byte length in the partition.
 * @param offset          Logical byte offset. Must be page aligned.
 * @param data            Destination buffer.
 * @param length          Number of bytes to read. Must be no larger than one page.
 *
 * @return EXTERNAL_FLASH_STATUS_OK on success, otherwise an error status.
 *
 * @note The destination buffer is supplied by the caller. It must remain valid
 *       and writable until this function returns.
 */
static ExternalFlashStatus_T
EXTERNAL_FLASH_ReadPartitionPageDma( const ExternalFlashPartition_T* partition,
                                     uint32_t readable_length, uint32_t offset, uint8_t* data,
                                     uint32_t length )
{
    if ( !external_flash_initialised )
    {
        return EXTERNAL_FLASH_STATUS_NOT_INITIALISED;
    }

    if ( ( partition == NULL ) || ( data == NULL ) || ( length == 0U ) )
    {
        return EXTERNAL_FLASH_STATUS_INVALID_ARG;
    }

    if ( external_flash_geometry.page_size_bytes == 0U )
    {
        return EXTERNAL_FLASH_STATUS_ERROR;
    }

    if ( length > external_flash_geometry.page_size_bytes )
    {
        return EXTERNAL_FLASH_STATUS_INVALID_ARG;
    }

    if ( ( offset % external_flash_geometry.page_size_bytes ) != 0U )
    {
        return EXTERNAL_FLASH_STATUS_INVALID_ARG;
    }

    if ( ( offset > readable_length ) || ( length > ( readable_length - offset ) ) )
    {
        return EXTERNAL_FLASH_STATUS_INVALID_ARG;
    }

    uint32_t page   = 0U;
    uint16_t column = 0U;

    if ( !EXTERNAL_FLASH_GetPhysicalAddress( partition, offset, &page, &column )
         || ( column != 0U ) )
    {
        return EXTERNAL_FLASH_STATUS_INVALID_ARG;
    }

    ExternalFlashStatus_T status =
        EXTERNAL_FLASH_MapNandStatus( HW_NAND_ReadPageDma( page, column, data, length ) );
    if ( status != EXTERNAL_FLASH_STATUS_OK )
    {
        return status;
    }

    return EXTERNAL_FLASH_WaitDmaTransferComplete();
}

/**
 * @brief Reads an arbitrary byte range from a partition using blocking NAND reads.
 *
 * @param partition       Partition to read from.
 * @param readable_length Maximum readable logical byte length in the partition.
 * @param offset          Logical byte offset.
 * @param data            Destination buffer.
 * @param length          Number of bytes to read.
 *
 * @return EXTERNAL_FLASH_STATUS_OK on success, otherwise an error status.
 *
 * @note This is the compatibility path for arbitrary byte reads. The page scoped
 *       instruction queue path should use EXTERNAL_FLASH_ReadPartitionPageDma.
 */
static ExternalFlashStatus_T
EXTERNAL_FLASH_ReadFromPartition( const ExternalFlashPartition_T* partition,
                                  uint32_t readable_length, uint32_t offset, uint8_t* data,
                                  uint32_t length )
{
    if ( !external_flash_initialised )
    {
        return EXTERNAL_FLASH_STATUS_NOT_INITIALISED;
    }

    if ( ( partition == NULL ) || ( data == NULL ) || ( length == 0U ) )
    {
        return EXTERNAL_FLASH_STATUS_INVALID_ARG;
    }

    if ( ( offset > readable_length ) || ( length > ( readable_length - offset ) ) )
    {
        return EXTERNAL_FLASH_STATUS_INVALID_ARG;
    }

    uint32_t bytes_read = 0U;

    while ( bytes_read < length )
    {
        uint32_t page   = 0U;
        uint16_t column = 0U;

        if ( !EXTERNAL_FLASH_GetPhysicalAddress( partition, offset + bytes_read, &page, &column ) )
        {
            return EXTERNAL_FLASH_STATUS_INVALID_ARG;
        }

        uint32_t page_remaining  = external_flash_geometry.page_size_bytes - column;
        uint32_t remaining       = length - bytes_read;
        uint32_t bytes_this_page = ( remaining < page_remaining ) ? remaining : page_remaining;

        ExternalFlashStatus_T status = EXTERNAL_FLASH_MapNandStatus(
            HW_NAND_ReadPageBlocking( page, column, &data[bytes_read], bytes_this_page ) );
        if ( status != EXTERNAL_FLASH_STATUS_OK )
        {
            return status;
        }

        bytes_read += bytes_this_page;
    }

    return EXTERNAL_FLASH_STATUS_OK;
}

/**=============================================================================
 *  Public Function Definitions
 *==============================================================================
 */

/**
 * @brief Initialises the external flash service, NAND device, geometry cache, and bad block table.
 */
ExternalFlashStatus_T EXTERNAL_FLASH_Init( void )
{
    external_flash_initialised                   = false;
    external_flash_session_active                = false;
    external_flash_result_length_bytes           = 0U;
    external_flash_committed_result_length_bytes = 0U;
    external_flash_result_page_fill              = 0U;
    external_flash_bad_block_count               = 0U;

    ExternalFlashStatus_T status = EXTERNAL_FLASH_MapNandStatus( HW_NAND_Init() );
    if ( status != EXTERNAL_FLASH_STATUS_OK )
    {
        return status;
    }

    status = EXTERNAL_FLASH_MapNandStatus( HW_NAND_GetGeometry( &external_flash_geometry ) );
    if ( status != EXTERNAL_FLASH_STATUS_OK )
    {
        return status;
    }

    if ( external_flash_geometry.page_size_bytes > sizeof( external_flash_result_page_buffer ) )
    {
        return EXTERNAL_FLASH_STATUS_ERROR;
    }

    if ( ( EXTERNAL_FLASH_INSTRUCTION_BLOCK_COUNT + EXTERNAL_FLASH_RESULT_BLOCK_COUNT )
         > external_flash_geometry.block_count )
    {
        return EXTERNAL_FLASH_STATUS_ERROR;
    }

    status = EXTERNAL_FLASH_ScanBadBlocks();
    if ( status != EXTERNAL_FLASH_STATUS_OK )
    {
        return status;
    }

    EXTERNAL_FLASH_ClearResultPageBuffer();
    external_flash_initialised = true;

    return EXTERNAL_FLASH_STATUS_OK;
}

/**
 * @brief Reports partition capacities, committed result length, and bad block count.
 */
ExternalFlashStatus_T EXTERNAL_FLASH_GetInfo( ExternalFlashInfo_T* info )
{
    if ( info == NULL )
    {
        return EXTERNAL_FLASH_STATUS_INVALID_ARG;
    }

    info->instruction_capacity_bytes =
        EXTERNAL_FLASH_GetPartitionCapacityBytes( &external_flash_instruction_partition );
    info->result_capacity_bytes =
        EXTERNAL_FLASH_GetPartitionCapacityBytes( &external_flash_result_partition );
    info->result_length_bytes = external_flash_committed_result_length_bytes;
    info->bad_block_count     = external_flash_bad_block_count;

    return EXTERNAL_FLASH_STATUS_OK;
}

/**
 * @brief Erases the result partition and starts a new volatile result session.
 */
ExternalFlashStatus_T EXTERNAL_FLASH_StartSession( void )
{
    if ( !external_flash_initialised )
    {
        return EXTERNAL_FLASH_STATUS_NOT_INITIALISED;
    }

    ExternalFlashStatus_T status = EXTERNAL_FLASH_EraseResultPartition();
    if ( status != EXTERNAL_FLASH_STATUS_OK )
    {
        return status;
    }

    external_flash_session_active                = true;
    external_flash_result_length_bytes           = 0U;
    external_flash_committed_result_length_bytes = 0U;
    EXTERNAL_FLASH_ClearResultPageBuffer();

    return EXTERNAL_FLASH_STATUS_OK;
}

/**
 * @brief Appends arbitrary result bytes through the byte stream compatibility path.
 */
ExternalFlashStatus_T EXTERNAL_FLASH_WriteResultBytes( const uint8_t* data, uint32_t length )
{
    if ( !external_flash_initialised )
    {
        return EXTERNAL_FLASH_STATUS_NOT_INITIALISED;
    }

    if ( !external_flash_session_active )
    {
        return EXTERNAL_FLASH_STATUS_ERROR;
    }

    if ( ( data == NULL ) || ( length == 0U ) )
    {
        return EXTERNAL_FLASH_STATUS_INVALID_ARG;
    }

    uint32_t result_capacity =
        EXTERNAL_FLASH_GetPartitionCapacityBytes( &external_flash_result_partition );
    if ( ( external_flash_result_length_bytes > result_capacity )
         || ( length > ( result_capacity - external_flash_result_length_bytes ) ) )
    {
        return EXTERNAL_FLASH_STATUS_STORAGE_FULL;
    }

    uint32_t bytes_written = 0U;

    while ( bytes_written < length )
    {
        uint32_t page_space =
            external_flash_geometry.page_size_bytes - external_flash_result_page_fill;
        uint32_t remaining       = length - bytes_written;
        uint32_t bytes_this_page = ( remaining < page_space ) ? remaining : page_space;

        ( void )memcpy( &external_flash_result_page_buffer[external_flash_result_page_fill],
                        &data[bytes_written], bytes_this_page );

        external_flash_result_page_fill += bytes_this_page;
        external_flash_result_length_bytes += bytes_this_page;
        bytes_written += bytes_this_page;

        if ( external_flash_result_page_fill == external_flash_geometry.page_size_bytes )
        {
            ExternalFlashStatus_T status = EXTERNAL_FLASH_ProgramStagedResultPage();
            if ( status != EXTERNAL_FLASH_STATUS_OK )
            {
                return status;
            }
        }
    }

    return EXTERNAL_FLASH_STATUS_OK;
}

/**
 * @brief Writes one page scoped result buffer to the result partition.
 */
ExternalFlashStatus_T EXTERNAL_FLASH_WriteResultPage( const uint8_t* data, uint32_t valid_length )
{
    if ( !external_flash_initialised )
    {
        return EXTERNAL_FLASH_STATUS_NOT_INITIALISED;
    }

    if ( !external_flash_session_active )
    {
        return EXTERNAL_FLASH_STATUS_ERROR;
    }

    if ( data == NULL )
    {
        return EXTERNAL_FLASH_STATUS_INVALID_ARG;
    }

    if ( ( valid_length == 0U ) || ( valid_length > external_flash_geometry.page_size_bytes ) )
    {
        return EXTERNAL_FLASH_STATUS_INVALID_ARG;
    }

    if ( external_flash_result_page_fill != 0U )
    {
        return EXTERNAL_FLASH_STATUS_ERROR;
    }

    if ( ( external_flash_result_length_bytes % external_flash_geometry.page_size_bytes ) != 0U )
    {
        return EXTERNAL_FLASH_STATUS_ERROR;
    }

    uint32_t result_capacity =
        EXTERNAL_FLASH_GetPartitionCapacityBytes( &external_flash_result_partition );
    if ( ( external_flash_result_length_bytes > result_capacity )
         || ( valid_length > ( result_capacity - external_flash_result_length_bytes ) ) )
    {
        return EXTERNAL_FLASH_STATUS_STORAGE_FULL;
    }

    uint32_t              page_start_offset = external_flash_result_length_bytes;
    ExternalFlashStatus_T status            = EXTERNAL_FLASH_STATUS_OK;

    if ( valid_length == external_flash_geometry.page_size_bytes )
    {
        status = EXTERNAL_FLASH_ProgramResultPageBuffer( data, page_start_offset, valid_length );
    }
    else
    {
        EXTERNAL_FLASH_ClearResultPageBuffer();

        ( void )memcpy( external_flash_result_page_buffer, data, valid_length );

        status = EXTERNAL_FLASH_ProgramResultPageBuffer( external_flash_result_page_buffer,
                                                         page_start_offset, valid_length );

        EXTERNAL_FLASH_ClearResultPageBuffer();
    }

    if ( status == EXTERNAL_FLASH_STATUS_OK )
    {
        external_flash_result_length_bytes += valid_length;
    }

    return status;
}

/**
 * @brief Commits any partial result page staged by the byte stream write path.
 */
ExternalFlashStatus_T EXTERNAL_FLASH_FlushResults( void )
{
    if ( !external_flash_initialised )
    {
        return EXTERNAL_FLASH_STATUS_NOT_INITIALISED;
    }

    if ( !external_flash_session_active )
    {
        return EXTERNAL_FLASH_STATUS_ERROR;
    }

    return EXTERNAL_FLASH_ProgramStagedResultPage();
}

/**
 * @brief Reads arbitrary instruction bytes through the byte oriented compatibility path.
 */
ExternalFlashStatus_T EXTERNAL_FLASH_ReadInstructions( uint32_t offset, uint8_t* data,
                                                       uint32_t length )
{
    return EXTERNAL_FLASH_ReadFromPartition(
        &external_flash_instruction_partition,
        EXTERNAL_FLASH_GetPartitionCapacityBytes( &external_flash_instruction_partition ), offset,
        data, length );
}

/**
 * @brief Reads one instruction queue page using the NAND DMA read path internally.
 */
ExternalFlashStatus_T EXTERNAL_FLASH_ReadInstructionPage( uint32_t offset, uint8_t* data,
                                                          uint32_t length )
{
    return EXTERNAL_FLASH_ReadPartitionPageDma(
        &external_flash_instruction_partition,
        EXTERNAL_FLASH_GetPartitionCapacityBytes( &external_flash_instruction_partition ), offset,
        data, length );
}

/**
 * @brief Reads committed result bytes for host result transfer.
 */
ExternalFlashStatus_T EXTERNAL_FLASH_ReadResults( uint32_t offset, uint8_t* data, uint32_t length )
{
    return EXTERNAL_FLASH_ReadFromPartition( &external_flash_result_partition,
                                             external_flash_committed_result_length_bytes, offset,
                                             data, length );
}

/**
 * @brief Returns whether the external flash service is initialised.
 */
bool EXTERNAL_FLASH_IsInitialised( void )
{
    return external_flash_initialised;
}
