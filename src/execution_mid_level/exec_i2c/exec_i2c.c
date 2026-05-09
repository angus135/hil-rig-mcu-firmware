/******************************************************************************
 *  File:       exec_i2c.c
 *  Author:     Coen Pasitchnyj
 *  Created:    20-Apr-2026
 *
 *  Description:
 *      Mid-level I2C execution layer implementation. Validates I2C configuration
 *      and transfer requests before delegating to the low-level hw_i2c driver.
 *      Provides a simplified API by handling buffer management and parameter validation.
 *
 *  Notes:
 *      - Configuration validation includes mode, speed, transfer path, and address checks
 *      - I2C1 does not support DMA transfers (interrupt-only)
 *      - I2C2 supports both interrupt and DMA transfer paths
 *      - FMPI2C1 is internal, interrupt-only, with fixed 100 kHz speed
 *      - All functions delegate to hw_i2c for actual hardware operations
 *      - Thread-safety must be ensured at higher layers
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

    if ( ( config->mode != HW_I2C_MODE_MASTER ) && ( config->mode != HW_I2C_MODE_SLAVE ) )
    {
        return EXEC_I2C_STATUS_INVALID_PARAM;
    }

    if ( ( config->speed != HW_I2C_SPEED_100KHZ ) && ( config->speed != HW_I2C_SPEED_400KHZ ) )
    {
        return EXEC_I2C_STATUS_INVALID_PARAM;
    }

        if ( ( config->tx_transfer_path != HW_I2C_TRANSFER_INTERRUPT )
            && ( config->tx_transfer_path != HW_I2C_TRANSFER_DMA ) )
    {
        return EXEC_I2C_STATUS_INVALID_PARAM;
    }

        if ( ( config->rx_transfer_path != HW_I2C_TRANSFER_INTERRUPT )
            && ( config->rx_transfer_path != HW_I2C_TRANSFER_DMA ) )
    {
        return EXEC_I2C_STATUS_INVALID_PARAM;
    }

    if ( config->own_address_7bit > 0x7FU )
    {
        return EXEC_I2C_STATUS_INVALID_PARAM;
    }

    if ( channel == HW_I2C_CHANNEL_1 )
    {
           if ( ( config->tx_transfer_path == HW_I2C_TRANSFER_DMA )
               || ( config->rx_transfer_path == HW_I2C_TRANSFER_DMA ) )
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

/**
 * @brief Configure all I2C channels with validation.
 *
 * Validates configuration parameters for both external channels (I2C1 and I2C2)
 * and the internal FMPI2C1 channel, then delegates configuration to hw_i2c.
 * Must be called before any transfers.
 *
 * @param[in] i2c1_config                           Configuration for I2C1 channel
 * @param[in] i2c2_config                           Configuration for I2C2 channel
 * @param[in] internal_fmpi2c1_own_address_7bit     Own address for FMPI2C1
 *
 * @return EXEC_I2C_STATUS_OK on success
 * @return EXEC_I2C_STATUS_INVALID_PARAM if any parameter is invalid
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
        .mode             = i2c1_config->mode,
        .speed            = i2c1_config->speed,
        .tx_transfer_path = i2c1_config->tx_transfer_path,
        .rx_transfer_path = i2c1_config->rx_transfer_path,
        .own_address_7bit = i2c1_config->own_address_7bit,
    };

    HWI2CChannelConfig_T hw_i2c2_config = {
        .mode             = i2c2_config->mode,
        .speed            = i2c2_config->speed,
        .tx_transfer_path = i2c2_config->tx_transfer_path,
        .rx_transfer_path = i2c2_config->rx_transfer_path,
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

/**
 * @brief Configure the internal FMPI2C1 channel with default settings.
 *
 * Initializes FMPI2C1 with a predefined own address (0x33).
 * Convenience function for internal channel initialization.
 *
 * @return EXEC_I2C_STATUS_OK on success
 * @return EXEC_I2C_STATUS_ERROR on failure
 */
EXECI2CStatus_T EXEC_I2C_Configuration_Internal( void )
{
    return EXEC_I2C_From_HW_Status(
        HW_I2C_Configure_Internal_FMPI2C1( EXEC_I2C_INTERNAL_FMPI2C1_OWN_ADDRESS_7BIT ) );
}

/**
 * @brief Master transmit on an external channel.
 *
 * Sends data to a slave device on the specified channel.
 * Data must be provided directly in the payload; internally handles buffering.
 *
 * @param[in] channel               I2C channel (HW_I2C_CHANNEL_1 or HW_I2C_CHANNEL_2)
 * @param[in] device_address_7bit   7-bit slave address
 * @param[in] payload               Data to transmit
 * @param[in] payload_length        Number of bytes to transmit
 *
 * @return true if transmission was initiated
 * @return false on failure
 */
bool EXEC_I2C_Master_Send( HWI2CChannel_T channel, uint16_t device_address_7bit,
                           const uint8_t* payload, uint16_t payload_length )
{
    bool is_ok = HW_I2C_Load_Stage_Buffer( channel, payload, payload_length );
    if ( !is_ok )
    {
        return false;
    }

    is_ok = HW_I2C_Trigger_Master_Transmit_External( channel, device_address_7bit );
    return is_ok;
}

/**
 * @brief Master transmit on the internal FMPI2C1 channel.
 *
 * Sends data to a slave device on the internal FMPI2C1 channel.
 * Data must be provided directly in the payload; internally handles buffering.
 *
 * @param[in] device_address_7bit   7-bit slave address
 * @param[in] payload               Data to transmit
 * @param[in] payload_length        Number of bytes to transmit
 *
 * @return true if transmission was initiated
 * @return false on failure
 */
bool EXEC_I2C_Internal_Master_Send( uint16_t device_address_7bit, const uint8_t* payload,
                                    uint16_t payload_length )
{
    bool is_ok = HW_I2C_Load_Stage_Buffer( HW_I2C_CHANNEL_FMPI2C1, payload, payload_length );

    // DELETING THIS BREAKS EXPANDER
    if ( !is_ok )
    {
        return false;
    }

    is_ok = HW_I2C_Trigger_Master_Transmit_Internal( HW_I2C_CHANNEL_FMPI2C1, device_address_7bit );
    return is_ok;
}

/**
 * @brief Slave transmit on an external channel.
 *
 * Prepares the channel to respond to a master read request with the provided data.
 *
 * @param[in] channel               I2C channel
 * @param[in] payload               Data to transmit when master requests
 * @param[in] payload_length        Number of bytes available to transmit
 *
 * @return true if slave transmit was prepared
 * @return false on failure
 */
bool EXEC_I2C_Slave_Send( HWI2CChannel_T channel, const uint8_t* payload,
                          uint16_t payload_length )
{
    bool is_ok = HW_I2C_Load_Stage_Buffer( channel, payload, payload_length );
    if ( !is_ok )
    {
        return false;
    }

    is_ok = HW_I2C_Trigger_Slave_Transmit( channel );
    return is_ok;
}

/**
 * @brief Initiate master receive on an external channel.
 *
 * Requests data from a slave device on an external channel (I2C1 or I2C2).
 * Received data is buffered internally and can be retrieved with
 * `EXEC_I2C_Receive_Copy_And_Consume()`.
 *
 * @param[in] channel               External I2C channel
 * @param[in] device_address_7bit   7-bit slave address
 * @param[in] expected_length       Number of bytes expected from slave
 *
 * @return true if receive was initiated
 * @return false on failure
 */
bool EXEC_I2C_Start_Master_Receive_External( HWI2CChannel_T channel, uint16_t device_address_7bit,
                                            uint16_t expected_length )
{
    bool is_ok = HW_I2C_Trigger_Master_Receive_External( channel, device_address_7bit, expected_length );
    return is_ok;
}

/**
 * @brief Initiate master receive on the internal FMPI2C1 channel.
 *
 * Requests data from a slave device on the internal FMPI2C1 channel.
 * Received data is buffered internally and can be retrieved with
 * `EXEC_I2C_Receive_Copy_And_Consume()`.
 *
 * @param[in] device_address_7bit   7-bit slave address
 * @param[in] expected_length       Number of bytes expected from slave
 *
 * @return true if receive was initiated
 * @return false on failure
 */
bool EXEC_I2C_Start_Master_Receive_Internal( HWI2CChannel_T channel, uint16_t device_address_7bit,
                                            uint16_t expected_length )
{
    bool is_ok = HW_I2C_Trigger_Master_Receive_Internal( channel, device_address_7bit, expected_length );
    return is_ok;
}

/**
 * @brief Initiate slave receive on an external channel.
 *
 * Prepares the channel to receive data from a master. Received data
 * is buffered internally and can be retrieved with EXEC_I2C_Receive_Copy_And_Consume().
 *
 * @param[in] channel           I2C channel
 * @param[in] expected_length   Number of bytes expected from master
 *
 * @return true if receive was prepared
 * @return false on failure
 */
bool EXEC_I2C_Start_Slave_Receive( HWI2CChannel_T channel, uint16_t expected_length )
{
    bool is_ok = HW_I2C_Trigger_Slave_Receive( channel, expected_length );
    return is_ok;
}

/**
 * @brief Copy received data and advance the receive pointer.
 *
 * Copies received data from the internal ring buffer into caller-provided storage,
 * then consumes (advances pointer past) the copied bytes. Atomic operation.
 *
 * @param[in]  channel                     I2C channel
 * @param[out] result_storage              Buffer to copy received data into
 * @param[in]  result_storage_capacity     Size of result_storage buffer
 * @param[out] bytes_copied                Number of bytes actually copied
 *
 * @return true if operation succeeded
 * @return false on failure
 */
bool EXEC_I2C_Receive_Copy_And_Consume( HWI2CChannel_T channel, uint8_t* result_storage,
                                        uint16_t result_storage_capacity,
                                        uint16_t* bytes_copied )
{
    *bytes_copied = 0U;

    HWI2CRxPeek_T peek      = { 0 };
    bool is_ok = HW_I2C_Peek_Received( channel, &peek );
    if ( !is_ok )
    {
        return false;
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

    is_ok = HW_I2C_Consume_Received( channel, copied );
    if ( !is_ok )
    {
        return false;
    }

    *bytes_copied = copied;
    return true;
}
