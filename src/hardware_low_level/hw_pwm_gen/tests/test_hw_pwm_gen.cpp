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
#include "hw_pwm_gen.c"

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
 *  Fake Hardware Registers
 *------------------------------------------------------------------------------
 */

TIM_TypeDef mock_tim12_regs{};
TIM_TypeDef mock_tim13_regs{};

TIM_HandleTypeDef htim12{};
TIM_HandleTypeDef htim13{};

/**-----------------------------------------------------------------------------
 *  Test Doubles / Mocks
 *------------------------------------------------------------------------------
 */

class MockHWPWM
{
public:
    MOCK_METHOD( HAL_StatusTypeDef, TIMPWMStart,
                 ( TIM_HandleTypeDef * htim, uint32_t channel ), () );
};

static MockHWPWM* g_mock = nullptr;

/**-----------------------------------------------------------------------------
 *  HAL Mock Implementations
 *------------------------------------------------------------------------------
 */

extern "C" HAL_StatusTypeDef HAL_TIM_PWM_Start( TIM_HandleTypeDef* htim,
                                                uint32_t channel )
{
    return g_mock->TIMPWMStart( htim, channel );
}

/**-----------------------------------------------------------------------------
 *  LL Mock Implementations
 *------------------------------------------------------------------------------
 */

extern "C" void LL_TIM_OC_SetCompareCH1( TIM_TypeDef* TIMx,
                                         uint32_t compare_value )
{
    TIMx->CCR1 = compare_value;
}

extern "C" void LL_TIM_OC_SetCompareCH2( TIM_TypeDef* TIMx,
                                         uint32_t compare_value )
{
    TIMx->CCR2 = compare_value;
}

extern "C" void LL_TIM_OC_SetCompareCH3( TIM_TypeDef* TIMx,
                                         uint32_t compare_value )
{
    TIMx->CCR3 = compare_value;
}

extern "C" void LL_TIM_OC_SetCompareCH4( TIM_TypeDef* TIMx,
                                         uint32_t compare_value )
{
    TIMx->CCR4 = compare_value;
}

extern "C" void LL_TIM_SetAutoReload( TIM_TypeDef* TIMx,
                                      uint32_t auto_reload )
{
    TIMx->ARR = auto_reload;
}

extern "C" void LL_TIM_SetPrescaler( TIM_TypeDef* TIMx,
                                     uint32_t prescaler )
{
    TIMx->PSC = prescaler;
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
        memset( &mock_tim13_regs, 0, sizeof( mock_tim13_regs ) );

        htim12.Instance = &mock_tim12_regs;
        htim13.Instance = &mock_tim13_regs;
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

TEST_F( HWPWMGenTest, ComputePSCReturnsZeroForInvalidFrequency )
{
    uint16_t psc = HW_PWM_GEN_compute_psc( 100000000, 1000000 );

    EXPECT_EQ( psc, 0 );
}

TEST_F( HWPWMGenTest, ComputePSCReturnsMaxForZeroFrequency )
{
    uint16_t psc = HW_PWM_GEN_compute_psc( 0, 1000000 );

    EXPECT_EQ( psc, 0xFFFF );
}

TEST_F( HWPWMGenTest, ComputePSCComputesExpectedValue )
{
    uint16_t psc = HW_PWM_GEN_compute_psc( 1000, 1000000 );

    EXPECT_EQ( psc, 1 );
}

TEST_F( HWPWMGenTest, ComputePSCIncreasesPrescalerWhenARRTooLarge )
{
    uint16_t psc = HW_PWM_GEN_compute_psc( 1, 1000000 );

    EXPECT_GT( psc, 1 );
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

TEST_F( HWPWMGenTest, SetPWMDirectUpdatesChannel1Registers )
{
    HW_PWM_GEN_set_pwm_direct( 1, 1000, 250, 4, &mock_tim12_regs );

    EXPECT_EQ( mock_tim12_regs.CCR1, 250 );
    EXPECT_EQ( mock_tim12_regs.ARR, 1000 );
    EXPECT_EQ( mock_tim12_regs.PSC, 4 );

    EXPECT_TRUE( mock_tim12_regs.EGR & TIM_EGR_UG );
}

TEST_F( HWPWMGenTest, SetPWMDirectUpdatesChannel2Registers )
{
    HW_PWM_GEN_set_pwm_direct( 2, 2000, 500, 8, &mock_tim12_regs );

    EXPECT_EQ( mock_tim12_regs.CCR2, 500 );
    EXPECT_EQ( mock_tim12_regs.ARR, 2000 );
    EXPECT_EQ( mock_tim12_regs.PSC, 8 );
}

TEST_F( HWPWMGenTest, SetPWMDirectUpdatesChannel3Registers )
{
    HW_PWM_GEN_set_pwm_direct( 3, 3000, 750, 16, &mock_tim12_regs );

    EXPECT_EQ( mock_tim12_regs.CCR3, 750 );
    EXPECT_EQ( mock_tim12_regs.ARR, 3000 );
    EXPECT_EQ( mock_tim12_regs.PSC, 16 );
}

TEST_F( HWPWMGenTest, SetPWMDirectUpdatesChannel4Registers )
{
    HW_PWM_GEN_set_pwm_direct( 4, 4000, 900, 32, &mock_tim12_regs );

    EXPECT_EQ( mock_tim12_regs.CCR4, 900 );
    EXPECT_EQ( mock_tim12_regs.ARR, 4000 );
    EXPECT_EQ( mock_tim12_regs.PSC, 32 );
}

/*-----------------------------------------------------------------------------
 * Inline Wrapper Tests
 *---------------------------------------------------------------------------*/

TEST_F( HWPWMGenTest, SetPWM1DirectUpdatesTIM12Channel2 )
{
    HW_PWM_GEN_set_pwm1_direct( 1000, 400, 5 );

    EXPECT_EQ( mock_tim12_regs.CCR2, 400 );
    EXPECT_EQ( mock_tim12_regs.ARR, 1000 );
    EXPECT_EQ( mock_tim12_regs.PSC, 5 );
}

TEST_F( HWPWMGenTest, SetPWM2DirectUpdatesTIM13Channel1 )
{
    HW_PWM_GEN_set_pwm2_direct( 2000, 800, 10 );

    EXPECT_EQ( mock_tim13_regs.CCR1, 800 );
    EXPECT_EQ( mock_tim13_regs.ARR, 2000 );
    EXPECT_EQ( mock_tim13_regs.PSC, 10 );
}

/*-----------------------------------------------------------------------------
 * Configure Tests
 *---------------------------------------------------------------------------*/

TEST_F( HWPWMGenTest, ConfigStartsPWMForChannel1Low )
{
    EXPECT_CALL( mock,
                 TIMPWMStart( &htim12, TIM_CHANNEL_2 ) )
        .Times( 1 )
        .WillOnce( Return( HAL_OK ) );

    HW_PWM_GEN_config( 1, 0 );
}

TEST_F( HWPWMGenTest, ConfigStartsPWMForChannel1High )
{
    EXPECT_CALL( mock,
                 TIMPWMStart( &htim12, TIM_CHANNEL_2 ) )
        .Times( 1 )
        .WillOnce( Return( HAL_OK ) );

    HW_PWM_GEN_config( 1, 1 );
}

TEST_F( HWPWMGenTest, ConfigStartsPWMForChannel2 )
{
    EXPECT_CALL( mock,
                 TIMPWMStart( &htim12, TIM_CHANNEL_2 ) )
        .Times( 1 )
        .WillOnce( Return( HAL_OK ) );

    HW_PWM_GEN_config( 2, 0 );
}

TEST_F( HWPWMGenTest, ConfigStartsPWMForChannel3 )
{
    EXPECT_CALL( mock,
                 TIMPWMStart( &htim12, TIM_CHANNEL_2 ) )
        .Times( 1 )
        .WillOnce( Return( HAL_OK ) );

    HW_PWM_GEN_config( 3, 1 );
}

TEST_F( HWPWMGenTest, ConfigDoesNothingForInvalidChannel )
{
    EXPECT_CALL( mock,
                 TIMPWMStart( _, _ ) )
        .Times( 0 );

    HW_PWM_GEN_config( 99, 1 );
}