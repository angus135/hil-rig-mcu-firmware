/******************************************************************************
 *  File:       test_execution_manager.cpp
 *  Author:     Angus Corr
 *  Created:    06-Dec-2025
 *
 *  Description:
 *      Unit tests for the Execution Manager lifecycle and ISR scaffold.
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
#include "execution_manager.h"
#include "execution_manager_isr.h"
#include "hw_timer.h"
#include <stdint.h>
}

using ::testing::StrictMock;

/**-----------------------------------------------------------------------------
 *  Test Doubles / Mocks
 *------------------------------------------------------------------------------
 */

class MockExecutionManagerTimer
{
public:
    MOCK_METHOD( void, Configure, ( Timer_T timer, uint32_t psc, uint32_t arr ) );
    MOCK_METHOD( void, Start, ( Timer_T timer ) );
    MOCK_METHOD( void, Stop, ( Timer_T timer ) );
};

static MockExecutionManagerTimer* g_mock_timer = nullptr;

extern "C"
{
void HW_TIMER_Configure_Timer( Timer_T timer, uint32_t psc, uint32_t arr )
{
    g_mock_timer->Configure( timer, psc, arr );
}

void HW_TIMER_Start_Timer( Timer_T timer )
{
    g_mock_timer->Start( timer );
}

void HW_TIMER_Stop_Timer( Timer_T timer )
{
    g_mock_timer->Stop( timer );
}
}

/**-----------------------------------------------------------------------------
 *  Test Fixture
 *------------------------------------------------------------------------------
 */

class ExecutionManagerTest : public ::testing::Test
{
protected:
    StrictMock<MockExecutionManagerTimer> mock_timer;

    void SetUp( void ) override
    {
        g_mock_timer = &mock_timer;
    }

    void TearDown( void ) override
    {
        g_mock_timer = nullptr;
    }
};

/**-----------------------------------------------------------------------------
 *  Test Cases
 *------------------------------------------------------------------------------
 */

TEST_F( ExecutionManagerTest, StartRejectsNullConfiguration )
{
    EXPECT_FALSE( EXECUTION_MANAGER_Start( nullptr ) );
}

TEST_F( ExecutionManagerTest, StartRejectsZeroTicks )
{
    const ExecutionManagerConfig_T config = { FREQUENCY_1KHZ, 0U };

    EXPECT_FALSE( EXECUTION_MANAGER_Start( &config ) );
}

TEST_F( ExecutionManagerTest, StartRejectsInvalidFrequency )
{
    const ExecutionManagerConfig_T config = { static_cast<FrequencyMode_T>( 3 ), 10U };

    EXPECT_FALSE( EXECUTION_MANAGER_Start( &config ) );
}

TEST_F( ExecutionManagerTest, IsrEntryPointIsSafeWhenExecutionIsStopped )
{
    ExecutionManagerStatus_T status = {};

    EXECUTION_MANAGER_Process_From_ISR();
    EXECUTION_MANAGER_Get_Status( &status );

    EXPECT_EQ( status.state, EXECUTION_MANAGER_STATE_STOPPED );
    EXPECT_EQ( status.failure, EXECUTION_MANAGER_FAILURE_NONE );
    EXPECT_EQ( status.ticks_completed, 0U );
}

TEST_F( ExecutionManagerTest, StartConfiguresOneHundredHertzTimer )
{
    const ExecutionManagerConfig_T config = { FREQUENCY_100HZ, 10U };

    EXPECT_CALL( mock_timer, Configure( EXECUTION_MANAGER_TIMER, 14U, 59999U ) );
    EXPECT_CALL( mock_timer, Start( EXECUTION_MANAGER_TIMER ) );

    EXPECT_TRUE( EXECUTION_MANAGER_Start( &config ) );
}

TEST_F( ExecutionManagerTest, StartConfiguresOneKilohertzTimer )
{
    const ExecutionManagerConfig_T config = { FREQUENCY_1KHZ, 10U };

    EXPECT_CALL( mock_timer, Configure( EXECUTION_MANAGER_TIMER, 1U, 44999U ) );
    EXPECT_CALL( mock_timer, Start( EXECUTION_MANAGER_TIMER ) );

    EXPECT_TRUE( EXECUTION_MANAGER_Start( &config ) );
}

TEST_F( ExecutionManagerTest, StartConfiguresTenKilohertzTimer )
{
    const ExecutionManagerConfig_T config = { FREQUENCY_10KHZ, 10U };

    EXPECT_CALL( mock_timer, Configure( EXECUTION_MANAGER_TIMER, 0U, 8999U ) );
    EXPECT_CALL( mock_timer, Start( EXECUTION_MANAGER_TIMER ) );

    EXPECT_TRUE( EXECUTION_MANAGER_Start( &config ) );
}

TEST_F( ExecutionManagerTest, AbortStopsTimerAndUpdatesStatus )
{
    const ExecutionManagerConfig_T config = { FREQUENCY_1KHZ, 10U };
    ExecutionManagerStatus_T       status = {};

    EXPECT_CALL( mock_timer, Configure( EXECUTION_MANAGER_TIMER, 1U, 44999U ) );
    EXPECT_CALL( mock_timer, Start( EXECUTION_MANAGER_TIMER ) );
    ASSERT_TRUE( EXECUTION_MANAGER_Start( &config ) );

    EXPECT_CALL( mock_timer, Stop( EXECUTION_MANAGER_TIMER ) );
    EXECUTION_MANAGER_Abort();
    EXECUTION_MANAGER_Get_Status( &status );

    EXPECT_EQ( status.state, EXECUTION_MANAGER_STATE_ABORTED );
    EXPECT_EQ( status.failure, EXECUTION_MANAGER_FAILURE_NONE );
    EXPECT_EQ( status.ticks_completed, 0U );
}

TEST_F( ExecutionManagerTest, StatusIsCopiedIntoCallerOwnedStorage )
{
    const ExecutionManagerConfig_T config = { FREQUENCY_1KHZ, 10U };
    ExecutionManagerStatus_T       status = {};

    EXPECT_CALL( mock_timer, Configure( EXECUTION_MANAGER_TIMER, 1U, 44999U ) );
    EXPECT_CALL( mock_timer, Start( EXECUTION_MANAGER_TIMER ) );
    ASSERT_TRUE( EXECUTION_MANAGER_Start( &config ) );

    EXECUTION_MANAGER_Get_Status( &status );
    EXPECT_EQ( status.state, EXECUTION_MANAGER_STATE_RUNNING );
    EXPECT_EQ( status.failure, EXECUTION_MANAGER_FAILURE_NONE );
    EXPECT_EQ( status.ticks_completed, 0U );

    status.state           = EXECUTION_MANAGER_STATE_FAILED;
    status.failure         = EXECUTION_MANAGER_FAILURE_INTERNAL;
    status.ticks_completed = 99U;
    EXECUTION_MANAGER_Get_Status( &status );

    EXPECT_EQ( status.state, EXECUTION_MANAGER_STATE_RUNNING );
    EXPECT_EQ( status.failure, EXECUTION_MANAGER_FAILURE_NONE );
    EXPECT_EQ( status.ticks_completed, 0U );
}

TEST_F( ExecutionManagerTest, IsrRunsCurrentStubPathWithoutDriverCalls )
{
    const ExecutionManagerConfig_T config = { FREQUENCY_1KHZ, 2U };
    ExecutionManagerStatus_T       status = {};

    EXPECT_CALL( mock_timer, Configure( EXECUTION_MANAGER_TIMER, 1U, 44999U ) );
    EXPECT_CALL( mock_timer, Start( EXECUTION_MANAGER_TIMER ) );
    ASSERT_TRUE( EXECUTION_MANAGER_Start( &config ) );

    EXECUTION_MANAGER_Process_From_ISR();
    EXECUTION_MANAGER_Get_Status( &status );

    EXPECT_EQ( status.state, EXECUTION_MANAGER_STATE_RUNNING );
    EXPECT_EQ( status.failure, EXECUTION_MANAGER_FAILURE_NONE );
    EXPECT_EQ( status.ticks_completed, 1U );
}

TEST_F( ExecutionManagerTest, IsrCompletesAfterConfiguredTickCount )
{
    const ExecutionManagerConfig_T config = { FREQUENCY_1KHZ, 1U };
    ExecutionManagerStatus_T       status = {};

    EXPECT_CALL( mock_timer, Configure( EXECUTION_MANAGER_TIMER, 1U, 44999U ) );
    EXPECT_CALL( mock_timer, Start( EXECUTION_MANAGER_TIMER ) );
    ASSERT_TRUE( EXECUTION_MANAGER_Start( &config ) );

    EXPECT_CALL( mock_timer, Stop( EXECUTION_MANAGER_TIMER ) );
    EXECUTION_MANAGER_Process_From_ISR();
    EXECUTION_MANAGER_Get_Status( &status );

    EXPECT_EQ( status.state, EXECUTION_MANAGER_STATE_COMPLETE );
    EXPECT_EQ( status.failure, EXECUTION_MANAGER_FAILURE_NONE );
    EXPECT_EQ( status.ticks_completed, 1U );
}
