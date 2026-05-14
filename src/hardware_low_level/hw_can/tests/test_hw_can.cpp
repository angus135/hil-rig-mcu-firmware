/******************************************************************************
 *  File:       test_hw_can.cpp
 *  Author:     timothy vogelsang
 *  Created:    25-Mar-2026
 *
 *  Description:
 *      Unit tests for the hw_can module using GoogleTest and GoogleMock.
 *      This file validates the public API and behaviour defined in hw_can.h.
 *
 *  Notes:
 *      - Production code is written in C; tests are written in C++.
 *      - C headers must be included inside an extern "C" block.
 *      - GoogleMock may be used to mock external dependencies.
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>

extern "C"
{
#include "hw_can.h"
#include <stdint.h>
#include <stdbool.h>

// Add any other C headers required by the module
#include "hw_can.c"
}

using ::testing::_;
using ::testing::Return;

// Add additional C++ includes here if required
/**-----------------------------------------------------------------------------
 *  Test Constants / Macros
 *------------------------------------------------------------------------------
 */

static CAN_TypeDef fake_can1 {};
static CAN_TypeDef fake_can2 {};

CAN_HandleTypeDef hcan1 {};
CAN_HandleTypeDef hcan2{};

/**-----------------------------------------------------------------------------
 *  Test Doubles / Mocks
 *------------------------------------------------------------------------------
 */

class MockHWCAN
{
public:
    MOCK_METHOD(HAL_StatusTypeDef, CANInit,
                (CAN_HandleTypeDef* hcan), ());

    MOCK_METHOD(HAL_StatusTypeDef, CANConfigFilter,
                (CAN_HandleTypeDef* hcan,
                 CAN_FilterTypeDef* filter), ());

    MOCK_METHOD(HAL_StatusTypeDef, CANStart,
                (CAN_HandleTypeDef* hcan), ());

    MOCK_METHOD(HAL_StatusTypeDef, CANActivateNotification,
                (CAN_HandleTypeDef* hcan,
                 uint32_t flags), ());
};

static MockHWCAN* g_mock = nullptr;

extern "C"
HAL_StatusTypeDef HAL_CAN_Init(CAN_HandleTypeDef* hcan)
{
    return g_mock->CANInit(hcan);
}

extern "C"
HAL_StatusTypeDef HAL_CAN_ConfigFilter(
    CAN_HandleTypeDef* hcan,
    CAN_FilterTypeDef* filter)
{
    return g_mock->CANConfigFilter(hcan, filter);
}

extern "C"
HAL_StatusTypeDef HAL_CAN_Start(CAN_HandleTypeDef* hcan)
{
    return g_mock->CANStart(hcan);
}

extern "C"
HAL_StatusTypeDef HAL_CAN_ActivateNotification(
    CAN_HandleTypeDef* hcan,
    uint32_t flags)
{
    return g_mock->CANActivateNotification(hcan, flags);
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
class HWCANTest : public ::testing::Test
{
protected:
    MockHWCAN mock;

    void SetUp() override
    {
        g_mock = &mock;

        memset(&fake_can1, 0, sizeof(fake_can1));
        memset(&fake_can2, 0, sizeof(fake_can2));

        hcan1.Instance = &fake_can1;
        hcan2.Instance = &fake_can2;
    }

    void TearDown() override
    {
        g_mock = nullptr;
    }
};

/**-----------------------------------------------------------------------------
 *  Test Cases
 *------------------------------------------------------------------------------
 */

/*-----------------------------------------------------------------------------
 * Compute Properties Tests
 *---------------------------------------------------------------------------*/

TEST_F(HWCANTest, ComputePropertiesReturnsExpectedValuesFor1Mbps)
{
    CanProperties_T props =
        HW_CAN_Compute_Properties(1000000, 15, 800);

    EXPECT_EQ(props.bs1, 11);
    EXPECT_EQ(props.bs2, 3);
    EXPECT_EQ(props.psc, 3);
    EXPECT_EQ(props.timer_hz, 45000000);
}

TEST_F(HWCANTest, ComputePropertiesRejectsInvalidBitrate)
{
    CanProperties_T props =
        HW_CAN_Compute_Properties(0, 15, 800);

    EXPECT_EQ(props.bs1, 0);
    EXPECT_EQ(props.bs2, 0);
    EXPECT_EQ(props.psc, 0);
}

/*-----------------------------------------------------------------------------
 * Transmit Tests
 *---------------------------------------------------------------------------*/

TEST_F(HWCANTest, TransmitFailsWhenMailboxBusy)
{
    fake_can1.TSR = 0;

    uint8_t data[8] = {1,2,3,4,5,6,7,8};

    int result = HW_CAN_Transmit(&hcan1, data);

    EXPECT_EQ(result, 1);
}

TEST_F(HWCANTest, TransmitLoadsMailboxCorrectly)
{
    fake_can1.TSR = CAN_TSR_TME0;

    uint8_t data[8] = {1,2,3,4,5,6,7,8};

    int result = HW_CAN_Transmit(&hcan1, data);

    EXPECT_EQ(result, 0);

    EXPECT_EQ(fake_can1.sTxMailBox[0].TDTR, 8);

    EXPECT_EQ(fake_can1.sTxMailBox[0].TDLR,
              0x04030201);

    EXPECT_EQ(fake_can1.sTxMailBox[0].TDHR,
              0x08070605);

    EXPECT_TRUE(
        fake_can1.sTxMailBox[0].TIR &
        CAN_TI0R_TXRQ);
}

/*-----------------------------------------------------------------------------
 * Receive Tests
 *---------------------------------------------------------------------------*/

TEST_F(HWCANTest, ReceiveFailsWhenFIFOEmpty)
{
    fake_can1.RF0R = 0;

    uint8_t rx[8];

    int result = HW_CAN_Receive(&hcan1, rx);

    EXPECT_EQ(result, 1);
}

TEST_F(HWCANTest, ReceiveReadsFIFODataCorrectly)
{
    fake_can1.RF0R = CAN_RF0R_FMP0;

    fake_can1.sFIFOMailBox[0].RDLR = 0x04030201;
    fake_can1.sFIFOMailBox[0].RDHR = 0x08070605;

    uint8_t rx[8] = {0};

    int result = HW_CAN_Receive(&hcan1, rx);

    EXPECT_EQ(result, 0);

    EXPECT_EQ(rx[0], 1);
    EXPECT_EQ(rx[1], 2);
    EXPECT_EQ(rx[2], 3);
    EXPECT_EQ(rx[3], 4);
    EXPECT_EQ(rx[4], 5);
    EXPECT_EQ(rx[5], 6);
    EXPECT_EQ(rx[6], 7);
    EXPECT_EQ(rx[7], 8);

    EXPECT_TRUE(fake_can1.RF0R & CAN_RF0R_RFOM0);
}

/*-----------------------------------------------------------------------------
 * Buffer Tests
 *---------------------------------------------------------------------------*/

TEST_F(HWCANTest, TxBufferWriteAndPopWorks)
{
    uint8_t tx[1][CAN_PACKET_SIZE] =
    {
        {1,2,3,4,5,6,7,8}
    };

    EXPECT_EQ(
        HW_CAN_Tx_Buffer_Write1(tx, 1),
        0);

    uint8_t out[CAN_PACKET_SIZE] = {0};

    EXPECT_EQ(
        HW_CAN_Tx_Buffer_Pop1(out),
        0);

    EXPECT_EQ(out[0], 1);
    EXPECT_EQ(out[7], 8);
}

TEST_F(HWCANTest, BufferPopFailsWhenEmpty)
{
    uint8_t out[CAN_PACKET_SIZE];

    EXPECT_EQ(
        HW_CAN_Tx_Buffer_Pop1(out),
        1);
}

/*-----------------------------------------------------------------------------
 * ISR Tests
 *---------------------------------------------------------------------------*/

TEST_F(HWCANTest, TxIRQDisablesInterruptWhenBufferEmpty)
{
    fake_can1.IER = CAN_IER_TMEIE;

    HW_CAN_CH1_TX_IRQ_HANDLER();

    EXPECT_FALSE(fake_can1.IER & CAN_IER_TMEIE);
}

TEST_F(HWCANTest, TxIRQTransmitsBufferedPacket)
{
    fake_can1.TSR = CAN_TSR_TME0;

    uint8_t tx[1][CAN_PACKET_SIZE] =
    {
        {9,8,7,6,5,4,3,2}
    };

    HW_CAN_Tx_Buffer_Write1(tx, 1);

    HW_CAN_CH1_TX_IRQ_HANDLER();

    EXPECT_EQ(
        fake_can1.sTxMailBox[0].TDLR,
        0x06070809);

    EXPECT_EQ(
        fake_can1.sTxMailBox[0].TDHR,
        0x02030405);
}

/*-----------------------------------------------------------------------------
 * Configure Tests
 *---------------------------------------------------------------------------*/

TEST_F(HWCANTest, ConfigureReturns0OnSuccess)
{
    EXPECT_CALL(mock, CANInit(_))
        .WillOnce(Return(HAL_OK));

    EXPECT_CALL(mock, CANConfigFilter(_, _))
        .WillOnce(Return(HAL_OK));

    EXPECT_CALL(mock, CANStart(_))
        .WillOnce(Return(HAL_OK));

    EXPECT_CALL(mock,
        CANActivateNotification(_, _))
        .WillOnce(Return(HAL_OK));

    int result = HW_CAN_Configure1(1000000);

    EXPECT_EQ(result, 0);
}

TEST_F(HWCANTest, ConfigureReturns1WhenInitFails)
{
    EXPECT_CALL(mock, CANInit(_))
        .WillOnce(Return(HAL_ERROR));

    int result = HW_CAN_Configure1(1000000);

    EXPECT_EQ(result, 1);
}
