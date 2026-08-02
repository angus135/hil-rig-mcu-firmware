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
bool HW_CAN_Channl1_sent( void );

/**
 * @brief Returns the sent flag for channel 2.
 *
 * The sent flag is set after the transmit trigger is called, when the CAN
 * transmit buffer has been emptied and the final message has been sent.
 *
 * @return true if the final buffered CAN message has been sent.
 */
bool HW_CAN_Channl2_sent( void );

/**
 * @brief Calculates the required CAN timing properties.
 *
 * @param bitrate               Desired bitrate in bits per second.
 * @param total_TQ              Total number of time quanta per bit.
 * @param sample_point_1t1000   Desired sample point, expressed from
 *                             700 to 1000.
 *
 * @return Calculated CAN timing properties.
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
 *      4: notification activation error
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
 *      4: notification activation error
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
 * @brief Receives a CAN packet on channel 1.
 *
 * @param rxPacket Pointer to the CAN packet where the received identifier
 *                 and data will be stored.
 *
 * @return 0 if a CAN packet was received successfully,
 *         non-zero otherwise.
 */
int HW_CAN_Recieve1( CAN_Packet_T* rxPacket );

/**
 * @brief Transmits a CAN packet on channel 1.
 *
 * @param txData Pointer to CAN_PACKET_SIZE bytes of data.
 * @param id     Standard 11-bit CAN identifier.
 * @param dlc    Number of valid payload bytes, from 0 through 8.
 *
 * @return 0 if the transmission was successfully loaded into a mailbox,
 *         non-zero otherwise.
 */
int HW_CAN_Transmit1( uint8_t* txData, uint16_t id, uint8_t dlc );

/**
 * @brief Receives a CAN packet on channel 2.
 *
 * @param rxPacket Pointer to the CAN packet where the received identifier
 *                 and data will be stored.
 *
 * @return 0 if a CAN packet was received successfully,
 *         non-zero otherwise.
 */
int HW_CAN_Recieve2( CAN_Packet_T* rxPacket );

/**
 * @brief Transmits a CAN packet on channel 2.
 *
 * @param txData Pointer to CAN_PACKET_SIZE bytes of data.
 * @param id     Standard 11-bit CAN identifier.
 * @param dlc    Number of valid payload bytes, from 0 through 8.
 *
 * @return 0 if the transmission was successfully loaded into a mailbox,
 *         non-zero otherwise.
 */
int HW_CAN_Transmit2( uint8_t* txData, uint16_t id, uint8_t dlc );

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
 * @return 0 if the write was successful,
 *         1 if the buffer could not accept all packets.
 */
uint16_t HW_CAN_Tx_Buffer_Write1( CAN_Packet_T source[], uint16_t length );

/**
 * @brief Writes CAN packets to the channel 1 receive buffer.
 *
 * @param source  Array of CAN_Packet_T packets to write.
 * @param length  Number of CAN packets to write.
 *
 * @return 0 if the write was successful,
 *         1 if the buffer could not accept all packets.
 */
uint16_t HW_CAN_Rx_Buffer_Write1( CAN_Packet_T source[], uint16_t length );

/**
 * @brief Reads CAN packets from the channel 1 receive buffer.
 *
 * Reading does not consume the packets from the buffer. Use
 * HW_CAN_Rx_Buffer_consume1() to advance the read pointer.
 *
 * @param dest  Destination array for the CAN packets.
 *
 * @return Number of CAN packets available in the buffer.
 */
uint16_t HW_CAN_Rx_Buffer_Read1( CAN_Packet_T dest[] );

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
uint16_t HW_CAN_Tx_Buffer_Write2( CAN_Packet_T source[], uint16_t length );

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
 * Reading does not consume the packets from the buffer. Use
 * HW_CAN_Rx_Buffer_consume2() to advance the read pointer.
 *
 * @param dest  Destination array for the CAN packets.
 *
 * @return Number of CAN packets available in the buffer.
 */
uint16_t HW_CAN_Rx_Buffer_Read2( CAN_Packet_T dest[] );

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
 * @brief Enables transmit interrupts on channel 1.
 *
 * Used to enable transmission of messages through CAN channel 1.
 * Once the transmit buffer is empty, the ISR disables the interrupt again.
 */
void HW_CAN_Tx_Trigger1( void );

/**
 * @brief Enables transmit interrupts on channel 2.
 *
 * Used to enable transmission of messages through CAN channel 2.
 * Once the transmit buffer is empty, the ISR disables the interrupt again.
 */
void HW_CAN_Tx_Trigger2( void );

#ifdef __cplusplus
}
#endif

#endif /* HW_CAN_H */
