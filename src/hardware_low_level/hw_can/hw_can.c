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

#define CAN_TIMER_HZ 45000000
#define TOTAL_TQ ( uint32_t )15
#define MBPS_SAMPLE_POINT ( uint32_t )800

#define RECIEVE_BUFFER_WIDTH 10
#define TRANSMIT_BUFFER_WIDTH 10
#define CAN_PACKET_SIZE 8

#define HW_UART_CH1_TX_IRQ_HANDLER CAN1_TX_IRQHandler
#define HW_UART_CH1_RX_IRQ_HANDLER CAN1_RX0_IRQHandler
#define HW_UART_CH2_TX_IRQ_HANDLER CAN2_TX_IRQHandler
#define HW_UART_CH2_RX_IRQ_HANDLER CAN2_RX0_IRQHandler

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

static uint8_t  can_rx_buffer[RECIEVE_BUFFER_WIDTH][CAN_PACKET_SIZE];
static uint16_t can_rx_wp = 0;  // Writing to RX buffer handled by ISR
static uint16_t can_rx_rp = 0;  // Reading from RX buffer handled by HW_CAN_rx_buffer_read

static uint8_t  can_tx_buffer[TRANSMIT_BUFFER_WIDTH][CAN_PACKET_SIZE];
static uint16_t can_tx_wp = 0;  // Writing to TX buffer handled by HW_CAN_tx_buffer_write
static uint16_t can_tx_rp = 0;  // Reading from TX buffer handled by ISR

/** Buffer example:
 *          [0,0,0,0,0,0,0,0],  <- rp
 *          [0,0,0,0,0,0,0,0],
 *          [0,0,0,0,0,0,0,0],
 *   wp ->  [0,0,0,0,0,0,0,0],
 *          [0,0,0,0,0,0,0,0],
 *          [0,0,0,0,0,0,0,0],
 *          [0,0,0,0,0,0,0,0],
 *
 */

/**-----------------------------------------------------------------------------
 *  Private (static) Function Prototypes
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Private Execution Function Definitions
 *------------------------------------------------------------------------------
 */

inline uint8_t HW_CAN_buffer_write( uint8_t** buffer, uint16_t* wp, uint16_t* rp,
                                    uint16_t buffer_width, uint8_t** source, uint16_t length )
{
    for ( int i = 0; i < length; i++ )
    {
        if ( ( ( *wp + 1 ) % buffer_width ) == *rp )
        {
            return 1;
        }
        for ( int j = 0; j < CAN_PACKET_SIZE; j++ )
        {
            buffer[*wp][j] = source[i][j];
        }
        *wp = ( *wp + 1 ) % buffer_width;
    }
    return 0;
}

inline uint16_t HW_CAN_buffer_read( uint8_t** buffer, uint16_t* wp, uint16_t* rp,
                                    uint16_t buffer_width, uint8_t** dest )
{
    uint16_t count = 0;
    for ( int i = 0; i < buffer_width; i++ )
    {
        if ( *wp == *rp )
        {
            return count;
        }
        for ( int j = 0; j < CAN_PACKET_SIZE; j++ )
        {
            dest[i][j] = buffer[*rp][j];
        }
        *rp = ( *rp + 1 ) % buffer_width;
        count += 1;
    }
    return count;
}

uint16_t HW_CAN_tx_buffer_write( uint8_t** source, uint16_t length )
{
    return HW_CAN_buffer_write( can_tx_buffer, &can_tx_wp, &can_tx_rp, TRANSMIT_BUFFER_WIDTH,
                                source, length );
}

uint16_t HW_CAN_tx_buffer_read( uint8_t** dest )
{
    return HW_CAN_buffer_read( can_tx_buffer, &can_tx_wp, &can_tx_rp, TRANSMIT_BUFFER_WIDTH, dest );
}

uint16_t HW_CAN_rx_buffer_write( uint8_t** source, uint16_t length )
{
    return HW_CAN_buffer_write( can_rx_buffer, &can_rx_wp, &can_rx_rp, RECIEVE_BUFFER_WIDTH, source,
                                length );
}

uint16_t HW_CAN_rx_buffer_read( uint8_t** dest )
{
    return HW_CAN_buffer_read( can_rx_buffer, &can_rx_wp, &can_rx_rp, RECIEVE_BUFFER_WIDTH, dest );
}

#ifndef TEST_BUILD
/**
 * @brief transmits the txData (8 bytes) over the hcan CAN channel
 *
 * @param hcan the pointer to the handle for the can peripheral
 * @param txData pointer to 8 bytes of data
 *
 * Uses HAL to transmit message over CAN channel
 */
int HW_CAN_transmit( CAN_HandleTypeDef* hcan, uint8_t* txData )
{
    CAN_TxHeaderTypeDef txHeader;
    uint32_t            txMailbox;

    txHeader.StdId = 0x123;
    txHeader.IDE   = CAN_ID_STD;
    txHeader.RTR   = CAN_RTR_DATA;
    txHeader.DLC   = 8;

    if ( HAL_CAN_AddTxMessage( hcan, &txHeader, txData, &txMailbox ) != HAL_OK )
    {
        // print error
        return 1;
    }
    return 0;
}

/**
 * @brief recieves data and stores it in rxData (8 bytes) over the hcan CAN channel
 *
 * @param hcan the pointer to the handle for the can peripheral
 * @param rxData pointer to 8 bytes of available storage
 *
 * Uses HAL to recieve message over CAN channel
 */
int HW_CAN_recieve( CAN_HandleTypeDef* hcan, uint8_t* rxData )
{
    CAN_RxHeaderTypeDef rxHeader;
    if ( HAL_CAN_GetRxMessage( hcan, CAN_RX_FIFO0, &rxHeader, rxData ) != HAL_OK )
    {
        // print error
        return 1;
    }
    return 0;
}

#endif

/**-----------------------------------------------------------------------------
 *  Private Configuration Function Definitions
 *------------------------------------------------------------------------------
 */

#ifndef TEST_BUILD
/**
 * @brief Applies the CAN timing peripherals, part of CAN configuration
 *
 * @param hcan pointer to the handle for the can peripheral
 * @param props the can properties, as calculated by HW_CAN_compute_properties
 *
 * This function takes and applies the desired can properties using the HAL library
 *
 */
HAL_StatusTypeDef HW_CAN_apply_timing_HAL( CAN_HandleTypeDef* hcan, CanProperties_T props )
{
    if ( props.bs2 == 0 || props.bs2 == 0 || props.psc == 0 || props.timer_hz == 0 )
    {
        return HAL_ERROR;
    }
    // set prescaler
    hcan->Init.Prescaler = props.psc;
    // set CAN operating mode
    // hcan.Init.Mode      = CAN_MODE_NORMAL;
    hcan->Init.Mode = CAN_MODE_LOOPBACK;  // Testing mode

    // set the sync jump width
    hcan->Init.SyncJumpWidth = CAN_SJW_1TQ;

    // Map BS1
    switch ( props.bs1 )
    {
        case 1:
            hcan->Init.TimeSeg1 = CAN_BS1_1TQ;
            break;
        case 2:
            hcan->Init.TimeSeg1 = CAN_BS1_2TQ;
            break;
        case 3:
            hcan->Init.TimeSeg1 = CAN_BS1_3TQ;
            break;
        case 4:
            hcan->Init.TimeSeg1 = CAN_BS1_4TQ;
            break;
        case 5:
            hcan->Init.TimeSeg1 = CAN_BS1_5TQ;
            break;
        case 6:
            hcan->Init.TimeSeg1 = CAN_BS1_6TQ;
            break;
        case 7:
            hcan->Init.TimeSeg1 = CAN_BS1_7TQ;
            break;
        case 8:
            hcan->Init.TimeSeg1 = CAN_BS1_8TQ;
            break;
        case 9:
            hcan->Init.TimeSeg1 = CAN_BS1_9TQ;
            break;
        case 10:
            hcan->Init.TimeSeg1 = CAN_BS1_10TQ;
            break;
        case 11:
            hcan->Init.TimeSeg1 = CAN_BS1_11TQ;
            break;
        case 12:
            hcan->Init.TimeSeg1 = CAN_BS1_12TQ;
            break;
        case 13:
            hcan->Init.TimeSeg1 = CAN_BS1_13TQ;
            break;
        case 14:
            hcan->Init.TimeSeg1 = CAN_BS1_14TQ;
            break;
        case 15:
            hcan->Init.TimeSeg1 = CAN_BS1_15TQ;
            break;
        case 16:
            hcan->Init.TimeSeg1 = CAN_BS1_16TQ;
            break;
    }

    // Map BS2
    switch ( props.bs2 )
    {
        case 1:
            hcan->Init.TimeSeg2 = CAN_BS2_1TQ;
            break;
        case 2:
            hcan->Init.TimeSeg2 = CAN_BS2_2TQ;
            break;
        case 3:
            hcan->Init.TimeSeg2 = CAN_BS2_3TQ;
            break;
        case 4:
            hcan->Init.TimeSeg2 = CAN_BS2_4TQ;
            break;
        case 5:
            hcan->Init.TimeSeg2 = CAN_BS2_5TQ;
            break;
        case 6:
            hcan->Init.TimeSeg2 = CAN_BS2_6TQ;
            break;
        case 7:
            hcan->Init.TimeSeg2 = CAN_BS2_7TQ;
            break;
        case 8:
            hcan->Init.TimeSeg2 = CAN_BS2_8TQ;
            break;
    }

    // Other required settings
    hcan->Init.TimeTriggeredMode    = DISABLE;
    hcan->Init.AutoBusOff           = DISABLE;
    hcan->Init.AutoWakeUp           = DISABLE;
    hcan->Init.AutoRetransmission   = ENABLE;
    hcan->Init.ReceiveFifoLocked    = DISABLE;
    hcan->Init.TransmitFifoPriority = DISABLE;

    return HAL_CAN_Init( hcan );
}

/**
 * @brief Applies the filter to the can peripherals
 *
 * @param filter The filter struct associated with hcan (that we are writing to)
 * @param hcan the pointer to the handle for the can peripheral
 *
 * This function applies the desired can filter properties using the HAL library
 *
 */
HAL_StatusTypeDef HW_CAN_apply_filter_HAL( CAN_FilterTypeDef filter, CAN_HandleTypeDef* hcan )
{
    filter.FilterBank  = 0;
    filter.FilterMode  = CAN_FILTERMODE_IDMASK;
    filter.FilterScale = CAN_FILTERSCALE_32BIT;

    filter.FilterIdHigh     = 0x0000;
    filter.FilterIdLow      = 0x0000;
    filter.FilterMaskIdHigh = 0x0000;
    filter.FilterMaskIdLow  = 0x0000;

    filter.FilterFIFOAssignment = CAN_FILTER_FIFO0;
    filter.FilterActivation     = ENABLE;

    return HAL_CAN_ConfigFilter( hcan, &filter );
}

/**
 * @brief Configures the CAN peripherals
 *
 * @param hcan the pointer to the handle for the can peripheral
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
int HW_CAN_configure( CAN_HandleTypeDef* hcan, uint32_t bitrate )
{
    CAN_FilterTypeDef filter;
    CanProperties_T   can_props = HW_CAN_compute_properties( bitrate, TOTAL_TQ, MBPS_SAMPLE_POINT );
    __HAL_RCC_CAN1_FORCE_RESET();
    __HAL_RCC_CAN1_RELEASE_RESET();
    __HAL_RCC_CAN1_CLK_ENABLE();
    if ( HW_CAN_apply_timing_HAL( hcan, can_props ) != HAL_OK )
    {
        return 1;
    }
    if ( HW_CAN_apply_filter_HAL( filter, hcan ) != HAL_OK )
    {
        return 2;
    }
    if ( HAL_CAN_Start( hcan ) != HAL_OK )
    {
        return 3;
    }
    return 0;
}

#endif

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
 * Computes the register values for the given conditions
 */
CanProperties_T HW_CAN_compute_properties( uint32_t bitrate, uint32_t total_TQ,
                                           uint32_t sample_point_1t1000 )
{
    if ( bitrate < 1 || bitrate > 1000000 )
    {
        // Bitrate out of bounds
        return ( CanProperties_T ){ 0, 0, 0, 0 };
    }
    if ( sample_point_1t1000 < 700 || sample_point_1t1000 > 1000 )
    {
        // sample point out of bounds
        // sample point should be between 70 and 100 %
        return ( CanProperties_T ){ 0, 0, 0, 0 };
    }
    uint32_t timer_hz = CAN_TIMER_HZ;
    uint32_t bs1      = ( sample_point_1t1000 * total_TQ ) / 1000 - 1;
    uint32_t bs2      = total_TQ - bs1 - 1;
    uint32_t psc      = timer_hz / ( bitrate * ( 1 + bs1 + bs2 ) );
    if ( bs1 < 1 || bs1 > 16 )
    {
        // bs1 out of bounds
        return ( CanProperties_T ){ 0, 0, 0, 0 };
    }
    if ( bs2 < 1 || bs2 > 8 )
    {
        // bs2 out of bounds
        return ( CanProperties_T ){ 0, 0, 0, 0 };
    }
    return ( CanProperties_T ){ bs1, bs2, psc, timer_hz };
}

#ifndef TEST_BUILD

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
int HW_CAN_configure1( uint32_t bitrate )
{
    return HW_CAN_configure( &hcan1, bitrate );
}

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
int HW_CAN_configure2( uint32_t bitrate )
{
    return HW_CAN_configure( &hcan2, bitrate );
}

#endif

/**-----------------------------------------------------------------------------
 *  Public Execution Function Definitions
 *------------------------------------------------------------------------------
 */

/**
 * @brief transmits the txData (8 bytes) over CAN channel 1
 *
 * @param txData pointer to 8 bytes of data
 *
 * Uses HAL to transmit message over CAN channel 1
 */
int HW_CAN_transmit1( uint8_t* txData )
{
#ifndef TEST_BUILD
    return HW_CAN_transmit( &hcan1, txData );
#else
    ( void )txData;
#endif
}

/**
 * @brief recieves data and stores it in rxData (8 bytes) over the hcan CAN channel 1
 *
 * @param rxData pointer to 8 bytes of available storage
 *
 * Uses HAL to recieve message over CAN channel 1
 */
int HW_CAN_recieve1( uint8_t* rxData )
{
#ifndef TEST_BUILD
    return HW_CAN_recieve( &hcan1, rxData );
#else
    ( void )rxData;
#endif
}

/**
 * @brief transmits the txData (8 bytes) over CAN channel 2
 *
 * @param txData pointer to 8 bytes of data
 *
 * Uses HAL to transmit message over CAN channel 2
 */
int HW_CAN_transmit2( uint8_t* txData )
{
#ifndef TEST_BUILD
    return HW_CAN_transmit( &hcan2, txData );
#else
    ( void )txData;
#endif
}

/**
 * @brief recieves data and stores it in rxData (8 bytes) over the hcan CAN channel 2
 *
 * @param rxData pointer to 8 bytes of available storage
 *
 * Uses HAL to recieve message over CAN channel 2
 */
int HW_CAN_recieve2( uint8_t* rxData )
{
#ifndef TEST_BUILD
    return HW_CAN_recieve( &hcan2, rxData );
#else
    ( void )rxData;
#endif
}
