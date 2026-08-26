/******************************************************************************
 *  File:       test_exec_digital_output.cpp
 *  Author:     Angus Corr
 *  Created:    25-Mar-2026
 *
 *  Description:
 *      White-box unit tests for the aggregate digital-output execution
 *      lifecycle and board-level voltage-selection mapping.
 *
 *  Notes:
 *      The implementation is included directly so private lifecycle state and
 *      fixed board mappings can be reset and verified. HW GPIO and Logic
 *      Expander APIs are mocked at the module boundary.
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <vector>

extern "C"
{
#include "exec_digital_output.c" /* Module under test */  // NOLINT
}

/**-----------------------------------------------------------------------------
 *  Test Constants / Macros
 *------------------------------------------------------------------------------
 */

using ::testing::_;
using ::testing::ElementsAre;
using ::testing::Invoke;
using ::testing::Return;

/**-----------------------------------------------------------------------------
 *  Test Doubles / Mocks
 *------------------------------------------------------------------------------
 */

class MockExecDigitalOutputDependencies
{
public:
    MOCK_METHOD( void, SetManyPins, ( GPIOOutput_T * pins, uint16_t length ) );
    MOCK_METHOD( void, ResetManyPins, ( GPIOOutput_T * pins, uint16_t length ) );
    MOCK_METHOD( DigitalOutputPinmask_T, CombinePortPinMasks,
                 ( GPIOOutput_T * pins, uint8_t length ) );
    MOCK_METHOD( void, SetOutput, ( uint32_t pin_mask ) );
    MOCK_METHOD( void, ResetOutput, ( uint32_t pin_mask ) );
    MOCK_METHOD( LogicExpanderStatus_T, LoadControlBit,
                 ( LogicExpanderIndex_T expander, LogicExpanderPort_T port, uint8_t bit,
                   bool value ) );
    MOCK_METHOD( LogicExpanderStatus_T, SendControlBits, () );
};

static MockExecDigitalOutputDependencies* g_mock = nullptr;

/**-----------------------------------------------------------------------------
 *  Link Seam: Mocked Function Definitions
 *------------------------------------------------------------------------------
 */

extern "C"
{
void HW_GPIO_Set_Many_Pins( GPIOOutput_T* pins, uint16_t length )
{
    g_mock->SetManyPins( pins, length );
}

void HW_GPIO_Reset_Many_Pins( GPIOOutput_T* pins, uint16_t length )
{
    g_mock->ResetManyPins( pins, length );
}

DigitalOutputPinmask_T HW_GPIO_Combine_Port_Pin_Masks( GPIOOutput_T* pins, uint8_t length )
{
    return g_mock->CombinePortPinMasks( pins, length );
}

void HW_GPIO_Set_Output( uint32_t pin_mask )
{
    g_mock->SetOutput( pin_mask );
}

void HW_GPIO_Reset_Output( uint32_t pin_mask )
{
    g_mock->ResetOutput( pin_mask );
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

/**
 * @brief Test fixture for exec digital-output white-box tests.
 *
 * Resets private lifecycle/configuration state and connects dependency mocks
 * before every test.
 */
class ExecDigitalOutputTest : public ::testing::Test
{
protected:
    MockExecDigitalOutputDependencies mock;

    void SetUp( void ) override
    {
        g_mock                            = &mock;
        exec_digital_output_state         = EXEC_DIGITAL_OUTPUT_STATE_DISABLED;
        exec_digital_output_configuration = {};
    }

    void TearDown( void ) override
    {
        g_mock = nullptr;
    }

    void ExpectSetOfAllChannels( void )
    {
        EXPECT_CALL( mock, SetManyPins( _, EXEC_DIGITAL_OUTPUT_CHANNEL_COUNT ) )
            .WillOnce( Invoke( []( GPIOOutput_T* pins, uint16_t length ) {
                EXPECT_EQ( length, EXEC_DIGITAL_OUTPUT_CHANNEL_COUNT );
                EXPECT_THAT( std::vector<GPIOOutput_T>( pins, pins + length ),
                             ElementsAre( DIGITAL_OUTPUT_0, DIGITAL_OUTPUT_1, DIGITAL_OUTPUT_2,
                                          DIGITAL_OUTPUT_3, DIGITAL_OUTPUT_4, DIGITAL_OUTPUT_5,
                                          DIGITAL_OUTPUT_6, DIGITAL_OUTPUT_7, DIGITAL_OUTPUT_8,
                                          DIGITAL_OUTPUT_9 ) );
            } ) );
    }
};

/**-----------------------------------------------------------------------------
 *  Test Cases
 *------------------------------------------------------------------------------
 */

TEST_F( ExecDigitalOutputTest, ControlMappingMatchesBoardSchematic )
{
    const ExecDigitalOutputControlMapping_T expected[EXEC_DIGITAL_OUTPUT_CHANNEL_COUNT] = {
        { LOGIC_EXPANDER_DO_2, LOGIC_EXPANDER_PORT_A, 6U, 7U },
        { LOGIC_EXPANDER_DO_1, LOGIC_EXPANDER_PORT_A, 1U, 0U },
        { LOGIC_EXPANDER_DO_1, LOGIC_EXPANDER_PORT_A, 6U, 7U },
        { LOGIC_EXPANDER_DO_1, LOGIC_EXPANDER_PORT_A, 4U, 5U },
        { LOGIC_EXPANDER_DO_1, LOGIC_EXPANDER_PORT_B, 4U, 5U },
        { LOGIC_EXPANDER_DO_1, LOGIC_EXPANDER_PORT_B, 0U, 1U },
        { LOGIC_EXPANDER_DO_2, LOGIC_EXPANDER_PORT_B, 2U, 3U },
        { LOGIC_EXPANDER_DO_1, LOGIC_EXPANDER_PORT_B, 7U, 6U },
        { LOGIC_EXPANDER_DO_2, LOGIC_EXPANDER_PORT_B, 6U, 7U },
        { LOGIC_EXPANDER_DO_2, LOGIC_EXPANDER_PORT_B, 5U, 4U },
    };

    for ( uint32_t channel = 0U; channel < ( uint32_t )EXEC_DIGITAL_OUTPUT_CHANNEL_COUNT;
          channel++ )
    {
        EXPECT_EQ( exec_digital_output_control_mappings[channel].expander,
                   expected[channel].expander );
        EXPECT_EQ( exec_digital_output_control_mappings[channel].port, expected[channel].port );
        EXPECT_EQ( exec_digital_output_control_mappings[channel].a0_bit, expected[channel].a0_bit );
        EXPECT_EQ( exec_digital_output_control_mappings[channel].a1_bit, expected[channel].a1_bit );
        EXPECT_EQ( exec_digital_output_gpio_channels[channel],
                   static_cast<GPIOOutput_T>( DIGITAL_OUTPUT_0 + channel ) );
    }
}

TEST_F( ExecDigitalOutputTest, StageVoltageUsesA0A1TruthTable )
{
    struct VoltageCase
    {
        ExecDigitalOutputMode_T mode;
        bool                    a0;
        bool                    a1;
    };

    const VoltageCase cases[] = {
        { EXEC_DIGITAL_OUTPUT_MODE_3V3, false, false },
        { EXEC_DIGITAL_OUTPUT_MODE_5V, false, true },
        { EXEC_DIGITAL_OUTPUT_MODE_12V, true, false },
        { EXEC_DIGITAL_OUTPUT_MODE_24V, true, true },
    };

    for ( const VoltageCase& test_case : cases )
    {
        EXPECT_CALL(
            mock, LoadControlBit( LOGIC_EXPANDER_DO_2, LOGIC_EXPANDER_PORT_A, 6U, test_case.a0 ) )
            .WillOnce( Return( LOGIC_EXPANDER_STATUS_OK ) );
        EXPECT_CALL(
            mock, LoadControlBit( LOGIC_EXPANDER_DO_2, LOGIC_EXPANDER_PORT_A, 7U, test_case.a1 ) )
            .WillOnce( Return( LOGIC_EXPANDER_STATUS_OK ) );

        EXPECT_TRUE(
            EXEC_DIGITAL_OUTPUT_Stage_Voltage( EXEC_DIGITAL_OUTPUT_CHANNEL_1, test_case.mode ) );
    }
}

TEST_F( ExecDigitalOutputTest, ConfigureRejectsInvalidEnabledModeWithoutHardwareChanges )
{
    ExecDigitalOutputConfig_T config = {};
    config.channels[0].is_enabled    = true;
    config.channels[0].mode =
        static_cast<ExecDigitalOutputMode_T>( EXEC_DIGITAL_OUTPUT_MODE_COUNT );
    exec_digital_output_state = EXEC_DIGITAL_OUTPUT_STATE_CONFIGURED;

    EXPECT_CALL( mock, ResetManyPins( _, _ ) ).Times( 0 );
    EXPECT_CALL( mock, LoadControlBit( _, _, _, _ ) ).Times( 0 );
    EXPECT_FALSE( EXEC_DIGITAL_OUTPUT_Configure( &config ) );
    EXPECT_TRUE( EXEC_DIGITAL_OUTPUT_Is_Configured() );
}

TEST_F( ExecDigitalOutputTest, ConfigureRejectsNullAndReconfigurationWhileStarted )
{
    EXPECT_CALL( mock, ResetManyPins( _, _ ) ).Times( 0 );
    EXPECT_FALSE( EXEC_DIGITAL_OUTPUT_Configure( nullptr ) );

    const ExecDigitalOutputConfig_T config = {};
    exec_digital_output_state              = EXEC_DIGITAL_OUTPUT_STATE_STARTED;
    EXPECT_FALSE( EXEC_DIGITAL_OUTPUT_Configure( &config ) );
    EXPECT_TRUE( EXEC_DIGITAL_OUTPUT_Is_Started() );
}

TEST_F( ExecDigitalOutputTest, ConfigureAllDisabledApplies3V3AndBecomesConfigured )
{
    ExecDigitalOutputConfig_T config = {};
    config.channels[3].mode =
        static_cast<ExecDigitalOutputMode_T>( EXEC_DIGITAL_OUTPUT_MODE_COUNT );

    ExpectSetOfAllChannels();
    EXPECT_CALL( mock, LoadControlBit( _, _, _, false ) )
        .Times( 20 )
        .WillRepeatedly( Return( LOGIC_EXPANDER_STATUS_OK ) );
    EXPECT_CALL( mock, SendControlBits() ).WillOnce( Return( LOGIC_EXPANDER_STATUS_OK ) );

    EXPECT_TRUE( EXEC_DIGITAL_OUTPUT_Configure( &config ) );
    EXPECT_TRUE( EXEC_DIGITAL_OUTPUT_Is_Configured() );
    EXPECT_FALSE( EXEC_DIGITAL_OUTPUT_Is_Started() );
}

TEST_F( ExecDigitalOutputTest, ConfigureRetainsCompleteSuccessfulConfiguration )
{
    ExecDigitalOutputConfig_T config = {};
    config.channels[0]               = { true, EXEC_DIGITAL_OUTPUT_MODE_5V, true };
    config.channels[9]               = { true, EXEC_DIGITAL_OUTPUT_MODE_24V, false };

    ExpectSetOfAllChannels();
    EXPECT_CALL( mock, LoadControlBit( _, _, _, _ ) )
        .Times( 20 )
        .WillRepeatedly( Return( LOGIC_EXPANDER_STATUS_OK ) );
    EXPECT_CALL( mock, SendControlBits() ).WillOnce( Return( LOGIC_EXPANDER_STATUS_OK ) );

    ASSERT_TRUE( EXEC_DIGITAL_OUTPUT_Configure( &config ) );
    EXPECT_EQ( exec_digital_output_configuration.channels[0].mode, EXEC_DIGITAL_OUTPUT_MODE_5V );
    EXPECT_TRUE( exec_digital_output_configuration.channels[0].initial_high );
    EXPECT_EQ( exec_digital_output_configuration.channels[9].mode, EXEC_DIGITAL_OUTPUT_MODE_24V );
}

TEST_F( ExecDigitalOutputTest, ConfigurationFailureLeavesSubsystemDisabled )
{
    ExecDigitalOutputConfig_T config = {};
    exec_digital_output_state        = EXEC_DIGITAL_OUTPUT_STATE_CONFIGURED;

    ExpectSetOfAllChannels();
    EXPECT_CALL( mock, LoadControlBit( _, _, _, _ ) )
        .WillOnce( Return( LOGIC_EXPANDER_STATUS_ERROR ) );

    EXPECT_FALSE( EXEC_DIGITAL_OUTPUT_Configure( &config ) );
    EXPECT_FALSE( EXEC_DIGITAL_OUTPUT_Is_Configured() );
    EXPECT_FALSE( EXEC_DIGITAL_OUTPUT_Start() );
}

TEST_F( ExecDigitalOutputTest, SendFailureLeavesSubsystemDisabled )
{
    const ExecDigitalOutputConfig_T config = {};
    exec_digital_output_state              = EXEC_DIGITAL_OUTPUT_STATE_CONFIGURED;

    ExpectSetOfAllChannels();
    EXPECT_CALL( mock, LoadControlBit( _, _, _, false ) )
        .Times( 20 )
        .WillRepeatedly( Return( LOGIC_EXPANDER_STATUS_OK ) );
    EXPECT_CALL( mock, SendControlBits() ).WillOnce( Return( LOGIC_EXPANDER_STATUS_BUSY ) );

    EXPECT_FALSE( EXEC_DIGITAL_OUTPUT_Configure( &config ) );
    EXPECT_FALSE( EXEC_DIGITAL_OUTPUT_Is_Configured() );
}

TEST_F( ExecDigitalOutputTest, StartSetsOnlyEnabledInitiallyHighChannelsInOneBatch )
{
    exec_digital_output_state                     = EXEC_DIGITAL_OUTPUT_STATE_CONFIGURED;
    exec_digital_output_configuration.channels[0] = { true, EXEC_DIGITAL_OUTPUT_MODE_3V3, true };
    exec_digital_output_configuration.channels[1] = { true, EXEC_DIGITAL_OUTPUT_MODE_5V, false };
    exec_digital_output_configuration.channels[4] = { true, EXEC_DIGITAL_OUTPUT_MODE_12V, true };
    exec_digital_output_configuration.channels[9] = { false, EXEC_DIGITAL_OUTPUT_MODE_24V, true };

    EXPECT_CALL( mock, ResetManyPins( _, 2U ) )
        .WillOnce( Invoke( []( GPIOOutput_T* pins, uint16_t length ) {
            EXPECT_THAT( std::vector<GPIOOutput_T>( pins, pins + length ),
                         ElementsAre( DIGITAL_OUTPUT_0, DIGITAL_OUTPUT_4 ) );
        } ) );

    EXPECT_TRUE( EXEC_DIGITAL_OUTPUT_Start() );
    EXPECT_TRUE( EXEC_DIGITAL_OUTPUT_Is_Started() );
}

TEST_F( ExecDigitalOutputTest, StartWithNoInitiallyHighChannelsPerformsNoGPIOWrite )
{
    exec_digital_output_state = EXEC_DIGITAL_OUTPUT_STATE_CONFIGURED;

    EXPECT_CALL( mock, ResetManyPins( _, _ ) ).Times( 0 );
    EXPECT_TRUE( EXEC_DIGITAL_OUTPUT_Start() );
    EXPECT_TRUE( EXEC_DIGITAL_OUTPUT_Is_Started() );
    EXPECT_FALSE( EXEC_DIGITAL_OUTPUT_Start() );
}

TEST_F( ExecDigitalOutputTest, StopSetsAllChannelsAndRetainsConfiguration )
{
    exec_digital_output_state                     = EXEC_DIGITAL_OUTPUT_STATE_STARTED;
    exec_digital_output_configuration.channels[2] = { true, EXEC_DIGITAL_OUTPUT_MODE_12V, true };

    ExpectSetOfAllChannels();
    EXPECT_TRUE( EXEC_DIGITAL_OUTPUT_Stop() );
    EXPECT_TRUE( EXEC_DIGITAL_OUTPUT_Is_Configured() );
    EXPECT_FALSE( EXEC_DIGITAL_OUTPUT_Is_Started() );
    EXPECT_EQ( exec_digital_output_configuration.channels[2].mode, EXEC_DIGITAL_OUTPUT_MODE_12V );
}

TEST_F( ExecDigitalOutputTest, StopRejectsCallsUnlessStarted )
{
    EXPECT_CALL( mock, SetManyPins( _, _ ) ).Times( 0 );
    EXPECT_FALSE( EXEC_DIGITAL_OUTPUT_Stop() );

    exec_digital_output_state = EXEC_DIGITAL_OUTPUT_STATE_CONFIGURED;
    EXPECT_FALSE( EXEC_DIGITAL_OUTPUT_Stop() );
}

TEST_F( ExecDigitalOutputTest, DirectRuntimeWrappersRemainUnconditional )
{
    GPIOOutput_T pins[] = { DIGITAL_OUTPUT_0, DIGITAL_OUTPUT_4 };

    EXPECT_CALL( mock, CombinePortPinMasks( pins, 2U ) ).WillOnce( Return( 0x0084U ) );
    EXPECT_EQ( EXEC_DIGITAL_OUTPUT_Combine_Port_Pin_Masks( pins, 2U ), 0x0084U );
    EXPECT_CALL( mock, ResetOutput( 0x0084U ) );
    EXPECT_CALL( mock, SetOutput( 0x0084U ) );
    EXEC_DIGITAL_OUTPUT_Set_Output( 0x0084U );
    EXEC_DIGITAL_OUTPUT_Reset_Output( 0x0084U );
}
