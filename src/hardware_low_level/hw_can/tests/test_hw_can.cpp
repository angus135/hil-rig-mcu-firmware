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

static bool nvic_irq_enabled[6] = {};

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

    can_sent_flag1          = false;
    can_sent_flag2          = false;
    can_tx_active1          = false;
    can_tx_active2          = false;
    can_rx_dropped_count1   = 0;
    can_rx_dropped_count2   = 0;
    can_tx_pending_mailbox1 = 0;
    can_tx_pending_mailbox2 = 0;
    can_tx_status1          = HW_CAN_TX_STATUS_IDLE;
    can_tx_status2          = HW_CAN_TX_STATUS_IDLE;
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

    MOCK_METHOD( HAL_StatusTypeDef, CANStop, ( CAN_HandleTypeDef * hcan ), () );
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

extern "C" HAL_StatusTypeDef HAL_CAN_Stop( CAN_HandleTypeDef* hcan )
{
    return g_mock->CANStop( hcan );
}

extern "C" uint32_t NVIC_GetEnableIRQ( IRQn_Type irq )
{
    return nvic_irq_enabled[irq] ? 1U : 0U;
}

extern "C" void NVIC_DisableIRQ( IRQn_Type irq )
{
    nvic_irq_enabled[irq] = false;
}

extern "C" void NVIC_EnableIRQ( IRQn_Type irq )
{
    nvic_irq_enabled[irq] = true;
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

        for ( bool& enabled : nvic_irq_enabled )
        {
            enabled = true;
        }

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

/** Verify other project bitrates that are exact with the fixed 45 MHz, 15-TQ model. */
TEST_F( HWCANTest, ComputePropertiesAcceptsExactProjectBitrates )
{
    CanProperties_T props_500k = HW_CAN_Compute_Properties( 500000U, 15U, 800U );
    EXPECT_EQ( props_500k.bs1, 11U );
    EXPECT_EQ( props_500k.bs2, 3U );
    EXPECT_EQ( props_500k.psc, 6U );

    CanProperties_T props_250k = HW_CAN_Compute_Properties( 250000U, 15U, 800U );
    EXPECT_EQ( props_250k.bs1, 11U );
    EXPECT_EQ( props_250k.bs2, 3U );
    EXPECT_EQ( props_250k.psc, 12U );
}

/** Verify that timing which would require prescaler truncation is rejected. */
TEST_F( HWCANTest, ComputePropertiesRejectsInexact800Kbps )
{
    CanProperties_T props = HW_CAN_Compute_Properties( 800000U, 15U, 800U );

    EXPECT_EQ( props.bs1, 0U );
    EXPECT_EQ( props.bs2, 0U );
    EXPECT_EQ( props.psc, 0U );
    EXPECT_EQ( props.timer_hz, 0U );
}

/** Verify invalid total-TQ, sample-point, and prescaler ranges are rejected safely. */
TEST_F( HWCANTest, ComputePropertiesRejectsInvalidTimingRanges )
{
    EXPECT_EQ( HW_CAN_Compute_Properties( 500000U, 0U, 800U ).psc, 0U );
    EXPECT_EQ( HW_CAN_Compute_Properties( 500000U, 15U, 699U ).psc, 0U );
    EXPECT_EQ( HW_CAN_Compute_Properties( 500000U, 15U, 1000U ).psc, 0U );
    EXPECT_EQ( HW_CAN_Compute_Properties( 1000U, 15U, 800U ).psc, 0U );
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

    EXPECT_EQ( result, HW_CAN_RESULT_BUSY );
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

/** Verify that the four supported DLC boundaries are written to the mailbox exactly. */
TEST_F( HWCANTest, TransmitSupportsClassicalCANPayloadLengths )
{
    uint8_t data[8]       = { 1, 2, 3, 4, 5, 6, 7, 8 };
    uint8_t valid_dlcs[4] = { 0, 1, 4, 8 };

    for ( uint8_t dlc : valid_dlcs )
    {
        memset( &mock_can1_regs.sTxMailBox[0], 0xFF, sizeof( mock_can1_regs.sTxMailBox[0] ) );
        mock_can1_regs.TSR = CAN_TSR_TME0;

        ASSERT_EQ( HW_CAN_Transmit( &hcan1, data, 0x123, dlc ), 0 );
        EXPECT_EQ( mock_can1_regs.sTxMailBox[0].TDTR, dlc );

        uint32_t expected_low  = 0;
        uint32_t expected_high = 0;
        for ( uint8_t i = 0; i < dlc; i++ )
        {
            if ( i < 4U )
            {
                expected_low |= static_cast<uint32_t>( data[i] ) << ( i * 8U );
            }
            else
            {
                expected_high |= static_cast<uint32_t>( data[i] ) << ( ( i - 4U ) * 8U );
            }
        }
        EXPECT_EQ( mock_can1_regs.sTxMailBox[0].TDLR, expected_low );
        EXPECT_EQ( mock_can1_regs.sTxMailBox[0].TDHR, expected_high );
    }
}

/** Verify that a DLC above the classical CAN payload limit is rejected. */
TEST_F( HWCANTest, TransmitRejectsDLCAboveEight )
{
    mock_can1_regs.TSR = CAN_TSR_TME0;
    uint8_t data[8]    = {};

    EXPECT_EQ( HW_CAN_Transmit( &hcan1, data, 0x123, 9 ), 1 );
    EXPECT_EQ( mock_can1_regs.sTxMailBox[0].TIR, 0U );
}

/** Verify that a short frame replaces stale low and high mailbox payload data. */
TEST_F( HWCANTest, TransmitClearsReusedMailboxForShortPayload )
{
    mock_can1_regs.TSR                = CAN_TSR_TME0;
    mock_can1_regs.sTxMailBox[0].TDLR = 0xFFFFFFFF;
    mock_can1_regs.sTxMailBox[0].TDHR = 0xFFFFFFFF;
    uint8_t data[3]                   = { 0x00, 0x10, 0x80 };

    ASSERT_EQ( HW_CAN_Transmit( &hcan1, data, 0x123, 3 ), 0 );

    EXPECT_EQ( mock_can1_regs.sTxMailBox[0].TDLR, 0x00801000 );
    EXPECT_EQ( mock_can1_regs.sTxMailBox[0].TDHR, 0x00000000 );
}

/** Verify that a long frame replaces stale bits in both mailbox payload registers. */
TEST_F( HWCANTest, TransmitClearsReusedMailboxForLongPayload )
{
    mock_can1_regs.TSR                = CAN_TSR_TME0;
    mock_can1_regs.sTxMailBox[0].TDLR = 0xFFFFFFFF;
    mock_can1_regs.sTxMailBox[0].TDHR = 0xFFFFFFFF;
    uint8_t data[6]                   = { 0x00, 0x01, 0x02, 0x03, 0x04, 0x00 };

    ASSERT_EQ( HW_CAN_Transmit( &hcan1, data, 0x123, 6 ), 0 );

    EXPECT_EQ( mock_can1_regs.sTxMailBox[0].TDLR, 0x03020100 );
    EXPECT_EQ( mock_can1_regs.sTxMailBox[0].TDHR, 0x00000004 );
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

/** Verify that standard identifier zero is accepted. */
TEST_F( HWCANTest, TransmitAcceptsZeroID )
{
    mock_can1_regs.TSR = CAN_TSR_TME0;
    uint8_t data[1]    = { 0xAA };

    ASSERT_EQ( HW_CAN_Transmit( &hcan1, data, 0x000, 1 ), 0 );
    EXPECT_EQ( mock_can1_regs.sTxMailBox[0].TIR, CAN_TI0R_TXRQ );
}

/** Verify a null payload is rejected before mailbox or channel state changes. */
TEST_F( HWCANTest, TransmitRejectsNullPayloadForNonzeroDLCWithoutSideEffects )
{
    mock_can1_regs.TSR = CAN_TSR_TME;
    memset( &mock_can1_regs.sTxMailBox[0], 0xA5, sizeof( mock_can1_regs.sTxMailBox[0] ) );
    CAN_TxMailBox_TypeDef original_mailbox = mock_can1_regs.sTxMailBox[0];
    uint32_t              original_tsr     = mock_can1_regs.TSR;
    can_tx_status1                         = HW_CAN_TX_STATUS_COMPLETE;

    EXPECT_EQ( HW_CAN_Transmit1( NULL, 0x123U, 1U ), HW_CAN_RESULT_ERROR );
    EXPECT_EQ(
        memcmp( &mock_can1_regs.sTxMailBox[0], &original_mailbox, sizeof( original_mailbox ) ), 0 );
    EXPECT_EQ( mock_can1_regs.TSR, original_tsr );
    EXPECT_EQ( HW_CAN_Tx_Status1(), HW_CAN_TX_STATUS_COMPLETE );
}

/** Verify a zero-DLC frame may use a null payload pointer. */
TEST_F( HWCANTest, TransmitAllowsNullPayloadForZeroDLC )
{
    mock_can2_regs.TSR                = CAN_TSR_TME;
    mock_can2_regs.sTxMailBox[0].TDLR = 0xFFFFFFFFU;
    mock_can2_regs.sTxMailBox[0].TDHR = 0xFFFFFFFFU;

    ASSERT_EQ( HW_CAN_Transmit2( NULL, 0x321U, 0U ), HW_CAN_RESULT_OK );
    EXPECT_EQ( mock_can2_regs.sTxMailBox[0].TIR,
               ( static_cast<uint32_t>( 0x321U ) << 21 ) | CAN_TI0R_TXRQ );
    EXPECT_EQ( mock_can2_regs.sTxMailBox[0].TDTR, 0U );
    EXPECT_EQ( mock_can2_regs.sTxMailBox[0].TDLR, 0U );
    EXPECT_EQ( mock_can2_regs.sTxMailBox[0].TDHR, 0U );
    EXPECT_EQ( HW_CAN_Tx_Status2(), HW_CAN_TX_STATUS_IDLE );
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
    mock_can1_regs.RF0R = 1U;

    uint16_t id = 0x456;

    mock_can1_regs.sFIFOMailBox[0].RIR = static_cast<uint32_t>( id ) << 21;

    mock_can1_regs.sFIFOMailBox[0].RDTR = 8;

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

    EXPECT_EQ( mock_can1_regs.RF0R & CAN_RF0R_FMP0, 0U );
    EXPECT_TRUE( mock_can1_regs.RF0R & CAN_RF0R_RFOM0 );
}

/** Verify that RX preserves DLC and clears bytes outside the received payload. */
TEST_F( HWCANTest, ReceiveExtractsDLCAndOnlyCopiesValidBytes )
{
    mock_can1_regs.RF0R                 = 1U;
    mock_can1_regs.sFIFOMailBox[0].RIR  = static_cast<uint32_t>( 0x321 ) << 21;
    mock_can1_regs.sFIFOMailBox[0].RDTR = 4;
    mock_can1_regs.sFIFOMailBox[0].RDLR = 0x04030201;
    mock_can1_regs.sFIFOMailBox[0].RDHR = 0xFFFFFFFF;
    CAN_Packet_T packet                 = {};
    memset( packet.data, 0xAA, sizeof( packet.data ) );

    ASSERT_EQ( HW_CAN_Receive( &hcan1, &packet ), 0 );
    EXPECT_EQ( packet.dlc, 4 );
    EXPECT_EQ( packet.data[0], 1 );
    EXPECT_EQ( packet.data[3], 4 );
    EXPECT_EQ( packet.data[4], 0 );
    EXPECT_EQ( packet.data[7], 0 );
}

/** Verify a null RX destination cannot consume or alter a pending FIFO entry. */
TEST_F( HWCANTest, ReceiveRejectsNullDestinationWithoutReleasingFIFO )
{
    mock_can1_regs.RF0R                   = 1U | CAN_RF0R_FULL0 | CAN_RF0R_FOVR0;
    mock_can1_regs.sFIFOMailBox[0].RIR    = static_cast<uint32_t>( 0x456U ) << 21;
    mock_can1_regs.sFIFOMailBox[0].RDTR   = 1U;
    mock_can1_regs.sFIFOMailBox[0].RDLR   = 0x5AU;
    CAN_FIFOMailBox_TypeDef original_fifo = mock_can1_regs.sFIFOMailBox[0];
    uint32_t                original_rf0r = mock_can1_regs.RF0R;

    EXPECT_NE( HW_CAN_Recieve1( NULL ), 0 );
    EXPECT_EQ( mock_can1_regs.RF0R, original_rf0r );
    EXPECT_EQ( memcmp( &mock_can1_regs.sFIFOMailBox[0], &original_fifo, sizeof( original_fifo ) ),
               0 );
    EXPECT_EQ( can_rx_wp1, 0U );
    EXPECT_EQ( can_rx_rp1, 0U );
    EXPECT_EQ( HW_CAN_Tx_Status1(), HW_CAN_TX_STATUS_IDLE );
}

/**-----------------------------------------------------------------------------
 *  Buffer Tests
 *------------------------------------------------------------------------------
 */

/** Verify that a packet written to the channel 1 TX buffer can be read back without changing its ID
 * or payload; reading is non-consuming. */
TEST_F( HWCANTest, TxBufferWriteAndReadPreservesIDAndData )
{
    CAN_Packet_T tx[1] = { { .id = 0x123, .dlc = 8, .data = { 1, 2, 3, 4, 5, 6, 7, 8 } } };

    EXPECT_EQ( HW_CAN_Tx_Buffer_Write1( tx, 1 ), 0 );

    CAN_Packet_T out[1] = {};

    uint16_t count = HW_CAN_Tx_Buffer_Read1( out );

    EXPECT_EQ( count, 1 );

    EXPECT_EQ( out[0].id, 0x123 );

    EXPECT_EQ( out[0].dlc, 8 );

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
    CAN_Packet_T tx[1] = { { .id = 0x7FF, .dlc = 8, .data = { 1, 2, 3, 4, 5, 6, 7, 8 } } };

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

/** Verify that a batch containing an invalid DLC is rejected without enqueuing its valid prefix. */
TEST_F( HWCANTest, TxBufferRejectsInvalidDLCBatchAtomically )
{
    CAN_Packet_T packets[2] = {
        { .id = 0x123, .dlc = 1, .data = { 0xAA } },
        { .id = 0x124, .dlc = 9, .data = {} },
    };

    EXPECT_EQ( HW_CAN_Tx_Buffer_Write1( packets, 2 ), 1 );
    EXPECT_EQ( can_tx_wp1, 0 );

    CAN_Packet_T out[1] = {};
    EXPECT_EQ( HW_CAN_Tx_Buffer_Read1( out ), 0 );
}

/** Verify that a batch containing an extended-range ID is rejected without changing the queue. */
TEST_F( HWCANTest, TxBufferRejectsInvalidIDBatchAtomically )
{
    CAN_Packet_T packets[2] = {
        { .id = 0x7FF, .dlc = 8, .data = {} },
        { .id = 0x800, .dlc = 8, .data = {} },
    };

    EXPECT_EQ( HW_CAN_Tx_Buffer_Write2( packets, 2 ), 1 );
    EXPECT_EQ( can_tx_wp2, 0 );

    CAN_Packet_T out[1] = {};
    EXPECT_EQ( HW_CAN_Tx_Buffer_Read2( out ), 0 );
}

/**-----------------------------------------------------------------------------
 *  TX ISR Tests
 *------------------------------------------------------------------------------
 */

/** Verify that a stray TX service event disables its interrupt without reporting completion. */
TEST_F( HWCANTest, TxIRQDisablesInterruptWhenBufferEmpty )
{
    ResetCANBuffers();
    mock_can1_regs.IER = CAN_IER_TMEIE;  // enable interrupt

    CAN1_TX_IRQHandler();

    // check it disabled the interrupt
    EXPECT_FALSE( mock_can1_regs.IER & CAN_IER_TMEIE );

    EXPECT_FALSE( HW_CAN_Channl1_sent() );
}

/** Verify that the channel 1 TX interrupt handler loads a buffered packet into the CAN TX mailbox
 * with the expected ID, payload, and DLC. */
TEST_F( HWCANTest, TxIRQTransmitsBufferedPacketWithCorrectIDAndData )
{
    mock_can1_regs.TSR = CAN_TSR_TME0;

    CAN_Packet_T packet[1] = { { .id = 0x321, .dlc = 8, .data = { 9, 8, 7, 6, 5, 4, 3, 2 } } };

    EXPECT_EQ( HW_CAN_Tx_Buffer_Write1( packet, 1 ), 0 );

    ASSERT_EQ( HW_CAN_Tx_Trigger1(), HW_CAN_RESULT_OK );

    EXPECT_EQ( mock_can1_regs.sTxMailBox[0].TIR,
               ( static_cast<uint32_t>( 0x321 ) << 21 ) | CAN_TI0R_TXRQ );

    EXPECT_EQ( mock_can1_regs.sTxMailBox[0].TDLR, 0x06070809 );

    EXPECT_EQ( mock_can1_regs.sTxMailBox[0].TDHR, 0x02030405 );

    EXPECT_EQ( mock_can1_regs.sTxMailBox[0].TDTR, 8 );
}

/** Verify that buffered transmission uses the packet DLC instead of always sending eight bytes. */
TEST_F( HWCANTest, TxIRQUsesBufferedPacketDLC )
{
    mock_can1_regs.TSR     = CAN_TSR_TME0;
    CAN_Packet_T packet[1] = { { .id = 0x321, .dlc = 4, .data = { 9, 8, 7, 6, 5, 4, 3, 2 } } };

    ASSERT_EQ( HW_CAN_Tx_Buffer_Write1( packet, 1 ), 0 );
    ASSERT_EQ( HW_CAN_Tx_Trigger1(), HW_CAN_RESULT_OK );

    EXPECT_EQ( mock_can1_regs.sTxMailBox[0].TDTR, 4 );
    EXPECT_EQ( mock_can1_regs.sTxMailBox[0].TDLR, 0x06070809 );
    EXPECT_EQ( mock_can1_regs.sTxMailBox[0].TDHR, 0U );
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

    mock_can1_regs.RF0R = 1U;

    mock_can1_regs.sFIFOMailBox[0].RIR = static_cast<uint32_t>( id ) << 21;

    mock_can1_regs.sFIFOMailBox[0].RDTR = 8;

    mock_can1_regs.sFIFOMailBox[0].RDLR = 0x04030201;

    mock_can1_regs.sFIFOMailBox[0].RDHR = 0x08070605;

    CAN1_RX0_IRQHandler();

    CAN_Packet_T out[1] = {};

    EXPECT_EQ( HW_CAN_Rx_Buffer_Read1( out, 1 ), 1 );

    EXPECT_EQ( out[0].id, id );

    EXPECT_EQ( out[0].data[0], 1 );

    EXPECT_EQ( out[0].data[7], 8 );
}

/** Verify that the actual RX0 vectors route pending frames to the matching software buffer. */
TEST_F( HWCANTest, RX0VectorsRouteBothChannels )
{
    mock_can1_regs.RF0R                 = 1U;
    mock_can1_regs.sFIFOMailBox[0].RIR  = static_cast<uint32_t>( 0x111 ) << 21;
    mock_can1_regs.sFIFOMailBox[0].RDTR = 8;
    mock_can1_regs.sFIFOMailBox[0].RDLR = 0x04030201;
    mock_can1_regs.sFIFOMailBox[0].RDHR = 0x08070605;
    mock_can2_regs.RF0R                 = 1U;
    mock_can2_regs.sFIFOMailBox[0].RIR  = static_cast<uint32_t>( 0x222 ) << 21;
    mock_can2_regs.sFIFOMailBox[0].RDTR = 8;
    mock_can2_regs.sFIFOMailBox[0].RDLR = 0x0C0B0A09;
    mock_can2_regs.sFIFOMailBox[0].RDHR = 0x100F0E0D;

    CAN1_RX0_IRQHandler();
    CAN2_RX0_IRQHandler();

    CAN_Packet_T channel1[1] = {};
    CAN_Packet_T channel2[1] = {};

    ASSERT_EQ( HW_CAN_Rx_Buffer_Read1( channel1, 1 ), 1 );
    ASSERT_EQ( HW_CAN_Rx_Buffer_Read2( channel2, 1 ), 1 );
    EXPECT_EQ( channel1[0].id, 0x111 );
    EXPECT_EQ( channel1[0].data[0], 1 );
    EXPECT_EQ( channel2[0].id, 0x222 );
    EXPECT_EQ( channel2[0].data[0], 9 );
}

/** Verify that the actual TX vectors advance multi-packet batches on both channels. */
TEST_F( HWCANTest, TXVectorsAdvanceBothChannelsAndMultiPacketSequence )
{
    mock_can1_regs.TSR = CAN_TSR_TME0;
    mock_can2_regs.TSR = CAN_TSR_TME0;

    CAN_Packet_T channel1[2] = {
        { .id = 0x101, .dlc = 8, .data = { 1, 2, 3, 4, 5, 6, 7, 8 } },
        { .id = 0x102, .dlc = 8, .data = { 9, 10, 11, 12, 13, 14, 15, 16 } },
    };
    CAN_Packet_T channel2[2] = {
        { .id = 0x201, .dlc = 8, .data = { 17, 18, 19, 20, 21, 22, 23, 24 } },
        { .id = 0x202, .dlc = 8, .data = { 25, 26, 27, 28, 29, 30, 31, 32 } },
    };

    ASSERT_EQ( HW_CAN_Tx_Buffer_Write1( channel1, 2 ), 0 );
    ASSERT_EQ( HW_CAN_Tx_Buffer_Write2( channel2, 2 ), 0 );

    HW_CAN_Tx_Trigger1();
    HW_CAN_Tx_Trigger2();

    EXPECT_EQ( mock_can1_regs.sTxMailBox[0].TIR,
               ( static_cast<uint32_t>( 0x101 ) << 21 ) | CAN_TI0R_TXRQ );
    EXPECT_EQ( mock_can2_regs.sTxMailBox[0].TIR,
               ( static_cast<uint32_t>( 0x201 ) << 21 ) | CAN_TI0R_TXRQ );

    mock_can1_regs.TSR = CAN_TSR_RQCP0 | CAN_TSR_TXOK0 | CAN_TSR_TME0;
    mock_can2_regs.TSR = CAN_TSR_RQCP0 | CAN_TSR_TXOK0 | CAN_TSR_TME0;
    CAN1_TX_IRQHandler();
    CAN2_TX_IRQHandler();

    EXPECT_EQ( mock_can1_regs.sTxMailBox[0].TIR,
               ( static_cast<uint32_t>( 0x102 ) << 21 ) | CAN_TI0R_TXRQ );
    EXPECT_EQ( mock_can2_regs.sTxMailBox[0].TIR,
               ( static_cast<uint32_t>( 0x202 ) << 21 ) | CAN_TI0R_TXRQ );
    EXPECT_FALSE( HW_CAN_Channl1_sent() );
    EXPECT_FALSE( HW_CAN_Channl2_sent() );

    mock_can1_regs.TSR = CAN_TSR_RQCP0 | CAN_TSR_TXOK0 | CAN_TSR_TME0;
    mock_can2_regs.TSR = CAN_TSR_RQCP0 | CAN_TSR_TXOK0 | CAN_TSR_TME0;
    CAN1_TX_IRQHandler();
    CAN2_TX_IRQHandler();

    EXPECT_TRUE( HW_CAN_Channl1_sent() );
    EXPECT_TRUE( HW_CAN_Channl2_sent() );
}

/** Verify that clearing mailbox 0 completion leaves unrelated mailbox 1 status untouched. */
TEST_F( HWCANTest, TXVectorClearsOnlyCompletedMailboxStatus )
{
    mock_can1_regs.TSR     = CAN_TSR_TME0;
    CAN_Packet_T packet[1] = { { .id = 0x123, .dlc = 1, .data = { 0xAA } } };
    ASSERT_EQ( HW_CAN_Tx_Buffer_Write1( packet, 1 ), HW_CAN_RESULT_OK );
    ASSERT_EQ( HW_CAN_Tx_Trigger1(), HW_CAN_RESULT_OK );

    uint32_t unrelated_status = CAN_TSR_TXOK1 | CAN_TSR_ALST1 | CAN_TSR_TERR1;
    mock_can1_regs.TSR        = CAN_TSR_RQCP0 | CAN_TSR_TXOK0 | CAN_TSR_TME0 | unrelated_status;
    CAN1_TX_IRQHandler();

    EXPECT_EQ( mock_can1_regs.TSR & ( CAN_TSR_RQCP0 | CAN_TSR_TXOK0 ), 0U );
    EXPECT_EQ( mock_can1_regs.TSR & unrelated_status, unrelated_status );
    EXPECT_TRUE( HW_CAN_Channl1_sent() );
}

/** Verify that arbitration loss and transmit errors cannot complete a buffered batch. */
TEST_F( HWCANTest, TXVectorDoesNotCompleteBatchAfterHardwareError )
{
    uint32_t error_flags[2] = { CAN_TSR_ALST0, CAN_TSR_TERR0 };

    for ( uint32_t error_flag : error_flags )
    {
        ResetCANBuffers();
        memset( &mock_can1_regs, 0, sizeof( mock_can1_regs ) );
        mock_can1_regs.TSR     = CAN_TSR_TME0;
        CAN_Packet_T packet[1] = { { .id = 0x123, .dlc = 1, .data = { 0xAA } } };
        ASSERT_EQ( HW_CAN_Tx_Buffer_Write1( packet, 1 ), HW_CAN_RESULT_OK );
        ASSERT_EQ( HW_CAN_Tx_Trigger1(), HW_CAN_RESULT_OK );

        mock_can1_regs.TSR = CAN_TSR_RQCP0 | error_flag | CAN_TSR_TME0;
        CAN1_TX_IRQHandler();

        EXPECT_FALSE( can_tx_active1 );
        EXPECT_FALSE( HW_CAN_Channl1_sent() );
        EXPECT_EQ( HW_CAN_Tx_Status1(), HW_CAN_TX_STATUS_ERROR );
        EXPECT_FALSE( mock_can1_regs.IER & CAN_IER_TMEIE );
        EXPECT_EQ( mock_can1_regs.TSR & ( CAN_TSR_RQCP0 | error_flag ), 0U );
    }
}

/** Verify that an RX vector records a frame dropped by a full software buffer. */
TEST_F( HWCANTest, RxOverflowRecordsDroppedFrame )
{
    CAN_Packet_T buffered[RECEIVE_BUFFER_WIDTH - 1] = {};
    for ( CAN_Packet_T& packet : buffered )
    {
        packet.id  = 0x123;
        packet.dlc = 1;
    }
    ASSERT_EQ( HW_CAN_Rx_Buffer_Write1( buffered, RECEIVE_BUFFER_WIDTH - 1 ), 0 );

    mock_can1_regs.RF0R                 = 1U;
    mock_can1_regs.sFIFOMailBox[0].RIR  = static_cast<uint32_t>( 0x321 ) << 21;
    mock_can1_regs.sFIFOMailBox[0].RDTR = 1;
    mock_can1_regs.sFIFOMailBox[0].RDLR = 0xAA;

    CAN1_RX0_IRQHandler();

    EXPECT_EQ( HW_CAN_Rx_Dropped_Count1(), 1U );
    EXPECT_EQ( can_rx_wp1, RECEIVE_BUFFER_WIDTH - 1 );
}

/** Verify that an RX0 vector releases FIFO entries and clears a latched hardware overrun. */
TEST_F( HWCANTest, RX0VectorReleasesFIFOAndClearsHardwareOverrun )
{
    mock_can2_regs.RF0R                 = 1U | CAN_RF0R_FULL0 | CAN_RF0R_FOVR0;
    mock_can2_regs.sFIFOMailBox[0].RIR  = static_cast<uint32_t>( 0x456 ) << 21;
    mock_can2_regs.sFIFOMailBox[0].RDTR = 1U;
    mock_can2_regs.sFIFOMailBox[0].RDLR = 0x5AU;

    CAN2_RX0_IRQHandler();

    EXPECT_EQ( mock_can2_regs.RF0R & CAN_RF0R_FMP0, 0U );
    EXPECT_EQ( mock_can2_regs.RF0R & ( CAN_RF0R_FULL0 | CAN_RF0R_FOVR0 ), 0U );
    EXPECT_TRUE( mock_can2_regs.RF0R & CAN_RF0R_RFOM0 );
    EXPECT_EQ( HW_CAN_Rx_Dropped_Count2(), 1U );

    CAN_Packet_T packet = {};
    ASSERT_EQ( HW_CAN_Rx_Buffer_Read2( &packet, 1U ), 1U );
    EXPECT_EQ( packet.id, 0x456U );
    EXPECT_EQ( packet.data[0], 0x5AU );
}

/** Verify that resetting channel 1 clears only channel 1 queue and status state. */
TEST_F( HWCANTest, ChannelResetClearsSelectedChannelOnly )
{
    CAN_Packet_T channel1[1] = { { .id = 0x111, .dlc = 1, .data = { 1 } } };
    CAN_Packet_T channel2[1] = { { .id = 0x222, .dlc = 1, .data = { 2 } } };
    ASSERT_EQ( HW_CAN_Tx_Buffer_Write1( channel1, 1 ), HW_CAN_RESULT_OK );
    ASSERT_EQ( HW_CAN_Rx_Buffer_Write1( channel1, 1 ), 0 );
    ASSERT_EQ( HW_CAN_Tx_Buffer_Write2( channel2, 1 ), HW_CAN_RESULT_OK );
    ASSERT_EQ( HW_CAN_Rx_Buffer_Write2( channel2, 1 ), 0 );

    can_tx_active1                 = true;
    can_sent_flag1                 = true;
    can_rx_dropped_count1          = 3;
    can_tx_active2                 = true;
    can_sent_flag2                 = true;
    can_rx_dropped_count2          = 4;
    mock_can1_regs.IER             = CAN_IER_TMEIE;
    mock_can2_regs.IER             = CAN_IER_TMEIE;
    nvic_irq_enabled[CAN1_TX_IRQn] = false;

    HW_CAN_Reset1();

    EXPECT_EQ( can_tx_wp1, 0 );
    EXPECT_EQ( can_tx_rp1, 0 );
    EXPECT_EQ( can_rx_wp1, 0 );
    EXPECT_EQ( can_rx_rp1, 0 );
    EXPECT_FALSE( can_tx_active1 );
    EXPECT_FALSE( can_sent_flag1 );
    EXPECT_EQ( HW_CAN_Rx_Dropped_Count1(), 0U );
    EXPECT_FALSE( mock_can1_regs.IER & CAN_IER_TMEIE );
    EXPECT_FALSE( nvic_irq_enabled[CAN1_TX_IRQn] );
    EXPECT_TRUE( nvic_irq_enabled[CAN1_RX0_IRQn] );

    EXPECT_EQ( can_tx_wp2, 1 );
    EXPECT_EQ( can_rx_wp2, 1 );
    EXPECT_TRUE( can_tx_active2 );
    EXPECT_TRUE( can_sent_flag2 );
    EXPECT_EQ( HW_CAN_Rx_Dropped_Count2(), 4U );
    EXPECT_TRUE( mock_can2_regs.IER & CAN_IER_TMEIE );
}

/** Verify that resetting a channel clears its sticky RX overflow diagnostic. */
TEST_F( HWCANTest, ChannelResetClearsRxOverflowDiagnostic )
{
    can_rx_dropped_count2 = 7;

    HW_CAN_Reset2();

    EXPECT_EQ( HW_CAN_Rx_Dropped_Count2(), 0U );
}

/**-----------------------------------------------------------------------------
 *  Full Buffer Tests
 *------------------------------------------------------------------------------
 */

/** Verify that the TX ring buffer rejects a write once its usable capacity is exhausted. */
TEST_F( HWCANTest, TxBufferWriteFailsWhenFull )
{
    CAN_Packet_T packet[1] = { { .id = 0x123, .dlc = 8, .data = { 1, 2, 3, 4, 5, 6, 7, 8 } } };

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
    CAN_Packet_T packet[1] = { { .id = 0x456, .dlc = 8, .data = { 9, 8, 7, 6, 5, 4, 3, 2 } } };

    for ( int i = 0; i < RECEIVE_BUFFER_WIDTH - 1; i++ )
    {
        EXPECT_EQ( HW_CAN_Rx_Buffer_Write1( packet, 1 ), 0 );
    }

    EXPECT_EQ( HW_CAN_Rx_Buffer_Write1( packet, 1 ), 1 );
}

/** Verify that bounded RX reads preserve order across partial reads and ring wraparound. */
TEST_F( HWCANTest, RxBufferBoundedReadsPreserveRemainingPacketsAcrossWraparound )
{
    CAN_Packet_T packets[15] = {};
    for ( uint16_t i = 0; i < 15U; i++ )
    {
        packets[i].id      = ( uint16_t )( 0x100U + i );
        packets[i].dlc     = 1;
        packets[i].data[0] = ( uint8_t )i;
    }
    ASSERT_EQ( HW_CAN_Rx_Buffer_Write1( packets, 15 ), 0 );

    CAN_Packet_T first_read[10] = {};
    ASSERT_EQ( HW_CAN_Rx_Buffer_Read1( first_read, 10 ), 10 );
    for ( uint16_t i = 0; i < 10U; i++ )
    {
        EXPECT_EQ( first_read[i].id, 0x100U + i );
    }

    CAN_Packet_T wrapped[10] = {};
    for ( uint16_t i = 0; i < 10U; i++ )
    {
        wrapped[i].id      = ( uint16_t )( 0x10FU + i );
        wrapped[i].dlc     = 1;
        wrapped[i].data[0] = ( uint8_t )( 15U + i );
    }
    ASSERT_EQ( HW_CAN_Rx_Buffer_Write1( wrapped, 10 ), 0 );

    CAN_Packet_T second_read[7] = {};
    ASSERT_EQ( HW_CAN_Rx_Buffer_Read1( second_read, 7 ), 7 );
    for ( uint16_t i = 0; i < 7U; i++ )
    {
        EXPECT_EQ( second_read[i].id, 0x10AU + i );
    }

    CAN_Packet_T remaining[8] = {};
    ASSERT_EQ( HW_CAN_Rx_Buffer_Read1( remaining, 8 ), 8 );
    for ( uint16_t i = 0; i < 8U; i++ )
    {
        EXPECT_EQ( remaining[i].id, 0x111U + i );
    }
    EXPECT_EQ( can_rx_rp1, can_rx_wp1 );
}

/** Verify that zero capacity and a null destination do not consume RX packets. */
TEST_F( HWCANTest, RxBufferRejectsInvalidDestinationWithoutConsuming )
{
    CAN_Packet_T packet[1] = { { .id = 0x123, .dlc = 1, .data = { 0xAA } } };
    ASSERT_EQ( HW_CAN_Rx_Buffer_Write1( packet, 1 ), 0 );

    CAN_Packet_T out[1] = {};
    EXPECT_EQ( HW_CAN_Rx_Buffer_Read1( out, 0 ), 0 );
    EXPECT_EQ( can_rx_rp1, 0 );
    EXPECT_EQ( HW_CAN_Rx_Buffer_Read1( NULL, 1 ), 0 );
    EXPECT_EQ( can_rx_rp1, 0 );

    ASSERT_EQ( HW_CAN_Rx_Buffer_Read1( out, 1 ), 1 );
    EXPECT_EQ( out[0].id, 0x123 );
}

/** Verify that the public channel RX pop wrappers remove one packet from the selected channel. */
TEST_F( HWCANTest, RxBufferPopWrappersConsumeSelectedChannel )
{
    CAN_Packet_T channel1[1] = { { .id = 0x111, .dlc = 1, .data = { 1 } } };
    CAN_Packet_T channel2[1] = { { .id = 0x222, .dlc = 1, .data = { 2 } } };
    ASSERT_EQ( HW_CAN_Rx_Buffer_Write1( channel1, 1 ), 0 );
    ASSERT_EQ( HW_CAN_Rx_Buffer_Write2( channel2, 1 ), 0 );

    CAN_Packet_T out = {};
    EXPECT_EQ( HW_CAN_Rx_Buffer_Pop1( &out ), 0 );
    EXPECT_EQ( out.id, 0x111 );
    EXPECT_EQ( HW_CAN_Rx_Buffer_Pop1( &out ), 1 );
    EXPECT_EQ( HW_CAN_Rx_Buffer_Pop2( &out ), 0 );
    EXPECT_EQ( out.id, 0x222 );
    EXPECT_EQ( HW_CAN_Rx_Buffer_Pop2( NULL ), 1 );
}

/** Verify that TX ring-buffer wraparound preserves FIFO ordering after packets are consumed and new
 * packets are written. */
TEST_F( HWCANTest, TxBufferWraparoundPreservesPackets )
{
    CAN_Packet_T packet[1] = {};
    packet[0].dlc          = 8;

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

    EXPECT_EQ( filter.FilterIdLow, 0U );

    EXPECT_EQ( filter.FilterMaskIdLow, CAN_RI0R_IDE | CAN_RI0R_RTR );

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

/** Verify inexact timing is rejected before HAL or channel state is touched. */
TEST_F( HWCANTest, ConfigureRejectsInexactTimingWithoutHardwareOrStateChanges )
{
    mock_can1_regs.IER = 0xA5A5U;
    can_tx_status1     = HW_CAN_TX_STATUS_COMPLETE;
    EXPECT_CALL( mock, CANInit( _ ) ).Times( 0 );
    EXPECT_CALL( mock, CANConfigFilter( _, _ ) ).Times( 0 );
    EXPECT_CALL( mock, CANStart( _ ) ).Times( 0 );

    EXPECT_EQ( HW_CAN_Configure( &hcan1, 800000U, 0U, 0x123U, 0x7FFU ), 1 );
    EXPECT_EQ( mock_can1_regs.IER, 0xA5A5U );
    EXPECT_EQ( HW_CAN_Tx_Status1(), HW_CAN_TX_STATUS_COMPLETE );
    EXPECT_EQ( can_tx_wp1, 0U );
    EXPECT_EQ( can_tx_rp1, 0U );
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

/** Verify that HW_CAN_Configure() completes successfully when each HAL configuration step succeeds.
 */
TEST_F( HWCANTest, ConfigureSucceedsWithValidConfiguration )
{
    EXPECT_CALL( mock, CANInit( _ ) ).WillOnce( Return( HAL_OK ) );

    EXPECT_CALL( mock, CANConfigFilter( _, _ ) ).WillOnce( Return( HAL_OK ) );

    EXPECT_CALL( mock, CANStart( _ ) ).WillOnce( Return( HAL_OK ) );

    int result = HW_CAN_Configure( &hcan1, 1000000, 0, 0x123, 0x7FF );

    EXPECT_EQ( result, 0 );
    EXPECT_EQ( mock_can1_regs.IER & ( CAN_IER_FMPIE0 | CAN_IER_FFIE0 | CAN_IER_FOVIE0 ),
               CAN_IER_FMPIE0 | CAN_IER_FFIE0 | CAN_IER_FOVIE0 );
    EXPECT_EQ( mock_can1_regs.IER & HW_CAN_ERROR_INTERRUPT_MASK, HW_CAN_ERROR_INTERRUPT_MASK );
    EXPECT_FALSE( mock_can1_regs.IER & CAN_IER_TMEIE );
}

/** Verify that reconfiguring one channel starts with deterministic empty software state. */
TEST_F( HWCANTest, ChannelConfigurationResetsSoftwareState )
{
    CAN_Packet_T packet[1] = { { .id = 0x123, .dlc = 1, .data = { 0xAA } } };
    ASSERT_EQ( HW_CAN_Tx_Buffer_Write1( packet, 1 ), HW_CAN_RESULT_OK );
    ASSERT_EQ( HW_CAN_Rx_Buffer_Write1( packet, 1 ), 0 );
    can_tx_active1        = true;
    can_sent_flag1        = true;
    can_rx_dropped_count1 = 2;
    mock_can1_regs.IER    = CAN_IER_TMEIE;

    EXPECT_CALL( mock, CANInit( &hcan1 ) ).WillOnce( Return( HAL_OK ) );
    EXPECT_CALL( mock, CANConfigFilter( &hcan1, _ ) ).WillOnce( Return( HAL_OK ) );
    EXPECT_CALL( mock, CANStart( &hcan1 ) ).WillOnce( Return( HAL_OK ) );
    ASSERT_EQ( HW_CAN_Configure1( 1000000, 0, 0x123, 0x7FF ), 0 );

    EXPECT_EQ( can_tx_wp1, 0 );
    EXPECT_EQ( can_tx_rp1, 0 );
    EXPECT_EQ( can_rx_wp1, 0 );
    EXPECT_EQ( can_rx_rp1, 0 );
    EXPECT_FALSE( can_tx_active1 );
    EXPECT_FALSE( can_sent_flag1 );
    EXPECT_EQ( HW_CAN_Tx_Status1(), HW_CAN_TX_STATUS_IDLE );
    EXPECT_EQ( HW_CAN_Rx_Dropped_Count1(), 0U );
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
    EXPECT_EQ( HW_CAN_Tx_Status1(), HW_CAN_TX_STATUS_IDLE );
    EXPECT_EQ( HW_CAN_Tx_Status2(), HW_CAN_TX_STATUS_IDLE );
}

/** Verify that the direct SCE vector records and acknowledges a controller error. */
TEST_F( HWCANTest, SCEVectorChangesActiveBatchToErrorAndClearsFlags )
{
    mock_can1_regs.TSR     = CAN_TSR_TME0;
    CAN_Packet_T packet[1] = { { .id = 0x123, .dlc = 1, .data = { 0xAA } } };
    ASSERT_EQ( HW_CAN_Tx_Buffer_Write1( packet, 1 ), HW_CAN_RESULT_OK );
    ASSERT_EQ( HW_CAN_Tx_Trigger1(), HW_CAN_RESULT_OK );

    mock_can1_regs.MSR = CAN_MSR_ERRI;
    mock_can1_regs.ESR = CAN_ESR_BOFF | CAN_ESR_LEC_0;
    CAN1_SCE_IRQHandler();

    EXPECT_EQ( HW_CAN_Tx_Status1(), HW_CAN_TX_STATUS_ERROR );
    EXPECT_FALSE( can_tx_active1 );
    EXPECT_FALSE( HW_CAN_Channl1_sent() );
    EXPECT_EQ( mock_can1_regs.MSR & CAN_MSR_ERRI, 0U );
    EXPECT_EQ( mock_can1_regs.ESR & CAN_ESR_LEC, 0U );
    EXPECT_FALSE( mock_can1_regs.IER & CAN_IER_TMEIE );
}

/** Verify that a bus-off status interrupt puts the affected channel in error. */
TEST_F( HWCANTest, SCEVectorRecordsBusOffAsError )
{
    mock_can2_regs.MSR = CAN_MSR_ERRI;
    mock_can2_regs.ESR = CAN_ESR_BOFF;

    CAN2_SCE_IRQHandler();

    EXPECT_EQ( HW_CAN_Tx_Status2(), HW_CAN_TX_STATUS_ERROR );
    EXPECT_FALSE( HW_CAN_Channl2_sent() );
    EXPECT_EQ( mock_can2_regs.MSR & CAN_MSR_ERRI, 0U );
}

/** Verify that task-context recovery clears pending work and permits a later batch. */
TEST_F( HWCANTest, RecoveryClearsFailedWorkAndAllowsSubsequentTransmission )
{
    mock_can1_regs.TSR      = CAN_TSR_TME0;
    CAN_Packet_T packets[2] = {
        { .id = 0x123, .dlc = 1, .data = { 0xAA } },
        { .id = 0x124, .dlc = 1, .data = { 0xBB } },
    };
    ASSERT_EQ( HW_CAN_Tx_Buffer_Write1( packets, 2 ), HW_CAN_RESULT_OK );
    ASSERT_EQ( HW_CAN_Tx_Trigger1(), HW_CAN_RESULT_OK );
    ASSERT_NE( mock_can1_regs.sTxMailBox[0].TIR & CAN_TI0R_TXRQ, 0U );

    mock_can1_regs.MSR = CAN_MSR_ERRI;
    mock_can1_regs.ESR = CAN_ESR_BOFF | CAN_ESR_LEC_0;
    CAN1_SCE_IRQHandler();
    ASSERT_EQ( HW_CAN_Tx_Status1(), HW_CAN_TX_STATUS_ERROR );

    EXPECT_CALL( mock, CANStop( &hcan1 ) ).WillOnce( Return( HAL_OK ) );
    EXPECT_CALL( mock, CANStart( &hcan1 ) ).WillOnce( Return( HAL_OK ) );
    ASSERT_EQ( HW_CAN_Recover1(), HW_CAN_RESULT_OK );

    EXPECT_EQ( HW_CAN_Tx_Status1(), HW_CAN_TX_STATUS_IDLE );
    EXPECT_FALSE( HW_CAN_Channl1_sent() );
    EXPECT_EQ( can_tx_wp1, 0U );
    EXPECT_EQ( can_tx_rp1, 0U );
    EXPECT_EQ( can_tx_pending_mailbox1, 0U );
    EXPECT_EQ( mock_can1_regs.sTxMailBox[0].TIR & CAN_TI0R_TXRQ, 0U );
    EXPECT_EQ( mock_can1_regs.TSR & CAN_TSR_TME, CAN_TSR_TME );
    EXPECT_EQ( mock_can1_regs.IER & HW_CAN_RX_INTERRUPT_MASK, HW_CAN_RX_INTERRUPT_MASK );
    EXPECT_EQ( mock_can1_regs.IER & HW_CAN_ERROR_INTERRUPT_MASK, HW_CAN_ERROR_INTERRUPT_MASK );
    EXPECT_TRUE( nvic_irq_enabled[CAN1_TX_IRQn] );
    EXPECT_TRUE( nvic_irq_enabled[CAN1_RX0_IRQn] );
    EXPECT_TRUE( nvic_irq_enabled[CAN1_SCE_IRQn] );

    CAN_Packet_T retry[1] = { { .id = 0x321, .dlc = 1, .data = { 0x5A } } };
    ASSERT_EQ( HW_CAN_Tx_Buffer_Write1( retry, 1 ), HW_CAN_RESULT_OK );
    ASSERT_EQ( HW_CAN_Tx_Trigger1(), HW_CAN_RESULT_OK );
    EXPECT_EQ( HW_CAN_Tx_Status1(), HW_CAN_TX_STATUS_ACTIVE );
    mock_can1_regs.TSR = CAN_TSR_RQCP0 | CAN_TSR_TXOK0 | CAN_TSR_TME;
    CAN1_TX_IRQHandler();
    EXPECT_EQ( HW_CAN_Tx_Status1(), HW_CAN_TX_STATUS_COMPLETE );
    EXPECT_TRUE( HW_CAN_Channl1_sent() );
}

/** Verify that a failed stop keeps the channel in error and reports recovery failure. */
TEST_F( HWCANTest, RecoveryFailureDoesNotReportIdleOrComplete )
{
    can_tx_status2 = HW_CAN_TX_STATUS_ERROR;
    EXPECT_CALL( mock, CANStop( &hcan2 ) ).WillOnce( Return( HAL_ERROR ) );
    EXPECT_CALL( mock, CANStart( &hcan2 ) ).Times( 0 );

    EXPECT_EQ( HW_CAN_Recover2(), HW_CAN_RESULT_ERROR );
    EXPECT_EQ( HW_CAN_Tx_Status2(), HW_CAN_TX_STATUS_ERROR );
    EXPECT_FALSE( HW_CAN_Channl2_sent() );
}

/** Verify that direct transmission cannot overwrite an active buffered batch. */
TEST_F( HWCANTest, DirectTransmitReturnsBusyDuringBufferedBatch )
{
    mock_can1_regs.TSR     = CAN_TSR_TME0;
    CAN_Packet_T packet[1] = { { .id = 0x123, .dlc = 1, .data = { 0xAA } } };
    ASSERT_EQ( HW_CAN_Tx_Buffer_Write1( packet, 1 ), HW_CAN_RESULT_OK );
    ASSERT_EQ( HW_CAN_Tx_Trigger1(), HW_CAN_RESULT_OK );
    uint32_t mailbox_id = mock_can1_regs.sTxMailBox[0].TIR;
    uint8_t  direct[1]  = { 0x55 };

    EXPECT_EQ( HW_CAN_Transmit1( direct, 0x456, 1 ), HW_CAN_RESULT_BUSY );
    EXPECT_EQ( mock_can1_regs.sTxMailBox[0].TIR, mailbox_id );
    EXPECT_EQ( HW_CAN_Tx_Status1(), HW_CAN_TX_STATUS_ACTIVE );
}

/** Verify that direct hardware work makes buffered trigger busy without consuming its queue. */
TEST_F( HWCANTest, BufferedTriggerRetainsQueueWhileDirectTransmitIsPending )
{
    mock_can2_regs.TSR = CAN_TSR_TME;
    uint8_t direct[1]  = { 0x55 };
    ASSERT_EQ( HW_CAN_Transmit2( direct, 0x456, 1 ), HW_CAN_RESULT_OK );

    CAN_Packet_T packet[1] = { { .id = 0x222, .dlc = 1, .data = { 0xAA } } };
    ASSERT_EQ( HW_CAN_Tx_Buffer_Write2( packet, 1 ), HW_CAN_RESULT_OK );
    EXPECT_EQ( HW_CAN_Tx_Trigger2(), HW_CAN_RESULT_BUSY );
    EXPECT_EQ( can_tx_rp2, 0U );
    EXPECT_EQ( can_tx_wp2, 1U );
    EXPECT_EQ( HW_CAN_Tx_Status2(), HW_CAN_TX_STATUS_IDLE );
    EXPECT_FALSE( mock_can2_regs.IER & CAN_IER_TMEIE );

    CAN_Packet_T retained[1] = {};
    ASSERT_EQ( HW_CAN_Tx_Buffer_Read2( retained ), 1U );
    EXPECT_EQ( retained[0].id, 0x222U );
}

/** Verify that a completed direct request cannot complete a later buffered batch. */
TEST_F( HWCANTest, BufferedTriggerClearsStaleDirectCompletionStatus )
{
    mock_can2_regs.TSR = CAN_TSR_TME;
    uint8_t direct[1]  = { 0x55 };
    ASSERT_EQ( HW_CAN_Transmit2( direct, 0x456, 1 ), HW_CAN_RESULT_OK );

    CLEAR_BIT( mock_can2_regs.sTxMailBox[0].TIR, CAN_TI0R_TXRQ );
    mock_can2_regs.TSR = CAN_TSR_RQCP0 | CAN_TSR_TXOK0 | CAN_TSR_TME;

    CAN_Packet_T packet[1] = { { .id = 0x222, .dlc = 1, .data = { 0xAA } } };
    ASSERT_EQ( HW_CAN_Tx_Buffer_Write2( packet, 1 ), HW_CAN_RESULT_OK );
    ASSERT_EQ( HW_CAN_Tx_Trigger2(), HW_CAN_RESULT_OK );

    EXPECT_EQ( mock_can2_regs.TSR & ( CAN_TSR_RQCP0 | CAN_TSR_TXOK0 ), 0U );
    EXPECT_EQ( HW_CAN_Tx_Status2(), HW_CAN_TX_STATUS_ACTIVE );
    EXPECT_FALSE( HW_CAN_Channl2_sent() );

    CAN2_TX_IRQHandler();
    EXPECT_EQ( HW_CAN_Tx_Status2(), HW_CAN_TX_STATUS_ACTIVE );
    EXPECT_FALSE( HW_CAN_Channl2_sent() );

    mock_can2_regs.TSR = CAN_TSR_RQCP0 | CAN_TSR_TXOK0 | CAN_TSR_TME;
    CAN2_TX_IRQHandler();
    EXPECT_EQ( HW_CAN_Tx_Status2(), HW_CAN_TX_STATUS_COMPLETE );
    EXPECT_TRUE( HW_CAN_Channl2_sent() );
}

/** Verify that one batch becomes active on trigger and complete only after hardware completion. */
TEST_F( HWCANTest, BatchTransitionsFromActiveToCompleted )
{
    mock_can1_regs.TSR     = CAN_TSR_TME0;
    CAN_Packet_T packet[1] = { { .id = 0x123, .dlc = 1, .data = { 0xAA } } };

    ASSERT_EQ( HW_CAN_Tx_Buffer_Write1( packet, 1 ), HW_CAN_RESULT_OK );
    ASSERT_EQ( HW_CAN_Tx_Trigger1(), HW_CAN_RESULT_OK );
    EXPECT_TRUE( can_tx_active1 );
    EXPECT_FALSE( HW_CAN_Channl1_sent() );
    EXPECT_EQ( HW_CAN_Tx_Status1(), HW_CAN_TX_STATUS_ACTIVE );

    mock_can1_regs.TSR = CAN_TSR_RQCP0 | CAN_TSR_TXOK0 | CAN_TSR_TME0;
    CAN1_TX_IRQHandler();

    EXPECT_FALSE( can_tx_active1 );
    EXPECT_TRUE( HW_CAN_Channl1_sent() );
    EXPECT_EQ( HW_CAN_Tx_Status1(), HW_CAN_TX_STATUS_COMPLETE );
    EXPECT_FALSE( mock_can1_regs.IER & CAN_IER_TMEIE );
}

/** Verify that starting a second batch clears the previous completion result. */
TEST_F( HWCANTest, StartingSecondBatchClearsCompletedState )
{
    mock_can1_regs.TSR     = CAN_TSR_TME0;
    CAN_Packet_T packet[1] = { { .id = 0x123, .dlc = 1, .data = { 0xAA } } };

    ASSERT_EQ( HW_CAN_Tx_Buffer_Write1( packet, 1 ), HW_CAN_RESULT_OK );
    ASSERT_EQ( HW_CAN_Tx_Trigger1(), HW_CAN_RESULT_OK );
    mock_can1_regs.TSR = CAN_TSR_RQCP0 | CAN_TSR_TXOK0 | CAN_TSR_TME0;
    CAN1_TX_IRQHandler();
    ASSERT_TRUE( HW_CAN_Channl1_sent() );

    ASSERT_EQ( HW_CAN_Tx_Buffer_Write1( packet, 1 ), HW_CAN_RESULT_OK );
    ASSERT_EQ( HW_CAN_Tx_Trigger1(), HW_CAN_RESULT_OK );

    EXPECT_TRUE( can_tx_active1 );
    EXPECT_FALSE( HW_CAN_Channl1_sent() );
}

/** Verify that retriggering an active batch returns busy without changing its queue state. */
TEST_F( HWCANTest, TriggerWhileActiveReturnsBusy )
{
    mock_can1_regs.TSR      = CAN_TSR_TME0;
    CAN_Packet_T packets[2] = {
        { .id = 0x123, .dlc = 1, .data = { 0xAA } },
        { .id = 0x124, .dlc = 1, .data = { 0xBB } },
    };

    ASSERT_EQ( HW_CAN_Tx_Buffer_Write1( packets, 2 ), HW_CAN_RESULT_OK );
    ASSERT_EQ( HW_CAN_Tx_Trigger1(), HW_CAN_RESULT_OK );
    uint16_t read_position = can_tx_rp1;

    EXPECT_EQ( HW_CAN_Tx_Trigger1(), HW_CAN_RESULT_BUSY );
    EXPECT_EQ( can_tx_rp1, read_position );
    EXPECT_TRUE( can_tx_active1 );
    EXPECT_FALSE( HW_CAN_Channl1_sent() );
}

/** Verify that loading a new batch while transmission is active returns busy. */
TEST_F( HWCANTest, LoadWhileActiveReturnsBusy )
{
    mock_can1_regs.TSR     = CAN_TSR_TME0;
    CAN_Packet_T packet[1] = { { .id = 0x123, .dlc = 1, .data = { 0xAA } } };

    ASSERT_EQ( HW_CAN_Tx_Buffer_Write1( packet, 1 ), HW_CAN_RESULT_OK );
    ASSERT_EQ( HW_CAN_Tx_Trigger1(), HW_CAN_RESULT_OK );
    uint16_t write_position = can_tx_wp1;

    EXPECT_EQ( HW_CAN_Tx_Buffer_Write1( packet, 1 ), HW_CAN_RESULT_BUSY );
    EXPECT_EQ( can_tx_wp1, write_position );
    EXPECT_TRUE( can_tx_active1 );
}

/** Verify that an empty trigger reports empty without creating a false operation. */
TEST_F( HWCANTest, EmptyTriggerReturnsEmpty )
{
    EXPECT_EQ( HW_CAN_Tx_Trigger1(), HW_CAN_RESULT_EMPTY );
    EXPECT_FALSE( can_tx_active1 );
    EXPECT_FALSE( HW_CAN_Channl1_sent() );
    EXPECT_FALSE( mock_can1_regs.IER & CAN_IER_TMEIE );
}

/** Verify that a busy mailbox leaves the next packet queued for a later service event. */
TEST_F( HWCANTest, MailboxBusyDoesNotConsumeQueuedPacket )
{
    CAN_Packet_T packet[1] = { { .id = 0x123, .dlc = 1, .data = { 0xAA } } };
    ASSERT_EQ( HW_CAN_Tx_Buffer_Write1( packet, 1 ), HW_CAN_RESULT_OK );
    mock_can1_regs.TSR = 0;

    ASSERT_EQ( HW_CAN_Tx_Trigger1(), HW_CAN_RESULT_OK );
    EXPECT_EQ( can_tx_rp1, 0 );
    EXPECT_TRUE( can_tx_active1 );
    EXPECT_FALSE( HW_CAN_Channl1_sent() );

    mock_can1_regs.TSR = CAN_TSR_RQCP0 | CAN_TSR_TXOK0 | CAN_TSR_TME0;
    CAN1_TX_IRQHandler();

    EXPECT_EQ( can_tx_rp1, 1 );
    EXPECT_TRUE( can_tx_active1 );
    EXPECT_FALSE( HW_CAN_Channl1_sent() );

    mock_can1_regs.TSR = CAN_TSR_RQCP0 | CAN_TSR_TXOK0 | CAN_TSR_TME0;
    CAN1_TX_IRQHandler();
    EXPECT_TRUE( HW_CAN_Channl1_sent() );
}

/** Verify that an unexpected invalid queued frame is retained and never reported complete. */
TEST_F( HWCANTest, InvalidQueuedFrameIsNotSilentlyDiscarded )
{
    mock_can1_regs.TSR     = CAN_TSR_TME0;
    CAN_Packet_T packet[1] = { { .id = 0x123, .dlc = 1, .data = { 0xAA } } };
    ASSERT_EQ( HW_CAN_Tx_Buffer_Write1( packet, 1 ), HW_CAN_RESULT_OK );
    can_tx_buffer1[0].dlc = 9;

    EXPECT_EQ( HW_CAN_Tx_Trigger1(), HW_CAN_RESULT_ERROR );
    EXPECT_EQ( can_tx_rp1, 0 );
    EXPECT_FALSE( can_tx_active1 );
    EXPECT_FALSE( HW_CAN_Channl1_sent() );
    EXPECT_FALSE( mock_can1_regs.IER & CAN_IER_TMEIE );
}

/** Verify that a multi-frame batch completes only after the final hardware completion event. */
TEST_F( HWCANTest, MultiFrameBatchCompletesOnlyAfterFinalHardwareEvent )
{
    mock_can1_regs.TSR      = CAN_TSR_TME0;
    CAN_Packet_T packets[2] = {
        { .id = 0x123, .dlc = 1, .data = { 0xAA } },
        { .id = 0x124, .dlc = 1, .data = { 0xBB } },
    };

    ASSERT_EQ( HW_CAN_Tx_Buffer_Write1( packets, 2 ), HW_CAN_RESULT_OK );
    ASSERT_EQ( HW_CAN_Tx_Trigger1(), HW_CAN_RESULT_OK );
    EXPECT_FALSE( HW_CAN_Channl1_sent() );

    mock_can1_regs.TSR = CAN_TSR_RQCP0 | CAN_TSR_TXOK0 | CAN_TSR_TME0;
    CAN1_TX_IRQHandler();
    EXPECT_TRUE( can_tx_active1 );
    EXPECT_FALSE( HW_CAN_Channl1_sent() );

    mock_can1_regs.TSR = CAN_TSR_RQCP0 | CAN_TSR_TXOK0 | CAN_TSR_TME0;
    CAN1_TX_IRQHandler();
    EXPECT_FALSE( can_tx_active1 );
    EXPECT_TRUE( HW_CAN_Channl1_sent() );
}
