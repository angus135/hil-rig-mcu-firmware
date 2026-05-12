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
 * @brief recieves data and stores it in rxData (8 bytes) over the hcan CAN channel 1
 *
 * @param rxData pointer to array of 8 bytes sections of available storage
 * @param length the number of 8 byte sections in rxData (the amount of CAN packages being read)
 *
 * Uses HAL to receive message over CAN channel 1
 */
int EXEC_CAN_recieve1( uint8_t** rxData, uint32_t length );

/**
 * @brief recieves data and stores it in rxData (8 bytes) over the hcan CAN channel 2
 *
 * @param rxData pointer to array of 8 bytes sections of available storage
 * @param length the number of 8 byte sections in rxData (the amount of CAN packages being read)
 *
 * Uses HAL to receive message over CAN channel 2
 */
int EXEC_CAN_recieve2( uint8_t** rxData, uint32_t length );

/**
 * @brief transmits a number of the txData (8 bytes) over CAN channel 1
 *
 * @param txData pointer to array of 8 bytes sections of data
 *
 * Uses HAL to transmit message over CAN channel 1
 */
int EXEC_CAN_transmit1( uint8_t** txData, uint32_t length );

/**
 * @brief transmits a number of the txData (8 bytes) over CAN channel 2
 *
 * @param txData pointer to array of 8 bytes sections of data
 *
 * Uses HAL to transmit message over CAN channel 2
 */
int EXEC_CAN_transmit2( uint8_t** txData, uint32_t length );

#ifdef __cplusplus
}
#endif

#endif /* EXEC_CAN_H */
