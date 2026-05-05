/******************************************************************************
 *  File:       hw_qspi.c
 *  Author:     Callum Rafferty
 *  Created:    04/05/2026
 *
 *  Description:
 *      Low level STM32 QSPI peripheral wrapper.
 *
 *  Notes:
 *      This module translates project-level QSPI command descriptions into
 *      STM32 HAL QSPI command structures. It deliberately does not encode
 *      NAND flash command meanings, page geometry, ECC status, or bad block
 *      handling.
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include "hw_qspi.h"

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifndef TEST_BUILD
#include "stm32f4xx_hal.h"
#endif

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */
typedef enum
{
    /** No asynchronous transfer is currently active. */
    HW_QSPI_TRANSFER_IDLE = 0,

    /** A DMA or interrupt based transfer has been started but not completed. */
    HW_QSPI_TRANSFER_ACTIVE,

    /** The most recent asynchronous transfer completed successfully. */
    HW_QSPI_TRANSFER_COMPLETE,

    /** The most recent asynchronous transfer failed or was left in an error state. */
    HW_QSPI_TRANSFER_ERROR
} HW_QSPI_InternalTransferState_T;

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
/** HAL QSPI handle currently owned by this wrapper. NULL means the module is not initialised. */
static QSPI_HandleTypeDef* qspi_handle = NULL;

/** Timeout used when a command does not provide a per-command timeout override. */
static uint32_t qspi_default_timeout_ms = 0U;

/** Tracks asynchronous QSPI transfer progress for DMA completion polling. */
static volatile HW_QSPI_InternalTransferState_T qspi_transfer_state = HW_QSPI_TRANSFER_IDLE;

/**-----------------------------------------------------------------------------
 *  Private (static) Function Prototypes
 *------------------------------------------------------------------------------
 */
static uint32_t         HW_QSPI_Get_Timeout( const HW_QSPI_Command_T* command );
static HW_QSPI_Status_T HW_QSPI_Map_HAL_Status( HAL_StatusTypeDef status );
static uint32_t         HW_QSPI_Get_Instruction_Mode( HW_QSPI_Lines_T lines );
static uint32_t         HW_QSPI_Get_Address_Mode( HW_QSPI_Lines_T lines );
static uint32_t         HW_QSPI_Get_Address_Size( HW_QSPI_AddressSize_T address_size );
static uint32_t         HW_QSPI_Get_Alternate_Bytes_Mode( HW_QSPI_Lines_T lines );
static uint32_t
HW_QSPI_Get_Alternate_Bytes_Size( HW_QSPI_AlternateBytesSize_T alternate_bytes_size );
static uint32_t         HW_QSPI_Get_Data_Mode( HW_QSPI_Lines_T lines );
static bool             HW_QSPI_Is_HAL_Busy_State( HAL_QSPI_StateTypeDef state );
static HW_QSPI_Status_T HW_QSPI_Build_Command( const HW_QSPI_Command_T* command, uint32_t length,
                                               QSPI_CommandTypeDef* qspi_command );
static HW_QSPI_Status_T HW_QSPI_Issue_Command_For_Data( const HW_QSPI_Command_T* command,
                                                        uint32_t                 length,
                                                        QSPI_CommandTypeDef*     qspi_command );

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

/**
 * @brief Gets the timeout to use for a QSPI transaction.
 *
 * @param command Command description that may provide a per-command timeout.
 *
 * @return command->timeout_ms when non-zero, otherwise qspi_default_timeout_ms.
 */
static uint32_t HW_QSPI_Get_Timeout( const HW_QSPI_Command_T* command )
{
    if ( ( command != NULL ) && ( command->timeout_ms != 0U ) )
    {
        return command->timeout_ms;
    }

    return qspi_default_timeout_ms;
}

/**
 * @brief Converts an STM32 HAL status into this module's public status enum.
 *
 * @param status HAL status returned by a QSPI HAL call.
 *
 * @return Equivalent HW_QSPI status.
 */
static HW_QSPI_Status_T HW_QSPI_Map_HAL_Status( HAL_StatusTypeDef status )
{
    switch ( status )
    {
        case HAL_OK:
            return HW_QSPI_STATUS_OK;
        case HAL_BUSY:
            return HW_QSPI_STATUS_BUSY;
        case HAL_TIMEOUT:
            return HW_QSPI_STATUS_TIMEOUT;
        case HAL_ERROR:
        default:
            return HW_QSPI_STATUS_ERROR;
    }
}

/**
 * @brief Converts a project-level instruction line count into a HAL instruction mode.
 *
 * @param lines Number of QSPI lines used by the instruction phase.
 *
 * @return STM32 HAL QSPI instruction mode constant.
 */
static uint32_t HW_QSPI_Get_Instruction_Mode( HW_QSPI_Lines_T lines )
{
    switch ( lines )
    {
        case HW_QSPI_LINES_1:
            return QSPI_INSTRUCTION_1_LINE;
        case HW_QSPI_LINES_2:
            return QSPI_INSTRUCTION_2_LINES;
        case HW_QSPI_LINES_4:
            return QSPI_INSTRUCTION_4_LINES;
        case HW_QSPI_LINES_NONE:
        default:
            return QSPI_INSTRUCTION_NONE;
    }
}

/**
 * @brief Converts a project-level address line count into a HAL address mode.
 *
 * @param lines Number of QSPI lines used by the address phase.
 *
 * @return STM32 HAL QSPI address mode constant.
 */
static uint32_t HW_QSPI_Get_Address_Mode( HW_QSPI_Lines_T lines )
{
    switch ( lines )
    {
        case HW_QSPI_LINES_1:
            return QSPI_ADDRESS_1_LINE;
        case HW_QSPI_LINES_2:
            return QSPI_ADDRESS_2_LINES;
        case HW_QSPI_LINES_4:
            return QSPI_ADDRESS_4_LINES;
        case HW_QSPI_LINES_NONE:
        default:
            return QSPI_ADDRESS_NONE;
    }
}

/**
 * @brief Converts a project-level address size into a HAL address size.
 *
 * @param address_size Number of address bits sent during the address phase.
 *
 * @return STM32 HAL QSPI address size constant.
 */
static uint32_t HW_QSPI_Get_Address_Size( HW_QSPI_AddressSize_T address_size )
{
    switch ( address_size )
    {
        case HW_QSPI_ADDR_8_BITS:
            return QSPI_ADDRESS_8_BITS;
        case HW_QSPI_ADDR_16_BITS:
            return QSPI_ADDRESS_16_BITS;
        case HW_QSPI_ADDR_24_BITS:
            return QSPI_ADDRESS_24_BITS;
        case HW_QSPI_ADDR_32_BITS:
            return QSPI_ADDRESS_32_BITS;
        case HW_QSPI_ADDR_NONE:
        default:
            return QSPI_ADDRESS_8_BITS;
    }
}

/**
 * @brief Converts a project-level alternate-byte line count into a HAL alternate-byte mode.
 *
 * @param lines Number of QSPI lines used by the alternate-byte phase.
 *
 * @return STM32 HAL QSPI alternate-byte mode constant.
 */
static uint32_t HW_QSPI_Get_Alternate_Bytes_Mode( HW_QSPI_Lines_T lines )
{
    switch ( lines )
    {
        case HW_QSPI_LINES_1:
            return QSPI_ALTERNATE_BYTES_1_LINE;
        case HW_QSPI_LINES_2:
            return QSPI_ALTERNATE_BYTES_2_LINES;
        case HW_QSPI_LINES_4:
            return QSPI_ALTERNATE_BYTES_4_LINES;
        case HW_QSPI_LINES_NONE:
        default:
            return QSPI_ALTERNATE_BYTES_NONE;
    }
}

/**
 * @brief Converts a project-level alternate-byte size into a HAL alternate-byte size.
 *
 * @param alternate_bytes_size Number of alternate-byte bits sent during the alternate-byte phase.
 *
 * @return STM32 HAL QSPI alternate-byte size constant.
 */
static uint32_t
HW_QSPI_Get_Alternate_Bytes_Size( HW_QSPI_AlternateBytesSize_T alternate_bytes_size )
{
    switch ( alternate_bytes_size )
    {
        case HW_QSPI_ALT_BYTES_8_BITS:
            return QSPI_ALTERNATE_BYTES_8_BITS;
        case HW_QSPI_ALT_BYTES_16_BITS:
            return QSPI_ALTERNATE_BYTES_16_BITS;
        case HW_QSPI_ALT_BYTES_24_BITS:
            return QSPI_ALTERNATE_BYTES_24_BITS;
        case HW_QSPI_ALT_BYTES_32_BITS:
            return QSPI_ALTERNATE_BYTES_32_BITS;
        case HW_QSPI_ALT_BYTES_NONE:
        default:
            return QSPI_ALTERNATE_BYTES_8_BITS;
    }
}

/**
 * @brief Converts a project-level data line count into a HAL data mode.
 *
 * @param lines Number of QSPI lines used by the data phase.
 *
 * @return STM32 HAL QSPI data mode constant.
 */
static uint32_t HW_QSPI_Get_Data_Mode( HW_QSPI_Lines_T lines )
{
    switch ( lines )
    {
        case HW_QSPI_LINES_1:
            return QSPI_DATA_1_LINE;
        case HW_QSPI_LINES_2:
            return QSPI_DATA_2_LINES;
        case HW_QSPI_LINES_4:
            return QSPI_DATA_4_LINES;
        case HW_QSPI_LINES_NONE:
        default:
            return QSPI_DATA_NONE;
    }
}

/**
 * @brief Determines whether a HAL QSPI state represents an active operation.
 *
 * @param state HAL QSPI state to inspect.
 *
 * @return true when the HAL state is busy or aborting, otherwise false.
 */
static bool HW_QSPI_Is_HAL_Busy_State( HAL_QSPI_StateTypeDef state )
{
    switch ( state )
    {
        case HAL_QSPI_STATE_BUSY:
        case HAL_QSPI_STATE_BUSY_INDIRECT_TX:
        case HAL_QSPI_STATE_BUSY_INDIRECT_RX:
        case HAL_QSPI_STATE_BUSY_AUTO_POLLING:
        case HAL_QSPI_STATE_BUSY_MEM_MAPPED:
        case HAL_QSPI_STATE_ABORT:
            return true;
        case HAL_QSPI_STATE_RESET:
        case HAL_QSPI_STATE_READY:
        case HAL_QSPI_STATE_ERROR:
        default:
            return false;
    }
}

/**
 * @brief Builds an STM32 HAL QSPI command from the project-level command description.
 *
 * @param command      Project-level QSPI command description.
 * @param length       Number of data bytes for the transaction. Zero disables the data phase.
 * @param qspi_command Destination HAL command structure.
 *
 * @return HW_QSPI_STATUS_OK when the HAL command is built, otherwise HW_QSPI_STATUS_INVALID_ARG.
 */
static HW_QSPI_Status_T HW_QSPI_Build_Command( const HW_QSPI_Command_T* command, uint32_t length,
                                               QSPI_CommandTypeDef* qspi_command )
{
    if ( ( command == NULL ) || ( qspi_command == NULL ) )
    {
        return HW_QSPI_STATUS_INVALID_ARG;
    }

    *qspi_command = ( QSPI_CommandTypeDef ){ 0 };

    qspi_command->Instruction     = command->instruction;
    qspi_command->InstructionMode = HW_QSPI_Get_Instruction_Mode( command->instruction_lines );

    qspi_command->Address     = command->address;
    qspi_command->AddressMode = HW_QSPI_Get_Address_Mode( command->address_lines );
    qspi_command->AddressSize = HW_QSPI_Get_Address_Size( command->address_size );

    qspi_command->AlternateBytes = command->alternate_bytes;
    qspi_command->AlternateByteMode =
        HW_QSPI_Get_Alternate_Bytes_Mode( command->alternate_bytes_lines );
    qspi_command->AlternateBytesSize =
        HW_QSPI_Get_Alternate_Bytes_Size( command->alternate_bytes_size );

    qspi_command->DummyCycles = command->dummy_cycles;

    if ( length == 0U )
    {
        qspi_command->DataMode = QSPI_DATA_NONE;
        qspi_command->NbData   = 0U;
    }
    else
    {
        qspi_command->DataMode = HW_QSPI_Get_Data_Mode( command->data_lines );
        qspi_command->NbData   = length;
    }

    qspi_command->DdrMode          = QSPI_DDR_MODE_DISABLE;
    qspi_command->DdrHoldHalfCycle = QSPI_DDR_HHC_ANALOG_DELAY;
    qspi_command->SIOOMode         = QSPI_SIOO_INST_EVERY_CMD;

    return HW_QSPI_STATUS_OK;
}

/**
 * @brief Builds and issues a HAL command that will be followed by a data phase.
 *
 * @param command      Project-level QSPI command description.
 * @param length       Number of data bytes that will be transferred.
 * @param qspi_command Destination HAL command structure.
 *
 * @return HW_QSPI_STATUS_OK when the command phase succeeds, otherwise an error status.
 */
static HW_QSPI_Status_T HW_QSPI_Issue_Command_For_Data( const HW_QSPI_Command_T* command,
                                                        uint32_t                 length,
                                                        QSPI_CommandTypeDef*     qspi_command )
{
    HW_QSPI_Status_T status = HW_QSPI_Build_Command( command, length, qspi_command );
    if ( status != HW_QSPI_STATUS_OK )
    {
        return status;
    }

    return HW_QSPI_Map_HAL_Status(
        HAL_QSPI_Command( qspi_handle, qspi_command, HW_QSPI_Get_Timeout( command ) ) );
}

/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */

HW_QSPI_Status_T HW_QSPI_Init( const HW_QSPI_Config_T* config )
{
    if ( ( config == NULL ) || ( config->hal_handle == NULL ) )
    {
        return HW_QSPI_STATUS_INVALID_ARG;
    }

    config->hal_handle->Init.ClockPrescaler     = config->clock_prescaler;
    config->hal_handle->Init.FifoThreshold      = config->fifo_threshold;
    config->hal_handle->Init.SampleShifting     = config->sample_shifting;
    config->hal_handle->Init.FlashSize          = config->flash_size;
    config->hal_handle->Init.ChipSelectHighTime = config->chip_select_high_time;
    config->hal_handle->Init.ClockMode          = config->clock_mode;
    config->hal_handle->Init.FlashID            = QSPI_FLASH_ID_1;
    config->hal_handle->Init.DualFlash          = QSPI_DUALFLASH_DISABLE;

    HW_QSPI_Status_T status = HW_QSPI_Map_HAL_Status( HAL_QSPI_Init( config->hal_handle ) );
    if ( status != HW_QSPI_STATUS_OK )
    {
        qspi_handle             = NULL;
        qspi_default_timeout_ms = 0U;
        qspi_transfer_state     = HW_QSPI_TRANSFER_ERROR;
        return status;
    }

    qspi_handle             = config->hal_handle;
    qspi_default_timeout_ms = config->default_timeout_ms;
    qspi_transfer_state     = HW_QSPI_TRANSFER_IDLE;

    return HW_QSPI_STATUS_OK;
}

HW_QSPI_Status_T HW_QSPI_Command( const HW_QSPI_Command_T* command )
{
    if ( qspi_handle == NULL )
    {
        return HW_QSPI_STATUS_NOT_INITIALISED;
    }

    if ( command == NULL )
    {
        return HW_QSPI_STATUS_INVALID_ARG;
    }

    if ( HW_QSPI_IsBusy() )
    {
        return HW_QSPI_STATUS_BUSY;
    }

    QSPI_CommandTypeDef qspi_command = { 0 };

    HW_QSPI_Status_T status = HW_QSPI_Build_Command( command, 0U, &qspi_command );
    if ( status != HW_QSPI_STATUS_OK )
    {
        return status;
    }

    status = HW_QSPI_Map_HAL_Status(
        HAL_QSPI_Command( qspi_handle, &qspi_command, HW_QSPI_Get_Timeout( command ) ) );
    qspi_transfer_state =
        ( status == HW_QSPI_STATUS_OK ) ? HW_QSPI_TRANSFER_IDLE : HW_QSPI_TRANSFER_ERROR;

    return status;
}

HW_QSPI_Status_T HW_QSPI_ReadBlocking( const HW_QSPI_Command_T* command, uint8_t* data,
                                       uint32_t length )
{
    if ( qspi_handle == NULL )
    {
        return HW_QSPI_STATUS_NOT_INITIALISED;
    }

    if ( ( command == NULL ) || ( data == NULL ) || ( length == 0U ) )
    {
        return HW_QSPI_STATUS_INVALID_ARG;
    }

    if ( HW_QSPI_IsBusy() )
    {
        return HW_QSPI_STATUS_BUSY;
    }

    QSPI_CommandTypeDef qspi_command = { 0 };

    HW_QSPI_Status_T status = HW_QSPI_Issue_Command_For_Data( command, length, &qspi_command );
    if ( status != HW_QSPI_STATUS_OK )
    {
        qspi_transfer_state = HW_QSPI_TRANSFER_ERROR;
        return status;
    }

    qspi_transfer_state = HW_QSPI_TRANSFER_ACTIVE;
    status              = HW_QSPI_Map_HAL_Status(
        HAL_QSPI_Receive( qspi_handle, data, HW_QSPI_Get_Timeout( command ) ) );
    qspi_transfer_state =
        ( status == HW_QSPI_STATUS_OK ) ? HW_QSPI_TRANSFER_IDLE : HW_QSPI_TRANSFER_ERROR;

    return status;
}

HW_QSPI_Status_T HW_QSPI_WriteBlocking( const HW_QSPI_Command_T* command, const uint8_t* data,
                                        uint32_t length )
{
    if ( qspi_handle == NULL )
    {
        return HW_QSPI_STATUS_NOT_INITIALISED;
    }

    if ( ( command == NULL ) || ( data == NULL ) || ( length == 0U ) )
    {
        return HW_QSPI_STATUS_INVALID_ARG;
    }

    if ( HW_QSPI_IsBusy() )
    {
        return HW_QSPI_STATUS_BUSY;
    }

    QSPI_CommandTypeDef qspi_command = { 0 };

    HW_QSPI_Status_T status = HW_QSPI_Issue_Command_For_Data( command, length, &qspi_command );
    if ( status != HW_QSPI_STATUS_OK )
    {
        qspi_transfer_state = HW_QSPI_TRANSFER_ERROR;
        return status;
    }

    qspi_transfer_state = HW_QSPI_TRANSFER_ACTIVE;
    status              = HW_QSPI_Map_HAL_Status(
        HAL_QSPI_Transmit( qspi_handle, ( uint8_t* )data, HW_QSPI_Get_Timeout( command ) ) );
    qspi_transfer_state =
        ( status == HW_QSPI_STATUS_OK ) ? HW_QSPI_TRANSFER_IDLE : HW_QSPI_TRANSFER_ERROR;

    return status;
}

HW_QSPI_Status_T HW_QSPI_WriteDma( const HW_QSPI_Command_T* command, const uint8_t* data,
                                   uint32_t length )
{
    if ( qspi_handle == NULL )
    {
        return HW_QSPI_STATUS_NOT_INITIALISED;
    }

    if ( ( command == NULL ) || ( data == NULL ) || ( length == 0U ) )
    {
        return HW_QSPI_STATUS_INVALID_ARG;
    }

    if ( HW_QSPI_IsBusy() )
    {
        return HW_QSPI_STATUS_BUSY;
    }

    QSPI_CommandTypeDef qspi_command = { 0 };

    HW_QSPI_Status_T status = HW_QSPI_Issue_Command_For_Data( command, length, &qspi_command );
    if ( status != HW_QSPI_STATUS_OK )
    {
        qspi_transfer_state = HW_QSPI_TRANSFER_ERROR;
        return status;
    }

    qspi_transfer_state = HW_QSPI_TRANSFER_ACTIVE;
    status = HW_QSPI_Map_HAL_Status( HAL_QSPI_Transmit_DMA( qspi_handle, ( uint8_t* )data ) );
    if ( status != HW_QSPI_STATUS_OK )
    {
        qspi_transfer_state = HW_QSPI_TRANSFER_ERROR;
    }

    return status;
}

HW_QSPI_Status_T HW_QSPI_ReadDma( const HW_QSPI_Command_T* command, uint8_t* data, uint32_t length )
{
    if ( qspi_handle == NULL )
    {
        return HW_QSPI_STATUS_NOT_INITIALISED;
    }

    if ( ( command == NULL ) || ( data == NULL ) || ( length == 0U ) )
    {
        return HW_QSPI_STATUS_INVALID_ARG;
    }

    if ( HW_QSPI_IsBusy() )
    {
        return HW_QSPI_STATUS_BUSY;
    }

    QSPI_CommandTypeDef qspi_command = { 0 };

    HW_QSPI_Status_T status = HW_QSPI_Issue_Command_For_Data( command, length, &qspi_command );
    if ( status != HW_QSPI_STATUS_OK )
    {
        qspi_transfer_state = HW_QSPI_TRANSFER_ERROR;
        return status;
    }

    qspi_transfer_state = HW_QSPI_TRANSFER_ACTIVE;
    status              = HW_QSPI_Map_HAL_Status( HAL_QSPI_Receive_DMA( qspi_handle, data ) );
    if ( status != HW_QSPI_STATUS_OK )
    {
        qspi_transfer_state = HW_QSPI_TRANSFER_ERROR;
    }

    return status;
}

bool HW_QSPI_IsTransferComplete( void )
{
    return qspi_transfer_state == HW_QSPI_TRANSFER_COMPLETE;
}

bool HW_QSPI_IsBusy( void )
{
    if ( qspi_handle == NULL )
    {
        return false;
    }

    if ( qspi_transfer_state == HW_QSPI_TRANSFER_ACTIVE )
    {
        return true;
    }

    return HW_QSPI_Is_HAL_Busy_State( HAL_QSPI_GetState( qspi_handle ) );
}

HW_QSPI_Status_T HW_QSPI_Abort( void )
{
    if ( qspi_handle == NULL )
    {
        return HW_QSPI_STATUS_NOT_INITIALISED;
    }

    HW_QSPI_Status_T status = HW_QSPI_Map_HAL_Status( HAL_QSPI_Abort( qspi_handle ) );
    qspi_transfer_state =
        ( status == HW_QSPI_STATUS_OK ) ? HW_QSPI_TRANSFER_IDLE : HW_QSPI_TRANSFER_ERROR;

    return status;
}

void HAL_QSPI_TxCpltCallback( QSPI_HandleTypeDef* hqspi )
{
    if ( hqspi == qspi_handle )
    {
        qspi_transfer_state = HW_QSPI_TRANSFER_COMPLETE;
    }
}

void HAL_QSPI_RxCpltCallback( QSPI_HandleTypeDef* hqspi )
{
    if ( hqspi == qspi_handle )
    {
        qspi_transfer_state = HW_QSPI_TRANSFER_COMPLETE;
    }
}

void HAL_QSPI_ErrorCallback( QSPI_HandleTypeDef* hqspi )
{
    if ( hqspi == qspi_handle )
    {
        qspi_transfer_state = HW_QSPI_TRANSFER_ERROR;
    }
}

void HAL_QSPI_AbortCpltCallback( QSPI_HandleTypeDef* hqspi )
{
    if ( hqspi == qspi_handle )
    {
        qspi_transfer_state = HW_QSPI_TRANSFER_IDLE;
    }
}
