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

int HW_CAN_configure1( uint32_t bitrate );

int HW_CAN_configure2( uint32_t bitrate );

int HW_CAN_recieve1(uint8_t * rxData);

int HW_CAN_transmit1( uint8_t* txData );

int HW_CAN_recieve2(uint8_t * rxData);

int HW_CAN_transmit2( uint8_t * txData);

#ifdef __cplusplus
}
#endif

#endif /* <FILENAME>_H */
