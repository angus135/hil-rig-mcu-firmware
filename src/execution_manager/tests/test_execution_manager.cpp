#include <gtest/gtest.h>

extern "C"
{
#include "execution_manager.h"
#include "execution_manager_isr.h"
#include "execution_operation_adapters.h"
#include "flash_manager.h"
}

static FlashManagerInstructionReadStatus_T peek_status;
static FlashManagerInstructionView_T       instruction;
static ExecutionOperationAdapterResult_T   adapter_result;
static bool                                consume_result;
static uint32_t                            peek_calls;
static uint32_t                            adapter_calls;
static uint32_t                            consume_calls;
static uint32_t                            terminal_callback_calls;
static ExecutionManagerTickResult_T        terminal_callback_result;
static ExecutionManagerFailure_T           terminal_callback_failure;
static BaseType_t*                         terminal_callback_task_woken;
static BaseType_t*                         consume_task_woken;

static void TestTerminalCallback( ExecutionManagerTickResult_T result,
                                  ExecutionManagerFailure_T    failure,
                                  BaseType_t*                   higher_priority_task_woken )
{
    terminal_callback_calls++;
    terminal_callback_result  = result;
    terminal_callback_failure = failure;
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
    return consume_result;
}

extern "C" ExecutionOperationAdapterResult_T
EXECUTION_OPERATION_ADAPTER_ApplyOperations( const uint8_t* operations, uint8_t operation_count )
{
    ( void )operations;
    ( void )operation_count;
    adapter_calls++;
    return adapter_result;
}

class ExecutionManagerTest : public ::testing::Test
{
protected:
    uint8_t operations[12] = {};
    BaseType_t task_woken  = pdFALSE;

    ExecutionManagerTickResult_T ProcessTick()
    {
        return EXECUTION_MANAGER_ProcessTickFromISR( &task_woken );
    }

    void SetUp() override
    {
        EXECUTION_MANAGER_SetTerminalCallback( nullptr );
        ( void )EXECUTION_MANAGER_Prepare( 1U );
        EXECUTION_MANAGER_Abort();
        peek_status               = FLASH_MANAGER_INSTRUCTION_END_OF_STREAM;
        adapter_result            = EXECUTION_OPERATION_ADAPTER_ACCEPTED;
        consume_result            = true;
        peek_calls                = 0U;
        adapter_calls             = 0U;
        consume_calls             = 0U;
        terminal_callback_calls   = 0U;
        terminal_callback_result  = EXECUTION_MANAGER_TICK_CONTINUE;
        terminal_callback_failure = EXECUTION_MANAGER_FAILURE_NONE;
        terminal_callback_task_woken = nullptr;
        consume_task_woken           = nullptr;
        instruction               = {};
        instruction.operations    = operations;
    }
};

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
    EXPECT_EQ( EXECUTION_MANAGER_GetCurrentTick(), 1U );
    EXPECT_EQ( ProcessTick(), EXECUTION_MANAGER_TICK_COMPLETE );
    EXPECT_EQ( EXECUTION_MANAGER_GetCurrentTick(), 2U );
    EXPECT_EQ( EXECUTION_MANAGER_GetFailure(), EXECUTION_MANAGER_FAILURE_NONE );
    EXPECT_EQ( peek_calls, 1U );
}

TEST_F( ExecutionManagerTest, PrepareEstablishesTickZeroInitialCondition )
{
    ASSERT_TRUE( EXECUTION_MANAGER_Prepare( 2U ) );

    EXPECT_EQ( EXECUTION_MANAGER_GetCurrentTick(), 0U );
    EXPECT_EQ( ProcessTick(), EXECUTION_MANAGER_TICK_CONTINUE );
    EXPECT_EQ( EXECUTION_MANAGER_GetCurrentTick(), 1U );
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

TEST_F( ExecutionManagerTest, DueInstructionIsAppliedThenConsumed )
{
    instruction.header.timestamp       = 1U;
    instruction.header.operation_count = 1U;
    peek_status                        = FLASH_MANAGER_INSTRUCTION_AVAILABLE;
    ASSERT_TRUE( EXECUTION_MANAGER_Prepare( 1U ) );

    EXPECT_EQ( ProcessTick(), EXECUTION_MANAGER_TICK_COMPLETE );
    EXPECT_EQ( adapter_calls, 1U );
    EXPECT_EQ( consume_calls, 1U );
    EXPECT_EQ( consume_task_woken, &task_woken );
}

TEST_F( ExecutionManagerTest, LateInstructionFailsAndIsNotConsumed )
{
    instruction.header.timestamp = 2U;
    peek_status                  = FLASH_MANAGER_INSTRUCTION_AVAILABLE;
    ASSERT_TRUE( EXECUTION_MANAGER_Prepare( 3U ) );
    EXPECT_EQ( ProcessTick(), EXECUTION_MANAGER_TICK_CONTINUE );

    instruction.header.timestamp = 1U;

    EXPECT_EQ( ProcessTick(), EXECUTION_MANAGER_TICK_FAILED );
    EXPECT_EQ( EXECUTION_MANAGER_GetFailure(), EXECUTION_MANAGER_FAILURE_INSTRUCTION_LATE );
    EXPECT_EQ( EXECUTION_MANAGER_GetCurrentTick(), 2U );
    EXPECT_EQ( consume_calls, 0U );
}

TEST_F( ExecutionManagerTest, TimestampZeroIsLateAtFirstExecutionBoundary )
{
    instruction.header.timestamp = 0U;
    peek_status                  = FLASH_MANAGER_INSTRUCTION_AVAILABLE;
    ASSERT_TRUE( EXECUTION_MANAGER_Prepare( 1U ) );

    EXPECT_EQ( ProcessTick(), EXECUTION_MANAGER_TICK_FAILED );
    EXPECT_EQ( EXECUTION_MANAGER_GetFailure(), EXECUTION_MANAGER_FAILURE_INSTRUCTION_LATE );
    EXPECT_EQ( EXECUTION_MANAGER_GetCurrentTick(), 1U );
    EXPECT_EQ( adapter_calls, 0U );
    EXPECT_EQ( consume_calls, 0U );
}

TEST_F( ExecutionManagerTest, InstructionUnderrunFailsTick )
{
    peek_status = FLASH_MANAGER_INSTRUCTION_NOT_BUFFERED;
    ASSERT_TRUE( EXECUTION_MANAGER_Prepare( 1U ) );

    EXPECT_EQ( ProcessTick(), EXECUTION_MANAGER_TICK_FAILED );
    EXPECT_EQ( EXECUTION_MANAGER_GetFailure(), EXECUTION_MANAGER_FAILURE_INSTRUCTION_UNDERRUN );
    EXPECT_EQ( EXECUTION_MANAGER_GetCurrentTick(), 1U );
}

TEST_F( ExecutionManagerTest, CorruptInstructionFailsTick )
{
    peek_status = FLASH_MANAGER_INSTRUCTION_CORRUPT;
    ASSERT_TRUE( EXECUTION_MANAGER_Prepare( 1U ) );

    EXPECT_EQ( ProcessTick(), EXECUTION_MANAGER_TICK_FAILED );
    EXPECT_EQ( EXECUTION_MANAGER_GetFailure(), EXECUTION_MANAGER_FAILURE_INSTRUCTION_CORRUPT );
    EXPECT_EQ( EXECUTION_MANAGER_GetCurrentTick(), 1U );
}

TEST_F( ExecutionManagerTest, RejectedOperationLeavesInstructionUnconsumed )
{
    instruction.header.timestamp = 1U;
    peek_status                  = FLASH_MANAGER_INSTRUCTION_AVAILABLE;
    adapter_result               = EXECUTION_OPERATION_ADAPTER_REJECTED;
    ASSERT_TRUE( EXECUTION_MANAGER_Prepare( 1U ) );

    EXPECT_EQ( ProcessTick(), EXECUTION_MANAGER_TICK_FAILED );
    EXPECT_EQ( EXECUTION_MANAGER_GetFailure(), EXECUTION_MANAGER_FAILURE_OPERATION_REJECTED );
    EXPECT_EQ( consume_calls, 0U );
}

TEST_F( ExecutionManagerTest, ConsumeFailureIsReported )
{
    instruction.header.timestamp = 1U;
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
    EXPECT_EQ( EXECUTION_MANAGER_GetCurrentTick(), 1U );
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
