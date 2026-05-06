/******************************************************************************
 *  File:       hw_can.c
 *  Author:     Timothy Vogelsang
 *  Created:    4-Apr-2026
 *
 *  Description:
 *      Implementation of the CAN hardware abstraction layer.
 *
 *  Notes:
HAL Typedef Hierachy 
 CAN_HandleTypeDef        ← "Driver instance / state"
    ↓
CAN_InitTypeDef          ← "Peripheral configuration"
    ↓
CAN_TypeDef              ← "Hardware registers (memory mapped)"
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */
#include <stddef.h>
#ifndef TEST_BUILD
#include "can.h"
#include "stm32f4xx_hal_can.h"
#endif
#include "hw_can.h"
#include <stdint.h>
#include <stdbool.h>

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */

#define CAN_TIMER_HZ 90000000
#define TOTAL_TQ ( uint32_t ) 18
#define MBPS_SAMPLE_POINT ( uint32_t ) 800

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
 * @brief Calculates the required CAN protperties
 *
 * @param bitrate the desired bitrate in bits per second, eg 1Mbps = 1000000
 * @param total_TQ the total time quanta
 * @param sample_point_1t1000 the desired sample point, range 700 to 1000 (typically %)
 *
 */
CanProperties_T HW_CAN_compute_properties( uint32_t bitrate, uint32_t total_TQ, uint32_t sample_point_1t1000 )
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
    uint32_t bs2      = total_TQ - bs1 - 1;
    uint32_t psc      = timer_hz / ( bitrate * ( 1 + bs1 + bs2 ) );
    return ( CanProperties_T ){ bs1, bs2, psc, timer_hz };
}


#ifndef TEST_BUILD
/**
 * @brief Applies the CAN timing peripherals
 *
 * @param CAN The base register address of the can peripheral, either CAN1 or CAN2
 * @param hcan the handle for the can peripheral 
 * @param props the can properties, as calculated by HW_CAN_compute_properties
 *
 * This function takes and applies the desired can properties using the HAL library
 *
 */
void HW_CAN_apply_timing_HAL( CAN_HandleTypeDef hcan, CanProperties_T props )
{
    CAN_TypeDef* CAN = hcan.Instance;

    hcan.Init.Prescaler = props.psc;
    hcan.Init.Mode      = CAN_MODE_NORMAL;

    hcan.Init.SyncJumpWidth = CAN_SJW_1TQ;

    // Map BS1
    switch ( props.bs1 )
    {
        case 1:
            hcan.Init.TimeSeg1 = CAN_BS1_1TQ;
            break;
        case 2:
            hcan.Init.TimeSeg1 = CAN_BS1_2TQ;
            break;
        case 3:
            hcan.Init.TimeSeg1 = CAN_BS1_3TQ;
            break;
        case 4:
            hcan.Init.TimeSeg1 = CAN_BS1_4TQ;
            break;
        case 5:
            hcan.Init.TimeSeg1 = CAN_BS1_5TQ;
            break;
        case 6:
            hcan.Init.TimeSeg1 = CAN_BS1_6TQ;
            break;
        case 7:
            hcan.Init.TimeSeg1 = CAN_BS1_7TQ;
            break;
        case 8:
            hcan.Init.TimeSeg1 = CAN_BS1_8TQ;
            break;
        case 9:
            hcan.Init.TimeSeg1 = CAN_BS1_9TQ;
            break;
        case 10:
            hcan.Init.TimeSeg1 = CAN_BS1_10TQ;
            break;
        case 11:
            hcan.Init.TimeSeg1 = CAN_BS1_11TQ;
            break;
        case 12:
            hcan.Init.TimeSeg1 = CAN_BS1_12TQ;
            break;
        case 13:
            hcan.Init.TimeSeg1 = CAN_BS1_13TQ;
            break;
        case 14:
            hcan.Init.TimeSeg1 = CAN_BS1_14TQ;
            break;
        case 15:
            hcan.Init.TimeSeg1 = CAN_BS1_15TQ;
            break;
        case 16:
            hcan.Init.TimeSeg1 = CAN_BS1_16TQ;
            break;
    }

    // Map BS2
    switch ( props.bs2 )
    {
        case 1:
            hcan.Init.TimeSeg2 = CAN_BS2_1TQ;
            break;
        case 2:
            hcan.Init.TimeSeg2 = CAN_BS2_2TQ;
            break;
        case 3:
            hcan.Init.TimeSeg2 = CAN_BS2_3TQ;
            break;
        case 4:
            hcan.Init.TimeSeg2 = CAN_BS2_4TQ;
            break;
        case 5:
            hcan.Init.TimeSeg2 = CAN_BS2_5TQ;
            break;
        case 6:
            hcan.Init.TimeSeg2 = CAN_BS2_6TQ;
            break;
        case 7:
            hcan.Init.TimeSeg2 = CAN_BS2_7TQ;
            break;
        case 8:
            hcan.Init.TimeSeg2 = CAN_BS2_8TQ;
            break;
    }

    // Other required settings
    hcan.Init.TimeTriggeredMode    = DISABLE;
    hcan.Init.AutoBusOff           = DISABLE;
    hcan.Init.AutoWakeUp           = DISABLE;
    hcan.Init.AutoRetransmission   = ENABLE;
    hcan.Init.ReceiveFifoLocked    = DISABLE;
    hcan.Init.TransmitFifoPriority = DISABLE;

    HAL_CAN_Init( &hcan );
}

/**
 * @brief Applies the filter to the can peripherals
 *
 * @param filter The filter struct associated with hcan
 * @param hcan the handle for the can peripheral 
 *
 * This function takes and applies the desired can filter properties using the HAL library
 *
 */
void HW_CAN_apply_filter_HAL( CAN_FilterTypeDef filter, CAN_HandleTypeDef hcan)
{
    filter.FilterBank = 0;
    filter.FilterMode = CAN_FILTERMODE_IDMASK;
    filter.FilterScale = CAN_FILTERSCALE_32BIT;

    filter.FilterIdHigh = 0x0000;
    filter.FilterIdLow  = 0x0000;
    filter.FilterMaskIdHigh = 0x0000;
    filter.FilterMaskIdLow  = 0x0000;

    filter.FilterFIFOAssignment = CAN_FILTER_FIFO0;
    filter.FilterActivation = ENABLE;

    HAL_CAN_ConfigFilter(&hcan, &filter);
}

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
void HW_CAN_configure(CAN_HandleTypeDef hcan, uint32_t bitrate)
{
    CAN_FilterTypeDef filter;
    CanProperties_T can_props = HW_CAN_compute_properties( bitrate, TOTAL_TQ, MBPS_SAMPLE_POINT );
    HW_CAN_apply_timing_HAL( hcan, can_props );
    HW_CAN_apply_filter_HAL(filter, hcan);
    return;
}
#endif

/**-----------------------------------------------------------------------------
 *  Public Execution Function Definitions
 *------------------------------------------------------------------------------
 */
