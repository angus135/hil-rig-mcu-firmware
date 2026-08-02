/******************************************************************************
 *  File:       test_hw_can.cpp
 *  Author:     timothy vogelsang
 *  Created:    25-Mar-2026
 *
 *  Description:
 *      Unit tests for the hw_can module using GoogleTest and GoogleMock.
 *      The suite uses fake CAN peripheral registers and mocked HAL CAN
 *      functions to test transmission, reception, buffering, interrupts,
 *      filtering, configuration, and channel status behaviour in isolation.
 ******************************************************************************/

#include <gtest/gtest.h>
#include <gmock/gmock.h>

extern "C"
{
#include "hw_can.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
}

#include <cstring>

extern "C"
{
#include "hw_can.c"
}

using ::testing::_;
using ::testing::DoAll;
using ::testing::Return;
using ::testing::SaveArg;

/**-----------------------------------------------------------------------------
 *  Test Constants / Variables
 *------------------------------------------------------------------------------
 */

/* Fake CAN1 peripheral registers used to exercise register-level driver code without hardware. */
// static CAN_TypeDef mock_can1_regs{};
// /* Fake CAN2 peripheral registers used to exercise register-level driver code without hardware.
// */ static CAN_TypeDef mock_can2_regs{};

/* HAL CAN handle associated with the fake CAN1 peripheral instance. */
CAN_HandleTypeDef hcan1{};
/* HAL CAN handle associated with the fake CAN2 peripheral instance. */
CAN_HandleTypeDef hcan2{};

/**-----------------------------------------------------------------------------
 *  Test Helpers
 *------------------------------------------------------------------------------
 */

/**
 * Reset all CAN TX/RX software buffers and associated state.
 *
 * The driver uses static buffer storage and read/write pointers, so each test
 * must start from a known empty state to prevent one test from affecting
 * another.
 */
static void ResetCANBuffers()
{
    can_tx_wp1 = 0;
    can_tx_rp1 = 0;

    can_tx_wp2 = 0;
    can_tx_rp2 = 0;

    can_rx_wp1 = 0;
    can_rx_rp1 = 0;

    can_rx_wp2 = 0;
    can_rx_rp2 = 0;

    memset( ( void* )can_tx_buffer1, 0, sizeof( can_tx_buffer1 ) );

    memset( ( void* )can_tx_buffer2, 0, sizeof( can_tx_buffer2 ) );

    memset( ( void* )can_rx_buffer1, 0, sizeof( can_rx_buffer1 ) );

    memset( ( void* )can_rx_buffer2, 0, sizeof( can_rx_buffer2 ) );

    can_sent_flag1 = false;
    can_sent_flag2 = false;
}

/**-----------------------------------------------------------------------------
 *  Test Doubles / Mocks
 *------------------------------------------------------------------------------
 */

/**
 * Mock wrapper for the HAL CAN functions used by the configuration path.
 *
 * Mocking these calls allows the tests to exercise successful and failing HAL
 * operations without requiring an initialized STM32 CAN peripheral.
 */
class MockHWCAN
{
public:
    MOCK_METHOD( HAL_StatusTypeDef, CANInit, ( CAN_HandleTypeDef * hcan ), () );

    MOCK_METHOD( HAL_StatusTypeDef, CANConfigFilter,
                 ( CAN_HandleTypeDef * hcan, CAN_FilterTypeDef* filter ), () );

    MOCK_METHOD( HAL_StatusTypeDef, CANStart, ( CAN_HandleTypeDef * hcan ), () );

    MOCK_METHOD( HAL_StatusTypeDef, CANActivateNotification,
                 ( CAN_HandleTypeDef * hcan, uint32_t flags ), () );
};

/* Active GoogleMock instance used by the C-linkage HAL wrappers below. */
static MockHWCAN* g_mock = nullptr;

extern "C" HAL_StatusTypeDef HAL_CAN_Init( CAN_HandleTypeDef* hcan )
{
    return g_mock->CANInit( hcan );
}

extern "C" HAL_StatusTypeDef HAL_CAN_ConfigFilter( CAN_HandleTypeDef* hcan,
                                                   CAN_FilterTypeDef* filter )
{
    return g_mock->CANConfigFilter( hcan, filter );
}

extern "C" HAL_StatusTypeDef HAL_CAN_Start( CAN_HandleTypeDef* hcan )
{
    return g_mock->CANStart( hcan );
}

extern "C" HAL_StatusTypeDef HAL_CAN_ActivateNotification( CAN_HandleTypeDef* hcan, uint32_t flags )
{
    return g_mock->CANActivateNotification( hcan, flags );
}

/**-----------------------------------------------------------------------------
 *  Test Fixture
 *------------------------------------------------------------------------------
 */

/**
 * Common GoogleTest fixture for the hw_can unit tests.
 *
 * Each test receives a fresh mock, cleared fake CAN registers, reset software
 * buffers, and CAN handles pointing at the corresponding fake peripherals.
 */
class HWCANTest : public ::testing::Test
{
protected:
    MockHWCAN mock;

    /* Prepare isolated fake hardware and software state before each test. */
    void SetUp() override
    {
        g_mock = &mock;

        ResetCANBuffers();

        memset( &mock_can1_regs, 0, sizeof( mock_can1_regs ) );

        memset( &mock_can2_regs, 0, sizeof( mock_can2_regs ) );

        hcan1.Instance = &mock_can1_regs;
        hcan2.Instance = &mock_can2_regs;
    }

    /* Release the global mock pointer after each test. */
    void TearDown() override
    {
        g_mock = nullptr;
    }
};

/**-----------------------------------------------------------------------------
 *  Compute Properties Tests
 *------------------------------------------------------------------------------
 */

/** Verify the calculated CAN timing parameters for a 1 Mbps configuration. */
TEST_F( HWCANTest, ComputePropertiesReturnsExpectedValuesFor1Mbps )
{
    CanProperties_T props = HW_CAN_Compute_Properties( 1000000, 15, 800 );

    EXPECT_EQ( props.bs1, 11 );
    EXPECT_EQ( props.bs2, 3 );
    EXPECT_EQ( props.psc, 3 );
    EXPECT_EQ( props.timer_hz, 45000000 );
}

/** Verify that an invalid zero bitrate produces zeroed timing properties. */
TEST_F( HWCANTest, ComputePropertiesRejectsInvalidBitrate )
{
    CanProperties_T props = HW_CAN_Compute_Properties( 0, 15, 800 );

    EXPECT_EQ( props.bs1, 0 );
    EXPECT_EQ( props.bs2, 0 );
    EXPECT_EQ( props.psc, 0 );
}

/**-----------------------------------------------------------------------------
 *  Transmit Tests
 *------------------------------------------------------------------------------
 */

/** Verify that transmission fails when no TX mailbox is available. */
TEST_F( HWCANTest, TransmitFailsWhenMailboxBusy )
{
    mock_can1_regs.TSR = 0;

    uint8_t data[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };

    int result = HW_CAN_Transmit( &hcan1, data, 0x123, 8 );

    EXPECT_EQ( result, 1 );
}

/** Verify that a standard CAN frame is encoded into TX mailbox 0 with the expected ID, DLC, and
 * payload. */
TEST_F( HWCANTest, TransmitLoadsMailboxCorrectly )
{
    mock_can1_regs.TSR = CAN_TSR_TME0;

    uint8_t data[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };

    uint16_t id = 0x123;

    int result = HW_CAN_Transmit( &hcan1, data, id, 8 );

    EXPECT_EQ( result, 0 );

    EXPECT_EQ( mock_can1_regs.sTxMailBox[0].TIR,
               ( static_cast<uint32_t>( id ) << 21 ) | CAN_TI0R_TXRQ );

    EXPECT_EQ( mock_can1_regs.sTxMailBox[0].TDTR, 8 );

    EXPECT_EQ( mock_can1_regs.sTxMailBox[0].TDLR, 0x04030201 );

    EXPECT_EQ( mock_can1_regs.sTxMailBox[0].TDHR, 0x08070605 );
}

/** Verify that an identifier outside the standard 11-bit CAN ID range is rejected. */
TEST_F( HWCANTest, TransmitRejectsIDAbove11Bits )
{
    mock_can1_regs.TSR = CAN_TSR_TME0;

    uint8_t data[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };

    int result = HW_CAN_Transmit( &hcan1, data, 0x800, 8 );

    EXPECT_EQ( result, 1 );
}

/** Verify that the largest valid standard CAN identifier, 0x7FF, is accepted. */
TEST_F( HWCANTest, TransmitAcceptsMaximum11BitID )
{
    mock_can1_regs.TSR = CAN_TSR_TME0;

    uint8_t data[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };

    uint16_t id = 0x7FF;

    int result = HW_CAN_Transmit( &hcan1, data, id, 8 );

    EXPECT_EQ( result, 0 );

    EXPECT_EQ( mock_can1_regs.sTxMailBox[0].TIR,
               ( static_cast<uint32_t>( id ) << 21 ) | CAN_TI0R_TXRQ );
}

/**-----------------------------------------------------------------------------
 *  Receive Tests
 *------------------------------------------------------------------------------
 */

/** Verify that reception fails when CAN FIFO 0 contains no pending message. */
TEST_F( HWCANTest, ReceiveFailsWhenFIFOEmpty )
{
    mock_can1_regs.RF0R = 0;

    CAN_Packet_T packet = {};

    int result = HW_CAN_Receive( &hcan1, &packet );

    EXPECT_EQ( result, 1 );
}

/** Verify that a frame is decoded correctly from CAN FIFO 0 and that the FIFO release operation is
 * requested. */
TEST_F( HWCANTest, ReceiveReadsIDAndDataCorrectly )
{
    mock_can1_regs.RF0R = CAN_RF0R_FMP0;

    uint16_t id = 0x456;

    mock_can1_regs.sFIFOMailBox[0].RIR = static_cast<uint32_t>( id ) << 21;

    mock_can1_regs.sFIFOMailBox[0].RDLR = 0x04030201;

    mock_can1_regs.sFIFOMailBox[0].RDHR = 0x08070605;

    CAN_Packet_T packet = {};

    int result = HW_CAN_Receive( &hcan1, &packet );

    EXPECT_EQ( result, 0 );

    EXPECT_EQ( packet.id, id );

    EXPECT_EQ( packet.data[0], 1 );

    EXPECT_EQ( packet.data[1], 2 );

    EXPECT_EQ( packet.data[2], 3 );

    EXPECT_EQ( packet.data[3], 4 );

    EXPECT_EQ( packet.data[4], 5 );

    EXPECT_EQ( packet.data[5], 6 );

    EXPECT_EQ( packet.data[6], 7 );

    EXPECT_EQ( packet.data[7], 8 );

    EXPECT_TRUE( mock_can1_regs.RF0R & CAN_RF0R_RFOM0 );
}

/**-----------------------------------------------------------------------------
 *  Buffer Tests
 *------------------------------------------------------------------------------
 */

/** Verify that a packet written to the channel 1 TX buffer can be read back without changing its ID
 * or payload; reading is non-consuming. */
TEST_F( HWCANTest, TxBufferWriteAndReadPreservesIDAndData )
{
    CAN_Packet_T tx[1] = { { .id = 0x123, .data = { 1, 2, 3, 4, 5, 6, 7, 8 } } };

    EXPECT_EQ( HW_CAN_Tx_Buffer_Write1( tx, 1 ), 0 );

    CAN_Packet_T out[1] = {};

    uint16_t count = HW_CAN_Tx_Buffer_Read1( out );

    EXPECT_EQ( count, 1 );

    EXPECT_EQ( out[0].id, 0x123 );

    EXPECT_EQ( out[0].data[0], 1 );

    EXPECT_EQ( out[0].data[1], 2 );

    EXPECT_EQ( out[0].data[7], 8 );

    /* Read does not consume */
    EXPECT_EQ( can_tx_rp1, 0 );

    HW_CAN_Buffer_consume( &can_tx_rp1, 1, TRANSMIT_BUFFER_WIDTH );
}

/** Verify that the TX software buffer preserves the maximum standard CAN ID and all payload bytes.
 */
TEST_F( HWCANTest, TxBufferPreservesMaximum11BitID )
{
    CAN_Packet_T tx[1] = { { .id = 0x7FF, .data = { 1, 2, 3, 4, 5, 6, 7, 8 } } };

    EXPECT_EQ( HW_CAN_Tx_Buffer_Write1( tx, 1 ), 0 );

    CAN_Packet_T out[1] = {};

    EXPECT_EQ( HW_CAN_Tx_Buffer_Read1( out ), 1 );

    EXPECT_EQ( out[0].id, 0x7FF );

    for ( int i = 0; i < CAN_PACKET_SIZE; i++ )
    {
        EXPECT_EQ( out[0].data[i], i + 1 );
    }
}

/** Verify that reading an empty TX buffer reports that no packets are available. */
TEST_F( HWCANTest, BufferReadReturnsZeroWhenEmpty )
{
    CAN_Packet_T out[1] = {};

    EXPECT_EQ( HW_CAN_Tx_Buffer_Read1( out ), 0 );
}

/**-----------------------------------------------------------------------------
 *  TX ISR Tests
 *------------------------------------------------------------------------------
 */

/** Verify that the TX interrupt handler disables the TX mailbox-empty interrupt when no packet is
 * buffered and reports the channel as sent. */
TEST_F( HWCANTest, TxIRQDisablesInterruptWhenBufferEmpty )
{
    ResetCANBuffers();
    mock_can1_regs.IER = CAN_IER_TMEIE;  // enable interrupt

    HW_CAN_CH1_TX_IRQ_HANDLER();

    // check it disabled the interrupt
    EXPECT_FALSE( mock_can1_regs.IER & CAN_IER_TMEIE );

    EXPECT_TRUE( HW_CAN_Channl1_sent() );
}

/** Verify that the channel 1 TX interrupt handler loads a buffered packet into the CAN TX mailbox
 * with the expected ID, payload, and DLC. */
TEST_F( HWCANTest, TxIRQTransmitsBufferedPacketWithCorrectIDAndData )
{
    mock_can1_regs.TSR = CAN_TSR_TME0;

    CAN_Packet_T packet[1] = { { .id = 0x321, .data = { 9, 8, 7, 6, 5, 4, 3, 2 } } };

    EXPECT_EQ( HW_CAN_Tx_Buffer_Write1( packet, 1 ), 0 );

    HW_CAN_CH1_TX_IRQ_HANDLER();

    EXPECT_EQ( mock_can1_regs.sTxMailBox[0].TIR,
               ( static_cast<uint32_t>( 0x321 ) << 21 ) | CAN_TI0R_TXRQ );

    EXPECT_EQ( mock_can1_regs.sTxMailBox[0].TDLR, 0x06070809 );

    EXPECT_EQ( mock_can1_regs.sTxMailBox[0].TDHR, 0x02030405 );

    EXPECT_EQ( mock_can1_regs.sTxMailBox[0].TDTR, 8 );
}

/**-----------------------------------------------------------------------------
 *  RX ISR Tests
 *------------------------------------------------------------------------------
 */

/** Verify that the channel 1 RX interrupt handler stores a pending FIFO frame in the channel 1 RX
 * software buffer. */
TEST_F( HWCANTest, RxIRQStoresIDAndDataInBuffer )
{
    uint16_t id = 0x555;

    mock_can1_regs.RF0R = CAN_RF0R_FMP0;

    mock_can1_regs.sFIFOMailBox[0].RIR = static_cast<uint32_t>( id ) << 21;

    mock_can1_regs.sFIFOMailBox[0].RDLR = 0x04030201;

    mock_can1_regs.sFIFOMailBox[0].RDHR = 0x08070605;

    HW_CAN_CH1_RX_IRQ_HANDLER();

    CAN_Packet_T out[1] = {};

    EXPECT_EQ( HW_CAN_Rx_Buffer_Read1( out ), 1 );

    EXPECT_EQ( out[0].id, id );

    EXPECT_EQ( out[0].data[0], 1 );

    EXPECT_EQ( out[0].data[7], 8 );
}

/** Verify that HAL RX callbacks route pending frames to the matching software buffer. */
TEST_F( HWCANTest, HALRxCallbackRoutesBothChannels )
{
    mock_can1_regs.RF0R                 = CAN_RF0R_FMP0;
    mock_can1_regs.sFIFOMailBox[0].RIR  = static_cast<uint32_t>( 0x111 ) << 21;
    mock_can1_regs.sFIFOMailBox[0].RDLR = 0x04030201;
    mock_can1_regs.sFIFOMailBox[0].RDHR = 0x08070605;
    mock_can2_regs.RF0R                 = CAN_RF0R_FMP0;
    mock_can2_regs.sFIFOMailBox[0].RIR  = static_cast<uint32_t>( 0x222 ) << 21;
    mock_can2_regs.sFIFOMailBox[0].RDLR = 0x0C0B0A09;
    mock_can2_regs.sFIFOMailBox[0].RDHR = 0x100F0E0D;

    HAL_CAN_RxFifo0MsgPendingCallback( &hcan1 );
    HAL_CAN_RxFifo0MsgPendingCallback( &hcan2 );

    CAN_Packet_T channel1[1] = {};
    CAN_Packet_T channel2[1] = {};

    ASSERT_EQ( HW_CAN_Rx_Buffer_Read1( channel1 ), 1 );
    ASSERT_EQ( HW_CAN_Rx_Buffer_Read2( channel2 ), 1 );
    EXPECT_EQ( channel1[0].id, 0x111 );
    EXPECT_EQ( channel1[0].data[0], 1 );
    EXPECT_EQ( channel2[0].id, 0x222 );
    EXPECT_EQ( channel2[0].data[0], 9 );
}

/** Verify that HAL TX completion callbacks advance multi-packet batches on both channels. */
TEST_F( HWCANTest, HALTxCallbacksAdvanceBothChannelsAndMultiPacketSequence )
{
    mock_can1_regs.TSR = CAN_TSR_TME0;
    mock_can2_regs.TSR = CAN_TSR_TME0;

    CAN_Packet_T channel1[2] = {
        { .id = 0x101, .data = { 1, 2, 3, 4, 5, 6, 7, 8 } },
        { .id = 0x102, .data = { 9, 10, 11, 12, 13, 14, 15, 16 } },
    };
    CAN_Packet_T channel2[2] = {
        { .id = 0x201, .data = { 17, 18, 19, 20, 21, 22, 23, 24 } },
        { .id = 0x202, .data = { 25, 26, 27, 28, 29, 30, 31, 32 } },
    };

    ASSERT_EQ( HW_CAN_Tx_Buffer_Write1( channel1, 2 ), 0 );
    ASSERT_EQ( HW_CAN_Tx_Buffer_Write2( channel2, 2 ), 0 );

    HW_CAN_Tx_Trigger1();
    HW_CAN_Tx_Trigger2();

    EXPECT_EQ( mock_can1_regs.sTxMailBox[0].TIR,
               ( static_cast<uint32_t>( 0x101 ) << 21 ) | CAN_TI0R_TXRQ );
    EXPECT_EQ( mock_can2_regs.sTxMailBox[0].TIR,
               ( static_cast<uint32_t>( 0x201 ) << 21 ) | CAN_TI0R_TXRQ );

    HAL_CAN_TxMailbox0CompleteCallback( &hcan1 );
    HAL_CAN_TxMailbox1CompleteCallback( &hcan2 );

    EXPECT_EQ( mock_can1_regs.sTxMailBox[0].TIR,
               ( static_cast<uint32_t>( 0x102 ) << 21 ) | CAN_TI0R_TXRQ );
    EXPECT_EQ( mock_can2_regs.sTxMailBox[0].TIR,
               ( static_cast<uint32_t>( 0x202 ) << 21 ) | CAN_TI0R_TXRQ );
    EXPECT_FALSE( HW_CAN_Channl1_sent() );
    EXPECT_FALSE( HW_CAN_Channl2_sent() );

    HAL_CAN_TxMailbox2CompleteCallback( &hcan1 );
    HAL_CAN_TxMailbox2CompleteCallback( &hcan2 );

    EXPECT_TRUE( HW_CAN_Channl1_sent() );
    EXPECT_TRUE( HW_CAN_Channl2_sent() );
}

/**-----------------------------------------------------------------------------
 *  Full Buffer Tests
 *------------------------------------------------------------------------------
 */

/** Verify that the TX ring buffer rejects a write once its usable capacity is exhausted. */
TEST_F( HWCANTest, TxBufferWriteFailsWhenFull )
{
    CAN_Packet_T packet[1] = { { .id = 0x123, .data = { 1, 2, 3, 4, 5, 6, 7, 8 } } };

    /* Buffer capacity is width - 1 */
    for ( int i = 0; i < TRANSMIT_BUFFER_WIDTH - 1; i++ )
    {
        EXPECT_EQ( HW_CAN_Tx_Buffer_Write1( packet, 1 ), 0 );
    }

    /* Next write should fail */
    EXPECT_EQ( HW_CAN_Tx_Buffer_Write1( packet, 1 ), 1 );
}

/** Verify that the RX ring buffer rejects a write once its usable capacity is exhausted. */
TEST_F( HWCANTest, RxBufferWriteFailsWhenFull )
{
    CAN_Packet_T packet[1] = { { .id = 0x456, .data = { 9, 8, 7, 6, 5, 4, 3, 2 } } };

    for ( int i = 0; i < RECEIVE_BUFFER_WIDTH - 1; i++ )
    {
        EXPECT_EQ( HW_CAN_Rx_Buffer_Write1( packet, 1 ), 0 );
    }

    EXPECT_EQ( HW_CAN_Rx_Buffer_Write1( packet, 1 ), 1 );
}

/** Verify that TX ring-buffer wraparound preserves FIFO ordering after packets are consumed and new
 * packets are written. */
TEST_F( HWCANTest, TxBufferWraparoundPreservesPackets )
{
    CAN_Packet_T packet[1] = {};

    for ( int i = 0; i < 10; i++ )
    {
        packet[0].id = 0x100 + i;

        for ( int j = 0; j < CAN_PACKET_SIZE; j++ )
        {
            packet[0].data[j] = i;
        }

        EXPECT_EQ( HW_CAN_Tx_Buffer_Write1( packet, 1 ), 0 );
    }

    CAN_Packet_T out[20] = {};

    uint16_t count = HW_CAN_Tx_Buffer_Read1( out );

    EXPECT_EQ( count, 10 );

    for ( int i = 0; i < 10; i++ )
    {
        EXPECT_EQ( out[i].id, 0x100 + i );

        EXPECT_EQ( out[i].data[0], i );
    }

    /* Consume first 8 packets */
    HW_CAN_Buffer_consume( &can_tx_rp1, 8, TRANSMIT_BUFFER_WIDTH );

    /* Force wraparound */
    for ( int i = 0; i < 8; i++ )
    {
        packet[0].id = 0x200 + i;

        for ( int j = 0; j < CAN_PACKET_SIZE; j++ )
        {
            packet[0].data[j] = 100 + i;
        }

        EXPECT_EQ( HW_CAN_Tx_Buffer_Write1( packet, 1 ), 0 );
    }

    memset( out, 0, sizeof( out ) );

    count = HW_CAN_Tx_Buffer_Read1( out );

    EXPECT_EQ( count, 10 );

    /* Remaining original packets */
    EXPECT_EQ( out[0].id, 0x108 );

    EXPECT_EQ( out[1].id, 0x109 );

    /* Wrapped packets */
    for ( int i = 0; i < 8; i++ )
    {
        EXPECT_EQ( out[i + 2].id, 0x200 + i );

        EXPECT_EQ( out[i + 2].data[0], 100 + i );
    }
}

/**-----------------------------------------------------------------------------
 *  Filter Tests
 *------------------------------------------------------------------------------
 */

/** Verify that a standard 11-bit CAN ID and mask are translated into the expected STM32 filter
 * configuration. */
TEST_F( HWCANTest, FilterConfiguresStandardIDMaskCorrectly )
{
    CAN_FilterTypeDef filter = {};

    EXPECT_CALL( mock, CANConfigFilter( _, _ ) ).WillOnce( Return( HAL_OK ) );

    uint16_t filter_id   = 0x123;
    uint16_t filter_mask = 0x7FF;

    HAL_StatusTypeDef result =
        HW_CAN_Apply_Filter_HAL( &filter, &hcan1, 0, filter_id, filter_mask );

    EXPECT_EQ( result, HAL_OK );

    EXPECT_EQ( filter.FilterMode, CAN_FILTERMODE_IDMASK );

    EXPECT_EQ( filter.FilterScale, CAN_FILTERSCALE_32BIT );

    EXPECT_EQ( filter.FilterIdHigh, static_cast<uint16_t>( filter_id << 5 ) );

    EXPECT_EQ( filter.FilterMaskIdHigh, static_cast<uint16_t>( filter_mask << 5 ) );

    EXPECT_EQ( filter.FilterBank, 0 );

    EXPECT_EQ( filter.FilterFIFOAssignment, CAN_FILTER_FIFO0 );

    EXPECT_EQ( filter.FilterActivation, ENABLE );

    EXPECT_EQ( filter.SlaveStartFilterBank, 14 );
}

/** Verify that an out-of-range filter ID is rejected without calling HAL_CAN_ConfigFilter(). */
TEST_F( HWCANTest, FilterRejectsIDAbove11Bits )
{
    CAN_FilterTypeDef filter = {};

    EXPECT_CALL( mock, CANConfigFilter( _, _ ) ).Times( 0 );

    EXPECT_EQ( HW_CAN_Apply_Filter_HAL( &filter, &hcan1, 0, 0x800, 0x7FF ), HAL_ERROR );
}

/** Verify that an out-of-range filter mask is rejected without calling HAL_CAN_ConfigFilter(). */
TEST_F( HWCANTest, FilterRejectsMaskAbove11Bits )
{
    CAN_FilterTypeDef filter = {};

    EXPECT_CALL( mock, CANConfigFilter( _, _ ) ).Times( 0 );

    EXPECT_EQ( HW_CAN_Apply_Filter_HAL( &filter, &hcan1, 0, 0x123, 0x800 ), HAL_ERROR );
}

/** Verify that CAN1 may use filter bank 13. */
TEST_F( HWCANTest, CAN1FilterBank13IsAccepted )
{
    CAN_FilterTypeDef filter = {};

    EXPECT_CALL( mock, CANConfigFilter( _, _ ) ).WillOnce( Return( HAL_OK ) );

    EXPECT_EQ( HW_CAN_Apply_Filter_HAL( &filter, &hcan1, 13, 0x123, 0x7FF ), HAL_OK );
}

/** Verify that CAN1 may not use filter bank 14. */
TEST_F( HWCANTest, CAN1FilterBank14IsRejected )
{
    CAN_FilterTypeDef filter = {};

    EXPECT_CALL( mock, CANConfigFilter( _, _ ) ).Times( 0 );

    EXPECT_EQ( HW_CAN_Apply_Filter_HAL( &filter, &hcan1, 14, 0x123, 0x7FF ), HAL_ERROR );
}

/** Verify that CAN2 may use filter bank 14. */
TEST_F( HWCANTest, CAN2FilterBank14IsAccepted )
{
    CAN_FilterTypeDef filter = {};

    EXPECT_CALL( mock, CANConfigFilter( _, _ ) ).WillOnce( Return( HAL_OK ) );

    EXPECT_EQ( HW_CAN_Apply_Filter_HAL( &filter, &hcan2, 14, 0x123, 0x7FF ), HAL_OK );
}

/** Verify that CAN2 may use filter bank 27. */
TEST_F( HWCANTest, CAN2FilterBank27IsAccepted )
{
    CAN_FilterTypeDef filter = {};

    EXPECT_CALL( mock, CANConfigFilter( _, _ ) ).WillOnce( Return( HAL_OK ) );

    EXPECT_EQ( HW_CAN_Apply_Filter_HAL( &filter, &hcan2, 27, 0x123, 0x7FF ), HAL_OK );
}

/**-----------------------------------------------------------------------------
 *  Configure Tests
 *------------------------------------------------------------------------------
 */

/** Verify that HW_CAN_Configure() returns error code 1 when CAN initialization fails. */
TEST_F( HWCANTest, ConfigureReturns1WhenInitFails )
{
    EXPECT_CALL( mock, CANInit( _ ) ).WillOnce( Return( HAL_ERROR ) );

    EXPECT_CALL( mock, CANConfigFilter( _, _ ) ).Times( 0 );

    int result = HW_CAN_Configure( &hcan1, 1000000, 0, 0x123, 0x7FF );

    EXPECT_EQ( result, 1 );
}

/** Verify that HW_CAN_Configure() returns error code 2 when filter configuration fails. */
TEST_F( HWCANTest, ConfigureReturns2WhenFilterFails )
{
    EXPECT_CALL( mock, CANInit( _ ) ).WillOnce( Return( HAL_OK ) );

    EXPECT_CALL( mock, CANConfigFilter( _, _ ) ).WillOnce( Return( HAL_ERROR ) );

    int result = HW_CAN_Configure( &hcan1, 1000000, 0, 0x123, 0x7FF );

    EXPECT_EQ( result, 2 );
}

/** Verify that HW_CAN_Configure() returns error code 3 when starting the CAN peripheral fails. */
TEST_F( HWCANTest, ConfigureReturns3WhenStartFails )
{
    EXPECT_CALL( mock, CANInit( _ ) ).WillOnce( Return( HAL_OK ) );

    EXPECT_CALL( mock, CANConfigFilter( _, _ ) ).WillOnce( Return( HAL_OK ) );

    EXPECT_CALL( mock, CANStart( _ ) ).WillOnce( Return( HAL_ERROR ) );

    int result = HW_CAN_Configure( &hcan1, 1000000, 0, 0x123, 0x7FF );

    EXPECT_EQ( result, 3 );
}

/** Verify that HW_CAN_Configure() returns error code 4 when CAN notification activation fails. */
TEST_F( HWCANTest, ConfigureReturns4WhenNotificationActivationFails )
{
    EXPECT_CALL( mock, CANInit( _ ) ).WillOnce( Return( HAL_OK ) );

    EXPECT_CALL( mock, CANConfigFilter( _, _ ) ).WillOnce( Return( HAL_OK ) );

    EXPECT_CALL( mock, CANStart( _ ) ).WillOnce( Return( HAL_OK ) );

    EXPECT_CALL( mock, CANActivateNotification( _, _ ) ).WillOnce( Return( HAL_ERROR ) );

    int result = HW_CAN_Configure( &hcan1, 1000000, 0, 0x123, 0x7FF );

    EXPECT_EQ( result, 4 );
}

/** Verify that HW_CAN_Configure() completes successfully when each HAL configuration step succeeds.
 */
TEST_F( HWCANTest, ConfigureSucceedsWithValidConfiguration )
{
    EXPECT_CALL( mock, CANInit( _ ) ).WillOnce( Return( HAL_OK ) );

    EXPECT_CALL( mock, CANConfigFilter( _, _ ) ).WillOnce( Return( HAL_OK ) );

    EXPECT_CALL( mock, CANStart( _ ) ).WillOnce( Return( HAL_OK ) );

    EXPECT_CALL( mock, CANActivateNotification( _, _ ) ).WillOnce( Return( HAL_OK ) );

    int result = HW_CAN_Configure( &hcan1, 1000000, 0, 0x123, 0x7FF );

    EXPECT_EQ( result, 0 );
}

/**-----------------------------------------------------------------------------
 *  Sent Flag Tests
 *------------------------------------------------------------------------------
 */

/** Verify that both channel sent flags are cleared at the start of a test. */
TEST_F( HWCANTest, SentFlagStartsFalse )
{
    EXPECT_FALSE( HW_CAN_Channl1_sent() );

    EXPECT_FALSE( HW_CAN_Channl2_sent() );
}

/** Verify that the channel 1 TX interrupt handler sets the sent flag and disables the TX interrupt
 * when no packet remains in the TX buffer. */
TEST_F( HWCANTest, TxIRQSetsSentFlagWhenBufferEmpty )
{
    mock_can1_regs.IER = CAN_IER_TMEIE;

    HW_CAN_CH1_TX_IRQ_HANDLER();

    EXPECT_TRUE( HW_CAN_Channl1_sent() );

    EXPECT_FALSE( mock_can1_regs.IER & CAN_IER_TMEIE );
}
