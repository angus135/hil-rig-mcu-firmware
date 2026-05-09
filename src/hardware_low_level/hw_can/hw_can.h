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
 * @brief Calculates the required CAN protperties
 *
 * @param bitrate the desired bitrate in bits per second, eg 1Mbps = 1000000
 * @param total_TQ the total time quanta
 * @param sample_point_1t1000 the desired sample point, range 700 to 1000 (typically %)
 *
 * Computes the register values for the given conditions
 */
CanProperties_T HW_CAN_compute_properties( uint32_t bitrate, uint32_t total_TQ,
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
int HW_CAN_configure1( uint32_t bitrate );

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
int HW_CAN_configure2( uint32_t bitrate );

/**
 * @brief recieves data and stores it in rxData (8 bytes) over the hcan CAN channel 1
 *
 * @param hcan the pointer to the handle for the can peripheral
 * @param rxData pointer to 8 bytes of available storage
 *
 * Uses HAL to recieve message over CAN channel 1
 */
int HW_CAN_recieve1( uint8_t* rxData );

/**
 * @brief transmits the txData (8 bytes) over CAN channel 1
 *
 * @param txData pointer to 8 bytes of data
 *
 * Uses HAL to transmit message over CAN channel 1
 */
int HW_CAN_transmit1( uint8_t* txData );

/**
 * @brief recieves data and stores it in rxData (8 bytes) over the hcan CAN channel 2
 *
 * @param hcan the pointer to the handle for the can peripheral
 * @param rxData pointer to 8 bytes of available storage
 *
 * Uses HAL to recieve message over CAN channel 2
 */
int HW_CAN_recieve2( uint8_t* rxData );

/**
 * @brief transmits the txData (8 bytes) over CAN channel 2
 *
 * @param txData pointer to 8 bytes of data
 *
 * Uses HAL to transmit message over CAN channel 2
 */
int HW_CAN_transmit2( uint8_t* txData );

#ifdef __cplusplus
}
#endif

#endif /* <FILENAME>_H */
