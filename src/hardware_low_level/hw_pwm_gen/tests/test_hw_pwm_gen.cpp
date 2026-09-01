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
#include <cstring>

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
    MOCK_METHOD( HAL_StatusTypeDef, TIMPWMNStart, ( TIM_HandleTypeDef * htim, uint32_t channel ),
                 () );
    MOCK_METHOD( HAL_StatusTypeDef, TIMPWMStop, ( TIM_HandleTypeDef * htim, uint32_t channel ),
                 () );
    MOCK_METHOD( HAL_StatusTypeDef, TIMPWMNStop, ( TIM_HandleTypeDef * htim, uint32_t channel ),
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

extern "C" HAL_StatusTypeDef HAL_TIMEx_PWMN_Start( TIM_HandleTypeDef* htim, uint32_t channel )
{
    return g_mock->TIMPWMNStart( htim, channel );
}

extern "C" HAL_StatusTypeDef HAL_TIM_PWM_Stop( TIM_HandleTypeDef* htim, uint32_t channel )
{
    return g_mock->TIMPWMStop( htim, channel );
}

extern "C" HAL_StatusTypeDef HAL_TIMEx_PWMN_Stop( TIM_HandleTypeDef* htim, uint32_t channel )
{
    return g_mock->TIMPWMNStop( htim, channel );
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
        htim8.Instance  = &mock_tim8_regs;
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

TEST_F( HWPWMGenTest, ComputePSCRejectsInvalidInputsWithoutChangingOutput )
{
    uint16_t psc = 1234U;
    EXPECT_FALSE( HW_PWM_GEN_compute_psc( 0U, 1000000U, &psc ) );
    EXPECT_FALSE( HW_PWM_GEN_compute_psc( 1U, 0U, &psc ) );
    EXPECT_FALSE( HW_PWM_GEN_compute_psc( 1001U, 1000U, &psc ) );
    EXPECT_FALSE( HW_PWM_GEN_compute_psc( 1000001U, 90000000U, &psc ) );
    EXPECT_FALSE( HW_PWM_GEN_compute_psc( 1000U, 1000000U, nullptr ) );
    EXPECT_EQ( psc, 1234U );
}

TEST_F( HWPWMGenTest, ComputePSCSelectsSmallestDividerWithRoomForFullDuty )
{
    uint16_t psc = 0U;
    ASSERT_TRUE( HW_PWM_GEN_compute_psc( 1000U, 1000000U, &psc ) );
    EXPECT_EQ( psc, 0U );
    ASSERT_TRUE( HW_PWM_GEN_compute_psc( 1U, 1000000U, &psc ) );
    EXPECT_EQ( psc, 15U );
    ASSERT_TRUE( HW_PWM_GEN_compute_psc( 16U, 1000000U, &psc ) );
    EXPECT_EQ( psc, 0U );
    ASSERT_TRUE( HW_PWM_GEN_compute_psc( 1U, 65535U, &psc ) );
    EXPECT_EQ( psc, 0U );
    ASSERT_TRUE( HW_PWM_GEN_compute_psc( 1U, 65536U, &psc ) );
    EXPECT_EQ( psc, 1U );
    ASSERT_TRUE( HW_PWM_GEN_compute_psc( 1U, UINT32_MAX, &psc ) );
    EXPECT_EQ( psc, UINT16_MAX );  // A valid PSC, no longer an error sentinel.
}

/*-----------------------------------------------------------------------------
 * Compute ARR Tests
 *---------------------------------------------------------------------------*/

TEST_F( HWPWMGenTest, ComputeARRHandlesValidPeriodsAndRegisterBoundaries )
{
    uint16_t arr = 1234U;
    ASSERT_TRUE( HW_PWM_GEN_compute_arr( 1000U, 1000000U, 1U, &arr ) );
    EXPECT_EQ( arr, 499U );
    ASSERT_TRUE( HW_PWM_GEN_compute_arr( 100U, 1000000U, 9U, &arr ) );
    EXPECT_EQ( arr, 999U );
    ASSERT_TRUE( HW_PWM_GEN_compute_arr( 1000000U, 1000000U, 0U, &arr ) );
    EXPECT_EQ( arr, 0U );
    ASSERT_TRUE( HW_PWM_GEN_compute_arr( 1U, 65536U, 0U, &arr ) );
    EXPECT_EQ( arr, UINT16_MAX );
    ASSERT_TRUE( HW_PWM_GEN_compute_arr( 1U, UINT32_MAX, UINT16_MAX, &arr ) );
    EXPECT_EQ( arr, 65534U );
}

TEST_F( HWPWMGenTest, ComputeARRRejectsInvalidOrUnrepresentablePeriods )
{
    uint16_t arr = 1234U;
    EXPECT_FALSE( HW_PWM_GEN_compute_arr( 0U, 1000000U, 0U, &arr ) );
    EXPECT_FALSE( HW_PWM_GEN_compute_arr( 1U, 0U, 0U, &arr ) );
    EXPECT_FALSE( HW_PWM_GEN_compute_arr( 1000001U, 90000000U, 0U, &arr ) );
    EXPECT_FALSE( HW_PWM_GEN_compute_arr( 1001U, 1000U, 0U, &arr ) );
    EXPECT_FALSE( HW_PWM_GEN_compute_arr( 1000U, 1000U, 1U, &arr ) );
    EXPECT_FALSE( HW_PWM_GEN_compute_arr( 1U, 65537U, 0U, &arr ) );
    EXPECT_FALSE( HW_PWM_GEN_compute_arr( 1000U, 1000000U, 0U, nullptr ) );
    EXPECT_EQ( arr, 1234U );
}

TEST_F( HWPWMGenTest, ComputeARRRejectsOversizedDividerWithoutIntegerWrap )
{
    uint16_t arr = 1234U;
    // The first product used to wrap to zero; the second to a nonzero value.
    EXPECT_FALSE( HW_PWM_GEN_compute_arr( 65536U, UINT32_MAX, UINT16_MAX, &arr ) );
    EXPECT_FALSE( HW_PWM_GEN_compute_arr( 65537U, UINT32_MAX, UINT16_MAX, &arr ) );
    EXPECT_EQ( arr, 1234U );
}

/*-----------------------------------------------------------------------------
 * Compute CCR Tests
 *---------------------------------------------------------------------------*/

TEST_F( HWPWMGenTest, ComputeCCRHandlesZeroFractionalAndFullDuty )
{
    uint16_t ccr = 1234U;
    ASSERT_TRUE( HW_PWM_GEN_compute_ccr( 0U, 999U, &ccr ) );
    EXPECT_EQ( ccr, 0U );
    ASSERT_TRUE( HW_PWM_GEN_compute_ccr( 250U, 999U, &ccr ) );
    EXPECT_EQ( ccr, 250U );
    ASSERT_TRUE( HW_PWM_GEN_compute_ccr( 500U, 999U, &ccr ) );
    EXPECT_EQ( ccr, 500U );
    ASSERT_TRUE( HW_PWM_GEN_compute_ccr( 1000U, 999U, &ccr ) );
    EXPECT_EQ( ccr, 1000U );
}

TEST_F( HWPWMGenTest, ComputeCCRRejectsInvalidDutyAndFullDutyOverflow )
{
    uint16_t ccr = 1234U;
    EXPECT_FALSE( HW_PWM_GEN_compute_ccr( 1001U, 999U, &ccr ) );
    EXPECT_FALSE( HW_PWM_GEN_compute_ccr( UINT16_MAX, 999U, &ccr ) );
    EXPECT_FALSE( HW_PWM_GEN_compute_ccr( 1000U, UINT16_MAX, &ccr ) );
    EXPECT_FALSE( HW_PWM_GEN_compute_ccr( 500U, 999U, nullptr ) );
    EXPECT_EQ( ccr, 1234U );
}

TEST_F( HWPWMGenTest, ComputeCCRSupportsRepresentableBoundaryValues )
{
    uint16_t ccr = 0U;
    ASSERT_TRUE( HW_PWM_GEN_compute_ccr( 1000U, 65534U, &ccr ) );
    EXPECT_EQ( ccr, UINT16_MAX );
    ASSERT_TRUE( HW_PWM_GEN_compute_ccr( 0U, UINT16_MAX, &ccr ) );
    EXPECT_EQ( ccr, 0U );
    ASSERT_TRUE( HW_PWM_GEN_compute_ccr( 999U, UINT16_MAX, &ccr ) );
    EXPECT_EQ( ccr, 65470U );
    ASSERT_TRUE( HW_PWM_GEN_compute_ccr( 1000U, 0U, &ccr ) );
    EXPECT_EQ( ccr, 1U );
}

TEST_F( HWPWMGenTest, PreparedWaveformSupportsFullDutyAtPeriodBoundary )
{
    uint16_t psc = 0U;
    uint16_t arr = 0U;
    uint16_t ccr = 0U;
    for ( uint32_t clock : { 65535U, 65536U, 1000000U, UINT32_MAX } )
    {
        ASSERT_TRUE( HW_PWM_GEN_compute_psc( 1U, clock, &psc ) );
        ASSERT_TRUE( HW_PWM_GEN_compute_arr( 1U, clock, psc, &arr ) );
        ASSERT_TRUE( HW_PWM_GEN_compute_ccr( 1000U, arr, &ccr ) );
        EXPECT_EQ( static_cast<uint32_t>( ccr ), static_cast<uint32_t>( arr ) + 1U );
        EXPECT_NE( ccr, 0U );
    }
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

TEST_F( HWPWMGenTest, SetPWM2DirectUpdatesTIM8Registers )
{
    HW_PWM_GEN_Set_PWM2_Direct( 2000, 500, 8 );

    EXPECT_EQ( mock_tim8_regs.CCR2, 500 );
    EXPECT_EQ( mock_tim8_regs.ARR, 2000 );
    EXPECT_EQ( mock_tim8_regs.PSC, 8 );
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

    EXPECT_EQ( mock_tim8_regs.CCR1, 0 );
    EXPECT_EQ( mock_tim8_regs.CCR2, 222 );
    EXPECT_EQ( mock_tim8_regs.CCR3, 0 );
    EXPECT_EQ( mock_tim8_regs.CCR4, 0 );
}

/*-----------------------------------------------------------------------------
 * Configure Tests
 *---------------------------------------------------------------------------*/

TEST_F( HWPWMGenTest, StartChannelReturnsFalseBeforeConfiguration )
{
    EXPECT_CALL( mock, TIMPWMNStart( _, _ ) ).Times( 0 );

    EXPECT_FALSE( HW_PWM_GEN_Start_Channel( HW_PWM_GEN_CHANNEL_HV ) );
}

TEST_F( HWPWMGenTest, ConfigureChannelLeavesLVChannelStopped )
{
    EXPECT_CALL( mock, TIMPWMStart( _, _ ) ).Times( 0 );
    EXPECT_CALL( mock, TIMPWMNStart( _, _ ) ).Times( 0 );

    EXPECT_TRUE( HW_PWM_GEN_Configure_Channel( HW_PWM_GEN_CHANNEL_LV ) );
}

TEST_F( HWPWMGenTest, ConfigureChannelRejectsInvalidChannel )
{
    const auto invalid_channel = static_cast<HwPwmGenChannel_T>( HW_PWM_GEN_CHANNEL_COUNT );

    EXPECT_FALSE( HW_PWM_GEN_Configure_Channel( invalid_channel ) );
}

TEST_F( HWPWMGenTest, StartChannelStartsLVNormalOutput )
{
    ASSERT_TRUE( HW_PWM_GEN_Configure_Channel( HW_PWM_GEN_CHANNEL_LV ) );
    EXPECT_CALL( mock, TIMPWMStart( &htim12, TIM_CHANNEL_2 ) ).WillOnce( Return( HAL_OK ) );

    EXPECT_TRUE( HW_PWM_GEN_Start_Channel( HW_PWM_GEN_CHANNEL_LV ) );

    EXPECT_CALL( mock, TIMPWMStop( &htim12, TIM_CHANNEL_2 ) ).WillOnce( Return( HAL_OK ) );
    EXPECT_TRUE( HW_PWM_GEN_Stop_Channel( HW_PWM_GEN_CHANNEL_LV ) );
}

TEST_F( HWPWMGenTest, StartChannelStartsHVComplementaryOutput )
{
    ASSERT_TRUE( HW_PWM_GEN_Configure_Channel( HW_PWM_GEN_CHANNEL_HV ) );
    EXPECT_CALL( mock, TIMPWMNStart( &htim8, TIM_CHANNEL_2 ) ).WillOnce( Return( HAL_OK ) );

    EXPECT_TRUE( HW_PWM_GEN_Start_Channel( HW_PWM_GEN_CHANNEL_HV ) );

    EXPECT_CALL( mock, TIMPWMNStop( &htim8, TIM_CHANNEL_2 ) ).WillOnce( Return( HAL_OK ) );
    EXPECT_TRUE( HW_PWM_GEN_Stop_Channel( HW_PWM_GEN_CHANNEL_HV ) );
}

TEST_F( HWPWMGenTest, StartChannelRejectsInvalidChannel )
{
    const auto invalid_channel = static_cast<HwPwmGenChannel_T>( HW_PWM_GEN_CHANNEL_COUNT );

    EXPECT_CALL( mock, TIMPWMStart( _, _ ) ).Times( 0 );
    EXPECT_FALSE( HW_PWM_GEN_Start_Channel( invalid_channel ) );
}

TEST_F( HWPWMGenTest, StartChannelPreservesStoppedStateWhenHALStartFails )
{
    ASSERT_TRUE( HW_PWM_GEN_Configure_Channel( HW_PWM_GEN_CHANNEL_LV ) );
    EXPECT_CALL( mock, TIMPWMStart( &htim12, TIM_CHANNEL_2 ) ).WillOnce( Return( HAL_ERROR ) );

    EXPECT_FALSE( HW_PWM_GEN_Start_Channel( HW_PWM_GEN_CHANNEL_LV ) );

    EXPECT_CALL( mock, TIMPWMStart( &htim12, TIM_CHANNEL_2 ) ).WillOnce( Return( HAL_OK ) );
    EXPECT_TRUE( HW_PWM_GEN_Start_Channel( HW_PWM_GEN_CHANNEL_LV ) );
    EXPECT_CALL( mock, TIMPWMStop( &htim12, TIM_CHANNEL_2 ) ).WillOnce( Return( HAL_OK ) );
    EXPECT_TRUE( HW_PWM_GEN_Stop_Channel( HW_PWM_GEN_CHANNEL_LV ) );
}

TEST_F( HWPWMGenTest, ConfigureChannelReturnsFalseWhileStarted )
{
    ASSERT_TRUE( HW_PWM_GEN_Configure_Channel( HW_PWM_GEN_CHANNEL_HV ) );
    EXPECT_CALL( mock, TIMPWMNStart( &htim8, TIM_CHANNEL_2 ) ).WillOnce( Return( HAL_OK ) );
    ASSERT_TRUE( HW_PWM_GEN_Start_Channel( HW_PWM_GEN_CHANNEL_HV ) );

    EXPECT_FALSE( HW_PWM_GEN_Configure_Channel( HW_PWM_GEN_CHANNEL_HV ) );

    EXPECT_CALL( mock, TIMPWMNStop( &htim8, TIM_CHANNEL_2 ) ).WillOnce( Return( HAL_OK ) );
    EXPECT_TRUE( HW_PWM_GEN_Stop_Channel( HW_PWM_GEN_CHANNEL_HV ) );
}

TEST_F( HWPWMGenTest, StopChannelRetainsConfigurationForRestart )
{
    ASSERT_TRUE( HW_PWM_GEN_Configure_Channel( HW_PWM_GEN_CHANNEL_LV ) );
    EXPECT_CALL( mock, TIMPWMStart( &htim12, TIM_CHANNEL_2 ) ).WillOnce( Return( HAL_OK ) );
    ASSERT_TRUE( HW_PWM_GEN_Start_Channel( HW_PWM_GEN_CHANNEL_LV ) );
    EXPECT_CALL( mock, TIMPWMStop( &htim12, TIM_CHANNEL_2 ) ).WillOnce( Return( HAL_OK ) );
    ASSERT_TRUE( HW_PWM_GEN_Stop_Channel( HW_PWM_GEN_CHANNEL_LV ) );

    EXPECT_CALL( mock, TIMPWMStart( &htim12, TIM_CHANNEL_2 ) ).WillOnce( Return( HAL_OK ) );
    EXPECT_TRUE( HW_PWM_GEN_Start_Channel( HW_PWM_GEN_CHANNEL_LV ) );
    EXPECT_CALL( mock, TIMPWMStop( &htim12, TIM_CHANNEL_2 ) ).WillOnce( Return( HAL_OK ) );
    EXPECT_TRUE( HW_PWM_GEN_Stop_Channel( HW_PWM_GEN_CHANNEL_LV ) );
}

TEST_F( HWPWMGenTest, StopChannelPreservesStartedStateWhenHALStopFails )
{
    ASSERT_TRUE( HW_PWM_GEN_Configure_Channel( HW_PWM_GEN_CHANNEL_HV ) );
    EXPECT_CALL( mock, TIMPWMNStart( &htim8, TIM_CHANNEL_2 ) ).WillOnce( Return( HAL_OK ) );
    ASSERT_TRUE( HW_PWM_GEN_Start_Channel( HW_PWM_GEN_CHANNEL_HV ) );

    EXPECT_CALL( mock, TIMPWMNStop( &htim8, TIM_CHANNEL_2 ) ).WillOnce( Return( HAL_ERROR ) );
    EXPECT_FALSE( HW_PWM_GEN_Stop_Channel( HW_PWM_GEN_CHANNEL_HV ) );

    EXPECT_CALL( mock, TIMPWMNStop( &htim8, TIM_CHANNEL_2 ) ).WillOnce( Return( HAL_OK ) );
    EXPECT_TRUE( HW_PWM_GEN_Stop_Channel( HW_PWM_GEN_CHANNEL_HV ) );
}

TEST_F( HWPWMGenTest, StopChannelReturnsFalseWhenNotStarted )
{
    ASSERT_TRUE( HW_PWM_GEN_Configure_Channel( HW_PWM_GEN_CHANNEL_LV ) );
    EXPECT_CALL( mock, TIMPWMStop( _, _ ) ).Times( 0 );

    EXPECT_FALSE( HW_PWM_GEN_Stop_Channel( HW_PWM_GEN_CHANNEL_LV ) );
}

/*-----------------------------------------------------------------------------
 * Edge Case Tests
 *---------------------------------------------------------------------------*/

TEST_F( HWPWMGenTest, ComputeCCRHandlesSmallARR )
{
    uint16_t ccr = 0U;
    ASSERT_TRUE( HW_PWM_GEN_compute_ccr( 500U, 1U, &ccr ) );

    EXPECT_EQ( ccr, 1 );
}

TEST_F( HWPWMGenTest, ComputeARRHandlesPrescalerZero )
{
    uint16_t arr = 0U;
    ASSERT_TRUE( HW_PWM_GEN_compute_arr( 1000U, 1000000U, 0U, &arr ) );

    EXPECT_EQ( arr, 999 );
}
