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
 * and delegates configuration to hw_i2c.
 * Must be called before any transfers.
 *
 * @note Validity checks are minimal. Callers must ensure:
 *       - i2c1_config is non-NULL
 *       - i2c2_config is non-NULL
 *       Configuration validation occurs; invalid configs will be rejected.
 *
 * @param[in] i2c1_config                           Configuration for I2C1 channel
 * @param[in] i2c2_config                           Configuration for I2C2 channel
 *
 * @return EXEC_I2C_STATUS_OK on success
 * @return EXEC_I2C_STATUS_INVALID_PARAM if any parameter is invalid
 */
EXECI2CStatus_T EXEC_I2C_Configuration( const EXECI2CChannelConfig_T* i2c1_config,
                                        const EXECI2CChannelConfig_T* i2c2_config );

/**
 * @brief Master transmit on an external channel.
 *
 * Sends data to a slave device on the specified channel.
 * Data must be provided directly in the payload; internally handles buffering.
 *
 * @note Validity checks are minimal. Callers must ensure:
 *       - channel is a valid external I2C channel (HW_I2C_CHANNEL_1 or HW_I2C_CHANNEL_2)
 *       - channel has been previously configured via EXEC_I2C_Configuration()
 *       - payload is non-NULL if payload_length > 0
 *       Invalid channel access will result in undefined behavior (no range checking).
 *
 * @param[in] channel               I2C channel (HW_I2C_CHANNEL_1 or HW_I2C_CHANNEL_2)
 * @param[in] device_address_7bit   7-bit slave address
 * @param[in] payload               Data to transmit
 * @param[in] payload_length        Number of bytes to transmit
 *
 * @return true if transmission was initiated
 * @return false on failure
 */
bool EXEC_I2C_Master_Transmit_External( HWI2CChannel_T channel, uint16_t device_address_7bit,
                           const uint8_t* payload, uint16_t payload_length );

/**
 * @brief Slave transmit on an external channel.
 *
 * Prepares the channel to respond to a master read request with the provided data.
 *
 * @note Validity checks are minimal. Callers must ensure:
 *       - channel is a valid external I2C channel (HW_I2C_CHANNEL_1 or HW_I2C_CHANNEL_2)
 *       - channel has been previously configured via EXEC_I2C_Configuration()
 *       - payload is non-NULL if payload_length > 0
 *       Invalid channel access will result in undefined behavior (no range checking).
 *
 * @param[in] channel               I2C channel
 * @param[in] payload               Data to transmit when master requests
 * @param[in] payload_length        Number of bytes available to transmit
 *
 * @return true if slave transmit was prepared
 * @return false on failure
 */
bool EXEC_I2C_Slave_Transmit_External( HWI2CChannel_T channel, const uint8_t* payload,
                          uint16_t payload_length );

/**
 * @brief Initiate master receive on an external I2C channel.
 *
 * Requests data from a slave device on the specified external channel (I2C1 or I2C2).
 * Received data is buffered internally and can be retrieved with EXEC_I2C_Receive_Copy_And_Consume().
 *
 * @note Validity checks are minimal. Callers must ensure:
 *       - channel is a valid external I2C channel (HW_I2C_CHANNEL_1 or HW_I2C_CHANNEL_2)
 *       - channel has been previously configured via EXEC_I2C_Configuration()
 *       Invalid channel access will result in undefined behavior (no range checking).
 *
 * @param[in] channel               External I2C channel (HW_I2C_CHANNEL_1 or HW_I2C_CHANNEL_2)
 * @param[in] device_address_7bit   7-bit slave address
 * @param[in] expected_length       Number of bytes expected from slave
 *
 * @return true if receive was initiated
 * @return false on failure
 */
bool EXEC_I2C_Start_Master_Receive_External( HWI2CChannel_T channel, uint16_t device_address_7bit,
                                             uint16_t expected_length );

/**
 * @brief Initiate slave receive on an external channel.
 *
 * Prepares the channel to receive data from a master. Received data
 * is buffered internally and can be retrieved with EXEC_I2C_Receive_Copy_And_Consume().
 *
 * @note Validity checks are minimal. Callers must ensure:
 *       - channel is a valid external I2C channel (HW_I2C_CHANNEL_1 or HW_I2C_CHANNEL_2)
 *       - channel has been previously configured via EXEC_I2C_Configuration()
 *       Invalid channel access will result in undefined behavior (no range checking).
 *
 * @param[in] channel           I2C channel
 * @param[in] expected_length   Number of bytes expected from master
 *
 * @return true if receive was prepared
 * @return false on failure
 */
bool EXEC_I2C_Start_Slave_Receive_External( HWI2CChannel_T channel, uint16_t expected_length );

/**
 * @brief Copy received data and advance the receive pointer.
 *
 * Copies received data from the internal ring buffer into caller-provided storage,
 * then consumes (advances pointer past) the copied bytes. Atomic operation.
 *
 * @note Validity checks are minimal. Callers must ensure:
 *       - channel is a valid external I2C channel (HW_I2C_CHANNEL_1 or HW_I2C_CHANNEL_2)
 *       - channel has been previously configured via EXEC_I2C_Configuration()
 *       - result_storage is non-NULL
 *       - bytes_copied is non-NULL
 *       Invalid channel access will result in undefined behavior (no range checking).
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
