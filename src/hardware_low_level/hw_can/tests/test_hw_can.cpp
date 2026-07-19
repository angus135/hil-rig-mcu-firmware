/******************************************************************************
 *  File:       test_hw_can.cpp
 *  Author:     HIL-RIG Firmware Team
 *  Created:    25-Mar-2026
 *
 *  Description:
 *      Focused unit tests for the STM32 bxCAN low-level driver.
 *
 *      These tests exercise the contracts introduced by the frame-aware,
 *      asynchronous driver: cold-path timing and lifecycle configuration,
 *      explicit CAN identifiers and DLC values, atomic TX queue publication,
 *      mailbox peek/consume ownership, bounded interrupt processing, complete
 *      RX metadata, and externally observable error diagnostics.
 *
 *  Notes:
 *      Production registers with write-one-to-clear or command semantics cannot
 *      be represented by plain host RAM. hw_can_mocks.h therefore records and
 *      emulates those writes so these tests fail if the driver reintroduces an
 *      unsafe register read-modify-write.
 ******************************************************************************/

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <vector>

extern "C"
{
#include "hw_can.h"

// The implementation is included so the existing focused suite can reset and
// inspect private queue state without adding test-only production APIs.
#include "hw_can.c"
}

using ::testing::_;
using ::testing::InSequence;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;

/**-----------------------------------------------------------------------------
 *  Mock Hardware Objects
 *------------------------------------------------------------------------------
 */

extern "C"
{
CAN_TypeDef mock_can1_regs = {};
CAN_TypeDef mock_can2_regs = {};

CAN_HandleTypeDef hcan1 = {};
CAN_HandleTypeDef hcan2 = {};

uint32_t  mock_last_tsr_write   = 0U;
uint32_t  mock_last_rf0r_write  = 0U;
uint32_t  mock_last_msr_write   = 0U;
IRQn_Type mock_last_pending_irq = ( IRQn_Type )-1;
IRQn_Type mock_last_cleared_irq = ( IRQn_Type )-1;
}

/**
 * @brief GoogleMock facade for the cold-path STM32 HAL dependencies.
 *
 * Runtime CAN ISR paths intentionally bypass HAL. These methods therefore
 * describe only configuration, lifecycle, notification, and clock operations.
 */
class MockHWCAN
{
public:
    MOCK_METHOD( HAL_StatusTypeDef, CANInit, ( CAN_HandleTypeDef * handle ), () );
    MOCK_METHOD( HAL_StatusTypeDef, CANConfigFilter,
                 ( CAN_HandleTypeDef * handle, CAN_FilterTypeDef* filter ), () );
    MOCK_METHOD( HAL_StatusTypeDef, CANStart, ( CAN_HandleTypeDef * handle ), () );
    MOCK_METHOD( HAL_StatusTypeDef, CANStop, ( CAN_HandleTypeDef * handle ), () );
    MOCK_METHOD( HAL_StatusTypeDef, CANActivateNotification,
                 ( CAN_HandleTypeDef * handle, uint32_t notifications ), () );
    MOCK_METHOD( HAL_StatusTypeDef, CANDeactivateNotification,
                 ( CAN_HandleTypeDef * handle, uint32_t notifications ), () );
    MOCK_METHOD( uint32_t, PeripheralClockHz, (), () );
};

static MockHWCAN* g_mock = nullptr;

extern "C" HAL_StatusTypeDef HAL_CAN_Init( CAN_HandleTypeDef* handle )
{
    return g_mock->CANInit( handle );
}

extern "C" HAL_StatusTypeDef HAL_CAN_ConfigFilter( CAN_HandleTypeDef* handle,
                                                   CAN_FilterTypeDef* filter )
{
    return g_mock->CANConfigFilter( handle, filter );
}

extern "C" HAL_StatusTypeDef HAL_CAN_Start( CAN_HandleTypeDef* handle )
{
    return g_mock->CANStart( handle );
}

extern "C" HAL_StatusTypeDef HAL_CAN_Stop( CAN_HandleTypeDef* handle )
{
    return g_mock->CANStop( handle );
}

extern "C" HAL_StatusTypeDef HAL_CAN_ActivateNotification( CAN_HandleTypeDef* handle,
                                                           uint32_t           notifications )
{
    return g_mock->CANActivateNotification( handle, notifications );
}

extern "C" HAL_StatusTypeDef HAL_CAN_DeactivateNotification( CAN_HandleTypeDef* handle,
                                                             uint32_t           notifications )
{
    return g_mock->CANDeactivateNotification( handle, notifications );
}

extern "C" uint32_t HAL_RCC_GetPCLK1Freq( void )
{
    return g_mock->PeripheralClockHz();
}

/**-----------------------------------------------------------------------------
 *  Test Helpers
 *------------------------------------------------------------------------------
 */

/**
 * @brief Build the normal one-Mbit/s, request-order, accept-all configuration.
 *
 * Accept-all is explicit here because bxCAN has no implicit receive path: at
 * least one active filter must accept a frame before it reaches FIFO0.
 */
static HwCanConfig_T MakeDefaultConfiguration( void )
{
    HwCanConfig_T config = {};

    config.bitrate                    = 1000000U;
    config.sample_point_permill       = ( uint16_t )HW_CAN_DEFAULT_SAMPLE_POINT_PERMILL;
    config.sync_jump_width_tq         = 1U;
    config.mode                       = HW_CAN_MODE_NORMAL;
    config.tx_priority                = HW_CAN_TX_PRIORITY_REQUEST_ORDER;
    config.filter_policy              = HW_CAN_FILTER_ACCEPT_ALL;
    config.filters                    = nullptr;
    config.filter_count               = 0U;
    config.automatic_retransmission   = false;
    config.automatic_bus_off_recovery = false;
    config.automatic_wake_up          = false;
    config.receive_fifo_locked        = false;

    return config;
}

/**
 * @brief Construct a valid classic-CAN frame with deterministic payload bytes.
 */
static HwCanFrame_T MakeFrame( uint32_t identifier, uint8_t dlc, bool extended = false,
                               bool remote = false, uint8_t first_data_byte = 0x10U )
{
    HwCanFrame_T frame = {};

    frame.identifier      = identifier;
    frame.dlc             = dlc;
    frame.is_extended_id  = extended;
    frame.is_remote_frame = remote;

    for ( uint32_t i = 0U; i < HW_CAN_CLASSIC_MAX_DATA_BYTES; ++i )
    {
        frame.data[i] = ( uint8_t )( ( uint32_t )first_data_byte + i );
    }

    return frame;
}

/**
 * @brief Fixture that restores both software queues and bxCAN register models.
 */
class HWCANTest : public ::testing::Test
{
protected:
    NiceMock<MockHWCAN> mock;

    void SetUp() override
    {
        g_mock = &mock;

        std::memset( &mock_can1_regs, 0, sizeof( mock_can1_regs ) );
        std::memset( &mock_can2_regs, 0, sizeof( mock_can2_regs ) );
        std::memset( &hcan1, 0, sizeof( hcan1 ) );
        std::memset( &hcan2, 0, sizeof( hcan2 ) );
        std::memset( hw_can_channel_states, 0, sizeof( hw_can_channel_states ) );

        hcan1.Instance = CAN1;
        hcan2.Instance = CAN2;

        // Reset-state bxCAN mailboxes are empty and immediately loadable.
        mock_can1_regs.TSR = CAN_TSR_TME0 | CAN_TSR_TME1 | CAN_TSR_TME2;
        mock_can2_regs.TSR = CAN_TSR_TME0 | CAN_TSR_TME1 | CAN_TSR_TME2;

        mock_last_tsr_write   = 0U;
        mock_last_rf0r_write  = 0U;
        mock_last_msr_write   = 0U;
        mock_last_pending_irq = ( IRQn_Type )-1;
        mock_last_cleared_irq = ( IRQn_Type )-1;

        ON_CALL( mock, CANInit( _ ) ).WillByDefault( Return( HAL_OK ) );
        ON_CALL( mock, CANConfigFilter( _, _ ) ).WillByDefault( Return( HAL_OK ) );
        ON_CALL( mock, CANStart( _ ) ).WillByDefault( Return( HAL_OK ) );
        ON_CALL( mock, CANStop( _ ) ).WillByDefault( Return( HAL_OK ) );
        ON_CALL( mock, CANActivateNotification( _, _ ) ).WillByDefault( Return( HAL_OK ) );
        ON_CALL( mock, CANDeactivateNotification( _, _ ) ).WillByDefault( Return( HAL_OK ) );
        ON_CALL( mock, PeripheralClockHz() ).WillByDefault( Return( 45000000U ) );
    }

    void TearDown() override
    {
        g_mock = nullptr;
    }

    void ConfigureOnly( HwCanChannel_T channel = HW_CAN_CHANNEL_1 )
    {
        HwCanConfig_T config = MakeDefaultConfiguration();
        ASSERT_EQ( HW_CAN_Configure_Channel( channel, &config ), HW_CAN_RESULT_OK );
    }

    void ConfigureAndStart( HwCanChannel_T channel = HW_CAN_CHANNEL_1 )
    {
        ConfigureOnly( channel );
        ASSERT_EQ( HW_CAN_Start_Channel( channel ), HW_CAN_RESULT_OK );
    }

    HwCanStatus_T ReadStatus( HwCanChannel_T channel = HW_CAN_CHANNEL_1 )
    {
        HwCanStatus_T status = {};
        EXPECT_EQ( HW_CAN_Get_Status( channel, &status ), HW_CAN_RESULT_OK );
        return status;
    }
};

/**-----------------------------------------------------------------------------
 *  Configuration and Lifecycle Tests
 *------------------------------------------------------------------------------
 */

TEST_F( HWCANTest, ConfigureComputesExactTimingProgramsFilterPartitionAndStarts )
{
    std::vector<CAN_FilterTypeDef> programmed_filters;
    uint32_t                       activated_notifications = 0U;

    // Configuration must discard messages and pending error causes retained by
    // the peripheral from an earlier stopped configuration.
    mock_can1_regs.RF0R = 2U | CAN_RF0R_FULL0 | CAN_RF0R_FOVR0;
    mock_can1_regs.MSR  = CAN_MSR_ERRI;
    mock_can1_regs.ESR  = ( uint32_t )HW_CAN_LAST_ERROR_FORM << CAN_ESR_LEC_Pos;

    EXPECT_CALL( mock, PeripheralClockHz() ).WillOnce( Return( 45000000U ) );
    EXPECT_CALL( mock, CANInit( &hcan1 ) ).Times( 1 );
    EXPECT_CALL( mock, CANConfigFilter( &hcan1, _ ) )
        .Times( HW_CAN_MAX_FILTERS_PER_CHANNEL )
        .WillRepeatedly( Invoke( [&]( CAN_HandleTypeDef*, CAN_FilterTypeDef* filter ) {
            programmed_filters.push_back( *filter );
            return HAL_OK;
        } ) );
    EXPECT_CALL( mock, CANStart( &hcan1 ) ).Times( 1 );
    EXPECT_CALL( mock, CANActivateNotification( &hcan1, _ ) )
        .WillOnce( Invoke( [&]( CAN_HandleTypeDef*, uint32_t notifications ) {
            activated_notifications = notifications;
            return HAL_OK;
        } ) );

    HwCanConfig_T config = MakeDefaultConfiguration();

    ASSERT_EQ( HW_CAN_Configure_Channel( HW_CAN_CHANNEL_1, &config ), HW_CAN_RESULT_OK );

    // Forty-five MHz divided by BRP 3 and 15 TQ gives exactly one Mbit/s.
    EXPECT_EQ( hcan1.Init.Prescaler, 3U );
    EXPECT_EQ( hcan1.Init.TimeSeg1, CAN_BS1_11TQ );
    EXPECT_EQ( hcan1.Init.TimeSeg2, CAN_BS2_3TQ );
    EXPECT_EQ( hcan1.Init.SyncJumpWidth, CAN_SJW_1TQ );
    EXPECT_EQ( hcan1.Init.TransmitFifoPriority, ENABLE );

    ASSERT_EQ( programmed_filters.size(), ( std::size_t )HW_CAN_MAX_FILTERS_PER_CHANNEL );
    EXPECT_EQ( programmed_filters[0].FilterBank, 0U );
    EXPECT_EQ( programmed_filters[0].FilterActivation, ENABLE );
    EXPECT_EQ( programmed_filters[0].FilterIdHigh, 0U );
    EXPECT_EQ( programmed_filters[0].FilterIdLow, 0U );
    EXPECT_EQ( programmed_filters[0].FilterMaskIdHigh, 0U );
    EXPECT_EQ( programmed_filters[0].FilterMaskIdLow, 0U );

    for ( std::size_t i = 1U; i < programmed_filters.size(); ++i )
    {
        EXPECT_EQ( programmed_filters[i].FilterBank, ( uint32_t )i );
        EXPECT_EQ( programmed_filters[i].FilterActivation, DISABLE );
    }

    // Configuration explicitly removes stale W1C state from an earlier run.
    EXPECT_EQ( mock_last_tsr_write, CAN_TSR_RQCP0 | CAN_TSR_RQCP1 | CAN_TSR_RQCP2 );
    EXPECT_EQ( mock_last_rf0r_write, CAN_RF0R_FULL0 | CAN_RF0R_FOVR0 );
    EXPECT_EQ( mock_can1_regs.RF0R & CAN_RF0R_FMP0, 0U );
    EXPECT_EQ( mock_can1_regs.MSR & CAN_MSR_ERRI, 0U );
    EXPECT_EQ( mock_can1_regs.ESR & CAN_ESR_LEC, 0U );
    EXPECT_EQ( mock_last_cleared_irq, CAN1_SCE_IRQn );

    ASSERT_EQ( HW_CAN_Start_Channel( HW_CAN_CHANNEL_1 ), HW_CAN_RESULT_OK );
    EXPECT_TRUE( HW_CAN_Is_Channel_Started( HW_CAN_CHANNEL_1 ) );
    EXPECT_EQ( activated_notifications, HW_CAN_RUNTIME_NOTIFICATION_MASK );

    HwCanStatus_T status = ReadStatus();
    EXPECT_EQ( status.state, HW_CAN_STATE_ACTIVE );
    EXPECT_EQ( status.requested_bitrate, 1000000U );
    EXPECT_EQ( status.actual_bitrate, 1000000U );
    EXPECT_EQ( status.actual_sample_point_permill, 800U );
    EXPECT_EQ( status.bitrate_error_ppm, 0U );
    EXPECT_EQ( status.prescaler, 3U );
    EXPECT_EQ( status.bit_segment_1_tq, 11U );
    EXPECT_EQ( status.bit_segment_2_tq, 3U );
}

TEST_F( HWCANTest, ConfigureRejectsTimingWhenPeripheralClockIsUnavailable )
{
    EXPECT_CALL( mock, PeripheralClockHz() ).WillOnce( Return( 0U ) );
    EXPECT_CALL( mock, CANInit( _ ) ).Times( 0 );

    HwCanConfig_T config = MakeDefaultConfiguration();

    EXPECT_EQ( HW_CAN_Configure_Channel( HW_CAN_CHANNEL_1, &config ),
               HW_CAN_RESULT_UNSUPPORTED_TIMING );
    EXPECT_EQ( ReadStatus().state, HW_CAN_STATE_UNCONFIGURED );
}

TEST_F( HWCANTest, RecoverRejectsFaultWhenNoCompleteConfigurationWasStored )
{
    EXPECT_CALL( mock, CANInit( &hcan1 ) ).WillOnce( Return( HAL_ERROR ) );

    HwCanConfig_T config = MakeDefaultConfiguration();

    EXPECT_EQ( HW_CAN_Configure_Channel( HW_CAN_CHANNEL_1, &config ),
               HW_CAN_RESULT_HARDWARE_ERROR );
    EXPECT_EQ( ReadStatus().state, HW_CAN_STATE_FAULT );

    // No timing/filter configuration reached persistent storage, so recovery
    // must reject the request without indexing zero-valued timing fields.
    EXPECT_EQ( HW_CAN_Recover_Channel( HW_CAN_CHANNEL_1 ), HW_CAN_RESULT_NOT_CONFIGURED );
}

TEST_F( HWCANTest, BusOffAfterLoadingRetainsCompleteBatchForRecovery )
{
    ConfigureAndStart();

    mock_can1_regs.ESR = CAN_ESR_BOFF;
    HwCanFrame_T frame = MakeFrame( 0x456U, 4U, false, false, 0x30U );

    EXPECT_EQ( HW_CAN_Load_Tx_Buffer( HW_CAN_CHANNEL_1, &frame, 1U ), HW_CAN_RESULT_OK );
    EXPECT_EQ( HW_CAN_Tx_Trigger( HW_CAN_CHANNEL_1 ), HW_CAN_RESULT_BUS_OFF );

    // Simulate a TX vector that was already pending before trigger observed
    // bus-off. It may classify old completions, but it must not transfer the
    // retained software frame into a hardware mailbox.
    HW_CAN_CH1_TX_IRQ_HANDLER();

    HwCanStatus_T status = ReadStatus();
    EXPECT_EQ( status.state, HW_CAN_STATE_FAULT );
    EXPECT_EQ( status.tx_queue_frame_count, 1U );
    EXPECT_EQ( status.tx_in_flight_count, 0U );
    EXPECT_EQ( status.tx_frames_queued, 1U );
    EXPECT_EQ( status.tx_frames_submitted, 0U );
    EXPECT_EQ( mock_can1_regs.IER & CAN_IER_TMEIE, 0U );
}

TEST_F( HWCANTest, StartRollsBackToConfiguredWhenNotificationActivationFails )
{
    ConfigureOnly();

    EXPECT_CALL( mock, CANStart( &hcan1 ) ).WillOnce( Return( HAL_OK ) );
    EXPECT_CALL( mock, CANActivateNotification( &hcan1, _ ) ).WillOnce( Return( HAL_ERROR ) );
    EXPECT_CALL( mock, CANStop( &hcan1 ) ).WillOnce( Return( HAL_OK ) );

    EXPECT_EQ( HW_CAN_Start_Channel( HW_CAN_CHANNEL_1 ), HW_CAN_RESULT_HARDWARE_ERROR );
    EXPECT_FALSE( HW_CAN_Is_Channel_Started( HW_CAN_CHANNEL_1 ) );
    EXPECT_EQ( ReadStatus().state, HW_CAN_STATE_CONFIGURED );
}

TEST_F( HWCANTest, FailedStartRollbackRetainsPhysicalStateForDeconfigureRetry )
{
    ConfigureOnly();

    {
        InSequence sequence;

        EXPECT_CALL( mock, CANStart( &hcan1 ) ).WillOnce( Return( HAL_OK ) );
        EXPECT_CALL( mock, CANActivateNotification( &hcan1, _ ) ).WillOnce( Return( HAL_ERROR ) );
        EXPECT_CALL( mock, CANStop( &hcan1 ) ).WillOnce( Return( HAL_ERROR ) );
    }

    EXPECT_EQ( HW_CAN_Start_Channel( HW_CAN_CHANNEL_1 ), HW_CAN_RESULT_HARDWARE_ERROR );
    EXPECT_FALSE( HW_CAN_Is_Channel_Started( HW_CAN_CHANNEL_1 ) );
    EXPECT_EQ( ReadStatus().state, HW_CAN_STATE_FAULT );

    {
        InSequence sequence;

        // Deconfiguration must retry HAL stop because the failed rollback left
        // the peripheral physically started even though it is not API-usable.
        EXPECT_CALL( mock, CANDeactivateNotification( &hcan1, _ ) ).WillOnce( Return( HAL_OK ) );
        EXPECT_CALL( mock, CANStop( &hcan1 ) ).WillOnce( Return( HAL_OK ) );
    }

    EXPECT_EQ( HW_CAN_Deconfigure_Channel( HW_CAN_CHANNEL_1 ), HW_CAN_RESULT_OK );
    EXPECT_EQ( ReadStatus().state, HW_CAN_STATE_UNCONFIGURED );
}

/**-----------------------------------------------------------------------------
 *  Transmit Queue and Mailbox Tests
 *------------------------------------------------------------------------------
 */

TEST_F( HWCANTest, TxMailboxEncodingPreservesStandardExtendedRemoteIdsAndDlc )
{
    ConfigureAndStart();

    HwCanFrame_T frames[3] = {
        MakeFrame( 0x321U, 3U, false, false, 0x10U ),
        MakeFrame( 0x01ABCDEU, 8U, true, false, 0x20U ),
        MakeFrame( 0x01234567U, 4U, true, true, 0xA0U ),
    };

    ASSERT_EQ( HW_CAN_Load_Tx_Buffer( HW_CAN_CHANNEL_1, frames, 3U ), HW_CAN_RESULT_OK );
    ASSERT_EQ( HW_CAN_Tx_Trigger( HW_CAN_CHANNEL_1 ), HW_CAN_RESULT_OK );
    EXPECT_EQ( mock_last_pending_irq, CAN1_TX_IRQn );

    // The pended interrupt is invoked explicitly by the host test harness.
    HW_CAN_CH1_TX_IRQ_HANDLER();

    EXPECT_EQ( mock_can1_regs.sTxMailBox[0].TIR, ( 0x321U << CAN_TI0R_STID_Pos ) | CAN_TI0R_TXRQ );
    EXPECT_EQ( mock_can1_regs.sTxMailBox[0].TDTR, 3U );
    EXPECT_EQ( mock_can1_regs.sTxMailBox[0].TDLR, 0x00121110U );
    EXPECT_EQ( mock_can1_regs.sTxMailBox[0].TDHR, 0U );

    EXPECT_EQ( mock_can1_regs.sTxMailBox[1].TIR,
               ( 0x01ABCDEU << CAN_TI0R_EXID_Pos ) | CAN_TI0R_IDE | CAN_TI0R_TXRQ );
    EXPECT_EQ( mock_can1_regs.sTxMailBox[1].TDTR, 8U );
    EXPECT_EQ( mock_can1_regs.sTxMailBox[1].TDLR, 0x23222120U );
    EXPECT_EQ( mock_can1_regs.sTxMailBox[1].TDHR, 0x27262524U );

    // A remote frame carries the requested DLC but no data-field payload.
    EXPECT_EQ( mock_can1_regs.sTxMailBox[2].TIR,
               ( 0x01234567U << CAN_TI0R_EXID_Pos ) | CAN_TI0R_IDE | CAN_TI0R_RTR | CAN_TI0R_TXRQ );
    EXPECT_EQ( mock_can1_regs.sTxMailBox[2].TDTR, 4U );
    EXPECT_EQ( mock_can1_regs.sTxMailBox[2].TDLR, 0U );
    EXPECT_EQ( mock_can1_regs.sTxMailBox[2].TDHR, 0U );

    HwCanStatus_T status = ReadStatus();
    EXPECT_EQ( status.tx_queue_frame_count, 0U );
    EXPECT_EQ( status.tx_in_flight_count, 3U );
    EXPECT_EQ( status.tx_frames_submitted, 3U );
}

TEST_F( HWCANTest, TxBatchLoadingIsAllOrNothingForInvalidAndFullBatches )
{
    ConfigureAndStart();

    EXPECT_EQ( HW_CAN_Load_Tx_Buffer( HW_CAN_CHANNEL_1, nullptr, 0U ),
               HW_CAN_RESULT_INVALID_ARGUMENT );

    HwCanFrame_T initial_frames[HW_CAN_TX_QUEUE_DEPTH_FRAMES - 1U] = {};
    for ( uint32_t i = 0U; i < HW_CAN_TX_QUEUE_DEPTH_FRAMES - 1U; ++i )
    {
        initial_frames[i] = MakeFrame( 0x100U + i, 1U );
    }

    ASSERT_EQ( HW_CAN_Load_Tx_Buffer( HW_CAN_CHANNEL_1, initial_frames,
                                      HW_CAN_TX_QUEUE_DEPTH_FRAMES - 1U ),
               HW_CAN_RESULT_OK );

    // Validation happens before publication, so the valid first element cannot
    // become visible when the second element has an invalid standard ID.
    HwCanFrame_T invalid_batch[2] = {
        MakeFrame( 0x500U, 1U ),
        MakeFrame( 0x800U, 1U ),
    };
    EXPECT_EQ( HW_CAN_Load_Tx_Buffer( HW_CAN_CHANNEL_1, invalid_batch, 2U ),
               HW_CAN_RESULT_INVALID_ARGUMENT );

    HwCanStatus_T after_invalid = ReadStatus();
    EXPECT_EQ( after_invalid.tx_queue_frame_count, HW_CAN_TX_QUEUE_DEPTH_FRAMES - 1U );
    EXPECT_EQ( after_invalid.tx_frames_rejected, 2U );

    // Only one slot remains, so neither member of a two-frame valid batch may
    // be accepted. Returning QUEUE_FULL must not mean partially successful.
    HwCanFrame_T capacity_batch[2] = {
        MakeFrame( 0x501U, 2U ),
        MakeFrame( 0x502U, 2U ),
    };
    EXPECT_EQ( HW_CAN_Load_Tx_Buffer( HW_CAN_CHANNEL_1, capacity_batch, 2U ),
               HW_CAN_RESULT_QUEUE_FULL );

    HwCanStatus_T after_full = ReadStatus();
    EXPECT_EQ( after_full.tx_queue_frame_count, HW_CAN_TX_QUEUE_DEPTH_FRAMES - 1U );
    EXPECT_EQ( after_full.tx_frames_queued, HW_CAN_TX_QUEUE_DEPTH_FRAMES - 1U );
    EXPECT_EQ( after_full.tx_frames_rejected, 4U );
}

TEST_F( HWCANTest, BusyMailboxesLeavePeekedTxFrameUnconsumedUntilOwnershipTransfers )
{
    ConfigureAndStart();

    HwCanFrame_T frame = MakeFrame( 0x456U, 4U, false, false, 0x30U );
    ASSERT_EQ( HW_CAN_Load_Tx_Buffer( HW_CAN_CHANNEL_1, &frame, 1U ), HW_CAN_RESULT_OK );
    ASSERT_EQ( HW_CAN_Tx_Trigger( HW_CAN_CHANNEL_1 ), HW_CAN_RESULT_OK );

    // No TME bit means all hardware mailboxes are occupied. The ISR must return
    // without advancing the software queue tail.
    mock_can1_regs.TSR = 0U;
    HW_CAN_CH1_TX_IRQ_HANDLER();

    HwCanStatus_T busy_status = ReadStatus();
    EXPECT_EQ( busy_status.tx_queue_frame_count, 1U );
    EXPECT_EQ( busy_status.tx_in_flight_count, 0U );
    EXPECT_EQ( busy_status.tx_frames_submitted, 0U );
    EXPECT_FALSE( HW_CAN_Is_Tx_Complete( HW_CAN_CHANNEL_1 ) );

    // Once mailbox zero is free, the same peeked frame is copied and consumed.
    mock_can1_regs.TSR = CAN_TSR_TME0;
    HW_CAN_CH1_TX_IRQ_HANDLER();

    HwCanStatus_T submitted_status = ReadStatus();
    EXPECT_EQ( submitted_status.tx_queue_frame_count, 0U );
    EXPECT_EQ( submitted_status.tx_in_flight_count, 1U );
    EXPECT_EQ( submitted_status.tx_frames_submitted, 1U );
    EXPECT_EQ( mock_can1_regs.sTxMailBox[0].TIR, ( 0x456U << CAN_TI0R_STID_Pos ) | CAN_TI0R_TXRQ );
    EXPECT_EQ( mock_can1_regs.sTxMailBox[0].TDLR, 0x33323130U );
}

TEST_F( HWCANTest, TxCompletionSnapshotClassifiesEveryMailboxBeforeCombinedW1cClear )
{
    ConfigureAndStart();

    HwCanFrame_T frames[3] = {
        MakeFrame( 0x101U, 1U ),
        MakeFrame( 0x102U, 1U ),
        MakeFrame( 0x103U, 1U ),
    };
    ASSERT_EQ( HW_CAN_Load_Tx_Buffer( HW_CAN_CHANNEL_1, frames, 3U ), HW_CAN_RESULT_OK );
    HW_CAN_CH1_TX_IRQ_HANDLER();
    ASSERT_EQ( ReadStatus().tx_in_flight_count, 3U );

    // One immutable TSR snapshot contains success, arbitration loss, and bus
    // transmission error outcomes. A direct combined RQCP write must occur only
    // after every outcome has been classified.
    mock_can1_regs.TSR = CAN_TSR_TME0 | CAN_TSR_TME1 | CAN_TSR_TME2 | CAN_TSR_RQCP0 | CAN_TSR_TXOK0
                         | CAN_TSR_RQCP1 | CAN_TSR_ALST1 | CAN_TSR_RQCP2 | CAN_TSR_TERR2;
    HW_CAN_CH1_TX_IRQ_HANDLER();

    EXPECT_EQ( mock_last_tsr_write, CAN_TSR_RQCP0 | CAN_TSR_RQCP1 | CAN_TSR_RQCP2 );

    // Exercise the fourth possible RQCP outcome: no success/error bit means the
    // mailbox request was aborted rather than completing on the wire.
    HwCanFrame_T aborted_frame = MakeFrame( 0x104U, 1U );
    ASSERT_EQ( HW_CAN_Load_Tx_Buffer( HW_CAN_CHANNEL_1, &aborted_frame, 1U ), HW_CAN_RESULT_OK );
    HW_CAN_CH1_TX_IRQ_HANDLER();

    mock_can1_regs.TSR = CAN_TSR_TME0 | CAN_TSR_TME1 | CAN_TSR_TME2 | CAN_TSR_RQCP0;
    HW_CAN_CH1_TX_IRQ_HANDLER();

    HwCanStatus_T status = ReadStatus();
    EXPECT_EQ( status.tx_frames_succeeded, 1U );
    EXPECT_EQ( status.tx_arbitration_losses, 1U );
    EXPECT_EQ( status.tx_errors, 1U );
    EXPECT_EQ( status.tx_aborted, 1U );
    EXPECT_EQ( status.tx_in_flight_count, 0U );
    EXPECT_TRUE( HW_CAN_Is_Tx_Complete( HW_CAN_CHANNEL_1 ) );

    EXPECT_NE( status.latched_faults & HW_CAN_FAULT_TX_ARBITRATION_LOST, 0U );
    EXPECT_NE( status.latched_faults & HW_CAN_FAULT_TX_ERROR, 0U );
    EXPECT_NE( status.latched_faults & HW_CAN_FAULT_TX_ABORTED, 0U );
    EXPECT_EQ( mock_can1_regs.IER & CAN_IER_TMEIE, 0U );
}

/**-----------------------------------------------------------------------------
 *  Receive and Error Tests
 *------------------------------------------------------------------------------
 */

TEST_F( HWCANTest, RxPeekPreservesCompleteMetadataUntilExplicitConsume )
{
    ConfigureAndStart();

    const uint32_t identifier = 0x01ABCDEU;

    mock_can1_regs.RF0R                = 1U;
    mock_can1_regs.sFIFOMailBox[0].RIR = ( identifier << CAN_RI0R_EXID_Pos ) | CAN_RI0R_IDE;
    mock_can1_regs.sFIFOMailBox[0].RDTR =
        6U | ( 0x2AU << CAN_RDT0R_FMI_Pos ) | ( 0xBEEFU << CAN_RDT0R_TIME_Pos );
    mock_can1_regs.sFIFOMailBox[0].RDLR = 0x04030201U;
    mock_can1_regs.sFIFOMailBox[0].RDHR = 0x08070605U;

    HW_CAN_CH1_RX_IRQ_HANDLER();

    HwCanRxSpans_T spans = HW_CAN_Rx_Peek( HW_CAN_CHANNEL_1 );
    ASSERT_EQ( spans.total_frame_count, 1U );
    ASSERT_EQ( spans.first_span.frame_count, 1U );
    ASSERT_NE( spans.first_span.frames, nullptr );
    EXPECT_EQ( spans.second_span.frame_count, 0U );

    const HwCanRxFrame_T& received = spans.first_span.frames[0];
    EXPECT_EQ( received.frame.identifier, identifier );
    EXPECT_TRUE( received.frame.is_extended_id );
    EXPECT_FALSE( received.frame.is_remote_frame );
    EXPECT_EQ( received.frame.dlc, 6U );
    EXPECT_EQ( received.filter_match_index, 0x2AU );
    EXPECT_EQ( received.timestamp, 0xBEEFU );

    const uint8_t expected_payload[6] = { 1U, 2U, 3U, 4U, 5U, 6U };
    for ( uint32_t i = 0U; i < 6U; ++i )
    {
        EXPECT_EQ( received.frame.data[i], expected_payload[i] );
    }

    // Peek is non-destructive and consume refuses an over-large count without
    // changing the read pointer.
    HwCanRxSpans_T second_peek = HW_CAN_Rx_Peek( HW_CAN_CHANNEL_1 );
    EXPECT_EQ( second_peek.first_span.frames, spans.first_span.frames );
    EXPECT_EQ( HW_CAN_Rx_Consume( HW_CAN_CHANNEL_1, 2U ), HW_CAN_RESULT_INVALID_ARGUMENT );
    EXPECT_EQ( HW_CAN_Rx_Peek( HW_CAN_CHANNEL_1 ).total_frame_count, 1U );

    EXPECT_EQ( HW_CAN_Rx_Consume( HW_CAN_CHANNEL_1, 1U ), HW_CAN_RESULT_OK );
    EXPECT_EQ( HW_CAN_Rx_Peek( HW_CAN_CHANNEL_1 ).total_frame_count, 0U );
    EXPECT_EQ( mock_last_rf0r_write, CAN_RF0R_RFOM0 );
}

TEST_F( HWCANTest, RxOverflowAndSceFaultsRemainBoundedAndVisibleInStatus )
{
    ConfigureAndStart();

    // Fill every software RX slot using one hardware frame per interrupt.
    for ( uint32_t i = 0U; i < HW_CAN_RX_QUEUE_DEPTH_FRAMES; ++i )
    {
        mock_can1_regs.RF0R                 = 1U;
        mock_can1_regs.sFIFOMailBox[0].RIR  = ( ( 0x100U + i ) << CAN_RI0R_STID_Pos );
        mock_can1_regs.sFIFOMailBox[0].RDTR = 8U;
        HW_CAN_CH1_RX_IRQ_HANDLER();
    }

    // bxCAN FIFO0 can report at most three pending messages. Even though the
    // software queue is full, the ISR releases exactly those three entry-time
    // messages, counts three deliberate drops, and terminates.
    mock_can1_regs.RF0R = 3U | CAN_RF0R_FULL0 | CAN_RF0R_FOVR0;
    HW_CAN_CH1_RX_IRQ_HANDLER();

    EXPECT_EQ( mock_can1_regs.RF0R & CAN_RF0R_FMP0, 0U );

    // SCE processing takes one MSR/ESR snapshot and latches only scalar state;
    // no recovery or retry loop is permitted in this interrupt.
    mock_can1_regs.MSR = CAN_MSR_ERRI;
    mock_can1_regs.ESR = CAN_ESR_EWGF | CAN_ESR_EPVF | CAN_ESR_BOFF
                         | ( ( uint32_t )HW_CAN_LAST_ERROR_ACKNOWLEDGEMENT << CAN_ESR_LEC_Pos )
                         | ( 0x34U << CAN_ESR_TEC_Pos ) | ( 0x12U << CAN_ESR_REC_Pos );
    HW_CAN_CH1_SCE_IRQ_HANDLER();

    HwCanStatus_T status = ReadStatus();
    EXPECT_EQ( status.rx_queue_frame_count, HW_CAN_RX_QUEUE_DEPTH_FRAMES );
    EXPECT_EQ( status.rx_frames_received, HW_CAN_RX_QUEUE_DEPTH_FRAMES + 3U );
    EXPECT_EQ( status.rx_software_drops, 3U );
    EXPECT_EQ( status.rx_fifo_full_events, 1U );
    EXPECT_EQ( status.rx_fifo_overruns, 1U );

    EXPECT_TRUE( status.error_warning );
    EXPECT_TRUE( status.error_passive );
    EXPECT_TRUE( status.bus_off );
    EXPECT_EQ( status.transmit_error_count, 0x34U );
    EXPECT_EQ( status.receive_error_count, 0x12U );
    EXPECT_EQ( status.last_error, HW_CAN_LAST_ERROR_ACKNOWLEDGEMENT );
    EXPECT_EQ( status.error_warning_events, 1U );
    EXPECT_EQ( status.error_passive_events, 1U );
    EXPECT_EQ( status.bus_off_events, 1U );

    EXPECT_NE( status.latched_faults & HW_CAN_FAULT_RX_SOFTWARE_OVERFLOW, 0U );
    EXPECT_NE( status.latched_faults & HW_CAN_FAULT_RX_FIFO_FULL, 0U );
    EXPECT_NE( status.latched_faults & HW_CAN_FAULT_RX_FIFO_OVERRUN, 0U );
    EXPECT_NE( status.latched_faults & HW_CAN_FAULT_ERROR_WARNING, 0U );
    EXPECT_NE( status.latched_faults & HW_CAN_FAULT_ERROR_PASSIVE, 0U );
    EXPECT_NE( status.latched_faults & HW_CAN_FAULT_BUS_OFF, 0U );
    EXPECT_NE( status.latched_faults & HW_CAN_FAULT_PROTOCOL_ERROR, 0U );

    EXPECT_EQ( mock_last_msr_write, CAN_MSR_ERRI );
    EXPECT_EQ( mock_can1_regs.MSR & CAN_MSR_ERRI, 0U );
}
