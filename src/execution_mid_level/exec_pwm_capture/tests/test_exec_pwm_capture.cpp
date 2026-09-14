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
    MOCK_METHOD( bool, Configure_Channel, ( HwPWMCaptureChannel_T, bool ));
    MOCK_METHOD( bool, Start_Channel, ( HwPWMCaptureChannel_T ) );
    MOCK_METHOD( bool, Stop_Channel, ( HwPWMCaptureChannel_T ) );
    MOCK_METHOD( bool, Read_Snapshot, ( HwPWMCaptureChannel_T, HwPWMCaptureSnapshot_T* ));
    MOCK_METHOD( uint32_t, Get_Timer_Clock_Hz, ( HwPWMCaptureChannel_T ) );
};

class MockLogicExpander
{
public:
    MOCK_METHOD( LogicExpanderStatus_T, Load_Control_Bit,
                 ( LogicExpanderIndex_T, LogicExpanderPort_T, uint8_t, bool ));
    MOCK_METHOD( LogicExpanderStatus_T, Send_Control_Bits, () );
};

static MockHwPwmCapture*  g_mock_hw             = nullptr;
static MockLogicExpander* g_mock_logic_expander = nullptr;

static void Reset_Exec_PWM_Capture_State( void )
{
    for ( uint32_t i = 0U; i < EXEC_PWM_CAPTURE_CHANNEL_COUNT; i++ )
    {
        exec_pwm_capture_channel_state[i]       = EXEC_PWM_CAPTURE_STATE_DISABLED;
        exec_pwm_capture_discard_next_result[i] = false;
    }
}

extern "C"
{
bool HW_PWM_Capture_Configure_Channel( HwPWMCaptureChannel_T channel, bool is_enabled )
{
    return g_mock_hw->Configure_Channel( channel, is_enabled );
}

bool HW_PWM_Capture_Start_Channel( HwPWMCaptureChannel_T channel )
{
    return g_mock_hw->Start_Channel( channel );
}

bool HW_PWM_Capture_Stop_Channel( HwPWMCaptureChannel_T channel )
{
    return g_mock_hw->Stop_Channel( channel );
}

bool HW_PWM_Capture_Read_Snapshot( HwPWMCaptureChannel_T channel, HwPWMCaptureSnapshot_T* snapshot )
{
    return g_mock_hw->Read_Snapshot( channel, snapshot );
}

uint32_t HW_PWM_Capture_Get_Timer_Clock_Hz( HwPWMCaptureChannel_T channel )
{
    return g_mock_hw->Get_Timer_Clock_Hz( channel );
}

LogicExpanderStatus_T LOGIC_EXPANDER_Load_Control_Bit( LogicExpanderIndex_T expander_index,
                                                       LogicExpanderPort_T port, uint8_t bit_index,
                                                       bool bit_value )
{
    return g_mock_logic_expander->Load_Control_Bit( expander_index, port, bit_index, bit_value );
}

LogicExpanderStatus_T LOGIC_EXPANDER_Send_Control_Bits( void )
{
    return g_mock_logic_expander->Send_Control_Bits();
}
}

/**-----------------------------------------------------------------------------
 *  Test Fixture
 *------------------------------------------------------------------------------
 */

class ExecPWMCaptureTest : public ::testing::Test
{
protected:
    MockHwPwmCapture  mock_hw;
    MockLogicExpander mock_logic_expander;

    void SetUp( void ) override
    {
        g_mock_hw             = &mock_hw;
        g_mock_logic_expander = &mock_logic_expander;
        ON_CALL( mock_logic_expander, Load_Control_Bit( _, _, _, _ ) )
            .WillByDefault( Return( LOGIC_EXPANDER_STATUS_OK ) );
        ON_CALL( mock_logic_expander, Send_Control_Bits() )
            .WillByDefault( Return( LOGIC_EXPANDER_STATUS_OK ) );
        Reset_Exec_PWM_Capture_State();
    }

    void TearDown( void ) override
    {
        g_mock_hw             = nullptr;
        g_mock_logic_expander = nullptr;
    }

    HwPWMCaptureSnapshot_T MakeHwSnapshot( uint32_t period, uint32_t high, bool is_overrun = false )
    {
        HwPWMCaptureSnapshot_T snapshot = {};
        snapshot.has_new_data           = true;
        snapshot.is_overrun             = is_overrun;
        snapshot.period_ticks           = period;
        snapshot.high_ticks             = high;
        return snapshot;
    }
};

/**-----------------------------------------------------------------------------
 *  Test Cases
 *------------------------------------------------------------------------------
 */

TEST_F( ExecPWMCaptureTest, ConfigureEnabledAppliesModeAndConfiguresMappedHardwareChannel )
{
    ExecPwmCaptureConfig_T config = {};
    config.mode                   = EXEC_PWM_CAPTURE_LV_5V;
    config.is_enabled             = true;

    EXPECT_CALL( mock_logic_expander,
                 Load_Control_Bit( LOGIC_EXPANDER_PWM_SPI, LOGIC_EXPANDER_PORT_A, 2U, false ) );
    EXPECT_CALL( mock_logic_expander,
                 Load_Control_Bit( LOGIC_EXPANDER_PWM_SPI, LOGIC_EXPANDER_PORT_A, 3U, true ) );
    EXPECT_CALL( mock_logic_expander, Send_Control_Bits() );
    EXPECT_CALL( mock_hw, Configure_Channel( HW_PWM_CAPTURE_CHANNEL_2, true ) )
        .WillOnce( Return( true ) );

    EXPECT_TRUE( EXEC_PWM_Capture_Configure_Channel( EXEC_PWM_CAPTURE_CHANNEL_2, &config ) );
}

TEST_F( ExecPWMCaptureTest, FailedStoppedChannelReconfigurationRevokesStartPermission )
{
    ExecPwmCaptureConfig_T config                              = {};
    config.mode                                                = EXEC_PWM_CAPTURE_LV_3V3;
    config.is_enabled                                          = true;
    exec_pwm_capture_channel_state[EXEC_PWM_CAPTURE_CHANNEL_1] = EXEC_PWM_CAPTURE_STATE_CONFIGURED;

    EXPECT_CALL( mock_hw, Configure_Channel( _, _ ) ).WillOnce( Return( false ) );

    EXPECT_FALSE( EXEC_PWM_Capture_Configure_Channel( EXEC_PWM_CAPTURE_CHANNEL_1, &config ) );
    EXPECT_FALSE( EXEC_PWM_Capture_Is_Configured( EXEC_PWM_CAPTURE_CHANNEL_1 ) );

    EXPECT_CALL( mock_hw, Start_Channel( _ ) ).Times( 0 );
    EXPECT_FALSE( EXEC_PWM_Capture_Start_Channel( EXEC_PWM_CAPTURE_CHANNEL_1 ) );
}

TEST_F( ExecPWMCaptureTest, ConfigureReturnsFalseForNullConfig )
{
    EXPECT_CALL( mock_hw, Configure_Channel( _, _ ) ).Times( 0 );

    EXPECT_FALSE( EXEC_PWM_Capture_Configure_Channel( EXEC_PWM_CAPTURE_CHANNEL_1, nullptr ) );
}

TEST_F( ExecPWMCaptureTest, ConfigureReturnsFalseForInvalidChannel )
{
    ExecPwmCaptureConfig_T config = {};
    config.mode                   = EXEC_PWM_CAPTURE_LV_3V3;
    config.is_enabled             = true;

    EXPECT_CALL( mock_hw, Configure_Channel( _, _ ) ).Times( 0 );

    EXPECT_FALSE(
        EXEC_PWM_Capture_Configure_Channel( static_cast<ExecPwmCaptureChannel_T>( 2U ), &config ) );
}

TEST_F( ExecPWMCaptureTest, LifecycleRejectsNegativeChannelWithoutHardwareAccess )
{
    const ExecPwmCaptureChannel_T negative_channel = static_cast<ExecPwmCaptureChannel_T>( -1 );
    ExecPwmCaptureConfig_T        config           = {};
    config.mode                                    = EXEC_PWM_CAPTURE_LV_3V3;
    config.is_enabled                              = true;

    EXPECT_CALL( mock_hw, Configure_Channel( _, _ ) ).Times( 0 );
    EXPECT_CALL( mock_hw, Start_Channel( _ ) ).Times( 0 );
    EXPECT_CALL( mock_hw, Stop_Channel( _ ) ).Times( 0 );
    EXPECT_CALL( mock_logic_expander, Load_Control_Bit( _, _, _, _ ) ).Times( 0 );
    EXPECT_CALL( mock_logic_expander, Send_Control_Bits() ).Times( 0 );

    EXPECT_FALSE( EXEC_PWM_Capture_Configure_Channel( negative_channel, &config ) );
    EXPECT_FALSE( EXEC_PWM_Capture_Start_Channel( negative_channel ) );
    EXPECT_FALSE( EXEC_PWM_Capture_Stop_Channel( negative_channel ) );
    EXPECT_FALSE( EXEC_PWM_Capture_Is_Configured( negative_channel ) );
    EXPECT_FALSE( EXEC_PWM_Capture_Is_Started( negative_channel ) );
}

TEST_F( ExecPWMCaptureTest, ConfigureDisabledStopsHardwareAndAppliesSafeMode )
{
    ExecPwmCaptureConfig_T config = {};
    config.mode                   = EXEC_PWM_CAPTURE_LV_3V3;
    config.is_enabled             = false;

    EXPECT_CALL( mock_hw, Configure_Channel( HW_PWM_CAPTURE_CHANNEL_1, false ) )
        .WillOnce( Return( true ) );
    EXPECT_CALL( mock_logic_expander,
                 Load_Control_Bit( LOGIC_EXPANDER_PWM_SPI, LOGIC_EXPANDER_PORT_A, 0U, false ) );
    EXPECT_CALL( mock_logic_expander,
                 Load_Control_Bit( LOGIC_EXPANDER_PWM_SPI, LOGIC_EXPANDER_PORT_A, 1U, false ) );
    EXPECT_CALL( mock_logic_expander, Send_Control_Bits() );

    EXPECT_TRUE( EXEC_PWM_Capture_Configure_Channel( EXEC_PWM_CAPTURE_CHANNEL_1, &config ) );
}

TEST_F( ExecPWMCaptureTest, StartChannelStartsConfiguredHardwareChannel )
{
    ExecPwmCaptureConfig_T config = {};
    config.mode                   = EXEC_PWM_CAPTURE_LV_3V3;
    config.is_enabled             = true;

    EXPECT_CALL( mock_hw, Configure_Channel( HW_PWM_CAPTURE_CHANNEL_1, true ) )
        .WillOnce( Return( true ) );
    EXPECT_TRUE( EXEC_PWM_Capture_Configure_Channel( EXEC_PWM_CAPTURE_CHANNEL_1, &config ) );
    EXPECT_TRUE( EXEC_PWM_Capture_Is_Configured( EXEC_PWM_CAPTURE_CHANNEL_1 ) );
    EXPECT_FALSE( EXEC_PWM_Capture_Is_Started( EXEC_PWM_CAPTURE_CHANNEL_1 ) );

    EXPECT_CALL( mock_hw, Start_Channel( HW_PWM_CAPTURE_CHANNEL_1 ) ).WillOnce( Return( true ) );
    EXPECT_TRUE( EXEC_PWM_Capture_Start_Channel( EXEC_PWM_CAPTURE_CHANNEL_1 ) );
    EXPECT_TRUE( EXEC_PWM_Capture_Is_Configured( EXEC_PWM_CAPTURE_CHANNEL_1 ) );
    EXPECT_TRUE( EXEC_PWM_Capture_Is_Started( EXEC_PWM_CAPTURE_CHANNEL_1 ) );
    EXPECT_FALSE( EXEC_PWM_Capture_Start_Channel( EXEC_PWM_CAPTURE_CHANNEL_1 ) );
}

TEST_F( ExecPWMCaptureTest, StartChannelReturnsFalseWhenChannelIsNotConfigured )
{
    EXPECT_CALL( mock_hw, Start_Channel( _ ) ).Times( 0 );
    EXPECT_FALSE( EXEC_PWM_Capture_Start_Channel( EXEC_PWM_CAPTURE_CHANNEL_1 ) );
}

TEST_F( ExecPWMCaptureTest, StartChannelReturnsFalseWhenHardwareStartFails )
{
    ExecPwmCaptureConfig_T config = {};
    config.mode                   = EXEC_PWM_CAPTURE_LV_5V;
    config.is_enabled             = true;

    EXPECT_CALL( mock_hw, Configure_Channel( _, true ) ).WillOnce( Return( true ) );
    EXPECT_TRUE( EXEC_PWM_Capture_Configure_Channel( EXEC_PWM_CAPTURE_CHANNEL_1, &config ) );
    EXPECT_CALL( mock_hw, Start_Channel( _ ) ).WillOnce( Return( false ) );
    EXPECT_FALSE( EXEC_PWM_Capture_Start_Channel( EXEC_PWM_CAPTURE_CHANNEL_1 ) );
    // A failed start retains configured state and permits a retry.
    EXPECT_CALL( mock_hw, Start_Channel( HW_PWM_CAPTURE_CHANNEL_1 ) ).WillOnce( Return( true ) );
    EXPECT_TRUE( EXEC_PWM_Capture_Start_Channel( EXEC_PWM_CAPTURE_CHANNEL_1 ) );
}

TEST_F( ExecPWMCaptureTest, StopChannelStopsStartedHardwareChannel )
{
    ExecPwmCaptureConfig_T config = {};
    config.mode                   = EXEC_PWM_CAPTURE_LV_5V;
    config.is_enabled             = true;

    EXPECT_CALL( mock_hw, Configure_Channel( _, true ) ).WillOnce( Return( true ) );
    EXPECT_TRUE( EXEC_PWM_Capture_Configure_Channel( EXEC_PWM_CAPTURE_CHANNEL_1, &config ) );
    EXPECT_CALL( mock_hw, Start_Channel( _ ) ).WillOnce( Return( true ) );
    EXPECT_TRUE( EXEC_PWM_Capture_Start_Channel( EXEC_PWM_CAPTURE_CHANNEL_1 ) );

    EXPECT_CALL( mock_hw, Stop_Channel( HW_PWM_CAPTURE_CHANNEL_1 ) ).WillOnce( Return( true ) );
    EXPECT_TRUE( EXEC_PWM_Capture_Stop_Channel( EXEC_PWM_CAPTURE_CHANNEL_1 ) );
    EXPECT_TRUE( EXEC_PWM_Capture_Is_Configured( EXEC_PWM_CAPTURE_CHANNEL_1 ) );
    EXPECT_FALSE( EXEC_PWM_Capture_Is_Started( EXEC_PWM_CAPTURE_CHANNEL_1 ) );
}

TEST_F( ExecPWMCaptureTest, LifecycleQueriesRejectInvalidAndDisabledChannels )
{
    const ExecPwmCaptureChannel_T invalid = static_cast<ExecPwmCaptureChannel_T>( 2U );

    EXPECT_FALSE( EXEC_PWM_Capture_Is_Configured( EXEC_PWM_CAPTURE_CHANNEL_1 ) );
    EXPECT_FALSE( EXEC_PWM_Capture_Is_Started( EXEC_PWM_CAPTURE_CHANNEL_1 ) );
    EXPECT_FALSE( EXEC_PWM_Capture_Is_Configured( invalid ) );
    EXPECT_FALSE( EXEC_PWM_Capture_Is_Started( invalid ) );
}

TEST_F( ExecPWMCaptureTest, StopChannelReturnsFalseForInvalidChannel )
{
    EXPECT_CALL( mock_hw, Stop_Channel( _ ) ).Times( 0 );

    EXPECT_FALSE( EXEC_PWM_Capture_Stop_Channel( static_cast<ExecPwmCaptureChannel_T>( 2U ) ) );
}

TEST_F( ExecPWMCaptureTest, StopChannelReturnsFalseWhenChannelNotStarted )
{
    EXPECT_CALL( mock_hw, Stop_Channel( _ ) ).Times( 0 );

    EXPECT_FALSE( EXEC_PWM_Capture_Stop_Channel( EXEC_PWM_CAPTURE_CHANNEL_1 ) );
}

TEST_F( ExecPWMCaptureTest, StopChannelReturnsFalseWhenHardwareStopFails )
{
    ExecPwmCaptureConfig_T config = {};
    config.mode                   = EXEC_PWM_CAPTURE_LV_3V3;
    config.is_enabled             = true;

    EXPECT_CALL( mock_hw, Configure_Channel( _, true ) ).WillOnce( Return( true ) );
    EXPECT_TRUE( EXEC_PWM_Capture_Configure_Channel( EXEC_PWM_CAPTURE_CHANNEL_1, &config ) );
    EXPECT_CALL( mock_hw, Start_Channel( _ ) ).WillOnce( Return( true ) );
    EXPECT_TRUE( EXEC_PWM_Capture_Start_Channel( EXEC_PWM_CAPTURE_CHANNEL_1 ) );
    EXPECT_CALL( mock_hw, Stop_Channel( _ ) ).WillOnce( Return( false ) );

    EXPECT_FALSE( EXEC_PWM_Capture_Stop_Channel( EXEC_PWM_CAPTURE_CHANNEL_1 ) );
}

TEST_F( ExecPWMCaptureTest, StopThenStartChannelSucceeds )
{
    ExecPwmCaptureConfig_T config = {};
    config.mode                   = EXEC_PWM_CAPTURE_LV_3V3;
    config.is_enabled             = true;

    EXPECT_CALL( mock_hw, Configure_Channel( _, true ) ).WillOnce( Return( true ) );
    EXPECT_TRUE( EXEC_PWM_Capture_Configure_Channel( EXEC_PWM_CAPTURE_CHANNEL_1, &config ) );
    EXPECT_CALL( mock_hw, Start_Channel( _ ) ).Times( 2 ).WillRepeatedly( Return( true ) );
    EXPECT_TRUE( EXEC_PWM_Capture_Start_Channel( EXEC_PWM_CAPTURE_CHANNEL_1 ) );
    EXPECT_CALL( mock_hw, Stop_Channel( _ ) ).WillOnce( Return( true ) );
    EXPECT_TRUE( EXEC_PWM_Capture_Stop_Channel( EXEC_PWM_CAPTURE_CHANNEL_1 ) );
    EXPECT_TRUE( EXEC_PWM_Capture_Start_Channel( EXEC_PWM_CAPTURE_CHANNEL_1 ) );
}

TEST_F( ExecPWMCaptureTest, EstablishEpochFailsWhenChannelNotStarted )
{
    EXPECT_FALSE( EXEC_PWM_Capture_Establish_Epoch( EXEC_PWM_CAPTURE_CHANNEL_1 ) );
}

TEST_F( ExecPWMCaptureTest, EstablishEpochDrainsPendingCaptureAndSetsDiscardFlag )
{
    ExecPwmCaptureConfig_T config = {};
    config.mode                   = EXEC_PWM_CAPTURE_LV_3V3;
    config.is_enabled             = true;
    ExecPwmCaptureResult_T result = {};

    EXPECT_CALL( mock_hw, Configure_Channel( _, true ) ).WillOnce( Return( true ) );
    ASSERT_TRUE( EXEC_PWM_Capture_Configure_Channel( EXEC_PWM_CAPTURE_CHANNEL_1, &config ) );
    EXPECT_CALL( mock_hw, Start_Channel( _ ) ).WillOnce( Return( true ) );
    ASSERT_TRUE( EXEC_PWM_Capture_Start_Channel( EXEC_PWM_CAPTURE_CHANNEL_1 ) );

    EXPECT_CALL( mock_hw, Read_Snapshot( HW_PWM_CAPTURE_CHANNEL_1, _ ) ).WillOnce( Return( true ) );
    EXPECT_TRUE( EXEC_PWM_Capture_Establish_Epoch( EXEC_PWM_CAPTURE_CHANNEL_1 ) );

    /* First capture after epoch must be discarded */
    EXPECT_CALL( mock_hw, Read_Snapshot( HW_PWM_CAPTURE_CHANNEL_1, _ ) )
        .Times( 2 )
        .WillRepeatedly( testing::DoAll( testing::SetArgPointee<1>( MakeHwSnapshot( 100U, 50U ) ),
                                         Return( true ) ) );

    EXPECT_FALSE( EXEC_PWM_Capture_Consume( EXEC_PWM_CAPTURE_CHANNEL_1, &result ) );
    EXPECT_FALSE( result.has_new_data );

    EXPECT_TRUE( EXEC_PWM_Capture_Consume( EXEC_PWM_CAPTURE_CHANNEL_1, &result ) );
    EXPECT_TRUE( result.has_new_data );
    EXPECT_TRUE( result.is_valid );
}

TEST_F( ExecPWMCaptureTest, ConsumeReturnsFalseWhenNoNewHardwareData )
{
    ExecPwmCaptureResult_T result = {};

    EXPECT_CALL( mock_hw, Read_Snapshot( HW_PWM_CAPTURE_CHANNEL_1, _ ) )
        .WillOnce( Return( false ) );

    EXPECT_FALSE( EXEC_PWM_Capture_Consume( EXEC_PWM_CAPTURE_CHANNEL_1, &result ) );
    EXPECT_FALSE( result.has_new_data );
    EXPECT_FALSE( result.is_valid );
}

TEST_F( ExecPWMCaptureTest, ConsumeCopiesValidCaptureResult )
{
    ExecPwmCaptureResult_T result = {};

    EXPECT_CALL( mock_hw, Read_Snapshot( HW_PWM_CAPTURE_CHANNEL_1, _ ) )
        .WillOnce( testing::DoAll( testing::SetArgPointee<1>( MakeHwSnapshot( 1800U, 900U ) ),
                                   Return( true ) ) );

    EXPECT_TRUE( EXEC_PWM_Capture_Consume( EXEC_PWM_CAPTURE_CHANNEL_1, &result ) );
    EXPECT_TRUE( result.has_new_data );
    EXPECT_TRUE( result.is_valid );
    EXPECT_EQ( result.period_ticks, 1800U );
    EXPECT_EQ( result.high_ticks, 900U );
}

TEST_F( ExecPWMCaptureTest, FirstCaptureAfterStartIsConsumedWithoutBeingPublished )
{
    ExecPwmCaptureConfig_T config = {};
    config.mode                   = EXEC_PWM_CAPTURE_LV_3V3;
    config.is_enabled             = true;
    ExecPwmCaptureResult_T result = {};

    EXPECT_CALL( mock_hw, Configure_Channel( HW_PWM_CAPTURE_CHANNEL_1, true ) )
        .WillOnce( Return( true ) );
    ASSERT_TRUE( EXEC_PWM_Capture_Configure_Channel( EXEC_PWM_CAPTURE_CHANNEL_1, &config ) );
    EXPECT_CALL( mock_hw, Start_Channel( HW_PWM_CAPTURE_CHANNEL_1 ) ).WillOnce( Return( true ) );
    ASSERT_TRUE( EXEC_PWM_Capture_Start_Channel( EXEC_PWM_CAPTURE_CHANNEL_1 ) );

    EXPECT_CALL( mock_hw, Read_Snapshot( HW_PWM_CAPTURE_CHANNEL_1, _ ) )
        .Times( 2 )
        .WillRepeatedly( testing::DoAll( testing::SetArgPointee<1>( MakeHwSnapshot( 90U, 45U ) ),
                                         Return( true ) ) );

    EXPECT_FALSE( EXEC_PWM_Capture_Consume( EXEC_PWM_CAPTURE_CHANNEL_1, &result ) );
    EXPECT_FALSE( result.has_new_data );
    EXPECT_FALSE( result.is_valid );

    EXPECT_TRUE( EXEC_PWM_Capture_Consume( EXEC_PWM_CAPTURE_CHANNEL_1, &result ) );
    EXPECT_TRUE( result.has_new_data );
    EXPECT_TRUE( result.is_valid );
    EXPECT_EQ( result.period_ticks, 90U );
    EXPECT_EQ( result.high_ticks, 45U );
}

TEST_F( ExecPWMCaptureTest, ConsumeReturnsFalseWhenPeriodIsZero )
{
    ExecPwmCaptureResult_T result = {};

    EXPECT_CALL( mock_hw, Read_Snapshot( _, _ ) )
        .WillOnce( testing::DoAll( testing::SetArgPointee<1>( MakeHwSnapshot( 0U, 0U ) ),
                                   Return( true ) ) );

    EXPECT_FALSE( EXEC_PWM_Capture_Consume( EXEC_PWM_CAPTURE_CHANNEL_1, &result ) );
    EXPECT_TRUE( result.has_new_data );
    EXPECT_FALSE( result.is_valid );
}

TEST_F( ExecPWMCaptureTest, ConsumeReturnsFalseWhenHighExceedsPeriod )
{
    ExecPwmCaptureResult_T result = {};

    EXPECT_CALL( mock_hw, Read_Snapshot( _, _ ) )
        .WillOnce( testing::DoAll( testing::SetArgPointee<1>( MakeHwSnapshot( 1000U, 1200U ) ),
                                   Return( true ) ) );

    EXPECT_FALSE( EXEC_PWM_Capture_Consume( EXEC_PWM_CAPTURE_CHANNEL_1, &result ) );
    EXPECT_TRUE( result.has_new_data );
    EXPECT_FALSE( result.is_valid );
}

TEST_F( ExecPWMCaptureTest, ConsumeAcceptsOverrunCapture )
{
    ExecPwmCaptureResult_T result = {};

    EXPECT_CALL( mock_hw, Read_Snapshot( _, _ ) )
        .WillOnce( testing::DoAll( testing::SetArgPointee<1>( MakeHwSnapshot( 1000U, 500U, true ) ),
                                   Return( true ) ) );

    EXPECT_TRUE( EXEC_PWM_Capture_Consume( EXEC_PWM_CAPTURE_CHANNEL_1, &result ) );
    EXPECT_TRUE( result.has_new_data );
    EXPECT_TRUE( result.is_valid );
    EXPECT_EQ( result.period_ticks, 1000U );
    EXPECT_EQ( result.high_ticks, 500U );
}

TEST_F( ExecPWMCaptureTest, ConsumeAcceptsZeroPercentDuty )
{
    ExecPwmCaptureResult_T result = {};

    EXPECT_CALL( mock_hw, Read_Snapshot( _, _ ) )
        .WillOnce( testing::DoAll( testing::SetArgPointee<1>( MakeHwSnapshot( 1000U, 0U ) ),
                                   Return( true ) ) );

    EXPECT_TRUE( EXEC_PWM_Capture_Consume( EXEC_PWM_CAPTURE_CHANNEL_1, &result ) );
    EXPECT_TRUE( result.has_new_data );
    EXPECT_TRUE( result.is_valid );
    EXPECT_EQ( result.period_ticks, 1000U );
    EXPECT_EQ( result.high_ticks, 0U );
}

TEST_F( ExecPWMCaptureTest, ConsumeAcceptsHundredPercentDuty )
{
    ExecPwmCaptureResult_T result = {};

    EXPECT_CALL( mock_hw, Read_Snapshot( _, _ ) )
        .WillOnce( testing::DoAll( testing::SetArgPointee<1>( MakeHwSnapshot( 1000U, 1000U ) ),
                                   Return( true ) ) );

    EXPECT_TRUE( EXEC_PWM_Capture_Consume( EXEC_PWM_CAPTURE_CHANNEL_1, &result ) );
    EXPECT_TRUE( result.has_new_data );
    EXPECT_TRUE( result.is_valid );
    EXPECT_EQ( result.period_ticks, 1000U );
    EXPECT_EQ( result.high_ticks, 1000U );
}

TEST_F( ExecPWMCaptureTest, ConvertReturnsFalseForNullRaw )
{
    ExecPwmCapturePhysical_T out = {};

    EXPECT_CALL( mock_hw, Get_Timer_Clock_Hz( _ ) ).Times( 0 );

    EXPECT_FALSE( EXEC_PWM_Capture_Convert( EXEC_PWM_CAPTURE_CHANNEL_1, nullptr, &out ) );
}

TEST_F( ExecPWMCaptureTest, ConvertReturnsFalseForNullOut )
{
    ExecPwmCaptureResult_T raw = {};
    raw.is_valid               = true;

    EXPECT_CALL( mock_hw, Get_Timer_Clock_Hz( _ ) ).Times( 0 );

    EXPECT_FALSE( EXEC_PWM_Capture_Convert( EXEC_PWM_CAPTURE_CHANNEL_1, &raw, nullptr ) );
}

TEST_F( ExecPWMCaptureTest, ConvertReturnsFalseWhenResultIsInvalid )
{
    ExecPwmCaptureResult_T   raw = {};
    ExecPwmCapturePhysical_T out = {};
    raw.is_valid                 = false;

    EXPECT_CALL( mock_hw, Get_Timer_Clock_Hz( _ ) ).Times( 0 );

    EXPECT_FALSE( EXEC_PWM_Capture_Convert( EXEC_PWM_CAPTURE_CHANNEL_1, &raw, &out ) );
}

TEST_F( ExecPWMCaptureTest, ConvertReturnsFalseWhenClockHzIsZero )
{
    ExecPwmCaptureResult_T   raw = {};
    ExecPwmCapturePhysical_T out = {};
    raw.is_valid                 = true;
    raw.period_ticks             = 1000U;
    raw.high_ticks               = 500U;

    EXPECT_CALL( mock_hw, Get_Timer_Clock_Hz( HW_PWM_CAPTURE_CHANNEL_1 ) ).WillOnce( Return( 0U ) );

    EXPECT_FALSE( EXEC_PWM_Capture_Convert( EXEC_PWM_CAPTURE_CHANNEL_1, &raw, &out ) );
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

    EXPECT_TRUE( EXEC_PWM_Capture_Convert( EXEC_PWM_CAPTURE_CHANNEL_1, &raw, &out ) );
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

    EXPECT_TRUE( EXEC_PWM_Capture_Convert( EXEC_PWM_CAPTURE_CHANNEL_1, &raw, &out ) );
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

    EXPECT_TRUE( EXEC_PWM_Capture_Convert( EXEC_PWM_CAPTURE_CHANNEL_1, &raw, &out ) );
    EXPECT_EQ( out.duty_cycle_bp, 10000U );
}
