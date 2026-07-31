/******************************************************************************
 *  File:       hw_i2c.h
 *  Author:     Coen Pasitchnyj
 *  Created:    20-Apr-2026
 *
 *  Description:
 *      Low-level hardware I2C driver for STM32F446ZE microcontroller. Manages
 *      configuration, interrupt handling, and data transfer orchestration for
 *      three I2C channels: I2C3, I2C2 (both external), and FMPI2C1 (internal).
 *      Supports both master and slave modes with selectable interrupt or DMA-based
 *      transfer paths. Master requests are queued as complete transactions and
 *      received data is published as complete messages.
 *
 *  Notes:
 *      - I2C3 supports interrupt-only transfers (no DMA)
 *      - I2C2 supports both interrupt and DMA transfers
 *      - FMPI2C1 is a high-speed internal channel (interrupt-only)
 *      - Must call configuration function before any transfers
 *      - Interrupt handlers (HW_I2C_Service_*_IRQ) must be called from application ISRs
 *      - RX byte storage is 512 bytes; maximum TX message size is 256 bytes
 ******************************************************************************/

#ifndef HW_I2C_H
#define HW_I2C_H

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

/**-----------------------------------------------------------------------------
 *  Public Defines / Macros
 *------------------------------------------------------------------------------
 */

#define HW_I2C_RX_BUFFER_SIZE ( 512U )
#define HW_I2C_TX_MAX_MESSAGE_SIZE ( 256U )
#define HW_I2C_MASTER_TRANSACTION_QUEUE_DEPTH ( 8U )
#define HW_I2C_RX_MESSAGE_QUEUE_DEPTH ( 8U )

/* Compatibility name retained for the non-queued slave TX staging API. */
#define HW_I2C_TX_STAGE_SIZE HW_I2C_TX_MAX_MESSAGE_SIZE

/**-----------------------------------------------------------------------------
 *  Public Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

typedef enum HWI2CChannel_T
{
    HW_I2C_CHANNEL_1       = 0,
    HW_I2C_CHANNEL_2       = 1,
    HW_I2C_CHANNEL_FMPI2C1 = 2,

    HW_I2C_CHANNEL_COUNT,
} HWI2CChannel_T;

typedef enum HWI2CMode_T
{
    HW_I2C_MODE_MASTER,
    HW_I2C_MODE_SLAVE,
} HWI2CMode_T;

typedef enum HWI2CSpeed_T
{
    HW_I2C_SPEED_100KHZ,
    HW_I2C_SPEED_400KHZ,
} HWI2CSpeed_T;

typedef enum HWI2CTransferPath_T
{
    HW_I2C_TRANSFER_INTERRUPT,
    HW_I2C_TRANSFER_DMA,
} HWI2CTransferPath_T;

typedef enum HWI2CStatus_T
{
    HW_I2C_STATUS_OK,
    HW_I2C_STATUS_BUSY,
    HW_I2C_STATUS_ERROR,
    HW_I2C_STATUS_INVALID_PARAM,
    HW_I2C_STATUS_NOT_CONFIGURED,
    HW_I2C_STATUS_OVERFLOW,
} HWI2CStatus_T;

typedef enum HWI2CTransferKind_T
{
    HW_I2C_TRANSFER_KIND_IDLE = 0,
    HW_I2C_TRANSFER_KIND_MASTER_TX,
    HW_I2C_TRANSFER_KIND_MASTER_RX,
    HW_I2C_TRANSFER_KIND_SLAVE_TX,
    HW_I2C_TRANSFER_KIND_SLAVE_RX,
} HWI2CTransferKind_T;

typedef struct HWI2CChannelConfig_T
{
    HWI2CMode_T         mode;
    HWI2CSpeed_T        speed;
    HWI2CTransferPath_T tx_transfer_path;
    HWI2CTransferPath_T rx_transfer_path;
    uint16_t            own_address_7bit;
} HWI2CChannelConfig_T;

typedef struct HWI2CSpan_T
{
    const uint8_t* data;
    uint16_t       length;
} HWI2CSpan_T;

typedef struct HWI2CRxPeek_T
{
    HWI2CSpan_T first;
    HWI2CSpan_T second;
    uint16_t    total_length;
} HWI2CRxPeek_T;

typedef struct HWI2CRxMessageDescriptor_T
{
    HWI2CTransferKind_T transfer_kind;
    uint16_t            target_address_7bit;
    uint16_t            length;
    HWI2CStatus_T       status;
} HWI2CRxMessageDescriptor_T;

typedef struct HWI2CRxMessagePeek_T
{
    HWI2CRxMessageDescriptor_T descriptor;
    HWI2CSpan_T                first;
    HWI2CSpan_T                second;
} HWI2CRxMessagePeek_T;

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

/**
 * @brief Configure an external I2C channel (I2C3 or I2C2).
 *
 * Initializes an I2C channel with the specified configuration including mode
 * (master/slave), speed (100 kHz / 400 kHz), and transfer path (interrupt/DMA).
 * Must be called before any transfers on the channel.
 *
 * @param[in] channel       I2C channel to configure (HW_I2C_CHANNEL_1 or HW_I2C_CHANNEL_2)
 * @param[in] config        Pointer to configuration structure. Must not be NULL.
 *
 * @return HW_I2C_STATUS_OK on success
 * @return HW_I2C_STATUS_INVALID_PARAM if parameters are invalid or channel is not external
 */
HWI2CStatus_T HW_I2C_Configure_Channel( HWI2CChannel_T              channel,
                                        const HWI2CChannelConfig_T* config );

/**
 * @brief Configure the internal FMPI2C1 channel.
 *
 * Initializes the high-speed internal FMPI2C1 channel with a specified own address.
 * Channel operates in master mode with interrupt-based transfer path.
 *
 * @param[in] own_address_7bit  7-bit own address for the channel (0x00-0x7F)
 *
 * @return HW_I2C_STATUS_OK on success
 * @return HW_I2C_STATUS_INVALID_PARAM if address exceeds 7 bits
 */
HWI2CStatus_T HW_I2C_Configure_Internal_FMPI2C1( uint16_t own_address_7bit );

/**
 * @brief Atomically enqueue one complete master transmit transaction.
 *
 * The payload is copied into driver-owned storage before the queue entry is
 * published. A successful return means the complete request was accepted; it
 * does not mean the bus transaction has completed. Payload length must be
 * non-zero; address-only writes are not implemented.
 *
 * @return HW_I2C_STATUS_OK when accepted
 * @return HW_I2C_STATUS_BUSY when the fixed-depth queue is full
 * @return HW_I2C_STATUS_INVALID_PARAM for an invalid channel, address, payload, or length
 * @return HW_I2C_STATUS_NOT_CONFIGURED when the channel is not configured as a master
 */
HWI2CStatus_T HW_I2C_Enqueue_Master_Transmit( HWI2CChannel_T channel, uint16_t device_address_7bit,
                                              const uint8_t* payload, uint16_t payload_length );

/**
 * @brief Atomically enqueue one complete master receive transaction.
 *
 * The transaction remains in the queue until STOP/bus completion. Received
 * bytes become visible only as one complete RX message.
 */
HWI2CStatus_T HW_I2C_Enqueue_Master_Receive( HWI2CChannel_T channel, uint16_t device_address_7bit,
                                             uint16_t expected_length );

/**
 * @brief Service deferred queue completion and start the next safe transaction.
 *
 * Call from normal context while queued work is outstanding. The function does
 * not wait for BUSY to clear.
 */
void HW_I2C_Service_Transaction_Queue( HWI2CChannel_T channel );

/**
 * @brief Report whether all accepted transactions have physically completed.
 *
 * Completion requires an empty queue, no active transfer, an idle peripheral,
 * and observation of the final STOP/completion condition.
 */
bool HW_I2C_Is_Transaction_Queue_Complete( HWI2CChannel_T channel );

/**
 * @brief Return and clear the channel's latched asynchronous transfer result.
 */
HWI2CStatus_T HW_I2C_Get_And_Clear_Transfer_Result( HWI2CChannel_T channel );

/**
 * @brief Load data into the transmit stage buffer.
 *
 * Prepares data for transmission. Must be called before triggering a transmit.
 * Cannot be called while a transfer is in progress on the channel.
 *
 * @param[in] channel  I2C channel
 * @param[in] data     Pointer to data to transmit. May be NULL if length is 0.
 * @param[in] length   Number of bytes to transmit (max HW_I2C_TX_STAGE_SIZE)
 *
 * @return true if data was loaded successfully
 * @return false if transfer is in progress or length exceeds buffer size
 */
bool HW_I2C_Load_Stage_Buffer( HWI2CChannel_T channel, const uint8_t* data, uint16_t length );

/**
 * @brief Trigger a master transmit operation on an external I2C channel.
 *
 * Initiates an I2C master transmit to the specified device address on an external channel
 * (I2C3 or I2C2) using data previously loaded with HW_I2C_Load_Stage_Buffer().
 * Supports both interrupt and DMA-based transfer paths as configured.
 *
 * @param[in] channel               External I2C channel (HW_I2C_CHANNEL_1 or HW_I2C_CHANNEL_2)
 * @param[in] device_address_7bit   7-bit slave address to transmit to
 *
 * @return true if the staged data was accepted into the master queue
 * @return false if another transfer is already in progress
 */
bool HW_I2C_Trigger_Master_Transmit_External( HWI2CChannel_T channel,
                                              uint16_t       device_address_7bit );

/**
 * @brief Trigger a master transmit operation on the internal FMPI2C1 channel.
 *
 * Initiates an I2C master transmit to the specified device address on the internal
 * FMPI2C1 channel using data previously loaded with HW_I2C_Load_Stage_Buffer().
 * The FMPI2C1 channel uses interrupt-based transfer path only.
 *
 * @param[in] device_address_7bit   7-bit slave address to transmit to
 *
 * @return true if the staged data was accepted into the master queue
 * @return false if another transfer is already in progress
 */
bool HW_I2C_Trigger_Master_Transmit_Internal( uint16_t device_address_7bit );

/**
 * @brief Trigger a master receive operation on an external I2C channel.
 *
 * Initiates an I2C master receive from the specified device address on an external channel
 * (I2C3 or I2C2). Received data will be available via HW_I2C_Peek_Received() and consumed
 * with HW_I2C_Consume_Received(). Supports both interrupt and DMA-based transfer paths
 * as configured.
 *
 * @param[in] channel               External I2C channel (HW_I2C_CHANNEL_1 or HW_I2C_CHANNEL_2)
 * @param[in] device_address_7bit   7-bit slave address to receive from
 * @param[in] expected_length       Number of bytes expected to receive
 *
 * @return true if the receive transaction was accepted into the master queue
 * @return false if another transfer is already in progress
 */
bool HW_I2C_Trigger_Master_Receive_External( HWI2CChannel_T channel, uint16_t device_address_7bit,
                                             uint16_t expected_length );

/**
 * @brief Trigger a master receive operation on the internal FMPI2C1 channel.
 *
 * Initiates an I2C master receive from the specified device address on the internal
 * FMPI2C1 channel. Received data will be available via HW_I2C_Peek_Received() and consumed
 * with HW_I2C_Consume_Received(). The FMPI2C1 channel uses interrupt-based transfer path only.
 *
 * @param[in] device_address_7bit   7-bit slave address to receive from
 * @param[in] expected_length       Number of bytes expected to receive
 *
 * @return true if the receive transaction was accepted into the master queue
 * @return false if another transfer is already in progress
 */
bool HW_I2C_Trigger_Master_Receive_Internal( uint16_t device_address_7bit,
                                             uint16_t expected_length );

/**
 * @brief Trigger a slave transmit operation.
 *
 * Prepares the channel to transmit data in slave mode when the master
 * requests it. Data must be pre-loaded with HW_I2C_Load_Stage_Buffer().
 *
 * @param[in] channel  I2C channel
 *
 * @return true if transmit was prepared successfully
 * @return false if another transfer is already in progress
 */
bool HW_I2C_Trigger_Slave_Transmit_External( HWI2CChannel_T channel );

/**
 * @brief Trigger a slave receive operation.
 *
 * Prepares the channel to receive data in slave mode from a master.
 * Received data will be available via HW_I2C_Peek_Received() and consumed
 * with HW_I2C_Consume_Received().
 *
 * @param[in] channel           I2C channel
 * @param[in] expected_length   Number of bytes expected to receive
 *
 * @return true if receive was prepared successfully
 * @return false if another transfer is already in progress
 */
bool HW_I2C_Trigger_Slave_Receive_External( HWI2CChannel_T channel, uint16_t expected_length );

/**
 * @brief Peek at received data without consuming it.
 *
 * Provides zero-copy access to received data in the ring buffer via two
 * spans (first and second), which may wrap around the buffer.
 *
 * @param[in]  channel  I2C channel
 * @param[out] peek     Pointer to receive peek structure with first and second spans
 *
 * @return true on success
 * @return false on failure
 */
bool HW_I2C_Peek_Received( HWI2CChannel_T channel, HWI2CRxPeek_T* peek );

/**
 * @brief Consume received bytes from the ring buffer.
 *
 * Advances the tail pointer of the receive ring buffer to discard
 * the specified number of bytes.
 *
 * @param[in] channel           I2C channel
 * @param[in] bytes_to_consume  Number of bytes to consume
 *
 * @return true if bytes were consumed successfully
 * @return false if bytes_to_consume exceeds available data
 */
bool HW_I2C_Consume_Received( HWI2CChannel_T channel, uint16_t bytes_to_consume );

/**
 * @brief Peek exactly one complete received message without consuming it.
 *
 * For slave RX, target_address_7bit is zero because I2C does not expose a
 * master source address.
 */
bool HW_I2C_Peek_Received_Message( HWI2CChannel_T channel, HWI2CRxMessagePeek_T* message );

/**
 * @brief Consume the complete message returned by HW_I2C_Peek_Received_Message().
 */
bool HW_I2C_Consume_Received_Message( HWI2CChannel_T channel );

/**
 * @brief Check if an overflow occurred on the channel.
 *
 * Returns true if the ring buffer overflowed during the last receive transfer.
 * Once read, both the compatibility flag and a matching latched overflow result
 * are cleared. This is an atomic operation.
 *
 * @param[in] channel  I2C channel
 *
 * @return true if overflow was detected
 * @return false if no overflow
 */
bool HW_I2C_Get_Overflow_Status( HWI2CChannel_T channel );

#ifdef __cplusplus
}
#endif

#endif /* HW_I2C_H */
