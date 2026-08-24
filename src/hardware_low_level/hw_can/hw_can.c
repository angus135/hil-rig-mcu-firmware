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

#define RECEIVE_BUFFER_WIDTH ( HW_CAN_RX_QUEUE_CAPACITY + 1U )
#define TRANSMIT_BUFFER_WIDTH ( HW_CAN_TX_QUEUE_CAPACITY + 1U )
#define CAN_RX_FIFO_DEPTH 3U

#define HW_CAN_CH1_TX_IRQ_HANDLER CAN1_TX_IRQHandler
#define HW_CAN_CH1_RX_IRQ_HANDLER CAN1_RX0_IRQHandler
#define HW_CAN_CH2_TX_IRQ_HANDLER CAN2_TX_IRQHandler
#define HW_CAN_CH2_RX_IRQ_HANDLER CAN2_RX0_IRQHandler
#define HW_CAN_CH1_ERROR_IRQ_HANDLER CAN1_SCE_IRQHandler
#define HW_CAN_CH2_ERROR_IRQ_HANDLER CAN2_SCE_IRQHandler

#define HW_CAN_RX_INTERRUPT_MASK ( CAN_IER_FMPIE0 | CAN_IER_FFIE0 | CAN_IER_FOVIE0 )
#define HW_CAN_ERROR_INTERRUPT_MASK                                                                \
    ( CAN_IER_EWGIE | CAN_IER_EPVIE | CAN_IER_BOFIE | CAN_IER_LECIE | CAN_IER_ERRIE )
#define HW_CAN_TX_MAILBOX_EMPTY_MASK ( CAN_TSR_TME0 | CAN_TSR_TME1 | CAN_TSR_TME2 )

/**-----------------------------------------------------------------------------
 *  Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

typedef struct
{
    bool is_configured;
    bool is_started;
} HWCANLifecycleState_T;

/**-----------------------------------------------------------------------------
 *  Public (global) and Extern Variables
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Private (static) Variables
 *------------------------------------------------------------------------------
 */

static HWCANLifecycleState_T hw_can_lifecycle1 = {
    .is_configured = false,
    .is_started    = false,
};

static HWCANLifecycleState_T hw_can_lifecycle2 = {
    .is_configured = false,
    .is_started    = false,
};

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
static volatile bool               can_sent_flag1          = false;
static volatile bool               can_sent_flag2          = false;
static volatile bool               can_tx_active1          = false;
static volatile bool               can_tx_active2          = false;
static volatile uint32_t           can_rx_dropped_count1   = 0;
static volatile uint32_t           can_rx_dropped_count2   = 0;
static volatile uint32_t           can_tx_pending_mailbox1 = 0;
static volatile uint32_t           can_tx_pending_mailbox2 = 0;
static volatile HW_CAN_Tx_Status_T can_tx_status1          = HW_CAN_TX_STATUS_IDLE;
static volatile HW_CAN_Tx_Status_T can_tx_status2          = HW_CAN_TX_STATUS_IDLE;

/* Buffer for rx channel 1 */
static CAN_Packet_T      can_rx_buffer1[RECEIVE_BUFFER_WIDTH];
static volatile uint16_t can_rx_wp1 = 0;
static volatile uint16_t can_rx_rp1 = 0;
/* Buffer for tx channel 1 */
static CAN_Packet_T      can_tx_buffer1[TRANSMIT_BUFFER_WIDTH];
static volatile uint16_t can_tx_wp1 = 0;
static volatile uint16_t can_tx_rp1 = 0;
/* Buffer for rx channel 2 */
static CAN_Packet_T      can_rx_buffer2[RECEIVE_BUFFER_WIDTH];
static volatile uint16_t can_rx_wp2 = 0;
static volatile uint16_t can_rx_rp2 = 0;
/* Buffer for tx channel 2 */
static CAN_Packet_T      can_tx_buffer2[TRANSMIT_BUFFER_WIDTH];
static volatile uint16_t can_tx_wp2 = 0;
static volatile uint16_t can_tx_rp2 = 0;

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
void HW_CAN_CH1_ERROR_IRQ_HANDLER( void );
void HW_CAN_CH1_TX_IRQ_HANDLER( void );
void HW_CAN_CH1_RX_IRQ_HANDLER( void );

void HW_CAN_CH2_ERROR_IRQ_HANDLER( void );
void HW_CAN_CH2_TX_IRQ_HANDLER( void );
void HW_CAN_CH2_RX_IRQ_HANDLER( void );

static bool HW_CAN_Packet_Is_Valid( const CAN_Packet_T* packet );

static HW_CAN_Result_T HW_CAN_Transmit_To_Mailbox( CAN_HandleTypeDef* hcan, uint8_t* txData,
                                                   uint16_t id, uint8_t size,
                                                   uint32_t* request_complete_flag );

static HW_CAN_Result_T HW_CAN_Tx_Service( CAN_HandleTypeDef* hcan, CAN_Packet_T buffer[],
                                          volatile uint16_t* w_p, volatile uint16_t* r_p,
                                          uint16_t buffer_width, volatile bool* active,
                                          volatile bool*               completed,
                                          volatile uint32_t*           pending_mailbox,
                                          volatile HW_CAN_Tx_Status_T* status );

static HW_CAN_Result_T HW_CAN_Tx_Trigger( CAN_HandleTypeDef* hcan, CAN_Packet_T buffer[],
                                          volatile uint16_t* w_p, volatile uint16_t* r_p,
                                          uint16_t buffer_width, volatile bool* active,
                                          volatile bool*               completed,
                                          volatile uint32_t*           pending_mailbox,
                                          volatile HW_CAN_Tx_Status_T* status );

static void HW_CAN_Tx_IRQ( CAN_HandleTypeDef* hcan, CAN_Packet_T buffer[], volatile uint16_t* w_p,
                           volatile uint16_t* r_p, uint16_t buffer_width, volatile bool* active,
                           volatile bool* completed, volatile uint32_t* pending_mailbox,
                           volatile HW_CAN_Tx_Status_T* status );

static void HW_CAN_Rx_IRQ( CAN_HandleTypeDef* hcan, CAN_Packet_T buffer[], volatile uint16_t* w_p,
                           volatile uint16_t* r_p, volatile uint32_t* dropped_count );

static void HW_CAN_Error_IRQ( CAN_HandleTypeDef* hcan, volatile bool* active,
                              volatile bool* completed, volatile uint32_t* pending_mailbox,
                              volatile HW_CAN_Tx_Status_T* status );

static void HW_CAN_Reset_Channel( CAN_TypeDef* can, IRQn_Type tx_irq, IRQn_Type rx_irq,
                                  IRQn_Type error_irq, volatile uint16_t* tx_wp,
                                  volatile uint16_t* tx_rp, volatile uint16_t* rx_wp,
                                  volatile uint16_t* rx_rp, volatile bool* active,
                                  volatile bool* completed, volatile uint32_t* dropped_count,
                                  volatile uint32_t*           pending_mailbox,
                                  volatile HW_CAN_Tx_Status_T* status );

static HW_CAN_Result_T HW_CAN_Recover( CAN_HandleTypeDef* hcan, IRQn_Type tx_irq, IRQn_Type rx_irq,
                                       IRQn_Type error_irq, volatile uint16_t* tx_wp,
                                       volatile uint16_t* tx_rp, volatile bool* active,
                                       volatile bool* completed, volatile uint32_t* pending_mailbox,
                                       volatile HW_CAN_Tx_Status_T* status,
                                       HWCANLifecycleState_T*       lifecycle );

static void HW_CAN_Tx_Buffer_Cancel( IRQn_Type tx_irq, volatile uint16_t* w_p,
                                     volatile uint16_t* r_p );

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
uint8_t HW_CAN_Buffer_Write( CAN_Packet_T buffer[], volatile uint16_t* w_p, volatile uint16_t* r_p,
                             uint16_t buffer_width, CAN_Packet_T source[], uint16_t length )
{
    uint16_t temp_w_p = *w_p;
    uint16_t temp_r_p = *r_p;

    uint16_t free = ( temp_r_p - temp_w_p - 1 + buffer_width ) % buffer_width;

    /* Buffer full? */
    if ( length > free )
    {
        return 1;
    }

    for ( uint16_t i = 0; i < length; i++ )
    {
        buffer[( temp_w_p + i ) % buffer_width] = source[i];
    }

    *w_p = ( temp_w_p + length ) % buffer_width;

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
uint16_t HW_CAN_Buffer_Read( CAN_Packet_T buffer[], volatile uint16_t* w_p, volatile uint16_t* r_p,
                             uint16_t buffer_width, CAN_Packet_T dest[], uint16_t capacity )
{
    if ( capacity == 0U || dest == NULL )
    {
        return 0;
    }

    uint16_t temp_r_p = *r_p;
    uint16_t temp_w_p = *w_p;

    /* Nothing to read */
    if ( temp_r_p == temp_w_p )
    {
        return 0;
    }

    uint16_t count = ( temp_w_p - temp_r_p + buffer_width ) % buffer_width;
    if ( count > capacity )
    {
        count = capacity;
    }

    for ( uint16_t i = 0; i < count; i++ )
    {
        dest[i] = buffer[( temp_r_p + i ) % buffer_width];
    }

    return count;
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
uint16_t HW_CAN_Buffer_Pop( CAN_Packet_T buffer[], volatile uint16_t* w_p, volatile uint16_t* r_p,
                            uint16_t buffer_width, CAN_Packet_T* dest )
{
    if ( dest == NULL || *w_p == *r_p )
    {
        return 1;
    }

    *dest = buffer[*r_p];

    *r_p = ( *r_p + 1 ) % buffer_width;

    return 0;
}

/**
 * @brief Moves the pointer x times
 *
 * @param pointer       the address of the read pointer
 * @param update        the number of times we want to move the pointer
 * @param buffer_width  The width of the buffer CAN_PACKET_SIZE (8)
 *
 */
void HW_CAN_Buffer_consume( volatile uint16_t* pointer, uint16_t update, uint16_t buffer_width )
{
    *pointer = ( *pointer + update ) % buffer_width;
}

/**
 * @brief transmits the txData (8 bytes) over the hcan CAN channel
 *
 * @param hcan the pointer to the handle for the can peripheral
 * @param txData pointer to 8 bytes of data

    Typical CAN TIR is broken up into sections:

    31                          21 20-19 18-17 16.............0

    +--------------------------------+--+--+-------------------+
    | Standard ID (11 bits)         |RTR|IDE| Reserved        |
    +--------------------------------+--+--+-------------------+

    Typical CAN TDTR is broken up into sections:

    31                     16 15........8 7......4 3......0

    +-----------------------+-----------+--------+---------+
    | Timestamp             | Reserved  | TGT    | DLC     |
    +-----------------------+-----------+--------+---------+
 *
 * Writes the bxCAN mailbox registers directly.
 */
static HW_CAN_Result_T HW_CAN_Transmit_To_Mailbox( CAN_HandleTypeDef* hcan, uint8_t* txData,
                                                   uint16_t id, uint8_t size,
                                                   uint32_t* request_complete_flag )
{
    if ( id > CAN_STANDARD_ID_MAX || size > CAN_PACKET_SIZE || ( size > 0U && txData == NULL ) )
    {
        // The address is larger than 11 bits or the size is larger than 8 bytes
        return HW_CAN_RESULT_ERROR;
    }

    CAN_TypeDef* can     = hcan->Instance;
    uint8_t      mailbox = 0;

    // Check mailbox available
    if ( can->TSR & CAN_TSR_TME0 )
        mailbox = 0;
    else if ( can->TSR & CAN_TSR_TME1 )
        mailbox = 1;
    else if ( can->TSR & CAN_TSR_TME2 )
        mailbox = 2;
    else
        return HW_CAN_RESULT_BUSY;

    if ( request_complete_flag != NULL )
    {
        static const uint32_t request_complete_flags[3] = {
            CAN_TSR_RQCP0,
            CAN_TSR_RQCP1,
            CAN_TSR_RQCP2,
        };
        *request_complete_flag = request_complete_flags[mailbox];
    }

    // Standard ID is 11 bits and has to be shifted to the top 11 bits of the register
    can->sTxMailBox[mailbox].TIR = ( ( uint32_t )id << 21U );

    // DLC = 8, (sending 8 bytes)
    can->sTxMailBox[mailbox].TDTR = size;

    uint32_t low_data  = 0;
    uint32_t high_data = 0;

    for ( uint8_t i = 0; i < size && i < 4U; i++ )
    {
        low_data |= ( uint32_t )txData[i] << ( i * 8U );
    }

    for ( uint8_t i = 4U; i < size; i++ )
    {
        high_data |= ( uint32_t )txData[i] << ( ( i - 4U ) * 8U );
    }

    can->sTxMailBox[mailbox].TDLR = low_data;
    can->sTxMailBox[mailbox].TDHR = high_data;

    // Request transmission
    can->sTxMailBox[mailbox].TIR |= CAN_TI0R_TXRQ;
#ifdef TEST_BUILD
    static const uint32_t mailbox_empty_flags[3] = {
        CAN_TSR_TME0,
        CAN_TSR_TME1,
        CAN_TSR_TME2,
    };
    can->TSR &= ~mailbox_empty_flags[mailbox];
#endif
    return HW_CAN_RESULT_OK;
}

/** Clear one bxCAN TX request-complete group without touching another mailbox. */
static inline void HW_CAN_Clear_Tx_Request_Complete( CAN_TypeDef* can,
                                                     uint32_t     request_complete_flag,
                                                     uint32_t     mailbox_status_flags )
{
#ifdef TEST_BUILD
    can->TSR &= ~( request_complete_flag | mailbox_status_flags );
    if ( request_complete_flag == CAN_TSR_RQCP0 )
    {
        CLEAR_BIT( can->sTxMailBox[0].TIR, CAN_TI0R_TXRQ );
    }
    else if ( request_complete_flag == CAN_TSR_RQCP1 )
    {
        CLEAR_BIT( can->sTxMailBox[1].TIR, CAN_TI0R_TXRQ );
    }
    else if ( request_complete_flag == CAN_TSR_RQCP2 )
    {
        CLEAR_BIT( can->sTxMailBox[2].TIR, CAN_TI0R_TXRQ );
    }
#else
    can->TSR = request_complete_flag;
#endif
}

/** Release one bxCAN FIFO0 output entry using its write-one command bit. */
static inline void HW_CAN_Release_Rx_FIFO0( CAN_TypeDef* can )
{
#ifdef TEST_BUILD
    uint32_t pending = can->RF0R & CAN_RF0R_FMP0;
    if ( pending > 0U )
    {
        pending--;
    }
    can->RF0R = ( can->RF0R & ~( CAN_RF0R_FMP0 | CAN_RF0R_FULL0 ) ) | pending | CAN_RF0R_RFOM0;
#else
    can->RF0R = CAN_RF0R_RFOM0;
#endif
}

/** Clear selected bxCAN FIFO0 write-one-to-clear status flags. */
static inline void HW_CAN_Clear_Rx_FIFO0_Flags( CAN_TypeDef* can, uint32_t flags )
{
#ifdef TEST_BUILD
    can->RF0R &= ~flags;
#else
    can->RF0R = flags;
#endif
}

/**
 * @brief Receives data from the selected bxCAN FIFO0 mailbox.
 *
 * @param hcan the pointer to the handle for the can peripheral
 * @param rxPacket Destination for the received packet.
 *
 * Reads the bxCAN FIFO registers directly.
 */
int HW_CAN_Receive( CAN_HandleTypeDef* hcan, CAN_Packet_T* rxPacket )
{
    if ( rxPacket == NULL )
    {
        return 1;
    }

    CAN_TypeDef* can = hcan->Instance;

    /* Check FIFO0 has pending message */
    if ( ( can->RF0R & CAN_RF0R_FMP0 ) == 0 )
    {
        return 1;
    }

    /*
     * Standard CAN ID is stored in bits 31:21 of RIR.
     *
     * The current driver uses standard 11-bit CAN identifiers.
     */
    rxPacket->id  = ( uint16_t )( ( can->sFIFOMailBox[0].RIR >> 21 ) & CAN_STANDARD_ID_MAX );
    rxPacket->dlc = ( uint8_t )( can->sFIFOMailBox[0].RDTR & CAN_RDT0R_DLC );

    if ( rxPacket->dlc > CAN_PACKET_SIZE )
    {
        HW_CAN_Release_Rx_FIFO0( can );
        return 1;
    }

    uint32_t low  = can->sFIFOMailBox[0].RDLR;
    uint32_t high = can->sFIFOMailBox[0].RDHR;

    memset( rxPacket->data, 0, sizeof( rxPacket->data ) );
    for ( uint8_t i = 0; i < rxPacket->dlc; i++ )
    {
        uint32_t payload  = i < 4U ? low : high;
        uint8_t  offset   = i < 4U ? i : ( uint8_t )( i - 4U );
        rxPacket->data[i] = ( uint8_t )( payload >> ( offset * 8U ) );
    }

    /* Release FIFO */
    HW_CAN_Release_Rx_FIFO0( can );

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
uint16_t HW_CAN_Tx_Buffer_Pop1( CAN_Packet_T* dest )
{
    return HW_CAN_Buffer_Pop( can_tx_buffer1, &can_tx_wp1, &can_tx_rp1, TRANSMIT_BUFFER_WIDTH,
                              dest );
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
uint16_t HW_CAN_Tx_Buffer_Pop2( CAN_Packet_T* dest )
{
    return HW_CAN_Buffer_Pop( can_tx_buffer2, &can_tx_wp2, &can_tx_rp2, TRANSMIT_BUFFER_WIDTH,
                              dest );
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
    Can filtering works as follows:
    The ID of each incoming frame is compared in hardware to the filter banks via:
    (received_ID & mask) == (filter_id & mask)

    Incoming CAN Frame
            │
            ▼
    Check Bank 0
            │
            ├── Match?
            │
            ▼
    Check Bank 1
            │
            ├── Match?
            │
            ▼
    Check Bank 2
            │
            ...
            |
    Check Bank 27
            │
            ├── Match?
            |
        discard
 *
 */
HAL_StatusTypeDef HW_CAN_Apply_Filter_HAL( CAN_FilterTypeDef* filter, CAN_HandleTypeDef* hcan,
                                           uint16_t filter_bank, uint16_t filter_id,
                                           uint16_t filter_mask )
{
    // Either CAN_FILTERMODE_IDMASK, or CAN_FILTERMODE_IDLIST
    // CAN_FILTERMODE_IDMASK accepts a range of ID's based on the filter ID and the mask
    // CAN_FILTERMODE_IDLIST accepts an id if it is in the ID list
    ( *filter ).FilterMode = CAN_FILTERMODE_IDMASK;
    // Use one 32-bit filter entry per filter bank.
    ( *filter ).FilterScale = CAN_FILTERSCALE_32BIT;

    if ( filter_id > CAN_STANDARD_ID_MAX || filter_mask > CAN_STANDARD_ID_MAX )
    {
        return HAL_ERROR;
    }

    // Standard CAN ID's are 11 bits long and only use the High bits 15-5
    ( *filter ).FilterIdHigh     = filter_id << 5;    // the upper 16 bits of the filter ID value
    ( *filter ).FilterIdLow      = 0U;                // standard data frame: IDE = 0, RTR = 0
    ( *filter ).FilterMaskIdHigh = filter_mask << 5;  // the upper 16 bits of the filter mask value
    ( *filter ).FilterMaskIdLow  = CAN_RI0R_IDE | CAN_RI0R_RTR;

    ( *filter ).SlaveStartFilterBank =
        14;  // divides the two filter banks (CAN1 & CAN2) 0----------------13 |14----------------27

    if ( hcan->Instance == CAN1 )
    {
        if ( filter_bank > 13 )
        {
            return HAL_ERROR;
        }
    }
    else if ( hcan->Instance == CAN2 )
    {
        if ( filter_bank < 14 || filter_bank > 27 )
        {
            return HAL_ERROR;
        }
    }
    else
    {
        return HAL_ERROR;
    }

    ( *filter ).FilterBank = filter_bank;

    ( *filter ).FilterFIFOAssignment = CAN_FILTER_FIFO0;  // filter FIFO assignment
    ( *filter ).FilterActivation     = ENABLE;            // enables the filter

    return HAL_CAN_ConfigFilter( hcan, filter );
}

/**
 * @brief Configures the CAN peripherals
 *
 * @param hcan the pointer to the handle for the can peripheral
 * @param bitrate the desired bitrate in bits per second, eg 1Mbps = 1000000
 *
 * @return HW_CAN_RESULT_OK on success, HW_CAN_RESULT_TIMING_ERROR when timing
 *         cannot be applied, or HW_CAN_RESULT_FILTER_ERROR when filter
 *         configuration fails.
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
static HW_CAN_Result_T HW_CAN_Configure( CAN_HandleTypeDef* hcan, uint32_t bitrate,
                                         uint16_t filter_bank, uint16_t filter_id,
                                         uint16_t filter_mask )
{
    CAN_FilterTypeDef filter    = { 0 };
    CanProperties_T   can_props = HW_CAN_Compute_Properties( bitrate, TOTAL_TQ, MBPS_SAMPLE_POINT );

    if ( HW_CAN_Apply_Timing_HAL( hcan, can_props ) != HAL_OK )
    {
        return HW_CAN_RESULT_TIMING_ERROR;
    }

    if ( HW_CAN_Apply_Filter_HAL( &filter, hcan, filter_bank, filter_id, filter_mask ) != HAL_OK )
    {
        return HW_CAN_RESULT_FILTER_ERROR;
    }

    return HW_CAN_RESULT_OK;
}

static HW_CAN_Result_T HW_CAN_Start( CAN_HandleTypeDef* hcan, IRQn_Type tx_irq, IRQn_Type rx_irq,
                                     IRQn_Type error_irq, HWCANLifecycleState_T* lifecycle )
{
    if ( hcan == NULL || lifecycle == NULL )
    {
        return HW_CAN_RESULT_ERROR;
    }

    if ( !lifecycle->is_configured )
    {
        return HW_CAN_RESULT_NOT_CONFIGURED;
    }

    if ( lifecycle->is_started )
    {
        return HW_CAN_RESULT_BUSY;
    }

    if ( HAL_CAN_Start( hcan ) != HAL_OK )
    {
        return HW_CAN_RESULT_ERROR;
    }

    /*
     * TX mailbox-empty interrupts remain disabled until a buffered
     * transmission is triggered.
     */
    CLEAR_BIT( hcan->Instance->IER, CAN_IER_TMEIE );

    NVIC_EnableIRQ( tx_irq );
    NVIC_EnableIRQ( rx_irq );
    NVIC_EnableIRQ( error_irq );

    SET_BIT( hcan->Instance->IER, HW_CAN_RX_INTERRUPT_MASK | HW_CAN_ERROR_INTERRUPT_MASK );

    lifecycle->is_started = true;

    return HW_CAN_RESULT_OK;
}

static HW_CAN_Result_T HW_CAN_Stop( CAN_HandleTypeDef* hcan, IRQn_Type tx_irq, IRQn_Type rx_irq,
                                    IRQn_Type error_irq, volatile bool* tx_active,
                                    HWCANLifecycleState_T* lifecycle )
{
    if ( hcan == NULL || tx_active == NULL || lifecycle == NULL )
    {
        return HW_CAN_RESULT_ERROR;
    }

    if ( !lifecycle->is_configured )
    {
        return HW_CAN_RESULT_NOT_CONFIGURED;
    }

    if ( !lifecycle->is_started )
    {
        return HW_CAN_RESULT_NOT_STARTED;
    }

    /*
     * Mask interrupts before inspecting transmission state so an ISR cannot
     * advance the buffered transmission while Stop is deciding whether the
     * peripheral is idle.
     */
    uint32_t enabled_interrupts =
        hcan->Instance->IER
        & ( CAN_IER_TMEIE | HW_CAN_RX_INTERRUPT_MASK | HW_CAN_ERROR_INTERRUPT_MASK );

    NVIC_DisableIRQ( tx_irq );
    NVIC_DisableIRQ( rx_irq );
    NVIC_DisableIRQ( error_irq );

    CLEAR_BIT( hcan->Instance->IER,
               CAN_IER_TMEIE | HW_CAN_RX_INTERRUPT_MASK | HW_CAN_ERROR_INTERRUPT_MASK );

    bool mailbox_active = ( ( hcan->Instance->sTxMailBox[0].TIR & CAN_TI0R_TXRQ ) != 0U )
                          || ( ( hcan->Instance->sTxMailBox[1].TIR & CAN_TI0R_TXRQ ) != 0U )
                          || ( ( hcan->Instance->sTxMailBox[2].TIR & CAN_TI0R_TXRQ ) != 0U );

    if ( *tx_active || mailbox_active )
    {
        SET_BIT( hcan->Instance->IER, enabled_interrupts );
        NVIC_EnableIRQ( error_irq );
        NVIC_EnableIRQ( rx_irq );
        NVIC_EnableIRQ( tx_irq );

        return HW_CAN_RESULT_BUSY;
    }

    if ( HAL_CAN_Stop( hcan ) != HAL_OK )
    {
        SET_BIT( hcan->Instance->IER, enabled_interrupts );
        NVIC_EnableIRQ( error_irq );
        NVIC_EnableIRQ( rx_irq );
        NVIC_EnableIRQ( tx_irq );

        return HW_CAN_RESULT_ERROR;
    }

    lifecycle->is_started = false;

    return HW_CAN_RESULT_OK;
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
    const CanProperties_T invalid_properties = { 0, 0, 0, 0 };

    if ( bitrate == 0U || bitrate > 1000000U || total_TQ < 3U || total_TQ > 25U )
    {
        return invalid_properties;
    }
    if ( sample_point_1t1000 < 700U || sample_point_1t1000 > 1000U )
    {
        return invalid_properties;
    }

    uint64_t sample_product = ( uint64_t )sample_point_1t1000 * ( uint64_t )total_TQ;
    uint64_t sample_tq      = sample_product / 1000U;
    if ( sample_tq <= 1U || sample_tq >= total_TQ )
    {
        return invalid_properties;
    }

    uint32_t bs1 = ( uint32_t )sample_tq - 1U;
    uint32_t bs2 = total_TQ - bs1 - 1U;
    if ( bs1 < 1U || bs1 > 16U || bs2 < 1U || bs2 > 8U )
    {
        return invalid_properties;
    }

    uint64_t bitrate_denominator = ( uint64_t )bitrate * ( uint64_t )total_TQ;
    if ( bitrate_denominator == 0U || ( uint64_t )CAN_TIMER_HZ % bitrate_denominator != 0U )
    {
        return invalid_properties;
    }

    uint64_t prescaler = ( uint64_t )CAN_TIMER_HZ / bitrate_denominator;
    if ( prescaler < 1U || prescaler > 1024U )
    {
        return invalid_properties;
    }

    return ( CanProperties_T ){ bs1, bs2, ( uint32_t )prescaler, CAN_TIMER_HZ };
}

/**
 * @brief Configures the peripherals of CAN channel 1
 *
 * @param bitrate the desired bitrate in bits per second, eg 1Mbps = 1000000
 *
 * @return HW_CAN_RESULT_OK on success, HW_CAN_RESULT_BUSY if the channel is
 *         already started, or the configuration-specific error returned by
 *         the shared configuration helper.
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
HW_CAN_Result_T HW_CAN_Configure1( uint32_t bitrate, uint16_t filter_bank, uint16_t filter_id,
                                   uint16_t filter_mask )
{
    if ( hw_can_lifecycle1.is_started )
    {
        return HW_CAN_RESULT_BUSY;
    }

    hw_can_lifecycle1.is_configured = false;
    hw_can_lifecycle1.is_started    = false;

    __HAL_RCC_CAN1_CLK_ENABLE();

    HW_CAN_Result_T result =
        HW_CAN_Configure( &hcan1, bitrate, filter_bank, filter_id, filter_mask );

    CLEAR_BIT( CAN1->IER, CAN_IER_TMEIE | HW_CAN_RX_INTERRUPT_MASK | HW_CAN_ERROR_INTERRUPT_MASK );
    NVIC_DisableIRQ( CAN1_TX_IRQn );
    NVIC_DisableIRQ( CAN1_RX0_IRQn );
    NVIC_DisableIRQ( CAN1_SCE_IRQn );

    HW_CAN_Reset1();

    if ( result == HW_CAN_RESULT_OK )
    {
        hw_can_lifecycle1.is_configured = true;
    }

    return result;
}

/**
 * @brief Configures the peripherals of CAN channel 2
 *
 * @param bitrate the desired bitrate in bits per second, eg 1Mbps = 1000000
 *
 * @return HW_CAN_RESULT_OK on success, HW_CAN_RESULT_BUSY if the channel is
 *         already started, or the configuration-specific error returned by
 *         the shared configuration helper.
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
HW_CAN_Result_T HW_CAN_Configure2( uint32_t bitrate, uint16_t filter_bank, uint16_t filter_id,
                                   uint16_t filter_mask )
{
    if ( hw_can_lifecycle2.is_started )
    {
        return HW_CAN_RESULT_BUSY;
    }

    hw_can_lifecycle2.is_configured = false;
    hw_can_lifecycle2.is_started    = false;

    __HAL_RCC_CAN2_CLK_ENABLE();

    HW_CAN_Result_T result =
        HW_CAN_Configure( &hcan2, bitrate, filter_bank, filter_id, filter_mask );

    CLEAR_BIT( CAN2->IER, CAN_IER_TMEIE | HW_CAN_RX_INTERRUPT_MASK | HW_CAN_ERROR_INTERRUPT_MASK );
    NVIC_DisableIRQ( CAN2_TX_IRQn );
    NVIC_DisableIRQ( CAN2_RX0_IRQn );
    NVIC_DisableIRQ( CAN2_SCE_IRQn );

    HW_CAN_Reset2();

    if ( result == HW_CAN_RESULT_OK )
    {
        hw_can_lifecycle2.is_configured = true;
    }

    return result;
}

/** Reset channel 1 software state while its CAN interrupts are masked. */
void HW_CAN_Reset1( void )
{
    HW_CAN_Reset_Channel( CAN1, CAN1_TX_IRQn, CAN1_RX0_IRQn, CAN1_SCE_IRQn, &can_tx_wp1,
                          &can_tx_rp1, &can_rx_wp1, &can_rx_rp1, &can_tx_active1, &can_sent_flag1,
                          &can_rx_dropped_count1, &can_tx_pending_mailbox1, &can_tx_status1 );
}

/** Reset channel 2 software state while its CAN interrupts are masked. */
void HW_CAN_Reset2( void )
{
    HW_CAN_Reset_Channel( CAN2, CAN2_TX_IRQn, CAN2_RX0_IRQn, CAN2_SCE_IRQn, &can_tx_wp2,
                          &can_tx_rp2, &can_rx_wp2, &can_rx_rp2, &can_tx_active2, &can_sent_flag2,
                          &can_rx_dropped_count2, &can_tx_pending_mailbox2, &can_tx_status2 );
}

HW_CAN_Result_T HW_CAN_Recover1( void )
{
    return HW_CAN_Recover( &hcan1, CAN1_TX_IRQn, CAN1_RX0_IRQn, CAN1_SCE_IRQn, &can_tx_wp1,
                           &can_tx_rp1, &can_tx_active1, &can_sent_flag1, &can_tx_pending_mailbox1,
                           &can_tx_status1, &hw_can_lifecycle1 );
}

HW_CAN_Result_T HW_CAN_Recover2( void )
{
    return HW_CAN_Recover( &hcan2, CAN2_TX_IRQn, CAN2_RX0_IRQn, CAN2_SCE_IRQn, &can_tx_wp2,
                           &can_tx_rp2, &can_tx_active2, &can_sent_flag2, &can_tx_pending_mailbox2,
                           &can_tx_status2, &hw_can_lifecycle2 );
}

HW_CAN_Result_T HW_CAN_Start1( void )
{
    return HW_CAN_Start( &hcan1, CAN1_TX_IRQn, CAN1_RX0_IRQn, CAN1_SCE_IRQn, &hw_can_lifecycle1 );
}

HW_CAN_Result_T HW_CAN_Start2( void )
{
    return HW_CAN_Start( &hcan2, CAN2_TX_IRQn, CAN2_RX0_IRQn, CAN2_SCE_IRQn, &hw_can_lifecycle2 );
}

HW_CAN_Result_T HW_CAN_Stop1( void )
{
    return HW_CAN_Stop( &hcan1, CAN1_TX_IRQn, CAN1_RX0_IRQn, CAN1_SCE_IRQn, &can_tx_active1,
                        &hw_can_lifecycle1 );
}

HW_CAN_Result_T HW_CAN_Stop2( void )
{
    return HW_CAN_Stop( &hcan2, CAN2_TX_IRQn, CAN2_RX0_IRQn, CAN2_SCE_IRQn, &can_tx_active2,
                        &hw_can_lifecycle2 );
}

bool HW_CAN_Is_Configured1( void )
{
    return hw_can_lifecycle1.is_configured;
}

bool HW_CAN_Is_Configured2( void )
{
    return hw_can_lifecycle2.is_configured;
}

bool HW_CAN_Is_Started1( void )
{
    return hw_can_lifecycle1.is_started;
}

bool HW_CAN_Is_Started2( void )
{
    return hw_can_lifecycle2.is_started;
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
bool HW_CAN_Channel1_Sent( void )
{
    return can_tx_status1 == HW_CAN_TX_STATUS_COMPLETE;
}

/**
 * @brief If True then all channel 2 messages have been sent, since the last trigger
 *
 *
 * The sent flag is set flase after trigger is called when CAN has emptied the buffer
 * and set true when the last message is sent and the buffer is ready for a new message
 */
bool HW_CAN_Channel2_Sent( void )
{
    return can_tx_status2 == HW_CAN_TX_STATUS_COMPLETE;
}

HW_CAN_Tx_Status_T HW_CAN_Tx_Status1( void )
{
    return can_tx_status1;
}

HW_CAN_Tx_Status_T HW_CAN_Tx_Status2( void )
{
    return can_tx_status2;
}

/** Return channel 1's sticky software RX dropped-frame count. */
uint32_t HW_CAN_Rx_Dropped_Count1( void )
{
    return can_rx_dropped_count1;
}

/** Return channel 2's sticky software RX dropped-frame count. */
uint32_t HW_CAN_Rx_Dropped_Count2( void )
{
    return can_rx_dropped_count2;
}

/**
 * @brief transmits the txData (8 bytes) over CAN channel 1
 *
 * @param txData pointer to 8 bytes of data
 *
 * Writes the bxCAN mailbox registers directly.
 */
HW_CAN_Result_T HW_CAN_Transmit1( uint8_t* txData, uint16_t id, uint8_t dlc )
{
    if ( dlc > 0U && txData == NULL )
    {
        return HW_CAN_RESULT_ERROR;
    }
    if ( can_tx_active1 )
    {
        return HW_CAN_RESULT_BUSY;
    }
    if ( can_tx_status1 == HW_CAN_TX_STATUS_ERROR )
    {
        return HW_CAN_RESULT_ERROR;
    }
    return HW_CAN_Transmit_To_Mailbox( &hcan1, txData, id, dlc, NULL );
}

/**
 * @brief Receives data from the channel 1 bxCAN FIFO0 mailbox.
 *
 * @param rxPacket Destination for the received packet.
 *
 * Reads the bxCAN FIFO registers directly.
 */
int HW_CAN_Receive1( CAN_Packet_T* rxPacket )
{
    return HW_CAN_Receive( &hcan1, rxPacket );
}

/**
 * @brief transmits the txData (8 bytes) over CAN channel 2
 *
 * @param txData pointer to 8 bytes of data
 *
 * Writes the bxCAN mailbox registers directly.
 */
HW_CAN_Result_T HW_CAN_Transmit2( uint8_t* txData, uint16_t id, uint8_t dlc )
{
    if ( dlc > 0U && txData == NULL )
    {
        return HW_CAN_RESULT_ERROR;
    }
    if ( can_tx_active2 )
    {
        return HW_CAN_RESULT_BUSY;
    }
    if ( can_tx_status2 == HW_CAN_TX_STATUS_ERROR )
    {
        return HW_CAN_RESULT_ERROR;
    }
    return HW_CAN_Transmit_To_Mailbox( &hcan2, txData, id, dlc, NULL );
}

/**
 * @brief Receives data from the channel 2 bxCAN FIFO0 mailbox.
 *
 * @param rxPacket Destination for the received packet.
 *
 * Reads the bxCAN FIFO registers directly.
 */
int HW_CAN_Receive2( CAN_Packet_T* rxPacket )
{
    return HW_CAN_Receive( &hcan2, rxPacket );
}

/**
 * @brief Writes a number of 8 byte packets (source) to the tx buffer
 *
 * @param source an array of arrays, type:
uint8_t can_tx_buffer1[X][CAN_PACKET_SIZE];
 * @param length the number of can packets to be written (seen as X above)
 *
 * @return 0 if the write was successful, 1 otherwise. (partially successful = 1)
 */
HW_CAN_Result_T HW_CAN_Tx_Buffer_Write1( CAN_Packet_T source[], uint16_t length )
{
    if ( can_tx_active1 )
    {
        return HW_CAN_RESULT_BUSY;
    }
    if ( length > 0U && source == NULL )
    {
        return HW_CAN_RESULT_ERROR;
    }
    for ( uint16_t i = 0; i < length; i++ )
    {
        if ( !HW_CAN_Packet_Is_Valid( &source[i] ) )
        {
            return HW_CAN_RESULT_ERROR;
        }
    }

    return HW_CAN_Buffer_Write( can_tx_buffer1, &can_tx_wp1, &can_tx_rp1, TRANSMIT_BUFFER_WIDTH,
                                source, length )
                   == 0
               ? HW_CAN_RESULT_OK
               : HW_CAN_RESULT_ERROR;
}

void HW_CAN_Tx_Buffer_Cancel1( void )
{
    HW_CAN_Tx_Buffer_Cancel( CAN1_TX_IRQn, &can_tx_wp1, &can_tx_rp1 );
}

/**
 * @brief Writes a number of 8 byte packets (source) to the rx buffer
 *
 * @param source an array of arrays, type:
uint8_t can_rx_buffer1[X][CAN_PACKET_SIZE];
 * @param length the number of can packets to be written (seen as X above)
 *
 * @return 0 if the write was successful, 1 otherwise. (partially successful = 1)
 */
uint16_t HW_CAN_Rx_Buffer_Write1( CAN_Packet_T source[], uint16_t length )
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
 * @return 0 if the write was successful, 1 otherwise. (partially successful = 1)
 */
HW_CAN_Result_T HW_CAN_Tx_Buffer_Write2( CAN_Packet_T source[], uint16_t length )
{
    if ( can_tx_active2 )
    {
        return HW_CAN_RESULT_BUSY;
    }
    if ( length > 0U && source == NULL )
    {
        return HW_CAN_RESULT_ERROR;
    }
    for ( uint16_t i = 0; i < length; i++ )
    {
        if ( !HW_CAN_Packet_Is_Valid( &source[i] ) )
        {
            return HW_CAN_RESULT_ERROR;
        }
    }

    return HW_CAN_Buffer_Write( can_tx_buffer2, &can_tx_wp2, &can_tx_rp2, TRANSMIT_BUFFER_WIDTH,
                                source, length )
                   == 0
               ? HW_CAN_RESULT_OK
               : HW_CAN_RESULT_ERROR;
}

void HW_CAN_Tx_Buffer_Cancel2( void )
{
    HW_CAN_Tx_Buffer_Cancel( CAN2_TX_IRQn, &can_tx_wp2, &can_tx_rp2 );
}

/**
 * @brief Writes a number of 8 byte packets (source) to the rx buffer
 *
 * @param source an array of arrays, type:
uint8_t can_rx_buffer1[X][CAN_PACKET_SIZE];
 * @param length the number of can packets to be written (seen as X above)
 *
 * @return 0 if the write was successful, 1 otherwise. (partially successful = 1)
 */
uint16_t HW_CAN_Rx_Buffer_Write2( CAN_Packet_T source[], uint16_t length )
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
uint16_t HW_CAN_Rx_Buffer_Read1( CAN_Packet_T dest[], uint16_t capacity )
{
    uint16_t count = HW_CAN_Buffer_Read( can_rx_buffer1, &can_rx_wp1, &can_rx_rp1,
                                         RECEIVE_BUFFER_WIDTH, dest, capacity );
    HW_CAN_Rx_Buffer_consume1( count );
    return count;
}

/**
 * @brief Moves the channe 1 read pointer x times
 *
 * @param update        the number of times we want to move the pointer
 *
 */
void HW_CAN_Rx_Buffer_consume1( uint16_t update )
{
    HW_CAN_Buffer_consume( &can_rx_rp1, update, RECEIVE_BUFFER_WIDTH );
}

/**
 * @brief Reads from the rx buffer (channel 2) and writes it to dest
 *
 * @param dest the destination where the value will be written
 *
 * @return the number of CAN_PACKET_SIZE's read
 */
uint16_t HW_CAN_Rx_Buffer_Read2( CAN_Packet_T dest[], uint16_t capacity )
{
    uint16_t count = HW_CAN_Buffer_Read( can_rx_buffer2, &can_rx_wp2, &can_rx_rp2,
                                         RECEIVE_BUFFER_WIDTH, dest, capacity );
    HW_CAN_Rx_Buffer_consume2( count );
    return count;
}

/**
 * @brief Moves the channe 2 read pointer x times
 *
 * @param update        the number of times we want to move the pointer
 *
 */
void HW_CAN_Rx_Buffer_consume2( uint16_t update )
{
    HW_CAN_Buffer_consume( &can_rx_rp2, update, RECEIVE_BUFFER_WIDTH );
}

/**
 * @brief Reads from the tx buffer (channel 1) and writes it to dest
 *
 * @param dest the destination where the value will be written
 *
 * @return the number of CAN_PACKET_SIZE's read
 */
uint16_t HW_CAN_Tx_Buffer_Read1( CAN_Packet_T dest[] )
{
    return HW_CAN_Buffer_Read( can_tx_buffer1, &can_tx_wp1, &can_tx_rp1, TRANSMIT_BUFFER_WIDTH,
                               dest, TRANSMIT_BUFFER_WIDTH - 1U );
}

/**
 * @brief Reads from the tx buffer (channel 2) and writes it to dest
 *
 * @param dest the destination where the value will be written
 *
 * @return the number of CAN_PACKET_SIZE's read
 */
uint16_t HW_CAN_Tx_Buffer_Read2( CAN_Packet_T dest[] )
{
    return HW_CAN_Buffer_Read( can_tx_buffer2, &can_tx_wp2, &can_tx_rp2, TRANSMIT_BUFFER_WIDTH,
                               dest, TRANSMIT_BUFFER_WIDTH - 1U );
}

/** Remove one packet from the channel 1 receive buffer. */
uint16_t HW_CAN_Rx_Buffer_Pop1( CAN_Packet_T* dest )
{
    return HW_CAN_Buffer_Pop( can_rx_buffer1, &can_rx_wp1, &can_rx_rp1, RECEIVE_BUFFER_WIDTH,
                              dest );
}

/** Remove one packet from the channel 2 receive buffer. */
uint16_t HW_CAN_Rx_Buffer_Pop2( CAN_Packet_T* dest )
{
    return HW_CAN_Buffer_Pop( can_rx_buffer2, &can_rx_wp2, &can_rx_rp2, RECEIVE_BUFFER_WIDTH,
                              dest );
}

/**
 * @brief Enables tx interrupts on channel 1
 *
 * Used to enable the sending of messages through CAN channel 1
 * Once the write buffer is empty the ISR will disable again
 */
HW_CAN_Result_T HW_CAN_Tx_Trigger1( void )
{
    return HW_CAN_Tx_Trigger( &hcan1, can_tx_buffer1, &can_tx_wp1, &can_tx_rp1,
                              TRANSMIT_BUFFER_WIDTH, &can_tx_active1, &can_sent_flag1,
                              &can_tx_pending_mailbox1, &can_tx_status1 );
}

/**
 * @brief Enables tx interrupts on channel 2
 *
 * Used to enable the sending of messages through CAN channel 2
 * Once the write buffer is empty the ISR will disable again
 */
HW_CAN_Result_T HW_CAN_Tx_Trigger2( void )
{
    return HW_CAN_Tx_Trigger( &hcan2, can_tx_buffer2, &can_tx_wp2, &can_tx_rp2,
                              TRANSMIT_BUFFER_WIDTH, &can_tx_active2, &can_sent_flag2,
                              &can_tx_pending_mailbox2, &can_tx_status2 );
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
    HW_CAN_Tx_IRQ( &hcan1, can_tx_buffer1, &can_tx_wp1, &can_tx_rp1, TRANSMIT_BUFFER_WIDTH,
                   &can_tx_active1, &can_sent_flag1, &can_tx_pending_mailbox1, &can_tx_status1 );
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
    HW_CAN_Rx_IRQ( &hcan1, can_rx_buffer1, &can_rx_wp1, &can_rx_rp1, &can_rx_dropped_count1 );
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
    HW_CAN_Tx_IRQ( &hcan2, can_tx_buffer2, &can_tx_wp2, &can_tx_rp2, TRANSMIT_BUFFER_WIDTH,
                   &can_tx_active2, &can_sent_flag2, &can_tx_pending_mailbox2, &can_tx_status2 );
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
    HW_CAN_Rx_IRQ( &hcan2, can_rx_buffer2, &can_rx_wp2, &can_rx_rp2, &can_rx_dropped_count2 );
}

/** Direct CAN1 status/error interrupt vector. */
void HW_CAN_CH1_ERROR_IRQ_HANDLER( void )
{
    HW_CAN_Error_IRQ( &hcan1, &can_tx_active1, &can_sent_flag1, &can_tx_pending_mailbox1,
                      &can_tx_status1 );
}

/** Direct CAN2 status/error interrupt vector. */
void HW_CAN_CH2_ERROR_IRQ_HANDLER( void )
{
    HW_CAN_Error_IRQ( &hcan2, &can_tx_active2, &can_sent_flag2, &can_tx_pending_mailbox2,
                      &can_tx_status2 );
}

/** Directly service bxCAN transmit completion flags for one channel. */
static void HW_CAN_Tx_IRQ( CAN_HandleTypeDef* hcan, CAN_Packet_T buffer[], volatile uint16_t* w_p,
                           volatile uint16_t* r_p, uint16_t buffer_width, volatile bool* active,
                           volatile bool* completed, volatile uint32_t* pending_mailbox,
                           volatile HW_CAN_Tx_Status_T* status )
{
    static const uint32_t request_complete_flags[3] = {
        CAN_TSR_RQCP0,
        CAN_TSR_RQCP1,
        CAN_TSR_RQCP2,
    };
    static const uint32_t success_flags[3] = {
        CAN_TSR_TXOK0,
        CAN_TSR_TXOK1,
        CAN_TSR_TXOK2,
    };
    static const uint32_t arbitration_lost_flags[3] = {
        CAN_TSR_ALST0,
        CAN_TSR_ALST1,
        CAN_TSR_ALST2,
    };
    static const uint32_t transmit_error_flags[3] = {
        CAN_TSR_TERR0,
        CAN_TSR_TERR1,
        CAN_TSR_TERR2,
    };

    CAN_TypeDef* can                        = hcan->Instance;
    uint32_t     tsr                        = can->TSR;
    bool         batch_completion_seen      = false;
    bool         batch_completion_succeeded = false;
    bool         waiting_for_mailbox        = *active && *pending_mailbox == 0U;

    for ( uint8_t mailbox = 0U; mailbox < 3U; mailbox++ )
    {
        uint32_t request_complete = request_complete_flags[mailbox];
        if ( ( tsr & request_complete ) == 0U )
        {
            continue;
        }

        uint32_t mailbox_status = success_flags[mailbox] | arbitration_lost_flags[mailbox]
                                  | transmit_error_flags[mailbox];
        bool belongs_to_batch = ( *pending_mailbox & request_complete ) != 0U;
        if ( !batch_completion_seen && ( belongs_to_batch || waiting_for_mailbox ) )
        {
            batch_completion_seen = true;
            batch_completion_succeeded =
                ( tsr & success_flags[mailbox] ) != 0U
                && ( tsr & ( arbitration_lost_flags[mailbox] | transmit_error_flags[mailbox] ) )
                       == 0U;
        }

        HW_CAN_Clear_Tx_Request_Complete( can, request_complete, mailbox_status );
    }

    if ( !*active )
    {
        CLEAR_BIT( can->IER, CAN_IER_TMEIE );
        return;
    }
    if ( !batch_completion_seen )
    {
        return;
    }

    *pending_mailbox = 0U;
    if ( !batch_completion_succeeded )
    {
        CLEAR_BIT( can->IER, CAN_IER_TMEIE );
        *active    = false;
        *completed = false;
        *status    = HW_CAN_TX_STATUS_ERROR;
        return;
    }

    ( void )HW_CAN_Tx_Service( hcan, buffer, w_p, r_p, buffer_width, active, completed,
                               pending_mailbox, status );
}

/** Directly drain at most the hardware FIFO0 depth into one software RX queue. */
static void HW_CAN_Rx_IRQ( CAN_HandleTypeDef* hcan, CAN_Packet_T buffer[], volatile uint16_t* w_p,
                           volatile uint16_t* r_p, volatile uint32_t* dropped_count )
{
    CAN_TypeDef* can     = hcan->Instance;
    bool         overrun = ( can->RF0R & CAN_RF0R_FOVR0 ) != 0U;

    for ( uint8_t read_count = 0U; read_count < CAN_RX_FIFO_DEPTH; read_count++ )
    {
        if ( ( can->RF0R & CAN_RF0R_FMP0 ) == 0U )
        {
            break;
        }

        CAN_Packet_T packet;
        if ( HW_CAN_Receive( hcan, &packet ) != 0 )
        {
            break;
        }
        if ( HW_CAN_Buffer_Write( buffer, w_p, r_p, RECEIVE_BUFFER_WIDTH, &packet, 1U ) != 0U
             && *dropped_count != UINT32_MAX )
        {
            ( *dropped_count )++;
        }
    }

    if ( overrun && *dropped_count != UINT32_MAX )
    {
        ( *dropped_count )++;
    }

    uint32_t flags = can->RF0R & ( CAN_RF0R_FULL0 | CAN_RF0R_FOVR0 );
    if ( flags != 0U )
    {
        HW_CAN_Clear_Rx_FIFO0_Flags( can, flags );
    }
}

/** Acknowledge the bxCAN error interrupt's write-one-to-clear MSR flag. */
static void HW_CAN_Clear_Error_Interrupt( CAN_TypeDef* can )
{
#ifdef TEST_BUILD
    can->MSR &= ~CAN_MSR_ERRI;
#else
    can->MSR = CAN_MSR_ERRI;
#endif
}

/** Directly service a bxCAN status/error interrupt for one channel. */
static void HW_CAN_Error_IRQ( CAN_HandleTypeDef* hcan, volatile bool* active,
                              volatile bool* completed, volatile uint32_t* pending_mailbox,
                              volatile HW_CAN_Tx_Status_T* status )
{
    CAN_TypeDef* can = hcan->Instance;
    uint32_t     msr = can->MSR;
    uint32_t     esr = can->ESR;

    if ( ( msr & CAN_MSR_ERRI ) != 0U )
    {
        uint32_t last_error = esr & CAN_ESR_LEC;
        bool     bus_off    = ( esr & CAN_ESR_BOFF ) != 0U;
        if ( bus_off )
        {
            CLEAR_BIT( can->IER, CAN_IER_TMEIE );
            *active          = false;
            *completed       = false;
            *pending_mailbox = 0U;
            *status          = HW_CAN_TX_STATUS_ERROR;
        }

        if ( last_error != 0U )
        {
            CLEAR_BIT( can->ESR, CAN_ESR_LEC );
        }
    }

    HW_CAN_Clear_Error_Interrupt( can );
}

/** Reset one channel's software state while preserving its NVIC enable state. */
static void HW_CAN_Reset_Channel( CAN_TypeDef* can, IRQn_Type tx_irq, IRQn_Type rx_irq,
                                  IRQn_Type error_irq, volatile uint16_t* tx_wp,
                                  volatile uint16_t* tx_rp, volatile uint16_t* rx_wp,
                                  volatile uint16_t* rx_rp, volatile bool* active,
                                  volatile bool* completed, volatile uint32_t* dropped_count,
                                  volatile uint32_t*           pending_mailbox,
                                  volatile HW_CAN_Tx_Status_T* status )
{
    uint32_t tx_irq_was_enabled    = NVIC_GetEnableIRQ( tx_irq );
    uint32_t rx_irq_was_enabled    = NVIC_GetEnableIRQ( rx_irq );
    uint32_t error_irq_was_enabled = NVIC_GetEnableIRQ( error_irq );

    NVIC_DisableIRQ( tx_irq );
    NVIC_DisableIRQ( rx_irq );
    NVIC_DisableIRQ( error_irq );

    CLEAR_BIT( can->IER, CAN_IER_TMEIE );
    *tx_wp           = 0;
    *tx_rp           = 0;
    *rx_wp           = 0;
    *rx_rp           = 0;
    *active          = false;
    *completed       = false;
    *dropped_count   = 0;
    *pending_mailbox = 0U;
    *status          = HW_CAN_TX_STATUS_IDLE;

    if ( error_irq_was_enabled != 0U )
    {
        NVIC_EnableIRQ( error_irq );
    }
    if ( rx_irq_was_enabled != 0U )
    {
        NVIC_EnableIRQ( rx_irq );
    }
    if ( tx_irq_was_enabled != 0U )
    {
        NVIC_EnableIRQ( tx_irq );
    }
}

/**
 * @brief Services one step of an active buffered transmission.
 *
 * The next packet is consumed only after it has been loaded into a hardware
 * mailbox. A busy mailbox leaves the packet queued for a later completion
 * interrupt. Completion is set only when an interrupt services an empty queue
 * after the final packet was loaded.
 */
static HW_CAN_Result_T HW_CAN_Tx_Service( CAN_HandleTypeDef* hcan, CAN_Packet_T buffer[],
                                          volatile uint16_t* w_p, volatile uint16_t* r_p,
                                          uint16_t buffer_width, volatile bool* active,
                                          volatile bool*               completed,
                                          volatile uint32_t*           pending_mailbox,
                                          volatile HW_CAN_Tx_Status_T* status )
{
    if ( !*active )
    {
        CLEAR_BIT( hcan->Instance->IER, CAN_IER_TMEIE );
        return HW_CAN_RESULT_ERROR;
    }

    if ( *w_p == *r_p )
    {
        CLEAR_BIT( hcan->Instance->IER, CAN_IER_TMEIE );
        *active    = false;
        *completed = true;
        *status    = HW_CAN_TX_STATUS_COMPLETE;
        return HW_CAN_RESULT_OK;
    }

    CAN_Packet_T    packet       = buffer[*r_p];
    uint32_t        mailbox_flag = 0U;
    HW_CAN_Result_T result =
        HW_CAN_Transmit_To_Mailbox( hcan, packet.data, packet.id, packet.dlc, &mailbox_flag );

    if ( result == HW_CAN_RESULT_OK )
    {
        *pending_mailbox = mailbox_flag;
        HW_CAN_Buffer_consume( r_p, 1, buffer_width );
    }
    else if ( result == HW_CAN_RESULT_ERROR )
    {
        CLEAR_BIT( hcan->Instance->IER, CAN_IER_TMEIE );
        *active    = false;
        *completed = false;
        *status    = HW_CAN_TX_STATUS_ERROR;
    }

    return result;
}

/**
 * @brief Starts a buffered CAN transmission if the channel is idle and non-empty.
 */
static HW_CAN_Result_T HW_CAN_Tx_Trigger( CAN_HandleTypeDef* hcan, CAN_Packet_T buffer[],
                                          volatile uint16_t* w_p, volatile uint16_t* r_p,
                                          uint16_t buffer_width, volatile bool* active,
                                          volatile bool*               completed,
                                          volatile uint32_t*           pending_mailbox,
                                          volatile HW_CAN_Tx_Status_T* status )
{
    if ( *active )
    {
        return HW_CAN_RESULT_BUSY;
    }
    if ( *w_p == *r_p )
    {
        return HW_CAN_RESULT_EMPTY;
    }
    if ( *status == HW_CAN_TX_STATUS_ERROR )
    {
        return HW_CAN_RESULT_ERROR;
    }
    if ( ( hcan->Instance->sTxMailBox[0].TIR & CAN_TI0R_TXRQ ) != 0U
         || ( hcan->Instance->sTxMailBox[1].TIR & CAN_TI0R_TXRQ ) != 0U
         || ( hcan->Instance->sTxMailBox[2].TIR & CAN_TI0R_TXRQ ) != 0U )
    {
        return HW_CAN_RESULT_BUSY;
    }

    static const uint32_t request_complete_flags[3] = {
        CAN_TSR_RQCP0,
        CAN_TSR_RQCP1,
        CAN_TSR_RQCP2,
    };
    static const uint32_t mailbox_status_flags[3] = {
        CAN_TSR_TXOK0 | CAN_TSR_ALST0 | CAN_TSR_TERR0,
        CAN_TSR_TXOK1 | CAN_TSR_ALST1 | CAN_TSR_TERR1,
        CAN_TSR_TXOK2 | CAN_TSR_ALST2 | CAN_TSR_TERR2,
    };
    uint32_t stale_status = hcan->Instance->TSR;
    for ( uint8_t mailbox = 0U; mailbox < 3U; mailbox++ )
    {
        if ( ( stale_status & request_complete_flags[mailbox] ) != 0U )
        {
            HW_CAN_Clear_Tx_Request_Complete( hcan->Instance, request_complete_flags[mailbox],
                                              mailbox_status_flags[mailbox] );
        }
    }

    *active          = true;
    *completed       = false;
    *pending_mailbox = 0U;
    *status          = HW_CAN_TX_STATUS_ACTIVE;
    SET_BIT( hcan->Instance->IER, CAN_IER_TMEIE );

    HW_CAN_Result_T result = HW_CAN_Tx_Service( hcan, buffer, w_p, r_p, buffer_width, active,
                                                completed, pending_mailbox, status );

    return result == HW_CAN_RESULT_ERROR ? HW_CAN_RESULT_ERROR : HW_CAN_RESULT_OK;
}

/** Abort all three hardware TX mailboxes using bxCAN command semantics. */
static void HW_CAN_Abort_Tx_Mailboxes( CAN_TypeDef* can )
{
#ifdef TEST_BUILD
    can->TSR &= ~( CAN_TSR_RQCP0 | CAN_TSR_TXOK0 | CAN_TSR_ALST0 | CAN_TSR_TERR0 | CAN_TSR_RQCP1
                   | CAN_TSR_TXOK1 | CAN_TSR_ALST1 | CAN_TSR_TERR1 | CAN_TSR_RQCP2 | CAN_TSR_TXOK2
                   | CAN_TSR_ALST2 | CAN_TSR_TERR2 );
    can->TSR |= HW_CAN_TX_MAILBOX_EMPTY_MASK;
    for ( uint8_t mailbox = 0U; mailbox < 3U; mailbox++ )
    {
        CLEAR_BIT( can->sTxMailBox[mailbox].TIR, CAN_TI0R_TXRQ );
    }
#else
    can->TSR = CAN_TSR_ABRQ0 | CAN_TSR_ABRQ1 | CAN_TSR_ABRQ2;
#endif
}

/** Recover one CAN channel in task context after a terminal transmit error. */
static HW_CAN_Result_T HW_CAN_Recover( CAN_HandleTypeDef* hcan, IRQn_Type tx_irq, IRQn_Type rx_irq,
                                       IRQn_Type error_irq, volatile uint16_t* tx_wp,
                                       volatile uint16_t* tx_rp, volatile bool* active,
                                       volatile bool* completed, volatile uint32_t* pending_mailbox,
                                       volatile HW_CAN_Tx_Status_T* status,
                                       HWCANLifecycleState_T*       lifecycle )
{
    if ( hcan == NULL || lifecycle == NULL )
    {
        return HW_CAN_RESULT_ERROR;
    }

    if ( !lifecycle->is_configured )
    {
        return HW_CAN_RESULT_NOT_CONFIGURED;
    }

    if ( !lifecycle->is_started )
    {
        return HW_CAN_RESULT_NOT_STARTED;
    }

    CAN_TypeDef* can = hcan->Instance;

    NVIC_DisableIRQ( tx_irq );
    NVIC_DisableIRQ( rx_irq );
    NVIC_DisableIRQ( error_irq );

    CLEAR_BIT( can->IER, CAN_IER_TMEIE | HW_CAN_RX_INTERRUPT_MASK | HW_CAN_ERROR_INTERRUPT_MASK );

    HW_CAN_Abort_Tx_Mailboxes( can );

    HAL_StatusTypeDef stop_result = HAL_CAN_Stop( hcan );

    /*
     * Recovery deliberately discards the failed transmission and any queued
     * packets, matching the existing recovery contract.
     */
    *tx_wp           = 0U;
    *tx_rp           = 0U;
    *active          = false;
    *completed       = false;
    *pending_mailbox = 0U;

    CLEAR_BIT( can->ESR, CAN_ESR_LEC );
    HW_CAN_Clear_Error_Interrupt( can );

    if ( stop_result != HAL_OK )
    {
        *status = HW_CAN_TX_STATUS_ERROR;

        /*
         * No successful lifecycle transition occurred. Restore operation so
         * another recovery attempt remains possible.
         */
        SET_BIT( can->IER, HW_CAN_RX_INTERRUPT_MASK | HW_CAN_ERROR_INTERRUPT_MASK );
        NVIC_EnableIRQ( error_irq );
        NVIC_EnableIRQ( rx_irq );
        NVIC_EnableIRQ( tx_irq );

        return HW_CAN_RESULT_ERROR;
    }

    lifecycle->is_started = false;

    HW_CAN_Result_T start_result = HW_CAN_Start( hcan, tx_irq, rx_irq, error_irq, lifecycle );

    if ( start_result != HW_CAN_RESULT_OK )
    {
        *status = HW_CAN_TX_STATUS_ERROR;
        return start_result;
    }

    *status = HW_CAN_TX_STATUS_IDLE;

    return HW_CAN_RESULT_OK;
}

/** Discard queued TX packets while preserving all other channel state. */
static void HW_CAN_Tx_Buffer_Cancel( IRQn_Type tx_irq, volatile uint16_t* w_p,
                                     volatile uint16_t* r_p )
{
    uint32_t tx_irq_was_enabled = NVIC_GetEnableIRQ( tx_irq );
    NVIC_DisableIRQ( tx_irq );

    *w_p = *r_p;

    if ( tx_irq_was_enabled != 0U )
    {
        NVIC_EnableIRQ( tx_irq );
    }
}

/**
 * @brief Checks whether a packet fits the supported classical CAN data-frame contract.
 *
 * @param packet Packet to validate.
 *
 * @return true for a standard identifier and DLC from 0 through 8.
 */
static bool HW_CAN_Packet_Is_Valid( const CAN_Packet_T* packet )
{
    return packet->id <= CAN_STANDARD_ID_MAX && packet->dlc <= CAN_PACKET_SIZE;
}
