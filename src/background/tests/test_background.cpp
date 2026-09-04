/******************************************************************************
 *  File:       test_background.cpp
 *  Author:     Angus Corr
 *  Created:    06-Dec-2025
 *
 *  Description:
 *      Unit tests for background scheduling.
 ******************************************************************************/

#include <gmock/gmock.h>
#include <gtest/gtest.h>

extern "C"
{
#include "background.h"
#include "hw_gpio.h"
#include "logic_expander.h"
#include "run_state_manager.h"

#include "../background.c" /* Module under test */  // NOLINT
}

class MockBackgroundDependencies
{
public:
    MOCK_METHOD( LogicExpanderStatus_T, ProcessLogicExpander, () );
    MOCK_METHOD( void, ToggleOutput, ( GPIOOutput_T output ) );
    MOCK_METHOD( void, SetOutput, ( GPIOOutput_T output ) );
    MOCK_METHOD( void, ResetOutput, ( GPIOOutput_T output ) );
    MOCK_METHOD( void, GetRunStateStatus, ( RunStateManagerStatus_T * status ) );
    MOCK_METHOD( TickType_t, GetTickCount, () );
    MOCK_METHOD( void, DelayUntil, ( TickType_t * previous_wake_time, TickType_t increment ) );
};

static MockBackgroundDependencies* g_mock_dependencies = nullptr;

extern "C"
{
LogicExpanderStatus_T LOGIC_EXPANDER_Process( void )
{
    return g_mock_dependencies->ProcessLogicExpander();
}

void HW_GPIO_Toggle_Output( GPIOOutput_T output )
{
    g_mock_dependencies->ToggleOutput( output );
}

void HW_GPIO_Set_Single_Pin( GPIOOutput_T output )
{
    g_mock_dependencies->SetOutput( output );
}

void HW_GPIO_Reset_Single_Pin( GPIOOutput_T output )
{
    g_mock_dependencies->ResetOutput( output );
}

void RUN_STATE_MANAGER_GetStatus( RunStateManagerStatus_T* status )
{
    g_mock_dependencies->GetRunStateStatus( status );
}

TickType_t xTaskGetTickCount( void )
{
    return g_mock_dependencies->GetTickCount();
}

void vTaskDelayUntil( TickType_t* previous_wake_time, const TickType_t increment )
{
    g_mock_dependencies->DelayUntil( previous_wake_time, increment );
}
}

class BackgroundTaskExit
{
};

class BackgroundTest : public ::testing::Test
{
protected:
    testing::StrictMock<MockBackgroundDependencies> mock_dependencies;

    void SetUp( void ) override
    {
        g_mock_dependencies               = &mock_dependencies;
        background_led_cycles_remaining   = 0U;
        background_state_leds_initialised = false;
        background_displayed_state        = RUN_STATE_IDLE;
        background_fault_active           = false;
    }

    void TearDown( void ) override
    {
        g_mock_dependencies = nullptr;
    }
};

TEST_F( BackgroundTest, ServicesExpanderEveryCycle )
{
    EXPECT_CALL( mock_dependencies, ProcessLogicExpander() )
        .Times( 101 )
        .WillRepeatedly( testing::Return( LOGIC_EXPANDER_STATUS_NOT_READY ) );
    EXPECT_CALL( mock_dependencies, ToggleOutput( testing::_ ) ).Times( testing::AnyNumber() );
    EXPECT_CALL( mock_dependencies, SetOutput( testing::_ ) ).Times( testing::AnyNumber() );
    EXPECT_CALL( mock_dependencies, ResetOutput( testing::_ ) ).Times( testing::AnyNumber() );
    EXPECT_CALL( mock_dependencies, GetRunStateStatus( testing::_ ) )
        .Times( 101 )
        .WillRepeatedly(
            testing::Invoke( []( RunStateManagerStatus_T* status ) { *status = {}; } ) );

    for ( uint16_t cycle = 0U; cycle < 101U; ++cycle )
    {
        BACKGROUND_Process();
    }
}

TEST_F( BackgroundTest, TaskRunsPeriodicProcessing )
{
    testing::InSequence sequence;
    EXPECT_CALL( mock_dependencies, GetTickCount() ).WillOnce( testing::Return( 17U ) );
    EXPECT_CALL( mock_dependencies, ProcessLogicExpander() )
        .WillOnce( testing::Return( LOGIC_EXPANDER_STATUS_NOT_READY ) );
    EXPECT_CALL( mock_dependencies, ToggleOutput( testing::_ ) ).Times( testing::AnyNumber() );
    EXPECT_CALL( mock_dependencies, SetOutput( testing::_ ) ).Times( testing::AnyNumber() );
    EXPECT_CALL( mock_dependencies, ResetOutput( testing::_ ) ).Times( testing::AnyNumber() );
    EXPECT_CALL( mock_dependencies, GetRunStateStatus( testing::_ ) )
        .WillOnce( testing::Invoke( []( RunStateManagerStatus_T* status ) { *status = {}; } ) );
    EXPECT_CALL( mock_dependencies, DelayUntil( testing::_, BACKGROUND_TASK_PERIOD_MS ) )
        .WillOnce(
            testing::Invoke( []( TickType_t*, TickType_t ) { throw BackgroundTaskExit{}; } ) );

    EXPECT_THROW( BACKGROUND_Task( nullptr ), BackgroundTaskExit );
}

TEST_F( BackgroundTest, DisplaysIdleAsOneOnBlueStateLedsAndUsesBlueOneForHeartbeat )
{
    testing::InSequence sequence;
    EXPECT_CALL( mock_dependencies, GetRunStateStatus( testing::_ ) )
        .WillOnce( testing::Invoke( []( RunStateManagerStatus_T* status ) {
            *status                         = {};
            status->state                   = RUN_STATE_IDLE;
            status->execution_timer_running = false;
        } ) );
    EXPECT_CALL( mock_dependencies, SetOutput( USER_LED_BLUE_5 ) );
    EXPECT_CALL( mock_dependencies, ResetOutput( USER_LED_BLUE_4 ) );
    EXPECT_CALL( mock_dependencies, ResetOutput( USER_LED_BLUE_3 ) );
    EXPECT_CALL( mock_dependencies, ResetOutput( USER_LED_BLUE_2 ) );
    EXPECT_CALL( mock_dependencies, ResetOutput( USER_LED_BLUE_1 ) );
    EXPECT_CALL( mock_dependencies, ToggleOutput( USER_LED_BLUE_0 ) );

    BACKGROUND_Process_Status_LED();
}

TEST_F( BackgroundTest, DisplaysFaultCodeAndLightsEveryRedFaultLedImmediately )
{
    testing::InSequence sequence;
    EXPECT_CALL( mock_dependencies, GetRunStateStatus( testing::_ ) )
        .WillOnce( testing::Invoke( []( RunStateManagerStatus_T* status ) {
            *status                         = {};
            status->state                   = RUN_STATE_FAULT;
            status->execution_timer_running = false;
        } ) );
    EXPECT_CALL( mock_dependencies, SetOutput( USER_LED_BLUE_5 ) );
    EXPECT_CALL( mock_dependencies, ResetOutput( USER_LED_BLUE_4 ) );
    EXPECT_CALL( mock_dependencies, ResetOutput( USER_LED_BLUE_3 ) );
    EXPECT_CALL( mock_dependencies, SetOutput( USER_LED_BLUE_2 ) );
    EXPECT_CALL( mock_dependencies, ResetOutput( USER_LED_BLUE_1 ) );
    EXPECT_CALL( mock_dependencies, SetOutput( USER_LED_RED_0 ) );
    EXPECT_CALL( mock_dependencies, SetOutput( USER_LED_RED_1 ) );
    EXPECT_CALL( mock_dependencies, SetOutput( USER_LED_RED_2 ) );
    EXPECT_CALL( mock_dependencies, SetOutput( USER_LED_RED_3 ) );
    EXPECT_CALL( mock_dependencies, SetOutput( USER_LED_RED_4 ) );
    EXPECT_CALL( mock_dependencies, SetOutput( USER_LED_RED_5 ) );

    BACKGROUND_Process_Status_LED();
}
