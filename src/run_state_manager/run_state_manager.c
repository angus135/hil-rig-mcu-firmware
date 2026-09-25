/******************************************************************************
 *  File:       run_state_manager.c
 *  Author:     Callum Rafferty
 *  Created:    25-Mar-2026
 *
 *  Description:
 *      Owns the HIL-RIG runtime lifecycle and execution clock.
 *
 *  Notes:
 *      Named task notifications currently expose lifecycle control to the
 *      console and provide integration seams for future subsystem owners.
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */
#include "run_state_manager.h"
#include "dut_driver_lifecycle.h"
#include "exec_analogue_output.h"
#include "exec_can.h"
#include "exec_spi.h"
#include "exec_uart.h"
#include "execution_instruction.h"
#include "execution_manager.h"
#include "flash_manager.h"
#include "hw_timer.h"
#include "logic_expander.h"
#include "rtos_config.h"
#include "test_configuration.h"
#include <stdint.h>
#include <stdbool.h>
#include "hw_can.h"
#include "host_interface.h"

#define RUN_STATE_TAIL_MARGIN_NUMERATOR ( 120U )
#define RUN_STATE_TAIL_MARGIN_DENOMINATOR ( 100U )
#define RUN_STATE_UART_FRAME_BITS ( 10U )
#define RUN_STATE_CAN_FRAME_BITS ( 128U )
#define RUN_STATE_DAC_SPI_BAUD_HZ ( 703125U )

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */
#define RUN_STATE_MANAGER_NOTIFY_PACKAGE_RECEIVE ( 1UL << 0U )
#define RUN_STATE_MANAGER_NOTIFY_CONFIGURATION ( 1UL << 1U )
#define RUN_STATE_MANAGER_NOTIFY_EXECUTION ( 1UL << 2U )
#define RUN_STATE_MANAGER_NOTIFY_EXECUTION_COMPLETE ( 1UL << 3U )
#define RUN_STATE_MANAGER_NOTIFY_RESULT_TRANSFER ( 1UL << 4U )
#define RUN_STATE_MANAGER_NOTIFY_RESULT_TRANSFER_COMPLETE ( 1UL << 5U )
#define RUN_STATE_MANAGER_NOTIFY_FAULT ( 1UL << 6U )
#define RUN_STATE_MANAGER_NOTIFY_RESET ( 1UL << 7U )
#define RUN_STATE_MANAGER_NOTIFY_REPEAT ( 1UL << 8U )
#define RUN_STATE_MANAGER_NOTIFY_DISCARD_RESULTS ( 1UL << 9U )

#define RUN_STATE_MANAGER_PENDING_POLL_MS ( 10U )
#define RUN_STATE_MANAGER_INSTRUCTION_UPLOAD_TIMEOUT_MS ( 15000U )
#define RUN_STATE_MANAGER_CONFIGURATION_TIMEOUT_MS ( 15000U )
#define RUN_STATE_MANAGER_EXECUTION_PREPARATION_TIMEOUT_MS ( 15000U )
#define RUN_STATE_MANAGER_DRIVER_START_TIMEOUT_MS ( 15000U )
#define RUN_STATE_MANAGER_RESULT_FINALISATION_TIMEOUT_MS ( 15000U )
#define RUN_STATE_MANAGER_DRIVER_SHUTDOWN_TIMEOUT_MS ( 15000U )

#define PSC_100HZ 14U
#define ARR_100HZ 59999U

#define PSC_1KHZ 1U
#define ARR_1KHZ 44999U

#define PSC_10KHZ 0U
#define ARR_10KHZ 8999U

/**-----------------------------------------------------------------------------
 *  Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

typedef enum
{
    RUN_STATE_PENDING_NONE = 0,
    RUN_STATE_PENDING_INSTRUCTION_UPLOAD_PREPARATION,
    RUN_STATE_PENDING_INSTRUCTION_UPLOAD_FINALISATION,
    RUN_STATE_PENDING_CONFIGURATION,
    RUN_STATE_PENDING_EXECUTION_PREPARATION,
    RUN_STATE_PENDING_DRIVER_START,
    RUN_STATE_PENDING_DRIVER_SHUTDOWN,
    RUN_STATE_PENDING_IDLE_SHUTDOWN,
    RUN_STATE_PENDING_FAULT_SHUTDOWN,
    RUN_STATE_PENDING_RESULT_FINALISATION
} RunStatePendingOperation_T;

typedef struct
{
    uint32_t                tick_count;
    RunStateFrequencyMode_T frequency;
    bool                    enable_drain_tail;
} RunStatePreparedExecution_T;

/**-----------------------------------------------------------------------------
 *  Public (global) and Extern Variables
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Private (static) Variables
 *------------------------------------------------------------------------------
 */
static volatile RunState_T run_state = RUN_STATE_IDLE;

static RunStateFrequencyMode_T frequency_mode = RUN_STATE_FREQUENCY_1KHZ;

static RunStatePendingOperation_T pending_operation            = RUN_STATE_PENDING_NONE;
static TickType_t                 pending_operation_started_at = 0U;

static bool execution_active        = false;
static bool driver_cleanup_complete = true;

static bool                        execution_timer_running   = false;
static bool                        execution_request_pending = false;
static RunStatePreparedExecution_T prepared_execution        = {
           .tick_count = 0U, .frequency = RUN_STATE_FREQUENCY_1KHZ, .enable_drain_tail = false };

static volatile bool execution_abort_requested = false;

static TaskHandle_t run_state_manager_task_handle = NULL;

static uint32_t package_receive_expected_ticks = 0U;

static volatile RunStateFaultReason_T   fault_reason           = RUN_STATE_FAULT_NONE;
static volatile RunStateFaultReason_T   requested_fault_reason = RUN_STATE_FAULT_NONE;
static volatile RunStateRequest_T       last_request           = RUN_STATE_REQUEST_NONE;
static volatile RunStateRequestResult_T last_request_result    = RUN_STATE_REQUEST_RESULT_NONE;
static DutDriverConfiguration_T         run_configuration;
static bool                             run_configuration_owned = false;

static bool              request_timing_active        = false;
static RunStateRequest_T timed_request                = RUN_STATE_REQUEST_NONE;
static RunState_T        timed_request_target_state   = RUN_STATE_IDLE;
static TickType_t        timed_request_started_at     = 0U;
static bool              last_transition_timing_valid = false;
static RunStateRequest_T last_completed_request       = RUN_STATE_REQUEST_NONE;
static uint32_t          last_transition_duration_ms  = 0U;

/**-----------------------------------------------------------------------------
 *  Private (static) Function Prototypes
 *------------------------------------------------------------------------------
 */
static bool RUN_STATE_MANAGER_Notify( uint32_t notification );
static void RUN_STATE_MANAGER_HandleFlashFault( bool from_isr );
static void
            RUN_STATE_MANAGER_HandleExecutionTerminalFromISR( ExecutionManagerTickResult_T result,
                                                              ExecutionManagerFailure_T    failure,
                                                              BaseType_t* higher_priority_task_woken );
static bool RUN_STATE_MANAGER_RequestFaultFromISRInternal( RunStateFaultReason_T reason,
                                                           BaseType_t* higher_priority_task_woken );
static bool RUN_STATE_MANAGER_ExecutionDispatchAllowedFromISR( void );
static void RUN_STATE_MANAGER_RecordFault( RunStateFaultReason_T reason );
static void RUN_STATE_MANAGER_EnterFault( RunStateFaultReason_T reason );
static void RUN_STATE_MANAGER_CaptureExecutionMetadata( void );

static bool RUN_STATE_MANAGER_IsTransitionAllowed( RunState_T current_state,
                                                   RunState_T next_state );

static bool RUN_STATE_MANAGER_BeginInstructionUpload( uint32_t expected_tick_count );
static bool RUN_STATE_MANAGER_BeginInstructionUploadFinalisation( void );
static bool RUN_STATE_MANAGER_EnterTestPackageReceive( void );
static bool RUN_STATE_MANAGER_EnterConfiguration( void );
static bool RUN_STATE_MANAGER_BeginExecutionPreparation( void );
static bool RUN_STATE_MANAGER_BeginDriverStart( void );
static bool RUN_STATE_MANAGER_EnterExecution( void );
static bool RUN_STATE_MANAGER_StopExecution( void );
static bool RUN_STATE_MANAGER_BeginDriverShutdown( bool force_abort, bool clear_configuration,
                                                   RunStatePendingOperation_T operation );
static bool RUN_STATE_MANAGER_BeginResultFinalisation( void );
static bool RUN_STATE_MANAGER_BeginResultTransfer( void );
static bool RUN_STATE_MANAGER_EnterResultsReady( void );
static bool RUN_STATE_MANAGER_EnterResultTransfer( void );
static bool RUN_STATE_MANAGER_ClearConfigurationAndReturnToIdle( void );
static bool RUN_STATE_MANAGER_CompleteResultTransfer( void );
static bool RUN_STATE_MANAGER_DiscardCompletedResults( RunState_T next_state );
static bool RUN_STATE_MANAGER_FlashIsIdle( void );

static void RUN_STATE_MANAGER_ProcessPendingOperation( void );
static void RUN_STATE_MANAGER_ProcessRequest( RunStateRequest_T request );
static void RUN_STATE_MANAGER_ProcessNotifications( uint32_t notifications );

static bool     RUN_STATE_MANAGER_TransitionTo( RunState_T next_state );
static bool     RUN_STATE_MANAGER_StartExecutionTimer( void );
static void     RUN_STATE_MANAGER_StopExecutionTimer( void );
static void     RUN_STATE_MANAGER_StartPendingOperation( RunStatePendingOperation_T operation );
static void     RUN_STATE_MANAGER_ClearPendingOperation( void );
static bool     RUN_STATE_MANAGER_PendingOperationTimedOut( TickType_t timeout_ticks );
static bool     RUN_STATE_MANAGER_GetRequestTargetState( RunStateRequest_T request,
                                                         RunState_T*       target_state );
static void     RUN_STATE_MANAGER_StartRequestTiming( RunStateRequest_T request );
static void     RUN_STATE_MANAGER_CompleteRequestTimingIfReady( void );
static uint32_t RUN_STATE_MANAGER_TicksToMilliseconds( TickType_t ticks );

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

/**
 * @brief Sends a notification to the Run State Manager task.
 *
 * @param notification Notification bit to set.
 *
 * @returns true if the notification was delivered, otherwise false.
 */
static bool RUN_STATE_MANAGER_Notify( uint32_t notification )
{
    if ( run_state_manager_task_handle == NULL )
    {
        return false;
    }

    taskENTER_CRITICAL();
    last_request_result = RUN_STATE_REQUEST_RESULT_NONE;
    taskEXIT_CRITICAL();

    return xTaskNotify( run_state_manager_task_handle, notification, eSetBits ) == pdPASS;
}

static void RUN_STATE_MANAGER_HandleFlashFault( bool from_isr )
{
    if ( from_isr )
    {
        ( void )RUN_STATE_MANAGER_RequestFaultFromISR( RUN_STATE_FAULT_FLASH_MANAGER );
    }
    else
    {
        ( void )RUN_STATE_MANAGER_RequestFault( RUN_STATE_FAULT_FLASH_MANAGER );
    }
}

static void
RUN_STATE_MANAGER_HandleExecutionTerminalFromISR( ExecutionManagerTickResult_T result,
                                                  ExecutionManagerFailure_T    failure,
                                                  BaseType_t* higher_priority_task_woken )
{
    execution_abort_requested = true;

    if ( result == EXECUTION_MANAGER_TICK_COMPLETE )
    {
        ( void )RUN_METADATA_LatchTerminal( RUN_METADATA_TERMINAL_COMPLETE,
                                            RUN_METADATA_FAILURE_SOURCE_NONE, 0U );
        if ( run_state_manager_task_handle != NULL )
        {
            ( void )xTaskNotifyFromISR( run_state_manager_task_handle,
                                        RUN_STATE_MANAGER_NOTIFY_EXECUTION_COMPLETE, eSetBits,
                                        higher_priority_task_woken );
        }
    }
    else
    {
        ( void )RUN_METADATA_LatchTerminal( RUN_METADATA_TERMINAL_FAILED,
                                            RUN_METADATA_FAILURE_SOURCE_EXECUTION_MANAGER,
                                            ( uint32_t )failure );
        ( void )RUN_STATE_MANAGER_RequestFaultFromISRInternal( RUN_STATE_FAULT_EXECUTION_MANAGER,
                                                               higher_priority_task_woken );
    }
}

static bool RUN_STATE_MANAGER_ExecutionDispatchAllowedFromISR( void )
{
    return execution_active && !execution_abort_requested;
}

static void RUN_STATE_MANAGER_StartPendingOperation( RunStatePendingOperation_T operation )
{
    pending_operation            = operation;
    pending_operation_started_at = xTaskGetTickCount();
}

static void RUN_STATE_MANAGER_ClearPendingOperation( void )
{
    pending_operation            = RUN_STATE_PENDING_NONE;
    pending_operation_started_at = 0U;
}

static bool RUN_STATE_MANAGER_PendingOperationTimedOut( TickType_t timeout_ticks )
{
    const TickType_t elapsed = ( TickType_t )( xTaskGetTickCount() - pending_operation_started_at );
    return elapsed >= timeout_ticks;
}

static uint32_t RUN_STATE_MANAGER_TicksToMilliseconds( TickType_t ticks )
{
#ifdef TEST_BUILD
    return ( uint32_t )ticks;
#else
    return ( uint32_t )( ( ( uint64_t )ticks * 1000ULL ) / ( uint64_t )configTICK_RATE_HZ );
#endif
}

/** Returns the externally meaningful terminal state for a named request. */
static bool RUN_STATE_MANAGER_GetRequestTargetState( RunStateRequest_T request,
                                                     RunState_T*       target_state )
{
    if ( target_state == NULL )
    {
        return false;
    }

    switch ( request )
    {
        case RUN_STATE_REQUEST_PACKAGE_RECEIVE:
            *target_state = RUN_STATE_TEST_PACKAGE_RECEIVE;
            return true;
        case RUN_STATE_REQUEST_CONFIGURATION_READY:
            *target_state = RUN_STATE_ARMED;
            return true;
        case RUN_STATE_REQUEST_EXECUTION:
            *target_state = RUN_STATE_EXECUTION;
            return true;
        case RUN_STATE_REQUEST_EXECUTION_COMPLETE:
            *target_state = RUN_STATE_RESULTS_READY;
            return true;
        case RUN_STATE_REQUEST_RESULT_TRANSFER:
            *target_state = RUN_STATE_RESULT_TRANSFER;
            return true;
        case RUN_STATE_REQUEST_RESULT_TRANSFER_COMPLETE:
            *target_state = RUN_STATE_ARMED;
            return true;
        case RUN_STATE_REQUEST_DISCARD_RESULTS:
        case RUN_STATE_REQUEST_RESET:
            *target_state = RUN_STATE_IDLE;
            return true;
        case RUN_STATE_REQUEST_REPEAT:
            *target_state = RUN_STATE_ARMED;
            return true;
        case RUN_STATE_REQUEST_NONE:
        case RUN_STATE_REQUEST_FAULT:
        default:
            return false;
    }
}

static void RUN_STATE_MANAGER_CompleteRequestTimingIfReady( void )
{
    if ( !request_timing_active || run_state != timed_request_target_state )
    {
        return;
    }

    const TickType_t elapsed     = ( TickType_t )( xTaskGetTickCount() - timed_request_started_at );
    last_completed_request       = timed_request;
    last_transition_duration_ms  = RUN_STATE_MANAGER_TicksToMilliseconds( elapsed );
    last_transition_timing_valid = true;
    request_timing_active        = false;
}

static void RUN_STATE_MANAGER_StartRequestTiming( RunStateRequest_T request )
{
    RunState_T target_state = RUN_STATE_IDLE;
    if ( !RUN_STATE_MANAGER_GetRequestTargetState( request, &target_state ) )
    {
        return;
    }

    request_timing_active      = true;
    timed_request              = request;
    timed_request_target_state = target_state;
    timed_request_started_at   = xTaskGetTickCount();
    RUN_STATE_MANAGER_CompleteRequestTimingIfReady();
}

static void RUN_STATE_MANAGER_RecordFault( RunStateFaultReason_T reason )
{
    if ( fault_reason == RUN_STATE_FAULT_NONE && reason != RUN_STATE_FAULT_NONE )
    {
        fault_reason = reason;
    }
}

static void RUN_STATE_MANAGER_CaptureExecutionMetadata( void )
{
    RunMetadataExecutionCapture_T capture = { 0 };

    uint32_t boundary = 0U;
    if ( EXECUTION_MANAGER_GetLastCompletedBoundary( &boundary ) )
    {
        capture.valid_sections |= RUN_METADATA_VALID_LAST_COMPLETED_BOUNDARY;
        capture.last_completed_boundary = boundary;
    }

    HW_TIMER_ExecutionTiming_T timer_timing = { 0 };
    HW_TIMER_Get_Execution_Timing( &timer_timing );
    if ( timer_timing.sample_count > 0U )
    {
        capture.valid_sections |= RUN_METADATA_VALID_ISR_TIMING;
        capture.isr_timing.sample_count     = timer_timing.sample_count;
        capture.isr_timing.total_cycles     = timer_timing.total_cycles;
        capture.isr_timing.minimum_cycles   = timer_timing.minimum_cycles;
        capture.isr_timing.maximum_cycles   = timer_timing.maximum_cycles;
        capture.isr_timing.maximum_boundary = timer_timing.max_sample_number;
    }

    FlashManagerExecutionDiagnostics_T flash_diag = { 0 };
    if ( FLASH_MANAGER_GetExecutionDiagnostics( &flash_diag ) )
    {
        capture.valid_sections |= RUN_METADATA_VALID_INSTRUCTION_BUFFER;
        capture.instruction_buffer.sample_count = flash_diag.instruction_occupancy_samples;
        capture.instruction_buffer.minimum_unread_bytes =
            flash_diag.minimum_unread_instruction_bytes;
        capture.instruction_buffer.minimum_boundary =
            flash_diag.minimum_unread_instruction_boundary;

        capture.valid_sections |= RUN_METADATA_VALID_RESULT_BUFFER;
        capture.result_buffer.committed_record_count = flash_diag.committed_result_records;
        capture.result_buffer.committed_bytes        = flash_diag.committed_result_bytes;
        capture.result_buffer.peak_pending_bytes     = flash_diag.peak_pending_result_bytes;
        capture.result_buffer.peak_pending_boundary  = flash_diag.peak_pending_result_boundary;
        capture.result_buffer.reserve_failure_count  = flash_diag.result_reserve_failures;
        capture.result_buffer.commit_failure_count   = flash_diag.result_commit_failures;

        capture.valid_sections |= RUN_METADATA_VALID_FLASH_THROUGHPUT;
        capture.flash_throughput.result_pages_drained = flash_diag.result_pages_drained;
        capture.flash_throughput.result_bytes_drained = flash_diag.result_bytes_drained;
        capture.flash_throughput.result_drain_total_cycles =
            flash_diag.result_page_drain_total_cycles;
        capture.flash_throughput.result_drain_maximum_cycles =
            flash_diag.result_page_drain_max_cycles;
        capture.flash_throughput.instruction_pages_refilled = flash_diag.instruction_pages_refilled;
        capture.flash_throughput.instruction_bytes_refilled = flash_diag.instruction_bytes_refilled;
        capture.flash_throughput.instruction_refill_total_cycles =
            flash_diag.instruction_page_refill_total_cycles;
        capture.flash_throughput.instruction_refill_maximum_cycles =
            flash_diag.instruction_page_refill_max_cycles;
        capture.flash_throughput.instruction_publish_sample_count =
            flash_diag.instruction_page_publish_samples;
        capture.flash_throughput.instruction_publish_total_cycles =
            flash_diag.instruction_page_publish_total_cycles;
        capture.flash_throughput.instruction_publish_maximum_cycles =
            flash_diag.instruction_page_publish_max_cycles;
        capture.flash_throughput.service_gap_sample_count = flash_diag.nand_service_gap_samples;
        capture.flash_throughput.service_gap_total_cycles =
            flash_diag.nand_service_gap_total_cycles;
        capture.flash_throughput.service_gap_maximum_cycles =
            flash_diag.nand_service_gap_max_cycles;
        capture.flash_throughput.refill_drain_contention_count =
            flash_diag.refill_drain_contentions;
    }

    ( void )RUN_METADATA_CaptureExecution( &capture );
}

static void RUN_STATE_MANAGER_EnterFault( RunStateFaultReason_T reason )
{
    request_timing_active     = false;
    execution_abort_requested = true;
    taskENTER_CRITICAL();
    execution_request_pending = false;
    taskEXIT_CRITICAL();
    RUN_STATE_MANAGER_RecordFault( reason );

    if ( reason == RUN_STATE_FAULT_EXTERNAL_REQUEST )
    {
        ( void )RUN_METADATA_LatchTerminal( RUN_METADATA_TERMINAL_ABORTED,
                                            RUN_METADATA_FAILURE_SOURCE_RUN_STATE_MANAGER,
                                            ( uint32_t )reason );
    }
    else if ( ( reason == RUN_STATE_FAULT_FLASH_MANAGER )
              || ( reason == RUN_STATE_FAULT_FLASH_EXECUTION_PREPARATION )
              || ( reason == RUN_STATE_FAULT_FLASH_EXECUTION_PREPARATION_TIMEOUT )
              || ( reason == RUN_STATE_FAULT_FLASH_RESULT_FINALISATION )
              || ( reason == RUN_STATE_FAULT_FLASH_RESULT_FINALISATION_TIMEOUT )
              || ( reason == RUN_STATE_FAULT_FLASH_RESULT_TRANSFER )
              || ( reason == RUN_STATE_FAULT_FLASH_RESULT_DISPOSITION ) )
    {
        ( void )RUN_METADATA_LatchTerminal( RUN_METADATA_TERMINAL_FAILED,
                                            RUN_METADATA_FAILURE_SOURCE_FLASH_MANAGER,
                                            ( uint32_t )reason );
    }
    else if ( ( reason == RUN_STATE_FAULT_HOST_INTERFACE_RESPONSE_BLOCKED )
              || ( reason == RUN_STATE_FAULT_HOST_INTERFACE_USB_INIT )
              || ( reason == RUN_STATE_FAULT_HOST_INTERFACE_CODEC_INIT )
              || ( reason == RUN_STATE_FAULT_HOST_INTERFACE_TRANSPORT_INIT )
              || ( reason == RUN_STATE_FAULT_HOST_INTERFACE_ERROR ) )
    {
        ( void )RUN_METADATA_LatchTerminal( RUN_METADATA_TERMINAL_FAILED,
                                            RUN_METADATA_FAILURE_SOURCE_HOST_INTERFACE,
                                            ( uint32_t )reason );
    }
    else if ( reason == RUN_STATE_FAULT_EXECUTION_MANAGER )
    {
        ( void )RUN_METADATA_LatchTerminal( RUN_METADATA_TERMINAL_FAILED,
                                            RUN_METADATA_FAILURE_SOURCE_EXECUTION_MANAGER,
                                            ( uint32_t )EXECUTION_MANAGER_GetFailure() );
    }
    else if ( reason != RUN_STATE_FAULT_NONE )
    {
        ( void )RUN_METADATA_LatchTerminal( RUN_METADATA_TERMINAL_FAILED,
                                            RUN_METADATA_FAILURE_SOURCE_RUN_STATE_MANAGER,
                                            ( uint32_t )reason );
    }

    RUN_STATE_MANAGER_CaptureExecutionMetadata();
    ( void )RUN_METADATA_SetResultStreamStatus( RUN_METADATA_RESULT_STREAM_UNAVAILABLE );
    ( void )RUN_METADATA_Seal();

    ( void )RUN_STATE_MANAGER_TransitionTo( RUN_STATE_FAULT );
    ( void )HOST_INTERFACE_Notify( HOST_INTERFACE_NOTIFY_FAULT );
}

/**
 * @brief Determines whether a lifecycle transition is currently supported.
 *
 * Fault is reachable from every state. Re-entering the current state is
 * treated as an idempotent successful transition.
 */
static bool RUN_STATE_MANAGER_IsTransitionAllowed( RunState_T current_state, RunState_T next_state )
{
    if ( current_state == next_state )
    {
        return true;
    }

    if ( next_state == RUN_STATE_FAULT )
    {
        return true;
    }

    switch ( current_state )
    {
        case RUN_STATE_IDLE:
            return next_state == RUN_STATE_TEST_PACKAGE_RECEIVE;

        case RUN_STATE_TEST_PACKAGE_RECEIVE:
            return next_state == RUN_STATE_CONFIGURATION || next_state == RUN_STATE_IDLE;

        case RUN_STATE_CONFIGURATION:
            return next_state == RUN_STATE_ARMED || next_state == RUN_STATE_IDLE;

        case RUN_STATE_ARMED:
            return next_state == RUN_STATE_EXECUTION || next_state == RUN_STATE_CONFIGURATION
                   || next_state == RUN_STATE_IDLE;

        case RUN_STATE_EXECUTION:
            return next_state == RUN_STATE_ARMED || next_state == RUN_STATE_RESULT_FINALISATION;

        case RUN_STATE_RESULT_FINALISATION:
            return next_state == RUN_STATE_RESULTS_READY;

        case RUN_STATE_RESULTS_READY:
            return next_state == RUN_STATE_RESULT_TRANSFER || next_state == RUN_STATE_CONFIGURATION
                   || next_state == RUN_STATE_ARMED || next_state == RUN_STATE_IDLE;

        case RUN_STATE_RESULT_TRANSFER:
            return next_state == RUN_STATE_CONFIGURATION;

        case RUN_STATE_FAULT:
            return next_state == RUN_STATE_IDLE;

        default:
            return false;
    }
}

/**
 * @brief Begins instruction upload preparation in Flash Manager.
 *
 * Sizing is conservatively estimated using the expected ticks transmitted
 * in the configuration message and the maximum instruction size, reserving
 * at least one physical NAND block (128 KB) and bounded by partition capacity.
 *
 * @return true when Flash Manager accepts the request or is already in upload state;
 *         false if in an invalid state or request fails.
 */
static bool RUN_STATE_MANAGER_BeginInstructionUpload( uint32_t expected_tick_count )
{
    FlashManagerState_T flash_state = FLASH_MANAGER_STATE_UNINITIALISED;
    if ( !FLASH_MANAGER_GetState( &flash_state ) )
    {
        RUN_STATE_MANAGER_EnterFault( RUN_STATE_FAULT_FLASH_MANAGER );
        return false;
    }

    /* Re-entrant / already ready case (e.g. console command) */
    if ( flash_state == FLASH_MANAGER_STATE_INSTRUCTION_UPLOAD )
    {
        return RUN_STATE_MANAGER_TransitionTo( RUN_STATE_TEST_PACKAGE_RECEIVE );
    }

    if ( flash_state != FLASH_MANAGER_STATE_IDLE )
    {
        RUN_STATE_MANAGER_EnterFault( RUN_STATE_FAULT_FLASH_MANAGER );
        return false;
    }

    /* Conservative estimate: minimum 1 physical NAND block (128 KB) */
    uint32_t upload_estimate = 128U * 1024U;

    if ( expected_tick_count > 0U )
    {
        const uint64_t calculated =
            ( uint64_t )expected_tick_count * ( uint64_t )EXECUTION_INSTRUCTION_MAX_SIZE_BYTES;

        uint32_t instruction_capacity = 0U;
        if ( FLASH_MANAGER_GetInstructionCapacityBytes( &instruction_capacity )
             && ( instruction_capacity > 0U ) )
        {
            if ( calculated > ( uint64_t )instruction_capacity )
            {
                upload_estimate = instruction_capacity;
            }
            else if ( calculated > ( uint64_t )upload_estimate )
            {
                upload_estimate = ( uint32_t )calculated;
            }
        }
        else
        {
            const uint32_t fallback_capacity = 64U * 1024U * 1024U;
            if ( calculated > ( uint64_t )fallback_capacity )
            {
                upload_estimate = fallback_capacity;
            }
            else if ( calculated > ( uint64_t )upload_estimate )
            {
                upload_estimate = ( uint32_t )calculated;
            }
        }
    }

    const FlashManagerInstructionUploadRequestStatus_T status =
        FLASH_MANAGER_RequestInstructionUploadStart( upload_estimate );

    if ( status == FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_ACCEPTED )
    {
        RUN_STATE_MANAGER_StartPendingOperation( RUN_STATE_PENDING_INSTRUCTION_UPLOAD_PREPARATION );
        return true;
    }

    RUN_STATE_MANAGER_EnterFault( RUN_STATE_FAULT_FLASH_MANAGER );
    return false;
}

/**
 * @brief Begins finalisation of the uploaded instruction stream.
 *
 * @return true when finalisation is requested or Flash Manager is already idle;
 *         false if in an invalid state or request fails.
 */
static bool RUN_STATE_MANAGER_BeginInstructionUploadFinalisation( void )
{
    FlashManagerState_T flash_state = FLASH_MANAGER_STATE_UNINITIALISED;
    if ( !FLASH_MANAGER_GetState( &flash_state ) )
    {
        RUN_STATE_MANAGER_EnterFault( RUN_STATE_FAULT_FLASH_MANAGER );
        return false;
    }

    /* If already IDLE (no instructions uploaded or test harness), proceed to configuration */
    if ( flash_state == FLASH_MANAGER_STATE_IDLE )
    {
        if ( RUN_STATE_MANAGER_TransitionTo( RUN_STATE_CONFIGURATION ) )
        {
            RUN_STATE_MANAGER_StartPendingOperation( RUN_STATE_PENDING_CONFIGURATION );
            return true;
        }
        return false;
    }

    if ( flash_state == FLASH_MANAGER_STATE_INSTRUCTION_UPLOAD )
    {
        const FlashManagerInstructionUploadRequestStatus_T status =
            FLASH_MANAGER_RequestInstructionUploadFinish();
        if ( status == FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_ACCEPTED )
        {
            RUN_STATE_MANAGER_StartPendingOperation(
                RUN_STATE_PENDING_INSTRUCTION_UPLOAD_FINALISATION );
            return true;
        }
    }

    RUN_STATE_MANAGER_EnterFault( RUN_STATE_FAULT_FLASH_MANAGER );
    return false;
}

static bool RUN_STATE_MANAGER_EnterTestPackageReceive( void )
{
    /*
     * Future responsibility:
     * Tell Host Interface to accept a new test package.
     *
     * Until that interface exists, entering the state is sufficient.
     */
    return true;
}

/**
 * @brief Performs entry actions when transitioning into RUN_STATE_RESULTS_READY.
 *
 * @return true if the Host Interface was notified successfully; otherwise false.
 */
static bool RUN_STATE_MANAGER_EnterResultsReady( void )
{
    return HOST_INTERFACE_Notify( HOST_INTERFACE_NOTIFY_EXECUTION_COMPLETE );
}

/**
 * @brief Performs entry actions when transitioning into RUN_STATE_RESULT_TRANSFER.
 *
 * @return true if the Host Interface was notified successfully; otherwise false.
 */
static bool RUN_STATE_MANAGER_EnterResultTransfer( void )
{
    return HOST_INTERFACE_Notify( HOST_INTERFACE_NOTIFY_RESULT_TRANSFER );
}

/**
 * @brief Applies the active test configuration before execution preparation.
 *
 * @return true when every configuration action succeeds; otherwise false.
 *
 * @note This function runs in Run State Manager task context.
 * @note All configured DUT-facing drivers must remain stopped on return.
 */
static bool RUN_STATE_MANAGER_EnterConfiguration( void )
{
    DutDriverConfiguration_T configuration = { 0 };

    if ( !LOGIC_EXPANDER_Is_Ready() )
    {
        RUN_STATE_MANAGER_RecordFault( RUN_STATE_FAULT_LOGIC_EXPANDER_NOT_READY );
        return false;
    }

    if ( !run_configuration_owned && !TEST_CONFIGURATION_AcquireForRun( &run_configuration ) )
    {
        RUN_STATE_MANAGER_RecordFault( RUN_STATE_FAULT_CONFIGURATION_UNAVAILABLE );
        return false;
    }
    run_configuration_owned = true;
    configuration           = run_configuration;

    if ( !DUT_DRIVER_LIFECYCLE_Configure( &configuration ) )
    {
        RUN_STATE_MANAGER_RecordFault( RUN_STATE_FAULT_DRIVER_CONFIGURATION );
        return false;
    }

    return true;
}

static bool RUN_STATE_MANAGER_EnterArmed( void )
{
    return HOST_INTERFACE_Notify( HOST_INTERFACE_NOTIFY_ARMED );
}

/**
 * @brief Enters production execution.
 *
 * Starts the execution clock only after asynchronous driver startup has been
 * confirmed by the pending-operation state machine.
 *
 * @return true when the DUT drivers and execution clock are active;
 *         otherwise false.
 */
static bool RUN_STATE_MANAGER_EnterExecution( void )
{
    if ( execution_abort_requested || requested_fault_reason != RUN_STATE_FAULT_NONE )
    {
        RUN_STATE_MANAGER_EnterFault( ( requested_fault_reason != RUN_STATE_FAULT_NONE )
                                          ? requested_fault_reason
                                          : RUN_STATE_FAULT_EXTERNAL_REQUEST );
        return false;
    }

    if ( execution_active )
    {
        return true;
    }

    if ( !DUT_DRIVER_LIFECYCLE_EstablishExecutionEpoch() )
    {
        EXECUTION_MANAGER_Abort();
        RUN_STATE_MANAGER_EnterFault( RUN_STATE_FAULT_ACQUISITION_EPOCH );
        return false;
    }

    if ( execution_abort_requested || requested_fault_reason != RUN_STATE_FAULT_NONE )
    {
        EXECUTION_MANAGER_Abort();
        RUN_STATE_MANAGER_EnterFault( ( requested_fault_reason != RUN_STATE_FAULT_NONE )
                                          ? requested_fault_reason
                                          : RUN_STATE_FAULT_EXTERNAL_REQUEST );
        return false;
    }

    if ( !RUN_STATE_MANAGER_StartExecutionTimer() )
    {
        EXECUTION_MANAGER_Abort();
        ( void )DUT_DRIVER_LIFECYCLE_Stop();
        RUN_STATE_MANAGER_EnterFault( RUN_STATE_FAULT_EXECUTION_TIMER );
        return false;
    }

    taskENTER_CRITICAL();
    execution_active          = true;
    execution_request_pending = false;
    taskEXIT_CRITICAL();
    return true;
}

/** Starts DUT drivers and waits separately for external-interface completion. */
static uint32_t RUN_STATE_MANAGER_GetFrequencyHz( RunStateFrequencyMode_T frequency )
{
    return frequency == RUN_STATE_FREQUENCY_100HZ   ? 100U
           : frequency == RUN_STATE_FREQUENCY_10KHZ ? 10000U
                                                    : 1000U;
}

static uint32_t RUN_STATE_MANAGER_GetSpiBaudHz( ExecSPIBaudRate_T baud_rate )
{
    static const uint32_t spi_baud_hz[EXEC_SPI_BAUD_COUNT] = {
        [EXEC_SPI_BAUD_45MBIT] = 45000000U,   [EXEC_SPI_BAUD_22M5BIT] = 22500000U,
        [EXEC_SPI_BAUD_11M25BIT] = 11250000U, [EXEC_SPI_BAUD_5M625BIT] = 5625000U,
        [EXEC_SPI_BAUD_2M813BIT] = 2813000U,  [EXEC_SPI_BAUD_1M406BIT] = 1406000U,
        [EXEC_SPI_BAUD_703KBIT] = 703000U,    [EXEC_SPI_BAUD_352KBIT] = 352000U,
    };

    return spi_baud_hz[baud_rate];
}

static uint32_t RUN_STATE_MANAGER_CalculateDrainTailTicks( RunStateFrequencyMode_T frequency )
{
    uint64_t       required_ticks = 0U;
    const uint32_t frequency_hz   = RUN_STATE_MANAGER_GetFrequencyHz( frequency );

    for ( uint32_t channel = 0U; channel < EXEC_UART_CHANNEL_COUNT; channel++ )
    {
        const ExecUartConfig_T* uart = &run_configuration.uart_channels[channel];
        if ( !uart->is_enabled || !uart->tx_enabled || uart->baud_rate == 0U )
        {
            continue;
        }

        const uint64_t numerator = ( uint64_t )EXEC_UART_MAX_CHUNK_SIZE * RUN_STATE_UART_FRAME_BITS
                                   * frequency_hz * RUN_STATE_TAIL_MARGIN_NUMERATOR;
        const uint64_t denominator =
            ( uint64_t )uart->baud_rate * RUN_STATE_TAIL_MARGIN_DENOMINATOR;
        const uint64_t ticks = ( numerator + denominator - 1U ) / denominator;
        if ( ticks > required_ticks )
        {
            required_ticks = ticks;
        }
    }

    for ( uint32_t channel = 0U; channel < EXEC_SPI_CHANNEL_COUNT; channel++ )
    {
        const ExecSPIConfig_T* spi = &run_configuration.spi_channels[channel];
        if ( !spi->is_enabled )
        {
            continue;
        }

        const uint32_t baud_hz   = RUN_STATE_MANAGER_GetSpiBaudHz( spi->baud_rate );
        const uint64_t numerator = ( uint64_t )EXEC_SPI_MAX_RX_CHUNK_SIZE * 8U * frequency_hz
                                   * RUN_STATE_TAIL_MARGIN_NUMERATOR;
        const uint64_t denominator = ( uint64_t )baud_hz * RUN_STATE_TAIL_MARGIN_DENOMINATOR;
        const uint64_t ticks       = ( numerator + denominator - 1U ) / denominator;
        if ( ticks > required_ticks )
        {
            required_ticks = ticks;
        }
    }

    for ( uint32_t channel = 0U; channel < EXEC_CAN_CHANNEL_COUNT; channel++ )
    {
        const EXEC_CAN_Config_T* can = &run_configuration.can_channels[channel];
        if ( !can->is_enabled || can->bitrate == 0U )
        {
            continue;
        }

        const uint64_t numerator = ( uint64_t )EXEC_CAN_MAX_BATCH_SIZE * RUN_STATE_CAN_FRAME_BITS
                                   * frequency_hz * RUN_STATE_TAIL_MARGIN_NUMERATOR;
        const uint64_t denominator = ( uint64_t )can->bitrate * RUN_STATE_TAIL_MARGIN_DENOMINATOR;
        const uint64_t ticks       = ( numerator + denominator - 1U ) / denominator;
        if ( ticks > required_ticks )
        {
            required_ticks = ticks;
        }
    }

    if ( run_configuration.analogue_output.is_enabled )
    {
        const uint64_t numerator = ( uint64_t )EXEC_ANALOGUE_OUTPUT_BATCH_MAX_BYTES * 8U
                                   * frequency_hz * RUN_STATE_TAIL_MARGIN_NUMERATOR;
        const uint64_t denominator =
            ( uint64_t )RUN_STATE_DAC_SPI_BAUD_HZ * RUN_STATE_TAIL_MARGIN_DENOMINATOR;
        const uint64_t ticks = ( numerator + denominator - 1U ) / denominator;
        if ( ticks > required_ticks )
        {
            required_ticks = ticks;
        }
    }

    return required_ticks > UINT32_MAX ? UINT32_MAX : ( uint32_t )required_ticks;
}

static bool RUN_STATE_MANAGER_BeginDriverStart( void )
{
    if ( execution_abort_requested || requested_fault_reason != RUN_STATE_FAULT_NONE )
    {
        RUN_STATE_MANAGER_EnterFault( ( requested_fault_reason != RUN_STATE_FAULT_NONE )
                                          ? requested_fault_reason
                                          : RUN_STATE_FAULT_EXTERNAL_REQUEST );
        return false;
    }

    driver_cleanup_complete = false;

    DutDriverLifecycleStatus_T driver_status = { 0 };
    DUT_DRIVER_LIFECYCLE_GetStatus( &driver_status );

    /* Legacy console sessions may request the historical peripheral drain tail.
     * Variable-message sessions keep the execution range exactly host-defined. */
    const uint32_t execution_tail_ticks =
        prepared_execution.enable_drain_tail
            ? RUN_STATE_MANAGER_CalculateDrainTailTicks( prepared_execution.frequency )
            : 0U;

    uint32_t effective_tick_count = prepared_execution.tick_count;
    if ( execution_tail_ticks > ( UINT32_MAX - effective_tick_count ) )
    {
        RUN_STATE_MANAGER_EnterFault( RUN_STATE_FAULT_EXECUTION_MANAGER );
        return false;
    }
    effective_tick_count += execution_tail_ticks;

    const ExecutionMeasurementConfiguration_T measurement_configuration = {
        .analogue_input_enabled    = driver_status.analogue_input_enabled,
        .digital_input_enabled     = driver_status.digital_inputs_enabled,
        .pwm_capture_enabled_mask  = driver_status.pwm_capture_enabled_mask,
        .uart_receive_enabled_mask = driver_status.uart_receive_enabled_mask,
        .spi_receive_enabled_mask  = driver_status.spi_enabled_mask,
        .can_receive_enabled_mask  = driver_status.can_enabled_mask,
    };
    EXECUTION_MANAGER_ConfigureMeasurements( &measurement_configuration );

    if ( !EXECUTION_MANAGER_Prepare( effective_tick_count ) )
    {
        RUN_STATE_MANAGER_EnterFault( RUN_STATE_FAULT_EXECUTION_MANAGER );
        return false;
    }

    if ( !DUT_DRIVER_LIFECYCLE_Start() )
    {
        EXECUTION_MANAGER_Abort();
        RUN_STATE_MANAGER_EnterFault( RUN_STATE_FAULT_DRIVER_START );
        return false;
    }

    /* A fault may arrive while the task-level driver start call is running. */
    if ( execution_abort_requested || requested_fault_reason != RUN_STATE_FAULT_NONE )
    {
        RUN_STATE_MANAGER_EnterFault( ( requested_fault_reason != RUN_STATE_FAULT_NONE )
                                          ? requested_fault_reason
                                          : RUN_STATE_FAULT_EXTERNAL_REQUEST );
        return false;
    }

    RUN_STATE_MANAGER_StartPendingOperation( RUN_STATE_PENDING_DRIVER_START );
    return true;
}
/**
 * @brief Stops the execution timer followed by all active DUT-facing drivers.
 */
static bool RUN_STATE_MANAGER_StopExecution( void )
{
    RUN_STATE_MANAGER_StopExecutionTimer();
    RUN_STATE_MANAGER_CaptureExecutionMetadata();
    EXECUTION_MANAGER_Abort();

    execution_active = false;
    if ( !DUT_DRIVER_LIFECYCLE_Stop() )
    {
        RUN_STATE_MANAGER_RecordFault( RUN_STATE_FAULT_DRIVER_STOP );
        return false;
    }

    return true;
}

static bool RUN_STATE_MANAGER_BeginDriverShutdown( bool force_abort, bool clear_configuration,
                                                   RunStatePendingOperation_T operation )
{
    if ( force_abort )
    {
        execution_abort_requested = true;
    }
    RUN_STATE_MANAGER_StopExecutionTimer();
    RUN_STATE_MANAGER_CaptureExecutionMetadata();
    execution_active        = false;
    driver_cleanup_complete = false;

    if ( !DUT_DRIVER_LIFECYCLE_BeginShutdown( force_abort, clear_configuration ) )
    {
        return false;
    }

    RUN_STATE_MANAGER_StartPendingOperation( operation );
    return true;
}

/**
 * @brief Requests asynchronous preparation of the Flash Manager for execution.
 */
static bool RUN_STATE_MANAGER_BeginExecutionPreparation( void )
{
    uint32_t result_capacity_bytes = 0U;
    if ( !FLASH_MANAGER_GetResultCapacityBytes( &result_capacity_bytes )
         || ( result_capacity_bytes == 0U ) )
    {
        RUN_STATE_MANAGER_EnterFault( RUN_STATE_FAULT_FLASH_EXECUTION_PREPARATION );
        return false;
    }

    FlashManagerRequestStatus_T status =
        FLASH_MANAGER_RequestExecutionPreparation( result_capacity_bytes );

    if ( status == FLASH_MANAGER_REQUEST_OK )
    {
        RUN_STATE_MANAGER_StartPendingOperation( RUN_STATE_PENDING_EXECUTION_PREPARATION );
        return true;
    }

    RUN_STATE_MANAGER_EnterFault( RUN_STATE_FAULT_FLASH_EXECUTION_PREPARATION );
    return false;
}

/**
 * @brief Stops execution and requests asynchronous result finalisation.
 */
static bool RUN_STATE_MANAGER_BeginResultFinalisation( void )
{
    if ( !RUN_STATE_MANAGER_BeginDriverShutdown( false, false, RUN_STATE_PENDING_DRIVER_SHUTDOWN ) )
    {
        RUN_STATE_MANAGER_EnterFault( RUN_STATE_FAULT_DRIVER_STOP );
        return false;
    }
    return true;
}

/** Starts Flash Manager result retrieval before entering RESULT_TRANSFER. */
static bool RUN_STATE_MANAGER_BeginResultTransfer( void )
{
    if ( FLASH_MANAGER_RequestResultTransferStart() != FLASH_MANAGER_RESULT_TRANSFER_OK )
    {
        RUN_STATE_MANAGER_EnterFault( RUN_STATE_FAULT_FLASH_RESULT_TRANSFER );
        return false;
    }

    return RUN_STATE_MANAGER_TransitionTo( RUN_STATE_RESULT_TRANSFER );
}

/** Clears the retained test and asynchronously returns the DUT lifecycle to IDLE. */
static bool RUN_STATE_MANAGER_ClearConfigurationAndReturnToIdle( void )
{
    if ( !RUN_STATE_MANAGER_BeginDriverShutdown( false, true, RUN_STATE_PENDING_IDLE_SHUTDOWN ) )
    {
        return false;
    }

    TEST_CONFIGURATION_Clear();
    TEST_CONFIGURATION_ReleaseRunOwnership();
    run_configuration_owned = false;
    return true;
}

/** Finishes a fully consumed Flash result stream and returns to ARMED via CONFIGURATION. */
static bool RUN_STATE_MANAGER_CompleteResultTransfer( void )
{
    const FlashManagerResultTransferStatus_T status = FLASH_MANAGER_FinishResultTransfer();

    if ( status == FLASH_MANAGER_RESULT_TRANSFER_INCOMPLETE )
    {
        return false;
    }

    if ( status != FLASH_MANAGER_RESULT_TRANSFER_OK )
    {
        RUN_STATE_MANAGER_EnterFault( RUN_STATE_FAULT_FLASH_RESULT_TRANSFER );
        return false;
    }

    if ( !RUN_STATE_MANAGER_TransitionTo( RUN_STATE_CONFIGURATION ) )
    {
        return false;
    }

    RUN_STATE_MANAGER_StartPendingOperation( RUN_STATE_PENDING_CONFIGURATION );
    return true;
}

/**
 * @brief Releases completed Flash results before applying the selected policy.
 */
static bool RUN_STATE_MANAGER_DiscardCompletedResults( RunState_T next_state )
{
    if ( next_state == RUN_STATE_IDLE )
    {
        if ( !RUN_STATE_MANAGER_ClearConfigurationAndReturnToIdle() )
        {
            return false;
        }

        const FlashManagerRequestStatus_T status = FLASH_MANAGER_DiscardResults();
        if ( status != FLASH_MANAGER_REQUEST_OK )
        {
            RUN_STATE_MANAGER_EnterFault( RUN_STATE_FAULT_FLASH_RESULT_DISPOSITION );
            return false;
        }

        return true;
    }

    if ( next_state == RUN_STATE_ARMED )
    {
        /* Replay the committed configuration so outputs return to test-start values. */
        if ( !RUN_STATE_MANAGER_TransitionTo( RUN_STATE_CONFIGURATION ) )
        {
            return false;
        }

        const FlashManagerRequestStatus_T status = FLASH_MANAGER_DiscardResults();
        if ( status != FLASH_MANAGER_REQUEST_OK )
        {
            RUN_STATE_MANAGER_EnterFault( RUN_STATE_FAULT_FLASH_RESULT_DISPOSITION );
            return false;
        }

        RUN_STATE_MANAGER_StartPendingOperation( RUN_STATE_PENDING_CONFIGURATION );
        return true;
    }

    return RUN_STATE_MANAGER_TransitionTo( next_state );
}

/** Returns true only when Flash is in the reusable IDLE state. */
static bool RUN_STATE_MANAGER_FlashIsIdle( void )
{
    FlashManagerState_T flash_state = FLASH_MANAGER_STATE_UNINITIALISED;
    return FLASH_MANAGER_GetState( &flash_state ) && flash_state == FLASH_MANAGER_STATE_IDLE;
}

/**
 * @brief Advances an asynchronous Flash Manager lifecycle operation.
 */
static void RUN_STATE_MANAGER_ProcessPendingOperation( void )
{
    if ( pending_operation == RUN_STATE_PENDING_NONE )
    {
        return;
    }

    if ( pending_operation == RUN_STATE_PENDING_CONFIGURATION )
    {
        const DutDriverConfigurationStatus_T status = DUT_DRIVER_LIFECYCLE_GetConfigurationStatus();

        if ( status == DUT_DRIVER_CONFIGURATION_READY )
        {
            RUN_STATE_MANAGER_ClearPendingOperation();
            if ( !RUN_STATE_MANAGER_BeginExecutionPreparation() )
            {
                return;
            }
        }
        else if ( status == DUT_DRIVER_CONFIGURATION_FAILED )
        {
            RUN_STATE_MANAGER_EnterFault( RUN_STATE_FAULT_DRIVER_CONFIGURATION );
        }
        else if ( RUN_STATE_MANAGER_PendingOperationTimedOut(
                      pdMS_TO_TICKS( RUN_STATE_MANAGER_CONFIGURATION_TIMEOUT_MS ) ) )
        {
            RUN_STATE_MANAGER_EnterFault( RUN_STATE_FAULT_DRIVER_CONFIGURATION_TIMEOUT );
        }
        return;
    }

    if ( pending_operation == RUN_STATE_PENDING_DRIVER_START )
    {
        const DutDriverStartStatus_T status = DUT_DRIVER_LIFECYCLE_GetStartStatus();

        if ( status == DUT_DRIVER_START_READY )
        {
            RUN_STATE_MANAGER_ClearPendingOperation();
            ( void )RUN_STATE_MANAGER_TransitionTo( RUN_STATE_EXECUTION );
        }
        else if ( status == DUT_DRIVER_START_FAILED )
        {
            RUN_STATE_MANAGER_EnterFault( RUN_STATE_FAULT_DRIVER_START );
        }
        else if ( RUN_STATE_MANAGER_PendingOperationTimedOut(
                      pdMS_TO_TICKS( RUN_STATE_MANAGER_DRIVER_START_TIMEOUT_MS ) ) )
        {
            RUN_STATE_MANAGER_EnterFault( RUN_STATE_FAULT_DRIVER_START_TIMEOUT );
        }
        return;
    }

    if ( pending_operation == RUN_STATE_PENDING_DRIVER_SHUTDOWN
         || pending_operation == RUN_STATE_PENDING_IDLE_SHUTDOWN
         || pending_operation == RUN_STATE_PENDING_FAULT_SHUTDOWN )
    {
        const RunStatePendingOperation_T operation = pending_operation;
        const DutDriverShutdownStatus_T  status    = DUT_DRIVER_LIFECYCLE_GetShutdownStatus();

        if ( status == DUT_DRIVER_SHUTDOWN_COMPLETE )
        {
            driver_cleanup_complete = true;
            RUN_STATE_MANAGER_ClearPendingOperation();

            if ( operation == RUN_STATE_PENDING_DRIVER_SHUTDOWN )
            {
                if ( FLASH_MANAGER_RequestResultFinalisation() != FLASH_MANAGER_REQUEST_OK )
                {
                    RUN_STATE_MANAGER_EnterFault( RUN_STATE_FAULT_FLASH_RESULT_FINALISATION );
                    return;
                }
                RUN_STATE_MANAGER_StartPendingOperation( RUN_STATE_PENDING_RESULT_FINALISATION );
                ( void )RUN_STATE_MANAGER_TransitionTo( RUN_STATE_RESULT_FINALISATION );
            }
            else if ( operation == RUN_STATE_PENDING_IDLE_SHUTDOWN )
            {
                execution_abort_requested = false;
                ( void )RUN_STATE_MANAGER_TransitionTo( RUN_STATE_IDLE );
            }
        }
        else if ( status == DUT_DRIVER_SHUTDOWN_FAILED )
        {
            if ( operation == RUN_STATE_PENDING_FAULT_SHUTDOWN )
            {
                ( void )DUT_DRIVER_LIFECYCLE_BeginShutdown( true, true );
            }
            else
            {
                RUN_STATE_MANAGER_EnterFault( RUN_STATE_FAULT_DRIVER_STOP );
            }
        }
        else if ( RUN_STATE_MANAGER_PendingOperationTimedOut(
                      pdMS_TO_TICKS( RUN_STATE_MANAGER_DRIVER_SHUTDOWN_TIMEOUT_MS ) ) )
        {
            if ( operation != RUN_STATE_PENDING_FAULT_SHUTDOWN )
            {
                RUN_STATE_MANAGER_RecordFault( RUN_STATE_FAULT_DRIVER_STOP_TIMEOUT );
                RUN_STATE_MANAGER_EnterFault( RUN_STATE_FAULT_DRIVER_STOP_TIMEOUT );
            }
            else
            {
                ( void )DUT_DRIVER_LIFECYCLE_BeginShutdown( true, true );
                pending_operation_started_at = xTaskGetTickCount();
            }
        }
        return;
    }

    FlashManagerState_T flash_state = FLASH_MANAGER_STATE_UNINITIALISED;

    if ( !FLASH_MANAGER_GetState( &flash_state ) )
    {
        RUN_STATE_MANAGER_EnterFault( RUN_STATE_FAULT_FLASH_MANAGER );
        return;
    }

    if ( flash_state == FLASH_MANAGER_STATE_FAULT )
    {
        RUN_STATE_MANAGER_EnterFault( RUN_STATE_FAULT_FLASH_MANAGER );
        return;
    }

    switch ( pending_operation )
    {
        case RUN_STATE_PENDING_INSTRUCTION_UPLOAD_PREPARATION:
            if ( flash_state == FLASH_MANAGER_STATE_INSTRUCTION_UPLOAD )
            {
                RUN_STATE_MANAGER_ClearPendingOperation();
                if ( !RUN_STATE_MANAGER_TransitionTo( RUN_STATE_TEST_PACKAGE_RECEIVE ) )
                {
                    RUN_STATE_MANAGER_EnterFault( RUN_STATE_FAULT_INVALID_TRANSITION );
                }
            }
            else if ( flash_state != FLASH_MANAGER_STATE_PREPARING_INSTRUCTION_UPLOAD )
            {
                RUN_STATE_MANAGER_EnterFault( RUN_STATE_FAULT_FLASH_MANAGER );
            }
            else if ( RUN_STATE_MANAGER_PendingOperationTimedOut(
                          pdMS_TO_TICKS( RUN_STATE_MANAGER_INSTRUCTION_UPLOAD_TIMEOUT_MS ) ) )
            {
                RUN_STATE_MANAGER_EnterFault( RUN_STATE_FAULT_FLASH_MANAGER );
            }
            break;

        case RUN_STATE_PENDING_INSTRUCTION_UPLOAD_FINALISATION:
            if ( flash_state == FLASH_MANAGER_STATE_IDLE )
            {
                RUN_STATE_MANAGER_ClearPendingOperation();
                if ( RUN_STATE_MANAGER_TransitionTo( RUN_STATE_CONFIGURATION ) )
                {
                    RUN_STATE_MANAGER_StartPendingOperation( RUN_STATE_PENDING_CONFIGURATION );
                }
                else
                {
                    RUN_STATE_MANAGER_EnterFault( RUN_STATE_FAULT_INVALID_TRANSITION );
                }
            }
            else if ( flash_state != FLASH_MANAGER_STATE_FINALISING_INSTRUCTION_UPLOAD )
            {
                RUN_STATE_MANAGER_EnterFault( RUN_STATE_FAULT_FLASH_MANAGER );
            }
            else if ( RUN_STATE_MANAGER_PendingOperationTimedOut(
                          pdMS_TO_TICKS( RUN_STATE_MANAGER_INSTRUCTION_UPLOAD_TIMEOUT_MS ) ) )
            {
                RUN_STATE_MANAGER_EnterFault( RUN_STATE_FAULT_FLASH_MANAGER );
            }
            break;

        case RUN_STATE_PENDING_CONFIGURATION:
            /* Handled before querying Flash Manager state. */
            break;

        case RUN_STATE_PENDING_EXECUTION_PREPARATION:
            if ( flash_state == FLASH_MANAGER_STATE_EXECUTING )
            {
                RUN_STATE_MANAGER_ClearPendingOperation();
                ( void )RUN_STATE_MANAGER_TransitionTo( RUN_STATE_ARMED );
            }
            else if ( flash_state != FLASH_MANAGER_STATE_PREPARING_EXECUTION )
            {
                RUN_STATE_MANAGER_EnterFault( RUN_STATE_FAULT_FLASH_EXECUTION_PREPARATION );
            }
            else if ( RUN_STATE_MANAGER_PendingOperationTimedOut(
                          pdMS_TO_TICKS( RUN_STATE_MANAGER_EXECUTION_PREPARATION_TIMEOUT_MS ) ) )
            {
                RUN_STATE_MANAGER_EnterFault( RUN_STATE_FAULT_FLASH_EXECUTION_PREPARATION_TIMEOUT );
            }
            break;

        case RUN_STATE_PENDING_DRIVER_START:
            /* Handled before querying Flash Manager state. */
            break;

        case RUN_STATE_PENDING_DRIVER_SHUTDOWN:
        case RUN_STATE_PENDING_IDLE_SHUTDOWN:
        case RUN_STATE_PENDING_FAULT_SHUTDOWN:
            /* Handled before querying Flash Manager state. */
            break;

        case RUN_STATE_PENDING_RESULT_FINALISATION:
            if ( flash_state == FLASH_MANAGER_STATE_RESULTS_READY )
            {
                RUN_STATE_MANAGER_ClearPendingOperation();
                const RunMetadataResultStreamStatus_T stream_status =
                    ( fault_reason == RUN_STATE_FAULT_NONE ) ? RUN_METADATA_RESULT_STREAM_COMPLETE
                                                             : RUN_METADATA_RESULT_STREAM_PARTIAL;
                ( void )RUN_METADATA_SetResultStreamStatus( stream_status );
                ( void )RUN_METADATA_Seal();
                ( void )RUN_STATE_MANAGER_TransitionTo( RUN_STATE_RESULTS_READY );
            }
            else if ( flash_state != FLASH_MANAGER_STATE_FINALISING_RESULTS )
            {
                ( void )RUN_METADATA_SetResultStreamStatus(
                    RUN_METADATA_RESULT_STREAM_UNAVAILABLE );
                ( void )RUN_METADATA_Seal();
                RUN_STATE_MANAGER_EnterFault( RUN_STATE_FAULT_FLASH_RESULT_FINALISATION );
            }
            else if ( RUN_STATE_MANAGER_PendingOperationTimedOut(
                          pdMS_TO_TICKS( RUN_STATE_MANAGER_RESULT_FINALISATION_TIMEOUT_MS ) ) )
            {
                ( void )RUN_METADATA_SetResultStreamStatus(
                    RUN_METADATA_RESULT_STREAM_UNAVAILABLE );
                ( void )RUN_METADATA_Seal();
                RUN_STATE_MANAGER_EnterFault( RUN_STATE_FAULT_FLASH_RESULT_FINALISATION_TIMEOUT );
            }
            break;

        case RUN_STATE_PENDING_NONE:
        default:
            RUN_STATE_MANAGER_EnterFault( RUN_STATE_FAULT_INTERNAL );
            break;
    }
}

/** Validates and processes one externally supplied lifecycle event. */
static void RUN_STATE_MANAGER_ProcessRequest( RunStateRequest_T request )
{
    last_request = request;

    if ( pending_operation != RUN_STATE_PENDING_NONE && request != RUN_STATE_REQUEST_FAULT )
    {
        last_request_result = RUN_STATE_REQUEST_RESULT_REJECTED_PENDING;
        return;
    }

    bool accepted = false;

    switch ( request )
    {
        case RUN_STATE_REQUEST_PACKAGE_RECEIVE:
            if ( run_state == RUN_STATE_IDLE )
            {
                taskENTER_CRITICAL();
                const uint32_t expected_ticks = package_receive_expected_ticks;
                taskEXIT_CRITICAL();
                accepted = RUN_STATE_MANAGER_BeginInstructionUpload( expected_ticks );
            }
            break;

        case RUN_STATE_REQUEST_CONFIGURATION_READY:
            if ( run_state == RUN_STATE_TEST_PACKAGE_RECEIVE )
            {
                accepted = RUN_STATE_MANAGER_BeginInstructionUploadFinalisation();
            }
            break;

        case RUN_STATE_REQUEST_EXECUTION:
            if ( run_state == RUN_STATE_ARMED )
            {
                accepted = RUN_STATE_MANAGER_BeginDriverStart();
            }
            if ( !accepted )
            {
                taskENTER_CRITICAL();
                execution_request_pending = false;
                taskEXIT_CRITICAL();
            }
            break;

        case RUN_STATE_REQUEST_EXECUTION_COMPLETE:
            if ( run_state == RUN_STATE_EXECUTION )
            {
                accepted = RUN_STATE_MANAGER_BeginResultFinalisation();
            }
            break;

        case RUN_STATE_REQUEST_RESULT_TRANSFER:
            if ( run_state == RUN_STATE_RESULTS_READY )
            {
                accepted = RUN_STATE_MANAGER_BeginResultTransfer();
            }
            break;

        case RUN_STATE_REQUEST_RESULT_TRANSFER_COMPLETE:
            if ( run_state == RUN_STATE_RESULT_TRANSFER )
            {
                accepted = RUN_STATE_MANAGER_CompleteResultTransfer();
                if ( !accepted && run_state != RUN_STATE_FAULT )
                {
                    last_request_result = RUN_STATE_REQUEST_RESULT_REJECTED_SUBSYSTEM_STATE;
                    return;
                }
            }
            break;

        case RUN_STATE_REQUEST_REPEAT:
            if ( run_state == RUN_STATE_RESULTS_READY )
            {
                accepted = RUN_STATE_MANAGER_DiscardCompletedResults( RUN_STATE_ARMED );
            }
            break;

        case RUN_STATE_REQUEST_DISCARD_RESULTS:
            if ( run_state == RUN_STATE_RESULTS_READY )
            {
                accepted = RUN_STATE_MANAGER_DiscardCompletedResults( RUN_STATE_IDLE );
                if ( !accepted && run_state != RUN_STATE_FAULT )
                {
                    last_request_result = RUN_STATE_REQUEST_RESULT_REJECTED_SUBSYSTEM_STATE;
                    return;
                }
            }
            else if ( run_state == RUN_STATE_ARMED )
            {
                ( void )FLASH_MANAGER_RequestAbortSession();
                accepted = RUN_STATE_MANAGER_ClearConfigurationAndReturnToIdle();
            }
            break;

        case RUN_STATE_REQUEST_FAULT: {
            const RunStateFaultReason_T reason = requested_fault_reason;
            requested_fault_reason             = RUN_STATE_FAULT_NONE;
            RUN_STATE_MANAGER_EnterFault( reason );
            accepted = run_state == RUN_STATE_FAULT;
            break;
        }

        case RUN_STATE_REQUEST_RESET:
            if ( run_state == RUN_STATE_FAULT )
            {
                if ( !driver_cleanup_complete || !RUN_STATE_MANAGER_FlashIsIdle() )
                {
                    last_request_result = RUN_STATE_REQUEST_RESULT_REJECTED_SUBSYSTEM_STATE;
                    return;
                }

                accepted = RUN_STATE_MANAGER_TransitionTo( RUN_STATE_IDLE );
                if ( accepted )
                {
                    fault_reason              = RUN_STATE_FAULT_NONE;
                    execution_abort_requested = false;
                    TEST_CONFIGURATION_ReleaseRunOwnership();
                    run_configuration_owned = false;
                }
            }
            else if ( run_state == RUN_STATE_IDLE )
            {
                accepted = true;
            }
            break;

        case RUN_STATE_REQUEST_NONE:
        default:
            break;
    }

    if ( accepted )
    {
        last_request_result = RUN_STATE_REQUEST_RESULT_ACCEPTED;
        RUN_STATE_MANAGER_StartRequestTiming( request );
    }
    else if ( run_state == RUN_STATE_FAULT )
    {
        last_request_result = RUN_STATE_REQUEST_RESULT_FAILED;
    }
    else
    {
        last_request_result = RUN_STATE_REQUEST_RESULT_REJECTED_STATE;
    }
}

/**
 * @brief Processes one notification and preserves any coalesced notifications.
 *
 * Fault, reset, and timer-stop requests have priority. Remaining bits are
 * returned to the task notification word so no named event is silently lost.
 */
static void RUN_STATE_MANAGER_ProcessNotifications( uint32_t notifications )
{
    uint32_t          selected_bit = 0U;
    RunStateRequest_T request      = RUN_STATE_REQUEST_NONE;

    if ( ( notifications & RUN_STATE_MANAGER_NOTIFY_FAULT ) != 0U )
    {
        selected_bit = RUN_STATE_MANAGER_NOTIFY_FAULT;
        request      = RUN_STATE_REQUEST_FAULT;
    }
    else if ( ( notifications & RUN_STATE_MANAGER_NOTIFY_RESET ) != 0U )
    {
        selected_bit = RUN_STATE_MANAGER_NOTIFY_RESET;
        request      = RUN_STATE_REQUEST_RESET;
    }
    else if ( ( notifications & RUN_STATE_MANAGER_NOTIFY_PACKAGE_RECEIVE ) != 0U )
    {
        selected_bit = RUN_STATE_MANAGER_NOTIFY_PACKAGE_RECEIVE;
        request      = RUN_STATE_REQUEST_PACKAGE_RECEIVE;
    }
    else if ( ( notifications & RUN_STATE_MANAGER_NOTIFY_CONFIGURATION ) != 0U )
    {
        selected_bit = RUN_STATE_MANAGER_NOTIFY_CONFIGURATION;
        request      = RUN_STATE_REQUEST_CONFIGURATION_READY;
    }
    else if ( ( notifications & RUN_STATE_MANAGER_NOTIFY_EXECUTION ) != 0U )
    {
        selected_bit = RUN_STATE_MANAGER_NOTIFY_EXECUTION;
        request      = RUN_STATE_REQUEST_EXECUTION;
    }
    else if ( ( notifications & RUN_STATE_MANAGER_NOTIFY_EXECUTION_COMPLETE ) != 0U )
    {
        selected_bit = RUN_STATE_MANAGER_NOTIFY_EXECUTION_COMPLETE;
        request      = RUN_STATE_REQUEST_EXECUTION_COMPLETE;
    }
    else if ( ( notifications & RUN_STATE_MANAGER_NOTIFY_RESULT_TRANSFER ) != 0U )
    {
        selected_bit = RUN_STATE_MANAGER_NOTIFY_RESULT_TRANSFER;
        request      = RUN_STATE_REQUEST_RESULT_TRANSFER;
    }
    else if ( ( notifications & RUN_STATE_MANAGER_NOTIFY_RESULT_TRANSFER_COMPLETE ) != 0U )
    {
        selected_bit = RUN_STATE_MANAGER_NOTIFY_RESULT_TRANSFER_COMPLETE;
        request      = RUN_STATE_REQUEST_RESULT_TRANSFER_COMPLETE;
    }
    else if ( ( notifications & RUN_STATE_MANAGER_NOTIFY_REPEAT ) != 0U )
    {
        selected_bit = RUN_STATE_MANAGER_NOTIFY_REPEAT;
        request      = RUN_STATE_REQUEST_REPEAT;
    }
    else if ( ( notifications & RUN_STATE_MANAGER_NOTIFY_DISCARD_RESULTS ) != 0U )
    {
        selected_bit = RUN_STATE_MANAGER_NOTIFY_DISCARD_RESULTS;
        request      = RUN_STATE_REQUEST_DISCARD_RESULTS;
    }

    if ( selected_bit == 0U )
    {
        return;
    }

    RUN_STATE_MANAGER_ProcessRequest( request );

    const uint32_t remaining_notifications = notifications & ~selected_bit;
    if ( remaining_notifications != 0U )
    {
        ( void )RUN_STATE_MANAGER_Notify( remaining_notifications );
    }
}

/**
 * @brief Performs common exit and entry actions for a lifecycle transition.
 *
 * @param next_state State to enter.
 */
static bool RUN_STATE_MANAGER_TransitionTo( RunState_T next_state )
{
    if ( run_state == next_state )
    {
        return true;
    }

    if ( !RUN_STATE_MANAGER_IsTransitionAllowed( run_state, next_state ) )
    {
        RUN_STATE_MANAGER_EnterFault( RUN_STATE_FAULT_INVALID_TRANSITION );
        return false;
    }

    if ( next_state == RUN_STATE_FAULT )
    {
        RUN_STATE_MANAGER_ClearPendingOperation();
        if ( !RUN_STATE_MANAGER_BeginDriverShutdown( true, true,
                                                     RUN_STATE_PENDING_FAULT_SHUTDOWN ) )
        {
            driver_cleanup_complete = false;
            RUN_STATE_MANAGER_StartPendingOperation( RUN_STATE_PENDING_FAULT_SHUTDOWN );
        }
        ( void )FLASH_MANAGER_RequestAbortSession();
    }
    else if ( next_state != RUN_STATE_EXECUTION )
    {
        if ( execution_active && !RUN_STATE_MANAGER_StopExecution() )
        {
            RUN_STATE_MANAGER_EnterFault( RUN_STATE_FAULT_DRIVER_STOP );
            return false;
        }
    }

    switch ( next_state )
    {
        case RUN_STATE_IDLE:
            if ( ( fault_reason == RUN_STATE_FAULT_NONE )
                 && ( requested_fault_reason == RUN_STATE_FAULT_NONE ) )
            {
                execution_abort_requested = false;
            }
            break;

        case RUN_STATE_TEST_PACKAGE_RECEIVE:
            if ( !RUN_STATE_MANAGER_EnterTestPackageReceive() )
            {
                RUN_STATE_MANAGER_EnterFault( RUN_STATE_FAULT_INTERNAL );
                return false;
            }
            break;

        case RUN_STATE_CONFIGURATION:
            if ( ( fault_reason == RUN_STATE_FAULT_NONE )
                 && ( requested_fault_reason == RUN_STATE_FAULT_NONE ) )
            {
                execution_abort_requested = false;
            }
            if ( !RUN_STATE_MANAGER_EnterConfiguration() )
            {
                RUN_STATE_MANAGER_EnterFault( RUN_STATE_FAULT_DRIVER_CONFIGURATION );
                return false;
            }
            break;

        case RUN_STATE_ARMED:
            if ( !RUN_STATE_MANAGER_EnterArmed() )
            {
                RUN_STATE_MANAGER_EnterFault( RUN_STATE_FAULT_INTERNAL );
                return false;
            }
            break;

        case RUN_STATE_EXECUTION:
            if ( !RUN_STATE_MANAGER_EnterExecution() )
            {
                RUN_STATE_MANAGER_EnterFault( RUN_STATE_FAULT_INTERNAL );
                return false;
            }
            break;

        case RUN_STATE_RESULT_FINALISATION:
            break;

        case RUN_STATE_RESULTS_READY:
            if ( !RUN_STATE_MANAGER_EnterResultsReady() )
            {
                RUN_STATE_MANAGER_EnterFault( RUN_STATE_FAULT_HOST_INTERFACE_ERROR );
                return false;
            }
            break;

        case RUN_STATE_RESULT_TRANSFER:
            if ( !RUN_STATE_MANAGER_EnterResultTransfer() )
            {
                RUN_STATE_MANAGER_EnterFault( RUN_STATE_FAULT_HOST_INTERFACE_ERROR );
                return false;
            }
            break;

        case RUN_STATE_FAULT:
            break;

        default:
            RUN_STATE_MANAGER_EnterFault( RUN_STATE_FAULT_INTERNAL );
            return false;
    }

    run_state = next_state;
    RUN_STATE_MANAGER_CompleteRequestTimingIfReady();
    return true;
}

/**
 * @brief Configures and starts the Run State Manager-owned execution timer.
 */
static bool RUN_STATE_MANAGER_StartExecutionTimer( void )
{
    if ( execution_abort_requested || requested_fault_reason != RUN_STATE_FAULT_NONE )
    {
        return false;
    }

    if ( execution_timer_running )
    {
        return true;
    }

    switch ( prepared_execution.frequency )
    {
        case RUN_STATE_FREQUENCY_100HZ:
            HW_TIMER_Configure_Timer( EXECUTION_MANAGER_TIMER, PSC_100HZ, ARR_100HZ );
            break;
        case RUN_STATE_FREQUENCY_1KHZ:
            HW_TIMER_Configure_Timer( EXECUTION_MANAGER_TIMER, PSC_1KHZ, ARR_1KHZ );
            break;
        case RUN_STATE_FREQUENCY_10KHZ:
            HW_TIMER_Configure_Timer( EXECUTION_MANAGER_TIMER, PSC_10KHZ, ARR_10KHZ );
            break;
        default:
            return false;
    }

    if ( !HW_TIMER_Start_Timer( EXECUTION_MANAGER_TIMER ) )
    {
        return false;
    }

    execution_timer_running = true;

    return true;
}

/**
 * @brief Stops the Run State Manager-owned execution timer.
 */
static void RUN_STATE_MANAGER_StopExecutionTimer( void )
{
    HW_TIMER_Stop_Timer( EXECUTION_MANAGER_TIMER );
    execution_timer_running = false;
}

/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */

bool RUN_STATE_MANAGER_Set_Execution_Frequency( RunStateFrequencyMode_T mode )
{
    switch ( mode )
    {
        case RUN_STATE_FREQUENCY_100HZ:
        case RUN_STATE_FREQUENCY_1KHZ:
        case RUN_STATE_FREQUENCY_10KHZ:
            break;
        default:
            return false;
    }

    taskENTER_CRITICAL();
    const bool accepted = ( run_state == RUN_STATE_IDLE || run_state == RUN_STATE_ARMED
                            || run_state == RUN_STATE_TEST_PACKAGE_RECEIVE )
                          && pending_operation == RUN_STATE_PENDING_NONE && !execution_active
                          && !execution_request_pending;
    if ( accepted )
    {
        frequency_mode = mode;
    }
    taskEXIT_CRITICAL();
    return accepted;
}

RunStateFrequencyMode_T RUN_STATE_MANAGER_Get_Execution_Frequency( void )
{
    return frequency_mode;
}

void RUN_STATE_MANAGER_Init( void )
{
    RUN_STATE_MANAGER_StopExecutionTimer();
    RUN_METADATA_Reset();
    frequency_mode               = RUN_STATE_FREQUENCY_1KHZ;
    pending_operation            = RUN_STATE_PENDING_NONE;
    pending_operation_started_at = 0U;
    execution_active             = false;
    driver_cleanup_complete      = true;
    execution_timer_running      = false;
    execution_request_pending    = false;
    prepared_execution           = ( RunStatePreparedExecution_T ){
                  .tick_count = 0U, .frequency = RUN_STATE_FREQUENCY_1KHZ, .enable_drain_tail = false };
    execution_abort_requested      = false;
    fault_reason                   = RUN_STATE_FAULT_NONE;
    requested_fault_reason         = RUN_STATE_FAULT_NONE;
    last_request                   = RUN_STATE_REQUEST_NONE;
    last_request_result            = RUN_STATE_REQUEST_RESULT_NONE;
    request_timing_active          = false;
    timed_request                  = RUN_STATE_REQUEST_NONE;
    timed_request_target_state     = RUN_STATE_IDLE;
    timed_request_started_at       = 0U;
    last_transition_timing_valid   = false;
    last_completed_request         = RUN_STATE_REQUEST_NONE;
    last_transition_duration_ms    = 0U;
    run_state                      = RUN_STATE_IDLE;
    run_configuration_owned        = false;
    package_receive_expected_ticks = 0U;

    HW_TIMER_Set_Execution_Guard( RUN_STATE_MANAGER_ExecutionDispatchAllowedFromISR );
    FLASH_MANAGER_SetFaultCallback( RUN_STATE_MANAGER_HandleFlashFault );
    EXECUTION_MANAGER_SetTerminalCallback( RUN_STATE_MANAGER_HandleExecutionTerminalFromISR );
}

bool RUN_STATE_MANAGER_RequestPackageReceiveWithTicks( uint32_t expected_tick_count )
{
    taskENTER_CRITICAL();
    package_receive_expected_ticks = expected_tick_count;
    taskEXIT_CRITICAL();
    return RUN_STATE_MANAGER_Notify( RUN_STATE_MANAGER_NOTIFY_PACKAGE_RECEIVE );
}

bool RUN_STATE_MANAGER_RequestPackageReceive( void )
{
    return RUN_STATE_MANAGER_RequestPackageReceiveWithTicks( 0U );
}

bool RUN_STATE_MANAGER_RequestConfiguration( void )
{
    return RUN_STATE_MANAGER_Notify( RUN_STATE_MANAGER_NOTIFY_CONFIGURATION );
}

RunStateExecutionRequestResult_T
RUN_STATE_MANAGER_RequestExecution( const RunStateExecutionRequest_T* request )
{
    if ( ( request == NULL ) || ( request->tick_count == 0U ) )
    {
        return RUN_STATE_EXECUTION_REQUEST_INVALID_ARGUMENT;
    }

    taskENTER_CRITICAL();
    RunStateExecutionRequestResult_T result = RUN_STATE_EXECUTION_REQUEST_ACCEPTED;
    if ( run_state != RUN_STATE_ARMED )
    {
        result = RUN_STATE_EXECUTION_REQUEST_INVALID_STATE;
    }
    else if ( pending_operation != RUN_STATE_PENDING_NONE || execution_active
              || execution_request_pending )
    {
        result = RUN_STATE_EXECUTION_REQUEST_BUSY;
    }
    else
    {
        RUN_METADATA_Reset();
        prepared_execution = ( RunStatePreparedExecution_T ){
            .tick_count        = request->tick_count,
            .frequency         = frequency_mode,
            .enable_drain_tail = request->enable_drain_tail,
        };
        execution_request_pending = true;
    }
    taskEXIT_CRITICAL();

    if ( result != RUN_STATE_EXECUTION_REQUEST_ACCEPTED )
    {
        return result;
    }

    if ( RUN_STATE_MANAGER_Notify( RUN_STATE_MANAGER_NOTIFY_EXECUTION ) )
    {
        return RUN_STATE_EXECUTION_REQUEST_ACCEPTED;
    }

    taskENTER_CRITICAL();
    execution_request_pending = false;
    taskEXIT_CRITICAL();
    return RUN_STATE_EXECUTION_REQUEST_NOTIFY_FAILED;
}

bool RUN_STATE_MANAGER_RequestExecutionComplete( void )
{
    return RUN_STATE_MANAGER_Notify( RUN_STATE_MANAGER_NOTIFY_EXECUTION_COMPLETE );
}

bool RUN_STATE_MANAGER_RequestResultTransfer( void )
{
    return RUN_STATE_MANAGER_Notify( RUN_STATE_MANAGER_NOTIFY_RESULT_TRANSFER );
}

bool RUN_STATE_MANAGER_RequestResultTransferComplete( void )
{
    return RUN_STATE_MANAGER_Notify( RUN_STATE_MANAGER_NOTIFY_RESULT_TRANSFER_COMPLETE );
}

bool RUN_STATE_MANAGER_RequestRepeat( void )
{
    return RUN_STATE_MANAGER_Notify( RUN_STATE_MANAGER_NOTIFY_REPEAT );
}

bool RUN_STATE_MANAGER_RequestDiscardResults( void )
{
    return RUN_STATE_MANAGER_Notify( RUN_STATE_MANAGER_NOTIFY_DISCARD_RESULTS );
}

bool RUN_STATE_MANAGER_RequestFault( RunStateFaultReason_T reason )
{
    if ( reason == RUN_STATE_FAULT_NONE )
    {
        return false;
    }

    execution_abort_requested = true;

    bool stored_reason = false;

    taskENTER_CRITICAL();
    if ( requested_fault_reason == RUN_STATE_FAULT_NONE )
    {
        requested_fault_reason = reason;
        stored_reason          = true;
    }
    taskEXIT_CRITICAL();

    if ( !stored_reason )
    {
        return true;
    }

    if ( RUN_STATE_MANAGER_Notify( RUN_STATE_MANAGER_NOTIFY_FAULT ) )
    {
        return true;
    }

    taskENTER_CRITICAL();
    if ( requested_fault_reason == reason )
    {
        requested_fault_reason = RUN_STATE_FAULT_NONE;
    }
    taskEXIT_CRITICAL();
    return false;
}

static bool RUN_STATE_MANAGER_RequestFaultFromISRInternal( RunStateFaultReason_T reason,
                                                           BaseType_t* higher_priority_task_woken )
{
    if ( reason == RUN_STATE_FAULT_NONE )
    {
        return false;
    }

    execution_abort_requested = true;

    if ( requested_fault_reason == RUN_STATE_FAULT_NONE )
    {
        requested_fault_reason = reason;
    }

    if ( run_state_manager_task_handle == NULL )
    {
        return false;
    }

    return xTaskNotifyFromISR( run_state_manager_task_handle, RUN_STATE_MANAGER_NOTIFY_FAULT,
                               eSetBits, higher_priority_task_woken )
           == pdPASS;
}

bool RUN_STATE_MANAGER_RequestFaultFromISR( RunStateFaultReason_T reason )
{
    return RUN_STATE_MANAGER_RequestFaultFromISRInternal( reason, NULL );
}

bool RUN_STATE_MANAGER_ExecutionAbortRequestedFromISR( void )
{
    return execution_abort_requested;
}

bool RUN_STATE_MANAGER_RequestReset( void )
{
    return RUN_STATE_MANAGER_Notify( RUN_STATE_MANAGER_NOTIFY_RESET );
}

RunState_T RUN_STATE_MANAGER_GetState( void )
{
    return run_state;
}

void RUN_STATE_MANAGER_GetStatus( RunStateManagerStatus_T* status )
{
    if ( status == NULL )
    {
        return;
    }

    taskENTER_CRITICAL();
    const TickType_t timing_elapsed =
        request_timing_active ? ( TickType_t )( xTaskGetTickCount() - timed_request_started_at )
                              : 0U;
    *status = ( RunStateManagerStatus_T ){
        .state                    = run_state,
        .transition_pending       = pending_operation != RUN_STATE_PENDING_NONE,
        .execution_active         = execution_active,
        .execution_timer_running  = execution_timer_running,
        .execution_frequency      = frequency_mode,
        .fault_reason             = fault_reason,
        .last_request             = last_request,
        .last_request_result      = last_request_result,
        .request_timing_active    = request_timing_active,
        .timed_request            = request_timing_active ? timed_request : RUN_STATE_REQUEST_NONE,
        .timed_request_elapsed_ms = RUN_STATE_MANAGER_TicksToMilliseconds( timing_elapsed ),
        .last_transition_timing_valid = last_transition_timing_valid,
        .last_completed_request       = last_completed_request,
        .last_transition_duration_ms  = last_transition_duration_ms,
    };
    taskEXIT_CRITICAL();

    HW_CAN_GetDiagnostic( &status->can_diag );
}

bool RUN_STATE_MANAGER_IsTransitionPending( void )
{
    return pending_operation != RUN_STATE_PENDING_NONE;
}

bool RUN_STATE_MANAGER_IsExecutionActive( void )
{
    return execution_active;
}

bool RUN_STATE_MANAGER_IsExecutionTimerRunning( void )
{
    return execution_timer_running;
}

RunStateFaultReason_T RUN_STATE_MANAGER_GetFaultReason( void )
{
    return fault_reason;
}

RunStateRequest_T RUN_STATE_MANAGER_GetLastRequest( void )
{
    return last_request;
}

RunStateRequestResult_T RUN_STATE_MANAGER_GetLastRequestResult( void )
{
    return last_request_result;
}

void RUN_STATE_MANAGER_Task( void* task_parameters )
{
    ( void )task_parameters;

    RUN_STATE_MANAGER_Init();
    run_state_manager_task_handle = xTaskGetCurrentTaskHandle();
    FLASH_MANAGER_SetFaultCallback( RUN_STATE_MANAGER_HandleFlashFault );

    while ( true )
    {
        uint32_t   notifications = 0U;
        TickType_t wait_ticks    = portMAX_DELAY;

        if ( pending_operation != RUN_STATE_PENDING_NONE )
        {
            wait_ticks = pdMS_TO_TICKS( RUN_STATE_MANAGER_PENDING_POLL_MS );
        }

        if ( xTaskNotifyWait( 0U, UINT32_MAX, &notifications, wait_ticks ) == pdTRUE )
        {
            RUN_STATE_MANAGER_ProcessNotifications( notifications );
        }

        RUN_STATE_MANAGER_ProcessPendingOperation();
    }
}
