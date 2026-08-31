#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <cstdint>

/* Minimal HAL seam: exercise the exact private helper used by both PWM timers
 * without the host hw_timer.c stub bypassing the HAL start calls.
 */
struct TIM_HandleTypeDef
{
};
enum HAL_StatusTypeDef
{
    HAL_OK,
    HAL_ERROR,
    HAL_BUSY,
    HAL_TIMEOUT
};
static HAL_StatusTypeDef HAL_TIM_IC_Start( TIM_HandleTypeDef*, uint32_t );
static HAL_StatusTypeDef HAL_TIM_IC_Stop( TIM_HandleTypeDef*, uint32_t );
#include "hw_timer_capture_start.h"

class MockCaptureHal
{
public:
    MOCK_METHOD( HAL_StatusTypeDef, Start, ( TIM_HandleTypeDef*, uint32_t ) );
    MOCK_METHOD( HAL_StatusTypeDef, Stop, ( TIM_HandleTypeDef*, uint32_t ) );
};

static MockCaptureHal* capture_hal = nullptr;

static HAL_StatusTypeDef HAL_TIM_IC_Start( TIM_HandleTypeDef* handle, uint32_t channel )
{
    return capture_hal->Start( handle, channel );
}

static HAL_StatusTypeDef HAL_TIM_IC_Stop( TIM_HandleTypeDef* handle, uint32_t channel )
{
    return capture_hal->Stop( handle, channel );
}

class CaptureStartTest : public testing::TestWithParam<bool>
{
protected:
    testing::StrictMock<MockCaptureHal> hal;
    TIM_HandleTypeDef                   handle;
    uint32_t                            primary;
    uint32_t                            secondary;

    void SetUp() override
    {
        capture_hal = &hal;
        /* STM32 TIM_CHANNEL_1 = 0, TIM_CHANNEL_2 = 4; TIM5 reverses roles. */
        primary   = GetParam() ? 4U : 0U;
        secondary = GetParam() ? 0U : 4U;
    }
    void TearDown() override
    {
        capture_hal = nullptr;
    }
};

TEST_P( CaptureStartTest, BothInputsStartSuccessfully )
{
    testing::InSequence sequence;
    EXPECT_CALL( hal, Start( &handle, primary ) ).WillOnce( testing::Return( HAL_OK ) );
    EXPECT_CALL( hal, Start( &handle, secondary ) ).WillOnce( testing::Return( HAL_OK ) );
    EXPECT_TRUE( HW_TIMER_Start_Capture_Pair( &handle, primary, secondary ) );
}

TEST_P( CaptureStartTest, FirstFailureDoesNotStartSecondInput )
{
    for ( auto status : { HAL_ERROR, HAL_BUSY, HAL_TIMEOUT } )
    {
        EXPECT_CALL( hal, Start( &handle, primary ) ).WillOnce( testing::Return( status ) );
        EXPECT_FALSE( HW_TIMER_Start_Capture_Pair( &handle, primary, secondary ) );
        testing::Mock::VerifyAndClearExpectations( &hal );
    }
}

TEST_P( CaptureStartTest, SecondFailureStopsFirstInput )
{
    for ( auto status : { HAL_ERROR, HAL_BUSY, HAL_TIMEOUT } )
    {
        testing::InSequence sequence;
        EXPECT_CALL( hal, Start( &handle, primary ) ).WillOnce( testing::Return( HAL_OK ) );
        EXPECT_CALL( hal, Start( &handle, secondary ) ).WillOnce( testing::Return( status ) );
        EXPECT_CALL( hal, Stop( &handle, primary ) ).WillOnce( testing::Return( HAL_OK ) );
        EXPECT_FALSE( HW_TIMER_Start_Capture_Pair( &handle, primary, secondary ) );
        testing::Mock::VerifyAndClearExpectations( &hal );
    }
}

TEST_P( CaptureStartTest, CleanupFailureStillReturnsFailure )
{
    testing::InSequence sequence;
    EXPECT_CALL( hal, Start( &handle, primary ) ).WillOnce( testing::Return( HAL_OK ) );
    EXPECT_CALL( hal, Start( &handle, secondary ) ).WillOnce( testing::Return( HAL_ERROR ) );
    EXPECT_CALL( hal, Stop( &handle, primary ) ).WillOnce( testing::Return( HAL_ERROR ) );
    EXPECT_FALSE( HW_TIMER_Start_Capture_Pair( &handle, primary, secondary ) );
}

INSTANTIATE_TEST_SUITE_P( BothChannelOrders, CaptureStartTest, testing::Bool() );
