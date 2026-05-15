/******************************************************************************
 *  File:       test_logic_expander.cpp
 *  Author:     Coen Pasitchnyj
 *  Created:    20-Apr-2026
 *
 *  Description:
 *      Unit tests for the logic expander module.
 *
 *      These tests verify self-configuration, shadow-state updates, and
 *      payload emission for MCP23017 control writes over FMPI2C1.
 *
 *  Notes:
 *      - Production code is written in C; tests are written in C++.
 *      - C headers and implementation files are included inside extern "C".
 *      - The low-level hw_i2c functions are mocked so logic_expander.c can be
 *        exercised without hardware.
 *      - The fixture clears shared expander state before each test.
 *
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <array>
#include <cstring>

extern "C"
{
#include "logic_expander.h" /* Module under test */
#include "../../hardware_low_level/hw_i2c/hw_i2c.h"
#include <stdint.h>
#include <stdbool.h>

#include "../logic_expander.c" /* Module under test */  // NOLINT
}

/**-----------------------------------------------------------------------------
 *  Test Constants / Macros
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Test Doubles / Mocks
 *------------------------------------------------------------------------------
 */

class MockHWI2C
{
public:
    MOCK_METHOD( HWI2CStatus_T, ConfigureInternal, ( uint16_t own_address_7bit ), () );
    MOCK_METHOD( bool, LoadStageBuffer,
                 ( HWI2CChannel_T channel, const uint8_t* data, uint16_t length ), () );
    MOCK_METHOD( bool, TriggerMasterTransmitInternal, ( uint16_t device_address_7bit ), () );
    MOCK_METHOD( bool, TriggerMasterReceiveInternal,
                 ( uint16_t device_address_7bit, uint16_t expected_length ), () );
};

static MockHWI2C* g_mock_hw_i2c = nullptr;

extern "C"
{
HWI2CStatus_T HW_I2C_Configure_Internal_FMPI2C1( uint16_t own_address_7bit )
{
    return g_mock_hw_i2c->ConfigureInternal( own_address_7bit );
}

bool HW_I2C_Load_Stage_Buffer( HWI2CChannel_T channel, const uint8_t* data, uint16_t length )
{
    return g_mock_hw_i2c->LoadStageBuffer( channel, data, length );
}

bool HW_I2C_Trigger_Master_Transmit_Internal( uint16_t device_address_7bit )
{
    return g_mock_hw_i2c->TriggerMasterTransmitInternal( device_address_7bit );
}

bool HW_I2C_Trigger_Master_Receive_Internal( uint16_t device_address_7bit,
                                             uint16_t expected_length )
{
    return g_mock_hw_i2c->TriggerMasterReceiveInternal( device_address_7bit, expected_length );
}
}

using ::testing::InSequence;
using ::testing::Return;
using ::testing::StrictMock;

/**-----------------------------------------------------------------------------
 *  Test Fixture
 *------------------------------------------------------------------------------
 */

/**
 * @brief Test fixture for module tests.
 *
 * Provides a consistent setup/teardown environment for all test cases.
 */
class ExampleTest : public ::testing::Test
{
protected:
    StrictMock<MockHWI2C> mock_hw_i2c;

    void SetUp( void ) override
    {
        g_mock_hw_i2c = &mock_hw_i2c;
        std::memset( logic_expander_state, 0, sizeof( logic_expander_state ) );
        logic_expander_ready = false;
    }

    void TearDown( void ) override
    {
        g_mock_hw_i2c = nullptr;
    }
};

/**-----------------------------------------------------------------------------
 *  Test Cases
 *------------------------------------------------------------------------------
 */

TEST_F( ExampleTest, SelfConfig_InitializesActiveDeviceAndReportsSuccess )
{
    EXPECT_CALL( mock_hw_i2c, ConfigureInternal( 0x33U ) ).WillOnce( Return( HW_I2C_STATUS_OK ) );
    EXPECT_CALL( mock_hw_i2c,
                 LoadStageBuffer( HW_I2C_CHANNEL_FMPI2C1, ::testing::_, ::testing::_ ) )
        .Times( 8 );
    EXPECT_CALL( mock_hw_i2c, TriggerMasterTransmitInternal( 0x20U ) ).Times( 8 );

    EXPECT_EQ( LOGIC_EXPANDER_Self_Config(), LOGIC_EXPANDER_STATUS_OK );
    EXPECT_TRUE( logic_expander_ready );
    EXPECT_EQ( logic_expander_state[0].device_address_7bit, 0x20U );
    EXPECT_EQ( logic_expander_state[0].olat_a, 0x00U );
    EXPECT_EQ( logic_expander_state[0].olat_b, 0xFFU );
}

TEST_F( ExampleTest, SelfConfig_PropagatesHardwareErrorAndLeavesNotReady )
{
    EXPECT_CALL( mock_hw_i2c, ConfigureInternal( 0x33U ) ).WillOnce( Return( HW_I2C_STATUS_BUSY ) );
    EXPECT_CALL( mock_hw_i2c,
                 LoadStageBuffer( HW_I2C_CHANNEL_FMPI2C1, ::testing::_, ::testing::_ ) )
        .Times( 0 );
    EXPECT_CALL( mock_hw_i2c, TriggerMasterTransmitInternal( ::testing::_ ) ).Times( 0 );

    EXPECT_EQ( LOGIC_EXPANDER_Self_Config(), LOGIC_EXPANDER_STATUS_ERROR );
    EXPECT_FALSE( logic_expander_ready );
}

TEST_F( ExampleTest, InternalConfig_CallsLowLevelConfiguration )
{
    EXPECT_CALL( mock_hw_i2c, ConfigureInternal( 0x33U ) ).WillOnce( Return( HW_I2C_STATUS_OK ) );

    EXPECT_EQ( LOGIC_EXPANDER_I2C_Internal_Config(), LOGIC_EXPANDER_STATUS_OK );
}

TEST_F( ExampleTest, InternalTransmit_ForwardsToInternalChannel )
{
    const std::array<uint8_t, 2U> payload = { 0xA1U, 0xB2U };

    {
        InSequence sequence;
        EXPECT_CALL( mock_hw_i2c,
                     LoadStageBuffer( HW_I2C_CHANNEL_FMPI2C1, payload.data(), payload.size() ) )
            .WillOnce( Return( true ) );
        EXPECT_CALL( mock_hw_i2c, TriggerMasterTransmitInternal( 0x33U ) )
            .WillOnce( Return( true ) );
    }

    EXPECT_TRUE( LOGIC_EXPANDER_Master_Transmit_Internal( 0x33U, payload.data(), payload.size() ) );
}

TEST_F( ExampleTest, InternalReceive_ForwardsToInternalChannel )
{
    EXPECT_CALL( mock_hw_i2c, TriggerMasterReceiveInternal( 0x33U, 12U ) )
        .WillOnce( Return( true ) );

    EXPECT_TRUE( LOGIC_EXPANDER_Start_Master_Receive_Internal( 0x33U, 12U ) );
}

TEST_F( ExampleTest, LoadControlBit_ValidatesInputsAndUpdatesShadowState )
{
    EXPECT_EQ( LOGIC_EXPANDER_Load_Control_Bit( LOGIC_EXPANDER_RESERVED_0, LOGIC_EXPANDER_PORT_A,
                                                8U, true ),
               LOGIC_EXPANDER_STATUS_INVALID_PARAM );

    EXPECT_EQ( LOGIC_EXPANDER_Load_Control_Bit( LOGIC_EXPANDER_RESERVED_0, LOGIC_EXPANDER_PORT_A,
                                                3U, true ),
               LOGIC_EXPANDER_STATUS_OK );
    EXPECT_EQ( logic_expander_state[0].olat_a, 0x08U );

    EXPECT_EQ( LOGIC_EXPANDER_Load_Control_Bit( LOGIC_EXPANDER_RESERVED_0, LOGIC_EXPANDER_PORT_A,
                                                3U, false ),
               LOGIC_EXPANDER_STATUS_OK );
    EXPECT_EQ( logic_expander_state[0].olat_a, 0x00U );
}

TEST_F( ExampleTest, SendControlBits_ReturnsNotReadyBeforeSelfConfig )
{
    EXPECT_EQ( LOGIC_EXPANDER_Send_Control_Bits(), LOGIC_EXPANDER_STATUS_NOT_READY );
}

TEST_F( ExampleTest, SendControlBits_WritesActiveShadowRegisters )
{
    logic_expander_ready                        = true;
    logic_expander_state[0].device_address_7bit = 0x20U;
    logic_expander_state[0].olat_a              = 0x5AU;
    logic_expander_state[0].olat_b              = 0xA5U;

    const std::array<uint8_t, 3U> expected_payload = { 0x14U, 0x5AU, 0xA5U };

    EXPECT_CALL( mock_hw_i2c, LoadStageBuffer( HW_I2C_CHANNEL_FMPI2C1, expected_payload.data(),
                                               expected_payload.size() ) )
        .WillOnce( Return( true ) );
    EXPECT_CALL( mock_hw_i2c, TriggerMasterTransmitInternal( 0x20U ) ).WillOnce( Return( true ) );

    EXPECT_EQ( LOGIC_EXPANDER_Send_Control_Bits(), LOGIC_EXPANDER_STATUS_OK );
}

TEST_F( ExampleTest, GetStateSnapshot_CopiesShadowState )
{
    logic_expander_state[2].device_address_7bit = 0x25U;
    logic_expander_state[2].olat_a              = 0x11U;
    logic_expander_state[2].olat_b              = 0x22U;

    LogicExpanderStateSnapshot_T snapshot{};

    EXPECT_EQ(
        LOGIC_EXPANDER_Get_State_Snapshot( static_cast<LogicExpanderIndex_T>( 2U ), &snapshot ),
        LOGIC_EXPANDER_STATUS_OK );
    EXPECT_EQ( snapshot.device_address_7bit, 0x25U );
    EXPECT_EQ( snapshot.olat_a, 0x11U );
    EXPECT_EQ( snapshot.olat_b, 0x22U );
}

TEST_F( ExampleTest, GetStateSnapshot_RejectsInvalidParameters )
{
    EXPECT_EQ( LOGIC_EXPANDER_Get_State_Snapshot(
                   static_cast<LogicExpanderIndex_T>( LOGIC_EXPANDER_COUNT ), nullptr ),
               LOGIC_EXPANDER_STATUS_INVALID_PARAM );
}
