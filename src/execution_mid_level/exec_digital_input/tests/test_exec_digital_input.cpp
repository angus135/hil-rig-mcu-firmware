/******************************************************************************
 *  File:       test_exec_digital_input.cpp
 *  Author:     Angus Corr
 *  Created:    25-Mar-2026
 *
 *  Description:
 *      White-box tests for digital-input configuration, selector mapping,
 *      lifecycle state, and execution-time sampling.
 *
 *  Notes:
 *      The implementation is included directly so private board mappings and
 *      lifecycle masks can be reset and verified.
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>

extern "C"
{
#include "exec_digital_input.c" /* Module under test */  // NOLINT
}

using ::testing::_;
using ::testing::Return;

/**-----------------------------------------------------------------------------
 *  Test Doubles / Mocks
 *------------------------------------------------------------------------------
 */

class MockExecDigitalInputDependencies
{
public:
    MOCK_METHOD( uint32_t, ReadAllDigitalInputs, () );
    MOCK_METHOD( LogicExpanderStatus_T, LoadControlBit,
                 ( LogicExpanderIndex_T expander, LogicExpanderPort_T port, uint8_t bit,
                   bool value ) );
    MOCK_METHOD( LogicExpanderStatus_T, SendControlBits, () );
};

static MockExecDigitalInputDependencies* g_mock = nullptr;

/**-----------------------------------------------------------------------------
 *  Link Seam: Mocked Function Definitions
 *------------------------------------------------------------------------------
 */

extern "C"
{
uint32_t HW_GPIO_Read_All_Digital_Inputs( void )
{
    return g_mock->ReadAllDigitalInputs();
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

/**-----------------------------------------------------------------------------
 *  Test Fixture
 *------------------------------------------------------------------------------
 */

class ExecDigitalInputTest : public ::testing::Test
{
protected:
    MockExecDigitalInputDependencies mock;

    void SetUp( void ) override
    {
        g_mock                             = &mock;
        exec_digital_input_state           = EXEC_DIGITAL_INPUT_STATE_DISABLED;
        exec_digital_input_configured_mask = 0U;
        exec_digital_input_active_mask     = 0U;
    }

    void TearDown( void ) override
    {
        g_mock = nullptr;
    }
};

/**-----------------------------------------------------------------------------
 *  Test Cases
 *------------------------------------------------------------------------------
 */

TEST_F( ExecDigitalInputTest, ControlMappingMatchesBoardSchematic )
{
    const ExecDigitalInputControlMapping_T expected[EXEC_DIGITAL_INPUT_CHANNEL_COUNT] = {
        { LOGIC_EXPANDER_DI_1, LOGIC_EXPANDER_PORT_A, 6U, 7U },
        { LOGIC_EXPANDER_DI_1, LOGIC_EXPANDER_PORT_B, 4U, 5U },
        { LOGIC_EXPANDER_DI_1, LOGIC_EXPANDER_PORT_B, 3U, 2U },
        { LOGIC_EXPANDER_DI_1, LOGIC_EXPANDER_PORT_B, 7U, 6U },
        { LOGIC_EXPANDER_DI_2, LOGIC_EXPANDER_PORT_A, 6U, 7U },
        { LOGIC_EXPANDER_DI_1, LOGIC_EXPANDER_PORT_A, 0U, 1U },
        { LOGIC_EXPANDER_DI_2, LOGIC_EXPANDER_PORT_B, 7U, 6U },
        { LOGIC_EXPANDER_DI_2, LOGIC_EXPANDER_PORT_B, 5U, 4U },
        { LOGIC_EXPANDER_DI_2, LOGIC_EXPANDER_PORT_A, 0U, 1U },
        { LOGIC_EXPANDER_DI_2, LOGIC_EXPANDER_PORT_A, 2U, 3U },
    };

    for ( uint32_t channel = 0U;
          channel < ( uint32_t )EXEC_DIGITAL_INPUT_CHANNEL_COUNT;
          channel++ )
    {
        EXPECT_EQ( exec_digital_input_control_mappings[channel].expander,
                   expected[channel].expander );
        EXPECT_EQ( exec_digital_input_control_mappings[channel].port, expected[channel].port );
        EXPECT_EQ( exec_digital_input_control_mappings[channel].s0_bit, expected[channel].s0_bit );
        EXPECT_EQ( exec_digital_input_control_mappings[channel].s1_bit, expected[channel].s1_bit );
    }
}

TEST_F( ExecDigitalInputTest, StageModeUsesSchematicS1S0TruthTable )
{
    struct ModeCase
    {
        ExecDigitalInputMode_T mode;
        bool                   s0;
        bool                   s1;
    };

    const ModeCase cases[] = {
        { EXEC_DIGITAL_INPUT_MODE_DISABLED, false, false },
        { EXEC_DIGITAL_INPUT_MODE_3V3, false, false },
        { EXEC_DIGITAL_INPUT_MODE_5V, true, false },
        { EXEC_DIGITAL_INPUT_MODE_12V, false, true },
        { EXEC_DIGITAL_INPUT_MODE_24V, true, true },
    };

    for ( const ModeCase& test_case : cases )
    {
        EXPECT_CALL( mock,
                     LoadControlBit( LOGIC_EXPANDER_DI_1, LOGIC_EXPANDER_PORT_A, 6U,
                                     test_case.s0 ) )
            .WillOnce( Return( LOGIC_EXPANDER_STATUS_OK ) );
        EXPECT_CALL( mock,
                     LoadControlBit( LOGIC_EXPANDER_DI_1, LOGIC_EXPANDER_PORT_A, 7U,
                                     test_case.s1 ) )
            .WillOnce( Return( LOGIC_EXPANDER_STATUS_OK ) );

        EXPECT_TRUE( EXEC_DIGITAL_INPUT_Stage_Mode( EXEC_DIGITAL_INPUT_CHANNEL_1,
                                                    test_case.mode ) );
    }
}

TEST_F( ExecDigitalInputTest, ConfigureRejectsInvalidRequestWithoutChangingExistingState )
{
    ExecDigitalInputConfig_T config = {};
    config.channels[0] = static_cast<ExecDigitalInputMode_T>( EXEC_DIGITAL_INPUT_MODE_COUNT );
    exec_digital_input_state           = EXEC_DIGITAL_INPUT_STATE_CONFIGURED;
    exec_digital_input_configured_mask = 0x0100U;

    EXPECT_CALL( mock, LoadControlBit( _, _, _, _ ) ).Times( 0 );
    EXPECT_FALSE( EXEC_DIGITAL_INPUT_Configure( nullptr ) );
    EXPECT_FALSE( EXEC_DIGITAL_INPUT_Configure( &config ) );
    EXPECT_TRUE( EXEC_DIGITAL_INPUT_Is_Configured() );
    EXPECT_EQ( exec_digital_input_configured_mask, 0x0100U );
}

TEST_F( ExecDigitalInputTest, ConfigureStagesAllChannelsAndBuildsPhysicalGPIODMask )
{
    ExecDigitalInputConfig_T config = {};
    config.channels[EXEC_DIGITAL_INPUT_CHANNEL_1] = EXEC_DIGITAL_INPUT_MODE_3V3;
    config.channels[EXEC_DIGITAL_INPUT_CHANNEL_5] = EXEC_DIGITAL_INPUT_MODE_12V;
    config.channels[EXEC_DIGITAL_INPUT_CHANNEL_7] = EXEC_DIGITAL_INPUT_MODE_24V;

    EXPECT_CALL( mock, LoadControlBit( _, _, _, _ ) ).Times( 20 )
        .WillRepeatedly( Return( LOGIC_EXPANDER_STATUS_OK ) );
    EXPECT_CALL( mock, SendControlBits() ).WillOnce( Return( LOGIC_EXPANDER_STATUS_OK ) );

    EXPECT_TRUE( EXEC_DIGITAL_INPUT_Configure( &config ) );
    EXPECT_EQ( exec_digital_input_configured_mask, 0x00004101U );
    EXPECT_EQ( exec_digital_input_active_mask, 0U );
    EXPECT_TRUE( EXEC_DIGITAL_INPUT_Is_Configured() );
    EXPECT_FALSE( EXEC_DIGITAL_INPUT_Is_Started() );
}

TEST_F( ExecDigitalInputTest, ConfigurationFailureLeavesSubsystemDisabled )
{
    const ExecDigitalInputConfig_T config = {};
    exec_digital_input_state              = EXEC_DIGITAL_INPUT_STATE_CONFIGURED;

    EXPECT_CALL( mock, LoadControlBit( _, _, _, _ ) )
        .WillOnce( Return( LOGIC_EXPANDER_STATUS_ERROR ) );

    EXPECT_FALSE( EXEC_DIGITAL_INPUT_Configure( &config ) );
    EXPECT_FALSE( EXEC_DIGITAL_INPUT_Is_Configured() );
    EXPECT_FALSE( EXEC_DIGITAL_INPUT_Start() );
}

TEST_F( ExecDigitalInputTest, SendFailureLeavesSubsystemDisabled )
{
    const ExecDigitalInputConfig_T config = {};

    EXPECT_CALL( mock, LoadControlBit( _, _, _, _ ) ).Times( 20 )
        .WillRepeatedly( Return( LOGIC_EXPANDER_STATUS_OK ) );
    EXPECT_CALL( mock, SendControlBits() ).WillOnce( Return( LOGIC_EXPANDER_STATUS_BUSY ) );

    EXPECT_FALSE( EXEC_DIGITAL_INPUT_Configure( &config ) );
    EXPECT_FALSE( EXEC_DIGITAL_INPUT_Is_Configured() );
}

TEST_F( ExecDigitalInputTest, StartAndStopActivateAndRetainConfiguredMask )
{
    exec_digital_input_state           = EXEC_DIGITAL_INPUT_STATE_CONFIGURED;
    exec_digital_input_configured_mask = 0x00004101U;

    EXPECT_TRUE( EXEC_DIGITAL_INPUT_Start() );
    EXPECT_EQ( exec_digital_input_active_mask, 0x00004101U );
    EXPECT_TRUE( EXEC_DIGITAL_INPUT_Is_Started() );
    EXPECT_FALSE( EXEC_DIGITAL_INPUT_Start() );

    EXPECT_TRUE( EXEC_DIGITAL_INPUT_Stop() );
    EXPECT_EQ( exec_digital_input_active_mask, 0U );
    EXPECT_EQ( exec_digital_input_configured_mask, 0x00004101U );
    EXPECT_TRUE( EXEC_DIGITAL_INPUT_Is_Configured() );
    EXPECT_FALSE( EXEC_DIGITAL_INPUT_Stop() );
}

TEST_F( ExecDigitalInputTest, SampleAllReadsOnceAndAppliesActivePhysicalMask )
{
    exec_digital_input_active_mask = 0x00004101U;
    EXPECT_CALL( mock, ReadAllDigitalInputs() ).WillOnce( Return( 0x0000FFFFU ) );

    uint32_t sample = 0U;
    EXEC_DIGITAL_INPUT_Sample_All( &sample );

    EXPECT_EQ( sample, 0x00004101U );
}
