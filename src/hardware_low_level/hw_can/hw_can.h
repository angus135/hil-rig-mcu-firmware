/******************************************************************************
 *  File:       hw_can.h
 *  Author:     Callum Rafferty
 *  Created:    25-Mar-2026
 *
 *  Description:
 *      <Short description of the module, what it exposes, and how it should be used>
 *
 *  Notes:
 *      <Public assumptions, required initialisation order, dependencies, etc.>
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
// Add any needed standard or project-specific includes here

/**-----------------------------------------------------------------------------
 *  Public Defines / Macros
 *------------------------------------------------------------------------------
 */
#define CAN_PACKET_SIZE 8

/**-----------------------------------------------------------------------------
 *  Public Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

typedef struct CanProperties_T
{
    uint32_t bs1;
    uint32_t bs2;
    uint32_t psc;
    uint32_t timer_hz;
} CanProperties_T;

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

/**
 * @brief Moves the pointer x times
 *
 * @param pointer       the address of the read pointer
 * @param update        the number of times we want to move the pointer
 * @param buffer_width  The width of the buffer CAN_PACKET_SIZE (8)
 *
 */
static inline void HW_CAN_Buffer_consume( volatile uint16_t* pointer, uint16_t update, uint16_t buffer_width );

/**
 * @brief Returns the sent flag for channel 1
 *
 *
 * The sent flag is set after trigger is called when CAN has emptied the buffer and the last message
 * is sent
 */
bool HW_CAN_Channl1_sent();

/**
 * @brief Returns the sent flag for channel 2
 *
 *
 * The sent flag is set after trigger is called when CAN has emptied the buffer and the last message
 * is sent
 */
bool HW_CAN_Channl2_sent();

/**
 * @brief Calculates the required CAN protperties
 *
 * @param bitrate the desired bitrate in bits per second, eg 1Mbps = 1000000
 * @param total_TQ the total time quanta
 * @param sample_point_1t1000 the desired sample point, range 700 to 1000 (typically %)
 *
 * Computes the register values for the given conditions
 */
CanProperties_T HW_CAN_Compute_Properties( uint32_t bitrate, uint32_t total_TQ,
                                           uint32_t sample_point_1t1000 );

/**
 * @brief Configures the peripherals of CAN channel 1
 *
 * @param bitrate the desired bitrate in bits per second, eg 1Mbps = 1000000
 *
 * @return An int representing error codes:
 *      0: no error, config complete
 *      1: config timing error, not complete
 *      2: config filter error, not complete
 *      3: config start error, not complete
 *
 * Provides the configuration of the following:
 *      Prescaler
 *      Time Quanta in Bit Segment 1
 *      Time Quanta in Bit Segment 2
 *      ReSynchronization Jump Width
 *      Operating Modes:
 *          Normal
 *          Loopback
 *          Silent
 *      Filter and Mask:
 *          Acceptance filters and masks
 *          Acceptance of standard and extended frames via filter configuration
 *          FIFO assignment for accepted frames
 *
 */
int HW_CAN_Configure1( uint32_t bitrate );

/**
 * @brief Configures the peripherals of CAN channel 2
 *
 * @param bitrate the desired bitrate in bits per second, eg 1Mbps = 1000000
 *
 * @return An int representing error codes:
 *      0: no error, config complete
 *      1: config timing error, not complete
 *      2: config filter error, not complete
 *      3: config start error, not complete
 *
 * Provides the configuration of the following:
 *      Prescaler
 *      Time Quanta in Bit Segment 1
 *      Time Quanta in Bit Segment 2
 *      ReSynchronization Jump Width
 *      Operating Modes:
 *          Normal
 *          Loopback
 *          Silent
 *      Filter and Mask:
 *          Acceptance filters and masks
 *          Acceptance of standard and extended frames via filter configuration
 *          FIFO assignment for accepted frames
 *
 */
int HW_CAN_Configure2( uint32_t bitrate );

/**
 * @brief recieves data and stores it in rxData (8 bytes) over the hcan CAN channel 1
 *
 * @param hcan the pointer to the handle for the can peripheral
 * @param rxData pointer to 8 bytes of available storage
 *
 * Uses HAL to receive message over CAN channel 1
 */
int HW_CAN_Recieve1( uint8_t* rxData );

/**
 * @brief transmits the txData (8 bytes) over CAN channel 1
 *
 * @param txData pointer to 8 bytes of data
 *
 * Uses HAL to transmit message over CAN channel 1
 */
int HW_CAN_Transmit1( uint8_t* txData );

/**
 * @brief recieves data and stores it in rxData (8 bytes) over the hcan CAN channel 2
 *
 * @param hcan the pointer to the handle for the can peripheral
 * @param rxData pointer to 8 bytes of available storage
 *
 * Uses HAL to receive message over CAN channel 2
 */
int HW_CAN_Recieve2( uint8_t* rxData );

/**
 * @brief transmits the txData (8 bytes) over CAN channel 2
 *
 * @param txData pointer to 8 bytes of data
 *
 * Uses HAL to transmit message over CAN channel 2
 */
int HW_CAN_Transmit2( uint8_t* txData );

/**
 * @brief Writes a number of 8 byte packets (source) to the tx buffer
 *
 * @param source an array of arrays, type:
uint8_t can_tx_buffer1[X][CAN_PACKET_SIZE];
 * @param length the number of can packets to be written (seen as X above)
 *
 * @return 0 if the write was succesful, 1 otherwise. (partially succesful = 1)
 */
uint16_t HW_CAN_Tx_Buffer_Write1( uint8_t source[][CAN_PACKET_SIZE], uint16_t length );

/**
 * @brief Writes a number of 8 byte packets (source) to the rx buffer
 *
 * @param source an array of arrays, type:
uint8_t can_rx_buffer1[X][CAN_PACKET_SIZE];
 * @param length the number of can packets to be written (seen as X above)
 *
 * @return 0 if the write was succesful, 1 otherwise. (partially succesful = 1)
 */
uint16_t HW_CAN_Rx_Buffer_Write1( uint8_t source[][CAN_PACKET_SIZE], uint16_t length );

/**
 * @brief Writes a number of 8 byte packets (source) to the tx buffer
 *
 * @param source an array of arrays, type:
uint8_t can_tx_buffer1[X][CAN_PACKET_SIZE];
 * @param length the number of can packets to be written (seen as X above)
 *
 * @return 0 if the write was succesful, 1 otherwise. (partially succesful = 1)
 */
uint16_t HW_CAN_Tx_Buffer_Write2( uint8_t source[][CAN_PACKET_SIZE], uint16_t length );

/**
 * @brief Writes a number of 8 byte packets (source) to the rx buffer
 *
 * @param source an array of arrays, type:
uint8_t can_rx_buffer1[X][CAN_PACKET_SIZE];
 * @param length the number of can packets to be written (seen as X above)
 *
 * @return 0 if the write was succesful, 1 otherwise. (partially succesful = 1)
 */
uint16_t HW_CAN_Rx_Buffer_Write2( uint8_t source[][CAN_PACKET_SIZE], uint16_t length );

/**
 * @brief Reads from the rx buffer (channel 2) and writes it to dest
 *
 * @param dest the destination where the value will be written
 *
 * @return the number of CAN_PACKET_SIZE's read
 */
uint16_t HW_CAN_Rx_Buffer_Read1( uint8_t dest[][CAN_PACKET_SIZE] );

/**
 * @brief Moves the channe 1 read pointer x times
 *
 * @param update        the number of times we want to move the pointer
 *
 */
void HW_CAN_Rx_Buffer_consume1( uint16_t update );

/**
 * @brief Reads from the rx buffer (channel 2) and writes it to dest
 *
 * @param dest the destination where the value will be written
 *
 * @return the number of CAN_PACKET_SIZE's read
 */
uint16_t HW_CAN_Rx_Buffer_Read2( uint8_t dest[][CAN_PACKET_SIZE] );

/**
 * @brief Moves the channe 2 read pointer x times
 *
 * @param update        the number of times we want to move the pointer
 *
 */
void HW_CAN_Rx_Buffer_consume2( uint16_t update );

/**
 * @brief Reads from the tx buffer (channel 1) and writes it to dest
 *
 * @param dest the destination where the value will be written
 *
 * @return the number of CAN_PACKET_SIZE's read
 */
uint16_t HW_CAN_Tx_Buffer_Read1( uint8_t dest[][CAN_PACKET_SIZE] );

/**
 * @brief Reads from the tx buffer (channel 2) and writes it to dest
 *
 * @param dest the destination where the value will be written
 *
 * @return the number of CAN_PACKET_SIZE's read
 */
uint16_t HW_CAN_Tx_Buffer_Read2( uint8_t dest[][CAN_PACKET_SIZE] );

/**
 * @brief Enables tx interrupts on channel 1
 *
 * Used to enable the sending of messages through CAN channel 1
 * Once the write buffer is empty the ISR will disable again
 */
void HW_CAN_Tx_Trigger1( void );

/**
 * @brief Enables tx interrupts on channel 2
 *
 * Used to enable the sending of messages through CAN channel 2
 * Once the write buffer is empty the ISR will disable again
 */
void HW_CAN_Tx_Trigger2( void );

#ifdef __cplusplus
}
#endif

#endif /* <FILENAME>_H */
