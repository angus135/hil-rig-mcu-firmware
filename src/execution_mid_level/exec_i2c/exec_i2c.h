/******************************************************************************
 *  File:       exec_i2c.h
 *  Author:     Coen Pasitchnyj
 *  Created:    20-Apr-2026
 *
 *  Description:
 *      Mid-level execution layer for I2C communication. Provides a coordination
 *      interface between high-level application logic and low-level hw_i2c driver.
 *      Validates configuration and transfer requests, then delegates to hw_i2c.
 *      Submits complete master transactions to the low-level queue and retrieves
 *      complete receive messages without losing transaction boundaries.
 *
 *  Notes:
 *      - Manages I2C3, I2C2 (external) and FMPI2C1 (internal) channels
 *      - Does not directly manipulate hardware; all operations go through hw_i2c
 *      - Master requests are accepted asynchronously into the hw_i2c queue
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
    EXEC_I2C_STATUS_BUFFER_TOO_SMALL,
    EXEC_I2C_STATUS_NO_DATA,
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
 * Validates configuration parameters for both external channels (I2C3 and I2C2)
 * and delegates configuration to hw_i2c.
 * Must be called before any transfers.
 *
 * @note Validity checks are minimal. Callers must ensure:
 *       - i2c1_config is non-NULL
 *       - i2c2_config is non-NULL
 *       Configuration validation occurs; invalid configs will be rejected.
 *
 * @param[in] i2c1_config                           Configuration for I2C3 channel
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
 * The complete payload is validated and copied into driver-owned queue storage.
 *
 * @param[in] channel               I2C channel (HW_I2C_CHANNEL_1 or HW_I2C_CHANNEL_2)
 * @param[in] device_address_7bit   7-bit slave address
 * @param[in] payload               Data to transmit
 * @param[in] payload_length        Number of bytes to transmit
 *
 * @return true if the complete request was accepted into the driver queue
 * @return false on failure
 */
bool EXEC_I2C_Master_Transmit_External( HWI2CChannel_T channel, uint16_t device_address_7bit,
                                        const uint8_t* payload, uint16_t payload_length );

/**
 * @brief Slave transmit on an external channel.
 *
 * Prepares the channel to respond to a master read request with the provided data.
 *
 * The low-level trigger validates channel state and rejects an active slave transfer.
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
 * Requests data from a slave device on the specified external channel (I2C3 or I2C2).
 * Received data is buffered internally and can be retrieved with
EXEC_I2C_Receive_Copy_And_Consume().
 *
 * The complete receive request is validated and queued atomically.
 *
 * @param[in] channel               External I2C channel (HW_I2C_CHANNEL_1 or HW_I2C_CHANNEL_2)
 * @param[in] device_address_7bit   7-bit slave address
 * @param[in] expected_length       Number of bytes expected from slave
 *
 * @return true if the complete receive request was accepted into the queue
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
 * The low-level trigger validates expected length and rejects an active transfer.
 *
 * @param[in] channel           I2C channel
 * @param[in] expected_length   Number of bytes expected from master
 *
 * @return true if receive was prepared
 * @return false on failure
 */
bool EXEC_I2C_Start_Slave_Receive_External( HWI2CChannel_T channel, uint16_t expected_length );

/**
 * @brief Service deferred queue progress from normal execution context.
 */
void EXEC_I2C_Service_Transaction_Queue( HWI2CChannel_T channel );

/**
 * @brief Determine whether every accepted master transaction has physically completed.
 *
 * This includes final STOP observation and an idle peripheral; it is distinct
 * from a successful enqueue return.
 */
bool EXEC_I2C_Is_Transaction_Queue_Complete( HWI2CChannel_T channel );

/**
 * @brief Retrieve and clear the channel's latched asynchronous transfer result.
 */
EXECI2CStatus_T EXEC_I2C_Get_And_Clear_Transfer_Result( HWI2CChannel_T channel );

/**
 * @brief Copy and consume exactly one complete receive transaction.
 *
 * Services deferred queue progress before checking for a complete message.
 *
 * If the destination is too small, required_length reports the next complete
 * message size and the message remains unconsumed.
 *
 * @return EXEC_I2C_STATUS_OK when one message was copied and consumed
 * @return EXEC_I2C_STATUS_NO_DATA when no complete message is available
 * @return EXEC_I2C_STATUS_BUFFER_TOO_SMALL when capacity is insufficient
 * @return EXEC_I2C_STATUS_INVALID_PARAM for invalid output pointers
 */
EXECI2CStatus_T EXEC_I2C_Receive_Message_Copy_And_Consume(
    HWI2CChannel_T channel, uint8_t* result_storage, uint16_t result_storage_capacity,
    HWI2CRxMessageDescriptor_T* descriptor, uint16_t* bytes_copied, uint16_t* required_length );

/**
 * @brief Copy received data and advance the receive pointer.
 *
 * Compatibility wrapper around EXEC_I2C_Receive_Message_Copy_And_Consume(). It
 * retrieves at most one complete message. No-data polling remains successful
 * with bytes_copied set to zero for existing console callers. A message that
 * does not fit returns false and remains unconsumed.
 *
 * Parameters are validated by the message-oriented implementation.
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
                                        uint16_t result_storage_capacity, uint16_t* bytes_copied );

/**
 * @brief Check if the last transfer on the channel resulted in an overflow.
 *
 * Returns true if the ring buffer overflowed during the last receive transfer, indicating
 * that data was lost. This can be used by callers to detect when they are not consuming
 * received data fast enough.
 *
 * @param[in]  channel      I2C channel
 *
 * @return true if overflow was detected
 * @return false otherwise
 */
bool EXEC_I2C_Did_Last_Transfer_Overflow( HWI2CChannel_T channel );

#ifdef __cplusplus
}
#endif

#endif /* EXEC_I2C_H */
