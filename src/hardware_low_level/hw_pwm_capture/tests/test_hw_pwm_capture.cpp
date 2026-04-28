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
#include "hw_pwm_capture.h"  //Module under test
#include "hw_timer.h"
#include <stdint.h>
#include <stdbool.h>
}

/**-----------------------------------------------------------------------------
 *  Test Constants / Macros
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Test Doubles / Mocks
 *------------------------------------------------------------------------------
 */
extern "C"
{
TIM_TypeDef mock_tim2;
TIM_TypeDef mock_tim5;

void HW_TIMER_Stop_Timer( Timer_T timer )
{
    ( void )timer;
}

void HW_TIMER_Configure_Timer( Timer_T timer, uint32_t psc, uint32_t arr )
{
    ( void )timer;
    ( void )psc;
    ( void )arr;
}

void HW_TIMER_Start_Timer( Timer_T timer )
{
    ( void )timer;
}
}

/**-----------------------------------------------------------------------------
 *  Test Fixture
 *------------------------------------------------------------------------------
 */

/**
 * @brief Test fixture for module tests.
 *
 * Provides a consistent setup/teardown environment for all test cases.
 */
class HWPWMCaptureTest : public ::testing::Test
{
protected:
    void SetUp( void ) override
    {
        mock_tim2 = {};
        mock_tim5 = {};
    }

    void TearDown( void ) override
    {
    }
};

/**-----------------------------------------------------------------------------
 *  Test Cases
 *------------------------------------------------------------------------------
 */

TEST_F( HWPWMCaptureTest, ConfigureEnabledChannel1ReturnsTrue )
{
    HwPWMCaptureConfig_T config = {};
    config.mode                 = HW_PWM_CAPTURE_LV_3V3;
    config.is_enabled           = true;

    EXPECT_TRUE( HW_PWM_Capture_Configure_Channel( HW_PWM_CAPTURE_CHANNEL_1, &config ) );
}

TEST_F( HWPWMCaptureTest, ConfigureDisabledChannel1ReturnsTrue )
{
    HwPWMCaptureConfig_T config = {};
    config.mode                 = HW_PWM_CAPTURE_LV_3V3;
    config.is_enabled           = false;

    EXPECT_TRUE( HW_PWM_Capture_Configure_Channel( HW_PWM_CAPTURE_CHANNEL_1, &config ) );
}

TEST_F( HWPWMCaptureTest, ConfigureAllValidModesReturnsTrue )
{
    const HwPWMCaptureMode_T modes[] = {
        HW_PWM_CAPTURE_LV_3V3,
        HW_PWM_CAPTURE_LV_5V,
        HW_PWM_CAPTURE_HV_12V,
        HW_PWM_CAPTURE_HV_24V,
    };

    for ( HwPWMCaptureMode_T mode : modes )
    {
        HwPWMCaptureConfig_T config = {};
        config.mode                 = mode;
        config.is_enabled           = true;

        EXPECT_TRUE( HW_PWM_Capture_Configure_Channel( HW_PWM_CAPTURE_CHANNEL_1, &config ) );
    }
}

TEST_F( HWPWMCaptureTest, PeekChannel1ReturnsNoDataWhenNoNewCaptureFlag )
{
    mock_tim2.SR = 0U;

    HwPWMCaptureResult_T result = HW_PWM_Capture_Peek_Result( HW_PWM_CAPTURE_CHANNEL_1 );

    EXPECT_FALSE( result.has_new_data );
    EXPECT_EQ( result.period_ticks, nullptr );
    EXPECT_EQ( result.high_ticks, nullptr );
}

TEST_F( HWPWMCaptureTest, PeekChannel2ReturnsNoDataWhenNoNewCaptureFlag )
{
    mock_tim5.SR = 0U;

    HwPWMCaptureResult_T result = HW_PWM_Capture_Peek_Result( HW_PWM_CAPTURE_CHANNEL_2 );

    EXPECT_FALSE( result.has_new_data );
    EXPECT_EQ( result.period_ticks, nullptr );
    EXPECT_EQ( result.high_ticks, nullptr );
}

TEST_F( HWPWMCaptureTest, PeekChannel1ReturnsMappedRegisterPointersWhenNewCaptureFlag )
{
    mock_tim2.SR   = TIM_SR_CC1IF;
    mock_tim2.CCR1 = 1800U;
    mock_tim2.CCR2 = 900U;

    HwPWMCaptureResult_T result = HW_PWM_Capture_Peek_Result( HW_PWM_CAPTURE_CHANNEL_1 );

    ASSERT_TRUE( result.has_new_data );
    ASSERT_NE( result.period_ticks, nullptr );
    ASSERT_NE( result.high_ticks, nullptr );

    EXPECT_EQ( result.period_ticks, &mock_tim2.CCR1 );
    EXPECT_EQ( result.high_ticks, &mock_tim2.CCR2 );
    EXPECT_EQ( *( result.period_ticks ), 1800U );
    EXPECT_EQ( *( result.high_ticks ), 900U );
}

TEST_F( HWPWMCaptureTest, PeekChannel2ReturnsMappedRegisterPointersWhenNewCaptureFlag )
{
    mock_tim5.SR   = TIM_SR_CC2IF;
    mock_tim5.CCR2 = 3600U;
    mock_tim5.CCR1 = 1200U;

    HwPWMCaptureResult_T result = HW_PWM_Capture_Peek_Result( HW_PWM_CAPTURE_CHANNEL_2 );

    ASSERT_TRUE( result.has_new_data );
    ASSERT_NE( result.period_ticks, nullptr );
    ASSERT_NE( result.high_ticks, nullptr );

    EXPECT_EQ( result.period_ticks, &mock_tim5.CCR2 );
    EXPECT_EQ( result.high_ticks, &mock_tim5.CCR1 );
    EXPECT_EQ( *( result.period_ticks ), 3600U );
    EXPECT_EQ( *( result.high_ticks ), 1200U );
}

TEST_F( HWPWMCaptureTest, ConsumeChannel1AfterSuccessfulPeekClearsPeriodFlag )
{
    mock_tim2.SR = TIM_SR_CC1IF;

    HwPWMCaptureResult_T result = HW_PWM_Capture_Peek_Result( HW_PWM_CAPTURE_CHANNEL_1 );

    ASSERT_TRUE( result.has_new_data );

    HW_PWM_Capture_Consume_Result( HW_PWM_CAPTURE_CHANNEL_1 );

    EXPECT_EQ( mock_tim2.SR & TIM_SR_CC1IF, 0U );
}

TEST_F( HWPWMCaptureTest, ConsumeChannel2AfterSuccessfulPeekClearsPeriodFlag )
{
    mock_tim5.SR = TIM_SR_CC2IF;

    HwPWMCaptureResult_T result = HW_PWM_Capture_Peek_Result( HW_PWM_CAPTURE_CHANNEL_2 );

    ASSERT_TRUE( result.has_new_data );

    HW_PWM_Capture_Consume_Result( HW_PWM_CAPTURE_CHANNEL_2 );

    EXPECT_EQ( mock_tim5.SR & TIM_SR_CC2IF, 0U );
}

TEST_F( HWPWMCaptureTest, ConsumeChannel1PreservesOtherStatusFlags )
{
    mock_tim2.SR = TIM_SR_CC1IF | TIM_SR_CC2IF;

    HwPWMCaptureResult_T result = HW_PWM_Capture_Peek_Result( HW_PWM_CAPTURE_CHANNEL_1 );

    ASSERT_TRUE( result.has_new_data );

    HW_PWM_Capture_Consume_Result( HW_PWM_CAPTURE_CHANNEL_1 );

    EXPECT_EQ( mock_tim2.SR & TIM_SR_CC1IF, 0U );
    EXPECT_NE( mock_tim2.SR & TIM_SR_CC2IF, 0U );
}

TEST_F( HWPWMCaptureTest, ConsumeChannel2PreservesOtherStatusFlags )
{
    mock_tim5.SR = TIM_SR_CC1IF | TIM_SR_CC2IF;

    HwPWMCaptureResult_T result = HW_PWM_Capture_Peek_Result( HW_PWM_CAPTURE_CHANNEL_2 );

    ASSERT_TRUE( result.has_new_data );

    HW_PWM_Capture_Consume_Result( HW_PWM_CAPTURE_CHANNEL_2 );

    EXPECT_EQ( mock_tim5.SR & TIM_SR_CC2IF, 0U );
    EXPECT_NE( mock_tim5.SR & TIM_SR_CC1IF, 0U );
}

TEST_F( HWPWMCaptureTest, ConfigureReturnsFalseForNullConfig )
{
    EXPECT_FALSE( HW_PWM_Capture_Configure_Channel( HW_PWM_CAPTURE_CHANNEL_1, nullptr ) );
}

TEST_F( HWPWMCaptureTest, ConfigureReturnsFalseForInvalidChannel )
{
    HwPWMCaptureConfig_T config = {};
    config.mode                 = HW_PWM_CAPTURE_LV_3V3;
    config.is_enabled           = true;

    EXPECT_FALSE(
        HW_PWM_Capture_Configure_Channel( static_cast<HwPWMCaptureChannel_T>( 2U ), &config ) );
}

TEST_F( HWPWMCaptureTest, ConfigureReturnsFalseForInvalidMode )
{
    HwPWMCaptureConfig_T config = {};
    config.mode                 = static_cast<HwPWMCaptureMode_T>( 99U );
    config.is_enabled           = true;

    EXPECT_FALSE( HW_PWM_Capture_Configure_Channel( HW_PWM_CAPTURE_CHANNEL_1, &config ) );
}

TEST_F( HWPWMCaptureTest, PeekDoesNotClearFlag )
{
    mock_tim2.SR = TIM_SR_CC1IF;

    auto result1 = HW_PWM_Capture_Peek_Result( HW_PWM_CAPTURE_CHANNEL_1 );
    auto result2 = HW_PWM_Capture_Peek_Result( HW_PWM_CAPTURE_CHANNEL_1 );

    EXPECT_TRUE( result1.has_new_data );
    EXPECT_TRUE( result2.has_new_data );

    EXPECT_EQ( mock_tim2.SR & TIM_SR_CC1IF, TIM_SR_CC1IF );
}