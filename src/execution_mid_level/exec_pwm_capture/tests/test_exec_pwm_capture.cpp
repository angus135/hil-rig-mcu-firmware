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
#include "exec_pwm_capture.c"

extern "C"
{
#include "hw_pwm_capture.h"
#include <stdint.h>
#include <stdbool.h>
}

using ::testing::_;
using ::testing::AllOf;
using ::testing::Field;
using ::testing::Pointee;
using ::testing::Return;

/**-----------------------------------------------------------------------------
 *  Test Constants / Macros
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Test Doubles / Mocks
 *------------------------------------------------------------------------------
 */

class MockHwPwmCapture
{
public:
    MOCK_METHOD( bool, Configure_Channel, ( HwPWMCaptureChannel_T, const HwPWMCaptureConfig_T* ));
    MOCK_METHOD( HwPWMCaptureResult_T, Peek_Result, ( HwPWMCaptureChannel_T ) );
    MOCK_METHOD( void, Consume_Result, ( HwPWMCaptureChannel_T ) );
    MOCK_METHOD( uint32_t, Get_Timer_Clock_Hz, ( HwPWMCaptureChannel_T ) );
};

static MockHwPwmCapture* g_mock_hw = nullptr;

static void Reset_Exec_PWM_Capture_State( void )
{
    for ( uint32_t i = 0U; i < EXEC_PWM_CAPTURE_CHANNEL_COUNT; i++ )
    {
        exec_pwm_capture_channel_started[i] = false;
    }
}

extern "C"
{
bool HW_PWM_Capture_Configure_Channel( HwPWMCaptureChannel_T       channel,
                                       const HwPWMCaptureConfig_T* config )
{
    return g_mock_hw->Configure_Channel( channel, config );
}

HwPWMCaptureResult_T HW_PWM_Capture_Peek_Result( HwPWMCaptureChannel_T channel )
{
    return g_mock_hw->Peek_Result( channel );
}

void HW_PWM_Capture_Consume_Result( HwPWMCaptureChannel_T channel )
{
    g_mock_hw->Consume_Result( channel );
}

uint32_t HW_PWM_Capture_Get_Timer_Clock_Hz( HwPWMCaptureChannel_T channel )
{
    return g_mock_hw->Get_Timer_Clock_Hz( channel );
}
}

/**-----------------------------------------------------------------------------
 *  Test Fixture
 *------------------------------------------------------------------------------
 */

class ExecPWMCaptureTest : public ::testing::Test
{
protected:
    MockHwPwmCapture mock_hw;
    uint32_t         period_ticks;
    uint32_t         high_ticks;

    void SetUp( void ) override
    {
        g_mock_hw    = &mock_hw;
        period_ticks = 0U;
        high_ticks   = 0U;
        Reset_Exec_PWM_Capture_State();
    }

    void TearDown( void ) override
    {
        g_mock_hw = nullptr;
    }

    HwPWMCaptureResult_T MakeHwResult( uint32_t* period, uint32_t* high )
    {
        HwPWMCaptureResult_T hw_result = {};
        hw_result.has_new_data         = true;
        hw_result.period_ticks         = period;
        hw_result.high_ticks           = high;
        return hw_result;
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

    EXPECT_CALL( mock_hw,
                 Configure_Channel(
                     HW_PWM_CAPTURE_CHANNEL_2,
                     Pointee( AllOf( Field( &HwPWMCaptureConfig_T::mode, HW_PWM_CAPTURE_LV_5V ),
                                     Field( &HwPWMCaptureConfig_T::is_enabled, true ) ) ) ) )
        .WillOnce( Return( true ) );

    EXPECT_TRUE( EXEC_PWM_Capture_Start_Channel( HW_PWM_CAPTURE_CHANNEL_2, &config ) );
}

TEST_F( ExecPWMCaptureTest, StartChannelReturnsFalseWhenHardwareConfigurationFails )
{
    HwPWMCaptureConfig_T config = {};
    config.mode                 = HW_PWM_CAPTURE_LV_3V3;
    config.is_enabled           = true;

    EXPECT_CALL( mock_hw, Configure_Channel( _, _ ) ).WillOnce( Return( false ) );

    EXPECT_FALSE( EXEC_PWM_Capture_Start_Channel( HW_PWM_CAPTURE_CHANNEL_1, &config ) );
}

TEST_F( ExecPWMCaptureTest, StartChannelReturnsFalseForNullConfig )
{
    EXPECT_CALL( mock_hw, Configure_Channel( _, _ ) ).Times( 0 );

    EXPECT_FALSE( EXEC_PWM_Capture_Start_Channel( HW_PWM_CAPTURE_CHANNEL_1, nullptr ) );
}

TEST_F( ExecPWMCaptureTest, StartChannelReturnsFalseForInvalidChannel )
{
    HwPWMCaptureConfig_T config = {};
    config.mode                 = HW_PWM_CAPTURE_LV_3V3;
    config.is_enabled           = true;

    EXPECT_CALL( mock_hw, Configure_Channel( _, _ ) ).Times( 0 );

    EXPECT_FALSE(
        EXEC_PWM_Capture_Start_Channel( static_cast<HwPWMCaptureChannel_T>( 2U ), &config ) );
}

TEST_F( ExecPWMCaptureTest, StartChannelReturnsFalseWhenConfigIsDisabled )
{
    HwPWMCaptureConfig_T config = {};
    config.mode                 = HW_PWM_CAPTURE_LV_3V3;
    config.is_enabled           = false;

    EXPECT_CALL( mock_hw, Configure_Channel( _, _ ) ).Times( 0 );

    EXPECT_FALSE( EXEC_PWM_Capture_Start_Channel( HW_PWM_CAPTURE_CHANNEL_1, &config ) );
}

TEST_F( ExecPWMCaptureTest, StartChannelReturnsFalseWhenChannelAlreadyStarted )
{
    HwPWMCaptureConfig_T config = {};
    config.mode                 = HW_PWM_CAPTURE_LV_3V3;
    config.is_enabled           = true;

    EXPECT_CALL( mock_hw, Configure_Channel( _, _ ) ).WillOnce( Return( true ) );

    EXPECT_TRUE( EXEC_PWM_Capture_Start_Channel( HW_PWM_CAPTURE_CHANNEL_1, &config ) );
    EXPECT_FALSE( EXEC_PWM_Capture_Start_Channel( HW_PWM_CAPTURE_CHANNEL_1, &config ) );
}

TEST_F( ExecPWMCaptureTest, StopChannelAppliesDisabledConfiguration )
{
    HwPWMCaptureConfig_T config = {};
    config.mode                 = HW_PWM_CAPTURE_LV_5V;
    config.is_enabled           = true;

    EXPECT_CALL( mock_hw, Configure_Channel( HW_PWM_CAPTURE_CHANNEL_1, _ ) )
        .WillOnce( Return( true ) );

    EXPECT_TRUE( EXEC_PWM_Capture_Start_Channel( HW_PWM_CAPTURE_CHANNEL_1, &config ) );

    EXPECT_CALL( mock_hw,
                 Configure_Channel(
                     HW_PWM_CAPTURE_CHANNEL_1,
                     Pointee( AllOf( Field( &HwPWMCaptureConfig_T::mode, HW_PWM_CAPTURE_LV_3V3 ),
                                     Field( &HwPWMCaptureConfig_T::is_enabled, false ) ) ) ) )
        .WillOnce( Return( true ) );

    EXPECT_TRUE( EXEC_PWM_Capture_Stop_Channel( HW_PWM_CAPTURE_CHANNEL_1 ) );
}

TEST_F( ExecPWMCaptureTest, StopChannelReturnsFalseForInvalidChannel )
{
    EXPECT_CALL( mock_hw, Configure_Channel( _, _ ) ).Times( 0 );

    EXPECT_FALSE( EXEC_PWM_Capture_Stop_Channel( static_cast<HwPWMCaptureChannel_T>( 2U ) ) );
}

TEST_F( ExecPWMCaptureTest, StopChannelReturnsFalseWhenChannelNotStarted )
{
    EXPECT_CALL( mock_hw, Configure_Channel( _, _ ) ).Times( 0 );

    EXPECT_FALSE( EXEC_PWM_Capture_Stop_Channel( HW_PWM_CAPTURE_CHANNEL_1 ) );
}

TEST_F( ExecPWMCaptureTest, StopChannelReturnsFalseWhenHardwareConfigurationFails )
{
    HwPWMCaptureConfig_T config = {};
    config.mode                 = HW_PWM_CAPTURE_LV_3V3;
    config.is_enabled           = true;

    EXPECT_CALL( mock_hw, Configure_Channel( _, _ ) ).WillOnce( Return( true ) );

    EXPECT_TRUE( EXEC_PWM_Capture_Start_Channel( HW_PWM_CAPTURE_CHANNEL_1, &config ) );

    EXPECT_CALL( mock_hw, Configure_Channel( _, _ ) ).WillOnce( Return( false ) );

    EXPECT_FALSE( EXEC_PWM_Capture_Stop_Channel( HW_PWM_CAPTURE_CHANNEL_1 ) );
}

TEST_F( ExecPWMCaptureTest, StopThenStartChannelSucceeds )
{
    HwPWMCaptureConfig_T config = {};
    config.mode                 = HW_PWM_CAPTURE_LV_3V3;
    config.is_enabled           = true;

    EXPECT_CALL( mock_hw, Configure_Channel( _, _ ) ).WillRepeatedly( Return( true ) );

    EXPECT_TRUE( EXEC_PWM_Capture_Start_Channel( HW_PWM_CAPTURE_CHANNEL_1, &config ) );
    EXPECT_TRUE( EXEC_PWM_Capture_Stop_Channel( HW_PWM_CAPTURE_CHANNEL_1 ) );
    EXPECT_TRUE( EXEC_PWM_Capture_Start_Channel( HW_PWM_CAPTURE_CHANNEL_1, &config ) );
}

TEST_F( ExecPWMCaptureTest, ConsumeReturnsFalseWhenNoNewHardwareData )
{
    ExecPwmCaptureResult_T result = {};

    EXPECT_CALL( mock_hw, Peek_Result( HW_PWM_CAPTURE_CHANNEL_1 ) )
        .WillOnce( Return( HwPWMCaptureResult_T{} ) );
    EXPECT_CALL( mock_hw, Consume_Result( _ ) ).Times( 0 );

    EXPECT_FALSE( EXEC_PWM_Capture_Consume( HW_PWM_CAPTURE_CHANNEL_1, &result ) );
    EXPECT_FALSE( result.is_valid );
}

TEST_F( ExecPWMCaptureTest, ConsumeCopiesValidCaptureResult )
{
    ExecPwmCaptureResult_T result = {};
    period_ticks                  = 1800U;
    high_ticks                    = 900U;

    EXPECT_CALL( mock_hw, Peek_Result( HW_PWM_CAPTURE_CHANNEL_1 ) )
        .WillOnce( Return( MakeHwResult( &period_ticks, &high_ticks ) ) );
    EXPECT_CALL( mock_hw, Consume_Result( HW_PWM_CAPTURE_CHANNEL_1 ) ).Times( 1 );

    EXPECT_TRUE( EXEC_PWM_Capture_Consume( HW_PWM_CAPTURE_CHANNEL_1, &result ) );
    EXPECT_TRUE( result.is_valid );
    EXPECT_EQ( result.period_ticks, 1800U );
    EXPECT_EQ( result.high_ticks, 900U );
}

TEST_F( ExecPWMCaptureTest, ConsumeReturnsFalseWhenPeriodIsZero )
{
    ExecPwmCaptureResult_T result = {};
    period_ticks                  = 0U;
    high_ticks                    = 0U;

    EXPECT_CALL( mock_hw, Peek_Result( _ ) )
        .WillOnce( Return( MakeHwResult( &period_ticks, &high_ticks ) ) );
    EXPECT_CALL( mock_hw, Consume_Result( _ ) ).Times( 1 );

    EXPECT_FALSE( EXEC_PWM_Capture_Consume( HW_PWM_CAPTURE_CHANNEL_1, &result ) );
    EXPECT_FALSE( result.is_valid );
}

TEST_F( ExecPWMCaptureTest, ConsumeReturnsFalseWhenHighExceedsPeriod )
{
    ExecPwmCaptureResult_T result = {};
    period_ticks                  = 1000U;
    high_ticks                    = 1200U;

    EXPECT_CALL( mock_hw, Peek_Result( _ ) )
        .WillOnce( Return( MakeHwResult( &period_ticks, &high_ticks ) ) );
    EXPECT_CALL( mock_hw, Consume_Result( _ ) ).Times( 1 );

    EXPECT_FALSE( EXEC_PWM_Capture_Consume( HW_PWM_CAPTURE_CHANNEL_1, &result ) );
    EXPECT_FALSE( result.is_valid );
}

TEST_F( ExecPWMCaptureTest, ConsumeAcceptsZeroPercentDuty )
{
    ExecPwmCaptureResult_T result = {};
    period_ticks                  = 1000U;
    high_ticks                    = 0U;

    EXPECT_CALL( mock_hw, Peek_Result( _ ) )
        .WillOnce( Return( MakeHwResult( &period_ticks, &high_ticks ) ) );
    EXPECT_CALL( mock_hw, Consume_Result( _ ) ).Times( 1 );

    EXPECT_TRUE( EXEC_PWM_Capture_Consume( HW_PWM_CAPTURE_CHANNEL_1, &result ) );
    EXPECT_TRUE( result.is_valid );
    EXPECT_EQ( result.period_ticks, 1000U );
    EXPECT_EQ( result.high_ticks, 0U );
}

TEST_F( ExecPWMCaptureTest, ConsumeAcceptsHundredPercentDuty )
{
    ExecPwmCaptureResult_T result = {};
    period_ticks                  = 1000U;
    high_ticks                    = 1000U;

    EXPECT_CALL( mock_hw, Peek_Result( _ ) )
        .WillOnce( Return( MakeHwResult( &period_ticks, &high_ticks ) ) );
    EXPECT_CALL( mock_hw, Consume_Result( _ ) ).Times( 1 );

    EXPECT_TRUE( EXEC_PWM_Capture_Consume( HW_PWM_CAPTURE_CHANNEL_1, &result ) );
    EXPECT_TRUE( result.is_valid );
    EXPECT_EQ( result.period_ticks, 1000U );
    EXPECT_EQ( result.high_ticks, 1000U );
}

TEST_F( ExecPWMCaptureTest, ConvertReturnsFalseForNullRaw )
{
    ExecPwmCapturePhysical_T out = {};

    EXPECT_CALL( mock_hw, Get_Timer_Clock_Hz( _ ) ).Times( 0 );

    EXPECT_FALSE( EXEC_PWM_Capture_Convert( HW_PWM_CAPTURE_CHANNEL_1, nullptr, &out ) );
}

TEST_F( ExecPWMCaptureTest, ConvertReturnsFalseForNullOut )
{
    ExecPwmCaptureResult_T raw = {};
    raw.is_valid               = true;

    EXPECT_CALL( mock_hw, Get_Timer_Clock_Hz( _ ) ).Times( 0 );

    EXPECT_FALSE( EXEC_PWM_Capture_Convert( HW_PWM_CAPTURE_CHANNEL_1, &raw, nullptr ) );
}

TEST_F( ExecPWMCaptureTest, ConvertReturnsFalseWhenResultIsInvalid )
{
    ExecPwmCaptureResult_T   raw = {};
    ExecPwmCapturePhysical_T out = {};
    raw.is_valid                 = false;

    EXPECT_CALL( mock_hw, Get_Timer_Clock_Hz( _ ) ).Times( 0 );

    EXPECT_FALSE( EXEC_PWM_Capture_Convert( HW_PWM_CAPTURE_CHANNEL_1, &raw, &out ) );
}

TEST_F( ExecPWMCaptureTest, ConvertReturnsFalseWhenClockHzIsZero )
{
    ExecPwmCaptureResult_T   raw = {};
    ExecPwmCapturePhysical_T out = {};
    raw.is_valid                 = true;
    raw.period_ticks             = 1000U;
    raw.high_ticks               = 500U;

    EXPECT_CALL( mock_hw, Get_Timer_Clock_Hz( HW_PWM_CAPTURE_CHANNEL_1 ) ).WillOnce( Return( 0U ) );

    EXPECT_FALSE( EXEC_PWM_Capture_Convert( HW_PWM_CAPTURE_CHANNEL_1, &raw, &out ) );
}

TEST_F( ExecPWMCaptureTest, ConvertProducesCorrectFrequencyAndDutyCycle )
{
    ExecPwmCaptureResult_T   raw = {};
    ExecPwmCapturePhysical_T out = {};
    raw.is_valid                 = true;
    raw.period_ticks             = 1000U;
    raw.high_ticks               = 500U;

    EXPECT_CALL( mock_hw, Get_Timer_Clock_Hz( HW_PWM_CAPTURE_CHANNEL_1 ) )
        .WillOnce( Return( 1000000U ) );

    EXPECT_TRUE( EXEC_PWM_Capture_Convert( HW_PWM_CAPTURE_CHANNEL_1, &raw, &out ) );
    EXPECT_EQ( out.frequency_hz, 1000U );
    EXPECT_EQ( out.duty_cycle_bp, 5000U );
}

TEST_F( ExecPWMCaptureTest, ConvertProducesZeroDutyCycleForZeroHighTicks )
{
    ExecPwmCaptureResult_T   raw = {};
    ExecPwmCapturePhysical_T out = {};
    raw.is_valid                 = true;
    raw.period_ticks             = 1000U;
    raw.high_ticks               = 0U;

    EXPECT_CALL( mock_hw, Get_Timer_Clock_Hz( _ ) ).WillOnce( Return( 1000000U ) );

    EXPECT_TRUE( EXEC_PWM_Capture_Convert( HW_PWM_CAPTURE_CHANNEL_1, &raw, &out ) );
    EXPECT_EQ( out.duty_cycle_bp, 0U );
}

TEST_F( ExecPWMCaptureTest, ConvertProducesFullDutyCycleForHundredPercent )
{
    ExecPwmCaptureResult_T   raw = {};
    ExecPwmCapturePhysical_T out = {};
    raw.is_valid                 = true;
    raw.period_ticks             = 1000U;
    raw.high_ticks               = 1000U;

    EXPECT_CALL( mock_hw, Get_Timer_Clock_Hz( _ ) ).WillOnce( Return( 1000000U ) );

    EXPECT_TRUE( EXEC_PWM_Capture_Convert( HW_PWM_CAPTURE_CHANNEL_1, &raw, &out ) );
    EXPECT_EQ( out.duty_cycle_bp, 10000U );
}