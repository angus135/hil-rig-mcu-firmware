/******************************************************************************
 *  File:       hw_can.c
 *  Author:     Timothy Vogelsang
 *  Created:    4-Apr-2026
 *
 *  Description:
 *      Implementation of the CAN hardware abstraction layer.
 *
 *  Notes:
 *      <Any design notes, dependencies, or assumptions go here>
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */
#include <stddef.h>
#ifndef TEST_BUILD
#include "can.h"
#endif
#include "hw_can.h"
#include <stdint.h>
#include <stdbool.h>

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */

#define CAN_TIMER_HZ 90000000

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

typedef struct CanProperties_T
{
    uint32_t bs1;
    uint32_t bs2;
    uint32_t psc;
    uint32_t timer_hz;
} CanProperties_T;

/**-----------------------------------------------------------------------------
 *  Private (static) Function Prototypes
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Public Configure Function Definitions
 *------------------------------------------------------------------------------
 */

/**
 * @brief Configures the CAN peripherals
 *
 * @param XXXX
 *
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
void HW_CAN_configure()
{
    return;
}

/**
 * @brief Calculates the required CAN protperties
 *
 * @param bitrate the desired bitrate in bits per second, eg 1Mbps = 1000000
 * @param total_TQ the total time quanta
 * @param sample_point_1t1000 the desired sample point, range 700 to 1000 (typically %)
 *
 *
 *
 */
CanProperties_T HW_CAN_compute_properties( uint32_t bitrate, uint32_t total_TQ,
                                           uint32_t sample_point_1t1000 )
{
    if ( bitrate < 1 || bitrate > 1000000 )
    {
        // Bitrate out of bounds
        return ( CanProperties_T ){ NULL, NULL, NULL, NULL };
    }
    if ( sample_point_1t1000 < 700 || sample_point_1t1000 > 1000 )
    {
        // sample point out of bounds
        // sample point should be between 70 and 100 %
        return ( CanProperties_T ){ NULL, NULL, NULL, NULL };
    }
    uint32_t timer_hz = CAN_TIMER_HZ;
    uint32_t bs1      = ( sample_point_1t1000 * total_TQ ) / 1000 - 1;
    uint32_t bs2      = total_TQ - bs1;
    uint32_t psc      = timer_hz / ( bitrate * ( 1 + bs1 + bs2 ) );
    return ( CanProperties_T ){ bs1, bs2, psc, timer_hz };
}

/**-----------------------------------------------------------------------------
 *  Public Execution Function Definitions
 *------------------------------------------------------------------------------
 */
