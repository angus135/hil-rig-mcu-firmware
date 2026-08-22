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

#include "../background.c" /* Module under test */  // NOLINT
}

class MockBackgroundDependencies
{
public:
    MOCK_METHOD( bool, InitLogicExpander, () );
    MOCK_METHOD( LogicExpanderStatus_T, ProcessLogicExpander, () );
    MOCK_METHOD( void, ToggleOutput, ( GPIOOutput_T output ) );
    MOCK_METHOD( TickType_t, GetTickCount, () );
    MOCK_METHOD( void, DelayUntil, ( TickType_t * previous_wake_time, TickType_t increment ) );
    MOCK_METHOD( void, SuspendTask, ( TaskHandle_t task_handle ) );
};

static MockBackgroundDependencies* g_mock_dependencies = nullptr;

extern "C"
{
bool LOGIC_EXPANDER_Init( void )
{
    return g_mock_dependencies->InitLogicExpander();
}

LogicExpanderStatus_T LOGIC_EXPANDER_Process( void )
{
    return g_mock_dependencies->ProcessLogicExpander();
}

void HW_GPIO_Toggle_Output( GPIOOutput_T output )
{
    g_mock_dependencies->ToggleOutput( output );
}

volatile TickType_t xTaskGetTickCount( void )
{
    return g_mock_dependencies->GetTickCount();
}

void vTaskDelayUntil( TickType_t* previous_wake_time, const TickType_t increment )
{
    g_mock_dependencies->DelayUntil( previous_wake_time, increment );
}

void vTaskSuspend( TaskHandle_t task_handle )
{
    g_mock_dependencies->SuspendTask( task_handle );
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
        g_mock_dependencies             = &mock_dependencies;
        background_led_cycles_remaining = 0U;
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

    for ( uint16_t cycle = 0U; cycle < 101U; ++cycle )
    {
        BACKGROUND_Process();
    }
}

TEST_F( BackgroundTest, InitialiserListInvokesLogicExpanderInit )
{
    EXPECT_CALL( mock_dependencies, InitLogicExpander() ).WillOnce( testing::Return( true ) );

    EXPECT_TRUE( BACKGROUND_Run_Initialisers() );
}

TEST_F( BackgroundTest, TaskInitialisesOnceBeforePeriodicProcessing )
{
    testing::InSequence sequence;
    EXPECT_CALL( mock_dependencies, InitLogicExpander() ).WillOnce( testing::Return( true ) );
    EXPECT_CALL( mock_dependencies, GetTickCount() ).WillOnce( testing::Return( 17U ) );
    EXPECT_CALL( mock_dependencies, ProcessLogicExpander() )
        .WillOnce( testing::Return( LOGIC_EXPANDER_STATUS_NOT_READY ) );
    EXPECT_CALL( mock_dependencies, ToggleOutput( testing::_ ) ).Times( testing::AnyNumber() );
    EXPECT_CALL( mock_dependencies, DelayUntil( testing::_, BACKGROUND_TASK_PERIOD_MS ) )
        .WillOnce(
            testing::Invoke( []( TickType_t*, TickType_t ) { throw BackgroundTaskExit{}; } ) );

    EXPECT_THROW( BACKGROUND_Task( nullptr ), BackgroundTaskExit );
}

TEST_F( BackgroundTest, TaskSuspendsWithoutPeriodicProcessingWhenInitialisationFails )
{
    testing::InSequence sequence;
    EXPECT_CALL( mock_dependencies, InitLogicExpander() ).WillOnce( testing::Return( false ) );
    EXPECT_CALL( mock_dependencies, SuspendTask( nullptr ) )
        .WillOnce( testing::Invoke( []( TaskHandle_t ) { throw BackgroundTaskExit{}; } ) );

    EXPECT_THROW( BACKGROUND_Task( nullptr ), BackgroundTaskExit );
}
