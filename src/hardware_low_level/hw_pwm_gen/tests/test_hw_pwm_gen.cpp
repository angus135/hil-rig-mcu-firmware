/******************************************************************************
 *  File:       test_hw_pwm_gen.cpp
 *  Author:     timothy vogelsang
 *  Created:    17-May-2026
 *
 *  Description:
 *      Unit tests for the hw_pwm_gen module using GoogleTest and GoogleMock.
 *      This file validates the public API and behaviour defined in hw_pwm_gen.h.
 *
 *  Notes:
 *      - Production code is written in C; tests are written in C++.
 *      - C headers must be included inside an extern "C" block.
 *      - GoogleMock is used to mock HAL dependencies.
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>

extern "C"
{
#include "hw_pwm_gen.h"
#include "hw_pwm_gen_mocks.h"

#include <stdint.h>
#include <stdbool.h>
}

using ::testing::_;
using ::testing::Return;

/**-----------------------------------------------------------------------------
 *  Fake Hardware Registers
 *------------------------------------------------------------------------------
 */

TIM_TypeDef mock_tim12_regs{};
TIM_TypeDef mock_tim8_regs{};

TIM_HandleTypeDef htim12{};
TIM_HandleTypeDef htim8{};

/**-----------------------------------------------------------------------------
 *  Test Doubles / Mocks
 *------------------------------------------------------------------------------
 */

class MockHWPWM
{
public:
    MOCK_METHOD( HAL_StatusTypeDef, TIMPWMStart, ( TIM_HandleTypeDef * htim, uint32_t channel ),
                 () );
};

static MockHWPWM* g_mock = nullptr;

/**-----------------------------------------------------------------------------
 *  HAL Mock Implementations
 *------------------------------------------------------------------------------
 */

extern "C" HAL_StatusTypeDef HAL_TIM_PWM_Start( TIM_HandleTypeDef* htim, uint32_t channel )
{
    return g_mock->TIMPWMStart( htim, channel );
}

/**-----------------------------------------------------------------------------
 *  LL Mock Implementations
 *------------------------------------------------------------------------------
 */

extern "C" void LL_TIM_OC_SetCompareCH1( TIM_TypeDef* TIMx, uint32_t CompareValue )
{
    TIMx->CCR1 = CompareValue;
}

extern "C" void LL_TIM_OC_SetCompareCH2( TIM_TypeDef* TIMx, uint32_t CompareValue )
{
    TIMx->CCR2 = CompareValue;
}

extern "C" void LL_TIM_OC_SetCompareCH3( TIM_TypeDef* TIMx, uint32_t CompareValue )
{
    TIMx->CCR3 = CompareValue;
}

extern "C" void LL_TIM_OC_SetCompareCH4( TIM_TypeDef* TIMx, uint32_t CompareValue )
{
    TIMx->CCR4 = CompareValue;
}

extern "C" void LL_TIM_SetAutoReload( TIM_TypeDef* TIMx, uint32_t AutoReload )
{
    TIMx->ARR = AutoReload;
}

extern "C" void LL_TIM_SetPrescaler( TIM_TypeDef* TIMx, uint32_t Prescaler )
{
    TIMx->PSC = Prescaler;
}

/**-----------------------------------------------------------------------------
 *  Test Fixture
 *------------------------------------------------------------------------------
 */

class HWPWMGenTest : public ::testing::Test
{
protected:
    MockHWPWM mock;

    void SetUp() override
    {
        g_mock = &mock;

        memset( &mock_tim12_regs, 0, sizeof( mock_tim12_regs ) );
        memset( &mock_tim8_regs, 0, sizeof( mock_tim8_regs ) );

        htim12.Instance = &mock_tim12_regs;
        htim8.Instance = &mock_tim8_regs;
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
 * Compute PSC Tests
 *---------------------------------------------------------------------------*/

TEST_F( HWPWMGenTest, ComputePSCReturnsMaxForInvalidFrequency )
{
    uint16_t psc = HW_PWM_GEN_compute_psc( 10000000, 100000000 );

    EXPECT_EQ( psc, 0xFFFF );
}

TEST_F( HWPWMGenTest, ComputePSCReturnsMaxForZeroFrequency )
{
    uint16_t psc = HW_PWM_GEN_compute_psc( 0, 1000000 );

    EXPECT_EQ( psc, 0xFFFF );
}

TEST_F( HWPWMGenTest, ComputePSCReturnsOneWhenARRFitsWithoutDivision )
{
    uint16_t psc = HW_PWM_GEN_compute_psc( 1000, 1000000 );

    EXPECT_EQ( psc, 0 );
}

TEST_F( HWPWMGenTest, ComputePSCIncreasesDividerForLowFrequency )
{
    uint16_t psc = HW_PWM_GEN_compute_psc( 1, 1000000 );

    EXPECT_GT( psc, 1 );
}

TEST_F( HWPWMGenTest, ComputePSCHandlesBoundaryCondition )
{
    uint16_t psc = HW_PWM_GEN_compute_psc( 16, 1000000 );

    EXPECT_EQ( psc, 0 );
}

/*-----------------------------------------------------------------------------
 * Compute ARR Tests
 *---------------------------------------------------------------------------*/

TEST_F( HWPWMGenTest, ComputeARRReturnsExpectedValue )
{
    uint16_t arr = HW_PWM_GEN_compute_arr( 1000, 1000000, 1 );

    EXPECT_EQ( arr, 499 );
}

TEST_F( HWPWMGenTest, ComputeARRHandlesLargePrescaler )
{
    uint16_t arr = HW_PWM_GEN_compute_arr( 100, 1000000, 9 );

    EXPECT_EQ( arr, 999 );
}

TEST_F( HWPWMGenTest, ComputeARRReturnsZeroForMaximumFrequency )
{
    uint16_t arr = HW_PWM_GEN_compute_arr( 1000000, 1000000, 0 );

    EXPECT_EQ( arr, 0 );
}

/*-----------------------------------------------------------------------------
 * Compute CCR Tests
 *---------------------------------------------------------------------------*/

TEST_F( HWPWMGenTest, ComputeCCRReturnsZeroForZeroDuty )
{
    uint16_t ccr = HW_PWM_GEN_compute_ccr( 0, 999 );

    EXPECT_EQ( ccr, 0 );
}

TEST_F( HWPWMGenTest, ComputeCCRReturnsHalfDutyCorrectly )
{
    uint16_t ccr = HW_PWM_GEN_compute_ccr( 500, 999 );

    EXPECT_EQ( ccr, 500 );
}

TEST_F( HWPWMGenTest, ComputeCCRReturnsQuarterDutyCorrectly )
{
    uint16_t ccr = HW_PWM_GEN_compute_ccr( 250, 999 );

    EXPECT_EQ( ccr, 250 );
}

TEST_F( HWPWMGenTest, ComputeCCRReturnsFullScaleFor100PercentDuty )
{
    uint16_t ccr = HW_PWM_GEN_compute_ccr( 1000, 999 );

    EXPECT_EQ( ccr, 1000 );
}

TEST_F( HWPWMGenTest, ComputeCCRClampsDutyAbove100Percent )
{
    uint16_t ccr = HW_PWM_GEN_compute_ccr( 1500, 999 );

    EXPECT_EQ( ccr, 1000 );
}

/*-----------------------------------------------------------------------------
 * Direct PWM Register Tests
 *---------------------------------------------------------------------------*/

TEST_F( HWPWMGenTest, SetPWM1DirectUpdatesTIM12Registers )
{
    HW_PWM_GEN_Set_PWM1_Direct( 1000, 250, 4 );

    EXPECT_EQ( mock_tim12_regs.CCR2, 250 );
    EXPECT_EQ( mock_tim12_regs.ARR, 1000 );
    EXPECT_EQ( mock_tim12_regs.PSC, 4 );
}

TEST_F( HWPWMGenTest, SetPWM2DirectUpdatesTIM12Registers )
{
    HW_PWM_GEN_Set_PWM2_Direct( 2000, 500, 8 );

    EXPECT_EQ( mock_tim12_regs.CCR1, 500 );
    EXPECT_EQ( mock_tim12_regs.ARR, 2000 );
    EXPECT_EQ( mock_tim12_regs.PSC, 8 );
}

TEST_F( HWPWMGenTest, SetPWM1DirectDoesNotModifyOtherChannels )
{
    HW_PWM_GEN_Set_PWM1_Direct( 1000, 111, 2 );

    EXPECT_EQ( mock_tim12_regs.CCR1, 0 );
    EXPECT_EQ( mock_tim12_regs.CCR2, 111 );
    EXPECT_EQ( mock_tim12_regs.CCR3, 0 );
    EXPECT_EQ( mock_tim12_regs.CCR4, 0 );
}

TEST_F( HWPWMGenTest, SetPWM2DirectDoesNotModifyOtherChannels )
{
    HW_PWM_GEN_Set_PWM2_Direct( 1000, 222, 3 );

    EXPECT_EQ( mock_tim12_regs.CCR1, 222 );
    EXPECT_EQ( mock_tim12_regs.CCR2, 0 );
    EXPECT_EQ( mock_tim12_regs.CCR3, 0 );
    EXPECT_EQ( mock_tim12_regs.CCR4, 0 );
}

/*-----------------------------------------------------------------------------
 * Configure Tests
 *---------------------------------------------------------------------------*/

TEST_F( HWPWMGenTest, ConfigStartsLVChannelLowVoltagePWM )
{
    EXPECT_CALL( mock, TIMPWMStart( &htim12, TIM_CHANNEL_1 ) )
        .Times( 1 )
        .WillOnce( Return( HAL_OK ) );

    HW_PWM_GEN_Config( PWM_GEN_CHANNEL_LV, PWM_GEN_VOLTAGE_LOW );
}

TEST_F( HWPWMGenTest, ConfigStartsLVChannelHighVoltagePWM )
{
    EXPECT_CALL( mock, TIMPWMStart( &htim12, TIM_CHANNEL_1 ) )
        .Times( 1 )
        .WillOnce( Return( HAL_OK ) );

    HW_PWM_GEN_Config( PWM_GEN_CHANNEL_LV, PWM_GEN_VOLTAGE_HIGH );
}

TEST_F( HWPWMGenTest, ConfigStartsHVChannelLowVoltagePWM )
{
    EXPECT_CALL( mock, TIMPWMStart( &htim8, TIM_CHANNEL_2 ) )
        .Times( 1 )
        .WillOnce( Return( HAL_OK ) );

    HW_PWM_GEN_Config( PWM_GEN_CHANNEL_HV, PWM_GEN_VOLTAGE_LOW );
}

TEST_F( HWPWMGenTest, ConfigStartsHVChannelHighVoltagePWM )
{
    EXPECT_CALL( mock, TIMPWMStart( &htim8, TIM_CHANNEL_2 ) )
        .Times( 1 )
        .WillOnce( Return( HAL_OK ) );

    HW_PWM_GEN_Config( PWM_GEN_CHANNEL_HV, PWM_GEN_VOLTAGE_HIGH );
}

/*-----------------------------------------------------------------------------
 * Edge Case Tests
 *---------------------------------------------------------------------------*/

TEST_F( HWPWMGenTest, ComputeCCRHandlesSmallARR )
{
    uint16_t ccr = HW_PWM_GEN_compute_ccr( 500, 1 );

    EXPECT_EQ( ccr, 1 );
}

TEST_F( HWPWMGenTest, ComputeARRHandlesPrescalerZero )
{
    uint16_t arr = HW_PWM_GEN_compute_arr( 1000, 1000000, 0 );

    EXPECT_EQ( arr, 999 );
}