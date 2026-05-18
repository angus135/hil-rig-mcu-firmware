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

/**
 * @brief Activates can channel 1 to immidiatley begin sending messages from the tx buffer
 *
 */
void EXEC_CAN_Tx_Trigger1();

/**
 * @brief Activates can channel 2 to immidiatley begin sending messages from the tx buffer
 *
 */
void EXEC_CAN_Tx_Trigger2();

/**
 * @brief Writes a number of 8 byte packets (source) to the tx buffer of channel 1
 *
 * @param source an array of arrays, type:
uint8_t can_tx_buffer1[X][CAN_PACKET_SIZE];
 * @param length the number of can packets to be written (seen as X above)
 *
 * @return 0 if the write was succesful, 1 otherwise. (partially succesful = 1)
 */
uint16_t EXEC_CAN_Load_Tx1( uint8_t source[][CAN_PACKET_SIZE], uint16_t length );

/**
 * @brief Writes a number of 8 byte packets (source) to the tx buffer of channel 2
 *
 * @param source an array of arrays, type:
uint8_t can_tx_buffer1[X][CAN_PACKET_SIZE];
 * @param length the number of can packets to be written (seen as X above)
 *
 * @return 0 if the write was succesful, 1 otherwise. (partially succesful = 1)
 */
uint16_t EXEC_CAN_Load_Tx2( uint8_t source[][CAN_PACKET_SIZE], uint16_t length );

/**
 * @brief Reads values from the rx channel 1 buffer one at a time and places them in dest
 *
 * @param dest pointer to array of 8 bytes sections of available storage
 * @param len the number of 8 byte sections in dest (the amount of CAN packages being read)
 *
 * @return the number of entries read from the rx buffer (can be 0)
 */
uint16_t EXEC_CAN_Read_Rx1_Buffer( uint8_t dest[][CAN_PACKET_SIZE], uint16_t len );

/**
 * @brief Reads values from the rx channel 2 buffer one at a time and places them in dest
 *
 * @param dest pointer to array of 8 bytes sections of available storage
 * @param len the number of 8 byte sections in dest (the amount of CAN packages being read)
 *
 * @return the number of entries read from the rx buffer (can be 0)
 */
uint16_t EXEC_CAN_Read_Rx2_Buffer( uint8_t dest[][CAN_PACKET_SIZE], uint16_t len );

#ifdef __cplusplus
}
#endif

#endif /* EXEC_CAN_H */
