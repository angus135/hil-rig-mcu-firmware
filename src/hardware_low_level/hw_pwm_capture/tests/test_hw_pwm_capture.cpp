/******************************************************************************
 *  File:       test_hw_pwm_capture.cpp
 *  Author:     Callum Rafferty
 *  Created:    28-Apr-2026
 *
 *  Description:
 *
 *  Notes:
 *
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>

extern "C"
{
#include "hw_pwm_capture_mocks.h"
#include "hw_pwm_capture.h"
#include "hw_timer.h"
#include <stdint.h>
#include <stdbool.h>
}

using ::testing::_;

/**-----------------------------------------------------------------------------
 *  Test Constants / Macros
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Test Doubles / Mocks
 *------------------------------------------------------------------------------
 */

class MockHwTimer
{
public:
    MOCK_METHOD( void, Stop_Timer, ( Timer_T ) );
    MOCK_METHOD( void, Configure_Timer, ( Timer_T, uint32_t, uint32_t ) );
    MOCK_METHOD( bool, Start_Timer, ( Timer_T ) );
    MOCK_METHOD( uint32_t, Get_Clock_Hz, ( Timer_T ) );
};

static MockHwTimer* g_mock_timer = nullptr;

extern "C"
{
TIM_TypeDef mock_tim2;
TIM_TypeDef mock_tim5;

void HW_TIMER_Stop_Timer( Timer_T timer )
{
    g_mock_timer->Stop_Timer( timer );
}

void HW_TIMER_Configure_Timer( Timer_T timer, uint32_t psc, uint32_t arr )
{
    g_mock_timer->Configure_Timer( timer, psc, arr );
}

bool HW_TIMER_Start_Timer( Timer_T timer )
{
    return g_mock_timer->Start_Timer( timer );
}

uint32_t HW_TIMER_Get_Clock_Hz( Timer_T timer )
{
    return g_mock_timer->Get_Clock_Hz( timer );
}
}

/**-----------------------------------------------------------------------------
 *  Test Fixture
 *------------------------------------------------------------------------------
 */

class HWPWMCaptureTest : public ::testing::Test
{
protected:
    MockHwTimer mock_timer;

    void SetUp( void ) override
    {
        g_mock_timer = &mock_timer;
        ON_CALL( mock_timer, Start_Timer( _ ) ).WillByDefault( testing::Return( true ) );
        mock_tim2 = {};
        mock_tim5 = {};

        EXPECT_CALL( mock_timer, Stop_Timer( PWM_CAPTURE_TIMER_CH1 ) );
        EXPECT_CALL( mock_timer, Stop_Timer( PWM_CAPTURE_TIMER_CH2 ) );
        ASSERT_TRUE( HW_PWM_Capture_Configure_Channel( HW_PWM_CAPTURE_CHANNEL_1, false ) );
        ASSERT_TRUE( HW_PWM_Capture_Configure_Channel( HW_PWM_CAPTURE_CHANNEL_2, false ) );
        testing::Mock::VerifyAndClearExpectations( &mock_timer );
    }

    void TearDown( void ) override
    {
        g_mock_timer = nullptr;
    }
};

/**-----------------------------------------------------------------------------
 *  Test Cases
 *------------------------------------------------------------------------------
 */

TEST_F( HWPWMCaptureTest, ConfigureEnabledChannel1ReturnsTrue )
{
    testing::InSequence seq;
    EXPECT_CALL( mock_timer, Stop_Timer( PWM_CAPTURE_TIMER_CH1 ) );
    EXPECT_CALL( mock_timer, Configure_Timer( PWM_CAPTURE_TIMER_CH1, 0U, 0xFFFFFFFFU ) );
    EXPECT_CALL( mock_timer, Get_Clock_Hz( PWM_CAPTURE_TIMER_CH1 ) )
        .WillOnce( testing::Return( 1000000U ) );
    EXPECT_CALL( mock_timer, Start_Timer( _ ) ).Times( 0 );

    EXPECT_TRUE( HW_PWM_Capture_Configure_Channel( HW_PWM_CAPTURE_CHANNEL_1, true ) );
    EXPECT_EQ( HW_PWM_Capture_Get_Timer_Clock_Hz( HW_PWM_CAPTURE_CHANNEL_1 ), 1000000U );
}

TEST_F( HWPWMCaptureTest, ConfigureDisabledChannel1ReturnsTrue )
{
    EXPECT_CALL( mock_timer, Stop_Timer( _ ) );
    EXPECT_CALL( mock_timer, Configure_Timer( _, _, _ ) ).Times( 0 );
    EXPECT_CALL( mock_timer, Start_Timer( _ ) ).Times( 0 );
    EXPECT_CALL( mock_timer, Get_Clock_Hz( _ ) ).Times( 0 );

    EXPECT_TRUE( HW_PWM_Capture_Configure_Channel( HW_PWM_CAPTURE_CHANNEL_1, false ) );
}

TEST_F( HWPWMCaptureTest, StartConfiguredChannelStartsTimer )
{
    EXPECT_CALL( mock_timer, Stop_Timer( PWM_CAPTURE_TIMER_CH1 ) );
    EXPECT_CALL( mock_timer, Configure_Timer( PWM_CAPTURE_TIMER_CH1, 0U, 0xFFFFFFFFU ) );
    EXPECT_CALL( mock_timer, Get_Clock_Hz( PWM_CAPTURE_TIMER_CH1 ) )
        .WillOnce( testing::Return( 1000000U ) );
    EXPECT_TRUE( HW_PWM_Capture_Configure_Channel( HW_PWM_CAPTURE_CHANNEL_1, true ) );

    EXPECT_CALL( mock_timer, Start_Timer( PWM_CAPTURE_TIMER_CH1 ) );
    EXPECT_TRUE( HW_PWM_Capture_Start_Channel( HW_PWM_CAPTURE_CHANNEL_1 ) );
}

TEST_F( HWPWMCaptureTest, StartReturnsFalseWhenChannelIsNotConfigured )
{
    EXPECT_CALL( mock_timer, Start_Timer( _ ) ).Times( 0 );

    EXPECT_FALSE( HW_PWM_Capture_Start_Channel( HW_PWM_CAPTURE_CHANNEL_1 ) );
}

TEST_F( HWPWMCaptureTest, TimerStartFailureLeavesBothChannelsConfiguredAndRetryable )
{
    const HwPWMCaptureChannel_T channels[] = { HW_PWM_CAPTURE_CHANNEL_1, HW_PWM_CAPTURE_CHANNEL_2 };
    const Timer_T               timers[]   = { PWM_CAPTURE_TIMER_CH1, PWM_CAPTURE_TIMER_CH2 };
    for ( unsigned int i = 0U; i < 2U; ++i )
    {
        EXPECT_CALL( mock_timer, Stop_Timer( timers[i] ) );
        EXPECT_CALL( mock_timer, Configure_Timer( timers[i], _, _ ) );
        EXPECT_CALL( mock_timer, Get_Clock_Hz( timers[i] ) )
            .WillOnce( testing::Return( 1000000U ) );
        ASSERT_TRUE( HW_PWM_Capture_Configure_Channel( channels[i], true ) );

        EXPECT_CALL( mock_timer, Start_Timer( timers[i] ) )
            .WillOnce( testing::Return( false ) )
            .WillOnce( testing::Return( true ) );
        EXPECT_FALSE( HW_PWM_Capture_Start_Channel( channels[i] ) );
        EXPECT_FALSE( HW_PWM_Capture_Stop_Channel( channels[i] ) );
        EXPECT_EQ( HW_PWM_Capture_Get_Timer_Clock_Hz( channels[i] ), 1000000U );
        EXPECT_TRUE( HW_PWM_Capture_Start_Channel( channels[i] ) );
        EXPECT_FALSE( HW_PWM_Capture_Start_Channel( channels[i] ) );
    }
}

TEST_F( HWPWMCaptureTest, StartReturnsFalseWhenChannelIsAlreadyStarted )
{
    EXPECT_CALL( mock_timer, Stop_Timer( PWM_CAPTURE_TIMER_CH1 ) );
    EXPECT_CALL( mock_timer, Configure_Timer( PWM_CAPTURE_TIMER_CH1, _, _ ) );
    EXPECT_CALL( mock_timer, Get_Clock_Hz( PWM_CAPTURE_TIMER_CH1 ) )
        .WillOnce( testing::Return( 1000000U ) );
    ASSERT_TRUE( HW_PWM_Capture_Configure_Channel( HW_PWM_CAPTURE_CHANNEL_1, true ) );

    EXPECT_CALL( mock_timer, Start_Timer( PWM_CAPTURE_TIMER_CH1 ) ).Times( 1 );
    ASSERT_TRUE( HW_PWM_Capture_Start_Channel( HW_PWM_CAPTURE_CHANNEL_1 ) );
    EXPECT_FALSE( HW_PWM_Capture_Start_Channel( HW_PWM_CAPTURE_CHANNEL_1 ) );
}

TEST_F( HWPWMCaptureTest, StopStartedChannelStopsTimerAndRetainsConfiguration )
{
    EXPECT_CALL( mock_timer, Stop_Timer( PWM_CAPTURE_TIMER_CH1 ) );
    EXPECT_CALL( mock_timer, Configure_Timer( PWM_CAPTURE_TIMER_CH1, _, _ ) );
    EXPECT_CALL( mock_timer, Get_Clock_Hz( PWM_CAPTURE_TIMER_CH1 ) )
        .WillOnce( testing::Return( 1000000U ) );
    ASSERT_TRUE( HW_PWM_Capture_Configure_Channel( HW_PWM_CAPTURE_CHANNEL_1, true ) );

    EXPECT_CALL( mock_timer, Start_Timer( PWM_CAPTURE_TIMER_CH1 ) ).Times( 2 );
    ASSERT_TRUE( HW_PWM_Capture_Start_Channel( HW_PWM_CAPTURE_CHANNEL_1 ) );

    EXPECT_CALL( mock_timer, Stop_Timer( PWM_CAPTURE_TIMER_CH1 ) );
    EXPECT_TRUE( HW_PWM_Capture_Stop_Channel( HW_PWM_CAPTURE_CHANNEL_1 ) );
    EXPECT_EQ( HW_PWM_Capture_Get_Timer_Clock_Hz( HW_PWM_CAPTURE_CHANNEL_1 ), 1000000U );
    EXPECT_TRUE( HW_PWM_Capture_Start_Channel( HW_PWM_CAPTURE_CHANNEL_1 ) );
}

TEST_F( HWPWMCaptureTest, StopReturnsFalseWhenChannelIsNotStarted )
{
    EXPECT_CALL( mock_timer, Stop_Timer( _ ) ).Times( 0 );

    EXPECT_FALSE( HW_PWM_Capture_Stop_Channel( HW_PWM_CAPTURE_CHANNEL_1 ) );
}

TEST_F( HWPWMCaptureTest, DisableStartedChannelStopsTimerAndClearsConfiguration )
{
    EXPECT_CALL( mock_timer, Stop_Timer( PWM_CAPTURE_TIMER_CH1 ) );
    EXPECT_CALL( mock_timer, Configure_Timer( PWM_CAPTURE_TIMER_CH1, _, _ ) );
    EXPECT_CALL( mock_timer, Get_Clock_Hz( PWM_CAPTURE_TIMER_CH1 ) )
        .WillOnce( testing::Return( 1000000U ) );
    ASSERT_TRUE( HW_PWM_Capture_Configure_Channel( HW_PWM_CAPTURE_CHANNEL_1, true ) );
    EXPECT_CALL( mock_timer, Start_Timer( PWM_CAPTURE_TIMER_CH1 ) );
    ASSERT_TRUE( HW_PWM_Capture_Start_Channel( HW_PWM_CAPTURE_CHANNEL_1 ) );

    EXPECT_CALL( mock_timer, Stop_Timer( PWM_CAPTURE_TIMER_CH1 ) );
    EXPECT_TRUE( HW_PWM_Capture_Configure_Channel( HW_PWM_CAPTURE_CHANNEL_1, false ) );
    EXPECT_EQ( HW_PWM_Capture_Get_Timer_Clock_Hz( HW_PWM_CAPTURE_CHANNEL_1 ), 0U );
    EXPECT_FALSE( HW_PWM_Capture_Start_Channel( HW_PWM_CAPTURE_CHANNEL_1 ) );
}

TEST_F( HWPWMCaptureTest, ConfigureReturnsFalseForInvalidChannel )
{
    EXPECT_CALL( mock_timer, Stop_Timer( _ ) ).Times( 0 );

    EXPECT_FALSE(
        HW_PWM_Capture_Configure_Channel( static_cast<HwPWMCaptureChannel_T>( 2U ), true ) );
}

TEST_F( HWPWMCaptureTest, StartAndStopReturnFalseForInvalidChannel )
{
    const auto invalid_channel = static_cast<HwPWMCaptureChannel_T>( 2U );
    EXPECT_CALL( mock_timer, Start_Timer( _ ) ).Times( 0 );
    EXPECT_CALL( mock_timer, Stop_Timer( _ ) ).Times( 0 );

    EXPECT_FALSE( HW_PWM_Capture_Start_Channel( invalid_channel ) );
    EXPECT_FALSE( HW_PWM_Capture_Stop_Channel( invalid_channel ) );
}

TEST_F( HWPWMCaptureTest, ReadSnapshotReturnsFalseWhenChannelNotStarted )
{
    mock_tim2.SR                    = TIM_SR_CC1IF;
    HwPWMCaptureSnapshot_T snapshot = {};

    EXPECT_FALSE( HW_PWM_Capture_Read_Snapshot( HW_PWM_CAPTURE_CHANNEL_1, &snapshot ) );
    EXPECT_FALSE( snapshot.has_new_data );
}

TEST_F( HWPWMCaptureTest, ReadSnapshotReturnsFalseForNullPointerOrInvalidChannel )
{
    EXPECT_FALSE( HW_PWM_Capture_Read_Snapshot( HW_PWM_CAPTURE_CHANNEL_1, nullptr ) );
    HwPWMCaptureSnapshot_T snapshot = {};
    EXPECT_FALSE(
        HW_PWM_Capture_Read_Snapshot( static_cast<HwPWMCaptureChannel_T>( 2U ), &snapshot ) );
}

TEST_F( HWPWMCaptureTest, ReadSnapshotChannel1ReturnsNoDataWhenNoNewCaptureFlag )
{
    EXPECT_CALL( mock_timer, Configure_Timer( PWM_CAPTURE_TIMER_CH1, _, _ ) );
    EXPECT_CALL( mock_timer, Get_Clock_Hz( PWM_CAPTURE_TIMER_CH1 ) )
        .WillOnce( testing::Return( 1000000U ) );
    ASSERT_TRUE( HW_PWM_Capture_Configure_Channel( HW_PWM_CAPTURE_CHANNEL_1, true ) );
    EXPECT_CALL( mock_timer, Start_Timer( PWM_CAPTURE_TIMER_CH1 ) );
    ASSERT_TRUE( HW_PWM_Capture_Start_Channel( HW_PWM_CAPTURE_CHANNEL_1 ) );

    mock_tim2.SR = 0U;

    HwPWMCaptureSnapshot_T snapshot = {};
    EXPECT_FALSE( HW_PWM_Capture_Read_Snapshot( HW_PWM_CAPTURE_CHANNEL_1, &snapshot ) );
    EXPECT_FALSE( snapshot.has_new_data );
    EXPECT_FALSE( snapshot.is_overrun );
    EXPECT_EQ( snapshot.period_ticks, 0U );
    EXPECT_EQ( snapshot.high_ticks, 0U );
}

TEST_F( HWPWMCaptureTest, ReadSnapshotChannel2ReturnsNoDataWhenNoNewCaptureFlag )
{
    EXPECT_CALL( mock_timer, Configure_Timer( PWM_CAPTURE_TIMER_CH2, _, _ ) );
    EXPECT_CALL( mock_timer, Get_Clock_Hz( PWM_CAPTURE_TIMER_CH2 ) )
        .WillOnce( testing::Return( 1000000U ) );
    ASSERT_TRUE( HW_PWM_Capture_Configure_Channel( HW_PWM_CAPTURE_CHANNEL_2, true ) );
    EXPECT_CALL( mock_timer, Start_Timer( PWM_CAPTURE_TIMER_CH2 ) );
    ASSERT_TRUE( HW_PWM_Capture_Start_Channel( HW_PWM_CAPTURE_CHANNEL_2 ) );

    mock_tim5.SR = 0U;

    HwPWMCaptureSnapshot_T snapshot = {};
    EXPECT_FALSE( HW_PWM_Capture_Read_Snapshot( HW_PWM_CAPTURE_CHANNEL_2, &snapshot ) );
    EXPECT_FALSE( snapshot.has_new_data );
    EXPECT_FALSE( snapshot.is_overrun );
    EXPECT_EQ( snapshot.period_ticks, 0U );
    EXPECT_EQ( snapshot.high_ticks, 0U );
}

TEST_F( HWPWMCaptureTest, ReadSnapshotChannel1ReturnsValuesAndClearsFlag )
{
    EXPECT_CALL( mock_timer, Configure_Timer( PWM_CAPTURE_TIMER_CH1, _, _ ) );
    EXPECT_CALL( mock_timer, Get_Clock_Hz( PWM_CAPTURE_TIMER_CH1 ) )
        .WillOnce( testing::Return( 1000000U ) );
    ASSERT_TRUE( HW_PWM_Capture_Configure_Channel( HW_PWM_CAPTURE_CHANNEL_1, true ) );
    EXPECT_CALL( mock_timer, Start_Timer( PWM_CAPTURE_TIMER_CH1 ) );
    ASSERT_TRUE( HW_PWM_Capture_Start_Channel( HW_PWM_CAPTURE_CHANNEL_1 ) );

    mock_tim2.SR   = TIM_SR_CC1IF;
    mock_tim2.CCR1 = 1800U;
    mock_tim2.CCR2 = 900U;

    HwPWMCaptureSnapshot_T snapshot = {};
    ASSERT_TRUE( HW_PWM_Capture_Read_Snapshot( HW_PWM_CAPTURE_CHANNEL_1, &snapshot ) );

    EXPECT_TRUE( snapshot.has_new_data );
    EXPECT_FALSE( snapshot.is_overrun );
    EXPECT_EQ( snapshot.period_ticks, 1800U );
    EXPECT_EQ( snapshot.high_ticks, 900U );
    EXPECT_EQ( mock_tim2.SR & TIM_SR_CC1IF, 0U );
}

TEST_F( HWPWMCaptureTest, ReadSnapshotChannel2ReturnsValuesAndClearsFlag )
{
    EXPECT_CALL( mock_timer, Configure_Timer( PWM_CAPTURE_TIMER_CH2, _, _ ) );
    EXPECT_CALL( mock_timer, Get_Clock_Hz( PWM_CAPTURE_TIMER_CH2 ) )
        .WillOnce( testing::Return( 1000000U ) );
    ASSERT_TRUE( HW_PWM_Capture_Configure_Channel( HW_PWM_CAPTURE_CHANNEL_2, true ) );
    EXPECT_CALL( mock_timer, Start_Timer( PWM_CAPTURE_TIMER_CH2 ) );
    ASSERT_TRUE( HW_PWM_Capture_Start_Channel( HW_PWM_CAPTURE_CHANNEL_2 ) );

    mock_tim5.SR   = TIM_SR_CC2IF;
    mock_tim5.CCR2 = 3600U;
    mock_tim5.CCR1 = 1200U;

    HwPWMCaptureSnapshot_T snapshot = {};
    ASSERT_TRUE( HW_PWM_Capture_Read_Snapshot( HW_PWM_CAPTURE_CHANNEL_2, &snapshot ) );

    EXPECT_TRUE( snapshot.has_new_data );
    EXPECT_FALSE( snapshot.is_overrun );
    EXPECT_EQ( snapshot.period_ticks, 3600U );
    EXPECT_EQ( snapshot.high_ticks, 1200U );
    EXPECT_EQ( mock_tim5.SR & TIM_SR_CC2IF, 0U );
}

TEST_F( HWPWMCaptureTest, ReadSnapshotDetectsOverrunAndClearsOverrunFlag )
{
    EXPECT_CALL( mock_timer, Configure_Timer( PWM_CAPTURE_TIMER_CH1, _, _ ) );
    EXPECT_CALL( mock_timer, Get_Clock_Hz( PWM_CAPTURE_TIMER_CH1 ) )
        .WillOnce( testing::Return( 1000000U ) );
    ASSERT_TRUE( HW_PWM_Capture_Configure_Channel( HW_PWM_CAPTURE_CHANNEL_1, true ) );
    EXPECT_CALL( mock_timer, Start_Timer( PWM_CAPTURE_TIMER_CH1 ) );
    ASSERT_TRUE( HW_PWM_Capture_Start_Channel( HW_PWM_CAPTURE_CHANNEL_1 ) );

    mock_tim2.SR   = TIM_SR_CC1IF | TIM_SR_CC1OF;
    mock_tim2.CCR1 = 2000U;
    mock_tim2.CCR2 = 500U;

    HwPWMCaptureSnapshot_T snapshot = {};
    ASSERT_TRUE( HW_PWM_Capture_Read_Snapshot( HW_PWM_CAPTURE_CHANNEL_1, &snapshot ) );

    EXPECT_TRUE( snapshot.has_new_data );
    EXPECT_TRUE( snapshot.is_overrun );
    EXPECT_EQ( snapshot.period_ticks, 2000U );
    EXPECT_EQ( snapshot.high_ticks, 500U );
    EXPECT_EQ( mock_tim2.SR & ( TIM_SR_CC1IF | TIM_SR_CC1OF ), 0U );
}

TEST_F( HWPWMCaptureTest, ReadSnapshotPreservesOtherStatusFlags )
{
    EXPECT_CALL( mock_timer, Configure_Timer( PWM_CAPTURE_TIMER_CH1, _, _ ) );
    EXPECT_CALL( mock_timer, Get_Clock_Hz( PWM_CAPTURE_TIMER_CH1 ) )
        .WillOnce( testing::Return( 1000000U ) );
    ASSERT_TRUE( HW_PWM_Capture_Configure_Channel( HW_PWM_CAPTURE_CHANNEL_1, true ) );
    EXPECT_CALL( mock_timer, Start_Timer( PWM_CAPTURE_TIMER_CH1 ) );
    ASSERT_TRUE( HW_PWM_Capture_Start_Channel( HW_PWM_CAPTURE_CHANNEL_1 ) );

    mock_tim2.SR = TIM_SR_CC1IF | TIM_SR_CC2IF;

    HwPWMCaptureSnapshot_T snapshot = {};
    ASSERT_TRUE( HW_PWM_Capture_Read_Snapshot( HW_PWM_CAPTURE_CHANNEL_1, &snapshot ) );

    EXPECT_EQ( mock_tim2.SR & TIM_SR_CC1IF, 0U );
    EXPECT_NE( mock_tim2.SR & TIM_SR_CC2IF, 0U );
}
