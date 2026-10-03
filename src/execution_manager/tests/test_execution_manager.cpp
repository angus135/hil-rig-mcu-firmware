#include <gtest/gtest.h>

extern "C"
{
#include "execution_manager.h"
#include "execution_manager_isr.h"
#include "execution_operation_adapters.h"
#include "flash_manager.h"
}

static bool     measurement_result;
static uint32_t measurement_calls;
static uint32_t profiled_measurement_calls;
static uint32_t measurement_timestamp;
static uint32_t measurement_order;
static uint32_t operation_order;
static uint32_t call_order;

extern "C" bool
EXECUTION_MEASUREMENT_ADAPTER_ApplyMeasurements( uint32_t    timestamp,
                                                 BaseType_t* higher_priority_task_woken )
{
    ( void )higher_priority_task_woken;
    measurement_calls++;
    measurement_timestamp = timestamp;
    measurement_order     = ++call_order;
    return measurement_result;
}

extern "C" bool
EXECUTION_MEASUREMENT_ADAPTER_ApplyMeasurementsProfiled( uint32_t    timestamp,
                                                         BaseType_t* higher_priority_task_woken )
{
    profiled_measurement_calls++;
    return EXECUTION_MEASUREMENT_ADAPTER_ApplyMeasurements( timestamp, higher_priority_task_woken );
}

extern "C" void EXECUTION_MEASUREMENT_ADAPTER_ResetTiming( void )
{
}

extern "C" void
EXECUTION_MEASUREMENT_ADAPTER_Prepare( const ExecutionMeasurementConfiguration_T* configuration )
{
    ( void )configuration;
}

static FlashManagerInstructionReadStatus_T peek_status;
static FlashManagerInstructionView_T       instruction;
static ExecutionOperationAdapterResult_T   adapter_result;
static bool                                consume_result;
static uint32_t                            peek_calls;
static uint32_t                            adapter_calls;
static uint32_t                            profiled_adapter_calls;
static uint32_t                            consume_calls;
static uint32_t                            terminal_callback_calls;
static ExecutionManagerTickResult_T        terminal_callback_result;
static ExecutionManagerFailure_T           terminal_callback_failure;
static BaseType_t*                         terminal_callback_task_woken;
static BaseType_t*                         consume_task_woken;

static void TestTerminalCallback( ExecutionManagerTickResult_T result,
                                  ExecutionManagerFailure_T    failure,
                                  BaseType_t*                  higher_priority_task_woken )
{
    terminal_callback_calls++;
    terminal_callback_result     = result;
    terminal_callback_failure    = failure;
    terminal_callback_task_woken = higher_priority_task_woken;
}

extern "C" FlashManagerInstructionReadStatus_T
FLASH_MANAGER_PeekNextInstructionFromISR( const FlashManagerInstructionView_T** view )
{
    peek_calls++;
    if ( peek_status == FLASH_MANAGER_INSTRUCTION_AVAILABLE )
    {
        *view = &instruction;
    }
    return peek_status;
}

extern "C" bool FLASH_MANAGER_ConsumeInstructionFromISR( BaseType_t* task_woken )
{
    consume_task_woken = task_woken;
    consume_calls++;
    if ( consume_result )
    {
        peek_status = FLASH_MANAGER_INSTRUCTION_END_OF_STREAM;
    }
    return consume_result;
}

extern "C" void FLASH_MANAGER_RecordInstructionOccupancyFromISR( uint32_t boundary )
{
    ( void )boundary;
}

extern "C" ExecutionOperationAdapterResult_T
EXECUTION_OPERATION_ADAPTER_ApplyOperations( const uint8_t* operations, uint8_t operation_count )
{
    ( void )operations;
    ( void )operation_count;
    adapter_calls++;
    operation_order = ++call_order;
    return adapter_result;
}

extern "C" ExecutionOperationAdapterResult_T
EXECUTION_OPERATION_ADAPTER_ApplyOperationsProfiled( const uint8_t* operations,
                                                     uint8_t        operation_count )
{
    ( void )operations;
    ( void )operation_count;
    profiled_adapter_calls++;
    operation_order = ++call_order;
    return adapter_result;
}

extern "C" void EXECUTION_OPERATION_ADAPTER_ResetFailure( void )
{
}

extern "C" void EXECUTION_OPERATION_ADAPTER_ResetTiming( void )
{
}

extern "C" bool
EXECUTION_OPERATION_ADAPTER_GetFailure( ExecutionOperationAdapterFailure_T* failure )
{
    ( void )failure;
    return false;
}

class ExecutionManagerTest : public ::testing::Test
{
protected:
    uint8_t    operations[12] = {};
    BaseType_t task_woken     = pdFALSE;

    ExecutionManagerTickResult_T ProcessTick()
    {
        return EXECUTION_MANAGER_ProcessTickFromISR( &task_woken );
    }

    void SetUp() override
    {
        EXECUTION_MANAGER_SetTerminalCallback( nullptr );
        ( void )EXECUTION_MANAGER_Prepare( 1U );
        EXECUTION_MANAGER_Abort();
        peek_status                  = FLASH_MANAGER_INSTRUCTION_END_OF_STREAM;
        adapter_result               = EXECUTION_OPERATION_ADAPTER_ACCEPTED;
        consume_result               = true;
        peek_calls                   = 0U;
        adapter_calls                = 0U;
        profiled_adapter_calls       = 0U;
        consume_calls                = 0U;
        terminal_callback_calls      = 0U;
        terminal_callback_result     = EXECUTION_MANAGER_TICK_CONTINUE;
        terminal_callback_failure    = EXECUTION_MANAGER_FAILURE_NONE;
        terminal_callback_task_woken = nullptr;
        consume_task_woken           = nullptr;
        measurement_result           = true;
        measurement_calls            = 0U;
        profiled_measurement_calls   = 0U;
        measurement_timestamp        = 0U;
        measurement_order            = 0U;
        operation_order              = 0U;
        call_order                   = 0U;
        instruction                  = {};
        instruction.operations       = operations;
    }
};

TEST_F( ExecutionManagerTest, RequestedOperationTimingProfilesNextPreparedRunOnly )
{
    EXECUTION_MANAGER_RequestOperationTiming();
    ASSERT_TRUE( EXECUTION_MANAGER_Prepare( 1U ) );
    peek_status                        = FLASH_MANAGER_INSTRUCTION_AVAILABLE;
    instruction.header.timestamp       = 0U;
    instruction.header.operation_count = 1U;

    EXPECT_EQ( ProcessTick(), EXECUTION_MANAGER_TICK_CONTINUE );
    EXPECT_EQ( profiled_adapter_calls, 1U );
    EXPECT_EQ( adapter_calls, 0U );
    EXPECT_EQ( ProcessTick(), EXECUTION_MANAGER_TICK_COMPLETE );

    EXECUTION_MANAGER_Abort();
    ASSERT_TRUE( EXECUTION_MANAGER_Prepare( 1U ) );
    peek_status = FLASH_MANAGER_INSTRUCTION_AVAILABLE;
    EXPECT_EQ( ProcessTick(), EXECUTION_MANAGER_TICK_CONTINUE );
    EXPECT_EQ( profiled_adapter_calls, 1U );
    EXPECT_EQ( adapter_calls, 1U );
    EXPECT_EQ( ProcessTick(), EXECUTION_MANAGER_TICK_COMPLETE );
}

TEST_F( ExecutionManagerTest, PrepareRejectsZeroTicks )
{
    EXPECT_FALSE( EXECUTION_MANAGER_Prepare( 0U ) );
}

TEST_F( ExecutionManagerTest, TickWithoutPreparationFailsWithoutReadingFlash )
{
    EXPECT_EQ( ProcessTick(), EXECUTION_MANAGER_TICK_FAILED );
    EXPECT_EQ( EXECUTION_MANAGER_GetFailure(), EXECUTION_MANAGER_FAILURE_NOT_PREPARED );
    EXPECT_EQ( peek_calls, 0U );
}

TEST_F( ExecutionManagerTest, EmptyInstructionStreamStillCompletesConfiguredTicks )
{
    ASSERT_TRUE( EXECUTION_MANAGER_Prepare( 2U ) );

    EXPECT_EQ( ProcessTick(), EXECUTION_MANAGER_TICK_CONTINUE );
    EXPECT_EQ( EXECUTION_MANAGER_GetCurrentTick(), 0U );
    EXPECT_EQ( ProcessTick(), EXECUTION_MANAGER_TICK_CONTINUE );
    EXPECT_EQ( EXECUTION_MANAGER_GetCurrentTick(), 1U );
    EXPECT_EQ( ProcessTick(), EXECUTION_MANAGER_TICK_COMPLETE );
    EXPECT_EQ( EXECUTION_MANAGER_GetCurrentTick(), 2U );
    EXPECT_EQ( EXECUTION_MANAGER_GetFailure(), EXECUTION_MANAGER_FAILURE_NONE );
    EXPECT_EQ( peek_calls, 1U );
}

TEST_F( ExecutionManagerTest, FirstInterruptProcessesBoundaryZeroWithoutMeasurement )
{
    ASSERT_TRUE( EXECUTION_MANAGER_Prepare( 2U ) );

    EXPECT_EQ( EXECUTION_MANAGER_GetCurrentTick(), 0U );
    EXPECT_EQ( ProcessTick(), EXECUTION_MANAGER_TICK_CONTINUE );
    EXPECT_EQ( EXECUTION_MANAGER_GetCurrentTick(), 0U );
    EXPECT_EQ( measurement_calls, 0U );
}

TEST_F( ExecutionManagerTest, FutureInstructionRemainsUnconsumed )
{
    instruction.header.timestamp = 2U;
    peek_status                  = FLASH_MANAGER_INSTRUCTION_AVAILABLE;
    ASSERT_TRUE( EXECUTION_MANAGER_Prepare( 2U ) );

    EXPECT_EQ( ProcessTick(), EXECUTION_MANAGER_TICK_CONTINUE );
    EXPECT_EQ( adapter_calls, 0U );
    EXPECT_EQ( consume_calls, 0U );
}

TEST_F( ExecutionManagerTest, BoundaryZeroInstructionIsAppliedBeforeFirstMeasurement )
{
    instruction.header.timestamp       = 0U;
    instruction.header.operation_count = 1U;
    peek_status                        = FLASH_MANAGER_INSTRUCTION_AVAILABLE;
    ASSERT_TRUE( EXECUTION_MANAGER_Prepare( 1U ) );

    EXPECT_EQ( ProcessTick(), EXECUTION_MANAGER_TICK_CONTINUE );
    EXPECT_EQ( adapter_calls, 1U );
    EXPECT_EQ( consume_calls, 1U );
    EXPECT_EQ( consume_task_woken, &task_woken );
    EXPECT_EQ( measurement_calls, 0U );

    EXPECT_EQ( ProcessTick(), EXECUTION_MANAGER_TICK_COMPLETE );
    EXPECT_EQ( measurement_timestamp, 1U );
    EXPECT_LT( operation_order, measurement_order );
}

TEST_F( ExecutionManagerTest, RejectedMeasurementFailsBeforeReadingOrApplyingInstruction )
{
    instruction.header.timestamp       = 1U;
    instruction.header.operation_count = 1U;
    peek_status                        = FLASH_MANAGER_INSTRUCTION_AVAILABLE;
    measurement_result                 = false;
    ASSERT_TRUE( EXECUTION_MANAGER_Prepare( 1U ) );

    EXPECT_EQ( ProcessTick(), EXECUTION_MANAGER_TICK_CONTINUE );
    EXPECT_EQ( ProcessTick(), EXECUTION_MANAGER_TICK_FAILED );
    EXPECT_EQ( EXECUTION_MANAGER_GetFailure(), EXECUTION_MANAGER_FAILURE_MEASUREMENT_REJECTED );
    EXPECT_EQ( measurement_calls, 1U );
    EXPECT_EQ( peek_calls, 1U );
    EXPECT_EQ( adapter_calls, 0U );
    EXPECT_EQ( consume_calls, 0U );
}

TEST_F( ExecutionManagerTest, LateInstructionFailsAndIsNotConsumed )
{
    instruction.header.timestamp = 2U;
    peek_status                  = FLASH_MANAGER_INSTRUCTION_AVAILABLE;
    ASSERT_TRUE( EXECUTION_MANAGER_Prepare( 3U ) );
    EXPECT_EQ( ProcessTick(), EXECUTION_MANAGER_TICK_CONTINUE );
    EXPECT_EQ( ProcessTick(), EXECUTION_MANAGER_TICK_CONTINUE );

    instruction.header.timestamp = 0U;

    EXPECT_EQ( ProcessTick(), EXECUTION_MANAGER_TICK_FAILED );
    EXPECT_EQ( EXECUTION_MANAGER_GetFailure(), EXECUTION_MANAGER_FAILURE_INSTRUCTION_LATE );
    EXPECT_EQ( EXECUTION_MANAGER_GetCurrentTick(), 2U );
    EXPECT_EQ( consume_calls, 0U );
}

TEST_F( ExecutionManagerTest, TimestampZeroIsDueAtFirstExecutionBoundary )
{
    instruction.header.timestamp       = 0U;
    instruction.header.operation_count = 1U;
    peek_status                        = FLASH_MANAGER_INSTRUCTION_AVAILABLE;
    ASSERT_TRUE( EXECUTION_MANAGER_Prepare( 1U ) );

    EXPECT_EQ( ProcessTick(), EXECUTION_MANAGER_TICK_CONTINUE );
    EXPECT_EQ( EXECUTION_MANAGER_GetFailure(), EXECUTION_MANAGER_FAILURE_NONE );
    EXPECT_EQ( EXECUTION_MANAGER_GetCurrentTick(), 0U );
    EXPECT_EQ( measurement_calls, 0U );
    EXPECT_EQ( adapter_calls, 1U );
    EXPECT_EQ( consume_calls, 1U );
}

TEST_F( ExecutionManagerTest, InstructionUnderrunFailsTick )
{
    peek_status = FLASH_MANAGER_INSTRUCTION_NOT_BUFFERED;
    ASSERT_TRUE( EXECUTION_MANAGER_Prepare( 1U ) );

    EXPECT_EQ( ProcessTick(), EXECUTION_MANAGER_TICK_FAILED );
    EXPECT_EQ( EXECUTION_MANAGER_GetFailure(), EXECUTION_MANAGER_FAILURE_INSTRUCTION_UNDERRUN );
    EXPECT_EQ( EXECUTION_MANAGER_GetCurrentTick(), 0U );
}

TEST_F( ExecutionManagerTest, CorruptInstructionFailsTick )
{
    peek_status = FLASH_MANAGER_INSTRUCTION_CORRUPT;
    ASSERT_TRUE( EXECUTION_MANAGER_Prepare( 1U ) );

    EXPECT_EQ( ProcessTick(), EXECUTION_MANAGER_TICK_FAILED );
    EXPECT_EQ( EXECUTION_MANAGER_GetFailure(), EXECUTION_MANAGER_FAILURE_INSTRUCTION_CORRUPT );
    EXPECT_EQ( EXECUTION_MANAGER_GetCurrentTick(), 0U );
}

TEST_F( ExecutionManagerTest, RejectedOperationLeavesInstructionUnconsumed )
{
    instruction.header.timestamp = 0U;
    peek_status                  = FLASH_MANAGER_INSTRUCTION_AVAILABLE;
    adapter_result               = EXECUTION_OPERATION_ADAPTER_REJECTED;
    ASSERT_TRUE( EXECUTION_MANAGER_Prepare( 1U ) );

    EXPECT_EQ( ProcessTick(), EXECUTION_MANAGER_TICK_FAILED );
    EXPECT_EQ( EXECUTION_MANAGER_GetFailure(), EXECUTION_MANAGER_FAILURE_OPERATION_REJECTED );
    EXPECT_EQ( consume_calls, 0U );
}

TEST_F( ExecutionManagerTest, ConsumeFailureIsReported )
{
    instruction.header.timestamp = 0U;
    peek_status                  = FLASH_MANAGER_INSTRUCTION_AVAILABLE;
    consume_result               = false;
    ASSERT_TRUE( EXECUTION_MANAGER_Prepare( 1U ) );

    EXPECT_EQ( ProcessTick(), EXECUTION_MANAGER_TICK_FAILED );
    EXPECT_EQ( EXECUTION_MANAGER_GetFailure(), EXECUTION_MANAGER_FAILURE_INSTRUCTION_CONSUME );
}

TEST_F( ExecutionManagerTest, CompletionRemainsLatchedIfAnotherInterruptArrives )
{
    EXECUTION_MANAGER_SetTerminalCallback( TestTerminalCallback );
    ASSERT_TRUE( EXECUTION_MANAGER_Prepare( 1U ) );
    ASSERT_EQ( ProcessTick(), EXECUTION_MANAGER_TICK_CONTINUE );
    ASSERT_EQ( ProcessTick(), EXECUTION_MANAGER_TICK_COMPLETE );
    uint32_t reads_at_completion = peek_calls;

    EXPECT_EQ( ProcessTick(), EXECUTION_MANAGER_TICK_COMPLETE );
    EXPECT_EQ( EXECUTION_MANAGER_GetFailure(), EXECUTION_MANAGER_FAILURE_NONE );
    EXPECT_EQ( peek_calls, reads_at_completion );
    EXPECT_EQ( terminal_callback_calls, 1U );
    EXPECT_EQ( terminal_callback_result, EXECUTION_MANAGER_TICK_COMPLETE );
    EXPECT_EQ( terminal_callback_failure, EXECUTION_MANAGER_FAILURE_NONE );
    EXPECT_EQ( terminal_callback_task_woken, &task_woken );
}

TEST_F( ExecutionManagerTest, FailureRemainsLatchedIfAnotherInterruptArrives )
{
    EXECUTION_MANAGER_SetTerminalCallback( TestTerminalCallback );
    peek_status = FLASH_MANAGER_INSTRUCTION_NOT_BUFFERED;
    ASSERT_TRUE( EXECUTION_MANAGER_Prepare( 1U ) );
    ASSERT_EQ( ProcessTick(), EXECUTION_MANAGER_TICK_FAILED );
    uint32_t reads_at_failure = peek_calls;

    EXPECT_EQ( ProcessTick(), EXECUTION_MANAGER_TICK_FAILED );
    EXPECT_EQ( EXECUTION_MANAGER_GetCurrentTick(), 0U );
    EXPECT_EQ( peek_calls, reads_at_failure );
    EXPECT_EQ( terminal_callback_calls, 1U );
    EXPECT_EQ( terminal_callback_result, EXECUTION_MANAGER_TICK_FAILED );
    EXPECT_EQ( terminal_callback_failure, EXECUTION_MANAGER_FAILURE_INSTRUCTION_UNDERRUN );
    EXPECT_EQ( terminal_callback_task_woken, &task_woken );
}

TEST_F( ExecutionManagerTest, PrepareClearsPreviousFailure )
{
    EXPECT_EQ( ProcessTick(), EXECUTION_MANAGER_TICK_FAILED );
    ASSERT_TRUE( EXECUTION_MANAGER_Prepare( 1U ) );

    EXPECT_EQ( EXECUTION_MANAGER_GetFailure(), EXECUTION_MANAGER_FAILURE_NONE );
    EXPECT_EQ( EXECUTION_MANAGER_GetCurrentTick(), 0U );
}

TEST_F( ExecutionManagerTest, UnconsumedFutureInstructionFailsAtCompletion )
{
    instruction.header.timestamp = 2U;
    peek_status                  = FLASH_MANAGER_INSTRUCTION_AVAILABLE;
    ASSERT_TRUE( EXECUTION_MANAGER_Prepare( 1U ) );

    EXPECT_EQ( ProcessTick(), EXECUTION_MANAGER_TICK_CONTINUE );
    EXPECT_EQ( ProcessTick(), EXECUTION_MANAGER_TICK_FAILED );
    EXPECT_EQ( EXECUTION_MANAGER_GetFailure(), EXECUTION_MANAGER_FAILURE_INSTRUCTION_UNCONSUMED );
    EXPECT_EQ( EXECUTION_MANAGER_GetCurrentTick(), 1U );
    EXPECT_EQ( adapter_calls, 0U );
    EXPECT_EQ( consume_calls, 0U );
}

TEST_F( ExecutionManagerTest, ExhaustedStreamAtFinalTickCompletesNormally )
{
    instruction.header.timestamp       = 1U;
    instruction.header.operation_count = 1U;
    peek_status                        = FLASH_MANAGER_INSTRUCTION_AVAILABLE;
    ASSERT_TRUE( EXECUTION_MANAGER_Prepare( 2U ) );

    EXPECT_EQ( ProcessTick(), EXECUTION_MANAGER_TICK_CONTINUE );
    EXPECT_EQ( adapter_calls, 0U );
    EXPECT_EQ( consume_calls, 0U );

    EXPECT_EQ( ProcessTick(), EXECUTION_MANAGER_TICK_CONTINUE );
    EXPECT_EQ( adapter_calls, 1U );
    EXPECT_EQ( consume_calls, 1U );

    peek_status = FLASH_MANAGER_INSTRUCTION_END_OF_STREAM;

    EXPECT_EQ( ProcessTick(), EXECUTION_MANAGER_TICK_COMPLETE );
    EXPECT_EQ( EXECUTION_MANAGER_GetFailure(), EXECUTION_MANAGER_FAILURE_NONE );
}

TEST_F( ExecutionManagerTest, CompletedBoundaryTracksSuccessfulExecutionBoundaries )
{
    uint32_t boundary = 999U;
    EXPECT_FALSE( EXECUTION_MANAGER_GetLastCompletedBoundary( nullptr ) );
    EXPECT_FALSE( EXECUTION_MANAGER_GetLastCompletedBoundary( &boundary ) );

    ASSERT_TRUE( EXECUTION_MANAGER_Prepare( 2U ) );
    EXPECT_FALSE( EXECUTION_MANAGER_GetLastCompletedBoundary( &boundary ) );

    EXPECT_EQ( ProcessTick(), EXECUTION_MANAGER_TICK_CONTINUE );
    EXPECT_TRUE( EXECUTION_MANAGER_GetLastCompletedBoundary( &boundary ) );
    EXPECT_EQ( 0U, boundary );

    EXPECT_EQ( ProcessTick(), EXECUTION_MANAGER_TICK_CONTINUE );
    EXPECT_TRUE( EXECUTION_MANAGER_GetLastCompletedBoundary( &boundary ) );
    EXPECT_EQ( 1U, boundary );

    EXPECT_EQ( ProcessTick(), EXECUTION_MANAGER_TICK_COMPLETE );
    EXPECT_TRUE( EXECUTION_MANAGER_GetLastCompletedBoundary( &boundary ) );
    EXPECT_EQ( 2U, boundary );
}

TEST_F( ExecutionManagerTest, FailedBoundaryDoesNotAdvanceCompletedBoundary )
{
    uint32_t boundary = 999U;
    ASSERT_TRUE( EXECUTION_MANAGER_Prepare( 2U ) );

    EXPECT_EQ( ProcessTick(), EXECUTION_MANAGER_TICK_CONTINUE );
    EXPECT_TRUE( EXECUTION_MANAGER_GetLastCompletedBoundary( &boundary ) );
    EXPECT_EQ( 0U, boundary );

    measurement_result = false;
    EXPECT_EQ( ProcessTick(), EXECUTION_MANAGER_TICK_FAILED );
    EXPECT_TRUE( EXECUTION_MANAGER_GetLastCompletedBoundary( &boundary ) );
    EXPECT_EQ( 0U, boundary );
}
