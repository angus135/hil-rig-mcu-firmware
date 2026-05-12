/******************************************************************************
 *  File:       exec_can.c
 *  Author:     Timothy Vogelsang
 *  Created:    25-Mar-2026
 *
 *  Description:
 *      <Short description of the module's purpose and responsibilities>
 *
 *  Notes:
 *      <Any design notes, dependencies, or assumptions go here>
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include <stdint.h>
#include <stdbool.h>

#include "exec_can.h"
#include "hw_can.h"

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Public (global) and Extern Variables
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Private (static) Variables
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Private (static) Function Prototypes
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Public Function Definitions
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
int EXEC_CAN_recieve1( uint8_t** rxData, uint32_t length )
{
    for ( int i = 0; i < length; i++ )
    {
        if ( HW_CAN_recieve1( rxData[i] ) != 0 )
        {
            return 1;
        }
    }
    return 0;
}

/**
 * @brief recieves data and stores it in rxData (8 bytes) over the hcan CAN channel 2
 *
 * @param rxData pointer to array of 8 bytes sections of available storage
 * @param length the number of 8 byte sections in rxData (the amount of CAN packages being read)
 *
 * Uses HAL to receive message over CAN channel 2
 */
int EXEC_CAN_recieve2( uint8_t** rxData, uint32_t length )
{
    for ( int i = 0; i < length; i++ )
    {
        if ( HW_CAN_recieve2( rxData[i] ) != 0 )
        {
            return 1;
        }
    }
    return 0;
}

/**
 * @brief transmits a number of the txData (8 bytes) over CAN channel 1
 *
 * @param txData pointer to array of 8 bytes sections of data
 *
 * Uses HAL to transmit message over CAN channel 1
 */
int EXEC_CAN_transmit1( uint8_t** txData, uint32_t length )
{
    for ( int i = 0; i < length; i++ )
    {
        if ( HW_CAN_transmit1( txData[i] ) != 0 )
        {
            return 1;
        }
    }
    return 0;
}

/**
 * @brief transmits a number of the txData (8 bytes) over CAN channel 2
 *
 * @param txData pointer to array of 8 bytes sections of data
 *
 * Uses HAL to transmit message over CAN channel 2
 */
int EXEC_CAN_transmit2( uint8_t** txData, uint32_t length )
{
    for ( int i = 0; i < length; i++ )
    {
        if ( HW_CAN_transmit2( txData[i] ) != 0 )
        {
            return 1;
        }
    }
    return 0;
}