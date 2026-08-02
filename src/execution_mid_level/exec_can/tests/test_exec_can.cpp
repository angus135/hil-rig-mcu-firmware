/******************************************************************************
 *  File:       test_exec_can.cpp
 *  Author:     Angus Corr
 *  Created:    16-Dec-2025
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
#include "exec_can.h" /* Module under test */
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

static HW_CAN_Tx_Status_T status1;
static HW_CAN_Tx_Status_T status2;
static HW_CAN_Result_T    recover_result1;
static HW_CAN_Result_T    recover_result2;
static uint32_t           configure_bitrate;
static uint16_t           configure_bank;
static uint16_t           configure_id;
static uint16_t           configure_mask;
static uint8_t            configured_channel;
static uint8_t            recovered_channel;

extern "C" bool HW_CAN_Channl1_sent( void )
{
    return status1 == HW_CAN_TX_STATUS_COMPLETE;
}

extern "C" bool HW_CAN_Channl2_sent( void )
{
    return status2 == HW_CAN_TX_STATUS_COMPLETE;
}

extern "C" HW_CAN_Tx_Status_T HW_CAN_Tx_Status1( void )
{
    return status1;
}

extern "C" HW_CAN_Tx_Status_T HW_CAN_Tx_Status2( void )
{
    return status2;
}

extern "C" int HW_CAN_Configure1( uint32_t bitrate, uint16_t filter_bank, uint16_t filter_id,
                                  uint16_t filter_mask )
{
    configure_bitrate  = bitrate;
    configure_bank     = filter_bank;
    configure_id       = filter_id;
    configure_mask     = filter_mask;
    configured_channel = 1U;
    return 11;
}

extern "C" int HW_CAN_Configure2( uint32_t bitrate, uint16_t filter_bank, uint16_t filter_id,
                                  uint16_t filter_mask )
{
    configure_bitrate  = bitrate;
    configure_bank     = filter_bank;
    configure_id       = filter_id;
    configure_mask     = filter_mask;
    configured_channel = 2U;
    return 12;
}

extern "C" HW_CAN_Result_T HW_CAN_Recover1( void )
{
    recovered_channel = 1U;
    return recover_result1;
}

extern "C" HW_CAN_Result_T HW_CAN_Recover2( void )
{
    recovered_channel = 2U;
    return recover_result2;
}

extern "C" uint32_t HW_CAN_Rx_Dropped_Count1( void )
{
    return 0U;
}

extern "C" uint32_t HW_CAN_Rx_Dropped_Count2( void )
{
    return 0U;
}

extern "C" HW_CAN_Result_T HW_CAN_Tx_Trigger1( void )
{
    return HW_CAN_RESULT_OK;
}

extern "C" HW_CAN_Result_T HW_CAN_Tx_Trigger2( void )
{
    return HW_CAN_RESULT_OK;
}

extern "C" HW_CAN_Result_T HW_CAN_Tx_Buffer_Write1( CAN_Packet_T[], uint16_t )
{
    return HW_CAN_RESULT_OK;
}

extern "C" HW_CAN_Result_T HW_CAN_Tx_Buffer_Write2( CAN_Packet_T[], uint16_t )
{
    return HW_CAN_RESULT_OK;
}

extern "C" uint16_t HW_CAN_Rx_Buffer_Read1( CAN_Packet_T[], uint16_t )
{
    return 0U;
}

extern "C" uint16_t HW_CAN_Rx_Buffer_Read2( CAN_Packet_T[], uint16_t )
{
    return 0U;
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
class ExecCANTest : public ::testing::Test
{
protected:
    void SetUp( void ) override
    {
        status1            = HW_CAN_TX_STATUS_IDLE;
        status2            = HW_CAN_TX_STATUS_IDLE;
        recover_result1    = HW_CAN_RESULT_OK;
        recover_result2    = HW_CAN_RESULT_OK;
        configure_bitrate  = 0U;
        configure_bank     = 0U;
        configure_id       = 0U;
        configure_mask     = 0U;
        configured_channel = 0U;
        recovered_channel  = 0U;
    }

    void TearDown( void ) override
    {
    }
};

/**-----------------------------------------------------------------------------
 *  Test Cases
 *------------------------------------------------------------------------------
 */

TEST_F( ExecCANTest, ConfigurationWrappersPreserveArgumentsAndResults )
{
    EXPECT_EQ( EXEC_CAN_Configure1( 500000U, 13U, 0x123U, 0x7FFU ), 11 );
    EXPECT_EQ( configured_channel, 1U );
    EXPECT_EQ( configure_bitrate, 500000U );
    EXPECT_EQ( configure_bank, 13U );
    EXPECT_EQ( configure_id, 0x123U );
    EXPECT_EQ( configure_mask, 0x7FFU );

    EXPECT_EQ( EXEC_CAN_Configure2( 250000U, 14U, 0x456U, 0x700U ), 12 );
    EXPECT_EQ( configured_channel, 2U );
    EXPECT_EQ( configure_bitrate, 250000U );
    EXPECT_EQ( configure_bank, 14U );
    EXPECT_EQ( configure_id, 0x456U );
    EXPECT_EQ( configure_mask, 0x700U );
}

TEST_F( ExecCANTest, StatusAndRecoveryWrappersPreserveChannelResults )
{
    status1         = HW_CAN_TX_STATUS_ACTIVE;
    status2         = HW_CAN_TX_STATUS_ERROR;
    recover_result1 = HW_CAN_RESULT_BUSY;
    recover_result2 = HW_CAN_RESULT_OK;

    EXPECT_EQ( EXEC_CAN_Tx_Status1(), HW_CAN_TX_STATUS_ACTIVE );
    EXPECT_EQ( EXEC_CAN_Tx_Status2(), HW_CAN_TX_STATUS_ERROR );
    EXPECT_EQ( EXEC_CAN_Recover1(), HW_CAN_RESULT_BUSY );
    EXPECT_EQ( recovered_channel, 1U );
    EXPECT_EQ( EXEC_CAN_Recover2(), HW_CAN_RESULT_OK );
    EXPECT_EQ( recovered_channel, 2U );
}
