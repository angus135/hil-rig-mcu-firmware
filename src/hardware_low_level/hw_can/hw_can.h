/******************************************************************************
 *  File:       hw_can.h
 *  Author:     Timothy Vogelsang
 *  Created:    25-Mar-2026
 *
 *  Description:
 *      Hardware abstraction layer for the CAN peripherals.
 *
 *      Provides CAN configuration, transmission, reception, buffering,
 *      filtering, and transmit triggering for CAN channels 1 and 2.
 *
 *  Notes:
 *      CAN packets use standard 11-bit CAN identifiers and contain up to
 *      CAN_PACKET_SIZE bytes of data.
 ******************************************************************************/

#ifndef HW_CAN_H
#define HW_CAN_H

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

#define CAN_PACKET_SIZE ( 8U )
#define CAN_STANDARD_ID_MAX ( 0x7FFU )
#define HW_CAN_TX_QUEUE_CAPACITY ( 19U )
#define HW_CAN_RX_QUEUE_CAPACITY ( 19U )

/**-----------------------------------------------------------------------------
 *  Public Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

/**
 * @brief CAN timing properties calculated from the requested CAN bitrate.
 */
typedef struct CanProperties_T
{
    uint32_t bs1;
    uint32_t bs2;
    uint32_t psc;
    uint32_t timer_hz;

} CanProperties_T;

/**
 * @brief CAN packet containing an identifier and CAN data payload.
 *
 * The CAN identifier is a standard 11-bit CAN identifier stored in the
 * lower 11 bits of id.
 *
 * dlc contains the number of valid payload bytes, from 0 through
 * CAN_PACKET_SIZE. Only data[0] through data[dlc - 1] are valid.
 */
typedef struct CAN_Packet_T
{
    uint16_t id;
    uint8_t  dlc;
    uint8_t  data[CAN_PACKET_SIZE];

} CAN_Packet_T;

/**
 * @brief Result codes returned by buffered CAN load and trigger operations.
 */
typedef enum HW_CAN_Result_T
{
    HW_CAN_RESULT_OK = 0,
    HW_CAN_RESULT_ERROR,
    HW_CAN_RESULT_BUSY,
    HW_CAN_RESULT_EMPTY,
} HW_CAN_Result_T;

/**
 * @brief Observable state of one buffered CAN transmit batch.
 */
typedef enum HW_CAN_Tx_Status_T
{
    HW_CAN_TX_STATUS_IDLE = 0,
    HW_CAN_TX_STATUS_ACTIVE,
    HW_CAN_TX_STATUS_COMPLETE,
    HW_CAN_TX_STATUS_ERROR,
} HW_CAN_Tx_Status_T;

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

/**
 * @brief Moves a buffer pointer by the requested number of positions.
 *
 * @param pointer       Address of the read/write pointer.
 * @param update        Number of positions to move the pointer.
 * @param buffer_width  Width of the circular buffer.
 */
void HW_CAN_Buffer_consume( volatile uint16_t* pointer, uint16_t update, uint16_t buffer_width );

/**
 * @brief Returns the sent flag for channel 1.
 *
 * The sent flag is set after the transmit trigger is called, when the CAN
 * transmit buffer has been emptied and the final message has been sent.
 *
 * @return true if the final buffered CAN message has been sent.
 */
bool HW_CAN_Channel1_Sent( void );

/**
 * @brief Returns the sent flag for channel 2.
 *
 * The sent flag is set after the transmit trigger is called, when the CAN
 * transmit buffer has been emptied and the final message has been sent.
 *
 * @return true if the final buffered CAN message has been sent.
 */
bool HW_CAN_Channel2_Sent( void );

/** @return Current buffered transmit status for channel 1. */
HW_CAN_Tx_Status_T HW_CAN_Tx_Status1( void );

/** @return Current buffered transmit status for channel 2. */
HW_CAN_Tx_Status_T HW_CAN_Tx_Status2( void );

/**
 * @brief Returns the number of channel 1 frames dropped because the software RX buffer was full.
 *
 * @return Sticky dropped-frame count since the last channel 1 reset.
 */
uint32_t HW_CAN_Rx_Dropped_Count1( void );

/**
 * @brief Returns the number of channel 2 frames dropped because the software RX buffer was full.
 *
 * @return Sticky dropped-frame count since the last channel 2 reset.
 */
uint32_t HW_CAN_Rx_Dropped_Count2( void );

/**
 * @brief Calculates the required CAN timing properties.
 *
 * @param bitrate               Desired bitrate in bits per second.
 * @param total_TQ              Total number of time quanta per bit.
 * @param sample_point_1t1000   Desired sample point, expressed from
 *                             700 to 1000.
 *
 * @return Calculated CAN timing properties, or all-zero properties when the
 *         timing is invalid or the requested bitrate cannot be represented
 *         exactly by the bxCAN prescaler.
 */
CanProperties_T HW_CAN_Compute_Properties( uint32_t bitrate, uint32_t total_TQ,
                                           uint32_t sample_point_1t1000 );

/**
 * @brief Configures CAN channel 1.
 *
 * @param bitrate       Desired bitrate in bits per second.
 * @param filter_bank  CAN filter bank to configure.
 * @param filter_id    CAN standard identifier used by the filter.
 * @param filter_mask  CAN standard identifier filter mask.
 *
 * @return Error code:
 *      0: no error, configuration complete
 *      1: configuration timing error
 *      2: configuration filter error
 *      3: configuration start error
 *
 * Provides configuration of:
 *      - CAN prescaler
 *      - Time quanta in Bit Segment 1
 *      - Time quanta in Bit Segment 2
 *      - ReSynchronization Jump Width
 *      - Operating mode
 *      - Acceptance filter and mask
 *      - FIFO assignment
 *      - CAN interrupts
 */
int HW_CAN_Configure1( uint32_t bitrate, uint16_t filter_bank, uint16_t filter_id,
                       uint16_t filter_mask );

/**
 * @brief Configures CAN channel 2.
 *
 * @param bitrate       Desired bitrate in bits per second.
 * @param filter_bank  CAN filter bank to configure.
 * @param filter_id    CAN standard identifier used by the filter.
 * @param filter_mask  CAN standard identifier filter mask.
 *
 * @return Error code:
 *      0: no error, configuration complete
 *      1: configuration timing error
 *      2: configuration filter error
 *      3: configuration start error
 *
 * Provides configuration of:
 *      - CAN prescaler
 *      - Time quanta in Bit Segment 1
 *      - Time quanta in Bit Segment 2
 *      - ReSynchronization Jump Width
 *      - Operating mode
 *      - Acceptance filter and mask
 *      - FIFO assignment
 *      - CAN interrupts
 */
int HW_CAN_Configure2( uint32_t bitrate, uint16_t filter_bank, uint16_t filter_id,
                       uint16_t filter_mask );

/**
 * @brief Clears all channel 1 software queue, transmission, and RX diagnostic state.
 *
 * The channel 1 TX and RX interrupts are masked while the state is reset and
 * restored to their previous enable state before this function returns.
 */
void HW_CAN_Reset1( void );

/**
 * @brief Clears all channel 2 software queue, transmission, and RX diagnostic state.
 *
 * The channel 2 TX and RX interrupts are masked while the state is reset and
 * restored to their previous enable state before this function returns.
 */
void HW_CAN_Reset2( void );

/**
 * @brief Recovers channel 1 from a transmit or bus error in task context.
 *
 * Outstanding hardware requests and queued software packets are discarded.
 * Successful recovery leaves the channel idle and ready for a new batch.
 */
HW_CAN_Result_T HW_CAN_Recover1( void );

/**
 * @brief Recovers channel 2 from a transmit or bus error in task context.
 *
 * Outstanding hardware requests and queued software packets are discarded.
 * Successful recovery leaves the channel idle and ready for a new batch.
 */
HW_CAN_Result_T HW_CAN_Recover2( void );

/**
 * @brief Receives a CAN packet on channel 1.
 *
 * @param rxPacket Pointer to the CAN packet where the received identifier
 *                 and data will be stored.
 *
 * @return 0 if a CAN packet was received successfully,
 *         non-zero if no packet is pending or rxPacket is null. A null
 *         destination does not release a FIFO entry.
 */
int HW_CAN_Receive1( CAN_Packet_T* rxPacket );

/**
 * @brief Transmits a CAN packet on channel 1.
 *
 * @param txData Pointer to payload data. May be null only when dlc is zero.
 * @param id     Standard 11-bit CAN identifier.
 * @param dlc    Number of valid payload bytes, from 0 through 8.
 *
 * @return HW_CAN_RESULT_OK if loaded, HW_CAN_RESULT_BUSY while a buffered
 *         batch is active, or HW_CAN_RESULT_ERROR for invalid input/state.
 */
HW_CAN_Result_T HW_CAN_Transmit1( uint8_t* txData, uint16_t id, uint8_t dlc );

/**
 * @brief Receives a CAN packet on channel 2.
 *
 * @param rxPacket Pointer to the CAN packet where the received identifier
 *                 and data will be stored.
 *
 * @return 0 if a CAN packet was received successfully,
 *         non-zero if no packet is pending or rxPacket is null. A null
 *         destination does not release a FIFO entry.
 */
int HW_CAN_Receive2( CAN_Packet_T* rxPacket );

/**
 * @brief Transmits a CAN packet on channel 2.
 *
 * @param txData Pointer to payload data. May be null only when dlc is zero.
 * @param id     Standard 11-bit CAN identifier.
 * @param dlc    Number of valid payload bytes, from 0 through 8.
 *
 * @return HW_CAN_RESULT_OK if loaded, HW_CAN_RESULT_BUSY while a buffered
 *         batch is active, or HW_CAN_RESULT_ERROR for invalid input/state.
 */
HW_CAN_Result_T HW_CAN_Transmit2( uint8_t* txData, uint16_t id, uint8_t dlc );

/**-----------------------------------------------------------------------------
 *  Channel 1 Buffer Functions
 *------------------------------------------------------------------------------
 */

/**
 * @brief Writes CAN packets to the channel 1 transmit buffer.
 *
 * @param source  Array of CAN_Packet_T packets to write.
 * @param length  Number of CAN packets to write.
 *
 * @return HW_CAN_RESULT_OK if the complete batch was loaded,
 *         HW_CAN_RESULT_BUSY if transmission is active, or
 *         HW_CAN_RESULT_ERROR if the batch is invalid or does not fit.
 */
HW_CAN_Result_T HW_CAN_Tx_Buffer_Write1( CAN_Packet_T source[], uint16_t length );

/** Discard all queued, not-yet-transmitted channel 1 packets. */
void HW_CAN_Tx_Buffer_Cancel1( void );

/**
 * @brief Writes CAN packets to the channel 1 receive buffer.
 *
 * @param source  Array of CAN_Packet_T packets to write.
 * @param length  Number of CAN packets to write.
 *
 * @return HW_CAN_RESULT_OK if the complete batch was loaded,
 *         HW_CAN_RESULT_BUSY if transmission is active, or
 *         HW_CAN_RESULT_ERROR if the batch is invalid or does not fit.
 */
uint16_t HW_CAN_Rx_Buffer_Write1( CAN_Packet_T source[], uint16_t length );

/**
 * @brief Reads CAN packets from the channel 1 receive buffer.
 *
 * @param dest      Destination array for the CAN packets.
 * @param capacity  Maximum number of packets that fit in dest.
 *
 * @return Number of packets copied and consumed. The result is at most
 *         capacity. A zero capacity, or a null destination with non-zero
 *         capacity, returns zero without changing the buffer.
 */
uint16_t HW_CAN_Rx_Buffer_Read1( CAN_Packet_T dest[], uint16_t capacity );

/**
 * @brief Reads CAN packets from the channel 1 transmit buffer.
 *
 * Reading does not consume the packets from the buffer. Use the appropriate
 * buffer consume function to advance the read pointer.
 *
 * @param dest  Destination array for the CAN packets.
 *
 * @return Number of CAN packets available in the buffer.
 */
uint16_t HW_CAN_Tx_Buffer_Read1( CAN_Packet_T dest[] );

/**
 * @brief Moves the channel 1 receive buffer read pointer.
 *
 * @param update Number of packets to consume.
 */
void HW_CAN_Rx_Buffer_consume1( uint16_t update );

/**
 * @brief Removes one CAN packet from the channel 1 transmit buffer.
 *
 * @param dest Pointer to the destination CAN packet.
 *
 * @return 0 if a packet was successfully removed,
 *         1 if the buffer was empty.
 */
uint16_t HW_CAN_Tx_Buffer_Pop1( CAN_Packet_T* dest );

/**
 * @brief Removes one CAN packet from the channel 1 receive buffer.
 *
 * @param dest Pointer to the destination CAN packet.
 *
 * @return 0 if a packet was successfully removed,
 *         1 if the buffer was empty.
 */
uint16_t HW_CAN_Rx_Buffer_Pop1( CAN_Packet_T* dest );

/**-----------------------------------------------------------------------------
 *  Channel 2 Buffer Functions
 *------------------------------------------------------------------------------
 */

/**
 * @brief Writes CAN packets to the channel 2 transmit buffer.
 *
 * @param source  Array of CAN_Packet_T packets to write.
 * @param length  Number of CAN packets to write.
 *
 * @return 0 if the write was successful,
 *         1 if the buffer could not accept all packets.
 */
HW_CAN_Result_T HW_CAN_Tx_Buffer_Write2( CAN_Packet_T source[], uint16_t length );

/** Discard all queued, not-yet-transmitted channel 2 packets. */
void HW_CAN_Tx_Buffer_Cancel2( void );

/**
 * @brief Writes CAN packets to the channel 2 receive buffer.
 *
 * @param source  Array of CAN_Packet_T packets to write.
 * @param length  Number of CAN packets to write.
 *
 * @return 0 if the write was successful,
 *         1 if the buffer could not accept all packets.
 */
uint16_t HW_CAN_Rx_Buffer_Write2( CAN_Packet_T source[], uint16_t length );

/**
 * @brief Reads CAN packets from the channel 2 receive buffer.
 *
 * @param dest      Destination array for the CAN packets.
 * @param capacity  Maximum number of packets that fit in dest.
 *
 * @return Number of packets copied and consumed. The result is at most
 *         capacity. A zero capacity, or a null destination with non-zero
 *         capacity, returns zero without changing the buffer.
 */
uint16_t HW_CAN_Rx_Buffer_Read2( CAN_Packet_T dest[], uint16_t capacity );

/**
 * @brief Reads CAN packets from the channel 2 transmit buffer.
 *
 * Reading does not consume the packets from the buffer.
 *
 * @param dest  Destination array for the CAN packets.
 *
 * @return Number of CAN packets available in the buffer.
 */
uint16_t HW_CAN_Tx_Buffer_Read2( CAN_Packet_T dest[] );

/**
 * @brief Moves the channel 2 receive buffer read pointer.
 *
 * @param update Number of packets to consume.
 */
void HW_CAN_Rx_Buffer_consume2( uint16_t update );

/**
 * @brief Removes one CAN packet from the channel 2 transmit buffer.
 *
 * @param dest Pointer to the destination CAN packet.
 *
 * @return 0 if a packet was successfully removed,
 *         1 if the buffer was empty.
 */
uint16_t HW_CAN_Tx_Buffer_Pop2( CAN_Packet_T* dest );

/**
 * @brief Removes one CAN packet from the channel 2 receive buffer.
 *
 * @param dest Pointer to the destination CAN packet.
 *
 * @return 0 if a packet was successfully removed,
 *         1 if the buffer was empty.
 */
uint16_t HW_CAN_Rx_Buffer_Pop2( CAN_Packet_T* dest );

/**-----------------------------------------------------------------------------
 *  Transmit Trigger Functions
 *------------------------------------------------------------------------------
 */

/**
 * @brief Starts transmitting the buffered channel 1 batch.
 *
 * @return HW_CAN_RESULT_OK if the batch was started,
 *         HW_CAN_RESULT_BUSY if a batch is already active or a direct
 *         transmission occupies a hardware mailbox,
 *         HW_CAN_RESULT_EMPTY if no packet is queued, or
 *         HW_CAN_RESULT_ERROR if the first packet could not be transmitted.
 *
 * An empty trigger does not create an active operation or change the previous
 * completion result.
 */
HW_CAN_Result_T HW_CAN_Tx_Trigger1( void );

/**
 * @brief Starts transmitting the buffered channel 2 batch.
 *
 * @return HW_CAN_RESULT_OK if the batch was started,
 *         HW_CAN_RESULT_BUSY if a batch is already active or a direct
 *         transmission occupies a hardware mailbox,
 *         HW_CAN_RESULT_EMPTY if no packet is queued, or
 *         HW_CAN_RESULT_ERROR if the first packet could not be transmitted.
 *
 * An empty trigger does not create an active operation or change the previous
 * completion result.
 */
HW_CAN_Result_T HW_CAN_Tx_Trigger2( void );

#ifdef __cplusplus
}
#endif

#endif /* HW_CAN_H */
