#include <gtest/gtest.h>
#include <gmock/gmock.h>

extern "C"
{
#include "exec_pwm_gen.c"  // NOLINT
}

using ::testing::_;
using ::testing::InSequence;
using ::testing::Return;

class MockExecPwmGenDependencies
{
public:
    MOCK_METHOD( bool, ConfigureChannel, ( HwPwmGenChannel_T channel ), () );
    MOCK_METHOD( bool, StartChannel, ( HwPwmGenChannel_T channel ), () );
    MOCK_METHOD( bool, StopChannel, ( HwPwmGenChannel_T channel ), () );
    MOCK_METHOD( void, SetPwm1Direct, ( uint16_t arr, uint16_t ccr, uint16_t psc ), () );
    MOCK_METHOD( void, SetPwm2Direct, ( uint16_t arr, uint16_t ccr, uint16_t psc ), () );
    MOCK_METHOD( LogicExpanderStatus_T, LoadControlBit,
                 ( LogicExpanderIndex_T expander, LogicExpanderPort_T port, uint8_t bit,
                   bool value ),
                 () );
    MOCK_METHOD( LogicExpanderStatus_T, SendControlBits, (), () );
};

static MockExecPwmGenDependencies* g_mock = nullptr;

extern "C"
{
bool HW_PWM_GEN_Configure_Channel( HwPwmGenChannel_T channel )
{
    return g_mock->ConfigureChannel( channel );
}

bool HW_PWM_GEN_Start_Channel( HwPwmGenChannel_T channel )
{
    return g_mock->StartChannel( channel );
}

bool HW_PWM_GEN_Stop_Channel( HwPwmGenChannel_T channel )
{
    return g_mock->StopChannel( channel );
}

void HW_PWM_GEN_Set_PWM1_Direct( uint16_t arr, uint16_t ccr, uint16_t psc )
{
    g_mock->SetPwm1Direct( arr, ccr, psc );
}

void HW_PWM_GEN_Set_PWM2_Direct( uint16_t arr, uint16_t ccr, uint16_t psc )
{
    g_mock->SetPwm2Direct( arr, ccr, psc );
}

LogicExpanderStatus_T LOGIC_EXPANDER_Load_Control_Bit( LogicExpanderIndex_T expander,
                                                       LogicExpanderPort_T port, uint8_t bit,
                                                       bool value )
{
    return g_mock->LoadControlBit( expander, port, bit, value );
}

LogicExpanderStatus_T LOGIC_EXPANDER_Send_Control_Bits( void )
{
    return g_mock->SendControlBits();
}
}

class ExecPwmGenTest : public ::testing::Test
{
protected:
    MockExecPwmGenDependencies mock;

    void SetUp() override
    {
        g_mock                                               = &mock;
        exec_pwm_gen_channel_states[EXEC_PWM_GEN_CHANNEL_LV] = {
            .state         = EXEC_PWM_GEN_STATE_DISABLED,
            .voltage_level = EXEC_PWM_GEN_VOLTAGE_3V3,
        };
        exec_pwm_gen_channel_states[EXEC_PWM_GEN_CHANNEL_HV] = {
            .state         = EXEC_PWM_GEN_STATE_DISABLED,
            .voltage_level = EXEC_PWM_GEN_VOLTAGE_12V,
        };
    }

    void TearDown() override
    {
        g_mock = nullptr;
    }
};

TEST_F( ExecPwmGenTest, ConfigureLV5VSelectsBitSixAndLeavesChannelStopped )
{
    const ExecPwmGenConfig_T config = { true, EXEC_PWM_GEN_VOLTAGE_5V, 1000U, 250U, 4U };

    EXPECT_CALL( mock, ConfigureChannel( HW_PWM_GEN_CHANNEL_LV ) ).WillOnce( Return( true ) );
    EXPECT_CALL( mock, SetPwm1Direct( 1000U, 250U, 4U ) );
    EXPECT_CALL( mock, LoadControlBit( LOGIC_EXPANDER_PWM_SPI, LOGIC_EXPANDER_PORT_A, 6U, true ) )
        .WillOnce( Return( LOGIC_EXPANDER_STATUS_OK ) );
    EXPECT_CALL( mock, SendControlBits() ).WillOnce( Return( LOGIC_EXPANDER_STATUS_OK ) );

    EXPECT_TRUE( EXEC_PWM_GEN_Configure_Channel( EXEC_PWM_GEN_CHANNEL_LV, &config ) );
    EXPECT_TRUE( EXEC_PWM_GEN_Is_Configured( EXEC_PWM_GEN_CHANNEL_LV ) );
    EXPECT_FALSE( EXEC_PWM_GEN_Is_Started( EXEC_PWM_GEN_CHANNEL_LV ) );
}

TEST_F( ExecPwmGenTest, ConfigureHV24VSelectsMutuallyExclusiveHVBits )
{
    const ExecPwmGenConfig_T config = { true, EXEC_PWM_GEN_VOLTAGE_24V, 2000U, 500U, 8U };
    InSequence               sequence;

    EXPECT_CALL( mock, ConfigureChannel( HW_PWM_GEN_CHANNEL_HV ) ).WillOnce( Return( true ) );
    EXPECT_CALL( mock, SetPwm2Direct( 2000U, 500U, 8U ) );
    EXPECT_CALL( mock, LoadControlBit( LOGIC_EXPANDER_PWM_SPI, LOGIC_EXPANDER_PORT_A, 4U, false ) )
        .WillOnce( Return( LOGIC_EXPANDER_STATUS_OK ) );
    EXPECT_CALL( mock, LoadControlBit( LOGIC_EXPANDER_PWM_SPI, LOGIC_EXPANDER_PORT_A, 5U, true ) )
        .WillOnce( Return( LOGIC_EXPANDER_STATUS_OK ) );
    EXPECT_CALL( mock, SendControlBits() ).WillOnce( Return( LOGIC_EXPANDER_STATUS_OK ) );

    EXPECT_TRUE( EXEC_PWM_GEN_Configure_Channel( EXEC_PWM_GEN_CHANNEL_HV, &config ) );
}

TEST_F( ExecPwmGenTest, ConfigureRejectsVoltageFromOtherChannel )
{
    const ExecPwmGenConfig_T config = { true, EXEC_PWM_GEN_VOLTAGE_24V, 0U, 0U, 0U };

    EXPECT_CALL( mock, ConfigureChannel( _ ) ).Times( 0 );
    EXPECT_CALL( mock, LoadControlBit( _, _, _, _ ) ).Times( 0 );
    EXPECT_FALSE( EXEC_PWM_GEN_Configure_Channel( EXEC_PWM_GEN_CHANNEL_LV, &config ) );
}

TEST_F( ExecPwmGenTest, ConfigureRejectsInitialCCRGreaterThanARRPlusOne )
{
    const ExecPwmGenConfig_T config = { true, EXEC_PWM_GEN_VOLTAGE_3V3, 100U, 102U, 0U };

    EXPECT_CALL( mock, ConfigureChannel( _ ) ).Times( 0 );
    EXPECT_CALL( mock, SetPwm1Direct( _, _, _ ) ).Times( 0 );
    EXPECT_CALL( mock, LoadControlBit( _, _, _, _ ) ).Times( 0 );

    EXPECT_FALSE( EXEC_PWM_GEN_Configure_Channel( EXEC_PWM_GEN_CHANNEL_LV, &config ) );
}

TEST_F( ExecPwmGenTest, DisabledChannelsApplyTheirSafeSelections )
{
    const ExecPwmGenConfig_T disabled = { false, EXEC_PWM_GEN_VOLTAGE_3V3, 0U, 0U, 0U };

    EXPECT_CALL( mock, LoadControlBit( _, _, 6U, false ) )
        .WillOnce( Return( LOGIC_EXPANDER_STATUS_OK ) );
    EXPECT_CALL( mock, SendControlBits() ).WillOnce( Return( LOGIC_EXPANDER_STATUS_OK ) );
    EXPECT_TRUE( EXEC_PWM_GEN_Configure_Channel( EXEC_PWM_GEN_CHANNEL_LV, &disabled ) );

    EXPECT_CALL( mock, LoadControlBit( _, _, 4U, false ) )
        .WillOnce( Return( LOGIC_EXPANDER_STATUS_OK ) );
    EXPECT_CALL( mock, LoadControlBit( _, _, 5U, false ) )
        .WillOnce( Return( LOGIC_EXPANDER_STATUS_OK ) );
    EXPECT_CALL( mock, SendControlBits() ).WillOnce( Return( LOGIC_EXPANDER_STATUS_OK ) );
    EXPECT_TRUE( EXEC_PWM_GEN_Configure_Channel( EXEC_PWM_GEN_CHANNEL_HV, &disabled ) );
}

TEST_F( ExecPwmGenTest, DisableIgnoresInitialWaveformAndAppliesSafeSelection )
{
    const ExecPwmGenConfig_T disabled = {
        false, EXEC_PWM_GEN_VOLTAGE_5V, 0U, 2U, 0U,
    };

    EXPECT_CALL( mock, ConfigureChannel( _ ) ).Times( 0 );
    EXPECT_CALL( mock, SetPwm1Direct( _, _, _ ) ).Times( 0 );
    EXPECT_CALL( mock, LoadControlBit( LOGIC_EXPANDER_PWM_SPI, LOGIC_EXPANDER_PORT_A, 6U, false ) )
        .WillOnce( Return( LOGIC_EXPANDER_STATUS_OK ) );
    EXPECT_CALL( mock, SendControlBits() ).WillOnce( Return( LOGIC_EXPANDER_STATUS_OK ) );

    EXPECT_TRUE( EXEC_PWM_GEN_Configure_Channel( EXEC_PWM_GEN_CHANNEL_LV, &disabled ) );
    EXPECT_FALSE( EXEC_PWM_GEN_Is_Configured( EXEC_PWM_GEN_CHANNEL_LV ) );
    EXPECT_FALSE( EXEC_PWM_GEN_Is_Started( EXEC_PWM_GEN_CHANNEL_LV ) );
}

TEST_F( ExecPwmGenTest, LVAndHVCanBeStartedAtTheSameTime )
{
    exec_pwm_gen_channel_states[EXEC_PWM_GEN_CHANNEL_LV].state = EXEC_PWM_GEN_STATE_CONFIGURED;
    exec_pwm_gen_channel_states[EXEC_PWM_GEN_CHANNEL_HV].state = EXEC_PWM_GEN_STATE_CONFIGURED;

    EXPECT_CALL( mock, StartChannel( HW_PWM_GEN_CHANNEL_LV ) ).WillOnce( Return( true ) );
    EXPECT_CALL( mock, StartChannel( HW_PWM_GEN_CHANNEL_HV ) ).WillOnce( Return( true ) );
    EXPECT_TRUE( EXEC_PWM_GEN_Start_Channel( EXEC_PWM_GEN_CHANNEL_LV ) );
    EXPECT_TRUE( EXEC_PWM_GEN_Start_Channel( EXEC_PWM_GEN_CHANNEL_HV ) );
    EXPECT_TRUE( EXEC_PWM_GEN_Is_Started( EXEC_PWM_GEN_CHANNEL_LV ) );
    EXPECT_TRUE( EXEC_PWM_GEN_Is_Started( EXEC_PWM_GEN_CHANNEL_HV ) );
}

TEST_F( ExecPwmGenTest, LifecycleFailuresPreserveState )
{
    exec_pwm_gen_channel_states[EXEC_PWM_GEN_CHANNEL_LV].state = EXEC_PWM_GEN_STATE_CONFIGURED;
    EXPECT_CALL( mock, StartChannel( HW_PWM_GEN_CHANNEL_LV ) ).WillOnce( Return( false ) );
    EXPECT_FALSE( EXEC_PWM_GEN_Start_Channel( EXEC_PWM_GEN_CHANNEL_LV ) );
    EXPECT_TRUE( EXEC_PWM_GEN_Is_Configured( EXEC_PWM_GEN_CHANNEL_LV ) );

    exec_pwm_gen_channel_states[EXEC_PWM_GEN_CHANNEL_HV].state = EXEC_PWM_GEN_STATE_STARTED;
    EXPECT_CALL( mock, StopChannel( HW_PWM_GEN_CHANNEL_HV ) ).WillOnce( Return( false ) );
    EXPECT_FALSE( EXEC_PWM_GEN_Stop_Channel( EXEC_PWM_GEN_CHANNEL_HV ) );
    EXPECT_TRUE( EXEC_PWM_GEN_Is_Started( EXEC_PWM_GEN_CHANNEL_HV ) );
}

TEST_F( ExecPwmGenTest, DirectUpdateWrappersRemainUnconditional )
{
    EXPECT_CALL( mock, SetPwm1Direct( 1000U, 250U, 4U ) );
    EXPECT_CALL( mock, SetPwm2Direct( 2000U, 500U, 8U ) );

    EXEC_PWM_GEN_Set_PWM_LV( 1000U, 250U, 4U );
    EXEC_PWM_GEN_Set_PWM_HV( 2000U, 500U, 8U );
}
