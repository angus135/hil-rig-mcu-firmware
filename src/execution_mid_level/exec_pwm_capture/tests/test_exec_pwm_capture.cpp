/******************************************************************************
 *  File:       test_exec_pwm_capture.cpp
 *  Author:     Callum Rafferty
 *  Created:    28-Apr-2026
 *
 *  Description:
 *      Unit tests for exec_pwm_capture module.
 *
 *  Notes:
 *      Hardware PWM capture functions are mocked so tests only verify
 *      execution layer behaviour.
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>

extern "C"
{
#include "exec_pwm_capture.h"
#include "hw_pwm_capture.h"
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

static HwPWMCaptureResult_T  mock_hw_result;
static bool                  mock_configure_result;
static HwPWMCaptureChannel_T mock_last_configure_channel;
static HwPWMCaptureConfig_T  mock_last_config;

static bool                  mock_consume_called;
static HwPWMCaptureChannel_T mock_last_consumed_channel;

extern "C"
{
bool HW_PWM_Capture_Configure_Channel( HwPWMCaptureChannel_T       channel,
                                       const HwPWMCaptureConfig_T* config )
{
    mock_last_configure_channel = channel;

    if ( config != nullptr )
    {
        mock_last_config = *config;
    }

    return mock_configure_result;
}

HwPWMCaptureResult_T HW_PWM_Capture_Peek_Result( HwPWMCaptureChannel_T channel )
{
    ( void )channel;
    return mock_hw_result;
}

void HW_PWM_Capture_Consume_Result( HwPWMCaptureChannel_T channel )
{
    mock_consume_called        = true;
    mock_last_consumed_channel = channel;
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
class ExecPWMCaptureTest : public ::testing::Test
{
protected:
    uint32_t period_ticks;
    uint32_t high_ticks;

    void SetUp( void ) override
    {
        period_ticks = 0U;
        high_ticks   = 0U;

        mock_hw_result              = {};
        mock_configure_result       = true;
        mock_last_configure_channel = HW_PWM_CAPTURE_CHANNEL_1;
        mock_last_config            = {};

        mock_consume_called        = false;
        mock_last_consumed_channel = HW_PWM_CAPTURE_CHANNEL_1;
        EXEC_PWM_Capture_Test_Reset();
    }

    void TearDown( void ) override
    {
    }
};

/**-----------------------------------------------------------------------------
 *  Test Cases
 *------------------------------------------------------------------------------
 */
TEST_F( ExecPWMCaptureTest, StartChannelForwardsEnabledConfigurationToHardwareLayer )
{
    HwPWMCaptureConfig_T config = {};
    config.mode                 = HW_PWM_CAPTURE_LV_5V;
    config.is_enabled           = true;

    bool result = EXEC_PWM_Capture_Start_Channel( HW_PWM_CAPTURE_CHANNEL_2, &config );

    EXPECT_TRUE( result );
    EXPECT_EQ( mock_last_configure_channel, HW_PWM_CAPTURE_CHANNEL_2 );
    EXPECT_EQ( mock_last_config.mode, HW_PWM_CAPTURE_LV_5V );
    EXPECT_TRUE( mock_last_config.is_enabled );
}

TEST_F( ExecPWMCaptureTest, StartChannelReturnsFalseWhenHardwareConfigurationFails )
{
    HwPWMCaptureConfig_T config = {};
    config.mode                 = HW_PWM_CAPTURE_LV_3V3;
    config.is_enabled           = true;

    mock_configure_result = false;

    bool result = EXEC_PWM_Capture_Start_Channel( HW_PWM_CAPTURE_CHANNEL_1, &config );

    EXPECT_FALSE( result );
}

TEST_F( ExecPWMCaptureTest, ConsumeReturnsFalseWhenNoNewHardwareData )
{
    ExecPwmCaptureResult_T result = {};
    result.is_valid               = true;
    result.period_ticks           = 123U;
    result.high_ticks             = 45U;

    mock_hw_result.has_new_data = false;

    bool consumed = EXEC_PWM_Capture_Consume( HW_PWM_CAPTURE_CHANNEL_1, &result );

    EXPECT_FALSE( consumed );
    EXPECT_FALSE( result.is_valid );
    EXPECT_FALSE( mock_consume_called );
}

TEST_F( ExecPWMCaptureTest, ConsumeCopiesValidCaptureResult )
{
    ExecPwmCaptureResult_T result = {};

    period_ticks = 1800U;
    high_ticks   = 900U;

    mock_hw_result.has_new_data = true;
    mock_hw_result.period_ticks = &period_ticks;
    mock_hw_result.high_ticks   = &high_ticks;

    bool consumed = EXEC_PWM_Capture_Consume( HW_PWM_CAPTURE_CHANNEL_1, &result );

    EXPECT_TRUE( consumed );
    EXPECT_TRUE( result.is_valid );
    EXPECT_EQ( result.period_ticks, 1800U );
    EXPECT_EQ( result.high_ticks, 900U );
    EXPECT_TRUE( mock_consume_called );
    EXPECT_EQ( mock_last_consumed_channel, HW_PWM_CAPTURE_CHANNEL_1 );
}

TEST_F( ExecPWMCaptureTest, ConsumeReturnsFalseWhenPeriodIsZero )
{
    ExecPwmCaptureResult_T result = {};

    period_ticks = 0U;
    high_ticks   = 0U;

    mock_hw_result.has_new_data = true;
    mock_hw_result.period_ticks = &period_ticks;
    mock_hw_result.high_ticks   = &high_ticks;

    bool consumed = EXEC_PWM_Capture_Consume( HW_PWM_CAPTURE_CHANNEL_1, &result );

    EXPECT_FALSE( consumed );
    EXPECT_FALSE( result.is_valid );
    EXPECT_TRUE( mock_consume_called );
}

TEST_F( ExecPWMCaptureTest, ConsumeReturnsFalseWhenHighExceedsPeriod )
{
    ExecPwmCaptureResult_T result = {};

    period_ticks = 1000U;
    high_ticks   = 1200U;

    mock_hw_result.has_new_data = true;
    mock_hw_result.period_ticks = &period_ticks;
    mock_hw_result.high_ticks   = &high_ticks;

    bool consumed = EXEC_PWM_Capture_Consume( HW_PWM_CAPTURE_CHANNEL_1, &result );

    EXPECT_FALSE( consumed );
    EXPECT_FALSE( result.is_valid );
    EXPECT_TRUE( mock_consume_called );
}

TEST_F( ExecPWMCaptureTest, ConsumeAcceptsZeroPercentDuty )
{
    ExecPwmCaptureResult_T result = {};

    period_ticks = 1000U;
    high_ticks   = 0U;

    mock_hw_result.has_new_data = true;
    mock_hw_result.period_ticks = &period_ticks;
    mock_hw_result.high_ticks   = &high_ticks;

    bool consumed = EXEC_PWM_Capture_Consume( HW_PWM_CAPTURE_CHANNEL_1, &result );

    EXPECT_TRUE( consumed );
    EXPECT_TRUE( result.is_valid );
    EXPECT_EQ( result.period_ticks, 1000U );
    EXPECT_EQ( result.high_ticks, 0U );
}

TEST_F( ExecPWMCaptureTest, ConsumeAcceptsHundredPercentDuty )
{
    ExecPwmCaptureResult_T result = {};

    period_ticks = 1000U;
    high_ticks   = 1000U;

    mock_hw_result.has_new_data = true;
    mock_hw_result.period_ticks = &period_ticks;
    mock_hw_result.high_ticks   = &high_ticks;

    bool consumed = EXEC_PWM_Capture_Consume( HW_PWM_CAPTURE_CHANNEL_1, &result );

    EXPECT_TRUE( consumed );
    EXPECT_TRUE( result.is_valid );
    EXPECT_EQ( result.period_ticks, 1000U );
    EXPECT_EQ( result.high_ticks, 1000U );
}

TEST_F( ExecPWMCaptureTest, StartChannelReturnsFalseForNullConfig )
{
    bool result = EXEC_PWM_Capture_Start_Channel( HW_PWM_CAPTURE_CHANNEL_1, nullptr );

    EXPECT_FALSE( result );
}

TEST_F( ExecPWMCaptureTest, StartChannelReturnsFalseForInvalidChannel )
{
    HwPWMCaptureConfig_T config = {};
    config.mode                 = HW_PWM_CAPTURE_LV_3V3;
    config.is_enabled           = true;

    bool result =
        EXEC_PWM_Capture_Start_Channel( static_cast<HwPWMCaptureChannel_T>( 2U ), &config );

    EXPECT_FALSE( result );
}

TEST_F( ExecPWMCaptureTest, StartChannelReturnsFalseWhenConfigIsDisabled )
{
    HwPWMCaptureConfig_T config = {};
    config.mode                 = HW_PWM_CAPTURE_LV_3V3;
    config.is_enabled           = false;

    bool result = EXEC_PWM_Capture_Start_Channel( HW_PWM_CAPTURE_CHANNEL_1, &config );

    EXPECT_FALSE( result );
}

TEST_F( ExecPWMCaptureTest, StartChannelReturnsFalseWhenChannelAlreadyStarted )
{
    HwPWMCaptureConfig_T config = {};
    config.mode                 = HW_PWM_CAPTURE_LV_3V3;
    config.is_enabled           = true;

    EXPECT_TRUE( EXEC_PWM_Capture_Start_Channel( HW_PWM_CAPTURE_CHANNEL_1, &config ) );
    EXPECT_FALSE( EXEC_PWM_Capture_Start_Channel( HW_PWM_CAPTURE_CHANNEL_1, &config ) );
}

TEST_F( ExecPWMCaptureTest, StopChannelReturnsFalseForInvalidChannel )
{
    bool result = EXEC_PWM_Capture_Stop_Channel( static_cast<HwPWMCaptureChannel_T>( 2U ) );

    EXPECT_FALSE( result );
}

TEST_F( ExecPWMCaptureTest, StopChannelReturnsFalseWhenChannelNotStarted )
{
    bool result = EXEC_PWM_Capture_Stop_Channel( HW_PWM_CAPTURE_CHANNEL_1 );

    EXPECT_FALSE( result );
}

TEST_F( ExecPWMCaptureTest, StopChannelAppliesDisabledConfiguration )
{
    HwPWMCaptureConfig_T config = {};
    config.mode                 = HW_PWM_CAPTURE_LV_5V;
    config.is_enabled           = true;

    EXPECT_TRUE( EXEC_PWM_Capture_Start_Channel( HW_PWM_CAPTURE_CHANNEL_1, &config ) );

    bool result = EXEC_PWM_Capture_Stop_Channel( HW_PWM_CAPTURE_CHANNEL_1 );

    EXPECT_TRUE( result );
    EXPECT_EQ( mock_last_configure_channel, HW_PWM_CAPTURE_CHANNEL_1 );
    EXPECT_EQ( mock_last_config.mode, HW_PWM_CAPTURE_LV_3V3 );
    EXPECT_FALSE( mock_last_config.is_enabled );
}

TEST_F( ExecPWMCaptureTest, StopChannelReturnsFalseWhenHardwareConfigurationFails )
{
    HwPWMCaptureConfig_T config = {};
    config.mode                 = HW_PWM_CAPTURE_LV_3V3;
    config.is_enabled           = true;

    EXPECT_TRUE( EXEC_PWM_Capture_Start_Channel( HW_PWM_CAPTURE_CHANNEL_1, &config ) );

    mock_configure_result = false;

    bool result = EXEC_PWM_Capture_Stop_Channel( HW_PWM_CAPTURE_CHANNEL_1 );

    EXPECT_FALSE( result );
}

TEST_F( ExecPWMCaptureTest, StopThenStartChannelSucceeds )
{
    HwPWMCaptureConfig_T config = {};
    config.mode                 = HW_PWM_CAPTURE_LV_3V3;
    config.is_enabled           = true;

    EXPECT_TRUE( EXEC_PWM_Capture_Start_Channel( HW_PWM_CAPTURE_CHANNEL_1, &config ) );
    EXPECT_TRUE( EXEC_PWM_Capture_Stop_Channel( HW_PWM_CAPTURE_CHANNEL_1 ) );
    EXPECT_TRUE( EXEC_PWM_Capture_Start_Channel( HW_PWM_CAPTURE_CHANNEL_1, &config ) );
}
