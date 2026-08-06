/******************************************************************************
 *  File:       test_flash_manager.cpp
 *  Author:     Callum Rafferty
 *  Created:    25-Mar-2026
 *
 *  Description:
 *      Unit tests for Flash Manager initialisation, ISR-facing result record
 *      production, asynchronous lifecycle requests, NAND page draining,
 *      partial-page finalisation, and fault handling.
 *
 *  Notes:
 *      Production code is included directly so private state and drain helpers
 *      can be reset and inspected, following the existing project convention.
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>

extern "C"
{
#include "external_flash.h"
#include "flash_manager.h"
#include "result_buffer.h"
#include <stdint.h>
#include <stdbool.h>
}

#include "flash_manager_mocks.h"

static constexpr uint32_t TEST_PAGE_SIZE_BYTES = 32U;
static constexpr uint16_t TEST_FULL_PAGE_PAYLOAD_BYTES =
    static_cast<uint16_t>( TEST_PAGE_SIZE_BYTES - sizeof( FlashManagerResultHeader_T ) );
static constexpr uint16_t TEST_PARTIAL_PAYLOAD_BYTES = 4U;

static TaskHandle_t const TEST_FLASH_MANAGER_TASK_HANDLE =
    reinterpret_cast<TaskHandle_t>( 0x1234U );

static SemaphoreHandle_t semaphore_create_result = reinterpret_cast<SemaphoreHandle_t>( 0x5678U );
static BaseType_t        semaphore_take_result   = pdTRUE;
static BaseType_t        semaphore_give_result   = pdTRUE;
static uint32_t          semaphore_create_calls  = 0U;
static uint32_t          semaphore_take_calls    = 0U;
static uint32_t          semaphore_give_calls    = 0U;

static TaskHandle_t current_task_handle = TEST_FLASH_MANAGER_TASK_HANDLE;

static BaseType_t    notify_result      = pdPASS;
static uint32_t      notify_calls       = 0U;
static TaskHandle_t  notify_task_handle = nullptr;
static uint32_t      notify_value       = 0U;
static eNotifyAction notify_action      = eNoAction;

static BaseType_t    notify_from_isr_result            = pdPASS;
static BaseType_t    notify_from_isr_wake_value        = pdFALSE;
static bool          notify_from_isr_writes_wake_value = false;
static uint32_t      notify_from_isr_calls             = 0U;
static TaskHandle_t  notify_from_isr_task_handle       = nullptr;
static uint32_t      notify_from_isr_value             = 0U;
static eNotifyAction notify_from_isr_action            = eNoAction;
static BaseType_t*   notify_from_isr_wake_pointer      = nullptr;

static ExternalFlashStatus_T write_result_page_status = EXTERNAL_FLASH_STATUS_OK;
static uint32_t              write_result_page_calls  = 0U;
static const uint8_t*        write_result_page_data   = nullptr;
static uint32_t              write_result_page_length = 0U;

static ExternalFlashStatus_T start_session_status = EXTERNAL_FLASH_STATUS_OK;
static uint32_t              start_session_calls  = 0U;

extern "C" SemaphoreHandle_t xSemaphoreCreateMutexStatic( StaticSemaphore_t* mutex_buffer )
{
    semaphore_create_calls++;

    if ( mutex_buffer == nullptr )
    {
        return nullptr;
    }

    return semaphore_create_result;
}

extern "C" BaseType_t xSemaphoreTake( SemaphoreHandle_t semaphore, TickType_t ticks_to_wait )
{
    ( void )semaphore;
    ( void )ticks_to_wait;
    semaphore_take_calls++;
    return semaphore_take_result;
}

extern "C" BaseType_t xSemaphoreGive( SemaphoreHandle_t semaphore )
{
    ( void )semaphore;
    semaphore_give_calls++;
    return semaphore_give_result;
}

extern "C" TaskHandle_t xTaskGetCurrentTaskHandle( void )
{
    return current_task_handle;
}

extern "C" BaseType_t xTaskNotifyWait( uint32_t bits_to_clear_on_entry,
                                       uint32_t bits_to_clear_on_exit, uint32_t* notification_value,
                                       TickType_t ticks_to_wait )
{
    ( void )bits_to_clear_on_entry;
    ( void )bits_to_clear_on_exit;
    ( void )notification_value;
    ( void )ticks_to_wait;
    return pdFAIL;
}

extern "C" void vTaskDelay( TickType_t ticks_to_delay )
{
    ( void )ticks_to_delay;
}

extern "C" BaseType_t xTaskNotify( TaskHandle_t task_to_notify, uint32_t value,
                                   eNotifyAction action )
{
    notify_calls++;
    notify_task_handle = task_to_notify;
    notify_value       = value;
    notify_action      = action;
    return notify_result;
}

extern "C" BaseType_t xTaskNotifyFromISR( TaskHandle_t task_to_notify, uint32_t value,
                                          eNotifyAction action,
                                          BaseType_t*   higher_priority_task_woken )
{
    notify_from_isr_calls++;
    notify_from_isr_task_handle  = task_to_notify;
    notify_from_isr_value        = value;
    notify_from_isr_action       = action;
    notify_from_isr_wake_pointer = higher_priority_task_woken;

    if ( ( higher_priority_task_woken != nullptr ) && notify_from_isr_writes_wake_value )
    {
        *higher_priority_task_woken = notify_from_isr_wake_value;
    }

    return notify_from_isr_result;
}

extern "C" ExternalFlashStatus_T EXTERNAL_FLASH_WriteResultPage( const uint8_t* data,
                                                                 uint32_t       valid_length )
{
    write_result_page_calls++;
    write_result_page_data   = data;
    write_result_page_length = valid_length;
    return write_result_page_status;
}

extern "C" ExternalFlashStatus_T EXTERNAL_FLASH_StartSession( void )
{
    start_session_calls++;
    return start_session_status;
}

extern "C"
{
#include "../flash_manager.c" /* Private module under test */  // NOLINT
}

/**-----------------------------------------------------------------------------
 *  Test Fixture
 *------------------------------------------------------------------------------
 */

/**
 * @brief Test fixture for module tests.
 *
 * Provides a consistent setup/teardown environment for all test cases.
 */
class FlashManagerTest : public ::testing::Test
{
protected:
    void SetUp( void ) override
    {
        std::memset( &flash_manager_context, 0, sizeof( flash_manager_context ) );
        std::memset( &flash_manager_mutex_storage, 0, sizeof( flash_manager_mutex_storage ) );

        flash_manager_context.state = FLASH_MANAGER_STATE_UNINITIALISED;

        FLASH_MANAGER_TEST_ConfigureExternalFlashInfo( EXTERNAL_FLASH_STATUS_OK,
                                                       TEST_PAGE_SIZE_BYTES );

        semaphore_create_result = reinterpret_cast<SemaphoreHandle_t>( 0x5678U );
        semaphore_take_result   = pdTRUE;
        semaphore_give_result   = pdTRUE;
        semaphore_create_calls  = 0U;
        semaphore_take_calls    = 0U;
        semaphore_give_calls    = 0U;

        current_task_handle = TEST_FLASH_MANAGER_TASK_HANDLE;

        notify_result      = pdPASS;
        notify_calls       = 0U;
        notify_task_handle = nullptr;
        notify_value       = 0U;
        notify_action      = eNoAction;

        notify_from_isr_result            = pdPASS;
        notify_from_isr_wake_value        = pdFALSE;
        notify_from_isr_writes_wake_value = false;
        notify_from_isr_calls             = 0U;
        notify_from_isr_task_handle       = nullptr;
        notify_from_isr_value             = 0U;
        notify_from_isr_action            = eNoAction;
        notify_from_isr_wake_pointer      = nullptr;

        write_result_page_status = EXTERNAL_FLASH_STATUS_OK;
        write_result_page_calls  = 0U;
        write_result_page_data   = nullptr;
        write_result_page_length = 0U;

        start_session_status = EXTERNAL_FLASH_STATUS_OK;
        start_session_calls  = 0U;
    }

    void Initialise( void )
    {
        ASSERT_TRUE( FLASH_MANAGER_Init() );
    }

    void EnterExecutingState( void )
    {
        flash_manager_context.state       = FLASH_MANAGER_STATE_EXECUTING;
        flash_manager_context.task_handle = TEST_FLASH_MANAGER_TASK_HANDLE;
    }

    void RegisterTask( void )
    {
        flash_manager_context.task_handle = TEST_FLASH_MANAGER_TASK_HANDLE;
    }

    static FlashManagerResultWriteLease_T ReserveRecord( uint16_t payload_capacity_bytes )
    {
        FlashManagerResultWriteLease_T lease = {};
        EXPECT_TRUE( FLASH_MANAGER_ReserveResultRecordFromISR( payload_capacity_bytes, &lease ) );
        return lease;
    }

    static void CreateReadyPage( void )
    {
        FlashManagerResultWriteLease_T lease = {};
        ASSERT_TRUE( RESULT_BUFFER_ReserveRecord( TEST_FULL_PAGE_PAYLOAD_BYTES, &lease ) );
        std::memset( lease.payload, 0xA5, TEST_FULL_PAGE_PAYLOAD_BYTES );
        ASSERT_EQ( RESULT_BUFFER_RECORD_COMMIT_PAGE_READY_TO_DRAIN,
                   RESULT_BUFFER_CommitRecord( &lease, 1U, 2U, 3U, TEST_FULL_PAGE_PAYLOAD_BYTES ) );
    }
};

TEST_F( FlashManagerTest, InitCreatesMutexInitialisesBufferAndEntersIdle )
{
    EXPECT_TRUE( FLASH_MANAGER_Init() );

    EXPECT_EQ( 1U, semaphore_create_calls );
    EXPECT_EQ( semaphore_create_result, flash_manager_context.access_mutex );
    EXPECT_EQ( FLASH_MANAGER_STATE_IDLE, flash_manager_context.state );
    EXPECT_EQ( nullptr, flash_manager_context.task_handle );
}

TEST_F( FlashManagerTest, InitEntersFaultWhenResultBufferInitialisationFails )
{
    FLASH_MANAGER_TEST_ConfigureExternalFlashInfo( EXTERNAL_FLASH_STATUS_ERROR,
                                                   TEST_PAGE_SIZE_BYTES );

    EXPECT_FALSE( FLASH_MANAGER_Init() );
    EXPECT_EQ( FLASH_MANAGER_STATE_FAULT, flash_manager_context.state );
    EXPECT_NE( nullptr, flash_manager_context.access_mutex );
}

TEST_F( FlashManagerTest, InitFailsWhenMutexCreationFails )
{
    semaphore_create_result = nullptr;

    EXPECT_FALSE( FLASH_MANAGER_Init() );
    EXPECT_EQ( FLASH_MANAGER_STATE_UNINITIALISED, flash_manager_context.state );
    EXPECT_EQ( nullptr, flash_manager_context.access_mutex );
}

TEST_F( FlashManagerTest, InitRejectsReinitialisation )
{
    Initialise();

    EXPECT_FALSE( FLASH_MANAGER_Init() );
    EXPECT_EQ( 1U, semaphore_create_calls );
    EXPECT_EQ( FLASH_MANAGER_STATE_IDLE, flash_manager_context.state );
}

TEST_F( FlashManagerTest, GetStateReportsCurrentStateAndRejectsNullDestination )
{
    Initialise();
    FlashManagerState_T state = FLASH_MANAGER_STATE_UNINITIALISED;

    EXPECT_TRUE( FLASH_MANAGER_GetState( &state ) );
    EXPECT_EQ( FLASH_MANAGER_STATE_IDLE, state );
    EXPECT_FALSE( FLASH_MANAGER_GetState( nullptr ) );
}

TEST_F( FlashManagerTest, PreparationRequestRejectsUnavailableManagerAndTask )
{
    EXPECT_EQ( FLASH_MANAGER_REQUEST_NOT_INITIALISED, FLASH_MANAGER_RequestExecutionPreparation() );

    Initialise();
    EXPECT_EQ( FLASH_MANAGER_REQUEST_TASK_NOT_READY, FLASH_MANAGER_RequestExecutionPreparation() );
    EXPECT_EQ( FLASH_MANAGER_STATE_IDLE, flash_manager_context.state );
    EXPECT_EQ( 0U, notify_calls );
}

TEST_F( FlashManagerTest, PreparationRequestRejectsInvalidLifecycleState )
{
    Initialise();
    EnterExecutingState();

    EXPECT_EQ( FLASH_MANAGER_REQUEST_INVALID_STATE, FLASH_MANAGER_RequestExecutionPreparation() );
    EXPECT_EQ( FLASH_MANAGER_STATE_EXECUTING, flash_manager_context.state );
    EXPECT_EQ( 0U, notify_calls );
}

TEST_F( FlashManagerTest, PreparationRequestChangesStateAndNotifiesTask )
{
    Initialise();
    RegisterTask();

    EXPECT_EQ( FLASH_MANAGER_REQUEST_OK, FLASH_MANAGER_RequestExecutionPreparation() );
    EXPECT_EQ( FLASH_MANAGER_STATE_PREPARING_EXECUTION, flash_manager_context.state );
    EXPECT_EQ( 1U, notify_calls );
    EXPECT_EQ( TEST_FLASH_MANAGER_TASK_HANDLE, notify_task_handle );
    EXPECT_EQ( FLASH_MANAGER_NOTIFY_PREPARE_EXECUTION, notify_value );
    EXPECT_EQ( eSetBits, notify_action );
}

TEST_F( FlashManagerTest, PreparationNotificationFailureEntersFault )
{
    Initialise();
    RegisterTask();
    notify_result = pdFAIL;

    EXPECT_EQ( FLASH_MANAGER_REQUEST_NOTIFY_FAILED, FLASH_MANAGER_RequestExecutionPreparation() );
    EXPECT_EQ( FLASH_MANAGER_STATE_FAULT, flash_manager_context.state );
    EXPECT_EQ( 1U, notify_calls );
}

TEST_F( FlashManagerTest, PreparationHandlerStartsSessionResetsBufferAndEntersExecuting )
{
    Initialise();
    CreateReadyPage();
    RegisterTask();
    ASSERT_EQ( FLASH_MANAGER_REQUEST_OK, FLASH_MANAGER_RequestExecutionPreparation() );

    EXPECT_TRUE( FLASH_MANAGER_PrepareExecution() );
    EXPECT_EQ( 1U, start_session_calls );
    EXPECT_EQ( FLASH_MANAGER_STATE_EXECUTING, flash_manager_context.state );

    ResultBufferDrainLease_T drain_lease = {};
    EXPECT_FALSE( RESULT_BUFFER_AcquireDrainPage( &drain_lease ) );
}

TEST_F( FlashManagerTest, PreparationHandlerRejectsStaleNotificationWithoutStartingSession )
{
    Initialise();

    EXPECT_FALSE( FLASH_MANAGER_PrepareExecution() );
    EXPECT_EQ( 0U, start_session_calls );
    EXPECT_EQ( FLASH_MANAGER_STATE_IDLE, flash_manager_context.state );
}

TEST_F( FlashManagerTest, PreparationHandlerReportsSessionStartFailure )
{
    Initialise();
    RegisterTask();
    ASSERT_EQ( FLASH_MANAGER_REQUEST_OK, FLASH_MANAGER_RequestExecutionPreparation() );
    start_session_status = EXTERNAL_FLASH_STATUS_ERASE_FAIL;

    EXPECT_FALSE( FLASH_MANAGER_PrepareExecution() );
    EXPECT_EQ( 1U, start_session_calls );
    EXPECT_EQ( FLASH_MANAGER_STATE_PREPARING_EXECUTION, flash_manager_context.state );

    FLASH_MANAGER_EnterFault();
    EXPECT_EQ( FLASH_MANAGER_STATE_FAULT, flash_manager_context.state );
}

TEST_F( FlashManagerTest, FinalisationRequestRejectsInvalidStateAndMissingTask )
{
    EXPECT_EQ( FLASH_MANAGER_REQUEST_NOT_INITIALISED, FLASH_MANAGER_RequestResultFinalisation() );

    Initialise();

    EXPECT_EQ( FLASH_MANAGER_REQUEST_INVALID_STATE, FLASH_MANAGER_RequestResultFinalisation() );

    flash_manager_context.state = FLASH_MANAGER_STATE_EXECUTING;
    EXPECT_EQ( FLASH_MANAGER_REQUEST_TASK_NOT_READY, FLASH_MANAGER_RequestResultFinalisation() );
    EXPECT_EQ( FLASH_MANAGER_STATE_EXECUTING, flash_manager_context.state );
    EXPECT_EQ( 0U, notify_calls );
}

TEST_F( FlashManagerTest, FinalisationRequestChangesStateAndNotifiesTask )
{
    Initialise();
    EnterExecutingState();

    EXPECT_EQ( FLASH_MANAGER_REQUEST_OK, FLASH_MANAGER_RequestResultFinalisation() );
    EXPECT_EQ( FLASH_MANAGER_STATE_FINALISING_RESULTS, flash_manager_context.state );
    EXPECT_EQ( 1U, notify_calls );
    EXPECT_EQ( TEST_FLASH_MANAGER_TASK_HANDLE, notify_task_handle );
    EXPECT_EQ( FLASH_MANAGER_NOTIFY_FINALISE_RESULTS, notify_value );
    EXPECT_EQ( eSetBits, notify_action );
}

TEST_F( FlashManagerTest, FinalisationNotificationFailureEntersFault )
{
    Initialise();
    EnterExecutingState();
    notify_result = pdFAIL;

    EXPECT_EQ( FLASH_MANAGER_REQUEST_NOTIFY_FAILED, FLASH_MANAGER_RequestResultFinalisation() );
    EXPECT_EQ( FLASH_MANAGER_STATE_FAULT, flash_manager_context.state );
    EXPECT_EQ( 1U, notify_calls );
}

TEST_F( FlashManagerTest, EmptyResultFinalisationEntersResultsReadyWithoutNandWrite )
{
    Initialise();
    EnterExecutingState();
    ASSERT_EQ( FLASH_MANAGER_REQUEST_OK, FLASH_MANAGER_RequestResultFinalisation() );

    EXPECT_TRUE( FLASH_MANAGER_FinaliseResults() );
    EXPECT_EQ( 0U, write_result_page_calls );
    EXPECT_EQ( FLASH_MANAGER_STATE_RESULTS_READY, flash_manager_context.state );
    EXPECT_TRUE( RESULT_BUFFER_IsDrainComplete() );
}

TEST_F( FlashManagerTest, PageAlignedFinalisationWritesNoAdditionalPartialPage )
{
    Initialise();
    EnterExecutingState();
    CreateReadyPage();
    ASSERT_EQ( FLASH_MANAGER_REQUEST_OK, FLASH_MANAGER_RequestResultFinalisation() );

    EXPECT_TRUE( FLASH_MANAGER_FinaliseResults() );
    EXPECT_EQ( 1U, write_result_page_calls );
    EXPECT_EQ( TEST_PAGE_SIZE_BYTES, write_result_page_length );
    EXPECT_EQ( FLASH_MANAGER_STATE_RESULTS_READY, flash_manager_context.state );
    EXPECT_TRUE( RESULT_BUFFER_IsDrainComplete() );
}

TEST_F( FlashManagerTest, PartialResultFinalisationWritesOnlyCommittedRecordBytes )
{
    Initialise();
    EnterExecutingState();
    FlashManagerResultWriteLease_T lease = ReserveRecord( TEST_PARTIAL_PAYLOAD_BYTES );
    ASSERT_NE( nullptr, lease.payload );
    std::memset( lease.payload, 0xC3, TEST_PARTIAL_PAYLOAD_BYTES );
    ASSERT_EQ( FLASH_MANAGER_RESULT_COMMIT_OK,
               FLASH_MANAGER_CommitResultRecordFromISR( &lease, 10U, 2U, 3U,
                                                        TEST_PARTIAL_PAYLOAD_BYTES, nullptr ) );
    ASSERT_EQ( FLASH_MANAGER_REQUEST_OK, FLASH_MANAGER_RequestResultFinalisation() );

    EXPECT_TRUE( FLASH_MANAGER_FinaliseResults() );
    EXPECT_EQ( 1U, write_result_page_calls );
    EXPECT_EQ( sizeof( FlashManagerResultHeader_T ) + TEST_PARTIAL_PAYLOAD_BYTES,
               write_result_page_length );
    EXPECT_EQ( FLASH_MANAGER_STATE_RESULTS_READY, flash_manager_context.state );
    EXPECT_TRUE( RESULT_BUFFER_IsDrainComplete() );
}

TEST_F( FlashManagerTest, FinalisationRejectsActiveResultWriteLease )
{
    Initialise();
    EnterExecutingState();
    FlashManagerResultWriteLease_T lease = ReserveRecord( TEST_PARTIAL_PAYLOAD_BYTES );
    ASSERT_NE( nullptr, lease.payload );
    ASSERT_EQ( FLASH_MANAGER_REQUEST_OK, FLASH_MANAGER_RequestResultFinalisation() );

    EXPECT_FALSE( FLASH_MANAGER_FinaliseResults() );
    EXPECT_EQ( 0U, write_result_page_calls );
    EXPECT_EQ( FLASH_MANAGER_STATE_FINALISING_RESULTS, flash_manager_context.state );

    FLASH_MANAGER_EnterFault();
    EXPECT_EQ( FLASH_MANAGER_STATE_FAULT, flash_manager_context.state );
}

TEST_F( FlashManagerTest, FinalisationRejectsStaleNotificationWithoutMutatingBuffer )
{
    Initialise();

    EXPECT_FALSE( FLASH_MANAGER_FinaliseResults() );
    EXPECT_EQ( 0U, write_result_page_calls );
    EXPECT_EQ( FLASH_MANAGER_STATE_IDLE, flash_manager_context.state );
    EXPECT_FALSE( RESULT_BUFFER_IsDrainComplete() );
}

TEST_F( FlashManagerTest, FinalisationReportsNandFailureForTaskFaultHandling )
{
    Initialise();
    EnterExecutingState();
    FlashManagerResultWriteLease_T lease = ReserveRecord( TEST_PARTIAL_PAYLOAD_BYTES );
    ASSERT_NE( nullptr, lease.payload );
    ASSERT_EQ( FLASH_MANAGER_RESULT_COMMIT_OK,
               FLASH_MANAGER_CommitResultRecordFromISR( &lease, 10U, 2U, 3U,
                                                        TEST_PARTIAL_PAYLOAD_BYTES, nullptr ) );
    ASSERT_EQ( FLASH_MANAGER_REQUEST_OK, FLASH_MANAGER_RequestResultFinalisation() );
    write_result_page_status = EXTERNAL_FLASH_STATUS_PROGRAM_FAIL;

    EXPECT_FALSE( FLASH_MANAGER_FinaliseResults() );
    EXPECT_EQ( 1U, write_result_page_calls );
    EXPECT_EQ( FLASH_MANAGER_STATE_FINALISING_RESULTS, flash_manager_context.state );

    FLASH_MANAGER_EnterFault();
    EXPECT_EQ( FLASH_MANAGER_STATE_FAULT, flash_manager_context.state );
}

TEST_F( FlashManagerTest, ExecutionSessionFlowsFromPreparationThroughPartialFinalisation )
{
    Initialise();
    RegisterTask();
    ASSERT_EQ( FLASH_MANAGER_REQUEST_OK, FLASH_MANAGER_RequestExecutionPreparation() );
    ASSERT_TRUE( FLASH_MANAGER_PrepareExecution() );

    FlashManagerResultWriteLease_T lease = ReserveRecord( TEST_PARTIAL_PAYLOAD_BYTES );
    ASSERT_NE( nullptr, lease.payload );
    std::memset( lease.payload, 0x7E, TEST_PARTIAL_PAYLOAD_BYTES );
    ASSERT_EQ( FLASH_MANAGER_RESULT_COMMIT_OK,
               FLASH_MANAGER_CommitResultRecordFromISR( &lease, 50U, 4U, 5U,
                                                        TEST_PARTIAL_PAYLOAD_BYTES, nullptr ) );

    ASSERT_EQ( FLASH_MANAGER_REQUEST_OK, FLASH_MANAGER_RequestResultFinalisation() );
    ASSERT_TRUE( FLASH_MANAGER_FinaliseResults() );

    EXPECT_EQ( 1U, start_session_calls );
    EXPECT_EQ( 2U, notify_calls );
    EXPECT_EQ( 1U, write_result_page_calls );
    EXPECT_EQ( sizeof( FlashManagerResultHeader_T ) + TEST_PARTIAL_PAYLOAD_BYTES,
               write_result_page_length );
    EXPECT_EQ( FLASH_MANAGER_STATE_RESULTS_READY, flash_manager_context.state );
}

TEST_F( FlashManagerTest, ReserveClearsLeaseAndRejectsNonExecutingState )
{
    Initialise();
    FlashManagerResultWriteLease_T lease = {
        .payload                = reinterpret_cast<uint8_t*>( 0x1U ),
        .lease_id               = 42U,
        .payload_capacity_bytes = 12U,
    };

    EXPECT_FALSE( FLASH_MANAGER_ReserveResultRecordFromISR( 4U, &lease ) );
    EXPECT_EQ( nullptr, lease.payload );
    EXPECT_EQ( 0U, lease.lease_id );
    EXPECT_EQ( 0U, lease.payload_capacity_bytes );
}

TEST_F( FlashManagerTest, ReserveAndCancelForwardToResultBufferWhileExecuting )
{
    Initialise();
    EnterExecutingState();

    FlashManagerResultWriteLease_T lease = ReserveRecord( TEST_PARTIAL_PAYLOAD_BYTES );
    ASSERT_NE( nullptr, lease.payload );
    EXPECT_EQ( TEST_PARTIAL_PAYLOAD_BYTES, lease.payload_capacity_bytes );
    EXPECT_TRUE( FLASH_MANAGER_CancelResultRecordFromISR( &lease ) );
    EXPECT_FALSE( FLASH_MANAGER_CancelResultRecordFromISR( &lease ) );
}

TEST_F( FlashManagerTest, CommitRejectsNonExecutingStateWithoutNotification )
{
    Initialise();
    FlashManagerResultWriteLease_T lease      = {};
    BaseType_t                     task_woken = pdFALSE;

    EXPECT_EQ( FLASH_MANAGER_RESULT_COMMIT_INVALID_STATE,
               FLASH_MANAGER_CommitResultRecordFromISR( &lease, 1U, 2U, 3U, 0U, &task_woken ) );
    EXPECT_EQ( 0U, notify_from_isr_calls );
}

TEST_F( FlashManagerTest, PartialRecordCommitDoesNotNotifyDrainTask )
{
    Initialise();
    EnterExecutingState();
    FlashManagerResultWriteLease_T lease = ReserveRecord( TEST_PARTIAL_PAYLOAD_BYTES );
    ASSERT_NE( nullptr, lease.payload );
    std::memset( lease.payload, 0x3C, TEST_PARTIAL_PAYLOAD_BYTES );
    BaseType_t task_woken = pdFALSE;

    EXPECT_EQ( FLASH_MANAGER_RESULT_COMMIT_OK,
               FLASH_MANAGER_CommitResultRecordFromISR( &lease, 100U, 4U, 5U,
                                                        TEST_PARTIAL_PAYLOAD_BYTES, &task_woken ) );
    EXPECT_EQ( 0U, notify_from_isr_calls );
    EXPECT_EQ( pdFALSE, task_woken );
}

TEST_F( FlashManagerTest, CommitMapsInvalidLeaseAndPayloadOverflow )
{
    Initialise();
    EnterExecutingState();
    FlashManagerResultWriteLease_T invalid_lease = {};

    EXPECT_EQ( FLASH_MANAGER_RESULT_COMMIT_INVALID_LEASE,
               FLASH_MANAGER_CommitResultRecordFromISR( &invalid_lease, 1U, 2U, 3U, 0U, nullptr ) );

    FlashManagerResultWriteLease_T valid_lease = ReserveRecord( TEST_PARTIAL_PAYLOAD_BYTES );
    EXPECT_EQ( FLASH_MANAGER_RESULT_COMMIT_OVERFLOW,
               FLASH_MANAGER_CommitResultRecordFromISR(
                   &valid_lease, 1U, 2U, 3U,
                   static_cast<uint16_t>( TEST_PARTIAL_PAYLOAD_BYTES + 1U ), nullptr ) );

    /* An overflow leaves the reservation active so it can still be cancelled. */
    EXPECT_TRUE( FLASH_MANAGER_CancelResultRecordFromISR( &valid_lease ) );
    EXPECT_EQ( 0U, notify_from_isr_calls );
}

TEST_F( FlashManagerTest, FullPageCommitNotifiesDrainTaskAndPropagatesWakeFlag )
{
    Initialise();
    EnterExecutingState();
    FlashManagerResultWriteLease_T lease = ReserveRecord( TEST_FULL_PAGE_PAYLOAD_BYTES );
    ASSERT_NE( nullptr, lease.payload );
    std::memset( lease.payload, 0x5A, TEST_FULL_PAGE_PAYLOAD_BYTES );

    notify_from_isr_writes_wake_value = true;
    notify_from_isr_wake_value        = pdTRUE;
    BaseType_t task_woken             = pdFALSE;

    EXPECT_EQ( FLASH_MANAGER_RESULT_COMMIT_OK,
               FLASH_MANAGER_CommitResultRecordFromISR(
                   &lease, 100U, 4U, 5U, TEST_FULL_PAGE_PAYLOAD_BYTES, &task_woken ) );

    EXPECT_EQ( 1U, notify_from_isr_calls );
    EXPECT_EQ( TEST_FLASH_MANAGER_TASK_HANDLE, notify_from_isr_task_handle );
    EXPECT_EQ( FLASH_MANAGER_NOTIFY_DRAIN_RESULTS, notify_from_isr_value );
    EXPECT_EQ( eSetBits, notify_from_isr_action );
    EXPECT_EQ( &task_woken, notify_from_isr_wake_pointer );
    EXPECT_EQ( pdTRUE, task_woken );
    EXPECT_EQ( FLASH_MANAGER_STATE_EXECUTING, flash_manager_context.state );
}

TEST_F( FlashManagerTest, FullPageCommitAcceptsNullWakePointer )
{
    Initialise();
    EnterExecutingState();
    FlashManagerResultWriteLease_T lease = ReserveRecord( TEST_FULL_PAGE_PAYLOAD_BYTES );

    EXPECT_EQ( FLASH_MANAGER_RESULT_COMMIT_OK,
               FLASH_MANAGER_CommitResultRecordFromISR( &lease, 1U, 2U, 3U,
                                                        TEST_FULL_PAGE_PAYLOAD_BYTES, nullptr ) );
    EXPECT_EQ( 1U, notify_from_isr_calls );
    EXPECT_EQ( nullptr, notify_from_isr_wake_pointer );
}

TEST_F( FlashManagerTest, MissingDrainTaskHandleLatchesFaultAfterCommit )
{
    Initialise();
    EnterExecutingState();
    flash_manager_context.task_handle    = nullptr;
    FlashManagerResultWriteLease_T lease = ReserveRecord( TEST_FULL_PAGE_PAYLOAD_BYTES );

    EXPECT_EQ( FLASH_MANAGER_RESULT_COMMIT_INTERNAL_ERROR,
               FLASH_MANAGER_CommitResultRecordFromISR( &lease, 1U, 2U, 3U,
                                                        TEST_FULL_PAGE_PAYLOAD_BYTES, nullptr ) );
    EXPECT_EQ( FLASH_MANAGER_STATE_FAULT, flash_manager_context.state );
    EXPECT_EQ( 0U, notify_from_isr_calls );
}

TEST_F( FlashManagerTest, DrainNotificationFailureLatchesFaultAfterCommit )
{
    Initialise();
    EnterExecutingState();
    notify_from_isr_result               = pdFAIL;
    FlashManagerResultWriteLease_T lease = ReserveRecord( TEST_FULL_PAGE_PAYLOAD_BYTES );

    EXPECT_EQ( FLASH_MANAGER_RESULT_COMMIT_INTERNAL_ERROR,
               FLASH_MANAGER_CommitResultRecordFromISR( &lease, 1U, 2U, 3U,
                                                        TEST_FULL_PAGE_PAYLOAD_BYTES, nullptr ) );
    EXPECT_EQ( FLASH_MANAGER_STATE_FAULT, flash_manager_context.state );
    EXPECT_EQ( 1U, notify_from_isr_calls );
}

TEST_F( FlashManagerTest, DrainWorkerWritesEveryReadyPageAndCompletesLeases )
{
    Initialise();
    EnterExecutingState();
    CreateReadyPage();
    CreateReadyPage();

    EXPECT_TRUE( FLASH_MANAGER_DrainResultPages() );
    EXPECT_EQ( 2U, write_result_page_calls );
    EXPECT_NE( nullptr, write_result_page_data );
    EXPECT_EQ( TEST_PAGE_SIZE_BYTES, write_result_page_length );

    ResultBufferDrainLease_T drain_lease = {};
    EXPECT_FALSE( RESULT_BUFFER_AcquireDrainPage( &drain_lease ) );
}

TEST_F( FlashManagerTest, DrainWorkerAcceptsFinalisingResultsState )
{
    Initialise();
    EnterExecutingState();
    CreateReadyPage();
    flash_manager_context.state = FLASH_MANAGER_STATE_FINALISING_RESULTS;

    EXPECT_TRUE( FLASH_MANAGER_DrainResultPages() );
    EXPECT_EQ( 1U, write_result_page_calls );
}

TEST_F( FlashManagerTest, FailedNandWriteIsNotRetriedAfterEnteringFault )
{
    Initialise();
    EnterExecutingState();
    CreateReadyPage();
    write_result_page_status = EXTERNAL_FLASH_STATUS_TIMEOUT;

    ASSERT_FALSE( FLASH_MANAGER_DrainResultPages() );
    EXPECT_EQ( 1U, write_result_page_calls );

    FLASH_MANAGER_EnterFault();
    EXPECT_EQ( FLASH_MANAGER_STATE_FAULT, flash_manager_context.state );

    write_result_page_status = EXTERNAL_FLASH_STATUS_OK;
    EXPECT_TRUE( FLASH_MANAGER_DrainResultPages() );
    EXPECT_EQ( 1U, write_result_page_calls );

    flash_manager_context.state = FLASH_MANAGER_STATE_EXECUTING;
    EXPECT_TRUE( FLASH_MANAGER_DrainResultPages() );
    EXPECT_EQ( 2U, write_result_page_calls );
}
