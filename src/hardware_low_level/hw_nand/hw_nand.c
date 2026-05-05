/******************************************************************************
 *  File:       hw_nand.c
 *  Author:     Callum Rafferty
 *  Created:    5-May-2026
 *
 *  Description:
 *      Low level SPI NAND flash device driver implementation.
 *
 *  Notes:
 *      NAND-specific command sequencing belongs here. Raw QSPI transaction
 *      mechanics belong in hw_qspi, and application-level storage policy
 *      belongs in external_flash.
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include "hw_nand.h"

#include "hw_qspi.h"

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */

#define HW_NAND_OPCODE_RESET ( 0xFFU )
#define HW_NAND_OPCODE_READ_ID ( 0x9FU )
#define HW_NAND_OPCODE_WRITE_ENABLE ( 0x06U )
#define HW_NAND_OPCODE_WRITE_DISABLE ( 0x04U )
#define HW_NAND_OPCODE_GET_FEATURE ( 0x0FU )
#define HW_NAND_OPCODE_SET_FEATURE ( 0x1FU )
#define HW_NAND_OPCODE_PAGE_READ ( 0x13U )
#define HW_NAND_OPCODE_READ_CACHE_QUAD ( 0x6BU )
#define HW_NAND_OPCODE_PROGRAM_LOAD_QUAD ( 0x32U )
#define HW_NAND_OPCODE_PROGRAM_EXECUTE ( 0x10U )
#define HW_NAND_OPCODE_BLOCK_ERASE ( 0xD8U )

#define HW_NAND_ID_LENGTH_BYTES ( 2U )
#define HW_NAND_EXPECTED_MANUFACTURER_ID ( 0x01U )
#define HW_NAND_EXPECTED_DEVICE_ID ( 0x35U )

#define HW_NAND_FEATURE_BLOCK_LOCK ( 0xA0U )
#define HW_NAND_FEATURE_CONFIGURATION ( 0xB0U )
#define HW_NAND_FEATURE_STATUS ( 0xC0U )

#define HW_NAND_BLOCK_LOCK_UNLOCK_ALL ( 0x00U )
#define HW_NAND_CONFIGURATION_ECC_ENABLE_MASK ( 0x10U )
#define HW_NAND_CONFIGURATION_MODE_MASK ( 0xC2U )
#define HW_NAND_CONFIGURATION_NORMAL_MODE ( 0x00U )
#define HW_NAND_CONFIGURATION_ECC_ENABLE_NORMAL_MODE \
    ( HW_NAND_CONFIGURATION_ECC_ENABLE_MASK | HW_NAND_CONFIGURATION_NORMAL_MODE )

#define HW_NAND_STATUS_OIP_MASK ( 1U << 0U )
#define HW_NAND_STATUS_E_FAIL_MASK ( 1U << 2U )
#define HW_NAND_STATUS_P_FAIL_MASK ( 1U << 3U )
#define HW_NAND_STATUS_ECC_MASK ( 0x30U )
#define HW_NAND_STATUS_ECC_NONE ( 0x00U )
#define HW_NAND_STATUS_ECC_CORRECTED_1_TO_2 ( 0x10U )
#define HW_NAND_STATUS_ECC_CORRECTED_3_TO_6 ( 0x20U )
#define HW_NAND_STATUS_ECC_UNCORRECTABLE ( 0x30U )

#define HW_NAND_PAGE_SIZE_BYTES ( 2048U )
#define HW_NAND_SPARE_SIZE_BYTES ( 128U )
#define HW_NAND_PAGES_PER_BLOCK ( 64U )

#ifndef HW_NAND_BLOCK_COUNT
#define HW_NAND_BLOCK_COUNT ( 4096U )
#endif

#define HW_NAND_PAGE_TOTAL_BYTES ( HW_NAND_PAGE_SIZE_BYTES + HW_NAND_SPARE_SIZE_BYTES )
#define HW_NAND_PAGE_COUNT ( HW_NAND_BLOCK_COUNT * HW_NAND_PAGES_PER_BLOCK )
#define HW_NAND_BAD_BLOCK_MARKER_COLUMN ( HW_NAND_PAGE_SIZE_BYTES )
#define HW_NAND_BAD_BLOCK_MARKER_VALUE ( 0x00U )
#define HW_NAND_BAD_BLOCK_MARKER_PAGE_COUNT ( 3U )

#define HW_NAND_DEFAULT_TIMEOUT_MS ( 100U )
#define HW_NAND_RESET_TIMEOUT_MS ( 2U )
#define HW_NAND_PAGE_READ_TIMEOUT_MS ( 1U )
#define HW_NAND_PROGRAM_TIMEOUT_MS ( 1U )
#define HW_NAND_BLOCK_ERASE_TIMEOUT_MS ( 10U )

/*
 * The current low-level layer does not own an RTOS delay or tick source. Treat
 * timeout_ms as a bounded polling budget until hw_nand is wired into the flash
 * manager task timing model.
 */
#define HW_NAND_READY_POLLS_PER_TIMEOUT_MS ( 16U )

/**-----------------------------------------------------------------------------
 *  Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Public (global) and Extern Variables
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Private (static) Variables
 *------------------------------------------------------------------------------
 */

/** Tracks whether the NAND device has completed the driver initialisation sequence. */
static bool nand_initialised = false;

/** Caches the ECC result decoded after the most recent checked page read. */
static HW_NAND_EccStatus_T nand_last_ecc_status = HW_NAND_ECC_STATUS_UNKNOWN;

/** Compile-time geometry for the selected S35ML04G3 SPI NAND part. */
static const HW_NAND_Geometry_T nand_geometry = {
    HW_NAND_PAGE_SIZE_BYTES,
    HW_NAND_SPARE_SIZE_BYTES,
    HW_NAND_PAGES_PER_BLOCK,
    HW_NAND_BLOCK_COUNT,
};

/**-----------------------------------------------------------------------------
 *  Private (static) Function Prototypes
 *------------------------------------------------------------------------------
 */

/** Maps generic QSPI wrapper status values into NAND-level status values. */
static HW_NAND_Status_T HW_NAND_Map_QSPI_Status( HW_QSPI_Status_T qspi_status );

/** Builds an opcode-only QSPI command using the common NAND defaults. */
static HW_QSPI_Command_T HW_NAND_Make_Command( uint8_t opcode );

/** Builds a NAND command with an instruction and address phase. */
static HW_QSPI_Command_T HW_NAND_Make_Address_Command( uint8_t opcode, uint32_t address,
                                                       HW_QSPI_AddressSize_T address_size );

/** Builds a NAND data command that does not include an address phase. */
static HW_QSPI_Command_T HW_NAND_Make_No_Address_Data_Command( uint8_t opcode,
                                                               HW_QSPI_Lines_T data_lines,
                                                               uint32_t        dummy_cycles );

/** Builds a NAND data command that includes an address phase. */
static HW_QSPI_Command_T HW_NAND_Make_Data_Command( uint8_t opcode, uint32_t address,
                                                    HW_QSPI_AddressSize_T address_size,
                                                    HW_QSPI_Lines_T       data_lines,
                                                    uint32_t              dummy_cycles );

/** Issues the NAND write-enable command. */
static HW_NAND_Status_T HW_NAND_WriteEnable( void );

/** Issues the NAND write-disable command. */
static HW_NAND_Status_T HW_NAND_WriteDisable( void );

/** Reads the C0h status feature register. */
static HW_NAND_Status_T HW_NAND_ReadStatus( uint8_t* status );

/** Decodes the ECC status bits from the C0h status feature register. */
static HW_NAND_EccStatus_T HW_NAND_Decode_Ecc_Status( uint8_t status );

/** Converts a ready status register value into a NAND API result. */
static HW_NAND_Status_T HW_NAND_CheckReadyStatus( uint8_t status, bool check_program_fail,
                                                  bool check_erase_fail, bool check_ecc );

/** Polls C0h until OIP clears and then applies the requested status checks. */
static HW_NAND_Status_T HW_NAND_WaitReadyWithChecks( uint32_t timeout_ms,
                                                     bool     check_program_fail,
                                                     bool     check_erase_fail, bool check_ecc );

/** Applies startup feature configuration required before normal operation. */
static HW_NAND_Status_T HW_NAND_ConfigureDevice( void );

/** Reads and verifies the manufacturer and device ID bytes. */
static HW_NAND_Status_T HW_NAND_VerifyDeviceId( void );

/** Returns the first page row address for a physical block. */
static uint32_t HW_NAND_Block_First_Page( uint32_t block );

/** Returns the second page row address for a physical block. */
static uint32_t HW_NAND_Block_Second_Page( uint32_t block );

/** Returns the last page row address for a physical block. */
static uint32_t HW_NAND_Block_Last_Page( uint32_t block );

/** Checks whether a page row address is within the configured device geometry. */
static bool HW_NAND_Is_Valid_Page( uint32_t page );

/** Checks whether a block index is within the configured device geometry. */
static bool HW_NAND_Is_Valid_Block( uint32_t block );

/** Checks whether a cache column range fits within main plus spare page bytes. */
static bool HW_NAND_Is_Valid_Column_Range( uint16_t column, uint32_t length );

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

static HW_NAND_Status_T HW_NAND_Map_QSPI_Status( HW_QSPI_Status_T qspi_status )
{
    switch ( qspi_status )
    {
        case HW_QSPI_STATUS_OK:
            return HW_NAND_STATUS_OK;
        case HW_QSPI_STATUS_BUSY:
            return HW_NAND_STATUS_BUSY;
        case HW_QSPI_STATUS_TIMEOUT:
            return HW_NAND_STATUS_TIMEOUT;
        case HW_QSPI_STATUS_INVALID_ARG:
            return HW_NAND_STATUS_INVALID_ARG;
        case HW_QSPI_STATUS_NOT_INITIALISED:
            return HW_NAND_STATUS_NOT_INITIALISED;
        case HW_QSPI_STATUS_ERROR:
        default:
            return HW_NAND_STATUS_ERROR;
    }
}

static HW_QSPI_Command_T HW_NAND_Make_Command( uint8_t opcode )
{
    HW_QSPI_Command_T command = { 0 };

    command.instruction       = opcode;
    command.instruction_lines = HW_QSPI_LINES_1;
    command.address_size      = HW_QSPI_ADDR_NONE;
    command.address_lines     = HW_QSPI_LINES_NONE;
    command.data_lines        = HW_QSPI_LINES_NONE;
    command.timeout_ms        = HW_NAND_DEFAULT_TIMEOUT_MS;

    return command;
}

static HW_QSPI_Command_T HW_NAND_Make_Address_Command( uint8_t opcode, uint32_t address,
                                                       HW_QSPI_AddressSize_T address_size )
{
    HW_QSPI_Command_T command = HW_NAND_Make_Command( opcode );

    command.address       = address;
    command.address_size  = address_size;
    command.address_lines = HW_QSPI_LINES_1;

    return command;
}

static HW_QSPI_Command_T HW_NAND_Make_No_Address_Data_Command( uint8_t opcode,
                                                               HW_QSPI_Lines_T data_lines,
                                                               uint32_t        dummy_cycles )
{
    HW_QSPI_Command_T command = HW_NAND_Make_Command( opcode );

    command.data_lines   = data_lines;
    command.dummy_cycles = dummy_cycles;

    return command;
}

static HW_QSPI_Command_T HW_NAND_Make_Data_Command( uint8_t opcode, uint32_t address,
                                                    HW_QSPI_AddressSize_T address_size,
                                                    HW_QSPI_Lines_T       data_lines,
                                                    uint32_t              dummy_cycles )
{
    HW_QSPI_Command_T command = HW_NAND_Make_Address_Command( opcode, address, address_size );

    command.data_lines   = data_lines;
    command.dummy_cycles = dummy_cycles;

    return command;
}

static HW_NAND_Status_T HW_NAND_WriteEnable( void )
{
    HW_QSPI_Command_T command = HW_NAND_Make_Command( HW_NAND_OPCODE_WRITE_ENABLE );

    return HW_NAND_Map_QSPI_Status( HW_QSPI_Command( &command ) );
}

static HW_NAND_Status_T HW_NAND_WriteDisable( void )
{
    HW_QSPI_Command_T command = HW_NAND_Make_Command( HW_NAND_OPCODE_WRITE_DISABLE );

    return HW_NAND_Map_QSPI_Status( HW_QSPI_Command( &command ) );
}

static HW_NAND_Status_T HW_NAND_ReadStatus( uint8_t* status )
{
    if ( status == NULL )
    {
        return HW_NAND_STATUS_INVALID_ARG;
    }

    return HW_NAND_GetFeature( HW_NAND_FEATURE_STATUS, status );
}

static HW_NAND_EccStatus_T HW_NAND_Decode_Ecc_Status( uint8_t status )
{
    switch ( status & HW_NAND_STATUS_ECC_MASK )
    {
        case HW_NAND_STATUS_ECC_NONE:
            return HW_NAND_ECC_STATUS_NO_BIT_FLIPS;
        case HW_NAND_STATUS_ECC_CORRECTED_1_TO_2:
            return HW_NAND_ECC_STATUS_CORRECTED_1_TO_2;
        case HW_NAND_STATUS_ECC_CORRECTED_3_TO_6:
            return HW_NAND_ECC_STATUS_CORRECTED_3_TO_6;
        case HW_NAND_STATUS_ECC_UNCORRECTABLE:
            return HW_NAND_ECC_STATUS_UNCORRECTABLE;
        default:
            return HW_NAND_ECC_STATUS_UNKNOWN;
    }
}

static HW_NAND_Status_T HW_NAND_CheckReadyStatus( uint8_t status, bool check_program_fail,
                                                  bool check_erase_fail, bool check_ecc )
{
    if ( check_program_fail && ( ( status & HW_NAND_STATUS_P_FAIL_MASK ) != 0U ) )
    {
        return HW_NAND_STATUS_PROGRAM_FAIL;
    }

    if ( check_erase_fail && ( ( status & HW_NAND_STATUS_E_FAIL_MASK ) != 0U ) )
    {
        return HW_NAND_STATUS_ERASE_FAIL;
    }

    if ( check_ecc )
    {
        nand_last_ecc_status = HW_NAND_Decode_Ecc_Status( status );
    }

    if ( check_ecc && ( nand_last_ecc_status == HW_NAND_ECC_STATUS_UNCORRECTABLE ) )
    {
        return HW_NAND_STATUS_ECC_ERROR;
    }

    return HW_NAND_STATUS_OK;
}

static HW_NAND_Status_T HW_NAND_WaitReadyWithChecks( uint32_t timeout_ms,
                                                     bool     check_program_fail,
                                                     bool     check_erase_fail, bool check_ecc )
{
    uint32_t poll_count = timeout_ms * HW_NAND_READY_POLLS_PER_TIMEOUT_MS;
    if ( poll_count == 0U )
    {
        poll_count = 1U;
    }

    for ( uint32_t i = 0U; i < poll_count; i++ )
    {
        uint8_t status_register = 0U;

        HW_NAND_Status_T status = HW_NAND_ReadStatus( &status_register );
        if ( status != HW_NAND_STATUS_OK )
        {
            return status;
        }

        if ( ( status_register & HW_NAND_STATUS_OIP_MASK ) == 0U )
        {
            return HW_NAND_CheckReadyStatus( status_register, check_program_fail, check_erase_fail,
                                             check_ecc );
        }
    }

    return HW_NAND_STATUS_TIMEOUT;
}

static HW_NAND_Status_T HW_NAND_ConfigureDevice( void )
{
    HW_NAND_Status_T status =
        HW_NAND_SetFeature( HW_NAND_FEATURE_BLOCK_LOCK, HW_NAND_BLOCK_LOCK_UNLOCK_ALL );
    if ( status != HW_NAND_STATUS_OK )
    {
        return status;
    }

    uint8_t block_lock = 0U;
    status             = HW_NAND_GetFeature( HW_NAND_FEATURE_BLOCK_LOCK, &block_lock );
    if ( status != HW_NAND_STATUS_OK )
    {
        return status;
    }

    if ( block_lock != HW_NAND_BLOCK_LOCK_UNLOCK_ALL )
    {
        return HW_NAND_STATUS_ERROR;
    }

    status = HW_NAND_SetFeature( HW_NAND_FEATURE_CONFIGURATION,
                                 HW_NAND_CONFIGURATION_ECC_ENABLE_NORMAL_MODE );
    if ( status != HW_NAND_STATUS_OK )
    {
        return status;
    }

    uint8_t configuration = 0U;
    status                = HW_NAND_GetFeature( HW_NAND_FEATURE_CONFIGURATION, &configuration );
    if ( status != HW_NAND_STATUS_OK )
    {
        return status;
    }

    if ( ( configuration & HW_NAND_CONFIGURATION_ECC_ENABLE_MASK ) !=
         HW_NAND_CONFIGURATION_ECC_ENABLE_MASK )
    {
        return HW_NAND_STATUS_ERROR;
    }

    if ( ( configuration & HW_NAND_CONFIGURATION_MODE_MASK ) != HW_NAND_CONFIGURATION_NORMAL_MODE )
    {
        return HW_NAND_STATUS_ERROR;
    }

    return HW_NAND_STATUS_OK;
}

static HW_NAND_Status_T HW_NAND_VerifyDeviceId( void )
{
    HW_NAND_Id_T id = { 0U, 0U };

    HW_NAND_Status_T status = HW_NAND_ReadId( &id );
    if ( status != HW_NAND_STATUS_OK )
    {
        return status;
    }

    if ( ( id.manufacturer_id != HW_NAND_EXPECTED_MANUFACTURER_ID ) ||
         ( id.device_id != HW_NAND_EXPECTED_DEVICE_ID ) )
    {
        return HW_NAND_STATUS_UNSUPPORTED_DEVICE;
    }

    return HW_NAND_STATUS_OK;
}

static uint32_t HW_NAND_Block_First_Page( uint32_t block )
{
    return block * HW_NAND_PAGES_PER_BLOCK;
}

static uint32_t HW_NAND_Block_Second_Page( uint32_t block )
{
    return HW_NAND_Block_First_Page( block ) + 1U;
}

static uint32_t HW_NAND_Block_Last_Page( uint32_t block )
{
    return HW_NAND_Block_First_Page( block ) + ( HW_NAND_PAGES_PER_BLOCK - 1U );
}

static bool HW_NAND_Is_Valid_Page( uint32_t page )
{
    return page < HW_NAND_PAGE_COUNT;
}

static bool HW_NAND_Is_Valid_Block( uint32_t block )
{
    return block < HW_NAND_BLOCK_COUNT;
}

static bool HW_NAND_Is_Valid_Column_Range( uint16_t column, uint32_t length )
{
    if ( length == 0U )
    {
        return false;
    }

    if ( length > HW_NAND_PAGE_TOTAL_BYTES )
    {
        return false;
    }

    return ( uint32_t )column <= ( HW_NAND_PAGE_TOTAL_BYTES - length );
}

/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */

HW_NAND_Status_T HW_NAND_Init( void )
{
    nand_initialised = true;
    nand_last_ecc_status = HW_NAND_ECC_STATUS_UNKNOWN;

    HW_NAND_Status_T status = HW_NAND_Reset();
    if ( status != HW_NAND_STATUS_OK )
    {
        nand_initialised = false;
        return status;
    }

    status = HW_NAND_WaitReadyWithChecks( HW_NAND_RESET_TIMEOUT_MS, false, false, false );
    if ( status != HW_NAND_STATUS_OK )
    {
        nand_initialised = false;
        return status;
    }

    status = HW_NAND_VerifyDeviceId();
    if ( status != HW_NAND_STATUS_OK )
    {
        nand_initialised = false;
        return status;
    }

    status = HW_NAND_ConfigureDevice();
    if ( status != HW_NAND_STATUS_OK )
    {
        nand_initialised = false;
        return status;
    }

    return HW_NAND_STATUS_OK;
}

HW_NAND_Status_T HW_NAND_Reset( void )
{
    if ( !nand_initialised )
    {
        return HW_NAND_STATUS_NOT_INITIALISED;
    }

    HW_QSPI_Command_T command = HW_NAND_Make_Command( HW_NAND_OPCODE_RESET );

    return HW_NAND_Map_QSPI_Status( HW_QSPI_Command( &command ) );
}

HW_NAND_Status_T HW_NAND_ReadId( HW_NAND_Id_T* id )
{
    if ( !nand_initialised )
    {
        return HW_NAND_STATUS_NOT_INITIALISED;
    }

    if ( id == NULL )
    {
        return HW_NAND_STATUS_INVALID_ARG;
    }

    uint8_t id_bytes[HW_NAND_ID_LENGTH_BYTES] = { 0U, 0U };

    HW_QSPI_Command_T command =
        HW_NAND_Make_No_Address_Data_Command( HW_NAND_OPCODE_READ_ID, HW_QSPI_LINES_1, 8U );

    HW_NAND_Status_T status =
        HW_NAND_Map_QSPI_Status( HW_QSPI_ReadBlocking( &command, id_bytes,
                                                       HW_NAND_ID_LENGTH_BYTES ) );
    if ( status != HW_NAND_STATUS_OK )
    {
        return status;
    }

    id->manufacturer_id = id_bytes[0];
    id->device_id       = id_bytes[1];

    return HW_NAND_STATUS_OK;
}

HW_NAND_Status_T HW_NAND_GetGeometry( HW_NAND_Geometry_T* geometry )
{
    if ( geometry == NULL )
    {
        return HW_NAND_STATUS_INVALID_ARG;
    }

    *geometry = nand_geometry;

    return HW_NAND_STATUS_OK;
}

HW_NAND_Status_T HW_NAND_GetLastEccStatus( HW_NAND_EccStatus_T* ecc_status )
{
    if ( ecc_status == NULL )
    {
        return HW_NAND_STATUS_INVALID_ARG;
    }

    *ecc_status = nand_last_ecc_status;

    return HW_NAND_STATUS_OK;
}

HW_NAND_Status_T HW_NAND_GetFeature( uint8_t feature_address, uint8_t* value )
{
    if ( !nand_initialised )
    {
        return HW_NAND_STATUS_NOT_INITIALISED;
    }

    if ( value == NULL )
    {
        return HW_NAND_STATUS_INVALID_ARG;
    }

    HW_QSPI_Command_T command =
        HW_NAND_Make_Data_Command( HW_NAND_OPCODE_GET_FEATURE, feature_address,
                                   HW_QSPI_ADDR_8_BITS, HW_QSPI_LINES_1, 0U );

    return HW_NAND_Map_QSPI_Status( HW_QSPI_ReadBlocking( &command, value, 1U ) );
}

HW_NAND_Status_T HW_NAND_SetFeature( uint8_t feature_address, uint8_t value )
{
    if ( !nand_initialised )
    {
        return HW_NAND_STATUS_NOT_INITIALISED;
    }

    HW_QSPI_Command_T command =
        HW_NAND_Make_Data_Command( HW_NAND_OPCODE_SET_FEATURE, feature_address,
                                   HW_QSPI_ADDR_8_BITS, HW_QSPI_LINES_1, 0U );

    HW_NAND_Status_T status =
        HW_NAND_Map_QSPI_Status( HW_QSPI_WriteBlocking( &command, &value, 1U ) );

    /*
     * SET FEATURE does not require WREN for the normal block-lock/configuration
     * setup path, but leave WEL clear after any feature write sequence so later
     * program/erase operations must explicitly enable writes.
     */
    if ( status == HW_NAND_STATUS_OK )
    {
        ( void )HW_NAND_WriteDisable();
    }

    return status;
}

HW_NAND_Status_T HW_NAND_WaitReady( uint32_t timeout_ms )
{
    if ( !nand_initialised )
    {
        return HW_NAND_STATUS_NOT_INITIALISED;
    }

    return HW_NAND_WaitReadyWithChecks( timeout_ms, false, false, false );
}

HW_NAND_Status_T HW_NAND_WaitPageReadComplete( uint32_t timeout_ms )
{
    if ( !nand_initialised )
    {
        return HW_NAND_STATUS_NOT_INITIALISED;
    }

    return HW_NAND_WaitReadyWithChecks( timeout_ms, false, false, true );
}

HW_NAND_Status_T HW_NAND_WaitProgramComplete( uint32_t timeout_ms )
{
    if ( !nand_initialised )
    {
        return HW_NAND_STATUS_NOT_INITIALISED;
    }

    return HW_NAND_WaitReadyWithChecks( timeout_ms, true, false, false );
}

HW_NAND_Status_T HW_NAND_WaitBlockEraseComplete( uint32_t timeout_ms )
{
    if ( !nand_initialised )
    {
        return HW_NAND_STATUS_NOT_INITIALISED;
    }

    return HW_NAND_WaitReadyWithChecks( timeout_ms, false, true, false );
}

HW_NAND_Status_T HW_NAND_StartPageReadToCache( uint32_t page )
{
    if ( !nand_initialised )
    {
        return HW_NAND_STATUS_NOT_INITIALISED;
    }

    if ( !HW_NAND_Is_Valid_Page( page ) )
    {
        return HW_NAND_STATUS_INVALID_ARG;
    }

    HW_QSPI_Command_T command =
        HW_NAND_Make_Address_Command( HW_NAND_OPCODE_PAGE_READ, page, HW_QSPI_ADDR_24_BITS );
    command.timeout_ms = HW_NAND_PAGE_READ_TIMEOUT_MS;

    HW_NAND_Status_T status = HW_NAND_Map_QSPI_Status( HW_QSPI_Command( &command ) );
    if ( status != HW_NAND_STATUS_OK )
    {
        return status;
    }

    return HW_NAND_STATUS_OK;
}

HW_NAND_Status_T HW_NAND_ReadPageToCache( uint32_t page )
{
    if ( !nand_initialised )
    {
        return HW_NAND_STATUS_NOT_INITIALISED;
    }

    HW_NAND_Status_T status = HW_NAND_StartPageReadToCache( page );
    if ( status != HW_NAND_STATUS_OK )
    {
        return status;
    }

    return HW_NAND_WaitPageReadComplete( HW_NAND_PAGE_READ_TIMEOUT_MS );
}

HW_NAND_Status_T HW_NAND_ReadCacheBlocking( uint16_t column, uint8_t* data, uint32_t length )
{
    if ( !nand_initialised )
    {
        return HW_NAND_STATUS_NOT_INITIALISED;
    }

    if ( ( data == NULL ) || !HW_NAND_Is_Valid_Column_Range( column, length ) )
    {
        return HW_NAND_STATUS_INVALID_ARG;
    }

    HW_QSPI_Command_T command =
        HW_NAND_Make_Data_Command( HW_NAND_OPCODE_READ_CACHE_QUAD, column, HW_QSPI_ADDR_16_BITS,
                                   HW_QSPI_LINES_4, 8U );

    return HW_NAND_Map_QSPI_Status( HW_QSPI_ReadBlocking( &command, data, length ) );
}

HW_NAND_Status_T HW_NAND_ReadCacheDma( uint16_t column, uint8_t* data, uint32_t length )
{
    if ( !nand_initialised )
    {
        return HW_NAND_STATUS_NOT_INITIALISED;
    }

    if ( ( data == NULL ) || !HW_NAND_Is_Valid_Column_Range( column, length ) )
    {
        return HW_NAND_STATUS_INVALID_ARG;
    }

    HW_QSPI_Command_T command =
        HW_NAND_Make_Data_Command( HW_NAND_OPCODE_READ_CACHE_QUAD, column, HW_QSPI_ADDR_16_BITS,
                                   HW_QSPI_LINES_4, 8U );

    return HW_NAND_Map_QSPI_Status( HW_QSPI_ReadDma( &command, data, length ) );
}

HW_NAND_Status_T HW_NAND_ReadPageBlocking( uint32_t page, uint16_t column, uint8_t* data,
                                           uint32_t length )
{
    if ( !nand_initialised )
    {
        return HW_NAND_STATUS_NOT_INITIALISED;
    }

    HW_NAND_Status_T status = HW_NAND_ReadPageToCache( page );
    if ( status != HW_NAND_STATUS_OK )
    {
        return status;
    }

    return HW_NAND_ReadCacheBlocking( column, data, length );
}

HW_NAND_Status_T HW_NAND_ReadPageDma( uint32_t page, uint16_t column, uint8_t* data,
                                      uint32_t length )
{
    if ( !nand_initialised )
    {
        return HW_NAND_STATUS_NOT_INITIALISED;
    }

    HW_NAND_Status_T status = HW_NAND_ReadPageToCache( page );
    if ( status != HW_NAND_STATUS_OK )
    {
        return status;
    }

    return HW_NAND_ReadCacheDma( column, data, length );
}

HW_NAND_Status_T HW_NAND_ProgramLoadBlocking( uint16_t column, const uint8_t* data,
                                              uint32_t length )
{
    if ( !nand_initialised )
    {
        return HW_NAND_STATUS_NOT_INITIALISED;
    }

    if ( ( data == NULL ) || !HW_NAND_Is_Valid_Column_Range( column, length ) )
    {
        return HW_NAND_STATUS_INVALID_ARG;
    }

    HW_NAND_Status_T status = HW_NAND_WriteEnable();
    if ( status != HW_NAND_STATUS_OK )
    {
        return status;
    }

    HW_QSPI_Command_T command =
        HW_NAND_Make_Data_Command( HW_NAND_OPCODE_PROGRAM_LOAD_QUAD, column,
                                   HW_QSPI_ADDR_16_BITS, HW_QSPI_LINES_4, 0U );

    return HW_NAND_Map_QSPI_Status( HW_QSPI_WriteBlocking( &command, data, length ) );
}

HW_NAND_Status_T HW_NAND_ProgramLoadDma( uint16_t column, const uint8_t* data, uint32_t length )
{
    if ( !nand_initialised )
    {
        return HW_NAND_STATUS_NOT_INITIALISED;
    }

    if ( ( data == NULL ) || !HW_NAND_Is_Valid_Column_Range( column, length ) )
    {
        return HW_NAND_STATUS_INVALID_ARG;
    }

    HW_NAND_Status_T status = HW_NAND_WriteEnable();
    if ( status != HW_NAND_STATUS_OK )
    {
        return status;
    }

    HW_QSPI_Command_T command =
        HW_NAND_Make_Data_Command( HW_NAND_OPCODE_PROGRAM_LOAD_QUAD, column,
                                   HW_QSPI_ADDR_16_BITS, HW_QSPI_LINES_4, 0U );

    return HW_NAND_Map_QSPI_Status( HW_QSPI_WriteDma( &command, data, length ) );
}

HW_NAND_Status_T HW_NAND_StartProgramExecute( uint32_t page )
{
    if ( !nand_initialised )
    {
        return HW_NAND_STATUS_NOT_INITIALISED;
    }

    if ( !HW_NAND_Is_Valid_Page( page ) )
    {
        return HW_NAND_STATUS_INVALID_ARG;
    }

    HW_QSPI_Command_T command =
        HW_NAND_Make_Address_Command( HW_NAND_OPCODE_PROGRAM_EXECUTE, page, HW_QSPI_ADDR_24_BITS );
    command.timeout_ms = HW_NAND_PROGRAM_TIMEOUT_MS;

    HW_NAND_Status_T status = HW_NAND_Map_QSPI_Status( HW_QSPI_Command( &command ) );
    if ( status != HW_NAND_STATUS_OK )
    {
        return status;
    }

    return HW_NAND_STATUS_OK;
}

HW_NAND_Status_T HW_NAND_ProgramExecute( uint32_t page )
{
    HW_NAND_Status_T status = HW_NAND_StartProgramExecute( page );
    if ( status != HW_NAND_STATUS_OK )
    {
        return status;
    }

    return HW_NAND_WaitProgramComplete( HW_NAND_PROGRAM_TIMEOUT_MS );
}

HW_NAND_Status_T HW_NAND_ProgramPageBlocking( uint32_t page, uint16_t column, const uint8_t* data,
                                              uint32_t length )
{
    if ( !nand_initialised )
    {
        return HW_NAND_STATUS_NOT_INITIALISED;
    }

    HW_NAND_Status_T status = HW_NAND_ProgramLoadBlocking( column, data, length );
    if ( status != HW_NAND_STATUS_OK )
    {
        return status;
    }

    return HW_NAND_ProgramExecute( page );
}

HW_NAND_Status_T HW_NAND_StartBlockErase( uint32_t block )
{
    if ( !nand_initialised )
    {
        return HW_NAND_STATUS_NOT_INITIALISED;
    }

    if ( !HW_NAND_Is_Valid_Block( block ) )
    {
        return HW_NAND_STATUS_INVALID_ARG;
    }

    HW_NAND_Status_T status = HW_NAND_WriteEnable();
    if ( status != HW_NAND_STATUS_OK )
    {
        return status;
    }

    uint32_t page = block * HW_NAND_PAGES_PER_BLOCK;

    HW_QSPI_Command_T command =
        HW_NAND_Make_Address_Command( HW_NAND_OPCODE_BLOCK_ERASE, page, HW_QSPI_ADDR_24_BITS );
    command.timeout_ms = HW_NAND_BLOCK_ERASE_TIMEOUT_MS;

    status = HW_NAND_Map_QSPI_Status( HW_QSPI_Command( &command ) );
    if ( status != HW_NAND_STATUS_OK )
    {
        return status;
    }

    return HW_NAND_STATUS_OK;
}

HW_NAND_Status_T HW_NAND_BlockErase( uint32_t block )
{
    HW_NAND_Status_T status = HW_NAND_StartBlockErase( block );
    if ( status != HW_NAND_STATUS_OK )
    {
        return status;
    }

    return HW_NAND_WaitBlockEraseComplete( HW_NAND_BLOCK_ERASE_TIMEOUT_MS );
}

HW_NAND_Status_T HW_NAND_IsBlockBad( uint32_t block, bool* is_bad )
{
    if ( !nand_initialised )
    {
        return HW_NAND_STATUS_NOT_INITIALISED;
    }

    if ( ( is_bad == NULL ) || !HW_NAND_Is_Valid_Block( block ) )
    {
        return HW_NAND_STATUS_INVALID_ARG;
    }

    const uint32_t pages_to_check[HW_NAND_BAD_BLOCK_MARKER_PAGE_COUNT] = {
        HW_NAND_Block_First_Page( block ),
        HW_NAND_Block_Second_Page( block ),
        HW_NAND_Block_Last_Page( block ),
    };

    *is_bad = false;

    for ( uint32_t i = 0U; i < HW_NAND_BAD_BLOCK_MARKER_PAGE_COUNT; i++ )
    {
        uint8_t marker = 0xFFU;

        HW_NAND_Status_T status = HW_NAND_StartPageReadToCache( pages_to_check[i] );
        if ( status != HW_NAND_STATUS_OK )
        {
            return status;
        }

        status = HW_NAND_WaitReady( HW_NAND_PAGE_READ_TIMEOUT_MS );
        if ( status != HW_NAND_STATUS_OK )
        {
            return status;
        }

        status = HW_NAND_ReadCacheBlocking( HW_NAND_BAD_BLOCK_MARKER_COLUMN, &marker, 1U );
        if ( status != HW_NAND_STATUS_OK )
        {
            return status;
        }

        if ( marker != 0xFFU )
        {
            *is_bad = true;
            return HW_NAND_STATUS_OK;
        }
    }

    return HW_NAND_STATUS_OK;
}

HW_NAND_Status_T HW_NAND_MarkBlockBad( uint32_t block )
{
    if ( !nand_initialised )
    {
        return HW_NAND_STATUS_NOT_INITIALISED;
    }

    if ( !HW_NAND_Is_Valid_Block( block ) )
    {
        return HW_NAND_STATUS_INVALID_ARG;
    }

    uint8_t marker = HW_NAND_BAD_BLOCK_MARKER_VALUE;

    HW_NAND_Status_T status =
        HW_NAND_ProgramLoadBlocking( HW_NAND_BAD_BLOCK_MARKER_COLUMN, &marker, 1U );
    if ( status != HW_NAND_STATUS_OK )
    {
        return status;
    }

    return HW_NAND_ProgramExecute( HW_NAND_Block_First_Page( block ) );
}

bool HW_NAND_IsTransferComplete( void )
{
    return HW_QSPI_IsTransferComplete();
}

bool HW_NAND_IsBusy( void )
{
    return HW_QSPI_IsBusy();
}
