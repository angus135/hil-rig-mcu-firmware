/******************************************************************************
 *  File:       test_hw_i2c.cpp
 *  Author:     Angus Corr
 *  Created:    25-Mar-2026
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
#include "hw_i2c.h" /* Module under test */
#include <stdint.h>
#include <stdbool.h>
}

/**-----------------------------------------------------------------------------
 *  Test Constants / Macros
 *------------------------------------------------------------------------------
 */

static constexpr uint8_t TEST_I2C_ADDR = 0x2AU;

static HwI2cConfig_T MakeConfig( HwI2cRole_T role, HwI2cSpeed_T speed,
                                 HwI2cTransferMode_T transfer_mode, uint8_t own_address_7bit,
                                 bool rx_enabled, bool tx_enabled )
{
    HwI2cConfig_T config = {};
    config.role = role;
    config.speed = speed;
    config.transfer_mode = transfer_mode;
    config.own_address_7bit = own_address_7bit;
    config.rx_enabled = rx_enabled;
    config.tx_enabled = tx_enabled;
    return config;
}

/**-----------------------------------------------------------------------------
 *  Test Doubles / Mocks
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Test Fixture
 *------------------------------------------------------------------------------
 */

/**
 * @brief Test fixture for module tests.
 *
 * Provides a consistent setup/teardown environment for all test cases.
 */
class HWI2CTest : public ::testing::Test
{
protected:
    void SetUp( void ) override
    {
        const HwI2cConfig_T config = MakeConfig( HW_I2C_ROLE_MASTER, HW_I2C_SPEED_100KHZ,
                                                 HW_I2C_TRANSFER_INTERRUPT, 0x12U, true, true );

        ASSERT_TRUE( HW_I2C_Configure_Channel( HW_I2C_CHANNEL_1, &config ) );
        ASSERT_TRUE( HW_I2C_Configure_Channel( HW_I2C_CHANNEL_2, &config ) );
        ASSERT_TRUE( HW_I2C_Configure_Internal_Channel() );
    }

    void TearDown( void ) override
    {
    }
};

/**-----------------------------------------------------------------------------
 *  Test Cases
 *------------------------------------------------------------------------------
 */

TEST_F( HWI2CTest, ConfigureChannelRejectsNullConfig )
{
    EXPECT_FALSE( HW_I2C_Configure_Channel( HW_I2C_CHANNEL_1, nullptr ) );
}

TEST_F( HWI2CTest, ConfigureChannelRejectsInvalidExternalChannelEnum )
{
    const HwI2cConfig_T config = MakeConfig( HW_I2C_ROLE_MASTER, HW_I2C_SPEED_100KHZ,
                                             HW_I2C_TRANSFER_INTERRUPT, 0x10U, true, true );

    EXPECT_FALSE( HW_I2C_Configure_Channel( static_cast<HwI2cChannel_T>( 3 ), &config ) );
}

TEST_F( HWI2CTest, ConfigureInternalChannelFixedPolicyValidation )
{
    const HwI2cConfig_T invalid_internal_speed = MakeConfig(
        HW_I2C_ROLE_MASTER, HW_I2C_SPEED_400KHZ, HW_I2C_TRANSFER_INTERRUPT, 0x00U, true, true );

    EXPECT_FALSE( HW_I2C_Configure_Channel( HW_I2C_CHANNEL_INTERNAL_FMPI1, &invalid_internal_speed ) );

    EXPECT_TRUE( HW_I2C_Configure_Internal_Channel() );
}

TEST_F( HWI2CTest, RxStartRejectsInvalidChannel )
{
    EXPECT_FALSE( HW_I2C_Rx_Start( static_cast<HwI2cChannel_T>( 3 ), TEST_I2C_ADDR, 8U ) );
}

TEST_F( HWI2CTest, RxStartRejectsMasterInvalidAddressAndZeroLength )
{
    EXPECT_FALSE( HW_I2C_Rx_Start( HW_I2C_CHANNEL_1, 0x80U, 8U ) );
    EXPECT_FALSE( HW_I2C_Rx_Start( HW_I2C_CHANNEL_1, TEST_I2C_ADDR, 0U ) );
}

TEST_F( HWI2CTest, RxPeekReturnsEmptyWhenNoDataAvailable )
{
    const HwI2cRxSpans_T spans = HW_I2C_Rx_Peek( HW_I2C_CHANNEL_1 );

    EXPECT_EQ( spans.total_length_bytes, 0U );
    EXPECT_EQ( spans.first_span.length_bytes, 0U );
    EXPECT_EQ( spans.second_span.length_bytes, 0U );
}

TEST_F( HWI2CTest, TxLoadBufferRejectsInvalidInput )
{
    uint8_t payload[4] = { 1U, 2U, 3U, 4U };

    EXPECT_FALSE( HW_I2C_Tx_Load_Buffer( HW_I2C_CHANNEL_1, TEST_I2C_ADDR, nullptr, 4U ) );
    EXPECT_FALSE( HW_I2C_Tx_Load_Buffer( HW_I2C_CHANNEL_1, TEST_I2C_ADDR, payload, 0U ) );
    EXPECT_FALSE( HW_I2C_Tx_Load_Buffer( HW_I2C_CHANNEL_1, 0x80U, payload, 4U ) );
    EXPECT_FALSE( HW_I2C_Tx_Load_Buffer( HW_I2C_CHANNEL_1, TEST_I2C_ADDR, payload,
                                         static_cast<uint16_t>( HW_I2C_TX_BUFFER_SIZE + 1U ) ) );
}

TEST_F( HWI2CTest, TxLoadAndTriggerFlowLocksUntilReconfigured )
{
    uint8_t payload[3] = { 0xAAU, 0xBBU, 0xCCU };

    EXPECT_TRUE( HW_I2C_Tx_Load_Buffer( HW_I2C_CHANNEL_1, TEST_I2C_ADDR, payload, 3U ) );
    EXPECT_FALSE( HW_I2C_Tx_Load_Buffer( HW_I2C_CHANNEL_1, TEST_I2C_ADDR, payload, 3U ) );

    EXPECT_TRUE( HW_I2C_Tx_Trigger( HW_I2C_CHANNEL_1 ) );
    EXPECT_FALSE( HW_I2C_Tx_Trigger( HW_I2C_CHANNEL_1 ) );
    EXPECT_FALSE( HW_I2C_Tx_Load_Buffer( HW_I2C_CHANNEL_1, TEST_I2C_ADDR, payload, 3U ) );

    const HwI2cConfig_T config = MakeConfig( HW_I2C_ROLE_MASTER, HW_I2C_SPEED_100KHZ,
                                             HW_I2C_TRANSFER_INTERRUPT, 0x12U, true, true );

    ASSERT_TRUE( HW_I2C_Configure_Channel( HW_I2C_CHANNEL_1, &config ) );
    EXPECT_TRUE( HW_I2C_Tx_Load_Buffer( HW_I2C_CHANNEL_1, TEST_I2C_ADDR, payload, 3U ) );
}

TEST_F( HWI2CTest, TxTriggerRequiresValidPreparedState )
{
    EXPECT_FALSE( HW_I2C_Tx_Trigger( static_cast<HwI2cChannel_T>( 3 ) ) );

    const HwI2cConfig_T rx_only = MakeConfig( HW_I2C_ROLE_MASTER, HW_I2C_SPEED_100KHZ,
                                              HW_I2C_TRANSFER_INTERRUPT, 0x22U, true, false );

    ASSERT_TRUE( HW_I2C_Configure_Channel( HW_I2C_CHANNEL_2, &rx_only ) );
    EXPECT_FALSE( HW_I2C_Tx_Trigger( HW_I2C_CHANNEL_2 ) );
}
