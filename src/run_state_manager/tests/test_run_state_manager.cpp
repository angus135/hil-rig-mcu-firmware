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
#include "execution_manager.h"
#include "flash_manager.h"
#include "host_interface.h"
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
static bool               configuration_ownership_released;
static bool               driver_configure_result;
static DutDriverConfigurationStatus_T               driver_configuration_status;
static DutDriverStartStatus_T                       driver_start_status;
static bool                                         driver_start_result;
static bool                                         driver_epoch_result;
static uint32_t                                     driver_epoch_calls;
static bool                                         driver_stop_result;
static bool                                         driver_shutdown_begin_result;
static DutDriverShutdownStatus_T                    driver_shutdown_status;
static uint32_t                                     driver_shutdown_begin_calls;
static bool                                         driver_shutdown_force;
static bool                                         driver_shutdown_clear_configuration;
static uint32_t                                     driver_start_calls;
static uint32_t                                     driver_stop_calls;
static uint32_t                                     driver_idle_calls;
static uint32_t                                     driver_fault_calls;
static FlashManagerState_T                          flash_manager_state;
static bool                                         flash_get_state_result;
static FlashManagerRequestStatus_T                  flash_prepare_result;
static FlashManagerRequestStatus_T                  flash_finalise_result;
static FlashManagerRequestStatus_T                  flash_discard_result;
static uint32_t                                     flash_discard_calls;
static FlashManagerRequestStatus_T                  flash_abort_result;
static FlashManagerInstructionUploadRequestStatus_T flash_upload_start_result;
static uint32_t                                     flash_upload_start_calls;
static uint32_t                                     flash_upload_start_expected_length;
static FlashManagerInstructionUploadRequestStatus_T flash_upload_finish_result;
static uint32_t                                     flash_upload_finish_calls;
static bool                                         flash_instruction_capacity_result;
static uint32_t                                     flash_instruction_capacity_bytes;
static FlashManagerResultTransferStatus_T           flash_transfer_start_result;
static FlashManagerResultTransferStatus_T           flash_transfer_finish_result;
static uint32_t                                     flash_transfer_finish_calls;
static uint32_t                                     flash_abort_calls;
static uint32_t                                     timer_configure_calls;
static bool                                         timer_start_result;
static uint32_t                                     timer_start_calls;
static uint32_t                                     timer_stop_calls;
static HW_TIMER_ExecutionGuard_T                    execution_guard;
static FlashManagerFaultCallback_T                  flash_fault_callback;
static uint32_t                                     flash_prepare_capacity;
static bool                                         execution_prepare_result;
static uint32_t                                     execution_prepare_tick_count;
static uint32_t                                     execution_abort_calls;
static ExecutionManagerTerminalCallback_T           execution_terminal_callback;
static bool                                         host_interface_notify_result;
static uint32_t                                     host_interface_notified_bits;
static uint32_t                                     host_interface_notify_calls;

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
    if ( higher_priority_task_woken != nullptr )
    {
        *higher_priority_task_woken = pdTRUE;
    }
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
bool TEST_CONFIGURATION_AcquireForRun( DutDriverConfiguration_T* configuration )
{
    return TEST_CONFIGURATION_GetActive( configuration );
}
void TEST_CONFIGURATION_ReleaseRunOwnership( void )
{
    configuration_ownership_released = true;
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
void DUT_DRIVER_LIFECYCLE_GetStatus( DutDriverLifecycleStatus_T* status )
{
    *status = {};
}
bool DUT_DRIVER_LIFECYCLE_Start( void )
{
    driver_start_calls++;
    return driver_start_result;
}
bool DUT_DRIVER_LIFECYCLE_EstablishExecutionEpoch( void )
{
    driver_epoch_calls++;
    return driver_epoch_result;
}
DutDriverStartStatus_T DUT_DRIVER_LIFECYCLE_GetStartStatus( void )
{
    return driver_start_status;
}
bool DUT_DRIVER_LIFECYCLE_Stop( void )
{
    driver_stop_calls++;
    return driver_stop_result;
}
bool DUT_DRIVER_LIFECYCLE_BeginShutdown( bool force_abort, bool clear_configuration )
{
    driver_shutdown_begin_calls++;
    driver_shutdown_force               = force_abort;
    driver_shutdown_clear_configuration = clear_configuration;
    return driver_shutdown_begin_result;
}
DutDriverShutdownStatus_T DUT_DRIVER_LIFECYCLE_GetShutdownStatus( void )
{
    return driver_shutdown_status;
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
    flash_prepare_capacity = maximum_result_length_bytes;
    return flash_prepare_result;
}
FlashManagerRequestStatus_T FLASH_MANAGER_RequestResultFinalisation( void )
{
    return flash_finalise_result;
}
FlashManagerRequestStatus_T FLASH_MANAGER_DiscardResults( void )
{
    flash_discard_calls++;
    if ( flash_discard_result == FLASH_MANAGER_REQUEST_OK )
    {
        flash_manager_state = FLASH_MANAGER_STATE_IDLE;
    }
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
    flash_transfer_finish_calls++;
    return flash_transfer_finish_result;
}
void FLASH_MANAGER_SetFaultCallback( FlashManagerFaultCallback_T callback )
{
    flash_fault_callback = callback;
}
bool FLASH_MANAGER_GetResultCapacityBytes( uint32_t* capacity_bytes )
{
    if ( capacity_bytes != nullptr )
    {
        *capacity_bytes = 66453504U;
        return true;
    }
    return false;
}
FlashManagerInstructionUploadRequestStatus_T
FLASH_MANAGER_RequestInstructionUploadStart( uint32_t expected_length_bytes )
{
    flash_upload_start_calls++;
    flash_upload_start_expected_length = expected_length_bytes;
    return flash_upload_start_result;
}
FlashManagerInstructionUploadRequestStatus_T FLASH_MANAGER_RequestInstructionUploadFinish( void )
{
    flash_upload_finish_calls++;
    return flash_upload_finish_result;
}
bool FLASH_MANAGER_GetInstructionCapacityBytes( uint32_t* capacity_bytes )
{
    if ( flash_instruction_capacity_result && ( capacity_bytes != nullptr ) )
    {
        *capacity_bytes = flash_instruction_capacity_bytes;
        return true;
    }
    return false;
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
bool EXECUTION_MANAGER_Prepare( uint32_t tick_count )
{
    execution_prepare_tick_count = tick_count;
    return execution_prepare_result;
}
void EXECUTION_MANAGER_ConfigureMeasurements(
    const ExecutionMeasurementConfiguration_T* configuration )
{
    ( void )configuration;
}
void EXECUTION_MANAGER_Abort( void )
{
    execution_abort_calls++;
}
void EXECUTION_MANAGER_SetTerminalCallback( ExecutionManagerTerminalCallback_T callback )
{
    execution_terminal_callback = callback;
}
void HW_CAN_GetDiagnostic( HW_CAN_Diagnostic_T* diag )
{
    if ( diag != nullptr )
    {
        std::memset( diag, 0, sizeof( *diag ) );
    }
}
bool HOST_INTERFACE_Notify( uint32_t notification )
{
    host_interface_notify_calls++;
    host_interface_notified_bits |= notification;
    return host_interface_notify_result;
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
        notify_result                       = pdPASS;
        notified_bits                       = 0U;
        current_tick                        = 0U;
        logic_expander_ready                = true;
        active_configuration_available      = true;
        configuration_cleared               = false;
        configuration_ownership_released    = false;
        driver_configure_result             = true;
        driver_configuration_status         = DUT_DRIVER_CONFIGURATION_READY;
        driver_start_status                 = DUT_DRIVER_START_READY;
        driver_start_result                 = true;
        driver_epoch_result                 = true;
        driver_epoch_calls                  = 0U;
        driver_stop_result                  = true;
        driver_shutdown_begin_result        = true;
        driver_shutdown_status              = DUT_DRIVER_SHUTDOWN_COMPLETE;
        driver_shutdown_begin_calls         = 0U;
        driver_shutdown_force               = false;
        driver_shutdown_clear_configuration = false;
        driver_start_calls                  = 0U;
        driver_stop_calls                   = 0U;
        driver_idle_calls                   = 0U;
        driver_fault_calls                  = 0U;
        flash_manager_state                 = FLASH_MANAGER_STATE_IDLE;
        flash_get_state_result              = true;
        flash_prepare_result                = FLASH_MANAGER_REQUEST_OK;
        flash_finalise_result               = FLASH_MANAGER_REQUEST_OK;
        flash_discard_result                = FLASH_MANAGER_REQUEST_OK;
        flash_discard_calls                 = 0U;
        flash_abort_result                  = FLASH_MANAGER_REQUEST_OK;
        flash_upload_start_result           = FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_ACCEPTED;
        flash_upload_start_calls            = 0U;
        flash_upload_start_expected_length  = 0U;
        flash_upload_finish_result          = FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_ACCEPTED;
        flash_upload_finish_calls           = 0U;
        flash_instruction_capacity_result   = true;
        flash_instruction_capacity_bytes    = 66453504U;
        flash_transfer_start_result         = FLASH_MANAGER_RESULT_TRANSFER_OK;
        flash_transfer_finish_result        = FLASH_MANAGER_RESULT_TRANSFER_OK;
        flash_transfer_finish_calls         = 0U;
        flash_abort_calls                   = 0U;
        timer_configure_calls               = 0U;
        timer_start_result                  = true;
        timer_start_calls                   = 0U;
        timer_stop_calls                    = 0U;
        execution_guard                     = nullptr;
        flash_fault_callback                = nullptr;
        flash_prepare_capacity              = 0U;
        execution_prepare_result            = true;
        execution_prepare_tick_count        = 0U;
        execution_abort_calls               = 0U;
        execution_terminal_callback         = nullptr;
        host_interface_notify_result        = true;
        host_interface_notified_bits        = 0U;
        host_interface_notify_calls         = 0U;
        run_state_manager_task_handle       = TEST_RSM_TASK_HANDLE;
        RUN_STATE_MANAGER_Init();
        timer_stop_calls      = 0U;
        execution_abort_calls = 0U;
    }
    static void Process( RunStateRequest_T request )
    {
        RUN_STATE_MANAGER_ProcessRequest( request );
    }
    static void ConfigureToArmed( void )
    {
        Process( RUN_STATE_REQUEST_PACKAGE_RECEIVE );
        flash_manager_state = FLASH_MANAGER_STATE_INSTRUCTION_UPLOAD;
        RUN_STATE_MANAGER_ProcessPendingOperation();
        Process( RUN_STATE_REQUEST_CONFIGURATION_READY );
        flash_manager_state = FLASH_MANAGER_STATE_IDLE;
        RUN_STATE_MANAGER_ProcessPendingOperation();
        RUN_STATE_MANAGER_ProcessPendingOperation();
        flash_manager_state = FLASH_MANAGER_STATE_EXECUTING;
        RUN_STATE_MANAGER_ProcessPendingOperation();
        ASSERT_EQ( RUN_STATE_ARMED, run_state );
    }
    static void EnterExecution( void )
    {
        ConfigureToArmed();
        prepared_execution = ( RunStatePreparedExecution_T ){
            .tick_count = 10U, .frequency = RUN_STATE_FREQUENCY_1KHZ };
        Process( RUN_STATE_REQUEST_EXECUTION );
        RUN_STATE_MANAGER_ProcessPendingOperation();
        ASSERT_EQ( RUN_STATE_EXECUTION, run_state );
    }
};

TEST_F( RunStateManagerTest, InitialStatusSnapshotIsCoherentAndSafe )
{
    RunStateManagerStatus_T status = {};
    RUN_STATE_MANAGER_GetStatus( &status );
    EXPECT_EQ( RUN_STATE_IDLE, status.state );
    EXPECT_FALSE( status.request_timing_active );
    EXPECT_FALSE( status.last_transition_timing_valid );
    EXPECT_EQ( RUN_STATE_REQUEST_NONE, status.last_request );
    EXPECT_EQ( RUN_STATE_REQUEST_RESULT_NONE, status.last_request_result );
    EXPECT_EQ( RUN_STATE_REQUEST_NONE, status.last_completed_request );
    EXPECT_EQ( RUN_STATE_REQUEST_NONE, status.timed_request );
    EXPECT_EQ( 0U, status.timed_request_elapsed_ms );
    EXPECT_EQ( 0U, status.last_transition_duration_ms );
    EXPECT_EQ( RUN_STATE_FAULT_NONE, status.fault_reason );
}

TEST_F( RunStateManagerTest, NullStatusPointerIsIgnoredSafely )
{
    RUN_STATE_MANAGER_GetStatus( nullptr );
}

TEST_F( RunStateManagerTest, ReportsTotalConfigurationTransitionTime )
{
    driver_configuration_status = DUT_DRIVER_CONFIGURATION_PENDING;
    Process( RUN_STATE_REQUEST_PACKAGE_RECEIVE );
    flash_manager_state = FLASH_MANAGER_STATE_INSTRUCTION_UPLOAD;
    RUN_STATE_MANAGER_ProcessPendingOperation();

    current_tick = 100U;
    Process( RUN_STATE_REQUEST_CONFIGURATION_READY );
    flash_manager_state = FLASH_MANAGER_STATE_IDLE;
    RUN_STATE_MANAGER_ProcessPendingOperation();

    current_tick                   = 137U;
    RunStateManagerStatus_T status = {};
    RUN_STATE_MANAGER_GetStatus( &status );
    EXPECT_TRUE( status.request_timing_active );
    EXPECT_EQ( RUN_STATE_REQUEST_CONFIGURATION_READY, status.timed_request );
    EXPECT_EQ( 37U, status.timed_request_elapsed_ms );

    driver_configuration_status = DUT_DRIVER_CONFIGURATION_READY;
    RUN_STATE_MANAGER_ProcessPendingOperation();
    EXPECT_EQ( RUN_STATE_PENDING_EXECUTION_PREPARATION, pending_operation );

    flash_manager_state = FLASH_MANAGER_STATE_EXECUTING;
    current_tick        = 145U;
    RUN_STATE_MANAGER_ProcessPendingOperation();
    RUN_STATE_MANAGER_GetStatus( &status );
    EXPECT_FALSE( status.request_timing_active );
    EXPECT_TRUE( status.last_transition_timing_valid );
    EXPECT_EQ( RUN_STATE_REQUEST_CONFIGURATION_READY, status.last_completed_request );
    EXPECT_EQ( 45U, status.last_transition_duration_ms );
}

TEST_F( RunStateManagerTest, ExecutionTimingSpansDriverStartup )
{
    ConfigureToArmed();
    current_tick        = 200U;
    driver_start_status = DUT_DRIVER_START_PENDING;
    Process( RUN_STATE_REQUEST_EXECUTION );

    current_tick = 225U;
    RUN_STATE_MANAGER_ProcessPendingOperation();
    EXPECT_EQ( RUN_STATE_PENDING_DRIVER_START, pending_operation );

    current_tick        = 241U;
    driver_start_status = DUT_DRIVER_START_READY;
    RUN_STATE_MANAGER_ProcessPendingOperation();

    RunStateManagerStatus_T status = {};
    RUN_STATE_MANAGER_GetStatus( &status );
    EXPECT_EQ( RUN_STATE_EXECUTION, status.state );
    EXPECT_FALSE( status.request_timing_active );
    EXPECT_EQ( RUN_STATE_REQUEST_EXECUTION, status.last_completed_request );
    EXPECT_EQ( 41U, status.last_transition_duration_ms );
}

TEST_F( RunStateManagerTest, ConfigurationWaitsForReadinessBeforeArming )
{
    driver_configuration_status = DUT_DRIVER_CONFIGURATION_PENDING;
    Process( RUN_STATE_REQUEST_PACKAGE_RECEIVE );
    flash_manager_state = FLASH_MANAGER_STATE_INSTRUCTION_UPLOAD;
    RUN_STATE_MANAGER_ProcessPendingOperation();
    Process( RUN_STATE_REQUEST_CONFIGURATION_READY );
    flash_manager_state = FLASH_MANAGER_STATE_IDLE;
    RUN_STATE_MANAGER_ProcessPendingOperation();
    EXPECT_EQ( RUN_STATE_CONFIGURATION, run_state );
    EXPECT_EQ( RUN_STATE_PENDING_CONFIGURATION, pending_operation );
    RUN_STATE_MANAGER_ProcessPendingOperation();
    EXPECT_EQ( RUN_STATE_CONFIGURATION, run_state );
    driver_configuration_status = DUT_DRIVER_CONFIGURATION_READY;
    RUN_STATE_MANAGER_ProcessPendingOperation();
    EXPECT_EQ( RUN_STATE_CONFIGURATION, run_state );
    EXPECT_EQ( RUN_STATE_PENDING_EXECUTION_PREPARATION, pending_operation );
    flash_manager_state = FLASH_MANAGER_STATE_EXECUTING;
    RUN_STATE_MANAGER_ProcessPendingOperation();
    EXPECT_EQ( RUN_STATE_ARMED, run_state );
    EXPECT_EQ( RUN_STATE_PENDING_NONE, pending_operation );
}

TEST_F( RunStateManagerTest, ConfigurationFailureEntersFault )
{
    driver_configuration_status = DUT_DRIVER_CONFIGURATION_FAILED;
    Process( RUN_STATE_REQUEST_PACKAGE_RECEIVE );
    flash_manager_state = FLASH_MANAGER_STATE_INSTRUCTION_UPLOAD;
    RUN_STATE_MANAGER_ProcessPendingOperation();
    Process( RUN_STATE_REQUEST_CONFIGURATION_READY );
    flash_manager_state = FLASH_MANAGER_STATE_IDLE;
    RUN_STATE_MANAGER_ProcessPendingOperation();
    RUN_STATE_MANAGER_ProcessPendingOperation();
    EXPECT_EQ( RUN_STATE_FAULT, run_state );
    EXPECT_EQ( RUN_STATE_FAULT_DRIVER_CONFIGURATION, fault_reason );
    EXPECT_EQ( 1U, driver_shutdown_begin_calls );
    EXPECT_EQ( 1U, flash_abort_calls );
}

TEST_F( RunStateManagerTest, ConfigurationTimeoutEntersFault )
{
    driver_configuration_status = DUT_DRIVER_CONFIGURATION_PENDING;
    Process( RUN_STATE_REQUEST_PACKAGE_RECEIVE );
    flash_manager_state = FLASH_MANAGER_STATE_INSTRUCTION_UPLOAD;
    RUN_STATE_MANAGER_ProcessPendingOperation();
    current_tick = 100U;
    Process( RUN_STATE_REQUEST_CONFIGURATION_READY );
    flash_manager_state = FLASH_MANAGER_STATE_IDLE;
    RUN_STATE_MANAGER_ProcessPendingOperation();
    current_tick = 100U + RUN_STATE_MANAGER_CONFIGURATION_TIMEOUT_MS;
    RUN_STATE_MANAGER_ProcessPendingOperation();
    EXPECT_EQ( RUN_STATE_FAULT, run_state );
    EXPECT_EQ( RUN_STATE_FAULT_DRIVER_CONFIGURATION_TIMEOUT, fault_reason );
}

TEST_F( RunStateManagerTest, ExecutionStartsOnlyAfterDriverStartupComplete )
{
    ConfigureToArmed();
    prepared_execution =
        ( RunStatePreparedExecution_T ){ .tick_count = 25U, .frequency = RUN_STATE_FREQUENCY_1KHZ };
    driver_start_status = DUT_DRIVER_START_PENDING;
    Process( RUN_STATE_REQUEST_EXECUTION );
    EXPECT_EQ( RUN_STATE_ARMED, run_state );
    EXPECT_EQ( RUN_STATE_PENDING_DRIVER_START, pending_operation );
    EXPECT_EQ( 1U, driver_start_calls );
    EXPECT_FALSE( execution_timer_running );

    RUN_STATE_MANAGER_ProcessPendingOperation();
    EXPECT_EQ( RUN_STATE_ARMED, run_state );
    EXPECT_EQ( RUN_STATE_PENDING_DRIVER_START, pending_operation );
    EXPECT_FALSE( execution_timer_running );

    driver_start_status = DUT_DRIVER_START_READY;
    RUN_STATE_MANAGER_ProcessPendingOperation();
    EXPECT_EQ( RUN_STATE_EXECUTION, run_state );
    EXPECT_TRUE( execution_active );
    EXPECT_TRUE( execution_timer_running );
    EXPECT_EQ( 1U, driver_start_calls );
    EXPECT_EQ( 1U, driver_epoch_calls );
    EXPECT_EQ( 1U, timer_start_calls );
    EXPECT_EQ( 25U, execution_prepare_tick_count );
    EXPECT_EQ( 66453504U, flash_prepare_capacity );
}

TEST_F( RunStateManagerTest, AcquisitionEpochFailurePreventsTimerStart )
{
    ConfigureToArmed();
    prepared_execution =
        ( RunStatePreparedExecution_T ){ .tick_count = 10U, .frequency = RUN_STATE_FREQUENCY_1KHZ };
    driver_epoch_result = false;
    Process( RUN_STATE_REQUEST_EXECUTION );

    RUN_STATE_MANAGER_ProcessPendingOperation();

    EXPECT_EQ( RUN_STATE_FAULT, run_state );
    EXPECT_EQ( RUN_STATE_FAULT_ACQUISITION_EPOCH, fault_reason );
    EXPECT_EQ( 1U, driver_epoch_calls );
    EXPECT_EQ( 0U, timer_start_calls );
    EXPECT_FALSE( execution_active );
}

TEST_F( RunStateManagerTest, ExecutionRequestCopiesValidatedSessionBounds )
{
    RunStateExecutionRequest_T request = { 33U, 4096U };

    ConfigureToArmed();
    frequency_mode = RUN_STATE_FREQUENCY_10KHZ;
    EXPECT_EQ( RUN_STATE_EXECUTION_REQUEST_ACCEPTED,
               RUN_STATE_MANAGER_RequestExecution( &request ) );
    request.tick_count = 1U;
    EXPECT_EQ( 33U, prepared_execution.tick_count );
    EXPECT_EQ( RUN_STATE_FREQUENCY_10KHZ, prepared_execution.frequency );
    EXPECT_TRUE( execution_request_pending );
    EXPECT_EQ( RUN_STATE_MANAGER_NOTIFY_EXECUTION, notified_bits );
    EXPECT_EQ( RUN_STATE_EXECUTION_REQUEST_BUSY, RUN_STATE_MANAGER_RequestExecution( &request ) );
    EXPECT_EQ( RUN_STATE_EXECUTION_REQUEST_INVALID_ARGUMENT,
               RUN_STATE_MANAGER_RequestExecution( nullptr ) );
}

TEST_F( RunStateManagerTest, FrequencyChangeIsRejectedWhileExecutionRequestIsPending )
{
    ConfigureToArmed();
    RunStateExecutionRequest_T request = { 33U, 0U };

    EXPECT_TRUE( RUN_STATE_MANAGER_Set_Execution_Frequency( RUN_STATE_FREQUENCY_10KHZ ) );
    EXPECT_EQ( RUN_STATE_EXECUTION_REQUEST_ACCEPTED,
               RUN_STATE_MANAGER_RequestExecution( &request ) );
    EXPECT_FALSE( RUN_STATE_MANAGER_Set_Execution_Frequency( RUN_STATE_FREQUENCY_100HZ ) );
    EXPECT_EQ( RUN_STATE_FREQUENCY_10KHZ, frequency_mode );
    EXPECT_EQ( RUN_STATE_FREQUENCY_10KHZ, prepared_execution.frequency );
}

TEST_F( RunStateManagerTest, ExecutionRequestOwnershipIsHeldUntilExecutionBecomesActive )
{
    ConfigureToArmed();
    RunStateExecutionRequest_T request = { 25U, 0U };

    ASSERT_EQ( RUN_STATE_EXECUTION_REQUEST_ACCEPTED,
               RUN_STATE_MANAGER_RequestExecution( &request ) );
    driver_start_status = DUT_DRIVER_START_PENDING;
    Process( RUN_STATE_REQUEST_EXECUTION );
    EXPECT_TRUE( execution_request_pending );
    EXPECT_EQ( RUN_STATE_PENDING_DRIVER_START, pending_operation );

    RUN_STATE_MANAGER_ProcessPendingOperation();
    EXPECT_TRUE( execution_request_pending );
    EXPECT_EQ( RUN_STATE_PENDING_DRIVER_START, pending_operation );

    driver_start_status = DUT_DRIVER_START_READY;
    RUN_STATE_MANAGER_ProcessPendingOperation();
    EXPECT_TRUE( execution_active );
    EXPECT_FALSE( execution_request_pending );
}

TEST_F( RunStateManagerTest, FailedExecutionNotificationReleasesRequestOwnership )
{
    ConfigureToArmed();
    RunStateExecutionRequest_T request = { 33U, 0U };
    notify_result                      = pdFAIL;

    EXPECT_EQ( RUN_STATE_EXECUTION_REQUEST_NOTIFY_FAILED,
               RUN_STATE_MANAGER_RequestExecution( &request ) );
    EXPECT_FALSE( execution_request_pending );
}

TEST_F( RunStateManagerTest, ExecutionRequestIsRejectedOutsideArmedState )
{
    RunStateExecutionRequest_T request = { 33U, 0U };

    EXPECT_EQ( RUN_STATE_EXECUTION_REQUEST_INVALID_STATE,
               RUN_STATE_MANAGER_RequestExecution( &request ) );
    EXPECT_FALSE( execution_request_pending );
    EXPECT_EQ( 0U, notified_bits );
}

TEST_F( RunStateManagerTest, ExecutionManagerPreparationFailurePreventsDriverAndTimerStart )
{
    ConfigureToArmed();
    prepared_execution =
        ( RunStatePreparedExecution_T ){ .tick_count = 10U, .frequency = RUN_STATE_FREQUENCY_1KHZ };
    execution_prepare_result = false;
    Process( RUN_STATE_REQUEST_EXECUTION );

    EXPECT_EQ( RUN_STATE_FAULT, run_state );
    EXPECT_EQ( RUN_STATE_FAULT_EXECUTION_MANAGER, fault_reason );
    EXPECT_EQ( 0U, driver_start_calls );
    EXPECT_EQ( 0U, timer_start_calls );
}

TEST_F( RunStateManagerTest, ExecutionTimerStartFailureStopsDriversAndEntersFault )
{
    ConfigureToArmed();
    const uint32_t abort_calls_before_execution = execution_abort_calls;
    prepared_execution =
        ( RunStatePreparedExecution_T ){ .tick_count = 10U, .frequency = RUN_STATE_FREQUENCY_1KHZ };
    timer_start_result = false;
    Process( RUN_STATE_REQUEST_EXECUTION );

    RUN_STATE_MANAGER_ProcessPendingOperation();

    EXPECT_EQ( RUN_STATE_FAULT, run_state );
    EXPECT_EQ( RUN_STATE_FAULT_EXECUTION_TIMER, fault_reason );
    EXPECT_FALSE( execution_active );
    EXPECT_FALSE( execution_timer_running );
    EXPECT_EQ( 1U, driver_start_calls );
    EXPECT_EQ( 1U, driver_stop_calls );
    EXPECT_EQ( abort_calls_before_execution + 1U, execution_abort_calls );
    EXPECT_EQ( 1U, timer_start_calls );
}

TEST_F( RunStateManagerTest, ExecutionCompletionFromIsrInhibitsAndNotifiesOwner )
{
    EnterExecution();
    ASSERT_NE( nullptr, execution_terminal_callback );
    notified_bits                         = 0U;
    BaseType_t higher_priority_task_woken = pdFALSE;

    execution_terminal_callback( EXECUTION_MANAGER_TICK_COMPLETE, EXECUTION_MANAGER_FAILURE_NONE,
                                 &higher_priority_task_woken );

    EXPECT_TRUE( RUN_STATE_MANAGER_ExecutionAbortRequestedFromISR() );
    EXPECT_EQ( RUN_STATE_MANAGER_NOTIFY_EXECUTION_COMPLETE, notified_bits );
    EXPECT_EQ( pdTRUE, higher_priority_task_woken );
}

TEST_F( RunStateManagerTest, ExecutionFailureFromIsrInhibitsAndRequestsFault )
{
    EnterExecution();
    ASSERT_NE( nullptr, execution_terminal_callback );
    notified_bits                         = 0U;
    BaseType_t higher_priority_task_woken = pdFALSE;

    execution_terminal_callback( EXECUTION_MANAGER_TICK_FAILED,
                                 EXECUTION_MANAGER_FAILURE_INSTRUCTION_LATE,
                                 &higher_priority_task_woken );

    EXPECT_TRUE( RUN_STATE_MANAGER_ExecutionAbortRequestedFromISR() );
    EXPECT_EQ( RUN_STATE_MANAGER_NOTIFY_FAULT, notified_bits );
    EXPECT_EQ( pdTRUE, higher_priority_task_woken );
    EXPECT_EQ( RUN_STATE_FAULT_EXECUTION_MANAGER, requested_fault_reason );
}

TEST_F( RunStateManagerTest, DriverStartupWaitsForExternalInterfaceCompletion )
{
    ConfigureToArmed();
    driver_start_status = DUT_DRIVER_START_PENDING;
    Process( RUN_STATE_REQUEST_EXECUTION );

    RUN_STATE_MANAGER_ProcessPendingOperation();
    EXPECT_EQ( RUN_STATE_ARMED, run_state );
    EXPECT_EQ( RUN_STATE_PENDING_DRIVER_START, pending_operation );
    EXPECT_FALSE( execution_timer_running );

    driver_start_status = DUT_DRIVER_START_READY;
    RUN_STATE_MANAGER_ProcessPendingOperation();
    EXPECT_EQ( RUN_STATE_EXECUTION, run_state );
    EXPECT_TRUE( execution_timer_running );
}

TEST_F( RunStateManagerTest, DriverStartupFailureEntersFaultBeforeTimerStarts )
{
    ConfigureToArmed();
    driver_start_status = DUT_DRIVER_START_FAILED;
    Process( RUN_STATE_REQUEST_EXECUTION );
    RUN_STATE_MANAGER_ProcessPendingOperation();

    EXPECT_EQ( RUN_STATE_FAULT, run_state );
    EXPECT_EQ( RUN_STATE_FAULT_DRIVER_START, fault_reason );
    EXPECT_EQ( 0U, timer_start_calls );
}

TEST_F( RunStateManagerTest, DriverStartupTimeoutEntersFaultBeforeTimerStarts )
{
    ConfigureToArmed();
    driver_start_status = DUT_DRIVER_START_PENDING;
    Process( RUN_STATE_REQUEST_EXECUTION );
    current_tick += RUN_STATE_MANAGER_DRIVER_START_TIMEOUT_MS;
    RUN_STATE_MANAGER_ProcessPendingOperation();

    EXPECT_EQ( RUN_STATE_FAULT, run_state );
    EXPECT_EQ( RUN_STATE_FAULT_DRIVER_START_TIMEOUT, fault_reason );
    EXPECT_EQ( 0U, timer_start_calls );
}

TEST_F( RunStateManagerTest, ExecutionCompletionStopsDriversAndWaitsForResults )
{
    EnterExecution();
    Process( RUN_STATE_REQUEST_EXECUTION_COMPLETE );
    EXPECT_EQ( RUN_STATE_EXECUTION, run_state );
    EXPECT_FALSE( execution_active );
    EXPECT_FALSE( execution_timer_running );
    EXPECT_EQ( RUN_STATE_PENDING_DRIVER_SHUTDOWN, pending_operation );
    EXPECT_FALSE( driver_shutdown_force );
    RUN_STATE_MANAGER_ProcessPendingOperation();
    EXPECT_EQ( RUN_STATE_RESULT_FINALISATION, run_state );
    EXPECT_EQ( RUN_STATE_PENDING_RESULT_FINALISATION, pending_operation );
    flash_manager_state = FLASH_MANAGER_STATE_RESULTS_READY;
    RUN_STATE_MANAGER_ProcessPendingOperation();
    EXPECT_EQ( RUN_STATE_RESULTS_READY, run_state );
}

TEST_F( RunStateManagerTest, ExecutionCompletionDoesNotFinaliseWhileDriverShutdownIsBusy )
{
    EnterExecution();
    driver_shutdown_status = DUT_DRIVER_SHUTDOWN_PENDING;
    Process( RUN_STATE_REQUEST_EXECUTION_COMPLETE );
    RUN_STATE_MANAGER_ProcessPendingOperation();

    EXPECT_EQ( RUN_STATE_EXECUTION, run_state );
    EXPECT_EQ( RUN_STATE_PENDING_DRIVER_SHUTDOWN, pending_operation );
    EXPECT_FALSE( execution_timer_running );

    driver_shutdown_status = DUT_DRIVER_SHUTDOWN_COMPLETE;
    RUN_STATE_MANAGER_ProcessPendingOperation();
    EXPECT_EQ( RUN_STATE_RESULT_FINALISATION, run_state );
}

TEST_F( RunStateManagerTest, DriverShutdownTimeoutEntersFaultAndForcesAbort )
{
    EnterExecution();
    driver_shutdown_status = DUT_DRIVER_SHUTDOWN_PENDING;
    Process( RUN_STATE_REQUEST_EXECUTION_COMPLETE );
    current_tick += RUN_STATE_MANAGER_DRIVER_SHUTDOWN_TIMEOUT_MS;
    RUN_STATE_MANAGER_ProcessPendingOperation();

    EXPECT_EQ( RUN_STATE_FAULT, run_state );
    EXPECT_EQ( RUN_STATE_FAULT_DRIVER_STOP_TIMEOUT, fault_reason );
    EXPECT_EQ( RUN_STATE_PENDING_FAULT_SHUTDOWN, pending_operation );
    EXPECT_TRUE( driver_shutdown_force );
    EXPECT_TRUE( driver_shutdown_clear_configuration );
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
    run_state                 = RUN_STATE_RESULTS_READY;
    execution_abort_requested = true;
    Process( RUN_STATE_REQUEST_REPEAT );
    EXPECT_EQ( RUN_STATE_CONFIGURATION, run_state );
    EXPECT_EQ( RUN_STATE_PENDING_CONFIGURATION, pending_operation );

    RUN_STATE_MANAGER_ProcessPendingOperation();
    EXPECT_EQ( RUN_STATE_CONFIGURATION, run_state );
    EXPECT_EQ( RUN_STATE_PENDING_EXECUTION_PREPARATION, pending_operation );

    flash_manager_state = FLASH_MANAGER_STATE_EXECUTING;
    RUN_STATE_MANAGER_ProcessPendingOperation();
    EXPECT_EQ( RUN_STATE_ARMED, run_state );
    EXPECT_EQ( RUN_STATE_PENDING_NONE, pending_operation );
    EXPECT_FALSE( configuration_cleared );
    EXPECT_FALSE( execution_abort_requested );
}

TEST_F( RunStateManagerTest, DiscardClearsConfigurationAndReturnsToIdle )
{
    run_state = RUN_STATE_RESULTS_READY;
    Process( RUN_STATE_REQUEST_DISCARD_RESULTS );
    EXPECT_EQ( RUN_STATE_RESULTS_READY, run_state );
    EXPECT_EQ( RUN_STATE_PENDING_IDLE_SHUTDOWN, pending_operation );
    RUN_STATE_MANAGER_ProcessPendingOperation();
    EXPECT_EQ( RUN_STATE_IDLE, run_state );
    EXPECT_TRUE( configuration_cleared );
    EXPECT_TRUE( configuration_ownership_released );
    EXPECT_TRUE( driver_shutdown_clear_configuration );
}

TEST_F( RunStateManagerTest, DiscardFromArmedClearsRetainedTestAndReturnsToIdle )
{
    run_state                 = RUN_STATE_ARMED;
    run_configuration_owned   = true;
    execution_abort_requested = false;

    Process( RUN_STATE_REQUEST_DISCARD_RESULTS );

    EXPECT_EQ( RUN_STATE_ARMED, run_state );
    EXPECT_EQ( RUN_STATE_PENDING_IDLE_SHUTDOWN, pending_operation );
    EXPECT_TRUE( configuration_cleared );
    EXPECT_FALSE( run_configuration_owned );
    EXPECT_TRUE( driver_shutdown_clear_configuration );

    RUN_STATE_MANAGER_ProcessPendingOperation();
    EXPECT_EQ( RUN_STATE_IDLE, run_state );
    EXPECT_FALSE( execution_abort_requested );
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
    EXPECT_EQ( 1U, driver_shutdown_begin_calls );
    EXPECT_TRUE( driver_shutdown_force );
    EXPECT_TRUE( driver_shutdown_clear_configuration );
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

TEST_F( RunStateManagerTest, ResetWaitsForAcknowledgedDriverCleanup )
{
    run_state               = RUN_STATE_FAULT;
    fault_reason            = RUN_STATE_FAULT_EXTERNAL_REQUEST;
    flash_manager_state     = FLASH_MANAGER_STATE_IDLE;
    driver_cleanup_complete = false;

    Process( RUN_STATE_REQUEST_RESET );
    EXPECT_EQ( RUN_STATE_FAULT, run_state );
    EXPECT_EQ( RUN_STATE_REQUEST_RESULT_REJECTED_SUBSYSTEM_STATE, last_request_result );

    driver_cleanup_complete = true;
    Process( RUN_STATE_REQUEST_RESET );
    EXPECT_EQ( RUN_STATE_IDLE, run_state );
}

TEST_F( RunStateManagerTest, FirstFaultReasonWins )
{
    requested_fault_reason = RUN_STATE_FAULT_DRIVER_START;
    Process( RUN_STATE_REQUEST_FAULT );
    RUN_STATE_MANAGER_RecordFault( RUN_STATE_FAULT_FLASH_MANAGER );
    EXPECT_EQ( RUN_STATE_FAULT_DRIVER_START, fault_reason );
}

TEST_F( RunStateManagerTest, RepeatAllowsSubsequentExecutionWithoutFault )
{
    EnterExecution();
    Process( RUN_STATE_REQUEST_EXECUTION_COMPLETE );
    RUN_STATE_MANAGER_ProcessPendingOperation();
    flash_manager_state = FLASH_MANAGER_STATE_RESULTS_READY;
    RUN_STATE_MANAGER_ProcessPendingOperation();
    EXPECT_EQ( RUN_STATE_RESULTS_READY, run_state );
    EXPECT_FALSE( RUN_STATE_MANAGER_ExecutionAbortRequestedFromISR() );

    Process( RUN_STATE_REQUEST_REPEAT );
    EXPECT_EQ( RUN_STATE_CONFIGURATION, run_state );
    EXPECT_FALSE( RUN_STATE_MANAGER_ExecutionAbortRequestedFromISR() );
    RUN_STATE_MANAGER_ProcessPendingOperation();
    flash_manager_state = FLASH_MANAGER_STATE_EXECUTING;
    RUN_STATE_MANAGER_ProcessPendingOperation();
    EXPECT_EQ( RUN_STATE_ARMED, run_state );

    Process( RUN_STATE_REQUEST_EXECUTION );
    RUN_STATE_MANAGER_ProcessPendingOperation();
    EXPECT_EQ( RUN_STATE_EXECUTION, run_state );
    EXPECT_EQ( RUN_STATE_FAULT_NONE, fault_reason );
    EXPECT_FALSE( RUN_STATE_MANAGER_ExecutionAbortRequestedFromISR() );
}

TEST_F( RunStateManagerTest, DiscardResultsAllowsSubsequentPackageReceiveAndExecution )
{
    EnterExecution();
    Process( RUN_STATE_REQUEST_EXECUTION_COMPLETE );
    RUN_STATE_MANAGER_ProcessPendingOperation();
    flash_manager_state = FLASH_MANAGER_STATE_RESULTS_READY;
    RUN_STATE_MANAGER_ProcessPendingOperation();
    EXPECT_EQ( RUN_STATE_RESULTS_READY, run_state );

    Process( RUN_STATE_REQUEST_DISCARD_RESULTS );
    RUN_STATE_MANAGER_ProcessPendingOperation();
    EXPECT_EQ( RUN_STATE_IDLE, run_state );
    EXPECT_FALSE( RUN_STATE_MANAGER_ExecutionAbortRequestedFromISR() );

    active_configuration_available = true;
    EnterExecution();
    EXPECT_EQ( RUN_STATE_EXECUTION, run_state );
    EXPECT_EQ( RUN_STATE_FAULT_NONE, fault_reason );
    EXPECT_FALSE( RUN_STATE_MANAGER_ExecutionAbortRequestedFromISR() );
}

TEST_F( RunStateManagerTest, ExecutionGuardAllowsDispatchOnlyWhenActiveAndNoAbort )
{
    ASSERT_NE( nullptr, execution_guard );
    EXPECT_FALSE( execution_guard() );

    EnterExecution();
    EXPECT_TRUE( execution_guard() );

    EXPECT_TRUE( RUN_STATE_MANAGER_RequestFault( RUN_STATE_FAULT_FLASH_MANAGER ) );
    EXPECT_FALSE( execution_guard() );
}

TEST_F( RunStateManagerTest, ResultTransferCompletionRetainsConfigurationAndRearms )
{
    EnterExecution();
    Process( RUN_STATE_REQUEST_EXECUTION_COMPLETE );
    RUN_STATE_MANAGER_ProcessPendingOperation();
    flash_manager_state = FLASH_MANAGER_STATE_RESULTS_READY;
    RUN_STATE_MANAGER_ProcessPendingOperation();
    EXPECT_EQ( RUN_STATE_RESULTS_READY, run_state );

    Process( RUN_STATE_REQUEST_RESULT_TRANSFER );
    EXPECT_EQ( RUN_STATE_RESULT_TRANSFER, run_state );

    Process( RUN_STATE_REQUEST_RESULT_TRANSFER_COMPLETE );
    EXPECT_EQ( RUN_STATE_CONFIGURATION, run_state );
    EXPECT_EQ( RUN_STATE_PENDING_CONFIGURATION, pending_operation );
    EXPECT_EQ( 1U, flash_transfer_finish_calls );
    EXPECT_TRUE( run_configuration_owned );
    EXPECT_FALSE( configuration_cleared );

    RUN_STATE_MANAGER_ProcessPendingOperation();
    EXPECT_EQ( RUN_STATE_CONFIGURATION, run_state );
    EXPECT_EQ( RUN_STATE_PENDING_EXECUTION_PREPARATION, pending_operation );

    flash_manager_state = FLASH_MANAGER_STATE_EXECUTING;
    RUN_STATE_MANAGER_ProcessPendingOperation();
    EXPECT_EQ( RUN_STATE_ARMED, run_state );
    EXPECT_EQ( RUN_STATE_PENDING_NONE, pending_operation );
    EXPECT_TRUE( run_configuration_owned );
    EXPECT_FALSE( configuration_cleared );
}

TEST_F( RunStateManagerTest, DiscardResultsFailurePreservesResultsReadyAndAllowsRetry )
{
    EnterExecution();
    Process( RUN_STATE_REQUEST_EXECUTION_COMPLETE );
    RUN_STATE_MANAGER_ProcessPendingOperation();
    flash_manager_state = FLASH_MANAGER_STATE_RESULTS_READY;
    RUN_STATE_MANAGER_ProcessPendingOperation();
    EXPECT_EQ( RUN_STATE_RESULTS_READY, run_state );

    /* Fail driver shutdown initiation */
    driver_shutdown_begin_result = false;
    Process( RUN_STATE_REQUEST_DISCARD_RESULTS );

    EXPECT_EQ( RUN_STATE_RESULTS_READY, run_state );
    EXPECT_EQ( RUN_STATE_REQUEST_RESULT_REJECTED_SUBSYSTEM_STATE, last_request_result );
    EXPECT_EQ( 0U, flash_discard_calls );
    EXPECT_FALSE( configuration_cleared );
    EXPECT_TRUE( run_configuration_owned );

    /* Allow shutdown and retry */
    driver_shutdown_begin_result = true;
    Process( RUN_STATE_REQUEST_DISCARD_RESULTS );
    EXPECT_EQ( 1U, flash_discard_calls );
    EXPECT_EQ( RUN_STATE_PENDING_IDLE_SHUTDOWN, pending_operation );
    RUN_STATE_MANAGER_ProcessPendingOperation();
    EXPECT_EQ( RUN_STATE_IDLE, run_state );
    EXPECT_TRUE( configuration_cleared );
    EXPECT_TRUE( configuration_ownership_released );
}

TEST_F( RunStateManagerTest, ResultTransferFinishFailureTransitionsToFault )
{
    EnterExecution();
    Process( RUN_STATE_REQUEST_EXECUTION_COMPLETE );
    RUN_STATE_MANAGER_ProcessPendingOperation();
    flash_manager_state = FLASH_MANAGER_STATE_RESULTS_READY;
    RUN_STATE_MANAGER_ProcessPendingOperation();
    EXPECT_EQ( RUN_STATE_RESULTS_READY, run_state );

    Process( RUN_STATE_REQUEST_RESULT_TRANSFER );
    EXPECT_EQ( RUN_STATE_RESULT_TRANSFER, run_state );

    flash_transfer_finish_result = FLASH_MANAGER_RESULT_TRANSFER_INVALID_STATE;
    Process( RUN_STATE_REQUEST_RESULT_TRANSFER_COMPLETE );

    EXPECT_EQ( RUN_STATE_FAULT, run_state );
    EXPECT_EQ( RUN_STATE_FAULT_FLASH_RESULT_TRANSFER, fault_reason );
    EXPECT_EQ( RUN_STATE_REQUEST_RESULT_FAILED, last_request_result );
    EXPECT_EQ( 1U, flash_transfer_finish_calls );
}

TEST_F( RunStateManagerTest, ResultTransferEntryNotifiesHostInterface )
{
    EnterExecution();
    Process( RUN_STATE_REQUEST_EXECUTION_COMPLETE );
    RUN_STATE_MANAGER_ProcessPendingOperation();
    flash_manager_state = FLASH_MANAGER_STATE_RESULTS_READY;
    RUN_STATE_MANAGER_ProcessPendingOperation();
    EXPECT_EQ( RUN_STATE_RESULTS_READY, run_state );

    host_interface_notify_calls  = 0U;
    host_interface_notified_bits = 0U;
    Process( RUN_STATE_REQUEST_RESULT_TRANSFER );
    EXPECT_EQ( RUN_STATE_RESULT_TRANSFER, run_state );
    EXPECT_EQ( 1U, host_interface_notify_calls );
    EXPECT_EQ( HOST_INTERFACE_NOTIFY_RESULT_TRANSFER, host_interface_notified_bits );
}

TEST_F( RunStateManagerTest, ResultTransferEntryFailureTransitionsToFault )
{
    EnterExecution();
    Process( RUN_STATE_REQUEST_EXECUTION_COMPLETE );
    RUN_STATE_MANAGER_ProcessPendingOperation();
    flash_manager_state = FLASH_MANAGER_STATE_RESULTS_READY;
    RUN_STATE_MANAGER_ProcessPendingOperation();
    EXPECT_EQ( RUN_STATE_RESULTS_READY, run_state );

    host_interface_notify_result = false;
    Process( RUN_STATE_REQUEST_RESULT_TRANSFER );
    EXPECT_EQ( RUN_STATE_FAULT, run_state );
    EXPECT_EQ( RUN_STATE_FAULT_HOST_INTERFACE_ERROR, fault_reason );
}

TEST_F( RunStateManagerTest, FlashSessionAbortedDuringPackageReceiveFault )
{
    Process( RUN_STATE_REQUEST_PACKAGE_RECEIVE );
    flash_manager_state = FLASH_MANAGER_STATE_INSTRUCTION_UPLOAD;
    RUN_STATE_MANAGER_ProcessPendingOperation();
    EXPECT_EQ( RUN_STATE_TEST_PACKAGE_RECEIVE, run_state );

    flash_manager_state    = FLASH_MANAGER_STATE_INSTRUCTION_UPLOAD;
    requested_fault_reason = RUN_STATE_FAULT_EXTERNAL_REQUEST;
    Process( RUN_STATE_REQUEST_FAULT );

    EXPECT_EQ( RUN_STATE_FAULT, run_state );
    EXPECT_EQ( 1U, flash_abort_calls );

    RUN_STATE_MANAGER_ProcessPendingOperation();

    flash_manager_state = FLASH_MANAGER_STATE_ABORTING;
    Process( RUN_STATE_REQUEST_RESET );
    EXPECT_EQ( RUN_STATE_FAULT, run_state );
    EXPECT_EQ( RUN_STATE_REQUEST_RESULT_REJECTED_SUBSYSTEM_STATE, last_request_result );

    flash_manager_state = FLASH_MANAGER_STATE_IDLE;
    Process( RUN_STATE_REQUEST_RESET );
    EXPECT_EQ( RUN_STATE_IDLE, run_state );
}

TEST_F( RunStateManagerTest, FlashSessionAbortedDuringPreparingInstructionUpload )
{
    Process( RUN_STATE_REQUEST_PACKAGE_RECEIVE );
    EXPECT_EQ( RUN_STATE_IDLE, run_state );
    EXPECT_EQ( RUN_STATE_PENDING_INSTRUCTION_UPLOAD_PREPARATION, pending_operation );

    flash_manager_state    = FLASH_MANAGER_STATE_PREPARING_INSTRUCTION_UPLOAD;
    requested_fault_reason = RUN_STATE_FAULT_EXTERNAL_REQUEST;
    Process( RUN_STATE_REQUEST_FAULT );

    EXPECT_EQ( RUN_STATE_FAULT, run_state );
    EXPECT_EQ( 1U, flash_abort_calls );
}

TEST_F( RunStateManagerTest, FlashSessionAbortedDuringExecutionPreparation )
{
    Process( RUN_STATE_REQUEST_PACKAGE_RECEIVE );
    flash_manager_state = FLASH_MANAGER_STATE_INSTRUCTION_UPLOAD;
    RUN_STATE_MANAGER_ProcessPendingOperation();
    Process( RUN_STATE_REQUEST_CONFIGURATION_READY );
    flash_manager_state = FLASH_MANAGER_STATE_IDLE;
    RUN_STATE_MANAGER_ProcessPendingOperation();
    RUN_STATE_MANAGER_ProcessPendingOperation();
    EXPECT_EQ( RUN_STATE_PENDING_EXECUTION_PREPARATION, pending_operation );

    flash_manager_state    = FLASH_MANAGER_STATE_PREPARING_EXECUTION;
    requested_fault_reason = RUN_STATE_FAULT_EXTERNAL_REQUEST;
    Process( RUN_STATE_REQUEST_FAULT );

    EXPECT_EQ( RUN_STATE_FAULT, run_state );
    EXPECT_EQ( 1U, flash_abort_calls );
    EXPECT_EQ( RUN_STATE_PENDING_FAULT_SHUTDOWN, pending_operation );
}

TEST_F( RunStateManagerTest, FlashSessionAbortedDuringResultFinalisation )
{
    EnterExecution();
    Process( RUN_STATE_REQUEST_EXECUTION_COMPLETE );
    EXPECT_EQ( RUN_STATE_PENDING_DRIVER_SHUTDOWN, pending_operation );

    flash_manager_state    = FLASH_MANAGER_STATE_FINALISING_RESULTS;
    requested_fault_reason = RUN_STATE_FAULT_EXTERNAL_REQUEST;
    Process( RUN_STATE_REQUEST_FAULT );

    EXPECT_EQ( RUN_STATE_FAULT, run_state );
    EXPECT_EQ( 1U, flash_abort_calls );
}

TEST_F( RunStateManagerTest, FlashFaultCallbackNotifiesAndTransitionsToFault )
{
    ASSERT_NE( nullptr, flash_fault_callback );
    notified_bits = 0U;

    flash_fault_callback( false );
    EXPECT_NE( 0U, notified_bits & RUN_STATE_MANAGER_NOTIFY_FAULT );
    EXPECT_EQ( RUN_STATE_FAULT_FLASH_MANAGER, requested_fault_reason );

    Process( RUN_STATE_REQUEST_FAULT );
    EXPECT_EQ( RUN_STATE_FAULT, run_state );
    EXPECT_EQ( RUN_STATE_FAULT_FLASH_MANAGER, fault_reason );
}

TEST_F( RunStateManagerTest, RepeatRequestRejectedFromResultTransferState )
{
    EnterExecution();
    Process( RUN_STATE_REQUEST_EXECUTION_COMPLETE );
    RUN_STATE_MANAGER_ProcessPendingOperation();
    flash_manager_state = FLASH_MANAGER_STATE_RESULTS_READY;
    RUN_STATE_MANAGER_ProcessPendingOperation();
    EXPECT_EQ( RUN_STATE_RESULTS_READY, run_state );

    Process( RUN_STATE_REQUEST_RESULT_TRANSFER );
    EXPECT_EQ( RUN_STATE_RESULT_TRANSFER, run_state );

    Process( RUN_STATE_REQUEST_REPEAT );
    EXPECT_EQ( RUN_STATE_RESULT_TRANSFER, run_state );
    EXPECT_EQ( RUN_STATE_REQUEST_RESULT_REJECTED_STATE, last_request_result );
}

TEST_F( RunStateManagerTest, ConfigurationOwnershipHeldAcrossExecutionAndReleasedOnDiscard )
{
    EXPECT_FALSE( run_configuration_owned );
    EXPECT_FALSE( configuration_ownership_released );

    ConfigureToArmed();
    EXPECT_TRUE( run_configuration_owned );
    EXPECT_FALSE( configuration_ownership_released );

    Process( RUN_STATE_REQUEST_EXECUTION );
    RUN_STATE_MANAGER_ProcessPendingOperation();
    EXPECT_EQ( RUN_STATE_EXECUTION, run_state );
    EXPECT_TRUE( run_configuration_owned );

    Process( RUN_STATE_REQUEST_EXECUTION_COMPLETE );
    RUN_STATE_MANAGER_ProcessPendingOperation();
    flash_manager_state = FLASH_MANAGER_STATE_RESULTS_READY;
    RUN_STATE_MANAGER_ProcessPendingOperation();
    EXPECT_EQ( RUN_STATE_RESULTS_READY, run_state );
    EXPECT_TRUE( run_configuration_owned );

    Process( RUN_STATE_REQUEST_DISCARD_RESULTS );
    RUN_STATE_MANAGER_ProcessPendingOperation();
    EXPECT_EQ( RUN_STATE_IDLE, run_state );
    EXPECT_FALSE( run_configuration_owned );
    EXPECT_TRUE( configuration_cleared );
    EXPECT_TRUE( configuration_ownership_released );
}

TEST_F( RunStateManagerTest, PackageReceiveWithoutTicksCalculates128KBReservationAndPrepares )
{
    EXPECT_TRUE( RUN_STATE_MANAGER_RequestPackageReceive() );
    Process( RUN_STATE_REQUEST_PACKAGE_RECEIVE );

    EXPECT_EQ( 1U, flash_upload_start_calls );
    EXPECT_EQ( 128U * 1024U, flash_upload_start_expected_length );
    EXPECT_EQ( RUN_STATE_IDLE, run_state );
    EXPECT_EQ( RUN_STATE_PENDING_INSTRUCTION_UPLOAD_PREPARATION, pending_operation );

    flash_manager_state = FLASH_MANAGER_STATE_INSTRUCTION_UPLOAD;
    RUN_STATE_MANAGER_ProcessPendingOperation();
    EXPECT_EQ( RUN_STATE_TEST_PACKAGE_RECEIVE, run_state );
    EXPECT_EQ( RUN_STATE_PENDING_NONE, pending_operation );
}

TEST_F( RunStateManagerTest, PackageReceiveWithTicksCalculatesConservativeReservation )
{
    EXPECT_TRUE( RUN_STATE_MANAGER_RequestPackageReceiveWithTicks( 100U ) );
    Process( RUN_STATE_REQUEST_PACKAGE_RECEIVE );

    EXPECT_EQ( 1U, flash_upload_start_calls );
    EXPECT_EQ( 100U * 4096U, flash_upload_start_expected_length );
    EXPECT_EQ( RUN_STATE_IDLE, run_state );
    EXPECT_EQ( RUN_STATE_PENDING_INSTRUCTION_UPLOAD_PREPARATION, pending_operation );

    flash_manager_state = FLASH_MANAGER_STATE_INSTRUCTION_UPLOAD;
    RUN_STATE_MANAGER_ProcessPendingOperation();
    EXPECT_EQ( RUN_STATE_TEST_PACKAGE_RECEIVE, run_state );
    EXPECT_EQ( RUN_STATE_PENDING_NONE, pending_operation );
}

TEST_F( RunStateManagerTest, PackageReceiveWithLargeTicksIsCappedByCapacity )
{
    EXPECT_TRUE( RUN_STATE_MANAGER_RequestPackageReceiveWithTicks( 100000U ) );
    Process( RUN_STATE_REQUEST_PACKAGE_RECEIVE );

    EXPECT_EQ( 1U, flash_upload_start_calls );
    EXPECT_EQ( flash_instruction_capacity_bytes, flash_upload_start_expected_length );
}

TEST_F( RunStateManagerTest, PackageReceiveEntersFaultWhenFlashManagerRejectsStart )
{
    flash_upload_start_result = FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_INVALID_STATE;
    EXPECT_TRUE( RUN_STATE_MANAGER_RequestPackageReceive() );
    Process( RUN_STATE_REQUEST_PACKAGE_RECEIVE );

    EXPECT_EQ( RUN_STATE_FAULT, run_state );
    EXPECT_EQ( RUN_STATE_FAULT_FLASH_MANAGER, fault_reason );
}

TEST_F( RunStateManagerTest, PackageReceivePreparationTimeoutEntersFault )
{
    EXPECT_TRUE( RUN_STATE_MANAGER_RequestPackageReceive() );
    Process( RUN_STATE_REQUEST_PACKAGE_RECEIVE );
    EXPECT_EQ( RUN_STATE_IDLE, run_state );
    EXPECT_EQ( RUN_STATE_PENDING_INSTRUCTION_UPLOAD_PREPARATION, pending_operation );

    flash_manager_state = FLASH_MANAGER_STATE_PREPARING_INSTRUCTION_UPLOAD;
    current_tick        = RUN_STATE_MANAGER_INSTRUCTION_UPLOAD_TIMEOUT_MS + 1U;
    RUN_STATE_MANAGER_ProcessPendingOperation();

    EXPECT_EQ( RUN_STATE_FAULT, run_state );
    EXPECT_EQ( RUN_STATE_FAULT_FLASH_MANAGER, fault_reason );
}

TEST_F( RunStateManagerTest, ConfigurationReadyFinalisationTimeoutEntersFault )
{
    EXPECT_TRUE( RUN_STATE_MANAGER_RequestPackageReceive() );
    Process( RUN_STATE_REQUEST_PACKAGE_RECEIVE );
    flash_manager_state = FLASH_MANAGER_STATE_INSTRUCTION_UPLOAD;
    RUN_STATE_MANAGER_ProcessPendingOperation();

    EXPECT_TRUE( RUN_STATE_MANAGER_RequestConfiguration() );
    Process( RUN_STATE_REQUEST_CONFIGURATION_READY );
    EXPECT_EQ( RUN_STATE_PENDING_INSTRUCTION_UPLOAD_FINALISATION, pending_operation );
    EXPECT_EQ( 1U, flash_upload_finish_calls );

    flash_manager_state = FLASH_MANAGER_STATE_FINALISING_INSTRUCTION_UPLOAD;
    current_tick        = RUN_STATE_MANAGER_INSTRUCTION_UPLOAD_TIMEOUT_MS + 1U;
    RUN_STATE_MANAGER_ProcessPendingOperation();

    EXPECT_EQ( RUN_STATE_FAULT, run_state );
    EXPECT_EQ( RUN_STATE_FAULT_FLASH_MANAGER, fault_reason );
}

/**
 * @brief Verifies that entering RUN_STATE_RESULTS_READY notifies Host Interface with
 * HOST_INTERFACE_NOTIFY_EXECUTION_COMPLETE.
 */
TEST_F( RunStateManagerTest, ResultsReadyNotifiesHostInterfaceExecutionComplete )
{
    EnterExecution();
    Process( RUN_STATE_REQUEST_EXECUTION_COMPLETE );
    RUN_STATE_MANAGER_ProcessPendingOperation();
    flash_manager_state = FLASH_MANAGER_STATE_RESULTS_READY;

    host_interface_notified_bits = 0U;
    RUN_STATE_MANAGER_ProcessPendingOperation();
    EXPECT_EQ( RUN_STATE_RESULTS_READY, run_state );
    EXPECT_NE( 0U, host_interface_notified_bits & HOST_INTERFACE_NOTIFY_EXECUTION_COMPLETE );
}

/**
 * @brief Verifies that failure to notify Host Interface upon entering RESULTS_READY
 * causes RSM to transition to RUN_STATE_FAULT with RUN_STATE_FAULT_HOST_INTERFACE_ERROR.
 */
TEST_F( RunStateManagerTest, ResultsReadyNotificationFailureEntersFault )
{
    EnterExecution();
    Process( RUN_STATE_REQUEST_EXECUTION_COMPLETE );
    RUN_STATE_MANAGER_ProcessPendingOperation();
    flash_manager_state = FLASH_MANAGER_STATE_RESULTS_READY;

    host_interface_notify_result = false;
    RUN_STATE_MANAGER_ProcessPendingOperation();
    EXPECT_EQ( RUN_STATE_FAULT, run_state );
    EXPECT_EQ( RUN_STATE_FAULT_HOST_INTERFACE_ERROR, fault_reason );
}

/**
 * @brief Verifies that entering RUN_STATE_FAULT notifies Host Interface with
 * HOST_INTERFACE_NOTIFY_FAULT.
 */
TEST_F( RunStateManagerTest, FaultNotifiesHostInterfaceFault )
{
    host_interface_notified_bits = 0U;
    Process( RUN_STATE_REQUEST_FAULT );
    EXPECT_EQ( RUN_STATE_FAULT, run_state );
    EXPECT_NE( 0U, host_interface_notified_bits & HOST_INTERFACE_NOTIFY_FAULT );
}

