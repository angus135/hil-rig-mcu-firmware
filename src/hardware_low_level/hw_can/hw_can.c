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

#include <string.h>
#ifndef TEST_BUILD
#include "can.h"
#include "stm32f4xx_hal_can.h"
#else
#include "tests/hw_can_mocks.h"
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

#define RECEIVE_BUFFER_WIDTH 20
#define TRANSMIT_BUFFER_WIDTH 20


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

/**
 * At the moment these flags are set to true by the tx ISR when it has finished emptying the buffer
 * How I expect it to work is we call trigger, in time step x, then at x+1 we check if the flag
 * if the flag is true that means that all messages that were in the buffer when it was triggered
 * have been sent. Even tho this diverges slightly from what we discussed I think it is the best
 * approach because CAN messages are so small.
 *
 * If we think its absolutely neccesary to be able to check the status of each message
 * then I have an implementation in mind.
 */
static volatile bool can_sent_flag1 = false;
static volatile bool can_sent_flag2 = false;

// Buffer for rx channel 1
static volatile uint8_t  can_rx_buffer1[RECEIVE_BUFFER_WIDTH][CAN_PACKET_SIZE];
static volatile uint16_t can_rx_wp1 = 0;  // Writing to RX buffer handled by ISR
static volatile uint16_t can_rx_rp1 = 0;  // Reading from RX buffer handled by HW_CAN_rx_buffer_read

// Buffer for tx channel 1
static volatile uint8_t  can_tx_buffer1[TRANSMIT_BUFFER_WIDTH][CAN_PACKET_SIZE];
static volatile uint16_t can_tx_wp1 = 0;  // Writing to TX buffer handled by HW_CAN_tx_buffer_write
static volatile uint16_t can_tx_rp1 = 0;  // Reading from TX buffer handled by ISR

// Buffer for rx channel 2
static volatile uint8_t  can_rx_buffer2[RECEIVE_BUFFER_WIDTH][CAN_PACKET_SIZE];
static volatile uint16_t can_rx_wp2 = 0;  // Writing to RX buffer handled by ISR
static volatile uint16_t can_rx_rp2 = 0;  // Reading from RX buffer handled by HW_CAN_rx_buffer_read

// Buffer for tx channel 2
static volatile uint8_t  can_tx_buffer2[TRANSMIT_BUFFER_WIDTH][CAN_PACKET_SIZE];
static volatile uint16_t can_tx_wp2 = 0;  // Writing to TX buffer handled by HW_CAN_tx_buffer_write
static volatile uint16_t can_tx_rp2 = 0;  // Reading from TX buffer handled by ISR

/** Buffer example:
 *          [0,0,0,0,0,0,0,0],  <- r_p
 *          [0,0,0,0,0,0,0,0],
 *          [0,0,0,0,0,0,0,0],
 *   w_p ->  [0,0,0,0,0,0,0,0],
 *          [0,0,0,0,0,0,0,0],
 *          [0,0,0,0,0,0,0,0],
 *          [0,0,0,0,0,0,0,0],
 *
 */

// IRQ Re-Definitions
void HW_CAN_CH1_TX_IRQ_HANDLER( void );
void HW_CAN_CH1_RX_IRQ_HANDLER( void );
void HW_CAN_CH2_TX_IRQ_HANDLER( void );
void HW_CAN_CH2_RX_IRQ_HANDLER( void );

/**-----------------------------------------------------------------------------
 *  Private (static) Function Prototypes
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Private Execution Function Definitions
 *------------------------------------------------------------------------------
 */

/**
 * @brief writes source to the buffer
 *
 * @param buffer    A pointer to the buffer being used
 * @param w_p       the address of the write pointer
 * @param r_p       the address of the read pointer
 * @param buffer_width  The width of the buffer CAN_PACKET_SIZE (8)
 * @param source    A pointer to the source being used
 * @param length    the number of packets being written from source
 *
 * @return 1 if there is no room in the buffer
 * @return 0 if all of the elements have been written to the buffer
 *
 * @note The w_p always points to the next available position.
Meaning if the next position in the buffer has the r_p then the buffer is full.
e.g. here the buffer is 'full', even if ther is technically 1 spot left
 *          [0,0,0,0,0,0,0,0],
 *          [0,0,0,0,0,0,0,0],
 *          [0,0,0,0,0,0,0,0],  <- w_p
 *   r_p -> [0,0,0,0,0,0,0,0],
 *          [0,0,0,0,0,0,0,0],
 *          [0,0,0,0,0,0,0,0],
 *          [0,0,0,0,0,0,0,0],
 */
static inline uint8_t HW_CAN_Buffer_Write( volatile uint8_t   buffer[][CAN_PACKET_SIZE],
                                           volatile uint16_t* w_p, volatile uint16_t* r_p,
                                           uint16_t buffer_width, uint8_t source[][CAN_PACKET_SIZE],
                                           uint16_t length )
{
    for ( int i = 0; i < length; i++ )
    {
        // buffer full?
        if ( ( ( *w_p + 1 ) % buffer_width ) == *r_p )
        {
            return 1;
        }
        // iterate through packet
        for ( int j = 0; j < CAN_PACKET_SIZE; j++ )
        {
            buffer[*w_p][j] = source[i][j];
        }
        // update w_p
        *w_p = ( *w_p + 1 ) % buffer_width;
    }
    return 0;
}

/**
 * @brief reads many entries from the buffer
 *
 * @param buffer    A pointer to the buffer being used
 * @param w_p       the address of the write pointer
 * @param r_p       the address of the read pointer
 * @param buffer_width  The width of the buffer CAN_PACKET_SIZE (8)
 * @param dest      the destination array it writes to
 *
 * @return the number of entries read (can be 0)
 *
 * @note The w_p always points to the next available position.
Meaning if the next position in the buffer has the r_p then the buffer is full.
e.g. here the buffer is 'full', even if ther is technically 1 spot left
 *
 */
static inline uint16_t HW_CAN_Buffer_Read( volatile uint8_t   buffer[][CAN_PACKET_SIZE],
                                           volatile uint16_t* w_p, volatile uint16_t* r_p,
                                           uint16_t buffer_width, uint8_t dest[][CAN_PACKET_SIZE] )
{
    uint16_t temp_r_p = *r_p;
    uint16_t temp_w_p = *w_p;
    uint16_t count =
        temp_w_p < temp_r_p ? buffer_width - temp_r_p + temp_w_p + 1 : temp_w_p - temp_r_p;
    memcpy(dest, buffer, count);
    return count;
}

/**
 * @brief Moves the pointer x times
 *
 * @param pointer       the address of the read pointer
 * @param update        the number of times we want to move the pointer
 * @param buffer_width  The width of the buffer CAN_PACKET_SIZE (8)
 *
 */
static inline void HW_CAN_Buffer_consume( volatile uint16_t* pointer, uint16_t update, uint16_t buffer_width )
{
    *pointer = ( *pointer + update ) % buffer_width;
}

/**
 * @brief transmits the txData (8 bytes) over the hcan CAN channel
 *
 * @param hcan the pointer to the handle for the can peripheral
 * @param txData pointer to 8 bytes of data
 *
 * Uses HAL to transmit message over CAN channel
 */
int HW_CAN_Transmit( CAN_HandleTypeDef* hcan, uint8_t* txData )
{
    CAN_TypeDef* can = hcan->Instance;

    // Check mailbox available
    if ( ( can->TSR & CAN_TSR_TME0 ) == 0 )
    {
        return 1;
    }

    // Standard ID = 0x123
    can->sTxMailBox[0].TIR = ( 0x123 << 21 );

    // DLC = 8
    can->sTxMailBox[0].TDTR = 8;

    // Load first 4 bytes
    can->sTxMailBox[0].TDLR = ( ( uint32_t )txData[0] << 0 ) | ( ( uint32_t )txData[1] << 8 )
                              | ( ( uint32_t )txData[2] << 16 ) | ( ( uint32_t )txData[3] << 24 );

    // Load second 4 bytes
    can->sTxMailBox[0].TDHR = ( ( uint32_t )txData[4] << 0 ) | ( ( uint32_t )txData[5] << 8 )
                              | ( ( uint32_t )txData[6] << 16 ) | ( ( uint32_t )txData[7] << 24 );

    // Request transmission
    can->sTxMailBox[0].TIR |= CAN_TI0R_TXRQ;

    return 0;
}

/**
 * @brief recieves data and stores it in rxData (8 bytes) over the hcan CAN channel
 *
 * @param hcan the pointer to the handle for the can peripheral
 * @param rxData pointer to 8 bytes of available storage
 *
 * Uses HAL to receive message over CAN channel
 */
int HW_CAN_Receive( CAN_HandleTypeDef* hcan, uint8_t* rxData )
{
    CAN_TypeDef* can = hcan->Instance;

    // Check FIFO0 has pending message
    if ( ( can->RF0R & CAN_RF0R_FMP0 ) == 0 )
    {
        return 1;
    }

    uint32_t low  = can->sFIFOMailBox[0].RDLR;
    uint32_t high = can->sFIFOMailBox[0].RDHR;

    rxData[0] = ( low >> 0 ) & 0xFF;
    rxData[1] = ( low >> 8 ) & 0xFF;
    rxData[2] = ( low >> 16 ) & 0xFF;
    rxData[3] = ( low >> 24 ) & 0xFF;

    rxData[4] = ( high >> 0 ) & 0xFF;
    rxData[5] = ( high >> 8 ) & 0xFF;
    rxData[6] = ( high >> 16 ) & 0xFF;
    rxData[7] = ( high >> 24 ) & 0xFF;

    // Release FIFO
    can->RF0R |= CAN_RF0R_RFOM0;

    return 0;
}

/**
 * @brief reads one entrie from the buffer
 *
 * @param buffer    A pointer to the buffer being used
 * @param w_p       the address of the write pointer
 * @param r_p       the address of the read pointer
 * @param buffer_width  The width of the buffer CAN_PACKET_SIZE (8)
 * @param dest      the destination array it writes to
 *
 * @return 1 if there was nothing to read
 * @return 0 if the buffer was read correctly
 *
 *
 */
uint16_t HW_CAN_Buffer_Pop( volatile uint8_t buffer[][CAN_PACKET_SIZE], volatile uint16_t* w_p,
                            volatile uint16_t* r_p, uint16_t buffer_width,
                            uint8_t dest[CAN_PACKET_SIZE] )
{
    if ( *w_p == *r_p )
    {
        // r_p is up to w_p so nothing to read
        return 1;
    }

    for ( int i = 0; i < CAN_PACKET_SIZE; i++ )
    {
        dest[i] = buffer[*r_p][i];
    }
    // update read pointer
    *r_p = ( *r_p + 1 ) % buffer_width;

    return 0;
}

/**
 * @brief reads one entrie from the tx channel 1 buffer
 *
 * @param dest      the destination array it writes to
 *
 * @return 1 if there was nothing to read
 * @return 0 if the buffer was read correctly
 *
 *
 */
uint16_t HW_CAN_Tx_Buffer_Pop1( uint8_t dest[CAN_PACKET_SIZE] )
{
    return HW_CAN_Buffer_Pop(can_tx_buffer1, &can_tx_wp1, &can_tx_rp1, TRANSMIT_BUFFER_WIDTH, dest);
}

/**
 * @brief reads one entrie from the tx channel 2 buffer
 *
 * @param dest      the destination array it writes to
 *
 * @return 1 if there was nothing to read
 * @return 0 if the buffer was read correctly
 *
 *
 */
uint16_t HW_CAN_Tx_Buffer_Pop2( uint8_t dest[CAN_PACKET_SIZE] )
{
    return HW_CAN_Buffer_Pop(can_tx_buffer2, &can_tx_wp2, &can_tx_rp2, TRANSMIT_BUFFER_WIDTH, dest);
}

/**-----------------------------------------------------------------------------
 *  Private Configuration Function Definitions
 *------------------------------------------------------------------------------
 */

/**
 * @brief Applies the CAN timing peripherals, part of CAN configuration
 *
 * @param hcan pointer to the handle for the can peripheral
 * @param props the can properties, as calculated by HW_CAN_Compute_Properties
 *
 * This function takes and applies the desired can properties using the HAL library
 *
 */
HAL_StatusTypeDef HW_CAN_Apply_Timing_HAL( CAN_HandleTypeDef* hcan, CanProperties_T props )
{
    if ( props.bs1 == 0 || props.bs2 == 0 || props.psc == 0 || props.timer_hz == 0 )
    {
        return HAL_ERROR;
    }
    // set prescaler
    hcan->Init.Prescaler = props.psc;
    // set CAN operating mode
    hcan->Init.Mode = CAN_MODE_NORMAL;

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
        default:
            return HAL_ERROR;
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
        default:
            return HAL_ERROR;
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
HAL_StatusTypeDef HW_CAN_Apply_Filter_HAL( CAN_FilterTypeDef* filter, CAN_HandleTypeDef* hcan )
{
    ( *filter ).FilterMode  = CAN_FILTERMODE_IDMASK;
    ( *filter ).FilterScale = CAN_FILTERSCALE_32BIT;

    ( *filter ).FilterIdHigh     = 0x0000;
    ( *filter ).FilterIdLow      = 0x0000;
    ( *filter ).FilterMaskIdHigh = 0x0000;
    ( *filter ).FilterMaskIdLow  = 0x0000;

    ( *filter ).SlaveStartFilterBank = 14;

    if ( hcan->Instance == CAN1 )
    {
        ( *filter ).FilterBank = 0;
    }
    else if ( hcan->Instance == CAN2 )
    {
        ( *filter ).FilterBank = 14;
    }
    else
    {
        return HAL_ERROR;
    }

    ( *filter ).FilterFIFOAssignment = CAN_FILTER_FIFO0;
    ( *filter ).FilterActivation     = ENABLE;

    return HAL_CAN_ConfigFilter( hcan, filter );
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
int HW_CAN_Configure( CAN_HandleTypeDef* hcan, uint32_t bitrate )
{
    CAN_FilterTypeDef filter    = { 0 };
    CanProperties_T   can_props = HW_CAN_Compute_Properties( bitrate, TOTAL_TQ, MBPS_SAMPLE_POINT );
    if ( HW_CAN_Apply_Timing_HAL( hcan, can_props ) != HAL_OK )
    {
        return 1;
    }
    if ( HW_CAN_Apply_Filter_HAL( &filter, hcan ) != HAL_OK )
    {
        return 2;
    }
    if ( HAL_CAN_Start( hcan ) != HAL_OK )
    {
        return 3;
    }
    if ( HAL_CAN_ActivateNotification( hcan, CAN_IT_RX_FIFO0_MSG_PENDING | CAN_IT_TX_MAILBOX_EMPTY )
         != HAL_OK )
    {
        return 4;
    }
    return 0;
}

/**-----------------------------------------------------------------------------
 *  Public Configure Function Definitions
 *------------------------------------------------------------------------------
 */

/**
 * @brief Calculates the required CAN properties
 *
 * @param bitrate the desired bitrate in bits per second, eg 1Mbps = 1000000
 * @param total_TQ the total time quanta
 * @param sample_point_1t1000 the desired sample point, range 700 to 1000 (typically %)
 *
 * Computes the register values for the given conditions
 */
CanProperties_T HW_CAN_Compute_Properties( uint32_t bitrate, uint32_t total_TQ,
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
int HW_CAN_Configure1( uint32_t bitrate )
{
    // __HAL_RCC_CAN1_FORCE_RESET();
    // __HAL_RCC_CAN1_RELEASE_RESET();
    __HAL_RCC_CAN1_CLK_ENABLE();
    return HW_CAN_Configure( &hcan1, bitrate );
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
int HW_CAN_Configure2( uint32_t bitrate )
{
    // __HAL_RCC_CAN2_FORCE_RESET();
    // __HAL_RCC_CAN2_RELEASE_RESET();
    __HAL_RCC_CAN2_CLK_ENABLE();
    return HW_CAN_Configure( &hcan2, bitrate );
}

/**-----------------------------------------------------------------------------
 *  Public Execution Function Definitions
 *------------------------------------------------------------------------------
 */

/**
 * @brief If True then all channel 1 messages have been sent, since the last trigger
 *
 *
 * The sent flag is set flase after trigger is called when CAN has emptied the buffer
 * and set true when the last message is sent and the buffer is ready for a new message
 */
bool HW_CAN_Channl1_sent()
{
    return can_sent_flag1;
}

/**
 * @brief If True then all channel 2 messages have been sent, since the last trigger
 *
 *
 * The sent flag is set flase after trigger is called when CAN has emptied the buffer
 * and set true when the last message is sent and the buffer is ready for a new message
 */
bool HW_CAN_Channl2_sent()
{
    return can_sent_flag2;
}

/**
 * @brief transmits the txData (8 bytes) over CAN channel 1
 *
 * @param txData pointer to 8 bytes of data
 *
 * Uses HAL to transmit message over CAN channel 1
 */
int HW_CAN_Transmit1( uint8_t* txData )
{
    return HW_CAN_Transmit( &hcan1, txData );
}

/**
 * @brief recieves data and stores it in rxData (8 bytes) over the hcan CAN channel 1
 *
 * @param rxData pointer to 8 bytes of available storage
 *
 * Uses HAL to receive message over CAN channel 1
 */
int HW_CAN_Recieve1( uint8_t* rxData )
{
    return HW_CAN_Receive( &hcan1, rxData );
}

/**
 * @brief transmits the txData (8 bytes) over CAN channel 2
 *
 * @param txData pointer to 8 bytes of data
 *
 * Uses HAL to transmit message over CAN channel 2
 */
int HW_CAN_Transmit2( uint8_t* txData )
{
    return HW_CAN_Transmit( &hcan2, txData );
}

/**
 * @brief recieves data and stores it in rxData (8 bytes) over the hcan CAN channel 2
 *
 * @param rxData pointer to 8 bytes of available storage
 *
 * Uses HAL to receive message over CAN channel 2
 */
int HW_CAN_Recieve2( uint8_t* rxData )
{
    return HW_CAN_Receive( &hcan2, rxData );
}

/**
 * @brief Writes a number of 8 byte packets (source) to the tx buffer
 *
 * @param source an array of arrays, type:
uint8_t can_tx_buffer1[X][CAN_PACKET_SIZE];
 * @param length the number of can packets to be written (seen as X above)
 *
 * @return 0 if the write was succesful, 1 otherwise. (partially succesful = 1)
 */
uint16_t HW_CAN_Tx_Buffer_Write1( uint8_t source[][CAN_PACKET_SIZE], uint16_t length )
{
    return HW_CAN_Buffer_Write( can_tx_buffer1, &can_tx_wp1, &can_tx_rp1, TRANSMIT_BUFFER_WIDTH,
                                source, length );
}

/**
 * @brief Writes a number of 8 byte packets (source) to the rx buffer
 *
 * @param source an array of arrays, type:
uint8_t can_rx_buffer1[X][CAN_PACKET_SIZE];
 * @param length the number of can packets to be written (seen as X above)
 *
 * @return 0 if the write was succesful, 1 otherwise. (partially succesful = 1)
 */
uint16_t HW_CAN_Rx_Buffer_Write1( uint8_t source[][CAN_PACKET_SIZE], uint16_t length )
{
    return HW_CAN_Buffer_Write( can_rx_buffer1, &can_rx_wp1, &can_rx_rp1, RECEIVE_BUFFER_WIDTH,
                                source, length );
}

/**
 * @brief Writes a number of 8 byte packets (source) to the tx buffer
 *
 * @param source an array of arrays, type:
uint8_t can_tx_buffer1[X][CAN_PACKET_SIZE];
 * @param length the number of can packets to be written (seen as X above)
 *
 * @return 0 if the write was succesful, 1 otherwise. (partially succesful = 1)
 */
uint16_t HW_CAN_Tx_Buffer_Write2( uint8_t source[][CAN_PACKET_SIZE], uint16_t length )
{
    return HW_CAN_Buffer_Write( can_tx_buffer2, &can_tx_wp2, &can_tx_rp2, TRANSMIT_BUFFER_WIDTH,
                                source, length );
}

/**
 * @brief Writes a number of 8 byte packets (source) to the rx buffer
 *
 * @param source an array of arrays, type:
uint8_t can_rx_buffer1[X][CAN_PACKET_SIZE];
 * @param length the number of can packets to be written (seen as X above)
 *
 * @return 0 if the write was succesful, 1 otherwise. (partially succesful = 1)
 */
uint16_t HW_CAN_Rx_Buffer_Write2( uint8_t source[][CAN_PACKET_SIZE], uint16_t length )
{
    return HW_CAN_Buffer_Write( can_rx_buffer2, &can_rx_wp2, &can_rx_rp2, RECEIVE_BUFFER_WIDTH,
                                source, length );
}

/**
 * @brief Reads from the rx buffer (channel 2) and writes it to dest
 *
 * @param dest the destination where the value will be written
 *
 * @return the number of CAN_PACKET_SIZE's read
 */
uint16_t HW_CAN_Rx_Buffer_Read1( uint8_t dest[][CAN_PACKET_SIZE] )
{
    return HW_CAN_Buffer_Read( can_rx_buffer1, &can_rx_wp1, &can_rx_rp1, RECEIVE_BUFFER_WIDTH,
                              dest );
}

/**
 * @brief Moves the channe 1 read pointer x times
 *
 * @param update        the number of times we want to move the pointer
 *
 */
void HW_CAN_Rx_Buffer_consume1( uint16_t update )
{
    HW_CAN_Buffer_consume(&can_rx_rp1, update, RECEIVE_BUFFER_WIDTH);
}

/**
 * @brief Reads from the rx buffer (channel 2) and writes it to dest
 *
 * @param dest the destination where the value will be written
 *
 * @return the number of CAN_PACKET_SIZE's read
 */
uint16_t HW_CAN_Rx_Buffer_Read2( uint8_t dest[][CAN_PACKET_SIZE] )
{
    return HW_CAN_Buffer_Read( can_rx_buffer2, &can_rx_wp2, &can_rx_rp2, RECEIVE_BUFFER_WIDTH,
                              dest );
}

/**
 * @brief Moves the channe 2 read pointer x times
 *
 * @param update        the number of times we want to move the pointer
 *
 */
void HW_CAN_Rx_Buffer_consume2( uint16_t update )
{
    HW_CAN_Buffer_consume(&can_rx_rp2, update, RECEIVE_BUFFER_WIDTH);
}

/**
 * @brief Reads from the tx buffer (channel 1) and writes it to dest
 *
 * @param dest the destination where the value will be written
 *
 * @return the number of CAN_PACKET_SIZE's read
 */
uint16_t HW_CAN_Tx_Buffer_Read1( uint8_t dest[][CAN_PACKET_SIZE] )
{
    return HW_CAN_Buffer_Read( can_tx_buffer1, &can_tx_wp1, &can_tx_rp1, TRANSMIT_BUFFER_WIDTH,
                              dest );
}

/**
 * @brief Reads from the tx buffer (channel 2) and writes it to dest
 *
 * @param dest the destination where the value will be written
 *
 * @return the number of CAN_PACKET_SIZE's read
 */
uint16_t HW_CAN_Tx_Buffer_Read2( uint8_t dest[][CAN_PACKET_SIZE] )
{
    return HW_CAN_Buffer_Read( can_tx_buffer2, &can_tx_wp2, &can_tx_rp2, TRANSMIT_BUFFER_WIDTH,
                              dest );
}

/**
 * @brief Enables tx interrupts on channel 1
 *
 * Used to enable the sending of messages through CAN channel 1
 * Once the write buffer is empty the ISR will disable again
 */
void HW_CAN_Tx_Trigger1( void )
{
    SET_BIT( CAN1->IER, CAN_IER_TMEIE );
    HW_CAN_CH1_TX_IRQ_HANDLER();  // ISR doesn't trigger automatically
}

/**
 * @brief Enables tx interrupts on channel 2
 *
 * Used to enable the sending of messages through CAN channel 2
 * Once the write buffer is empty the ISR will disable again
 */
void HW_CAN_Tx_Trigger2( void )
{
    SET_BIT( CAN2->IER, CAN_IER_TMEIE );
    HW_CAN_CH2_TX_IRQ_HANDLER();  // ISR doesn't trigger automatically
}

/**
 * @brief  ...
 *
 *
 * @note   This handler must remain minimal and deterministic. No blocking or
 *         heavy processing should be introduced here.
 */
void HW_CAN_CH1_TX_IRQ_HANDLER( void )
{
    uint8_t packet[CAN_PACKET_SIZE];
    hcan1.Instance->TSR |= CAN_TSR_RQCP0;
    if ( HW_CAN_Tx_Buffer_Pop1( packet ) == 0 )
    {
        HW_CAN_Transmit( &hcan1, packet );
    }
    else
    {
        // No more packets to send.
        // Disable TX mailbox empty interrupt.
        CLEAR_BIT( hcan1.Instance->IER, CAN_IER_TMEIE );
        // all can messages that were in the buffer have been sent
        can_sent_flag1 = true;
    }
}

/**
 * @brief  ...
 *
 *
 * @note   This handler must remain minimal and deterministic. No blocking or
 *         heavy processing should be introduced here.
 */
void HW_CAN_CH1_RX_IRQ_HANDLER( void )
{
    uint8_t packet[1][CAN_PACKET_SIZE];

    if ( HW_CAN_Receive( &hcan1, packet[0] ) == 0 )
    {
        HW_CAN_Rx_Buffer_Write1( packet, 1 );
    }
}

/**
 * @brief  ...
 *
 *
 * @note   This handler must remain minimal and deterministic. No blocking or
 *         heavy processing should be introduced here.
 */
void HW_CAN_CH2_TX_IRQ_HANDLER( void )
{
    hcan2.Instance->TSR |= CAN_TSR_RQCP0;
    uint8_t packet[CAN_PACKET_SIZE];

    if ( HW_CAN_Tx_Buffer_Pop2( packet ) == 0 )
    {
        HW_CAN_Transmit( &hcan2, packet );
    }
    else
    {
        // No more packets to send.
        // Disable TX mailbox empty interrupt.
        CLEAR_BIT( hcan2.Instance->IER, CAN_IER_TMEIE );
        // all can messages that were in the buffer have been sent
        can_sent_flag2 = true;
    }
}

/**
 * @brief  ...
 *
 *
 * @note   This handler must remain minimal and deterministic. No blocking or
 *         heavy processing should be introduced here.
 */
void HW_CAN_CH2_RX_IRQ_HANDLER( void )
{
    uint8_t packet[1][CAN_PACKET_SIZE];

    if ( HW_CAN_Receive( &hcan2, packet[0] ) == 0 )
    {
        HW_CAN_Rx_Buffer_Write2( packet, 1 );
    }
}
