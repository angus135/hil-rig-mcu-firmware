/******************************************************************************
 *  File:       exec_can.h
 *  Author:     Angus Corr
 *  Created:    25-Mar-2026
 *
 *  Description:
 *      <Short description of the module, what it exposes, and how it should be used>
 *
 *  Notes:
 *      <Public assumptions, required initialisation order, dependencies, etc.>
 ******************************************************************************/

#ifndef EXEC_CAN_H
#define EXEC_CAN_H

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

#include "hw_can.h"

/**-----------------------------------------------------------------------------
 *  Public Defines / Macros
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Public Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

/**
 * @brief If True then all channel 1 messages have been sent, since the last trigger
 *
 *
 * The sent flag is set flase after trigger is called when CAN has emptied the buffer
 * and set true when the last message is sent and the buffer is ready for a new message
 */
bool EXEC_CAN_Channl1_sent();

/**
 * @brief If True then all channel 2 messages have been sent, since the last trigger
 *
 *
 * The sent flag is set flase after trigger is called when CAN has emptied the buffer
 * and set true when the last message is sent and the buffer is ready for a new message
 */
bool EXEC_CAN_Channl2_sent();

/** @return Current buffered transmit status for channel 1. */
HW_CAN_Tx_Status_T EXEC_CAN_Tx_Status1( void );

/** @return Current buffered transmit status for channel 2. */
HW_CAN_Tx_Status_T EXEC_CAN_Tx_Status2( void );

/** Configure CAN channel 1 using the low-level timing and filter contract. */
int EXEC_CAN_Configure1( uint32_t bitrate, uint16_t filter_bank, uint16_t filter_id,
                         uint16_t filter_mask );

/** Configure CAN channel 2 using the low-level timing and filter contract. */
int EXEC_CAN_Configure2( uint32_t bitrate, uint16_t filter_bank, uint16_t filter_id,
                         uint16_t filter_mask );

/** Recover channel 1 in task context, discarding failed pending work. */
HW_CAN_Result_T EXEC_CAN_Recover1( void );

/** Recover channel 2 in task context, discarding failed pending work. */
HW_CAN_Result_T EXEC_CAN_Recover2( void );

/**
 * @brief Returns channel 1 frames dropped because its software RX buffer was full.
 *
 * @return Sticky dropped-frame count since the last channel configuration/reset.
 */
uint32_t EXEC_CAN_Rx_Dropped_Count1( void );

/**
 * @brief Returns channel 2 frames dropped because its software RX buffer was full.
 *
 * @return Sticky dropped-frame count since the last channel configuration/reset.
 */
uint32_t EXEC_CAN_Rx_Dropped_Count2( void );

/**
 * @brief Starts transmitting the buffered channel 1 batch.
 *
 * @return See HW_CAN_Tx_Trigger1() for success, busy, empty, and error results.
 */
HW_CAN_Result_T EXEC_CAN_Tx_Trigger1( void );

/**
 * @brief Starts transmitting the buffered channel 2 batch.
 *
 * @return See HW_CAN_Tx_Trigger2() for success, busy, empty, and error results.
 */
HW_CAN_Result_T EXEC_CAN_Tx_Trigger2( void );

/**
 * @brief Writes a number of 8 byte packets (source) to the tx buffer of channel 1
 *
 * @param source an array of arrays, type:
uint8_t can_tx_buffer1[X][CAN_PACKET_SIZE];
 * @param length the number of can packets to be written (seen as X above)
 *
 * @return See HW_CAN_Tx_Buffer_Write1() for success, busy, and error results.
 */
HW_CAN_Result_T EXEC_CAN_Load_Tx1( CAN_Packet_T source[], uint16_t length );

/**
 * @brief Writes a number of 8 byte packets (source) to the tx buffer of channel 2
 *
 * @param source an array of arrays, type:
uint8_t can_tx_buffer1[X][CAN_PACKET_SIZE];
 * @param length the number of can packets to be written (seen as X above)
 *
 * @return See HW_CAN_Tx_Buffer_Write2() for success, busy, and error results.
 */
HW_CAN_Result_T EXEC_CAN_Load_Tx2( CAN_Packet_T source[], uint16_t length );

/**
 * @brief Reads values from the rx channel 1 buffer and places them in dest
 *
 * @param dest      Destination array for received CAN packets.
 * @param capacity  Maximum number of packets that fit in dest.
 *
 * @return Number of packets copied and consumed, up to capacity. Zero capacity,
 *         or a null destination with non-zero capacity, returns zero without
 *         consuming packets.
 */
uint16_t EXEC_CAN_Rx_Buffer_Read1( CAN_Packet_T dest[], uint16_t capacity );

/**
 * @brief Reads values from the rx channel 2 buffer and places them in dest
 *
 * @param dest      Destination array for received CAN packets.
 * @param capacity  Maximum number of packets that fit in dest.
 *
 * @return Number of packets copied and consumed, up to capacity. Zero capacity,
 *         or a null destination with non-zero capacity, returns zero without
 *         consuming packets.
 */
uint16_t EXEC_CAN_Rx_Buffer_Read2( CAN_Packet_T dest[], uint16_t capacity );

#ifdef __cplusplus
}
#endif

#endif /* EXEC_CAN_H */
