/******************************************************************************
 *  File:       exec_i2c.c
 *  Author:     Coen Pasitchnyj
 *  Created:    20-Apr-2026
 *
 *  Description:
 *      <Short description of the module's purpose and responsibilities>
 *
 *  Notes:
 *      <Any design notes, dependencies, or assumptions go here>
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include "exec_i2c.h"
#include "hw_i2c.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */

#define EXEC_I2C_INTERNAL_FMPI2C1_OWN_ADDRESS_7BIT ( 0x33U )

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

/**-----------------------------------------------------------------------------
 *  Private (static) Function Prototypes
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

static inline HWI2CMode_T EXEC_I2C_To_HW_Mode( EXECI2CMode_T mode )
{
    return ( mode == EXEC_I2C_MODE_MASTER ) ? HW_I2C_MODE_MASTER : HW_I2C_MODE_SLAVE;
}

static inline HWI2CSpeed_T EXEC_I2C_To_HW_Speed( EXECI2CSpeed_T speed )
{
    return ( speed == EXEC_I2C_SPEED_400KHZ ) ? HW_I2C_SPEED_400KHZ : HW_I2C_SPEED_100KHZ;
}

static inline HWI2CTransferPath_T EXEC_I2C_To_HW_Path( EXECI2CTransferPath_T path )
{
    return ( path == EXEC_I2C_TRANSFER_DMA ) ? HW_I2C_TRANSFER_DMA : HW_I2C_TRANSFER_INTERRUPT;
}

static inline EXECI2CStatus_T EXEC_I2C_From_HW_Status( HWI2CStatus_T status )
{
    switch ( status )
    {
        case HW_I2C_STATUS_OK:
            return EXEC_I2C_STATUS_OK;
        case HW_I2C_STATUS_BUSY:
            return EXEC_I2C_STATUS_BUSY;
        case HW_I2C_STATUS_INVALID_PARAM:
        case HW_I2C_STATUS_NOT_CONFIGURED:
            return EXEC_I2C_STATUS_INVALID_PARAM;
        case HW_I2C_STATUS_OVERFLOW:
            return EXEC_I2C_STATUS_OVERFLOW;
        case HW_I2C_STATUS_ERROR:
        default:
            return EXEC_I2C_STATUS_ERROR;
    }
}

static EXECI2CStatus_T EXEC_I2C_Validate_Config( HWI2CChannel_T                channel,
                                                 const EXECI2CChannelConfig_T* config )
{
    if ( config == NULL )
    {
        return EXEC_I2C_STATUS_INVALID_PARAM;
    }

    if ( ( config->mode != EXEC_I2C_MODE_MASTER ) && ( config->mode != EXEC_I2C_MODE_SLAVE ) )
    {
        return EXEC_I2C_STATUS_INVALID_PARAM;
    }

    if ( ( config->speed != EXEC_I2C_SPEED_100KHZ ) && ( config->speed != EXEC_I2C_SPEED_400KHZ ) )
    {
        return EXEC_I2C_STATUS_INVALID_PARAM;
    }

    if ( ( config->tx_transfer_path != EXEC_I2C_TRANSFER_INTERRUPT )
         && ( config->tx_transfer_path != EXEC_I2C_TRANSFER_DMA ) )
    {
        return EXEC_I2C_STATUS_INVALID_PARAM;
    }

    if ( ( config->rx_transfer_path != EXEC_I2C_TRANSFER_INTERRUPT )
         && ( config->rx_transfer_path != EXEC_I2C_TRANSFER_DMA ) )
    {
        return EXEC_I2C_STATUS_INVALID_PARAM;
    }

    if ( config->own_address_7bit > 0x7FU )
    {
        return EXEC_I2C_STATUS_INVALID_PARAM;
    }

    if ( channel == HW_I2C_CHANNEL_1 )
    {
        if ( ( config->tx_transfer_path == EXEC_I2C_TRANSFER_DMA )
             || ( config->rx_transfer_path == EXEC_I2C_TRANSFER_DMA ) )
        {
            return EXEC_I2C_STATUS_INVALID_PARAM;
        }
    }

    return EXEC_I2C_STATUS_OK;
}

/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */

EXECI2CStatus_T EXEC_I2C_Configuration( const EXECI2CChannelConfig_T* i2c1_config,
                                        const EXECI2CChannelConfig_T* i2c2_config,
                                        uint16_t internal_fmpi2c1_own_address_7bit )
{
    EXECI2CStatus_T status_1 = EXEC_I2C_Validate_Config( HW_I2C_CHANNEL_1, i2c1_config );
    EXECI2CStatus_T status_2 = EXEC_I2C_Validate_Config( HW_I2C_CHANNEL_2, i2c2_config );

    if ( ( status_1 != EXEC_I2C_STATUS_OK ) || ( status_2 != EXEC_I2C_STATUS_OK )
         || ( internal_fmpi2c1_own_address_7bit > 0x7FU ) )
    {
        return EXEC_I2C_STATUS_INVALID_PARAM;
    }

    HWI2CChannelConfig_T hw_i2c1_config = {
        .mode             = EXEC_I2C_To_HW_Mode( i2c1_config->mode ),
        .speed            = EXEC_I2C_To_HW_Speed( i2c1_config->speed ),
        .tx_transfer_path = EXEC_I2C_To_HW_Path( i2c1_config->tx_transfer_path ),
        .rx_transfer_path = EXEC_I2C_To_HW_Path( i2c1_config->rx_transfer_path ),
        .own_address_7bit = i2c1_config->own_address_7bit,
    };

    HWI2CChannelConfig_T hw_i2c2_config = {
        .mode             = EXEC_I2C_To_HW_Mode( i2c2_config->mode ),
        .speed            = EXEC_I2C_To_HW_Speed( i2c2_config->speed ),
        .tx_transfer_path = EXEC_I2C_To_HW_Path( i2c2_config->tx_transfer_path ),
        .rx_transfer_path = EXEC_I2C_To_HW_Path( i2c2_config->rx_transfer_path ),
        .own_address_7bit = i2c2_config->own_address_7bit,
    };

    HWI2CStatus_T hw_status = HW_I2C_Configure_Channel( HW_I2C_CHANNEL_1, &hw_i2c1_config );
    if ( hw_status != HW_I2C_STATUS_OK )
    {
        return EXEC_I2C_From_HW_Status( hw_status );
    }

    hw_status = HW_I2C_Configure_Channel( HW_I2C_CHANNEL_2, &hw_i2c2_config );
    if ( hw_status != HW_I2C_STATUS_OK )
    {
        return EXEC_I2C_From_HW_Status( hw_status );
    }

    hw_status = HW_I2C_Configure_Internal_FMPI2C1( internal_fmpi2c1_own_address_7bit );
    return EXEC_I2C_From_HW_Status( hw_status );
}

EXECI2CStatus_T EXEC_I2C_Configuration_Internal( void )
{
    return EXEC_I2C_From_HW_Status(
        HW_I2C_Configure_Internal_FMPI2C1( EXEC_I2C_INTERNAL_FMPI2C1_OWN_ADDRESS_7BIT ) );
}

EXECI2CStatus_T EXEC_I2C_Master_Send( HWI2CChannel_T channel,
                                      uint16_t device_address_7bit, const uint8_t* payload,
                                      uint16_t payload_length )
{
    HWI2CStatus_T hw_status = HW_I2C_Load_Stage_Buffer( channel, payload, payload_length );

    hw_status = HW_I2C_Trigger_Master_Transmit( channel, device_address_7bit );
    return EXEC_I2C_From_HW_Status( hw_status );
}

EXECI2CStatus_T EXEC_I2C_Internal_Master_Send( uint16_t device_address_7bit, const uint8_t* payload,
                                               uint16_t payload_length )
{
    HWI2CStatus_T hw_status =
        HW_I2C_Load_Stage_Buffer( HW_I2C_CHANNEL_FMPI2C1, payload, payload_length );

    // DELETING THIS BREAKS EXPANDER
    if ( hw_status != HW_I2C_STATUS_OK )
    {
        return EXEC_I2C_From_HW_Status( hw_status );
    }

    hw_status = HW_I2C_Trigger_Master_Transmit( HW_I2C_CHANNEL_FMPI2C1, device_address_7bit );
    return EXEC_I2C_From_HW_Status( hw_status );
}

EXECI2CStatus_T EXEC_I2C_Slave_Send( HWI2CChannel_T channel, const uint8_t* payload,
                                     uint16_t payload_length )
{
    HWI2CStatus_T hw_status = HW_I2C_Load_Stage_Buffer( channel, payload, payload_length );

    hw_status = HW_I2C_Trigger_Slave_Transmit( channel );
    return EXEC_I2C_From_HW_Status( hw_status );
}

EXECI2CStatus_T EXEC_I2C_Start_Master_Receive( HWI2CChannel_T channel,
                                               uint16_t                 device_address_7bit,
                                               uint16_t                 expected_length )
{
    return EXEC_I2C_From_HW_Status(
        HW_I2C_Trigger_Master_Receive( channel, device_address_7bit, expected_length ) );
}

EXECI2CStatus_T EXEC_I2C_Start_Slave_Receive( HWI2CChannel_T channel,
                                              uint16_t                 expected_length )
{
    return EXEC_I2C_From_HW_Status( HW_I2C_Trigger_Slave_Receive( channel, expected_length ) );
}

EXECI2CStatus_T EXEC_I2C_Receive_Copy_And_Consume( HWI2CChannel_T channel,
                                                   uint8_t*                 result_storage,
                                                   uint16_t                 result_storage_capacity,
                                                   uint16_t*                bytes_copied )
{
    *bytes_copied = 0U;

    HWI2CRxPeek_T peek      = { 0 };
    HWI2CStatus_T hw_status = HW_I2C_Peek_Received( channel, &peek );
    if ( hw_status != HW_I2C_STATUS_OK )
    {
        return EXEC_I2C_From_HW_Status( hw_status );
    }

    uint16_t bytes_to_copy = peek.total_length;
    if ( bytes_to_copy > result_storage_capacity )
    {
        bytes_to_copy = result_storage_capacity;
    }

    uint16_t copied = 0U;
    for ( uint16_t idx = 0U; ( idx < peek.first.length ) && ( copied < bytes_to_copy ); ++idx )
    {
        result_storage[copied] = peek.first.data[idx];
        ++copied;
    }

    for ( uint16_t idx = 0U; ( idx < peek.second.length ) && ( copied < bytes_to_copy ); ++idx )
    {
        result_storage[copied] = peek.second.data[idx];
        ++copied;
    }

    hw_status = HW_I2C_Consume_Received( channel, copied );
    if ( hw_status != HW_I2C_STATUS_OK )
    {
        return EXEC_I2C_From_HW_Status( hw_status );
    }

    *bytes_copied = copied;
    return EXEC_I2C_From_HW_Status( hw_status );
}
