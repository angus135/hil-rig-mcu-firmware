/******************************************************************************
 * Unit tests for Run State Manager lifecycle policy and sequencing.
 * Production code is included directly following the repository convention.
 ******************************************************************************/
#include <gtest/gtest.h>
#include <cstring>

extern "C"
{
#include "run_state_manager.h"
#include "dut_driver_lifecycle.h"
#include "flash_manager.h"
#include "hw_timer.h"
#include "logic_expander.h"
#include "test_configuration.h"
}

static TaskHandle_t const TEST_RSM_TASK_HANDLE = reinterpret_cast<TaskHandle_t>( 0x1234U );
static BaseType_t         notify_result;
static uint32_t           notified_bits;
static TickType_t         current_tick;
static bool               logic_expander_ready;
static bool               active_configuration_available;
static bool               configuration_cleared;
static bool               driver_configure_result;
static DutDriverConfigurationStatus_T     driver_configuration_status;
static bool                               driver_start_result;
static bool                               driver_stop_result;
static uint32_t                           driver_start_calls;
static uint32_t                           driver_stop_calls;
static uint32_t                           driver_idle_calls;
static uint32_t                           driver_fault_calls;
static FlashManagerState_T                flash_manager_state;
static bool                               flash_get_state_result;
static FlashManagerRequestStatus_T        flash_prepare_result;
static FlashManagerRequestStatus_T        flash_finalise_result;
static FlashManagerRequestStatus_T        flash_discard_result;
static FlashManagerRequestStatus_T        flash_abort_result;
static FlashManagerResultTransferStatus_T flash_transfer_start_result;
static FlashManagerResultTransferStatus_T flash_transfer_finish_result;
static uint32_t                           flash_abort_calls;
static uint32_t                           timer_configure_calls;
static bool                               timer_start_result;
static uint32_t                           timer_start_calls;
static uint32_t                           timer_stop_calls;
static HW_TIMER_ExecutionGuard_T          execution_guard;
static FlashManagerFaultCallback_T        flash_fault_callback;

extern "C"
{
BaseType_t xTaskNotify( TaskHandle_t task, uint32_t value, eNotifyAction action )
{
    EXPECT_EQ( TEST_RSM_TASK_HANDLE, task );
    EXPECT_EQ( eSetBits, action );
    notified_bits |= value;
    return notify_result;
}
BaseType_t xTaskNotifyFromISR( TaskHandle_t task, uint32_t value, eNotifyAction action,
                               BaseType_t* higher_priority_task_woken )
{
    ( void )higher_priority_task_woken;
    return xTaskNotify( task, value, action );
}
BaseType_t xTaskNotifyWait( uint32_t, uint32_t, uint32_t*, TickType_t )
{
    return pdFAIL;
}
TaskHandle_t xTaskGetCurrentTaskHandle( void )
{
    return TEST_RSM_TASK_HANDLE;
}
TickType_t xTaskGetTickCount( void )
{
    return current_tick;
}
bool LOGIC_EXPANDER_Is_Ready( void )
{
    return logic_expander_ready;
}
bool TEST_CONFIGURATION_GetActive( DutDriverConfiguration_T* configuration )
{
    if ( active_configuration_available && configuration != nullptr )
    {
        std::memset( configuration, 0, sizeof( *configuration ) );
        return true;
    }
    return false;
}
void TEST_CONFIGURATION_Clear( void )
{
    configuration_cleared          = true;
    active_configuration_available = false;
}
bool DUT_DRIVER_LIFECYCLE_Configure( const DutDriverConfiguration_T* configuration )
{
    return configuration != nullptr && driver_configure_result;
}
DutDriverConfigurationStatus_T DUT_DRIVER_LIFECYCLE_GetConfigurationStatus( void )
{
    return driver_configuration_status;
}
bool DUT_DRIVER_LIFECYCLE_Start( void )
{
    driver_start_calls++;
    return driver_start_result;
}
bool DUT_DRIVER_LIFECYCLE_Stop( void )
{
    driver_stop_calls++;
    return driver_stop_result;
}
void DUT_DRIVER_LIFECYCLE_EnterIdle( void )
{
    driver_idle_calls++;
}
void DUT_DRIVER_LIFECYCLE_EnterFault( void )
{
    driver_fault_calls++;
}
bool FLASH_MANAGER_GetState( FlashManagerState_T* state )
{
    if ( flash_get_state_result && state != nullptr )
    {
        *state = flash_manager_state;
    }
    return flash_get_state_result;
}
FlashManagerRequestStatus_T
FLASH_MANAGER_RequestExecutionPreparation( uint32_t maximum_result_length_bytes )
{
    ( void )maximum_result_length_bytes;
    return flash_prepare_result;
}
FlashManagerRequestStatus_T FLASH_MANAGER_RequestResultFinalisation( void )
{
    return flash_finalise_result;
}
FlashManagerRequestStatus_T FLASH_MANAGER_DiscardResults( void )
{
    return flash_discard_result;
}
FlashManagerRequestStatus_T FLASH_MANAGER_RequestAbortSession( void )
{
    flash_abort_calls++;
    return flash_abort_result;
}
FlashManagerResultTransferStatus_T FLASH_MANAGER_RequestResultTransferStart( void )
{
    return flash_transfer_start_result;
}
FlashManagerResultTransferStatus_T FLASH_MANAGER_FinishResultTransfer( void )
{
    return flash_transfer_finish_result;
}
void FLASH_MANAGER_SetFaultCallback( FlashManagerFaultCallback_T callback )
{
    flash_fault_callback = callback;
}
void HW_TIMER_Configure_Timer( Timer_T, uint32_t, uint32_t )
{
    timer_configure_calls++;
}

bool HW_TIMER_Start_Timer( Timer_T )
{
    timer_start_calls++;
    return timer_start_result;
}

void HW_TIMER_Stop_Timer( Timer_T )
{
    timer_stop_calls++;
}
void HW_TIMER_Set_Execution_Guard( HW_TIMER_ExecutionGuard_T guard )
{
    execution_guard = guard;
}
}

extern "C"
{
#if defined( __GNUC__ )
/*
 * The production implementation is C11. It is included here as C++ solely to
 * expose private module state, so suppress diagnostics for valid C aggregate
 * syntax that would otherwise require C++20 in this test translation unit.
 */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wc++20-extensions"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wpedantic"
#endif
#include "../run_state_manager.c" /* Private module under test */  // NOLINT
#if defined( __GNUC__ )
#pragma GCC diagnostic pop
#endif
}

class RunStateManagerTest : public ::testing::Test
{
protected:
    void SetUp( void ) override
    {
        notify_result                  = pdPASS;
        notified_bits                  = 0U;
        current_tick                   = 0U;
        logic_expander_ready           = true;
        active_configuration_available = true;
        configuration_cleared          = false;
        driver_configure_result        = true;
        driver_configuration_status    = DUT_DRIVER_CONFIGURATION_READY;
        driver_start_result            = true;
        driver_stop_result             = true;
        driver_start_calls             = 0U;
        driver_stop_calls              = 0U;
        driver_idle_calls              = 0U;
        driver_fault_calls             = 0U;
        flash_manager_state            = FLASH_MANAGER_STATE_IDLE;
        flash_get_state_result         = true;
        flash_prepare_result           = FLASH_MANAGER_REQUEST_OK;
        flash_finalise_result          = FLASH_MANAGER_REQUEST_OK;
        flash_discard_result           = FLASH_MANAGER_REQUEST_OK;
        flash_abort_result             = FLASH_MANAGER_REQUEST_OK;
        flash_transfer_start_result    = FLASH_MANAGER_RESULT_TRANSFER_OK;
        flash_transfer_finish_result   = FLASH_MANAGER_RESULT_TRANSFER_OK;
        flash_abort_calls              = 0U;
        timer_configure_calls          = 0U;
        timer_start_result             = true;
        timer_start_calls              = 0U;
        timer_stop_calls               = 0U;
        execution_guard                = nullptr;
        flash_fault_callback           = nullptr;
        run_state_manager_task_handle  = TEST_RSM_TASK_HANDLE;
        RUN_STATE_MANAGER_Init();
        timer_stop_calls = 0U;
    }
    static void Process( RunStateRequest_T request )
    {
        RUN_STATE_MANAGER_ProcessRequest( request );
    }
    static void ConfigureToArmed( void )
    {
        Process( RUN_STATE_REQUEST_PACKAGE_RECEIVE );
        Process( RUN_STATE_REQUEST_CONFIGURATION_READY );
        RUN_STATE_MANAGER_ProcessPendingOperation();
        ASSERT_EQ( RUN_STATE_ARMED, run_state );
    }
    static void EnterExecution( void )
    {
        ConfigureToArmed();
        Process( RUN_STATE_REQUEST_EXECUTION );
        flash_manager_state = FLASH_MANAGER_STATE_EXECUTING;
        RUN_STATE_MANAGER_ProcessPendingOperation();
        ASSERT_EQ( RUN_STATE_EXECUTION, run_state );
    }
};

TEST_F( RunStateManagerTest, InitialStatusSnapshotIsCoherentAndSafe )
{
    RunStateManagerStatus_T status = {};
    RUN_STATE_MANAGER_GetStatus( &status );
    EXPECT_EQ( RUN_STATE_IDLE, status.state );
    EXPECT_FALSE( status.transition_pending );
    EXPECT_FALSE( status.execution_active );
    EXPECT_FALSE( status.execution_timer_running );
    EXPECT_EQ( RUN_STATE_FREQUENCY_1KHZ, status.execution_frequency );
    EXPECT_EQ( RUN_STATE_FAULT_NONE, status.fault_reason );
    EXPECT_EQ( RUN_STATE_REQUEST_NONE, status.last_request );
    EXPECT_EQ( RUN_STATE_REQUEST_RESULT_NONE, status.last_request_result );
    RUN_STATE_MANAGER_GetStatus( nullptr );
}

TEST_F( RunStateManagerTest, ConfigurationWaitsForReadinessBeforeArming )
{
    driver_configuration_status = DUT_DRIVER_CONFIGURATION_PENDING;
    Process( RUN_STATE_REQUEST_PACKAGE_RECEIVE );
    Process( RUN_STATE_REQUEST_CONFIGURATION_READY );
    EXPECT_EQ( RUN_STATE_CONFIGURATION, run_state );
    EXPECT_EQ( RUN_STATE_PENDING_CONFIGURATION, pending_operation );
    RUN_STATE_MANAGER_ProcessPendingOperation();
    EXPECT_EQ( RUN_STATE_CONFIGURATION, run_state );
    driver_configuration_status = DUT_DRIVER_CONFIGURATION_READY;
    RUN_STATE_MANAGER_ProcessPendingOperation();
    EXPECT_EQ( RUN_STATE_ARMED, run_state );
    EXPECT_EQ( RUN_STATE_PENDING_NONE, pending_operation );
}

TEST_F( RunStateManagerTest, ConfigurationFailureEntersFault )
{
    driver_configuration_status = DUT_DRIVER_CONFIGURATION_FAILED;
    Process( RUN_STATE_REQUEST_PACKAGE_RECEIVE );
    Process( RUN_STATE_REQUEST_CONFIGURATION_READY );
    RUN_STATE_MANAGER_ProcessPendingOperation();
    EXPECT_EQ( RUN_STATE_FAULT, run_state );
    EXPECT_EQ( RUN_STATE_FAULT_DRIVER_CONFIGURATION, fault_reason );
    EXPECT_EQ( 1U, driver_fault_calls );
    EXPECT_EQ( 1U, flash_abort_calls );
}

TEST_F( RunStateManagerTest, ConfigurationTimeoutEntersFault )
{
    driver_configuration_status = DUT_DRIVER_CONFIGURATION_PENDING;
    Process( RUN_STATE_REQUEST_PACKAGE_RECEIVE );
    Process( RUN_STATE_REQUEST_CONFIGURATION_READY );
    current_tick = RUN_STATE_MANAGER_CONFIGURATION_TIMEOUT_MS;
    RUN_STATE_MANAGER_ProcessPendingOperation();
    EXPECT_EQ( RUN_STATE_FAULT, run_state );
    EXPECT_EQ( RUN_STATE_FAULT_DRIVER_CONFIGURATION_TIMEOUT, fault_reason );
}

TEST_F( RunStateManagerTest, ExecutionStartsOnlyAfterFlashIsExecuting )
{
    ConfigureToArmed();
    Process( RUN_STATE_REQUEST_EXECUTION );
    EXPECT_EQ( RUN_STATE_ARMED, run_state );
    EXPECT_EQ( RUN_STATE_PENDING_EXECUTION_PREPARATION, pending_operation );
    EXPECT_EQ( 0U, driver_start_calls );
    flash_manager_state = FLASH_MANAGER_STATE_EXECUTING;
    RUN_STATE_MANAGER_ProcessPendingOperation();
    EXPECT_EQ( RUN_STATE_EXECUTION, run_state );
    EXPECT_TRUE( execution_active );
    EXPECT_TRUE( execution_timer_running );
    EXPECT_EQ( 1U, driver_start_calls );
    EXPECT_EQ( 1U, timer_start_calls );
}

TEST_F( RunStateManagerTest, ExecutionCompletionStopsDriversAndWaitsForResults )
{
    EnterExecution();
    Process( RUN_STATE_REQUEST_EXECUTION_COMPLETE );
    EXPECT_EQ( RUN_STATE_RESULT_FINALISATION, run_state );
    EXPECT_FALSE( execution_active );
    EXPECT_FALSE( execution_timer_running );
    EXPECT_EQ( RUN_STATE_PENDING_RESULT_FINALISATION, pending_operation );
    flash_manager_state = FLASH_MANAGER_STATE_RESULTS_READY;
    RUN_STATE_MANAGER_ProcessPendingOperation();
    EXPECT_EQ( RUN_STATE_RESULTS_READY, run_state );
}

TEST_F( RunStateManagerTest, InvalidRequestIsRejectedWithoutFaulting )
{
    Process( RUN_STATE_REQUEST_EXECUTION );
    EXPECT_EQ( RUN_STATE_IDLE, run_state );
    EXPECT_EQ( RUN_STATE_FAULT_NONE, fault_reason );
    EXPECT_EQ( RUN_STATE_REQUEST_RESULT_REJECTED_STATE, last_request_result );
}

TEST_F( RunStateManagerTest, RepeatRetainsConfigurationAndReturnsToArmed )
{
    run_state = RUN_STATE_RESULTS_READY;
    Process( RUN_STATE_REQUEST_REPEAT );
    EXPECT_EQ( RUN_STATE_ARMED, run_state );
    EXPECT_FALSE( configuration_cleared );
}

TEST_F( RunStateManagerTest, DiscardClearsConfigurationAndReturnsToIdle )
{
    run_state = RUN_STATE_RESULTS_READY;
    Process( RUN_STATE_REQUEST_DISCARD_RESULTS );
    EXPECT_EQ( RUN_STATE_IDLE, run_state );
    EXPECT_TRUE( configuration_cleared );
    EXPECT_EQ( 1U, driver_idle_calls );
}

TEST_F( RunStateManagerTest, RuntimeFaultStopsExecutionAndRequestsFlashAbort )
{
    EnterExecution();
    EXPECT_FALSE( RUN_STATE_MANAGER_ExecutionAbortRequestedFromISR() );
    requested_fault_reason = RUN_STATE_FAULT_EXTERNAL_REQUEST;
    Process( RUN_STATE_REQUEST_FAULT );
    EXPECT_EQ( RUN_STATE_FAULT, run_state );
    EXPECT_FALSE( execution_active );
    EXPECT_FALSE( execution_timer_running );
    EXPECT_EQ( RUN_STATE_FAULT_EXTERNAL_REQUEST, fault_reason );
    EXPECT_EQ( 1U, flash_abort_calls );
    EXPECT_EQ( 1U, driver_fault_calls );
    EXPECT_TRUE( RUN_STATE_MANAGER_ExecutionAbortRequestedFromISR() );
}

TEST_F( RunStateManagerTest, TaskFaultRequestInhibitsExecutionBeforeTaskProcessesNotification )
{
    EnterExecution();
    EXPECT_TRUE( RUN_STATE_MANAGER_RequestFault( RUN_STATE_FAULT_FLASH_MANAGER ) );
    EXPECT_TRUE( RUN_STATE_MANAGER_ExecutionAbortRequestedFromISR() );
    EXPECT_EQ( RUN_STATE_MANAGER_NOTIFY_FAULT, notified_bits );
    EXPECT_EQ( RUN_STATE_EXECUTION, run_state );
}

TEST_F( RunStateManagerTest, IsrFaultRequestInhibitsExecutionAndNotifiesTask )
{
    EnterExecution();
    EXPECT_TRUE( RUN_STATE_MANAGER_RequestFaultFromISR( RUN_STATE_FAULT_FLASH_MANAGER ) );
    EXPECT_TRUE( RUN_STATE_MANAGER_ExecutionAbortRequestedFromISR() );
    EXPECT_EQ( RUN_STATE_MANAGER_NOTIFY_FAULT, notified_bits );
    EXPECT_EQ( RUN_STATE_FAULT_FLASH_MANAGER, requested_fault_reason );
}

TEST_F( RunStateManagerTest, ResetWaitsForFlashIdle )
{
    run_state           = RUN_STATE_FAULT;
    fault_reason        = RUN_STATE_FAULT_EXTERNAL_REQUEST;
    flash_manager_state = FLASH_MANAGER_STATE_ABORTING;
    Process( RUN_STATE_REQUEST_RESET );
    EXPECT_EQ( RUN_STATE_FAULT, run_state );
    EXPECT_EQ( RUN_STATE_REQUEST_RESULT_REJECTED_SUBSYSTEM_STATE, last_request_result );
    flash_manager_state = FLASH_MANAGER_STATE_IDLE;
    Process( RUN_STATE_REQUEST_RESET );
    EXPECT_EQ( RUN_STATE_IDLE, run_state );
    EXPECT_EQ( RUN_STATE_FAULT_NONE, fault_reason );
    EXPECT_FALSE( RUN_STATE_MANAGER_ExecutionAbortRequestedFromISR() );
}

TEST_F( RunStateManagerTest, FirstFaultReasonWins )
{
    requested_fault_reason = RUN_STATE_FAULT_DRIVER_START;
    Process( RUN_STATE_REQUEST_FAULT );
    RUN_STATE_MANAGER_RecordFault( RUN_STATE_FAULT_FLASH_MANAGER );
    EXPECT_EQ( RUN_STATE_FAULT_DRIVER_START, fault_reason );
}

TEST_F( RunStateManagerTest, DiagnosticTimerRequestsUseExplicitNotificationBits )
{
    EXPECT_TRUE( RUN_STATE_MANAGER_RequestDiagnosticExecutionTimerStart() );
    EXPECT_NE( 0U, notified_bits & RUN_STATE_MANAGER_NOTIFY_TIMER_START );
    notified_bits = 0U;
    EXPECT_TRUE( RUN_STATE_MANAGER_RequestDiagnosticExecutionTimerStop() );
    EXPECT_NE( 0U, notified_bits & RUN_STATE_MANAGER_NOTIFY_TIMER_STOP );
}
