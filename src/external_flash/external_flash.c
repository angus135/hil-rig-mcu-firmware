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
 *      result partition and reads instruction bytes from a fixed instruction
 *      partition. Physical NAND command sequencing remains in hw_nand.
 *
 *      The intended runtime path is:
 *      1. test_package_recieve receives a host test package and arranges for
 *         instruction bytes to be present in the instruction partition. This
 *         first implementation only reads instructions; package-upload writes
 *         should be added here when that manager is implemented.
 *      2. The flash manager calls EXTERNAL_FLASH_StartSession before execution.
 *         This erases the result partition so result writes never block on
 *         just-in-time erases during the execution run.
 *      3. flash_manager drains execution result buffers and calls
 *         EXTERNAL_FLASH_WriteResults with opaque byte spans.
 *      4. flash_manager refills instruction buffers by calling
 *         EXTERNAL_FLASH_ReadInstructions with logical byte offsets.
 *      5. result_transfer_manager calls EXTERNAL_FLASH_FlushResults, then
 *         EXTERNAL_FLASH_ReadResults to stream committed result bytes to the
 *         host interface.
 *
 *      Design boundaries:
 *      - execution_manager never calls this module directly.
 *      - flash_manager owns RAM instruction/result buffer lifetime and
 *        producer/consumer state.
 *      - flash_manager is the only normal runtime task responsible for flash
 *        access.
 *      - external_flash owns NAND partitioning, result page packing, bad-block
 *        skipping, erase-at-session-start, and committed result length.
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */
#include "external_flash.h"

#include "hw_nand.h"

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */

#define EXTERNAL_FLASH_PAGE_PROGRAM_DMA_POLL_LIMIT ( 1000000U )
#define EXTERNAL_FLASH_INVALID_BLOCK ( 0xFFFFFFFFU )

/**-----------------------------------------------------------------------------
 *  Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

typedef struct
{
    /** First physical block in the partition. */
    uint32_t start_block;

    /** Number of physical blocks assigned to the partition. */
    uint32_t block_count;
} ExternalFlashPartition_T;

/**-----------------------------------------------------------------------------
 *  Public (global) and Extern Variables
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Private (static) Variables
 *------------------------------------------------------------------------------
 */

/** Tracks whether the NAND device and partition scan completed successfully. */
static bool external_flash_initialised = false;

/** Tracks whether the current result partition has been erased for a new session. */
static bool external_flash_session_active = false;

/** Geometry cached from hw_nand during initialisation. */
static HW_NAND_Geometry_T external_flash_geometry = { 0U, 0U, 0U, 0U };

/** Bad-block bitmap indexed by physical block number. */
static bool external_flash_bad_block_table[EXTERNAL_FLASH_INSTRUCTION_BLOCK_COUNT
                                           + EXTERNAL_FLASH_RESULT_BLOCK_COUNT] = { false };

/** Number of bad blocks found across the configured instruction and result partitions. */
static uint32_t external_flash_bad_block_count = 0U;

/**
 * Total logical result bytes accepted in the current volatile session.
 *
 * This includes bytes still staged in RAM and is used to choose the physical
 * destination for the next page program.
 */
static uint32_t external_flash_result_length_bytes = 0U;

/**
 * Total logical result bytes successfully committed to NAND.
 *
 * Result transfer is limited to this value so the host never receives bytes
 * that are still only staged in RAM.
 */
static uint32_t external_flash_committed_result_length_bytes = 0U;

/** Number of bytes currently staged in the result page buffer. */
static uint32_t external_flash_result_page_fill = 0U;

/** Page-sized staging buffer used to pack arbitrary result bytes into NAND pages. */
static uint8_t external_flash_result_page_buffer[2048U] = { 0xFFU };

/**
 * Fixed partition used for pre-loaded execution instructions.
 *
 * This driver version reads from the partition but does not yet provide the
 * upload/programming API that test_package_recieve will eventually need.
 */
static const ExternalFlashPartition_T external_flash_instruction_partition = {
    EXTERNAL_FLASH_INSTRUCTION_START_BLOCK,
    EXTERNAL_FLASH_INSTRUCTION_BLOCK_COUNT,
};

/**
 * Fixed partition used for volatile execution result storage.
 *
 * The partition is erased at session start and is append-only while the test is
 * running.
 */
static const ExternalFlashPartition_T external_flash_result_partition = {
    EXTERNAL_FLASH_RESULT_START_BLOCK,
    EXTERNAL_FLASH_RESULT_BLOCK_COUNT,
};

/**-----------------------------------------------------------------------------
 *  Private (static) Function Prototypes
 *------------------------------------------------------------------------------
 */

/** Maps physical NAND status values into the public external_flash status enum. */
static ExternalFlashStatus_T EXTERNAL_FLASH_MapNandStatus( HW_NAND_Status_T nand_status );

/** Returns the number of main-area bytes in one physical block. */
static uint32_t EXTERNAL_FLASH_BlockDataSizeBytes( void );

/** Checks whether a physical block index is in the configured bad-block table. */
static bool EXTERNAL_FLASH_IsPhysicalBlockBad( uint32_t block );

/**
 * Converts a partition-local logical block index to a usable physical block.
 *
 * Logical block indexes count only good blocks. This is the central bad-block
 * skipping decision for instruction reads, result writes, and result readback.
 */
static bool
EXTERNAL_FLASH_GetPhysicalBlockForLogicalBlock( const ExternalFlashPartition_T* partition,
                                                uint32_t logical_block, uint32_t* physical_block );

/**
 * Converts a partition byte offset to a usable physical page and column.
 *
 * All public read/write APIs use logical byte offsets. This helper hides NAND
 * pages, blocks, and bad-block gaps from flash_manager and transfer managers.
 */
static bool EXTERNAL_FLASH_GetPhysicalAddress( const ExternalFlashPartition_T* partition,
                                               uint32_t offset, uint32_t* page, uint16_t* column );

/** Returns the logical byte capacity of a partition after bad-block removal. */
static uint32_t
EXTERNAL_FLASH_GetPartitionCapacityBytes( const ExternalFlashPartition_T* partition );

/** Scans both configured partitions and caches factory bad-block markers. */
static ExternalFlashStatus_T EXTERNAL_FLASH_ScanBadBlocks( void );

/** Erases all good blocks in the result partition. */
static ExternalFlashStatus_T EXTERNAL_FLASH_EraseResultPartition( void );

/** Clears the in-RAM result page staging buffer to erased NAND state. */
static void EXTERNAL_FLASH_ClearResultPageBuffer( void );

/**
 * Programs the currently staged result page into the next result partition page.
 *
 * Full pages are programmed during EXTERNAL_FLASH_WriteResults. Partial pages
 * are programmed by EXTERNAL_FLASH_FlushResults before host result transfer.
 */
static ExternalFlashStatus_T EXTERNAL_FLASH_ProgramStagedResultPage( void );

/** Reads opaque bytes from a partition by logical byte offset. */
static ExternalFlashStatus_T
EXTERNAL_FLASH_ReadFromPartition( const ExternalFlashPartition_T* partition,
                                  uint32_t readable_length, uint32_t offset, uint8_t* data,
                                  uint32_t length );

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
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

static uint32_t EXTERNAL_FLASH_BlockDataSizeBytes( void )
{
    return external_flash_geometry.page_size_bytes * external_flash_geometry.pages_per_block;
}

static bool EXTERNAL_FLASH_IsPhysicalBlockBad( uint32_t block )
{
    if ( block >= ( EXTERNAL_FLASH_INSTRUCTION_BLOCK_COUNT + EXTERNAL_FLASH_RESULT_BLOCK_COUNT ) )
    {
        return true;
    }

    return external_flash_bad_block_table[block];
}

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

static ExternalFlashStatus_T EXTERNAL_FLASH_EraseResultPartition( void )
{
    for ( uint32_t i = 0U; i < external_flash_result_partition.block_count; i++ )
    {
        uint32_t block = external_flash_result_partition.start_block + i;

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

static void EXTERNAL_FLASH_ClearResultPageBuffer( void )
{
    ( void )memset( external_flash_result_page_buffer, 0xFF,
                    external_flash_geometry.page_size_bytes );
    external_flash_result_page_fill = 0U;
}

static ExternalFlashStatus_T EXTERNAL_FLASH_ProgramStagedResultPage( void )
{
    if ( external_flash_result_page_fill == 0U )
    {
        return EXTERNAL_FLASH_STATUS_OK;
    }

    uint32_t page_start_offset =
        external_flash_result_length_bytes - external_flash_result_page_fill;
    uint32_t bytes_to_commit = external_flash_result_page_fill;

    for ( uint32_t attempts = 0U; attempts < external_flash_result_partition.block_count;
          attempts++ )
    {
        uint32_t page   = 0U;
        uint16_t column = 0U;

        if ( !EXTERNAL_FLASH_GetPhysicalAddress( &external_flash_result_partition,
                                                 page_start_offset, &page, &column )
             || ( column != 0U ) )
        {
            return EXTERNAL_FLASH_STATUS_STORAGE_FULL;
        }

        ExternalFlashStatus_T status = EXTERNAL_FLASH_MapNandStatus( HW_NAND_ProgramLoadDma(
            0U, external_flash_result_page_buffer, external_flash_geometry.page_size_bytes ) );
        if ( status != EXTERNAL_FLASH_STATUS_OK )
        {
            return status;
        }

        uint32_t poll_count = EXTERNAL_FLASH_PAGE_PROGRAM_DMA_POLL_LIMIT;
        while ( ( !HW_NAND_IsTransferComplete() ) && ( poll_count > 0U ) )
        {
            poll_count--;
        }

        if ( poll_count == 0U )
        {
            return EXTERNAL_FLASH_STATUS_TIMEOUT;
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
            external_flash_committed_result_length_bytes += bytes_to_commit;
            EXTERNAL_FLASH_ClearResultPageBuffer();
            return EXTERNAL_FLASH_STATUS_OK;
        }
    }

    return EXTERNAL_FLASH_STATUS_STORAGE_FULL;
}

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

/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
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

ExternalFlashStatus_T EXTERNAL_FLASH_WriteResults( const uint8_t* data, uint32_t length )
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

ExternalFlashStatus_T EXTERNAL_FLASH_ReadInstructions( uint32_t offset, uint8_t* data,
                                                       uint32_t length )
{
    return EXTERNAL_FLASH_ReadFromPartition(
        &external_flash_instruction_partition,
        EXTERNAL_FLASH_GetPartitionCapacityBytes( &external_flash_instruction_partition ), offset,
        data, length );
}

ExternalFlashStatus_T EXTERNAL_FLASH_ReadResults( uint32_t offset, uint8_t* data, uint32_t length )
{
    return EXTERNAL_FLASH_ReadFromPartition( &external_flash_result_partition,
                                             external_flash_committed_result_length_bytes, offset,
                                             data, length );
}

bool EXTERNAL_FLASH_IsInitialised( void )
{
    return external_flash_initialised;
}
