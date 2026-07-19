/******************************************************************************
 *  File:       test_exec_can.cpp
 *  Author:     HIL-RIG Firmware Team
 *  Created:    16-Dec-2025
 *
 *  Description:
 *      Focused unit tests for the execution-level classic-CAN driver.
 *
 *      The low-level HW_CAN entry points are replaced by GMock link seams so
 *      these tests exercise only execution-layer lifecycle and composition:
 *      configure/start rollback, all-or-nothing batch load followed by one
 *      trigger, and frame-based RX peek/copy/consume behaviour.
 *
 *  Notes:
 *      exec_can.c is included directly so its private lifecycle state can be
 *      reset between tests. The production exec_can library is not pulled from
 *      its static archive because this translation unit supplies the same
 *      public symbols.
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstring>

extern "C"
{
#include <stdbool.h>
#include <stdint.h>

#include "exec_can.h"
}

/**-----------------------------------------------------------------------------
 *  Test Constants / Macros
 *------------------------------------------------------------------------------
 */

static constexpr uint32_t TEST_EXEC_CAN_BATCH_SIZE = 3U;

using ::testing::_;
using ::testing::DoAll;
using ::testing::InSequence;
using ::testing::Return;
using ::testing::SetArgPointee;
using ::testing::StrictMock;

/**-----------------------------------------------------------------------------
 *  Test Doubles / Mocks
 *------------------------------------------------------------------------------
 */

class MockHwCan
{
public:
    MOCK_METHOD( HwCanResult_T, ConfigureChannel,
                 ( HwCanChannel_T channel, const HwCanConfig_T* config ) );
    MOCK_METHOD( HwCanResult_T, StartChannel, ( HwCanChannel_T channel ) );
    MOCK_METHOD( HwCanResult_T, StopChannel, ( HwCanChannel_T channel ) );
    MOCK_METHOD( HwCanResult_T, DeconfigureChannel, ( HwCanChannel_T channel ) );
    MOCK_METHOD( HwCanResult_T, RecoverChannel, ( HwCanChannel_T channel ) );
    MOCK_METHOD( HwCanResult_T, LoadTxBuffer,
                 ( HwCanChannel_T channel, const HwCanFrame_T* frames, uint32_t frame_count ) );
    MOCK_METHOD( HwCanResult_T, TxTrigger, ( HwCanChannel_T channel ) );
    MOCK_METHOD( HwCanRxSpans_T, RxPeek, ( HwCanChannel_T channel ) );
    MOCK_METHOD( HwCanResult_T, RxConsume, ( HwCanChannel_T channel, uint32_t frame_count ) );
    MOCK_METHOD( bool, IsTxComplete, ( HwCanChannel_T channel ) );
    MOCK_METHOD( HwCanResult_T, GetStatus, ( HwCanChannel_T channel, HwCanStatus_T* status ) );
    MOCK_METHOD( HwCanResult_T, ClearDiagnostics, ( HwCanChannel_T channel ) );
};

static MockHwCan* g_mock_hw_can = nullptr;

/**-----------------------------------------------------------------------------
 *  Test Data Helpers
 *------------------------------------------------------------------------------
 */

/**
 * @brief Build a representative normal-mode configuration.
 *
 * The low-level driver is mocked, so this helper does not retest bit timing or
 * filter encoding. It supplies realistic values and, importantly, stable
 * caller-owned storage for pointer forwarding checks.
 */
static HwCanConfig_T TEST_EXEC_CAN_Make_Default_Config( void )
{
    HwCanConfig_T config = {};

    config.bitrate                    = 1000000U;
    config.sample_point_permill       = HW_CAN_DEFAULT_SAMPLE_POINT_PERMILL;
    config.sync_jump_width_tq         = 1U;
    config.mode                       = HW_CAN_MODE_NORMAL;
    config.tx_priority                = HW_CAN_TX_PRIORITY_REQUEST_ORDER;
    config.filter_policy              = HW_CAN_FILTER_ACCEPT_ALL;
    config.filters                    = nullptr;
    config.filter_count               = 0U;
    config.automatic_retransmission   = true;
    config.automatic_bus_off_recovery = false;
    config.automatic_wake_up          = false;
    config.receive_fifo_locked        = false;

    return config;
}

/**
 * @brief Build one data frame whose identifier and payload are easy to compare.
 */
static HwCanFrame_T TEST_EXEC_CAN_Make_Frame( uint32_t identifier, uint8_t payload_seed )
{
    HwCanFrame_T frame = {};

    frame.identifier      = identifier;
    frame.dlc             = HW_CAN_CLASSIC_MAX_DATA_BYTES;
    frame.is_extended_id  = false;
    frame.is_remote_frame = false;

    for ( uint32_t i = 0U; i < HW_CAN_CLASSIC_MAX_DATA_BYTES; i++ )
    {
        frame.data[i] = ( uint8_t )( payload_seed + i );
    }

    return frame;
}

/**
 * @brief Build an RX queue record, including metadata that must survive copying.
 */
static HwCanRxFrame_T TEST_EXEC_CAN_Make_Rx_Frame( uint32_t identifier, uint8_t payload_seed,
                                                   uint16_t timestamp, uint8_t filter_match_index )
{
    HwCanRxFrame_T rx_frame = {};

    rx_frame.frame              = TEST_EXEC_CAN_Make_Frame( identifier, payload_seed );
    rx_frame.timestamp          = timestamp;
    rx_frame.filter_match_index = filter_match_index;
    return rx_frame;
}

/**
 * @brief Describe a wrapped low-level RX queue as two immutable frame spans.
 */
static HwCanRxSpans_T TEST_EXEC_CAN_Make_Rx_Spans( const HwCanRxFrame_T* first_frames,
                                                   uint32_t              first_count,
                                                   const HwCanRxFrame_T* second_frames,
                                                   uint32_t              second_count )
{
    HwCanRxSpans_T spans = {};

    spans.first_span.frames       = first_frames;
    spans.first_span.frame_count  = first_count;
    spans.second_span.frames      = second_frames;
    spans.second_span.frame_count = second_count;
    spans.total_frame_count       = first_count + second_count;
    return spans;
}

/**
 * @brief Compare every semantic field without relying on structure padding.
 */
static void TEST_EXEC_CAN_Expect_Rx_Frame_Equals( const HwCanRxFrame_T& actual,
                                                  const HwCanRxFrame_T& expected )
{
    EXPECT_EQ( actual.frame.identifier, expected.frame.identifier );
    EXPECT_EQ( actual.frame.dlc, expected.frame.dlc );
    EXPECT_EQ( actual.frame.is_extended_id, expected.frame.is_extended_id );
    EXPECT_EQ( actual.frame.is_remote_frame, expected.frame.is_remote_frame );
    EXPECT_EQ( actual.timestamp, expected.timestamp );
    EXPECT_EQ( actual.filter_match_index, expected.filter_match_index );

    for ( uint32_t i = 0U; i < HW_CAN_CLASSIC_MAX_DATA_BYTES; i++ )
    {
        EXPECT_EQ( actual.frame.data[i], expected.frame.data[i] );
    }
}

/**-----------------------------------------------------------------------------
 *  Link Seams: HW_CAN Functions Delegated to GMock
 *------------------------------------------------------------------------------
 */

// NOLINTBEGIN

extern "C" HwCanResult_T HW_CAN_Configure_Channel( HwCanChannel_T       channel,
                                                   const HwCanConfig_T* config )
{
    return g_mock_hw_can->ConfigureChannel( channel, config );
}

extern "C" HwCanResult_T HW_CAN_Start_Channel( HwCanChannel_T channel )
{
    return g_mock_hw_can->StartChannel( channel );
}

extern "C" HwCanResult_T HW_CAN_Stop_Channel( HwCanChannel_T channel )
{
    return g_mock_hw_can->StopChannel( channel );
}

extern "C" HwCanResult_T HW_CAN_Deconfigure_Channel( HwCanChannel_T channel )
{
    return g_mock_hw_can->DeconfigureChannel( channel );
}

extern "C" HwCanResult_T HW_CAN_Recover_Channel( HwCanChannel_T channel )
{
    return g_mock_hw_can->RecoverChannel( channel );
}

extern "C" HwCanResult_T HW_CAN_Load_Tx_Buffer( HwCanChannel_T channel, const HwCanFrame_T* frames,
                                                uint32_t frame_count )
{
    return g_mock_hw_can->LoadTxBuffer( channel, frames, frame_count );
}

extern "C" HwCanResult_T HW_CAN_Tx_Trigger( HwCanChannel_T channel )
{
    return g_mock_hw_can->TxTrigger( channel );
}

extern "C" HwCanRxSpans_T HW_CAN_Rx_Peek( HwCanChannel_T channel )
{
    return g_mock_hw_can->RxPeek( channel );
}

extern "C" HwCanResult_T HW_CAN_Rx_Consume( HwCanChannel_T channel, uint32_t frame_count )
{
    return g_mock_hw_can->RxConsume( channel, frame_count );
}

extern "C" bool HW_CAN_Is_Tx_Complete( HwCanChannel_T channel )
{
    return g_mock_hw_can->IsTxComplete( channel );
}

extern "C" HwCanResult_T HW_CAN_Get_Status( HwCanChannel_T channel, HwCanStatus_T* status )
{
    return g_mock_hw_can->GetStatus( channel, status );
}

extern "C" HwCanResult_T HW_CAN_Clear_Diagnostics( HwCanChannel_T channel )
{
    return g_mock_hw_can->ClearDiagnostics( channel );
}

// NOLINTEND

/**-----------------------------------------------------------------------------
 *  Implementation Under Test
 *------------------------------------------------------------------------------
 */

extern "C"
{
#include "exec_can.c"
}

/**-----------------------------------------------------------------------------
 *  Test Fixture
 *------------------------------------------------------------------------------
 */

/**
 * @brief Test fixture for module tests.
 *
 * The execution driver owns private static lifecycle state. Direct inclusion of
 * exec_can.c makes that state visible in this translation unit, allowing each
 * test to begin from a deterministic unconfigured state without issuing fake
 * production operations merely to clean up the previous test.
 */
class ExecCANTest : public ::testing::Test
{
protected:
    StrictMock<MockHwCan> mock_hw_can;

    void SetUp( void ) override
    {
        g_mock_hw_can = &mock_hw_can;
        std::memset( exec_can_channel_states, 0, sizeof( exec_can_channel_states ) );
    }

    void TearDown( void ) override
    {
        g_mock_hw_can = nullptr;
    }

    /**
     * @brief Configure and start one execution channel for hot-path tests.
     */
    void ExpectAndActivate( HwCanChannel_T channel, const HwCanConfig_T& config )
    {
        InSequence sequence;

        EXPECT_CALL( mock_hw_can, ConfigureChannel( channel, &config ) )
            .WillOnce( Return( HW_CAN_RESULT_OK ) );
        EXPECT_CALL( mock_hw_can, StartChannel( channel ) ).WillOnce( Return( HW_CAN_RESULT_OK ) );

        ASSERT_EQ( EXEC_CAN_Configure_Channel( channel, &config ), HW_CAN_RESULT_OK );
        ASSERT_EQ( exec_can_channel_states[channel], EXEC_CAN_STATE_ACTIVE );
    }
};

/**-----------------------------------------------------------------------------
 *  Test Cases
 *------------------------------------------------------------------------------
 */

TEST_F( ExecCANTest, ConfigureChannelConfiguresStartsAndRecordsActiveState )
{
    const HwCanConfig_T config = TEST_EXEC_CAN_Make_Default_Config();

    {
        InSequence sequence;

        EXPECT_CALL( mock_hw_can, ConfigureChannel( HW_CAN_CHANNEL_1, &config ) )
            .WillOnce( Return( HW_CAN_RESULT_OK ) );
        EXPECT_CALL( mock_hw_can, StartChannel( HW_CAN_CHANNEL_1 ) )
            .WillOnce( Return( HW_CAN_RESULT_OK ) );
    }

    EXPECT_EQ( EXEC_CAN_Configure_Channel( HW_CAN_CHANNEL_1, &config ), HW_CAN_RESULT_OK );
    EXPECT_EQ( exec_can_channel_states[HW_CAN_CHANNEL_1], EXEC_CAN_STATE_ACTIVE );
}

TEST_F( ExecCANTest, ConfigureFailureAttemptsRollbackAndPreservesOriginalResult )
{
    const HwCanConfig_T config = TEST_EXEC_CAN_Make_Default_Config();

    {
        InSequence sequence;

        EXPECT_CALL( mock_hw_can, ConfigureChannel( HW_CAN_CHANNEL_1, &config ) )
            .WillOnce( Return( HW_CAN_RESULT_FILTER_ERROR ) );

        // Low-level configuration can fail after partially changing timing or
        // shared filter resources. The execution layer must request cleanup,
        // but cleanup failure must not hide the original configuration error.
        EXPECT_CALL( mock_hw_can, DeconfigureChannel( HW_CAN_CHANNEL_1 ) )
            .WillOnce( Return( HW_CAN_RESULT_HARDWARE_ERROR ) );
    }

    EXPECT_CALL( mock_hw_can, StartChannel( _ ) ).Times( 0 );

    EXPECT_EQ( EXEC_CAN_Configure_Channel( HW_CAN_CHANNEL_1, &config ),
               HW_CAN_RESULT_FILTER_ERROR );
    EXPECT_EQ( exec_can_channel_states[HW_CAN_CHANNEL_1], EXEC_CAN_STATE_FAULT );

    // A failed cleanup is retained as a fault for a later cleanup retry, while
    // the hot API still rejects use without making another low-level call.
    const HwCanFrame_T frame = TEST_EXEC_CAN_Make_Frame( 0x123U, 0x10U );
    EXPECT_EQ( EXEC_CAN_Transmit( HW_CAN_CHANNEL_1, &frame, 1U ), HW_CAN_RESULT_NOT_CONFIGURED );
}

TEST_F( ExecCANTest, StartFailureAttemptsRollbackAndRetainsCleanupFailureAsFault )
{
    const HwCanConfig_T config = TEST_EXEC_CAN_Make_Default_Config();

    {
        InSequence sequence;

        EXPECT_CALL( mock_hw_can, ConfigureChannel( HW_CAN_CHANNEL_1, &config ) )
            .WillOnce( Return( HW_CAN_RESULT_OK ) );
        EXPECT_CALL( mock_hw_can, StartChannel( HW_CAN_CHANNEL_1 ) )
            .WillOnce( Return( HW_CAN_RESULT_HARDWARE_ERROR ) );

        // Rollback is deliberately best-effort. Even if hardware deconfigure
        // fails, the execution layer must not expose a half-started channel as
        // usable, and it must preserve the original start failure for diagnosis.
        EXPECT_CALL( mock_hw_can, DeconfigureChannel( HW_CAN_CHANNEL_1 ) )
            .WillOnce( Return( HW_CAN_RESULT_BUSY ) );
    }

    EXPECT_EQ( EXEC_CAN_Configure_Channel( HW_CAN_CHANNEL_1, &config ),
               HW_CAN_RESULT_HARDWARE_ERROR );
    EXPECT_EQ( exec_can_channel_states[HW_CAN_CHANNEL_1], EXEC_CAN_STATE_FAULT );

    const HwCanFrame_T frame = TEST_EXEC_CAN_Make_Frame( 0x123U, 0x10U );
    EXPECT_EQ( EXEC_CAN_Transmit( HW_CAN_CHANNEL_1, &frame, 1U ), HW_CAN_RESULT_NOT_CONFIGURED );
}

TEST_F( ExecCANTest, ReconfigureStopsFaultedChannelBeforeApplyingNewConfiguration )
{
    const HwCanConfig_T config                = TEST_EXEC_CAN_Make_Default_Config();
    exec_can_channel_states[HW_CAN_CHANNEL_1] = EXEC_CAN_STATE_FAULT;

    {
        InSequence sequence;

        EXPECT_CALL( mock_hw_can, StopChannel( HW_CAN_CHANNEL_1 ) )
            .WillOnce( Return( HW_CAN_RESULT_OK ) );
        EXPECT_CALL( mock_hw_can, ConfigureChannel( HW_CAN_CHANNEL_1, &config ) )
            .WillOnce( Return( HW_CAN_RESULT_OK ) );
        EXPECT_CALL( mock_hw_can, StartChannel( HW_CAN_CHANNEL_1 ) )
            .WillOnce( Return( HW_CAN_RESULT_OK ) );
    }

    EXPECT_EQ( EXEC_CAN_Configure_Channel( HW_CAN_CHANNEL_1, &config ), HW_CAN_RESULT_OK );
    EXPECT_EQ( exec_can_channel_states[HW_CAN_CHANNEL_1], EXEC_CAN_STATE_ACTIVE );
}

TEST_F( ExecCANTest, TransmitLoadsWholeBatchOnceThenTriggersOnce )
{
    const HwCanConfig_T config = TEST_EXEC_CAN_Make_Default_Config();
    ExpectAndActivate( HW_CAN_CHANNEL_1, config );

    const HwCanFrame_T frames[TEST_EXEC_CAN_BATCH_SIZE] = {
        TEST_EXEC_CAN_Make_Frame( 0x101U, 0x10U ),
        TEST_EXEC_CAN_Make_Frame( 0x202U, 0x20U ),
        TEST_EXEC_CAN_Make_Frame( 0x303U, 0x30U ),
    };

    {
        InSequence sequence;

        // One batch call preserves the low-level all-or-nothing queue contract.
        EXPECT_CALL( mock_hw_can,
                     LoadTxBuffer( HW_CAN_CHANNEL_1, frames, TEST_EXEC_CAN_BATCH_SIZE ) )
            .WillOnce( Return( HW_CAN_RESULT_OK ) );
        EXPECT_CALL( mock_hw_can, TxTrigger( HW_CAN_CHANNEL_1 ) )
            .WillOnce( Return( HW_CAN_RESULT_OK ) );
    }

    EXPECT_EQ( EXEC_CAN_Transmit( HW_CAN_CHANNEL_1, frames, TEST_EXEC_CAN_BATCH_SIZE ),
               HW_CAN_RESULT_OK );
}

TEST_F( ExecCANTest, TransmitLoadFailureDoesNotTrigger )
{
    const HwCanConfig_T config = TEST_EXEC_CAN_Make_Default_Config();
    ExpectAndActivate( HW_CAN_CHANNEL_2, config );

    const HwCanFrame_T frames[TEST_EXEC_CAN_BATCH_SIZE] = {
        TEST_EXEC_CAN_Make_Frame( 0x111U, 0x11U ),
        TEST_EXEC_CAN_Make_Frame( 0x222U, 0x22U ),
        TEST_EXEC_CAN_Make_Frame( 0x333U, 0x33U ),
    };

    EXPECT_CALL( mock_hw_can, LoadTxBuffer( HW_CAN_CHANNEL_2, frames, TEST_EXEC_CAN_BATCH_SIZE ) )
        .WillOnce( Return( HW_CAN_RESULT_QUEUE_FULL ) );
    EXPECT_CALL( mock_hw_can, TxTrigger( _ ) ).Times( 0 );

    EXPECT_EQ( EXEC_CAN_Transmit( HW_CAN_CHANNEL_2, frames, TEST_EXEC_CAN_BATCH_SIZE ),
               HW_CAN_RESULT_QUEUE_FULL );
}

TEST_F( ExecCANTest, ReceiveCopiesWrappedSpansAndConsumesExactlyCopiedFrames )
{
    const HwCanConfig_T config = TEST_EXEC_CAN_Make_Default_Config();
    ExpectAndActivate( HW_CAN_CHANNEL_1, config );

    const HwCanRxFrame_T first_span_frames[2] = {
        TEST_EXEC_CAN_Make_Rx_Frame( 0x101U, 0x10U, 11U, 1U ),
        TEST_EXEC_CAN_Make_Rx_Frame( 0x102U, 0x20U, 12U, 2U ),
    };
    const HwCanRxFrame_T second_span_frames[2] = {
        TEST_EXEC_CAN_Make_Rx_Frame( 0x103U, 0x30U, 13U, 3U ),
        TEST_EXEC_CAN_Make_Rx_Frame( 0x104U, 0x40U, 14U, 4U ),
    };
    const HwCanRxSpans_T spans =
        TEST_EXEC_CAN_Make_Rx_Spans( first_span_frames, 2U, second_span_frames, 2U );

    HwCanRxFrame_T destination[3] = {};
    uint32_t       frames_read    = 0U;

    {
        InSequence sequence;

        EXPECT_CALL( mock_hw_can, RxPeek( HW_CAN_CHANNEL_1 ) ).WillOnce( Return( spans ) );

        // Capacity is three frames while four are unread. The execution layer
        // must copy the two-frame first span, one frame from the wrapped span,
        // and leave the final frame available for a later call.
        EXPECT_CALL( mock_hw_can, RxConsume( HW_CAN_CHANNEL_1, 3U ) )
            .WillOnce( Return( HW_CAN_RESULT_OK ) );
    }

    ASSERT_EQ( EXEC_CAN_Receive( HW_CAN_CHANNEL_1, destination, 3U, &frames_read ),
               HW_CAN_RESULT_OK );
    ASSERT_EQ( frames_read, 3U );

    TEST_EXEC_CAN_Expect_Rx_Frame_Equals( destination[0], first_span_frames[0] );
    TEST_EXEC_CAN_Expect_Rx_Frame_Equals( destination[1], first_span_frames[1] );
    TEST_EXEC_CAN_Expect_Rx_Frame_Equals( destination[2], second_span_frames[0] );
}

TEST_F( ExecCANTest, BusOffTriggerFaultCanBeRecoveredWithoutReloadingFrames )
{
    const HwCanConfig_T config = TEST_EXEC_CAN_Make_Default_Config();
    ExpectAndActivate( HW_CAN_CHANNEL_1, config );

    const HwCanFrame_T frame = TEST_EXEC_CAN_Make_Frame( 0x321U, 0x50U );

    {
        InSequence sequence;

        EXPECT_CALL( mock_hw_can, LoadTxBuffer( HW_CAN_CHANNEL_1, &frame, 1U ) )
            .WillOnce( Return( HW_CAN_RESULT_OK ) );
        EXPECT_CALL( mock_hw_can, TxTrigger( HW_CAN_CHANNEL_1 ) )
            .WillOnce( Return( HW_CAN_RESULT_BUS_OFF ) );
    }

    EXPECT_EQ( EXEC_CAN_Transmit( HW_CAN_CHANNEL_1, &frame, 1U ), HW_CAN_RESULT_BUS_OFF );
    EXPECT_EQ( exec_can_channel_states[HW_CAN_CHANNEL_1], EXEC_CAN_STATE_FAULT );

    EXPECT_CALL( mock_hw_can, RecoverChannel( HW_CAN_CHANNEL_1 ) )
        .WillOnce( Return( HW_CAN_RESULT_OK ) );

    EXPECT_EQ( EXEC_CAN_Recover_Channel( HW_CAN_CHANNEL_1 ), HW_CAN_RESULT_OK );
    EXPECT_EQ( exec_can_channel_states[HW_CAN_CHANNEL_1], EXEC_CAN_STATE_ACTIVE );
}

TEST_F( ExecCANTest, GetStatusDelegatesDiagnosticSnapshotWithoutInterpretation )
{
    HwCanStatus_T low_level_status         = {};
    low_level_status.state                 = HW_CAN_STATE_ACTIVE;
    low_level_status.actual_bitrate        = 1000000U;
    low_level_status.tx_in_flight_count    = 2U;
    low_level_status.latched_faults        = HW_CAN_FAULT_TX_ARBITRATION_LOST;
    low_level_status.tx_arbitration_losses = 7U;

    HwCanStatus_T received_status = {};

    EXPECT_CALL( mock_hw_can, GetStatus( HW_CAN_CHANNEL_2, &received_status ) )
        .WillOnce( DoAll( SetArgPointee<1>( low_level_status ), Return( HW_CAN_RESULT_OK ) ) );

    ASSERT_EQ( EXEC_CAN_Get_Status( HW_CAN_CHANNEL_2, &received_status ), HW_CAN_RESULT_OK );
    EXPECT_EQ( received_status.state, HW_CAN_STATE_ACTIVE );
    EXPECT_EQ( received_status.actual_bitrate, 1000000U );
    EXPECT_EQ( received_status.tx_in_flight_count, 2U );
    EXPECT_EQ( received_status.latched_faults, HW_CAN_FAULT_TX_ARBITRATION_LOST );
    EXPECT_EQ( received_status.tx_arbitration_losses, 7U );
}
