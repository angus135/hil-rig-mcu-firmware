/******************************************************************************
 *  File:       exec_i2c.h
 *  Author:     Coen Pasitchnyj
 *  Created:    20-Apr-2026
 *
 *  Description:
 *      Mid-level execution layer for I2C communication. Provides a coordination
 *      interface between high-level application logic and low-level hw_i2c driver.
 *      Validates configuration and transfer requests, then delegates to hw_i2c.
 *      Handles transmit orchestration via stage-buffer pattern and receive
 *      orchestration via peek/copy/consume pattern.
 *
 *  Notes:
 *      - Manages I2C1, I2C2 (external) and FMPI2C1 (internal) channels
 *      - Does not directly manipulate hardware; all operations go through hw_i2c
 *      - Stage buffer size: 256 bytes (defined in hw_i2c)
 *      - Receive ring buffer size: 512 bytes (defined in hw_i2c)
 *      - Not thread-safe; assumes single-threaded execution or external synchronization
 ******************************************************************************/

#ifndef EXEC_I2C_H
#define EXEC_I2C_H

#ifdef __cplusplus
extern "C"
{
#endif

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include <stdint.h>
#include <stdbool.h>
#include "hw_i2c.h"

/**-----------------------------------------------------------------------------
 *  Public Defines / Macros
 *------------------------------------------------------------------------------
 */

#define EXEC_I2C_EXTERNAL_CHANNEL_COUNT ( 2U )

/**-----------------------------------------------------------------------------
 *  Public Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

typedef enum EXECI2CStatus_T
{
    EXEC_I2C_STATUS_OK,
    EXEC_I2C_STATUS_BUSY,
    EXEC_I2C_STATUS_ERROR,
    EXEC_I2C_STATUS_INVALID_PARAM,
    EXEC_I2C_STATUS_OVERFLOW,
} EXECI2CStatus_T;

typedef struct EXECI2CChannelConfig_T
{
    HWI2CMode_T         mode;
    HWI2CSpeed_T        speed;
    HWI2CTransferPath_T tx_transfer_path;
    HWI2CTransferPath_T rx_transfer_path;
    uint16_t            own_address_7bit;
} EXECI2CChannelConfig_T;

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
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
                                        uint16_t internal_fmpi2c1_own_address_7bit );

/**
 * @brief Configure the internal FMPI2C1 channel with default settings.
 *
 * Initializes FMPI2C1 with a predefined own address (0x33).
 * Convenience function for internal channel initialization.
 *
 * @return EXEC_I2C_STATUS_OK on success
 * @return EXEC_I2C_STATUS_ERROR on failure
 */
EXECI2CStatus_T EXEC_I2C_Configuration_Internal( void );

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
                           const uint8_t* payload, uint16_t payload_length );

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
                                    uint16_t payload_length );

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
                          uint16_t payload_length );

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
                                            uint16_t expected_length );

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
                                            uint16_t expected_length );

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
bool EXEC_I2C_Start_Slave_Receive( HWI2CChannel_T channel, uint16_t expected_length );

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
                                        uint16_t* bytes_copied );

#ifdef __cplusplus
}
#endif

#endif /* EXEC_I2C_H */
