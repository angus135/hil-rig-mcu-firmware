/******************************************************************************
 *  File:       flash_manager.c
 *  Author:     Callum Rafferty
 *  Created:    25-Mar-2026
 *
 *  Description:
 *      Coordinates Flash Manager lifecycle state, serves prefetched
 *      instructions, accepts packed result records from the execution timer
 *      ISR, streams host instruction uploads, and performs all NAND refill and
 *      drain operations from an RTOS task.
 *
 *  Notes:
 *      Execution-facing APIs are ISR-only and must never block. NAND access is
 *      restricted to the Flash Manager task. Buffer metadata shared by those
 *      contexts is protected by short task critical sections; NAND operations
 *      are deliberately performed outside critical sections.
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */
#include "flash_manager.h"
#include "external_flash.h"
#include "instruction_buffer.h"
#include "result_buffer.h"
#include "rtos_config.h"

#include <stdbool.h>
#include <stdint.h>

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */

/** Requests task-context preparation of a new execution session. */
#define FLASH_MANAGER_NOTIFY_PREPARE_EXECUTION ( 1UL << 0 )

/** Signals that at least one committed result page may be ready for NAND. */
#define FLASH_MANAGER_NOTIFY_DRAIN_RESULTS ( 1UL << 1 )

/** Requests publication and persistence of the final partial result page. */
#define FLASH_MANAGER_NOTIFY_FINALISE_RESULTS ( 1UL << 2 )

/** Signals that consumed instruction storage is available for a NAND refill. */
#define FLASH_MANAGER_NOTIFY_REFILL_INSTRUCTIONS ( 1UL << 3 )

/** Requests preparation of a new instruction upload. */
#define FLASH_MANAGER_NOTIFY_PREPARE_INSTRUCTION_UPLOAD ( 1UL << 4 )

/** Signals that at least one completed instruction-upload page is ready for NAND. */
#define FLASH_MANAGER_NOTIFY_DRAIN_INSTRUCTION_UPLOAD ( 1UL << 5 )

/** Requests persistence and closure of the final instruction-upload page. */
#define FLASH_MANAGER_NOTIFY_FINALISE_INSTRUCTION_UPLOAD ( 1UL << 6 )

#define FLASH_MANAGER_NOTIFY_FILL_RESULTS ( 1UL << 7 )
/**-----------------------------------------------------------------------------
 *  Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

typedef struct
{
    /**
     * Read by the execution ISR and written from startup or Flash Manager task
     * context. Volatile ensures each ISR call observes the current lifecycle
     * state; task-side runtime transitions that affect execution must also mask
     * the execution interrupt while updating this field.
     */
    volatile FlashManagerState_T state;

    /**
     * Serialises task-context lifecycle state and task-handle access. ISR-facing
     * APIs do not use this mutex because an ISR must never block. Shared buffer
     * metadata is instead protected by short task critical sections.
     */
    SemaphoreHandle_t access_mutex;

    /**
     * Written once by the Flash Manager task before execution may begin, then
     * read by the execution ISR when buffer work must be signalled.
     */
    TaskHandle_t task_handle;

} FlashManagerContext_T;

/**-----------------------------------------------------------------------------
 *  Public (global) and Extern Variables
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Private (static) Variables
 *------------------------------------------------------------------------------
 */

static FlashManagerContext_T flash_manager_context = {
    .state        = FLASH_MANAGER_STATE_UNINITIALISED,
    .access_mutex = NULL,
    .task_handle  = NULL,
};

static StaticSemaphore_t flash_manager_mutex_storage;

/**-----------------------------------------------------------------------------
 *  Private (static) Function Prototypes
 *------------------------------------------------------------------------------
 */
static bool FLASH_MANAGER_Lock( void );

static void FLASH_MANAGER_Unlock( void );

static bool FLASH_MANAGER_DrainResultPages( void );

static bool FLASH_MANAGER_FillInstructionPages( void );

static void FLASH_MANAGER_EnterFault( void );

static void FLASH_MANAGER_EnterFaultFromISR( void );

static bool FLASH_MANAGER_PrepareExecution( void );

static bool FLASH_MANAGER_FinaliseResults( void );

static bool FLASH_MANAGER_PrepareInstructionUpload( void );

static bool FLASH_MANAGER_DrainInstructionUploadPages( void );

static bool FLASH_MANAGER_FinaliseInstructionUpload( void );

static bool FLASH_Manager_FillResultPages( void );

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

/**
 * @brief Acquires exclusive access to task-context Flash Manager state.
 *
 * @return true when the mutex was acquired; otherwise false.
 *
 * @note This helper must never be called from interrupt context.
 */
static bool FLASH_MANAGER_Lock( void )
{
    if ( flash_manager_context.access_mutex == NULL )
    {
        return false;
    }

    return xSemaphoreTake( flash_manager_context.access_mutex, portMAX_DELAY ) == pdTRUE;
}

/**
 * @brief Releases task-context Flash Manager state access.
 *
 * @note This helper must never be called from interrupt context.
 */
static void FLASH_MANAGER_Unlock( void )
{
    if ( flash_manager_context.access_mutex != NULL )
    {
        ( void )xSemaphoreGive( flash_manager_context.access_mutex );
    }
}

/**
 * @brief Drains every currently ready result page to external NAND.
 *
 * Buffer ownership transitions are protected against the execution ISR, but
 * the potentially long NAND write occurs with interrupts enabled. Draining
 * continues until no ready page remains because notification bits may coalesce.
 *
 * @return true after all ready pages were drained; false after a NAND write or
 *         drain-lease completion failure.
 */
static bool FLASH_MANAGER_DrainResultPages( void )
{
    ResultBufferDrainLease_T drain_lease;

    for ( ;; )
    {
        bool drain_allowed = false;
        bool page_acquired = false;

        /*
         * Check lifecycle permission and acquire page ownership atomically with
         * respect to the execution ISR. This prevents a queued notification
         * from acquiring a failed page after the manager enters FAULT.
         */
        taskENTER_CRITICAL();
        drain_allowed =
            ( flash_manager_context.state == FLASH_MANAGER_STATE_EXECUTING )
            || ( flash_manager_context.state == FLASH_MANAGER_STATE_FINALISING_RESULTS );

        if ( drain_allowed )
        {
            page_acquired = RESULT_BUFFER_AcquireDrainPage( &drain_lease );
        }
        taskEXIT_CRITICAL();

        if ( !drain_allowed || !page_acquired )
        {
            return true;
        }

        ExternalFlashStatus_T nand_write_status =
            EXTERNAL_FLASH_WriteResultPage( drain_lease.page_data, drain_lease.valid_length_bytes );

        bool drain_completion_succeeded = false;

        taskENTER_CRITICAL();
        drain_completion_succeeded = RESULT_BUFFER_CompleteDrain(
            &drain_lease, nand_write_status == EXTERNAL_FLASH_STATUS_OK );
        taskEXIT_CRITICAL();

        if ( ( nand_write_status != EXTERNAL_FLASH_STATUS_OK ) || !drain_completion_succeeded )
        {
            /*
             * A failed NAND write returns a valid drain lease to
             * READY_TO_DRAIN. A completion failure may leave it DRAINING. In
             * either case, stop here and let the task enter FAULT rather than
             * retrying without an explicit recovery decision.
             */
            return false;
        }

        /*
         * Continue until no ready pages remain. Notification bits can coalesce,
         * so processing only one page per notification could strand another
         * ready page.
         */
    }
}

/**
 * @brief Fills every sequential empty instruction page from external NAND.
 *
 * Buffer ownership transitions are protected against the execution ISR. The
 * synchronous NAND/DMA read runs outside the critical section with interrupts
 * enabled. The loop continues until the buffer applies backpressure or the
 * complete instruction image has been loaded because notification bits may
 * coalesce.
 *
 * @return true after all currently available slots were filled; false after a
 *         NAND read or fill-lease completion failure.
 */
static bool FLASH_MANAGER_FillInstructionPages( void )
{
    InstructionBufferPageFillLease_T fill_lease;

    for ( ;; )
    {
        bool fill_allowed  = false;
        bool page_acquired = false;

        taskENTER_CRITICAL();
        fill_allowed = ( flash_manager_context.state == FLASH_MANAGER_STATE_PREPARING_EXECUTION )
                       || ( flash_manager_context.state == FLASH_MANAGER_STATE_EXECUTING );

        if ( fill_allowed )
        {
            page_acquired = INSTRUCTION_BUFFER_AcquireFillPage( &fill_lease );
        }
        taskEXIT_CRITICAL();

        if ( !fill_allowed || !page_acquired )
        {
            return true;
        }

        ExternalFlashStatus_T nand_read_status = EXTERNAL_FLASH_ReadInstructionPage(
            fill_lease.instruction_offset_bytes, fill_lease.page_data,
            fill_lease.read_length_bytes );

        bool fill_completion_succeeded = false;

        taskENTER_CRITICAL();
        fill_completion_succeeded = INSTRUCTION_BUFFER_CompleteFillPage(
            &fill_lease, nand_read_status == EXTERNAL_FLASH_STATUS_OK );
        taskEXIT_CRITICAL();

        if ( ( nand_read_status != EXTERNAL_FLASH_STATUS_OK ) || !fill_completion_succeeded )
        {
            return false;
        }
    }
}

/**
 * @brief Fills every sequentially available result page from external NAND.
 *
 * Page ownership is acquired while holding the Flash Manager mutex. The
 * DMA-backed NAND read runs outside the mutex, allowing the Host Interface to
 * consume a different ready page concurrently. The acquired page remains
 * inaccessible to the Host Interface until fill completion publishes it.
 *
 * @return true after all currently available result slots were filled; false
 *         after a NAND read, fill completion, or synchronization failure.
 */
static bool FLASH_MANAGER_FillResultPages( void )
{
    ResultBufferReadFillLease_T fill_lease;

    for ( ;; )
    {
        if ( !FLASH_MANAGER_Lock() )
        {
            return false;
        }

        bool fill_allowed = flash_manager_context.state == FLASH_MANAGER_STATE_TRANSFERRING_RESULTS;

        bool page_acquired = false;

        if ( fill_allowed )
        {
            page_acquired = RESULT_BUFFER_AcquireReadFillPage( &fill_lease );
        }

        FLASH_MANAGER_Unlock();

        /*
         * A stale notification after the transfer has ended requires no work
         * and is not itself a fault.
         */
        if ( !fill_allowed )
        {
            return true;
        }

        /*
         * Acquisition normally stops when all three slots are occupied or every
         * stored result byte has already been scheduled.
         */
        if ( !page_acquired )
        {
            return true;
        }

        ExternalFlashStatus_T nand_read_status = EXTERNAL_FLASH_ReadResultPage(
            fill_lease.result_offset_bytes, fill_lease.page_data, fill_lease.read_length_bytes );

        if ( !FLASH_MANAGER_Lock() )
        {
            return false;
        }

        bool fill_completion_succeeded = RESULT_BUFFER_CompleteReadFillPage(
            &fill_lease, nand_read_status == EXTERNAL_FLASH_STATUS_OK );

        FLASH_MANAGER_Unlock();

        if ( ( nand_read_status != EXTERNAL_FLASH_STATUS_OK ) || !fill_completion_succeeded )
        {
            /*
             * A reported NAND failure releases the page for a potential future
             * recovery policy. The current lifecycle treats the failure as
             * fatal and enters FAULT.
             */
            return false;
        }

        /*
         * Continue until RAM applies backpressure or the complete stored result
         * stream has been loaded. Notification bits may coalesce.
         */
    }
}

/**
 * @brief Places the Flash Manager into its fault state.
 *
 * Called from Flash Manager task context after an unrecoverable asynchronous
 * operation fails.
 */
static void FLASH_MANAGER_EnterFault( void )
{
    if ( !FLASH_MANAGER_Lock() )
    {
        return;
    }

    /* Prevent the execution ISR from observing a stale runtime state. */
    taskENTER_CRITICAL();
    flash_manager_context.state = FLASH_MANAGER_STATE_FAULT;
    taskEXIT_CRITICAL();

    FLASH_MANAGER_Unlock();
}

/**
 * @brief Latches an internal Flash Manager fault from interrupt context.
 *
 * This is a single aligned state write and does not call any blocking API. It
 * is used for instruction underrun/corruption and when committed or released
 * buffer work cannot be signalled to the Flash Manager task. A result record
 * that was committed before notification failure remains committed in RAM.
 */
static void FLASH_MANAGER_EnterFaultFromISR( void )
{
    flash_manager_context.state = FLASH_MANAGER_STATE_FAULT;
}

/**
 * @brief Prepares NAND and both RAM streams for a new execution session.
 *
 * @return true after the external-flash session starts, result RAM is reset,
 *         instruction RAM is preloaded, and the manager enters EXECUTING;
 *         otherwise false.
 *
 * @note Called only by the Flash Manager task after a preparation notification.
 */
static bool FLASH_MANAGER_PrepareExecution( void )
{
    if ( !FLASH_MANAGER_Lock() )
    {
        return false;
    }

    bool preparation_state_is_valid =
        flash_manager_context.state == FLASH_MANAGER_STATE_PREPARING_EXECUTION;

    FLASH_MANAGER_Unlock();

    /* Reject stale notifications before starting a destructive NAND session. */
    if ( !preparation_state_is_valid )
    {
        return false;
    }

    if ( EXTERNAL_FLASH_StartSession() != EXTERNAL_FLASH_STATUS_OK )
    {
        return false;
    }

    ExternalFlashInfo_T external_flash_info = { 0 };

    if ( EXTERNAL_FLASH_GetInfo( &external_flash_info ) != EXTERNAL_FLASH_STATUS_OK )
    {
        return false;
    }

    /*
     * Init has already established valid geometry. Reset is an unconditional
     * ownership/cursor reset and therefore has no failure result.
     */
    taskENTER_CRITICAL();
    RESULT_BUFFER_Reset();
    bool instruction_read_prepared =
        INSTRUCTION_BUFFER_PrepareRead( external_flash_info.instruction_length_bytes );
    taskEXIT_CRITICAL();

    if ( !instruction_read_prepared || !FLASH_MANAGER_FillInstructionPages() )
    {
        return false;
    }

    if ( !FLASH_MANAGER_Lock() )
    {
        return false;
    }

    preparation_state_is_valid =
        flash_manager_context.state == FLASH_MANAGER_STATE_PREPARING_EXECUTION;

    if ( preparation_state_is_valid )
    {
        taskENTER_CRITICAL();
        flash_manager_context.state = FLASH_MANAGER_STATE_EXECUTING;
        taskEXIT_CRITICAL();
    }

    FLASH_MANAGER_Unlock();

    return preparation_state_is_valid;
}

/**
 * @brief Publishes and drains the final result page for the active session.
 *
 * @return true after every committed result byte reaches NAND and the manager
 *         enters RESULTS_READY; otherwise false.
 *
 * @note The Run State Manager must stop the execution timer and ensure its ISR
 *       has returned before requesting this operation.
 */
static bool FLASH_MANAGER_FinaliseResults( void )
{
    if ( !FLASH_MANAGER_Lock() )
    {
        return false;
    }

    bool finalisation_state_is_valid =
        flash_manager_context.state == FLASH_MANAGER_STATE_FINALISING_RESULTS;

    FLASH_MANAGER_Unlock();

    /* A stale notification must not close or mutate an unrelated session. */
    if ( !finalisation_state_is_valid )
    {
        return false;
    }

    bool finalise_succeeded;

    /*
     * The Run State Manager has stopped the timer and waited for the execution
     * ISR to return. The instruction stream can therefore be invalidated
     * independently of whether every stored instruction was consumed.
     *
     * Publish the final partial result page, if one exists. An active result
     * record lease causes finalisation to fail.
     */
    taskENTER_CRITICAL();
    INSTRUCTION_BUFFER_EndRead();
    finalise_succeeded = RESULT_BUFFER_Finalise();
    taskEXIT_CRITICAL();

    if ( !finalise_succeeded )
    {
        return false;
    }

    /*
     * Writes all full pages and the newly published partial page. External
     * flash pads the physical remainder of the partial page with 0xFF.
     */
    if ( !FLASH_MANAGER_DrainResultPages() )
    {
        return false;
    }

    bool drain_complete;

    taskENTER_CRITICAL();
    drain_complete = RESULT_BUFFER_IsDrainComplete();
    taskEXIT_CRITICAL();

    if ( !drain_complete )
    {
        return false;
    }

    if ( !FLASH_MANAGER_Lock() )
    {
        return false;
    }

    finalisation_state_is_valid =
        flash_manager_context.state == FLASH_MANAGER_STATE_FINALISING_RESULTS;

    if ( finalisation_state_is_valid )
    {
        taskENTER_CRITICAL();
        flash_manager_context.state = FLASH_MANAGER_STATE_RESULTS_READY;
        taskEXIT_CRITICAL();
    }

    FLASH_MANAGER_Unlock();

    return finalisation_state_is_valid;
}

/**
 * @brief Starts external-flash preparation for the declared instruction image.
 *
 * @return true after NAND preparation succeeds and the manager enters
 *         INSTRUCTION_UPLOAD; otherwise false.
 *
 * @note Called only by the Flash Manager task after a start notification.
 */
static bool FLASH_MANAGER_PrepareInstructionUpload( void )
{
    if ( !FLASH_MANAGER_Lock() )
    {
        return false;
    }

    bool preparation_state_is_valid =
        flash_manager_context.state == FLASH_MANAGER_STATE_PREPARING_INSTRUCTION_UPLOAD;

    uint32_t expected_length_bytes = 0U;

    if ( preparation_state_is_valid )
    {
        preparation_state_is_valid =
            INSTRUCTION_BUFFER_GetUploadExpectedLength( &expected_length_bytes );
    }

    FLASH_MANAGER_Unlock();

    if ( !preparation_state_is_valid )
    {
        return false;
    }

    if ( EXTERNAL_FLASH_StartInstructionUpload( expected_length_bytes )
         != EXTERNAL_FLASH_STATUS_OK )
    {
        return false;
    }

    if ( !FLASH_MANAGER_Lock() )
    {
        return false;
    }

    preparation_state_is_valid =
        flash_manager_context.state == FLASH_MANAGER_STATE_PREPARING_INSTRUCTION_UPLOAD;

    if ( preparation_state_is_valid )
    {
        taskENTER_CRITICAL();
        flash_manager_context.state = FLASH_MANAGER_STATE_INSTRUCTION_UPLOAD;
        taskEXIT_CRITICAL();
    }

    FLASH_MANAGER_Unlock();

    return preparation_state_is_valid;
}

/**
 * @brief Drains every completed instruction-upload page to external NAND.
 *
 * Page acquisition and completion are serialised with Host Interface writes,
 * while the potentially long NAND operation runs without holding the Flash
 * Manager mutex. A page marked WRITING_TO_NAND is immutable, so the producer
 * may continue filling other available pages during that operation.
 *
 * @return true after all currently ready pages were drained; false after a
 *         NAND write, ownership completion, or lifecycle validation failure.
 */
static bool FLASH_MANAGER_DrainInstructionUploadPages( void )
{
    for ( ;; )
    {
        if ( !FLASH_MANAGER_Lock() )
        {
            return false;
        }

        bool drain_allowed =
            ( flash_manager_context.state == FLASH_MANAGER_STATE_INSTRUCTION_UPLOAD )
            || ( flash_manager_context.state == FLASH_MANAGER_STATE_FINALISING_INSTRUCTION_UPLOAD );

        const uint8_t* page_data          = NULL;
        uint32_t       valid_length_bytes = 0U;
        bool           page_acquired      = false;

        if ( drain_allowed )
        {
            page_acquired =
                INSTRUCTION_BUFFER_AcquireUploadDrainPage( &page_data, &valid_length_bytes );
        }

        FLASH_MANAGER_Unlock();

        if ( !drain_allowed )
        {
            return false;
        }

        if ( !page_acquired )
        {
            return true;
        }

        ExternalFlashStatus_T nand_write_status =
            EXTERNAL_FLASH_WriteInstructionPage( page_data, valid_length_bytes );

        if ( !FLASH_MANAGER_Lock() )
        {
            return false;
        }

        bool drain_completion_succeeded =
            INSTRUCTION_BUFFER_CompleteUploadDrain( nand_write_status == EXTERNAL_FLASH_STATUS_OK );

        FLASH_MANAGER_Unlock();

        if ( ( nand_write_status != EXTERNAL_FLASH_STATUS_OK ) || !drain_completion_succeeded )
        {
            return false;
        }
    }
}

/**
 * @brief Persists the final upload pages and closes the external-flash image.
 *
 * @return true after every declared instruction byte is in NAND, external
 *         flash commits the image, upload RAM is released, and the manager
 *         returns to IDLE; otherwise false.
 *
 * @note The final partial page is published by the finish-request API before
 *       this task-context helper runs.
 */
static bool FLASH_MANAGER_FinaliseInstructionUpload( void )
{
    if ( !FLASH_MANAGER_DrainInstructionUploadPages() )
    {
        return false;
    }

    if ( !FLASH_MANAGER_Lock() )
    {
        return false;
    }

    bool finalisation_state_is_valid =
        flash_manager_context.state == FLASH_MANAGER_STATE_FINALISING_INSTRUCTION_UPLOAD;
    bool upload_is_persisted =
        finalisation_state_is_valid && INSTRUCTION_BUFFER_IsUploadPersisted();

    FLASH_MANAGER_Unlock();

    if ( !upload_is_persisted )
    {
        return false;
    }

    if ( EXTERNAL_FLASH_FinishInstructionUpload() != EXTERNAL_FLASH_STATUS_OK )
    {
        return false;
    }

    if ( !FLASH_MANAGER_Lock() )
    {
        return false;
    }

    finalisation_state_is_valid =
        flash_manager_context.state == FLASH_MANAGER_STATE_FINALISING_INSTRUCTION_UPLOAD;

    if ( finalisation_state_is_valid )
    {
        finalisation_state_is_valid = INSTRUCTION_BUFFER_EndUpload();
    }

    if ( finalisation_state_is_valid )
    {
        flash_manager_context.state = FLASH_MANAGER_STATE_IDLE;
    }

    FLASH_MANAGER_Unlock();

    return finalisation_state_is_valid;
}
/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */

/**
 * @brief Runs asynchronous Flash Manager processing.
 */
void FLASH_MANAGER_Task( void* parameters )
{
    uint32_t notification_bits = 0U;

    ( void )parameters;

    /*
     * FLASH_MANAGER_Init() must have completed before the scheduler starts.
     * Registering the handle here keeps task ownership private to this module.
     */
    if ( !FLASH_MANAGER_Lock() )
    {
        /*
         * Returning from a FreeRTOS task is invalid. Latch the startup fault
         * directly because the mutex is unavailable, then permanently park
         * this task while allowing the rest of the scheduler to run.
         */
        flash_manager_context.state = FLASH_MANAGER_STATE_FAULT;

        for ( ;; )
        {
            vTaskDelay( portMAX_DELAY );
        }
    }

    flash_manager_context.task_handle = xTaskGetCurrentTaskHandle();

    FLASH_MANAGER_Unlock();

    for ( ;; )
    {
        notification_bits = 0U;

        if ( xTaskNotifyWait( 0U, UINT32_MAX, &notification_bits, portMAX_DELAY ) != pdTRUE )
        {
            continue;
        }

        /* Handle preparation of a new execution session. */
        if ( ( notification_bits & FLASH_MANAGER_NOTIFY_PREPARE_EXECUTION ) != 0U )
        {
            if ( !FLASH_MANAGER_PrepareExecution() )
            {
                FLASH_MANAGER_EnterFault();
            }
        }

        /* Handle preparation of a new instruction upload. */
        if ( ( notification_bits & FLASH_MANAGER_NOTIFY_PREPARE_INSTRUCTION_UPLOAD ) != 0U )
        {
            if ( !FLASH_MANAGER_PrepareInstructionUpload() )
            {
                FLASH_MANAGER_EnterFault();
            }
        }

        /* Drain completed pages from an active instruction upload. */
        if ( ( notification_bits & FLASH_MANAGER_NOTIFY_DRAIN_INSTRUCTION_UPLOAD ) != 0U )
        {
            if ( !FLASH_MANAGER_DrainInstructionUploadPages() )
            {
                FLASH_MANAGER_EnterFault();
            }
        }

        /* Commit the final instruction page and close the upload. */
        if ( ( notification_bits & FLASH_MANAGER_NOTIFY_FINALISE_INSTRUCTION_UPLOAD ) != 0U )
        {
            if ( !FLASH_MANAGER_FinaliseInstructionUpload() )
            {
                FLASH_MANAGER_EnterFault();
            }
        }

        /* Handle finalisation of result bytes. */
        if ( ( notification_bits & FLASH_MANAGER_NOTIFY_FINALISE_RESULTS ) != 0U )
        {
            /*
             * Finalisation must publish the final partial page before result
             * draining is considered complete.
             */
            if ( !FLASH_MANAGER_FinaliseResults() )
            {
                FLASH_MANAGER_EnterFault();
            }
        }

        /* Handle draining of result bytes. */
        if ( ( notification_bits & FLASH_MANAGER_NOTIFY_DRAIN_RESULTS ) != 0U )
        {
            /*
             * A notification may have been queued while a NAND operation was in
             * progress. Do not retry a failed page after entering FAULT.
             */
            if ( !FLASH_MANAGER_DrainResultPages() )
            {
                FLASH_MANAGER_EnterFault();
            }
        }

        /* Handle refilling of instruction pages. */
        if ( ( notification_bits & FLASH_MANAGER_NOTIFY_REFILL_INSTRUCTIONS ) != 0U )
        {
            if ( !FLASH_MANAGER_FillInstructionPages() )
            {
                FLASH_MANAGER_EnterFault();
            }
        }

        if ( notification_bits & FLASH_MANAGER_NOTIFY_FILL_RESULTS )
        {
            if ( !FLASH_Manager_FillResultPages() )
            {
                FLASH_MANAGER_EnterFault();
            }
        }
    }
}

/**
 * @brief Initialises task synchronisation and both buffer geometries.
 */
bool FLASH_MANAGER_Init( void )
{
    /*
     * This is a one-time startup operation. Reinitialisation would invalidate
     * active leases and recreate synchronization using the same static storage.
     */
    if ( flash_manager_context.access_mutex != NULL )
    {
        return false;
    }

    flash_manager_context.state = FLASH_MANAGER_STATE_UNINITIALISED;

    flash_manager_context.access_mutex =
        xSemaphoreCreateMutexStatic( &flash_manager_mutex_storage );

    if ( flash_manager_context.access_mutex == NULL )
    {
        return false;
    }

    /*
     * External flash must already be initialised so the result buffer can query
     * the active NAND geometry.
     */
    if ( !RESULT_BUFFER_Init() || !INSTRUCTION_BUFFER_Init() )
    {
        flash_manager_context.state = FLASH_MANAGER_STATE_FAULT;

        return false;
    }

    flash_manager_context.state = FLASH_MANAGER_STATE_IDLE;

    return true;
}

/**
 * @brief Reads the current lifecycle state from task context.
 */
bool FLASH_MANAGER_GetState( FlashManagerState_T* state )
{
    if ( state == NULL )
    {
        return false;
    }

    if ( !FLASH_MANAGER_Lock() )
    {
        return false;
    }

    *state = flash_manager_context.state;

    FLASH_MANAGER_Unlock();

    return true;
}

/**
 * @brief Requests task-context preparation of a new execution session.
 */
FlashManagerRequestStatus_T FLASH_MANAGER_RequestExecutionPreparation( void )
{
    if ( flash_manager_context.access_mutex == NULL )
    {
        return FLASH_MANAGER_REQUEST_NOT_INITIALISED;
    }

    if ( !FLASH_MANAGER_Lock() )
    {
        return FLASH_MANAGER_REQUEST_NOT_INITIALISED;
    }

    if ( flash_manager_context.state != FLASH_MANAGER_STATE_IDLE )
    {
        FLASH_MANAGER_Unlock();
        return FLASH_MANAGER_REQUEST_INVALID_STATE;
    }

    TaskHandle_t notification_task_handle = flash_manager_context.task_handle;

    if ( notification_task_handle == NULL )
    {
        FLASH_MANAGER_Unlock();
        return FLASH_MANAGER_REQUEST_TASK_NOT_READY;
    }

    taskENTER_CRITICAL();
    flash_manager_context.state = FLASH_MANAGER_STATE_PREPARING_EXECUTION;
    taskEXIT_CRITICAL();

    FLASH_MANAGER_Unlock();

    if ( xTaskNotify( notification_task_handle, FLASH_MANAGER_NOTIFY_PREPARE_EXECUTION, eSetBits )
         != pdPASS )
    {
        FLASH_MANAGER_EnterFault();
        return FLASH_MANAGER_REQUEST_NOTIFY_FAILED;
    }

    return FLASH_MANAGER_REQUEST_OK;
}

/**
 * @brief Requests task-context publication and draining of final result bytes.
 */
FlashManagerRequestStatus_T FLASH_MANAGER_RequestResultFinalisation( void )
{
    if ( flash_manager_context.access_mutex == NULL )
    {
        return FLASH_MANAGER_REQUEST_NOT_INITIALISED;
    }

    if ( !FLASH_MANAGER_Lock() )
    {
        return FLASH_MANAGER_REQUEST_NOT_INITIALISED;
    }

    if ( flash_manager_context.state != FLASH_MANAGER_STATE_EXECUTING )
    {
        FLASH_MANAGER_Unlock();
        return FLASH_MANAGER_REQUEST_INVALID_STATE;
    }

    TaskHandle_t notification_task_handle = flash_manager_context.task_handle;

    if ( notification_task_handle == NULL )
    {
        FLASH_MANAGER_Unlock();
        return FLASH_MANAGER_REQUEST_TASK_NOT_READY;
    }

    /*
     * Immediately prevent any late ISR call from reserving another result.
     */
    taskENTER_CRITICAL();
    flash_manager_context.state = FLASH_MANAGER_STATE_FINALISING_RESULTS;
    taskEXIT_CRITICAL();

    FLASH_MANAGER_Unlock();

    if ( xTaskNotify( notification_task_handle, FLASH_MANAGER_NOTIFY_FINALISE_RESULTS, eSetBits )
         != pdPASS )
    {
        FLASH_MANAGER_EnterFault();
        return FLASH_MANAGER_REQUEST_NOTIFY_FAILED;
    }

    return FLASH_MANAGER_REQUEST_OK;
}

FlashManagerInstructionUploadRequestStatus_T
FLASH_MANAGER_RequestInstructionUploadStart( uint32_t expected_length_bytes )
{
    /* Check if the Flash Manager is initialised. */
    if ( flash_manager_context.access_mutex == NULL )
    {
        return FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_NOT_INITIALISED;
    }
    /* Check that the expected length is valid. */
    if ( expected_length_bytes == 0U )
    {
        return FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_INVALID_ARGUMENT;
    }

    /* Lock the flash manager */
    if ( !FLASH_MANAGER_Lock() )
    {
        return FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_NOT_INITIALISED;
    }

    /* Check if the Flash Manager is in the idle state to begin an upload. */
    if ( flash_manager_context.state != FLASH_MANAGER_STATE_IDLE )
    {
        FLASH_MANAGER_Unlock();
        return FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_INVALID_STATE;
    }

    TaskHandle_t notification_task_handle = flash_manager_context.task_handle;

    if ( notification_task_handle == NULL )
    {
        FLASH_MANAGER_Unlock();
        return FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_TASK_NOT_READY;
    }

    if ( !INSTRUCTION_BUFFER_PrepareUpload( expected_length_bytes ) )
    {
        FLASH_MANAGER_Unlock();
        return FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_INVALID_ARGUMENT;
    }

    flash_manager_context.state = FLASH_MANAGER_STATE_PREPARING_INSTRUCTION_UPLOAD;

    FLASH_MANAGER_Unlock();

    if ( xTaskNotify( notification_task_handle, FLASH_MANAGER_NOTIFY_PREPARE_INSTRUCTION_UPLOAD,
                      eSetBits )
         != pdPASS )
    {
        FLASH_MANAGER_EnterFault();
        return FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_NOTIFY_FAILED;
    }

    return FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_ACCEPTED;
}

/**
 * @brief Copies one canonical instruction chunk into the upload ring.
 */
FlashManagerInstructionUploadRequestStatus_T
FLASH_MANAGER_SubmitInstructionUploadBytes( const uint8_t* data, uint32_t length )
{
    if ( flash_manager_context.access_mutex == NULL )
    {
        return FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_NOT_INITIALISED;
    }

    if ( ( data == NULL ) || ( length == 0U ) )
    {
        return FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_INVALID_ARGUMENT;
    }

    if ( !FLASH_MANAGER_Lock() )
    {
        return FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_NOT_INITIALISED;
    }

    if ( flash_manager_context.state != FLASH_MANAGER_STATE_INSTRUCTION_UPLOAD )
    {
        FLASH_MANAGER_Unlock();
        return FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_INVALID_STATE;
    }

    TaskHandle_t notification_task_handle = flash_manager_context.task_handle;

    if ( notification_task_handle == NULL )
    {
        FLASH_MANAGER_Unlock();
        return FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_TASK_NOT_READY;
    }

    InstructionBufferUploadWriteStatus_T write_status =
        INSTRUCTION_BUFFER_WriteUploadBytes( data, length );

    FLASH_MANAGER_Unlock();

    switch ( write_status )
    {
        case INSTRUCTION_BUFFER_UPLOAD_WRITE_ACCEPTED:
            return FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_ACCEPTED;

        case INSTRUCTION_BUFFER_UPLOAD_WRITE_BUSY:
            return FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_BUSY;

        case INSTRUCTION_BUFFER_UPLOAD_WRITE_INVALID_ARGUMENT:
            return FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_INVALID_ARGUMENT;

        case INSTRUCTION_BUFFER_UPLOAD_WRITE_INVALID_STATE:
            return FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_INVALID_STATE;

        case INSTRUCTION_BUFFER_UPLOAD_WRITE_PAGE_READY:
            break;

        default:
            FLASH_MANAGER_EnterFault();
            return FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_INVALID_STATE;
    }

    if ( xTaskNotify( notification_task_handle, FLASH_MANAGER_NOTIFY_DRAIN_INSTRUCTION_UPLOAD,
                      eSetBits )
         != pdPASS )
    {
        FLASH_MANAGER_EnterFault();
        return FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_NOTIFY_FAILED;
    }

    return FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_ACCEPTED;
}

/**
 * @brief Stops host production and requests task-context upload completion.
 */
FlashManagerInstructionUploadRequestStatus_T FLASH_MANAGER_RequestInstructionUploadFinish( void )
{
    if ( flash_manager_context.access_mutex == NULL )
    {
        return FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_NOT_INITIALISED;
    }

    if ( !FLASH_MANAGER_Lock() )
    {
        return FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_NOT_INITIALISED;
    }

    if ( flash_manager_context.state != FLASH_MANAGER_STATE_INSTRUCTION_UPLOAD )
    {
        FLASH_MANAGER_Unlock();
        return FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_INVALID_STATE;
    }

    TaskHandle_t notification_task_handle = flash_manager_context.task_handle;

    if ( notification_task_handle == NULL )
    {
        FLASH_MANAGER_Unlock();
        return FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_TASK_NOT_READY;
    }

    if ( !INSTRUCTION_BUFFER_IsUploadInputComplete() )
    {
        FLASH_MANAGER_Unlock();
        return FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_INVALID_STATE;
    }

    /*
     * This publishes a final partial page. It returns false while a NAND write
     * owns the oldest page, allowing the Host Interface to retry unchanged.
     */
    if ( !INSTRUCTION_BUFFER_FinaliseUpload() )
    {
        FLASH_MANAGER_Unlock();
        return FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_BUSY;
    }

    flash_manager_context.state = FLASH_MANAGER_STATE_FINALISING_INSTRUCTION_UPLOAD;

    FLASH_MANAGER_Unlock();

    if ( xTaskNotify( notification_task_handle, FLASH_MANAGER_NOTIFY_FINALISE_INSTRUCTION_UPLOAD,
                      eSetBits )
         != pdPASS )
    {
        FLASH_MANAGER_EnterFault();
        return FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_NOTIFY_FAILED;
    }

    return FLASH_MANAGER_INSTRUCTION_UPLOAD_REQUEST_ACCEPTED;
}

/**
 * @brief Reserves result payload storage from the execution ISR.
 */
bool FLASH_MANAGER_ReserveResultRecordFromISR( uint16_t payload_capacity_bytes,
                                               FlashManagerResultWriteLease_T* lease )
{
    if ( lease == NULL )
    {
        return false;
    }

    /*
     * Never leave stale ownership information with the execution path after a
     * rejected reservation.
     */
    *lease = ( FlashManagerResultWriteLease_T ){ 0 };

    /*
     * This function executes inside the execution timer ISR. It must never
     * take a mutex, block, or invoke a task-context RTOS API.
     *
     * The lifecycle controller must only enter EXECUTING after the Flash
     * Manager task and result buffer are ready.
     */
    if ( flash_manager_context.state != FLASH_MANAGER_STATE_EXECUTING )
    {
        return false;
    }

    return RESULT_BUFFER_ReserveRecord( payload_capacity_bytes, lease );
}

/**
 * @brief Cancels the active result reservation from the execution ISR.
 */
bool FLASH_MANAGER_CancelResultRecordFromISR( const FlashManagerResultWriteLease_T* lease )
{
    /*
     * No mutex is required here. An RTOS task cannot preempt the execution ISR.
     * Task-side buffer operations must later use short critical sections to
     * prevent this ISR from interrupting their metadata changes.
     */
    if ( flash_manager_context.state != FLASH_MANAGER_STATE_EXECUTING )
    {
        return false;
    }

    return RESULT_BUFFER_CancelRecord( lease );
}

/**
 * @brief Commits an ISR-produced result and signals task-context draining.
 */
FlashManagerResultCommitStatus_T FLASH_MANAGER_CommitResultRecordFromISR(
    const FlashManagerResultWriteLease_T* lease, uint32_t timestamp, uint8_t peripheral_type,
    uint8_t channel, uint16_t actual_payload_length_bytes, BaseType_t* higher_priority_task_woken )
{
    if ( flash_manager_context.state != FLASH_MANAGER_STATE_EXECUTING )
    {
        return FLASH_MANAGER_RESULT_COMMIT_INVALID_STATE;
    }

    ResultBufferRecordCommitStatus_T buffer_status = RESULT_BUFFER_CommitRecord(
        lease, timestamp, peripheral_type, channel, actual_payload_length_bytes );

    switch ( buffer_status )
    {
        case RESULT_BUFFER_RECORD_COMMIT_OK:
            return FLASH_MANAGER_RESULT_COMMIT_OK;

        case RESULT_BUFFER_RECORD_COMMIT_PAGE_READY_TO_DRAIN:
            /*
             * The page has already been committed in RAM. Notify the Flash
             * Manager task so it can drain the page after this ISR returns.
             */
            if ( flash_manager_context.task_handle == NULL )
            {
                FLASH_MANAGER_EnterFaultFromISR();
                return FLASH_MANAGER_RESULT_COMMIT_INTERNAL_ERROR;
            }

            if ( xTaskNotifyFromISR( flash_manager_context.task_handle,
                                     FLASH_MANAGER_NOTIFY_DRAIN_RESULTS, eSetBits,
                                     higher_priority_task_woken )
                 != pdPASS )
            {
                FLASH_MANAGER_EnterFaultFromISR();
                return FLASH_MANAGER_RESULT_COMMIT_INTERNAL_ERROR;
            }

            return FLASH_MANAGER_RESULT_COMMIT_OK;

        case RESULT_BUFFER_RECORD_COMMIT_INVALID_LEASE:
            return FLASH_MANAGER_RESULT_COMMIT_INVALID_LEASE;

        case RESULT_BUFFER_RECORD_COMMIT_OVERFLOW:
            return FLASH_MANAGER_RESULT_COMMIT_OVERFLOW;

        default:
            return FLASH_MANAGER_RESULT_COMMIT_INTERNAL_ERROR;
    }
}

/**
 * @brief Exposes the next buffered instruction to the execution ISR.
 *
 * Timestamp comparison remains the caller's responsibility. This function
 * faults only storage availability or record integrity failures.
 */
FlashManagerInstructionReadStatus_T
FLASH_MANAGER_PeekNextInstructionFromISR( const FlashManagerInstructionView_T** instruction )
{
    switch ( INSTRUCTION_BUFFER_PeekInstruction( instruction ) )
    {
        case INSTRUCTION_BUFFER_PEEK_AVAILABLE:
            return FLASH_MANAGER_INSTRUCTION_AVAILABLE;

        case INSTRUCTION_BUFFER_PEEK_NOT_BUFFERED:
            /* Timing can no longer be guaranteed once execution reaches unloaded data. */
            FLASH_MANAGER_EnterFaultFromISR();
            return FLASH_MANAGER_INSTRUCTION_NOT_BUFFERED;

        case INSTRUCTION_BUFFER_PEEK_END_OF_STREAM:
            return FLASH_MANAGER_INSTRUCTION_END_OF_STREAM;

        case INSTRUCTION_BUFFER_PEEK_CORRUPT:
            FLASH_MANAGER_EnterFaultFromISR();
            return FLASH_MANAGER_INSTRUCTION_CORRUPT;

        default:
            FLASH_MANAGER_EnterFaultFromISR();
            return FLASH_MANAGER_INSTRUCTION_CORRUPT;
    }
}

/**
 * @brief Consumes one instruction and signals task-context refill when needed.
 *
 * The caller must consume only an instruction due on the current tick. A future
 * instruction remains cached; a past instruction is an Execution Manager
 * overrun fault and must not be consumed.
 */
bool FLASH_MANAGER_ConsumeInstructionFromISR( BaseType_t* higher_priority_task_woken )
{
    InstructionBufferConsumeStatus_T consume_status = INSTRUCTION_BUFFER_ConsumeInstruction();

    if ( consume_status == INSTRUCTION_BUFFER_CONSUME_OK )
    {
        return true;
    }

    if ( consume_status == INSTRUCTION_BUFFER_CONSUME_REFILL_REQUIRED )
    {
        if ( flash_manager_context.task_handle == NULL )
        {
            FLASH_MANAGER_EnterFaultFromISR();
            return false;
        }

        if ( xTaskNotifyFromISR( flash_manager_context.task_handle,
                                 FLASH_MANAGER_NOTIFY_REFILL_INSTRUCTIONS, eSetBits,
                                 higher_priority_task_woken )
             != pdPASS )
        {
            FLASH_MANAGER_EnterFaultFromISR();
            return false;
        }

        return true;
    }

    return false;
}
