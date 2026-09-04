/******************************************************************************
 *  File:       test_flash_manager.cpp
 *  Author:     Callum Rafferty
 *  Created:    25-Mar-2026
 *
 *  Description:
 *      Unit tests for Flash Manager initialisation, ISR-facing result record
 *      production, cached instruction serving and refill, streamed instruction
 *      upload, asynchronous lifecycle requests, NAND page draining/refilling,
 *      partial-page finalisation, copied result retrieval, and faults.
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

#include <array>
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
static constexpr uint16_t TEST_PARTIAL_PAYLOAD_BYTES         = 4U;
static constexpr uint32_t TEST_INSTRUCTION_CAPACITY_BYTES    = TEST_PAGE_SIZE_BYTES * 8U;
static constexpr uint32_t TEST_MAX_INSTRUCTION_READS         = 8U;
static constexpr uint32_t TEST_INSTRUCTION_BUFFER_PAGE_COUNT = 3U;
static constexpr uint32_t TEST_RESULT_CAPACITY_BYTES         = TEST_PAGE_SIZE_BYTES * 8U;
static constexpr uint32_t TEST_MAX_RESULT_READS              = 8U;

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

static uint32_t fault_callback_calls;
static bool     fault_callback_from_isr;

static void TestFaultCallback( bool from_isr )
{
    fault_callback_calls++;
    fault_callback_from_isr = from_isr;
}

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

static ExternalFlashStatus_T start_instruction_upload_status = EXTERNAL_FLASH_STATUS_OK;
static uint32_t              start_instruction_upload_calls  = 0U;
static uint32_t              start_instruction_upload_length = 0U;

static ExternalFlashStatus_T write_instruction_page_status = EXTERNAL_FLASH_STATUS_OK;
static uint32_t              write_instruction_page_calls  = 0U;
static uint32_t              write_instruction_page_lengths[TEST_MAX_INSTRUCTION_READS]      = {};
static uint8_t write_instruction_page_data[TEST_MAX_INSTRUCTION_READS][TEST_PAGE_SIZE_BYTES] = {};

static ExternalFlashStatus_T finish_instruction_upload_status = EXTERNAL_FLASH_STATUS_OK;
static uint32_t              finish_instruction_upload_calls  = 0U;

static ExternalFlashStatus_T read_instruction_page_status = EXTERNAL_FLASH_STATUS_OK;
static uint32_t              read_instruction_page_calls  = 0U;
static uint32_t              read_instruction_page_offsets[TEST_MAX_INSTRUCTION_READS] = {};
static uint32_t              read_instruction_page_lengths[TEST_MAX_INSTRUCTION_READS] = {};
static uint8_t               instruction_image[TEST_INSTRUCTION_CAPACITY_BYTES]        = {};

static ExternalFlashStatus_T read_result_page_status = EXTERNAL_FLASH_STATUS_OK;
static uint32_t              read_result_page_calls  = 0U;
static uint32_t              read_result_page_offsets[TEST_MAX_RESULT_READS] = {};
static uint32_t              read_result_page_lengths[TEST_MAX_RESULT_READS] = {};
static uint8_t               result_image[TEST_RESULT_CAPACITY_BYTES]        = {};

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

extern "C" ExternalFlashStatus_T EXTERNAL_FLASH_StartInstructionUpload( uint32_t expected_length )
{
    start_instruction_upload_calls++;
    start_instruction_upload_length = expected_length;
    return start_instruction_upload_status;
}

extern "C" ExternalFlashStatus_T EXTERNAL_FLASH_WriteInstructionPage( const uint8_t* data,
                                                                      uint32_t       valid_length )
{
    if ( write_instruction_page_calls < TEST_MAX_INSTRUCTION_READS )
    {
        write_instruction_page_lengths[write_instruction_page_calls] = valid_length;

        if ( ( data != nullptr ) && ( valid_length <= TEST_PAGE_SIZE_BYTES ) )
        {
            std::memcpy( write_instruction_page_data[write_instruction_page_calls], data,
                         valid_length );
        }
    }

    write_instruction_page_calls++;
    return write_instruction_page_status;
}

extern "C" ExternalFlashStatus_T EXTERNAL_FLASH_FinishInstructionUpload( void )
{
    finish_instruction_upload_calls++;
    return finish_instruction_upload_status;
}

extern "C" ExternalFlashStatus_T EXTERNAL_FLASH_ReadInstructionPage( uint32_t offset, uint8_t* data,
                                                                     uint32_t length )
{
    if ( read_instruction_page_calls < TEST_MAX_INSTRUCTION_READS )
    {
        read_instruction_page_offsets[read_instruction_page_calls] = offset;
        read_instruction_page_lengths[read_instruction_page_calls] = length;
    }

    read_instruction_page_calls++;

    if ( read_instruction_page_status != EXTERNAL_FLASH_STATUS_OK )
    {
        return read_instruction_page_status;
    }

    std::memcpy( data, &instruction_image[offset], length );

    return EXTERNAL_FLASH_STATUS_OK;
}

extern "C" ExternalFlashStatus_T EXTERNAL_FLASH_ReadResultPage( uint32_t offset, uint8_t* data,
                                                                uint32_t length )
{
    if ( read_result_page_calls < TEST_MAX_RESULT_READS )
    {
        read_result_page_offsets[read_result_page_calls] = offset;
        read_result_page_lengths[read_result_page_calls] = length;
    }

    read_result_page_calls++;

    if ( read_result_page_status != EXTERNAL_FLASH_STATUS_OK )
    {
        return read_result_page_status;
    }

    std::memcpy( data, &result_image[offset], length );

    return EXTERNAL_FLASH_STATUS_OK;
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
#include "../flash_manager.c" /* Private module under test */  // NOLINT
#if defined( __GNUC__ )
#pragma GCC diagnostic pop
#endif
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
        flash_manager_fault_callback = nullptr;

        flash_manager_context.state = FLASH_MANAGER_STATE_UNINITIALISED;

        FLASH_MANAGER_TEST_ConfigureInstructionFlashInfo(
            EXTERNAL_FLASH_STATUS_OK, TEST_PAGE_SIZE_BYTES, TEST_INSTRUCTION_CAPACITY_BYTES );
        FLASH_MANAGER_TEST_SetInstructionLength( 0U );

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

        fault_callback_calls    = 0U;
        fault_callback_from_isr = false;

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

        start_instruction_upload_status = EXTERNAL_FLASH_STATUS_OK;
        start_instruction_upload_calls  = 0U;
        start_instruction_upload_length = 0U;

        write_instruction_page_status = EXTERNAL_FLASH_STATUS_OK;
        write_instruction_page_calls  = 0U;
        std::memset( write_instruction_page_lengths, 0, sizeof( write_instruction_page_lengths ) );
        std::memset( write_instruction_page_data, 0, sizeof( write_instruction_page_data ) );

        finish_instruction_upload_status = EXTERNAL_FLASH_STATUS_OK;
        finish_instruction_upload_calls  = 0U;

        read_instruction_page_status = EXTERNAL_FLASH_STATUS_OK;
        read_instruction_page_calls  = 0U;
        std::memset( read_instruction_page_offsets, 0, sizeof( read_instruction_page_offsets ) );
        std::memset( read_instruction_page_lengths, 0, sizeof( read_instruction_page_lengths ) );
        std::memset( instruction_image, 0, sizeof( instruction_image ) );

        read_result_page_status = EXTERNAL_FLASH_STATUS_OK;
        read_result_page_calls  = 0U;
        std::memset( read_result_page_offsets, 0, sizeof( read_result_page_offsets ) );
        std::memset( read_result_page_lengths, 0, sizeof( read_result_page_lengths ) );
        std::memset( result_image, 0, sizeof( result_image ) );
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

    void PrepareInstructionUpload( uint32_t expected_length_bytes )
    {
        Initialise();
        RegisterTask();
        ASSERT_EQ( FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_ACCEPTED,
                   FLASH_MANAGER_RequestInstructionUploadStart( expected_length_bytes ) );
        ASSERT_TRUE( FLASH_MANAGER_PrepareInstructionUpload() );

        notify_calls       = 0U;
        notify_task_handle = nullptr;
        notify_value       = 0U;
        notify_action      = eNoAction;
    }

    void PrepareCompletedResults( uint32_t result_length_bytes )
    {
        Initialise();
        RegisterTask();
        ASSERT_TRUE( RESULT_BUFFER_Finalise() );
        ASSERT_TRUE( RESULT_BUFFER_IsDrainComplete() );
        FLASH_MANAGER_TEST_SetResultLength( result_length_bytes );
        flash_manager_context.state = FLASH_MANAGER_STATE_RESULTS_READY;
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

    static void ConfigurePageAlignedInstructionImage( uint32_t page_count )
    {
        ASSERT_LE( page_count * TEST_PAGE_SIZE_BYTES, sizeof( instruction_image ) );

        for ( uint32_t page_index = 0U; page_index < page_count; page_index++ )
        {
            ExecutionInstructionHeader_T header = {
                page_index,
                static_cast<uint16_t>( TEST_PAGE_SIZE_BYTES
                                       - sizeof( ExecutionInstructionHeader_T ) ),
                1U,
                0U,
            };

            uint32_t page_offset_bytes = page_index * TEST_PAGE_SIZE_BYTES;
            std::memcpy( &instruction_image[page_offset_bytes], &header, sizeof( header ) );
            std::memset( &instruction_image[page_offset_bytes + sizeof( header )],
                         static_cast<int>( page_index ), header.operations_length_bytes );
        }

        FLASH_MANAGER_TEST_SetInstructionLength( page_count * TEST_PAGE_SIZE_BYTES );
    }

    static void FillBytes( uint8_t* data, uint32_t length, uint8_t seed )
    {
        for ( uint32_t index = 0U; index < length; index++ )
        {
            data[index] = static_cast<uint8_t>( seed + index );
        }
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

TEST_F( FlashManagerTest, InstructionUploadStartRejectsUnavailableManagerAndInvalidLength )
{
    EXPECT_EQ( FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_NOT_INITIALISED,
               FLASH_MANAGER_RequestInstructionUploadStart( TEST_PAGE_SIZE_BYTES ) );

    Initialise();
    RegisterTask();

    EXPECT_EQ( FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_INVALID_ARGUMENT,
               FLASH_MANAGER_RequestInstructionUploadStart( 0U ) );
    EXPECT_EQ( FLASH_MANAGER_STATE_IDLE, flash_manager_context.state );
    EXPECT_EQ( 0U, notify_calls );
}

TEST_F( FlashManagerTest, InstructionUploadStartRejectsInvalidStateAndMissingTask )
{
    Initialise();

    EXPECT_EQ( FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_TASK_NOT_READY,
               FLASH_MANAGER_RequestInstructionUploadStart( TEST_PAGE_SIZE_BYTES ) );
    EXPECT_EQ( FLASH_MANAGER_STATE_IDLE, flash_manager_context.state );

    RegisterTask();
    flash_manager_context.state = FLASH_MANAGER_STATE_EXECUTING;

    EXPECT_EQ( FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_INVALID_STATE,
               FLASH_MANAGER_RequestInstructionUploadStart( TEST_PAGE_SIZE_BYTES ) );
    EXPECT_EQ( FLASH_MANAGER_STATE_EXECUTING, flash_manager_context.state );
    EXPECT_EQ( 0U, notify_calls );
}

TEST_F( FlashManagerTest, InstructionUploadStartRejectsLengthBeyondInstructionPartition )
{
    Initialise();
    RegisterTask();

    EXPECT_EQ(
        FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_INVALID_ARGUMENT,
        FLASH_MANAGER_RequestInstructionUploadStart( TEST_INSTRUCTION_CAPACITY_BYTES + 1U ) );
    EXPECT_EQ( FLASH_MANAGER_STATE_IDLE, flash_manager_context.state );
    EXPECT_EQ( 0U, notify_calls );
}

TEST_F( FlashManagerTest, InstructionUploadStartPreparesBufferChangesStateAndNotifiesTask )
{
    constexpr uint32_t expected_length_bytes = TEST_PAGE_SIZE_BYTES + 5U;

    Initialise();
    RegisterTask();

    EXPECT_EQ( FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_ACCEPTED,
               FLASH_MANAGER_RequestInstructionUploadStart( expected_length_bytes ) );
    EXPECT_EQ( FLASH_MANAGER_STATE_PREPARING_INSTRUCTION_UPLOAD, flash_manager_context.state );
    EXPECT_EQ( 1U, notify_calls );
    EXPECT_EQ( TEST_FLASH_MANAGER_TASK_HANDLE, notify_task_handle );
    EXPECT_EQ( FLASH_MANAGER_NOTIFY_PREPARE_INSTRUCTION_UPLOAD, notify_value );
    EXPECT_EQ( eSetBits, notify_action );

    uint32_t prepared_length_bytes = 0U;
    ASSERT_TRUE( INSTRUCTION_BUFFER_GetUploadExpectedLength( &prepared_length_bytes ) );
    EXPECT_EQ( expected_length_bytes, prepared_length_bytes );
}

TEST_F( FlashManagerTest, InstructionUploadStartNotificationFailureEntersFault )
{
    Initialise();
    RegisterTask();
    notify_result = pdFAIL;

    EXPECT_EQ( FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_NOTIFY_FAILED,
               FLASH_MANAGER_RequestInstructionUploadStart( TEST_PAGE_SIZE_BYTES ) );
    EXPECT_EQ( FLASH_MANAGER_STATE_FAULT, flash_manager_context.state );
    EXPECT_EQ( 1U, notify_calls );
}

TEST_F( FlashManagerTest, InstructionUploadPreparationStartsNandUploadAndEntersUploadState )
{
    constexpr uint32_t expected_length_bytes = TEST_PAGE_SIZE_BYTES * 2U + 3U;

    Initialise();
    RegisterTask();
    ASSERT_EQ( FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_ACCEPTED,
               FLASH_MANAGER_RequestInstructionUploadStart( expected_length_bytes ) );

    EXPECT_TRUE( FLASH_MANAGER_PrepareInstructionUpload() );
    EXPECT_EQ( 1U, start_instruction_upload_calls );
    EXPECT_EQ( expected_length_bytes, start_instruction_upload_length );
    EXPECT_EQ( FLASH_MANAGER_STATE_INSTRUCTION_UPLOAD, flash_manager_context.state );
}

TEST_F( FlashManagerTest, InstructionUploadPreparationRejectsStaleNotification )
{
    Initialise();

    EXPECT_FALSE( FLASH_MANAGER_PrepareInstructionUpload() );
    EXPECT_EQ( 0U, start_instruction_upload_calls );
    EXPECT_EQ( FLASH_MANAGER_STATE_IDLE, flash_manager_context.state );
}

TEST_F( FlashManagerTest, InstructionUploadPreparationReportsNandPreparationFailure )
{
    Initialise();
    RegisterTask();
    ASSERT_EQ( FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_ACCEPTED,
               FLASH_MANAGER_RequestInstructionUploadStart( TEST_PAGE_SIZE_BYTES ) );
    start_instruction_upload_status = EXTERNAL_FLASH_STATUS_ERASE_FAIL;

    EXPECT_FALSE( FLASH_MANAGER_PrepareInstructionUpload() );
    EXPECT_EQ( 1U, start_instruction_upload_calls );
    EXPECT_EQ( TEST_PAGE_SIZE_BYTES, start_instruction_upload_length );
    EXPECT_EQ( FLASH_MANAGER_STATE_PREPARING_INSTRUCTION_UPLOAD, flash_manager_context.state );
}

TEST_F( FlashManagerTest, InstructionUploadSubmissionValidatesManagerStateAndArguments )
{
    std::array<uint8_t, 1U> data = { 0xA5U };

    EXPECT_EQ( FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_NOT_INITIALISED,
               FLASH_MANAGER_SubmitInstructionUploadBytes( data.data(), data.size() ) );

    Initialise();
    RegisterTask();

    EXPECT_EQ( FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_INVALID_ARGUMENT,
               FLASH_MANAGER_SubmitInstructionUploadBytes( nullptr, data.size() ) );
    EXPECT_EQ( FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_INVALID_ARGUMENT,
               FLASH_MANAGER_SubmitInstructionUploadBytes( data.data(), 0U ) );
    EXPECT_EQ( FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_INVALID_STATE,
               FLASH_MANAGER_SubmitInstructionUploadBytes( data.data(), data.size() ) );
}

TEST_F( FlashManagerTest, InstructionUploadSubmissionRejectsMissingTaskWithoutCopying )
{
    std::array<uint8_t, 1U> data = { 0xA5U };
    PrepareInstructionUpload( data.size() );
    flash_manager_context.task_handle = nullptr;

    EXPECT_EQ( FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_TASK_NOT_READY,
               FLASH_MANAGER_SubmitInstructionUploadBytes( data.data(), data.size() ) );
    EXPECT_FALSE( INSTRUCTION_BUFFER_IsUploadInputComplete() );
}

TEST_F( FlashManagerTest, InstructionUploadSubmissionRejectsChunkLargerThanOnePage )
{
    std::array<uint8_t, TEST_PAGE_SIZE_BYTES + 1U> data = {};
    PrepareInstructionUpload( data.size() );

    EXPECT_EQ( FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_INVALID_ARGUMENT,
               FLASH_MANAGER_SubmitInstructionUploadBytes( data.data(), data.size() ) );
    EXPECT_FALSE( INSTRUCTION_BUFFER_IsUploadInputComplete() );
    EXPECT_EQ( 0U, notify_calls );
}

TEST_F( FlashManagerTest, InstructionUploadSubmissionCopiesPartialChunkWithoutNotification )
{
    std::array<uint8_t, TEST_PARTIAL_PAYLOAD_BYTES> data = {};
    FillBytes( data.data(), data.size(), 0x10U );
    PrepareInstructionUpload( data.size() + 1U );

    EXPECT_EQ( FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_ACCEPTED,
               FLASH_MANAGER_SubmitInstructionUploadBytes( data.data(), data.size() ) );
    EXPECT_EQ( 0U, notify_calls );
    EXPECT_EQ( 0U, write_instruction_page_calls );
    EXPECT_FALSE( INSTRUCTION_BUFFER_IsUploadInputComplete() );
}

TEST_F( FlashManagerTest, InstructionUploadSubmissionNotifiesTaskWhenPageBecomesReady )
{
    std::array<uint8_t, TEST_PAGE_SIZE_BYTES> page = {};
    FillBytes( page.data(), page.size(), 0x20U );
    PrepareInstructionUpload( page.size() );

    EXPECT_EQ( FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_ACCEPTED,
               FLASH_MANAGER_SubmitInstructionUploadBytes( page.data(), page.size() ) );
    EXPECT_TRUE( INSTRUCTION_BUFFER_IsUploadInputComplete() );
    EXPECT_EQ( 1U, notify_calls );
    EXPECT_EQ( TEST_FLASH_MANAGER_TASK_HANDLE, notify_task_handle );
    EXPECT_EQ( FLASH_MANAGER_NOTIFY_DRAIN_INSTRUCTION_UPLOAD, notify_value );
    EXPECT_EQ( eSetBits, notify_action );
}

TEST_F( FlashManagerTest, InstructionUploadSubmissionNotificationFailureEntersFault )
{
    std::array<uint8_t, TEST_PAGE_SIZE_BYTES> page = {};
    PrepareInstructionUpload( page.size() );
    notify_result = pdFAIL;

    EXPECT_EQ( FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_NOTIFY_FAILED,
               FLASH_MANAGER_SubmitInstructionUploadBytes( page.data(), page.size() ) );
    EXPECT_EQ( FLASH_MANAGER_STATE_FAULT, flash_manager_context.state );
    EXPECT_TRUE( INSTRUCTION_BUFFER_IsUploadInputComplete() );
}

TEST_F( FlashManagerTest, InstructionUploadSubmissionAppliesAtomicBackpressureAndRetry )
{
    std::array<uint8_t, TEST_PAGE_SIZE_BYTES> page = {};
    PrepareInstructionUpload( page.size() * 4U );

    for ( uint8_t page_index = 0U; page_index < 3U; page_index++ )
    {
        FillBytes( page.data(), page.size(), page_index );
        ASSERT_EQ( FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_ACCEPTED,
                   FLASH_MANAGER_SubmitInstructionUploadBytes( page.data(), page.size() ) );
    }

    FillBytes( page.data(), page.size(), 0x80U );
    EXPECT_EQ( FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_BUSY,
               FLASH_MANAGER_SubmitInstructionUploadBytes( page.data(), page.size() ) );

    ASSERT_TRUE( FLASH_MANAGER_DrainInstructionUploadPages() );
    ASSERT_EQ( 3U, write_instruction_page_calls );

    EXPECT_EQ( FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_ACCEPTED,
               FLASH_MANAGER_SubmitInstructionUploadBytes( page.data(), page.size() ) );
    EXPECT_TRUE( INSTRUCTION_BUFFER_IsUploadInputComplete() );
}

TEST_F( FlashManagerTest, InstructionUploadDrainWritesReadyPagesInStreamOrder )
{
    std::array<uint8_t, TEST_PAGE_SIZE_BYTES> first_page  = {};
    std::array<uint8_t, TEST_PAGE_SIZE_BYTES> second_page = {};
    FillBytes( first_page.data(), first_page.size(), 0x11U );
    FillBytes( second_page.data(), second_page.size(), 0x55U );
    PrepareInstructionUpload( first_page.size() + second_page.size() );

    ASSERT_EQ( FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_ACCEPTED,
               FLASH_MANAGER_SubmitInstructionUploadBytes( first_page.data(), first_page.size() ) );
    ASSERT_EQ(
        FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_ACCEPTED,
        FLASH_MANAGER_SubmitInstructionUploadBytes( second_page.data(), second_page.size() ) );

    ASSERT_TRUE( FLASH_MANAGER_DrainInstructionUploadPages() );
    ASSERT_EQ( 2U, write_instruction_page_calls );
    EXPECT_EQ( TEST_PAGE_SIZE_BYTES, write_instruction_page_lengths[0] );
    EXPECT_EQ( TEST_PAGE_SIZE_BYTES, write_instruction_page_lengths[1] );
    EXPECT_EQ(
        0, std::memcmp( first_page.data(), write_instruction_page_data[0], first_page.size() ) );
    EXPECT_EQ(
        0, std::memcmp( second_page.data(), write_instruction_page_data[1], second_page.size() ) );
}

TEST_F( FlashManagerTest, InstructionUploadDrainFailureRetainsPageAndReportsFailure )
{
    std::array<uint8_t, TEST_PAGE_SIZE_BYTES> page = {};
    PrepareInstructionUpload( page.size() );
    ASSERT_EQ( FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_ACCEPTED,
               FLASH_MANAGER_SubmitInstructionUploadBytes( page.data(), page.size() ) );
    write_instruction_page_status = EXTERNAL_FLASH_STATUS_PROGRAM_FAIL;

    EXPECT_FALSE( FLASH_MANAGER_DrainInstructionUploadPages() );
    EXPECT_EQ( 1U, write_instruction_page_calls );
    EXPECT_FALSE( INSTRUCTION_BUFFER_IsUploadPersisted() );

    write_instruction_page_status = EXTERNAL_FLASH_STATUS_OK;
    EXPECT_TRUE( FLASH_MANAGER_DrainInstructionUploadPages() );
    EXPECT_EQ( 2U, write_instruction_page_calls );
}

TEST_F( FlashManagerTest, InstructionUploadFinishRejectsUnavailableIncompleteAndInvalidState )
{
    EXPECT_EQ( FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_NOT_INITIALISED,
               FLASH_MANAGER_RequestInstructionUploadFinish() );

    PrepareInstructionUpload( TEST_PAGE_SIZE_BYTES );
    EXPECT_EQ( FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_INVALID_STATE,
               FLASH_MANAGER_RequestInstructionUploadFinish() );

    flash_manager_context.state = FLASH_MANAGER_STATE_EXECUTING;
    EXPECT_EQ( FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_INVALID_STATE,
               FLASH_MANAGER_RequestInstructionUploadFinish() );
}

TEST_F( FlashManagerTest, InstructionUploadFinishReturnsBusyDuringActiveNandWrite )
{
    std::array<uint8_t, TEST_PAGE_SIZE_BYTES> page = {};
    PrepareInstructionUpload( page.size() );
    ASSERT_EQ( FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_ACCEPTED,
               FLASH_MANAGER_SubmitInstructionUploadBytes( page.data(), page.size() ) );

    const uint8_t* drain_data   = nullptr;
    uint32_t       drain_length = 0U;
    ASSERT_TRUE( INSTRUCTION_BUFFER_AcquireUploadDrainPage( &drain_data, &drain_length ) );

    EXPECT_EQ( FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_BUSY,
               FLASH_MANAGER_RequestInstructionUploadFinish() );
    EXPECT_EQ( FLASH_MANAGER_STATE_INSTRUCTION_UPLOAD, flash_manager_context.state );
    ASSERT_TRUE( INSTRUCTION_BUFFER_CompleteUploadDrain( true ) );
}

TEST_F( FlashManagerTest, InstructionUploadFinishRejectsMissingTaskWithoutFinalisingBuffer )
{
    std::array<uint8_t, TEST_PARTIAL_PAYLOAD_BYTES> data = {};
    PrepareInstructionUpload( data.size() );
    ASSERT_EQ( FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_ACCEPTED,
               FLASH_MANAGER_SubmitInstructionUploadBytes( data.data(), data.size() ) );
    flash_manager_context.task_handle = nullptr;

    EXPECT_EQ( FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_TASK_NOT_READY,
               FLASH_MANAGER_RequestInstructionUploadFinish() );
    EXPECT_EQ( FLASH_MANAGER_STATE_INSTRUCTION_UPLOAD, flash_manager_context.state );
    EXPECT_FALSE( INSTRUCTION_BUFFER_IsUploadPersisted() );
}

TEST_F( FlashManagerTest, InstructionUploadFinishPublishesPartialPageAndNotifiesTask )
{
    std::array<uint8_t, TEST_PARTIAL_PAYLOAD_BYTES> partial_page = {};
    PrepareInstructionUpload( partial_page.size() );
    ASSERT_EQ(
        FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_ACCEPTED,
        FLASH_MANAGER_SubmitInstructionUploadBytes( partial_page.data(), partial_page.size() ) );

    EXPECT_EQ( FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_ACCEPTED,
               FLASH_MANAGER_RequestInstructionUploadFinish() );
    EXPECT_EQ( FLASH_MANAGER_STATE_FINALISING_INSTRUCTION_UPLOAD, flash_manager_context.state );
    EXPECT_EQ( 1U, notify_calls );
    EXPECT_EQ( FLASH_MANAGER_NOTIFY_FINALISE_INSTRUCTION_UPLOAD, notify_value );
    EXPECT_EQ( eSetBits, notify_action );
}

TEST_F( FlashManagerTest, InstructionUploadFinalisationDrainsPartialPageClosesAndReleasesUpload )
{
    std::array<uint8_t, TEST_PAGE_SIZE_BYTES>       full_page    = {};
    std::array<uint8_t, TEST_PARTIAL_PAYLOAD_BYTES> partial_page = {};
    FillBytes( full_page.data(), full_page.size(), 0x22U );
    FillBytes( partial_page.data(), partial_page.size(), 0xA0U );
    PrepareInstructionUpload( full_page.size() + partial_page.size() );

    ASSERT_EQ( FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_ACCEPTED,
               FLASH_MANAGER_SubmitInstructionUploadBytes( full_page.data(), full_page.size() ) );
    ASSERT_EQ(
        FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_ACCEPTED,
        FLASH_MANAGER_SubmitInstructionUploadBytes( partial_page.data(), partial_page.size() ) );
    ASSERT_TRUE( FLASH_MANAGER_DrainInstructionUploadPages() );
    ASSERT_EQ( FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_ACCEPTED,
               FLASH_MANAGER_RequestInstructionUploadFinish() );

    ASSERT_TRUE( FLASH_MANAGER_FinaliseInstructionUpload() );
    ASSERT_EQ( 2U, write_instruction_page_calls );
    EXPECT_EQ( TEST_PAGE_SIZE_BYTES, write_instruction_page_lengths[0] );
    EXPECT_EQ( partial_page.size(), write_instruction_page_lengths[1] );
    EXPECT_EQ( 0, std::memcmp( partial_page.data(), write_instruction_page_data[1],
                               partial_page.size() ) );
    EXPECT_EQ( 1U, finish_instruction_upload_calls );
    EXPECT_EQ( FLASH_MANAGER_STATE_IDLE, flash_manager_context.state );
    uint32_t released_length_bytes = 0U;
    EXPECT_FALSE( INSTRUCTION_BUFFER_GetUploadExpectedLength( &released_length_bytes ) );
}

TEST_F( FlashManagerTest, InstructionUploadFinalisationReportsExternalCloseFailure )
{
    std::array<uint8_t, TEST_PAGE_SIZE_BYTES> page = {};
    PrepareInstructionUpload( page.size() );
    ASSERT_EQ( FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_ACCEPTED,
               FLASH_MANAGER_SubmitInstructionUploadBytes( page.data(), page.size() ) );
    ASSERT_TRUE( FLASH_MANAGER_DrainInstructionUploadPages() );
    ASSERT_EQ( FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_ACCEPTED,
               FLASH_MANAGER_RequestInstructionUploadFinish() );
    finish_instruction_upload_status = EXTERNAL_FLASH_STATUS_ERROR;

    EXPECT_FALSE( FLASH_MANAGER_FinaliseInstructionUpload() );
    EXPECT_EQ( 1U, finish_instruction_upload_calls );
    EXPECT_EQ( FLASH_MANAGER_STATE_FINALISING_INSTRUCTION_UPLOAD, flash_manager_context.state );
}

TEST_F( FlashManagerTest, InstructionUploadFinishNotificationFailureEntersFault )
{
    std::array<uint8_t, TEST_PAGE_SIZE_BYTES> page = {};
    PrepareInstructionUpload( page.size() );
    ASSERT_EQ( FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_ACCEPTED,
               FLASH_MANAGER_SubmitInstructionUploadBytes( page.data(), page.size() ) );
    ASSERT_TRUE( FLASH_MANAGER_DrainInstructionUploadPages() );
    notify_result = pdFAIL;

    EXPECT_EQ( FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_NOTIFY_FAILED,
               FLASH_MANAGER_RequestInstructionUploadFinish() );
    EXPECT_EQ( FLASH_MANAGER_STATE_FAULT, flash_manager_context.state );
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

TEST_F( FlashManagerTest, TaskFaultUsesRegisteredSystemHandoff )
{
    Initialise();
    FLASH_MANAGER_SetFaultCallback( TestFaultCallback );

    FLASH_MANAGER_EnterFault();

    EXPECT_EQ( FLASH_MANAGER_STATE_FAULT, flash_manager_context.state );
    EXPECT_EQ( 1U, fault_callback_calls );
    EXPECT_FALSE( fault_callback_from_isr );
}

TEST_F( FlashManagerTest, IsrFaultUsesRegisteredSystemHandoff )
{
    Initialise();
    FLASH_MANAGER_SetFaultCallback( TestFaultCallback );

    FLASH_MANAGER_EnterFaultFromISR();

    EXPECT_EQ( FLASH_MANAGER_STATE_FAULT, flash_manager_context.state );
    EXPECT_EQ( 1U, fault_callback_calls );
    EXPECT_TRUE( fault_callback_from_isr );
}

TEST_F( FlashManagerTest, PreparationPreloadsEveryAvailableInstructionPageBeforeExecuting )
{
    Initialise();
    RegisterTask();
    ConfigurePageAlignedInstructionImage( TEST_INSTRUCTION_BUFFER_PAGE_COUNT + 1U );
    ASSERT_EQ( FLASH_MANAGER_REQUEST_OK, FLASH_MANAGER_RequestExecutionPreparation() );

    ASSERT_TRUE( FLASH_MANAGER_PrepareExecution() );

    EXPECT_EQ( FLASH_MANAGER_STATE_EXECUTING, flash_manager_context.state );
    ASSERT_EQ( TEST_INSTRUCTION_BUFFER_PAGE_COUNT, read_instruction_page_calls );

    for ( uint32_t page_index = 0U; page_index < TEST_INSTRUCTION_BUFFER_PAGE_COUNT; page_index++ )
    {
        EXPECT_EQ( page_index * TEST_PAGE_SIZE_BYTES, read_instruction_page_offsets[page_index] );
        EXPECT_EQ( TEST_PAGE_SIZE_BYTES, read_instruction_page_lengths[page_index] );
    }
}

TEST_F( FlashManagerTest, PreparationReportsInstructionNandReadFailure )
{
    Initialise();
    RegisterTask();
    ConfigurePageAlignedInstructionImage( 1U );
    read_instruction_page_status = EXTERNAL_FLASH_STATUS_TIMEOUT;
    ASSERT_EQ( FLASH_MANAGER_REQUEST_OK, FLASH_MANAGER_RequestExecutionPreparation() );

    EXPECT_FALSE( FLASH_MANAGER_PrepareExecution() );
    EXPECT_EQ( 1U, read_instruction_page_calls );
    EXPECT_EQ( FLASH_MANAGER_STATE_PREPARING_EXECUTION, flash_manager_context.state );
}

TEST_F( FlashManagerTest, ConsumingPageNotifiesTaskAndRefillWorkerLoadsReleasedSlot )
{
    Initialise();
    RegisterTask();
    ConfigurePageAlignedInstructionImage( TEST_INSTRUCTION_BUFFER_PAGE_COUNT + 1U );
    ASSERT_EQ( FLASH_MANAGER_REQUEST_OK, FLASH_MANAGER_RequestExecutionPreparation() );
    ASSERT_TRUE( FLASH_MANAGER_PrepareExecution() );

    notify_from_isr_writes_wake_value = true;
    notify_from_isr_wake_value        = pdTRUE;
    BaseType_t task_woken             = pdFALSE;

    const FlashManagerInstructionView_T* view = nullptr;
    ASSERT_EQ( FLASH_MANAGER_INSTRUCTION_AVAILABLE,
               FLASH_MANAGER_PeekNextInstructionFromISR( &view ) );
    ASSERT_TRUE( FLASH_MANAGER_ConsumeInstructionFromISR( &task_woken ) );

    EXPECT_EQ( 1U, notify_from_isr_calls );
    EXPECT_EQ( FLASH_MANAGER_NOTIFY_REFILL_INSTRUCTIONS, notify_from_isr_value );
    EXPECT_EQ( TEST_FLASH_MANAGER_TASK_HANDLE, notify_from_isr_task_handle );
    EXPECT_EQ( eSetBits, notify_from_isr_action );
    EXPECT_EQ( &task_woken, notify_from_isr_wake_pointer );
    EXPECT_EQ( pdTRUE, task_woken );
    EXPECT_EQ( TEST_INSTRUCTION_BUFFER_PAGE_COUNT, read_instruction_page_calls );

    ASSERT_TRUE( FLASH_MANAGER_FillInstructionPages() );
    EXPECT_EQ( TEST_INSTRUCTION_BUFFER_PAGE_COUNT + 1U, read_instruction_page_calls );
    EXPECT_EQ( TEST_PAGE_SIZE_BYTES * TEST_INSTRUCTION_BUFFER_PAGE_COUNT,
               read_instruction_page_offsets[TEST_INSTRUCTION_BUFFER_PAGE_COUNT] );
}

TEST_F( FlashManagerTest, InstructionExhaustionRemainsExecutingUntilExplicitFinalisation )
{
    Initialise();
    RegisterTask();
    ConfigurePageAlignedInstructionImage( 1U );
    ASSERT_EQ( FLASH_MANAGER_REQUEST_OK, FLASH_MANAGER_RequestExecutionPreparation() );
    ASSERT_TRUE( FLASH_MANAGER_PrepareExecution() );

    const FlashManagerInstructionView_T* view = nullptr;
    ASSERT_EQ( FLASH_MANAGER_INSTRUCTION_AVAILABLE,
               FLASH_MANAGER_PeekNextInstructionFromISR( &view ) );

    const FlashManagerInstructionView_T* repeated_view = nullptr;
    ASSERT_EQ( FLASH_MANAGER_INSTRUCTION_AVAILABLE,
               FLASH_MANAGER_PeekNextInstructionFromISR( &repeated_view ) );
    EXPECT_EQ( view, repeated_view );

    ASSERT_TRUE( FLASH_MANAGER_ConsumeInstructionFromISR( nullptr ) );

    EXPECT_EQ( 0U, notify_from_isr_calls );
    EXPECT_EQ( FLASH_MANAGER_INSTRUCTION_END_OF_STREAM,
               FLASH_MANAGER_PeekNextInstructionFromISR( &view ) );
    EXPECT_EQ( FLASH_MANAGER_STATE_EXECUTING, flash_manager_context.state );

    FlashManagerResultWriteLease_T result_lease = ReserveRecord( TEST_PARTIAL_PAYLOAD_BYTES );
    std::memset( result_lease.payload, 0x6AU, TEST_PARTIAL_PAYLOAD_BYTES );
    ASSERT_EQ( FLASH_MANAGER_RESULT_COMMIT_OK,
               FLASH_MANAGER_CommitResultRecordFromISR( &result_lease, 100U, 2U, 3U,
                                                        TEST_PARTIAL_PAYLOAD_BYTES, nullptr ) );
    EXPECT_EQ( FLASH_MANAGER_STATE_EXECUTING, flash_manager_context.state );

    ASSERT_EQ( FLASH_MANAGER_REQUEST_OK, FLASH_MANAGER_RequestResultFinalisation() );
    EXPECT_EQ( FLASH_MANAGER_STATE_FINALISING_RESULTS, flash_manager_context.state );
    ASSERT_TRUE( FLASH_MANAGER_FinaliseResults() );
    EXPECT_EQ( FLASH_MANAGER_STATE_RESULTS_READY, flash_manager_context.state );

    /* Finalisation must release retrieval ownership of the shared instruction RAM. */
    EXPECT_TRUE( INSTRUCTION_BUFFER_PrepareUpload( TEST_PAGE_SIZE_BYTES ) );
}

TEST_F( FlashManagerTest, ReachingUnloadedInstructionDataLatchesUnderrunFault )
{
    Initialise();
    EnterExecutingState();
    ConfigurePageAlignedInstructionImage( 2U );
    ASSERT_TRUE( INSTRUCTION_BUFFER_PrepareRead( TEST_PAGE_SIZE_BYTES * 2U ) );

    InstructionBufferPageFillLease_T fill_lease = {};
    ASSERT_TRUE( INSTRUCTION_BUFFER_AcquireFillPage( &fill_lease ) );
    std::memcpy( fill_lease.page_data, instruction_image, fill_lease.read_length_bytes );
    ASSERT_TRUE( INSTRUCTION_BUFFER_CompleteFillPage( &fill_lease, true ) );

    const FlashManagerInstructionView_T* view = nullptr;
    ASSERT_EQ( FLASH_MANAGER_INSTRUCTION_AVAILABLE,
               FLASH_MANAGER_PeekNextInstructionFromISR( &view ) );
    ASSERT_TRUE( FLASH_MANAGER_ConsumeInstructionFromISR( nullptr ) );

    EXPECT_EQ( FLASH_MANAGER_INSTRUCTION_NOT_BUFFERED,
               FLASH_MANAGER_PeekNextInstructionFromISR( &view ) );
    EXPECT_EQ( FLASH_MANAGER_STATE_FAULT, flash_manager_context.state );
}

TEST_F( FlashManagerTest, CorruptStoredInstructionLatchesFault )
{
    Initialise();
    EnterExecutingState();
    ASSERT_TRUE( INSTRUCTION_BUFFER_PrepareRead( TEST_PAGE_SIZE_BYTES ) );

    InstructionBufferPageFillLease_T fill_lease = {};
    ASSERT_TRUE( INSTRUCTION_BUFFER_AcquireFillPage( &fill_lease ) );

    ExecutionInstructionHeader_T corrupt_header = {
        100U,
        static_cast<uint16_t>( TEST_PAGE_SIZE_BYTES ),
        1U,
        0U,
    };
    std::memcpy( fill_lease.page_data, &corrupt_header, sizeof( corrupt_header ) );
    ASSERT_TRUE( INSTRUCTION_BUFFER_CompleteFillPage( &fill_lease, true ) );

    const FlashManagerInstructionView_T* view = nullptr;
    EXPECT_EQ( FLASH_MANAGER_INSTRUCTION_CORRUPT,
               FLASH_MANAGER_PeekNextInstructionFromISR( &view ) );
    EXPECT_EQ( nullptr, view );
    EXPECT_EQ( FLASH_MANAGER_STATE_FAULT, flash_manager_context.state );
}

TEST_F( FlashManagerTest, RefillWorkerHandlesCoalescedPageReleaseNotifications )
{
    Initialise();
    RegisterTask();
    ConfigurePageAlignedInstructionImage( TEST_INSTRUCTION_BUFFER_PAGE_COUNT + 2U );
    ASSERT_EQ( FLASH_MANAGER_REQUEST_OK, FLASH_MANAGER_RequestExecutionPreparation() );
    ASSERT_TRUE( FLASH_MANAGER_PrepareExecution() );

    for ( uint32_t instruction_index = 0U; instruction_index < 2U; instruction_index++ )
    {
        const FlashManagerInstructionView_T* view = nullptr;
        ASSERT_EQ( FLASH_MANAGER_INSTRUCTION_AVAILABLE,
                   FLASH_MANAGER_PeekNextInstructionFromISR( &view ) );
        ASSERT_TRUE( FLASH_MANAGER_ConsumeInstructionFromISR( nullptr ) );
    }

    EXPECT_EQ( 2U, notify_from_isr_calls );

    /* One task wake processes both slots even when the notification bits coalesce. */
    ASSERT_TRUE( FLASH_MANAGER_FillInstructionPages() );
    EXPECT_EQ( TEST_INSTRUCTION_BUFFER_PAGE_COUNT + 2U, read_instruction_page_calls );
    EXPECT_EQ( TEST_PAGE_SIZE_BYTES * TEST_INSTRUCTION_BUFFER_PAGE_COUNT,
               read_instruction_page_offsets[TEST_INSTRUCTION_BUFFER_PAGE_COUNT] );
    EXPECT_EQ( TEST_PAGE_SIZE_BYTES * ( TEST_INSTRUCTION_BUFFER_PAGE_COUNT + 1U ),
               read_instruction_page_offsets[TEST_INSTRUCTION_BUFFER_PAGE_COUNT + 1U] );
}

TEST_F( FlashManagerTest, RefillNotificationFailureFaultsAfterInstructionWasConsumed )
{
    Initialise();
    RegisterTask();
    ConfigurePageAlignedInstructionImage( TEST_INSTRUCTION_BUFFER_PAGE_COUNT + 1U );
    ASSERT_EQ( FLASH_MANAGER_REQUEST_OK, FLASH_MANAGER_RequestExecutionPreparation() );
    ASSERT_TRUE( FLASH_MANAGER_PrepareExecution() );
    notify_from_isr_result = pdFAIL;

    const FlashManagerInstructionView_T* view = nullptr;
    ASSERT_EQ( FLASH_MANAGER_INSTRUCTION_AVAILABLE,
               FLASH_MANAGER_PeekNextInstructionFromISR( &view ) );

    EXPECT_FALSE( FLASH_MANAGER_ConsumeInstructionFromISR( nullptr ) );
    EXPECT_EQ( 1U, notify_from_isr_calls );
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
    FlashManagerResultWriteLease_T lease = {};
    lease.payload                        = reinterpret_cast<uint8_t*>( 0x1U );
    lease.lease_id                       = 42U;
    lease.payload_capacity_bytes         = 12U;

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

/**-----------------------------------------------------------------------------
 *  Host Interface Result Retrieval Tests
 *------------------------------------------------------------------------------
 */

TEST_F( FlashManagerTest, ResultTransferStartRejectsUnavailableManagerStateAndTask )
{
    EXPECT_EQ( FLASH_MANAGER_RESULT_TRANSFER_NOT_INITIALISED,
               FLASH_MANAGER_RequestResultTransferStart() );

    Initialise();
    EXPECT_EQ( FLASH_MANAGER_RESULT_TRANSFER_INVALID_STATE,
               FLASH_MANAGER_RequestResultTransferStart() );

    ASSERT_TRUE( RESULT_BUFFER_Finalise() );
    flash_manager_context.state = FLASH_MANAGER_STATE_RESULTS_READY;
    EXPECT_EQ( FLASH_MANAGER_RESULT_TRANSFER_TASK_NOT_READY,
               FLASH_MANAGER_RequestResultTransferStart() );
    EXPECT_EQ( FLASH_MANAGER_STATE_RESULTS_READY, flash_manager_context.state );
}

TEST_F( FlashManagerTest, ResultTransferStartPreparesReadStateAndNotifiesInitialPrefill )
{
    constexpr uint32_t result_length_bytes = TEST_PAGE_SIZE_BYTES * 2U + 5U;
    PrepareCompletedResults( result_length_bytes );

    ASSERT_EQ( FLASH_MANAGER_RESULT_TRANSFER_OK, FLASH_MANAGER_RequestResultTransferStart() );

    EXPECT_EQ( FLASH_MANAGER_STATE_TRANSFERRING_RESULTS, flash_manager_context.state );
    EXPECT_EQ( 1U, notify_calls );
    EXPECT_EQ( TEST_FLASH_MANAGER_TASK_HANDLE, notify_task_handle );
    EXPECT_EQ( FLASH_MANAGER_NOTIFY_FILL_RESULTS, notify_value );
    EXPECT_EQ( eSetBits, notify_action );

    std::array<uint8_t, 1U> destination = {};
    uint32_t                bytes_read  = 1U;
    EXPECT_EQ(
        FLASH_MANAGER_RESULT_TRANSFER_BUSY,
        FLASH_MANAGER_ReadResultBytes( destination.data(), destination.size(), &bytes_read ) );
    EXPECT_EQ( 0U, bytes_read );
}

TEST_F( FlashManagerTest, ResultTransferStartExternalInfoFailureEntersFault )
{
    PrepareCompletedResults( TEST_PAGE_SIZE_BYTES );
    FLASH_MANAGER_TEST_ConfigureExternalFlashInfo( EXTERNAL_FLASH_STATUS_ERROR,
                                                   TEST_PAGE_SIZE_BYTES );

    EXPECT_EQ( FLASH_MANAGER_RESULT_TRANSFER_INTERNAL_ERROR,
               FLASH_MANAGER_RequestResultTransferStart() );
    EXPECT_EQ( FLASH_MANAGER_STATE_FAULT, flash_manager_context.state );
    EXPECT_EQ( 0U, notify_calls );
}

TEST_F( FlashManagerTest, ResultTransferStartFaultsWhenResultBufferIsNotFinalised )
{
    Initialise();
    RegisterTask();
    FLASH_MANAGER_TEST_SetResultLength( TEST_PAGE_SIZE_BYTES );
    flash_manager_context.state = FLASH_MANAGER_STATE_RESULTS_READY;

    EXPECT_EQ( FLASH_MANAGER_RESULT_TRANSFER_INTERNAL_ERROR,
               FLASH_MANAGER_RequestResultTransferStart() );
    EXPECT_EQ( FLASH_MANAGER_STATE_FAULT, flash_manager_context.state );
    EXPECT_EQ( 0U, notify_calls );
}

TEST_F( FlashManagerTest, ResultTransferStartNotificationFailureEntersFault )
{
    PrepareCompletedResults( TEST_PAGE_SIZE_BYTES );
    notify_result = pdFAIL;

    EXPECT_EQ( FLASH_MANAGER_RESULT_TRANSFER_NOTIFY_FAILED,
               FLASH_MANAGER_RequestResultTransferStart() );
    EXPECT_EQ( FLASH_MANAGER_STATE_FAULT, flash_manager_context.state );
    EXPECT_EQ( 1U, notify_calls );
}

TEST_F( FlashManagerTest, ResultReadValidatesArgumentsStateAndReportsBusyBeforePrefill )
{
    std::array<uint8_t, 8U> destination = {};
    uint32_t                bytes_read  = 99U;

    EXPECT_EQ(
        FLASH_MANAGER_RESULT_TRANSFER_NOT_INITIALISED,
        FLASH_MANAGER_ReadResultBytes( destination.data(), destination.size(), &bytes_read ) );
    EXPECT_EQ( 0U, bytes_read );

    PrepareCompletedResults( TEST_PAGE_SIZE_BYTES );
    EXPECT_EQ( FLASH_MANAGER_RESULT_TRANSFER_INVALID_ARGUMENT,
               FLASH_MANAGER_ReadResultBytes( nullptr, destination.size(), &bytes_read ) );
    EXPECT_EQ( FLASH_MANAGER_RESULT_TRANSFER_INVALID_ARGUMENT,
               FLASH_MANAGER_ReadResultBytes( destination.data(), 0U, &bytes_read ) );
    EXPECT_EQ( FLASH_MANAGER_RESULT_TRANSFER_INVALID_ARGUMENT,
               FLASH_MANAGER_ReadResultBytes( destination.data(), destination.size(), nullptr ) );
    EXPECT_EQ(
        FLASH_MANAGER_RESULT_TRANSFER_INVALID_STATE,
        FLASH_MANAGER_ReadResultBytes( destination.data(), destination.size(), &bytes_read ) );

    ASSERT_EQ( FLASH_MANAGER_RESULT_TRANSFER_OK, FLASH_MANAGER_RequestResultTransferStart() );
    EXPECT_EQ(
        FLASH_MANAGER_RESULT_TRANSFER_BUSY,
        FLASH_MANAGER_ReadResultBytes( destination.data(), destination.size(), &bytes_read ) );
    EXPECT_EQ( 0U, bytes_read );
}

TEST_F( FlashManagerTest, ResultFillWorkerPrefetchesAvailablePagesAndFinalPartialPageInOrder )
{
    constexpr uint32_t result_length_bytes = TEST_PAGE_SIZE_BYTES * 3U + 7U;
    PrepareCompletedResults( result_length_bytes );
    FillBytes( result_image, result_length_bytes, 0x20U );
    ASSERT_EQ( FLASH_MANAGER_RESULT_TRANSFER_OK, FLASH_MANAGER_RequestResultTransferStart() );

    ASSERT_TRUE( FLASH_MANAGER_FillResultPages() );
    ASSERT_EQ( 3U, read_result_page_calls );

    for ( uint32_t page_index = 0U; page_index < 3U; page_index++ )
    {
        EXPECT_EQ( page_index * TEST_PAGE_SIZE_BYTES, read_result_page_offsets[page_index] );
        EXPECT_EQ( TEST_PAGE_SIZE_BYTES, read_result_page_lengths[page_index] );
    }

    /* All three RAM slots are occupied, so another fill applies backpressure. */
    ASSERT_TRUE( FLASH_MANAGER_FillResultPages() );
    EXPECT_EQ( 3U, read_result_page_calls );

    notify_calls                                         = 0U;
    std::array<uint8_t, TEST_PAGE_SIZE_BYTES> first_page = {};
    uint32_t                                  bytes_read = 0U;
    ASSERT_EQ( FLASH_MANAGER_RESULT_TRANSFER_OK,
               FLASH_MANAGER_ReadResultBytes( first_page.data(), first_page.size(), &bytes_read ) );
    ASSERT_EQ( first_page.size(), bytes_read );
    EXPECT_EQ( 0, std::memcmp( first_page.data(), result_image, first_page.size() ) );
    EXPECT_EQ( 1U, notify_calls );
    EXPECT_EQ( FLASH_MANAGER_NOTIFY_FILL_RESULTS, notify_value );

    ASSERT_TRUE( FLASH_MANAGER_FillResultPages() );
    ASSERT_EQ( 4U, read_result_page_calls );
    EXPECT_EQ( TEST_PAGE_SIZE_BYTES * 3U, read_result_page_offsets[3] );
    EXPECT_EQ( 7U, read_result_page_lengths[3] );
}

TEST_F( FlashManagerTest, StaleResultFillNotificationRequiresNoNandAccess )
{
    Initialise();

    EXPECT_TRUE( FLASH_MANAGER_FillResultPages() );
    EXPECT_EQ( 0U, read_result_page_calls );
}

TEST_F( FlashManagerTest, ResultTransferCopiesOrderedBytesRefillsAndFinishesInIdle )
{
    constexpr uint32_t result_length_bytes = TEST_PAGE_SIZE_BYTES * 3U + 7U;
    PrepareCompletedResults( result_length_bytes );
    FillBytes( result_image, result_length_bytes, 0x30U );
    ASSERT_EQ( FLASH_MANAGER_RESULT_TRANSFER_OK, FLASH_MANAGER_RequestResultTransferStart() );
    ASSERT_TRUE( FLASH_MANAGER_FillResultPages() );

    std::array<uint8_t, TEST_PAGE_SIZE_BYTES> first_page = {};
    uint32_t                                  bytes_read = 0U;
    ASSERT_EQ( FLASH_MANAGER_RESULT_TRANSFER_OK,
               FLASH_MANAGER_ReadResultBytes( first_page.data(), first_page.size(), &bytes_read ) );
    ASSERT_EQ( first_page.size(), bytes_read );
    ASSERT_TRUE( FLASH_MANAGER_FillResultPages() );

    std::array<uint8_t, result_length_bytes - TEST_PAGE_SIZE_BYTES> remaining = {};
    ASSERT_EQ( FLASH_MANAGER_RESULT_TRANSFER_OK,
               FLASH_MANAGER_ReadResultBytes( remaining.data(), remaining.size(), &bytes_read ) );
    ASSERT_EQ( remaining.size(), bytes_read );

    EXPECT_EQ( 0, std::memcmp( first_page.data(), result_image, first_page.size() ) );
    EXPECT_EQ(
        0, std::memcmp( remaining.data(), &result_image[first_page.size()], remaining.size() ) );
    EXPECT_EQ( FLASH_MANAGER_RESULT_TRANSFER_END_OF_STREAM,
               FLASH_MANAGER_ReadResultBytes( first_page.data(), first_page.size(), &bytes_read ) );
    EXPECT_EQ( 0U, bytes_read );

    EXPECT_EQ( FLASH_MANAGER_RESULT_TRANSFER_OK, FLASH_MANAGER_FinishResultTransfer() );
    EXPECT_EQ( FLASH_MANAGER_STATE_IDLE, flash_manager_context.state );
}

TEST_F( FlashManagerTest, EmptyResultTransferCanFinishWithoutNandRead )
{
    PrepareCompletedResults( 0U );
    ASSERT_EQ( FLASH_MANAGER_RESULT_TRANSFER_OK, FLASH_MANAGER_RequestResultTransferStart() );

    EXPECT_TRUE( FLASH_MANAGER_FillResultPages() );
    EXPECT_EQ( 0U, read_result_page_calls );

    std::array<uint8_t, 4U> destination = {};
    uint32_t                bytes_read  = 99U;
    EXPECT_EQ(
        FLASH_MANAGER_RESULT_TRANSFER_END_OF_STREAM,
        FLASH_MANAGER_ReadResultBytes( destination.data(), destination.size(), &bytes_read ) );
    EXPECT_EQ( 0U, bytes_read );
    EXPECT_EQ( FLASH_MANAGER_RESULT_TRANSFER_OK, FLASH_MANAGER_FinishResultTransfer() );
    EXPECT_EQ( FLASH_MANAGER_STATE_IDLE, flash_manager_context.state );
}

TEST_F( FlashManagerTest, ReleasedResultPageFaultsWhenRefillTaskIsUnavailable )
{
    PrepareCompletedResults( TEST_PAGE_SIZE_BYTES );
    FillBytes( result_image, TEST_PAGE_SIZE_BYTES, 0x50U );
    ASSERT_EQ( FLASH_MANAGER_RESULT_TRANSFER_OK, FLASH_MANAGER_RequestResultTransferStart() );
    ASSERT_TRUE( FLASH_MANAGER_FillResultPages() );
    flash_manager_context.task_handle = nullptr;

    std::array<uint8_t, TEST_PAGE_SIZE_BYTES> destination = {};
    uint32_t                                  bytes_read  = 0U;
    EXPECT_EQ(
        FLASH_MANAGER_RESULT_TRANSFER_INTERNAL_ERROR,
        FLASH_MANAGER_ReadResultBytes( destination.data(), destination.size(), &bytes_read ) );
    EXPECT_EQ( destination.size(), bytes_read );
    EXPECT_EQ( 0, std::memcmp( destination.data(), result_image, destination.size() ) );
    EXPECT_EQ( FLASH_MANAGER_STATE_FAULT, flash_manager_context.state );
}

TEST_F( FlashManagerTest, ReleasedResultPageReportsRefillNotificationFailure )
{
    PrepareCompletedResults( TEST_PAGE_SIZE_BYTES );
    FillBytes( result_image, TEST_PAGE_SIZE_BYTES, 0x60U );
    ASSERT_EQ( FLASH_MANAGER_RESULT_TRANSFER_OK, FLASH_MANAGER_RequestResultTransferStart() );
    ASSERT_TRUE( FLASH_MANAGER_FillResultPages() );
    notify_result = pdFAIL;

    std::array<uint8_t, TEST_PAGE_SIZE_BYTES> destination = {};
    uint32_t                                  bytes_read  = 0U;
    EXPECT_EQ(
        FLASH_MANAGER_RESULT_TRANSFER_NOTIFY_FAILED,
        FLASH_MANAGER_ReadResultBytes( destination.data(), destination.size(), &bytes_read ) );
    EXPECT_EQ( destination.size(), bytes_read );
    EXPECT_EQ( FLASH_MANAGER_STATE_FAULT, flash_manager_context.state );
}

TEST_F( FlashManagerTest, ResultFillFailureReleasesPageForRetryAtTheSameOffset )
{
    PrepareCompletedResults( TEST_PAGE_SIZE_BYTES );
    ASSERT_EQ( FLASH_MANAGER_RESULT_TRANSFER_OK, FLASH_MANAGER_RequestResultTransferStart() );
    read_result_page_status = EXTERNAL_FLASH_STATUS_ECC_ERROR;

    EXPECT_FALSE( FLASH_MANAGER_FillResultPages() );
    EXPECT_EQ( 1U, read_result_page_calls );

    read_result_page_status = EXTERNAL_FLASH_STATUS_OK;
    ASSERT_TRUE( FLASH_MANAGER_FillResultPages() );
    ASSERT_EQ( 2U, read_result_page_calls );
    EXPECT_EQ( 0U, read_result_page_offsets[1] );
    EXPECT_EQ( TEST_PAGE_SIZE_BYTES, read_result_page_lengths[1] );
}

TEST_F( FlashManagerTest, ResultTransferRejectsEarlyFinish )
{
    EXPECT_EQ( FLASH_MANAGER_RESULT_TRANSFER_NOT_INITIALISED,
               FLASH_MANAGER_FinishResultTransfer() );

    PrepareCompletedResults( TEST_PAGE_SIZE_BYTES );
    EXPECT_EQ( FLASH_MANAGER_RESULT_TRANSFER_INVALID_STATE, FLASH_MANAGER_FinishResultTransfer() );
    ASSERT_EQ( FLASH_MANAGER_RESULT_TRANSFER_OK, FLASH_MANAGER_RequestResultTransferStart() );

    EXPECT_EQ( FLASH_MANAGER_RESULT_TRANSFER_INVALID_STATE, FLASH_MANAGER_FinishResultTransfer() );
    EXPECT_EQ( FLASH_MANAGER_STATE_TRANSFERRING_RESULTS, flash_manager_context.state );
}

TEST_F( FlashManagerTest, ResultReadFaultsWhenManagerAndBufferStateAreInconsistent )
{
    Initialise();
    RegisterTask();
    flash_manager_context.state = FLASH_MANAGER_STATE_TRANSFERRING_RESULTS;

    std::array<uint8_t, 4U> destination = {};
    uint32_t                bytes_read  = 0U;
    EXPECT_EQ(
        FLASH_MANAGER_RESULT_TRANSFER_INTERNAL_ERROR,
        FLASH_MANAGER_ReadResultBytes( destination.data(), destination.size(), &bytes_read ) );
    EXPECT_EQ( FLASH_MANAGER_STATE_FAULT, flash_manager_context.state );
}
