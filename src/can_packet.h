/******************************************************************************
 *  File:       can_packet.h
 *
 *  Description:
 *      Shared in-memory representation of one classical CAN packet.
 ******************************************************************************/

#ifndef CAN_PACKET_H
#define CAN_PACKET_H

#include <stdint.h>

#define CAN_PACKET_SIZE ( 8U )
#define CAN_STANDARD_ID_MAX ( 0x7FFU )

/**
 * @brief CAN packet containing a standard 11-bit identifier and up to eight data bytes.
 */
typedef struct CAN_Packet_T
{
    uint16_t id;
    uint8_t  dlc;
    uint8_t  data[CAN_PACKET_SIZE];
} CAN_Packet_T;

#endif /* CAN_PACKET_H */
