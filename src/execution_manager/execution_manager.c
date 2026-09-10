/******************************************************************************
 *  File:       execution_manager.c
 *
 *  Description:
 *      Deterministic execution of prepared per-tick output instructions.
 ******************************************************************************/

#include "execution_manager.h"
#include "execution_manager_isr.h"
#include "execution_measurement_adapters.h"
#include "execution_operation_adapters.h"
#include "flash_manager.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum
{
    EXECUTION_STATE_IDLE = 0,
    EXECUTION_STATE_READY,
    EXECUTION_STATE_COMPLETE,
    EXECUTION_STATE_FAILED
} ExecutionState_T;

static uint32_t                           execution_tick_count = 0U;
static volatile uint32_t                  current_tick         = 0U;
static volatile ExecutionManagerFailure_T execution_failure    = EXECUTION_MANAGER_FAILURE_NONE;
static volatile ExecutionState_T          execution_state      = EXECUTION_STATE_IDLE;
static bool                               instruction_stream_exhausted = false;
static bool                               operation_timing_requested   = false;
static bool                               operation_timing_active      = false;
static ExecutionManagerTerminalCallback_T terminal_callback            = NULL;

static ExecutionManagerTickResult_T
EXECUTION_MANAGER_FailFromISR( ExecutionManagerFailure_T failure,
                               BaseType_t*               higher_priority_task_woken )
{
    if ( execution_failure == EXECUTION_MANAGER_FAILURE_NONE )
    {
        execution_failure = failure;
    }

    execution_state = EXECUTION_STATE_FAILED;
    if ( terminal_callback != NULL )
    {
        terminal_callback( EXECUTION_MANAGER_TICK_FAILED, execution_failure,
                           higher_priority_task_woken );
    }
    return EXECUTION_MANAGER_TICK_FAILED;
}

void EXECUTION_MANAGER_SetTerminalCallback( ExecutionManagerTerminalCallback_T callback )
{
    terminal_callback = callback;
}

void EXECUTION_MANAGER_ConfigureMeasurements(
    const ExecutionMeasurementConfiguration_T* configuration )
{
    EXECUTION_MEASUREMENT_ADAPTER_Prepare( configuration );
}

bool EXECUTION_MANAGER_Prepare( uint32_t tick_count )
{
    if ( tick_count == 0U )
    {
        return false;
    }

    execution_tick_count         = tick_count;
    current_tick                 = 0U;
    execution_failure            = EXECUTION_MANAGER_FAILURE_NONE;
    execution_state              = EXECUTION_STATE_READY;
    instruction_stream_exhausted = false;
    operation_timing_active      = operation_timing_requested;
    operation_timing_requested   = false;
    EXECUTION_OPERATION_ADAPTER_ResetFailure();
    EXECUTION_OPERATION_ADAPTER_ResetTiming();
    return true;
}

void EXECUTION_MANAGER_RequestOperationTiming( void )
{
    operation_timing_requested = true;
}

void EXECUTION_MANAGER_Abort( void )
{
    execution_state = EXECUTION_STATE_IDLE;
}

uint32_t EXECUTION_MANAGER_GetCurrentTick( void )
{
    return current_tick;
}

ExecutionManagerFailure_T EXECUTION_MANAGER_GetFailure( void )
{
    return execution_failure;
}

ExecutionManagerTickResult_T
EXECUTION_MANAGER_ProcessTickFromISR( BaseType_t* higher_priority_task_woken )
{
    const FlashManagerInstructionView_T* instruction = NULL;

    if ( execution_state == EXECUTION_STATE_COMPLETE )
    {
        return EXECUTION_MANAGER_TICK_COMPLETE;
    }

    if ( execution_state == EXECUTION_STATE_FAILED )
    {
        return EXECUTION_MANAGER_TICK_FAILED;
    }

    if ( execution_state != EXECUTION_STATE_READY )
    {
        return EXECUTION_MANAGER_FailFromISR( EXECUTION_MANAGER_FAILURE_NOT_PREPARED,
                                              higher_priority_task_woken );
    }

    /*
     * Tick zero is the configured initial condition at the instant the
     * execution timer starts. Each interrupt marks the next execution-clock
     * boundary, so the first interrupt processes tick one.
     *
     * Future measurement collection belongs immediately after this increment
     * and before output dispatch. Keeping the tick stable for the remainder of
     * the ISR gives every driver call at one boundary the same timestamp.
     */
    current_tick++;

    FlashManagerInstructionReadStatus_T read_status = FLASH_MANAGER_INSTRUCTION_END_OF_STREAM;

    /* Measurements are captured before outputs at this boundary. */
    if ( !EXECUTION_MEASUREMENT_ADAPTER_ApplyMeasurements( current_tick,
                                                           higher_priority_task_woken ) )
    {
        return EXECUTION_MANAGER_FailFromISR( EXECUTION_MANAGER_FAILURE_MEASUREMENT_REJECTED,
                                              higher_priority_task_woken );
    }

    /* Process outputs */
    if ( !instruction_stream_exhausted )
    {
        read_status = FLASH_MANAGER_PeekNextInstructionFromISR( &instruction );
    }

    if ( read_status == FLASH_MANAGER_INSTRUCTION_AVAILABLE )
    {
        if ( instruction->header.timestamp < current_tick )
        {
            return EXECUTION_MANAGER_FailFromISR( EXECUTION_MANAGER_FAILURE_INSTRUCTION_LATE,
                                                  higher_priority_task_woken );
        }

        if ( instruction->header.timestamp == current_tick )
        {
            const ExecutionOperationAdapterResult_T operation_result =
                operation_timing_active
                    ? EXECUTION_OPERATION_ADAPTER_ApplyOperationsProfiled(
                          instruction->operations, instruction->header.operation_count )
                    : EXECUTION_OPERATION_ADAPTER_ApplyOperations(
                          instruction->operations, instruction->header.operation_count );
            if ( operation_result != EXECUTION_OPERATION_ADAPTER_ACCEPTED )
            {
                return EXECUTION_MANAGER_FailFromISR( EXECUTION_MANAGER_FAILURE_OPERATION_REJECTED,
                                                      higher_priority_task_woken );
            }

            if ( !FLASH_MANAGER_ConsumeInstructionFromISR( higher_priority_task_woken ) )
            {
                return EXECUTION_MANAGER_FailFromISR( EXECUTION_MANAGER_FAILURE_INSTRUCTION_CONSUME,
                                                      higher_priority_task_woken );
            }
        }
    }
    else if ( read_status == FLASH_MANAGER_INSTRUCTION_NOT_BUFFERED )
    {
        return EXECUTION_MANAGER_FailFromISR( EXECUTION_MANAGER_FAILURE_INSTRUCTION_UNDERRUN,
                                              higher_priority_task_woken );
    }
    else if ( read_status != FLASH_MANAGER_INSTRUCTION_END_OF_STREAM )
    {
        return EXECUTION_MANAGER_FailFromISR( EXECUTION_MANAGER_FAILURE_INSTRUCTION_CORRUPT,
                                              higher_priority_task_woken );
    }
    else
    {
        instruction_stream_exhausted = true;
    }

    if ( current_tick == execution_tick_count )
    {
        execution_state = EXECUTION_STATE_COMPLETE;
        if ( terminal_callback != NULL )
        {
            terminal_callback( EXECUTION_MANAGER_TICK_COMPLETE, EXECUTION_MANAGER_FAILURE_NONE,
                               higher_priority_task_woken );
        }
        return EXECUTION_MANAGER_TICK_COMPLETE;
    }

    return EXECUTION_MANAGER_TICK_CONTINUE;
}
