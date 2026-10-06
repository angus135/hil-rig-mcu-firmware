/******************************************************************************
 *  File:       host_interface_state.c
 *  Author:     Timothy Vogelsang
 *  Created:    14-Sep-2026
 *
 *  Description:
 *      TODO
 *  Notes:
 *      TODO
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "host_process_message.h"
#include "config_message_handler.h"
#include "hil_rig_protocol/application/application.h"
#include "hil_rig_protocol/application/application_control.h"
#include "hil_rig_protocol/application/application_error.h"
#include "hil_rig_protocol/application/application_message.h"
#include "hil_rig_protocol/application/application_response.h"
#include "hil_rig_protocol/application/application_rig_status.h"
#include "hil_rig_protocol/application/application_run_report.h"
#include "hil_rig_protocol/transport/transport.h"
#include "hil_rig_protocol/version.h"
#include "host_interface.h"
#include "execution_manager.h"
#include "instruction_message_handler.h"
#include "result_message_producer.h"
#include "variable_instruction_message_handler.h"
#include "variable_result_message_producer.h"
#include "run_state_manager.h"

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */

/** Delay between Run State Manager transition-status polls. */
#define HOST_INTERFACE_STATE_TRANSITION_RETRY_DELAY_MS ( 10U )

/**
 * Package preparation wait covering the RSM's 15-second Flash timeout.
 *
 * The 16-second bound leaves scheduling margin while remaining below the
 * application's 30-second response timeout.
 */
#define HOST_INTERFACE_PACKAGE_RECEIVE_WAIT_TIMEOUT_MS ( 16000U )
#define HOST_INTERFACE_PACKAGE_RECEIVE_WAIT_ATTEMPTS                                               \
    ( HOST_INTERFACE_PACKAGE_RECEIVE_WAIT_TIMEOUT_MS                                               \
      / HOST_INTERFACE_STATE_TRANSITION_RETRY_DELAY_MS )

/** Wait covering the RSM's asynchronous 15-second DUT shutdown timeout. */
#define HOST_INTERFACE_POST_REPORT_RESET_WAIT_TIMEOUT_MS ( 16000U )
#define HOST_INTERFACE_POST_REPORT_RESET_WAIT_ATTEMPTS                                             \
    ( HOST_INTERFACE_POST_REPORT_RESET_WAIT_TIMEOUT_MS                                             \
      / HOST_INTERFACE_STATE_TRANSITION_RETRY_DELAY_MS )

/**-----------------------------------------------------------------------------
 *  Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Public (global) and Extern Variables
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Private (static) Variables
 *------------------------------------------------------------------------------
 */

/* The test session context */
static HostTestSession_T s_session = {
    .state                = HOST_INTERFACE_SESSION_STATE_IDLE,
    .has_active_test_id   = false,
    .active_test_id       = { { 0 } },
    .instruction_family   = HOST_INSTRUCTION_FAMILY_UNSET,
    .expected_tick_count  = 0U,
    .tick_period_us       = 0U,
    .result_ticks_emitted = 0U,
    .report_owed          = false,
    .report_in_flight     = false,
    .last_failure_source  = HIL_APPLICATION_FAILURE_SOURCE_NONE,
    .last_failure_stage   = HIL_APPLICATION_FAILURE_STAGE_NONE,
    .last_failure_reason  = HIL_APPLICATION_FAILURE_REASON_NONE,
};

/**-----------------------------------------------------------------------------
 *  Private (static) Function Prototypes
 *------------------------------------------------------------------------------
 */

void HOST_INTERFACE_Reset_Session( void )
{
    s_session.state              = HOST_INTERFACE_SESSION_STATE_IDLE;
    s_session.has_active_test_id = false;
    ( void )memset( &s_session.active_test_id, 0, sizeof( s_session.active_test_id ) );
    s_session.instruction_family   = HOST_INSTRUCTION_FAMILY_UNSET;
    s_session.expected_tick_count  = 0U;
    s_session.tick_period_us       = 0U;
    s_session.result_ticks_emitted = 0U;
    s_session.report_owed          = false;
    s_session.report_in_flight     = false;
    s_session.last_failure_source  = HIL_APPLICATION_FAILURE_SOURCE_NONE;
    s_session.last_failure_stage   = HIL_APPLICATION_FAILURE_STAGE_NONE;
    s_session.last_failure_reason  = HIL_APPLICATION_FAILURE_REASON_NONE;
    HOST_INSTRUCTION_HANDLER_Reset();
    RESULT_MESSAGE_PRODUCER_Reset();
    HOST_VARIABLE_INSTRUCTION_HANDLER_Reset();
    VARIABLE_RESULT_MESSAGE_PRODUCER_Reset();
}

const HostTestSession_T* HOST_INTERFACE_Get_Session( void )
{
    return &s_session;
}

/**
 * @brief Helper to validate that an incoming message's Test ID matches the session.
 */
static bool HOST_INTERFACE_Validate_Test_Id( const HIL_Application_Message_T* incoming_message )
{
    if ( !s_session.has_active_test_id )
    {
        return true;
    }
    if ( ( incoming_message->has_test_id == 0U )
         || ( memcmp( incoming_message->test_id.bytes, s_session.active_test_id.bytes,
                      sizeof( s_session.active_test_id.bytes ) )
              != 0 ) )
    {
        return false;
    }
    return true;
}

/**
 * @brief Construct a generic error message
 *
 * @details is given a pointer to a message and fills it with default error values
 *
 * @param[in] incoming_message         the info request message
 * @param[out] outgoing_message               if required the message to respond with
 * @param[out] response_required              whether or not a response is required
 * @param[out] data                           the optional additional date for variable length byte
spans
 * @param[out] data_size                      the size available to write to at data
 * @return HOST_INTERFACE_STATUS_OK if the message is processed succesfully
 */
HOST_Interface_Status_T HOST_INTERFACE_Default_Error( HIL_Application_Message_T* message )
{
    // Set the type and subtype
    message->type    = HIL_APPLICATION_MESSAGE_TYPE_ERROR;
    message->subtype = HIL_APPLICATION_MESSAGE_SUBTYPE_NONE;
    // Set Error body TODO update error catagory
    message->body.error.category             = HIL_APPLICATION_ERROR_CATEGORY_INVALID;
    message->body.error.recoverable          = 1U;
    message->body.error.has_tick_number      = 0U;
    message->body.error.tick_number          = 0U;
    message->body.error.detail               = 0U;
    message->body.error.diagnostic_data.size = 0U;
    message->body.error.diagnostic_data.data = NULL;
    return HOST_INTERFACE_STATUS_OK;
}

/** Builds the correlated Application Response for an Execution Control request. */
static void HOST_INTERFACE_BuildExecutionControlResponse(
    HIL_Application_Message_T* message, HIL_Application_Response_Outcome_T outcome,
    HIL_Application_Response_Reason_T reason, HIL_Application_Control_Command_T command )
{
    message->type                          = HIL_APPLICATION_MESSAGE_TYPE_RESPONSE;
    message->subtype                       = HIL_APPLICATION_MESSAGE_SUBTYPE_NONE;
    message->body.response.scope           = HIL_APPLICATION_RESPONSE_SCOPE_EXECUTION_CONTROL;
    message->body.response.outcome         = outcome;
    message->body.response.reason          = reason;
    message->body.response.tick_number     = 0U;
    message->body.response.control_command = command;
    message->body.response.global_control_command = HIL_APPLICATION_GLOBAL_CONTROL_INVALID;
    message->body.response.detail                 = 0U;
}

/** Builds the correlated Application Response for a Global Control request. */
static void HOST_INTERFACE_BuildGlobalControlResponse(
    HIL_Application_Message_T* message, HIL_Application_Response_Outcome_T outcome,
    HIL_Application_Response_Reason_T reason, HIL_Application_Global_Control_Command_T command,
    uint32_t detail )
{
    message->type                                 = HIL_APPLICATION_MESSAGE_TYPE_RESPONSE;
    message->subtype                              = HIL_APPLICATION_MESSAGE_SUBTYPE_NONE;
    message->has_test_id                          = 0U;
    message->body.response.scope                  = HIL_APPLICATION_RESPONSE_SCOPE_GLOBAL_CONTROL;
    message->body.response.outcome                = outcome;
    message->body.response.reason                 = reason;
    message->body.response.tick_number            = 0U;
    message->body.response.control_command        = HIL_APPLICATION_CONTROL_INVALID;
    message->body.response.global_control_command = command;
    message->body.response.detail                 = detail;
}

/** Maps one native first-cause failure into stable schema-1 wire values. */
static void HOST_INTERFACE_MapRunFailure( const RunMetadataSnapshot_T*  snapshot,
                                          HIL_Application_Run_Report_T* report )
{
    switch ( snapshot->failure_source )
    {
        case RUN_METADATA_FAILURE_SOURCE_NONE:
            report->failure_source = HIL_APPLICATION_FAILURE_SOURCE_NONE;
            report->failure_stage  = HIL_APPLICATION_FAILURE_STAGE_NONE;
            report->failure_reason = HIL_APPLICATION_FAILURE_REASON_NONE;
            return;

        case RUN_METADATA_FAILURE_SOURCE_EXECUTION_MANAGER:
            report->failure_source = HIL_APPLICATION_FAILURE_SOURCE_EXECUTION_MANAGER;
            report->failure_stage  = HIL_APPLICATION_FAILURE_STAGE_EXECUTION;
            switch ( ( ExecutionManagerFailure_T )snapshot->failure_reason )
            {
                case EXECUTION_MANAGER_FAILURE_NOT_PREPARED:
                    report->failure_reason = HIL_APPLICATION_FAILURE_REASON_EXECUTION_NOT_PREPARED;
                    return;
                case EXECUTION_MANAGER_FAILURE_INSTRUCTION_UNDERRUN:
                    report->failure_reason = HIL_APPLICATION_FAILURE_REASON_INSTRUCTION_UNDERRUN;
                    return;
                case EXECUTION_MANAGER_FAILURE_INSTRUCTION_CORRUPT:
                    report->failure_reason = HIL_APPLICATION_FAILURE_REASON_INSTRUCTION_CORRUPT;
                    return;
                case EXECUTION_MANAGER_FAILURE_INSTRUCTION_LATE:
                    report->failure_reason = HIL_APPLICATION_FAILURE_REASON_INSTRUCTION_LATE;
                    return;
                case EXECUTION_MANAGER_FAILURE_OPERATION_REJECTED:
                    report->failure_reason = HIL_APPLICATION_FAILURE_REASON_OPERATION_REJECTED;
                    return;
                case EXECUTION_MANAGER_FAILURE_INSTRUCTION_CONSUME:
                    report->failure_reason =
                        HIL_APPLICATION_FAILURE_REASON_INSTRUCTION_CONSUME_FAILED;
                    return;
                case EXECUTION_MANAGER_FAILURE_MEASUREMENT_REJECTED:
                    report->failure_reason = HIL_APPLICATION_FAILURE_REASON_MEASUREMENT_REJECTED;
                    return;
                case EXECUTION_MANAGER_FAILURE_INSTRUCTION_UNCONSUMED:
                    report->failure_reason = HIL_APPLICATION_FAILURE_REASON_INSTRUCTION_UNCONSUMED;
                    return;
                case EXECUTION_MANAGER_FAILURE_NONE:
                default:
                    report->failure_reason = HIL_APPLICATION_FAILURE_REASON_INTERNAL_FAILURE;
                    return;
            }

        case RUN_METADATA_FAILURE_SOURCE_FLASH_MANAGER:
            report->failure_source = HIL_APPLICATION_FAILURE_SOURCE_FLASH_MANAGER;
            break;
        case RUN_METADATA_FAILURE_SOURCE_HOST_INTERFACE:
            report->failure_source = HIL_APPLICATION_FAILURE_SOURCE_HOST_INTERFACE;
            break;
        case RUN_METADATA_FAILURE_SOURCE_RUN_STATE_MANAGER:
        default:
            report->failure_source = HIL_APPLICATION_FAILURE_SOURCE_RUN_STATE_MANAGER;
            break;
    }

    switch ( ( RunStateFaultReason_T )snapshot->failure_reason )
    {
        case RUN_STATE_FAULT_EXTERNAL_REQUEST:
            report->failure_stage  = HIL_APPLICATION_FAILURE_STAGE_CLEANUP;
            report->failure_reason = HIL_APPLICATION_FAILURE_REASON_HOST_ABORT;
            break;
        case RUN_STATE_FAULT_INVALID_TRANSITION:
            report->failure_stage  = HIL_APPLICATION_FAILURE_STAGE_PREPARATION;
            report->failure_reason = HIL_APPLICATION_FAILURE_REASON_INVALID_LIFECYCLE_STATE;
            break;
        case RUN_STATE_FAULT_LOGIC_EXPANDER_NOT_READY:
            report->failure_stage  = HIL_APPLICATION_FAILURE_STAGE_PREPARATION;
            report->failure_reason = HIL_APPLICATION_FAILURE_REASON_HARDWARE_NOT_READY;
            break;
        case RUN_STATE_FAULT_CONFIGURATION_UNAVAILABLE:
            report->failure_stage  = HIL_APPLICATION_FAILURE_STAGE_PREPARATION;
            report->failure_reason = HIL_APPLICATION_FAILURE_REASON_CONFIGURATION_UNAVAILABLE;
            break;
        case RUN_STATE_FAULT_DRIVER_CONFIGURATION:
            report->failure_source = HIL_APPLICATION_FAILURE_SOURCE_DRIVER_LIFECYCLE;
            report->failure_stage  = HIL_APPLICATION_FAILURE_STAGE_PREPARATION;
            report->failure_reason = HIL_APPLICATION_FAILURE_REASON_DRIVER_CONFIGURATION_FAILED;
            break;
        case RUN_STATE_FAULT_DRIVER_CONFIGURATION_TIMEOUT:
            report->failure_source = HIL_APPLICATION_FAILURE_SOURCE_DRIVER_LIFECYCLE;
            report->failure_stage  = HIL_APPLICATION_FAILURE_STAGE_PREPARATION;
            report->failure_reason = HIL_APPLICATION_FAILURE_REASON_DRIVER_CONFIGURATION_TIMEOUT;
            break;
        case RUN_STATE_FAULT_DRIVER_START:
            report->failure_source = HIL_APPLICATION_FAILURE_SOURCE_DRIVER_LIFECYCLE;
            report->failure_stage  = HIL_APPLICATION_FAILURE_STAGE_PREPARATION;
            report->failure_reason = HIL_APPLICATION_FAILURE_REASON_DRIVER_START_FAILED;
            break;
        case RUN_STATE_FAULT_DRIVER_START_TIMEOUT:
            report->failure_source = HIL_APPLICATION_FAILURE_SOURCE_DRIVER_LIFECYCLE;
            report->failure_stage  = HIL_APPLICATION_FAILURE_STAGE_PREPARATION;
            report->failure_reason = HIL_APPLICATION_FAILURE_REASON_DRIVER_START_TIMEOUT;
            break;
        case RUN_STATE_FAULT_ACQUISITION_EPOCH:
            report->failure_source = HIL_APPLICATION_FAILURE_SOURCE_DRIVER_LIFECYCLE;
            report->failure_stage  = HIL_APPLICATION_FAILURE_STAGE_PREPARATION;
            report->failure_reason = HIL_APPLICATION_FAILURE_REASON_ACQUISITION_EPOCH_FAILED;
            break;
        case RUN_STATE_FAULT_DRIVER_STOP:
            report->failure_source = HIL_APPLICATION_FAILURE_SOURCE_DRIVER_LIFECYCLE;
            report->failure_stage  = HIL_APPLICATION_FAILURE_STAGE_SHUTDOWN;
            report->failure_reason = HIL_APPLICATION_FAILURE_REASON_DRIVER_STOP_FAILED;
            break;
        case RUN_STATE_FAULT_DRIVER_STOP_TIMEOUT:
            report->failure_source = HIL_APPLICATION_FAILURE_SOURCE_DRIVER_LIFECYCLE;
            report->failure_stage  = HIL_APPLICATION_FAILURE_STAGE_SHUTDOWN;
            report->failure_reason = HIL_APPLICATION_FAILURE_REASON_DRIVER_STOP_TIMEOUT;
            break;
        case RUN_STATE_FAULT_EXECUTION_TIMER:
            report->failure_source = HIL_APPLICATION_FAILURE_SOURCE_EXECUTION_TIMER;
            report->failure_stage  = HIL_APPLICATION_FAILURE_STAGE_EXECUTION;
            report->failure_reason = HIL_APPLICATION_FAILURE_REASON_EXECUTION_TIMER_FAILED;
            break;
        case RUN_STATE_FAULT_FLASH_EXECUTION_PREPARATION:
            report->failure_stage  = HIL_APPLICATION_FAILURE_STAGE_PREPARATION;
            report->failure_reason = HIL_APPLICATION_FAILURE_REASON_FLASH_PREPARATION_FAILED;
            break;
        case RUN_STATE_FAULT_FLASH_EXECUTION_PREPARATION_TIMEOUT:
            report->failure_stage  = HIL_APPLICATION_FAILURE_STAGE_PREPARATION;
            report->failure_reason = HIL_APPLICATION_FAILURE_REASON_FLASH_PREPARATION_TIMEOUT;
            break;
        case RUN_STATE_FAULT_FLASH_RESULT_FINALISATION:
            report->failure_stage  = HIL_APPLICATION_FAILURE_STAGE_FINALISATION;
            report->failure_reason = HIL_APPLICATION_FAILURE_REASON_FLASH_FINALISATION_FAILED;
            break;
        case RUN_STATE_FAULT_FLASH_RESULT_FINALISATION_TIMEOUT:
            report->failure_stage  = HIL_APPLICATION_FAILURE_STAGE_FINALISATION;
            report->failure_reason = HIL_APPLICATION_FAILURE_REASON_FLASH_FINALISATION_TIMEOUT;
            break;
        case RUN_STATE_FAULT_FLASH_RESULT_TRANSFER:
            report->failure_stage  = HIL_APPLICATION_FAILURE_STAGE_TRANSFER;
            report->failure_reason = HIL_APPLICATION_FAILURE_REASON_RESULT_TRANSFER_FAILED;
            break;
        case RUN_STATE_FAULT_FLASH_RESULT_DISPOSITION:
            report->failure_stage  = HIL_APPLICATION_FAILURE_STAGE_CLEANUP;
            report->failure_reason = HIL_APPLICATION_FAILURE_REASON_RESULT_DISPOSITION_FAILED;
            break;
        case RUN_STATE_FAULT_FLASH_MANAGER:
            report->failure_stage  = HIL_APPLICATION_FAILURE_STAGE_EXECUTION;
            report->failure_reason = HIL_APPLICATION_FAILURE_REASON_FLASH_MANAGER_FAILED;
            break;
        case RUN_STATE_FAULT_HOST_INTERFACE_RESPONSE_BLOCKED:
            report->failure_stage  = HIL_APPLICATION_FAILURE_STAGE_TRANSFER;
            report->failure_reason = HIL_APPLICATION_FAILURE_REASON_HOST_RESPONSE_BLOCKED;
            break;
        case RUN_STATE_FAULT_HOST_INTERFACE_INSTRUCTION_UPLOAD_TIMEOUT:
            report->failure_stage  = HIL_APPLICATION_FAILURE_STAGE_PREPARATION;
            report->failure_reason = HIL_APPLICATION_FAILURE_REASON_INSTRUCTION_UPLOAD_TIMEOUT;
            break;
        case RUN_STATE_FAULT_HOST_INTERFACE_USB_INIT:
            report->failure_stage  = HIL_APPLICATION_FAILURE_STAGE_TRANSFER;
            report->failure_reason = HIL_APPLICATION_FAILURE_REASON_USB_INITIALISATION_FAILED;
            break;
        case RUN_STATE_FAULT_HOST_INTERFACE_CODEC_INIT:
            report->failure_stage  = HIL_APPLICATION_FAILURE_STAGE_TRANSFER;
            report->failure_reason = HIL_APPLICATION_FAILURE_REASON_CODEC_INITIALISATION_FAILED;
            break;
        case RUN_STATE_FAULT_HOST_INTERFACE_TRANSPORT_INIT:
            report->failure_stage  = HIL_APPLICATION_FAILURE_STAGE_TRANSFER;
            report->failure_reason = HIL_APPLICATION_FAILURE_REASON_TRANSPORT_INITIALISATION_FAILED;
            break;
        case RUN_STATE_FAULT_HOST_INTERFACE_ERROR:
            report->failure_stage  = HIL_APPLICATION_FAILURE_STAGE_TRANSFER;
            report->failure_reason = HIL_APPLICATION_FAILURE_REASON_HOST_INTERFACE_FAILED;
            break;
        case RUN_STATE_FAULT_NONE:
        case RUN_STATE_FAULT_EXECUTION_MANAGER:
        case RUN_STATE_FAULT_INTERNAL:
        default:
            report->failure_stage  = HIL_APPLICATION_FAILURE_STAGE_EXECUTION;
            report->failure_reason = HIL_APPLICATION_FAILURE_REASON_INTERNAL_FAILURE;
            break;
    }
}

/** Maps the RSM and Host Interface snapshot into a public schema-1 Rig Status. */
static void HOST_INTERFACE_BuildRigStatus( HIL_Application_Message_T*      message,
                                           HIL_Application_Status_Origin_T origin )
{
    RunStateManagerStatus_T rsm_status = { 0 };
    RUN_STATE_MANAGER_GetStatus( &rsm_status );

    message->type        = HIL_APPLICATION_MESSAGE_TYPE_RIG_STATUS;
    message->subtype     = HIL_APPLICATION_MESSAGE_SUBTYPE_NONE;
    message->has_test_id = s_session.has_active_test_id ? 1U : 0U;
    if ( s_session.has_active_test_id )
    {
        message->test_id = s_session.active_test_id;
    }

    HIL_Application_Rig_Status_T* status = &message->body.rig_status;
    ( void )memset( status, 0, sizeof( *status ) );
    status->schema_version = 1U;
    status->origin         = origin;

    switch ( rsm_status.state )
    {
        case RUN_STATE_IDLE:
            status->state = HIL_APPLICATION_RIG_STATE_IDLE;
            break;
        case RUN_STATE_TEST_PACKAGE_RECEIVE:
            status->state = HIL_APPLICATION_RIG_STATE_UPLOADING;
            break;
        case RUN_STATE_CONFIGURATION:
            status->state = HIL_APPLICATION_RIG_STATE_CONFIGURING;
            break;
        case RUN_STATE_ARMED:
            status->state = HIL_APPLICATION_RIG_STATE_ARMED;
            break;
        case RUN_STATE_EXECUTION:
            status->state = HIL_APPLICATION_RIG_STATE_RUNNING;
            break;
        case RUN_STATE_RESULT_FINALISATION:
            status->state = HIL_APPLICATION_RIG_STATE_FINALISING;
            break;
        case RUN_STATE_RESULTS_READY:
            status->state = HIL_APPLICATION_RIG_STATE_RESULTS_READY;
            break;
        case RUN_STATE_RESULT_TRANSFER:
            status->state = ( s_session.state == HOST_INTERFACE_SESSION_AWAITING_RESET )
                                ? HIL_APPLICATION_RIG_STATE_RESULTS_READY
                                : HIL_APPLICATION_RIG_STATE_TRANSFERRING;
            break;
        case RUN_STATE_FAULT:
            status->state = HIL_APPLICATION_RIG_STATE_FAULT;
            break;
        default:
            status->state = HIL_APPLICATION_RIG_STATE_INITIALISING;
            break;
    }

    const bool ready =
        rsm_status.state == RUN_STATE_IDLE && s_session.state == HOST_INTERFACE_SESSION_STATE_IDLE
        && !rsm_status.transition_pending && !s_session.report_owed && !s_session.report_in_flight;
    if ( ready )
    {
        status->flags |= HIL_APPLICATION_RIG_STATUS_READY_FOR_NEW_TEST;
    }
    if ( rsm_status.transition_pending )
    {
        status->flags |= HIL_APPLICATION_RIG_STATUS_TRANSITION_PENDING;
    }
    if ( !rsm_status.transition_pending && !rsm_status.execution_active && !s_session.report_owed
         && !s_session.report_in_flight )
    {
        status->flags |= HIL_APPLICATION_RIG_STATUS_RESET_PERMITTED;
    }
    if ( rsm_status.execution_active )
    {
        status->flags |= HIL_APPLICATION_RIG_STATUS_EXECUTION_ACTIVE;
    }

    status->failure_source = s_session.last_failure_source;
    status->failure_stage  = s_session.last_failure_stage;
    status->failure_reason = s_session.last_failure_reason;

    if ( rsm_status.state == RUN_STATE_FAULT
         && status->failure_reason == HIL_APPLICATION_FAILURE_REASON_NONE )
    {
        RunMetadataSnapshot_T snapshot = { 0 };
        snapshot.failure_source        = RUN_METADATA_FAILURE_SOURCE_RUN_STATE_MANAGER;
        snapshot.failure_reason        = ( uint32_t )rsm_status.fault_reason;
        switch ( rsm_status.fault_reason )
        {
            case RUN_STATE_FAULT_EXECUTION_MANAGER:
                snapshot.failure_source = RUN_METADATA_FAILURE_SOURCE_EXECUTION_MANAGER;
                snapshot.failure_reason = ( uint32_t )EXECUTION_MANAGER_GetFailure();
                break;
            case RUN_STATE_FAULT_FLASH_MANAGER:
            case RUN_STATE_FAULT_FLASH_EXECUTION_PREPARATION:
            case RUN_STATE_FAULT_FLASH_EXECUTION_PREPARATION_TIMEOUT:
            case RUN_STATE_FAULT_FLASH_RESULT_FINALISATION:
            case RUN_STATE_FAULT_FLASH_RESULT_FINALISATION_TIMEOUT:
            case RUN_STATE_FAULT_FLASH_RESULT_TRANSFER:
            case RUN_STATE_FAULT_FLASH_RESULT_DISPOSITION:
                snapshot.failure_source = RUN_METADATA_FAILURE_SOURCE_FLASH_MANAGER;
                break;
            case RUN_STATE_FAULT_HOST_INTERFACE_RESPONSE_BLOCKED:
            case RUN_STATE_FAULT_HOST_INTERFACE_INSTRUCTION_UPLOAD_TIMEOUT:
            case RUN_STATE_FAULT_HOST_INTERFACE_USB_INIT:
            case RUN_STATE_FAULT_HOST_INTERFACE_CODEC_INIT:
            case RUN_STATE_FAULT_HOST_INTERFACE_TRANSPORT_INIT:
            case RUN_STATE_FAULT_HOST_INTERFACE_ERROR:
                snapshot.failure_source = RUN_METADATA_FAILURE_SOURCE_HOST_INTERFACE;
                break;
            case RUN_STATE_FAULT_NONE:
            case RUN_STATE_FAULT_EXTERNAL_REQUEST:
            case RUN_STATE_FAULT_INVALID_TRANSITION:
            case RUN_STATE_FAULT_LOGIC_EXPANDER_NOT_READY:
            case RUN_STATE_FAULT_CONFIGURATION_UNAVAILABLE:
            case RUN_STATE_FAULT_DRIVER_CONFIGURATION:
            case RUN_STATE_FAULT_DRIVER_CONFIGURATION_TIMEOUT:
            case RUN_STATE_FAULT_DRIVER_START:
            case RUN_STATE_FAULT_DRIVER_START_TIMEOUT:
            case RUN_STATE_FAULT_ACQUISITION_EPOCH:
            case RUN_STATE_FAULT_DRIVER_STOP:
            case RUN_STATE_FAULT_DRIVER_STOP_TIMEOUT:
            case RUN_STATE_FAULT_EXECUTION_TIMER:
            case RUN_STATE_FAULT_INTERNAL:
            default:
                break;
        }

        HIL_Application_Run_Report_T mapped = { 0 };
        HOST_INTERFACE_MapRunFailure( &snapshot, &mapped );
        status->failure_source = mapped.failure_source;
        status->failure_stage  = mapped.failure_stage;
        status->failure_reason = mapped.failure_reason;
    }
}

/** Builds the schema-1 report from the RSM-owned sealed metadata snapshot. */
static bool HOST_INTERFACE_BuildRunReport( HIL_Application_Message_T* message )
{
    RunMetadataSnapshot_T snapshot = { 0 };
    if ( message == NULL || !s_session.report_owed || !s_session.has_active_test_id
         || !RUN_STATE_MANAGER_GetRunMetadataSnapshot( &snapshot ) )
    {
        return false;
    }

    HIL_Application_Run_Report_T* report            = &message->body.run_report;
    const bool                    execution_started = RUN_STATE_MANAGER_DidExecutionStart();
    ( void )memset( report, 0, sizeof( *report ) );
    message->type        = HIL_APPLICATION_MESSAGE_TYPE_RUN_REPORT;
    message->subtype     = HIL_APPLICATION_MESSAGE_SUBTYPE_NONE;
    message->has_test_id = 1U;
    message->test_id     = s_session.active_test_id;

    report->schema_version       = 1U;
    report->valid_sections       = snapshot.valid_sections;
    report->expected_tick_count  = s_session.expected_tick_count;
    report->tick_period_us       = s_session.tick_period_us;
    report->result_ticks_emitted = s_session.result_ticks_emitted;

    switch ( snapshot.terminal_status )
    {
        case RUN_METADATA_TERMINAL_COMPLETE:
            report->run_outcome       = HIL_APPLICATION_RUN_OUTCOME_SUCCESS;
            report->execution_outcome = HIL_APPLICATION_EXECUTION_OUTCOME_COMPLETE;
            break;
        case RUN_METADATA_TERMINAL_FAILED:
            report->run_outcome       = HIL_APPLICATION_RUN_OUTCOME_FAILED;
            report->execution_outcome = execution_started
                                            ? HIL_APPLICATION_EXECUTION_OUTCOME_FAILED
                                            : HIL_APPLICATION_EXECUTION_OUTCOME_NOT_STARTED;
            break;
        case RUN_METADATA_TERMINAL_ABORTED:
            report->run_outcome       = HIL_APPLICATION_RUN_OUTCOME_ABORTED;
            report->execution_outcome = execution_started
                                            ? HIL_APPLICATION_EXECUTION_OUTCOME_ABORTED
                                            : HIL_APPLICATION_EXECUTION_OUTCOME_NOT_STARTED;
            break;
        case RUN_METADATA_TERMINAL_REJECTED:
            report->run_outcome       = HIL_APPLICATION_RUN_OUTCOME_REJECTED;
            report->execution_outcome = HIL_APPLICATION_EXECUTION_OUTCOME_NOT_STARTED;
            break;
        case RUN_METADATA_TERMINAL_PENDING:
        default:
            return false;
    }

    switch ( snapshot.result_stream_status )
    {
        case RUN_METADATA_RESULT_STREAM_COMPLETE:
            report->result_status = HIL_APPLICATION_RUN_RESULT_STATUS_COMPLETE;
            break;
        case RUN_METADATA_RESULT_STREAM_PARTIAL:
            report->result_status = HIL_APPLICATION_RUN_RESULT_STATUS_PARTIAL;
            break;
        case RUN_METADATA_RESULT_STREAM_UNAVAILABLE:
            report->result_status = HIL_APPLICATION_RUN_RESULT_STATUS_UNAVAILABLE;
            break;
        case RUN_METADATA_RESULT_STREAM_PENDING:
        default:
            return false;
    }

    report->last_completed_boundary         = snapshot.last_completed_boundary;
    report->isr_timing.sample_count         = snapshot.isr_timing.sample_count;
    report->isr_timing.total_cycles         = snapshot.isr_timing.total_cycles;
    report->isr_timing.minimum_cycles       = snapshot.isr_timing.minimum_cycles;
    report->isr_timing.maximum_cycles       = snapshot.isr_timing.maximum_cycles;
    report->isr_timing.maximum_boundary     = snapshot.isr_timing.maximum_boundary;
    report->instruction_buffer.sample_count = snapshot.instruction_buffer.sample_count;
    report->instruction_buffer.minimum_unread_bytes =
        snapshot.instruction_buffer.minimum_unread_bytes;
    report->instruction_buffer.minimum_boundary  = snapshot.instruction_buffer.minimum_boundary;
    report->result_buffer.committed_record_count = snapshot.result_buffer.committed_record_count;
    report->result_buffer.committed_bytes        = snapshot.result_buffer.committed_bytes;
    report->result_buffer.peak_pending_bytes     = snapshot.result_buffer.peak_pending_bytes;
    report->result_buffer.peak_pending_boundary  = snapshot.result_buffer.peak_pending_boundary;
    report->result_buffer.reserve_failure_count  = snapshot.result_buffer.reserve_failure_count;
    report->result_buffer.commit_failure_count   = snapshot.result_buffer.commit_failure_count;
    report->flash.result_pages_drained           = snapshot.flash_throughput.result_pages_drained;
    report->flash.result_bytes_drained           = snapshot.flash_throughput.result_bytes_drained;
    report->flash.result_drain_total_cycles = snapshot.flash_throughput.result_drain_total_cycles;
    report->flash.result_drain_maximum_cycles =
        snapshot.flash_throughput.result_drain_maximum_cycles;
    report->flash.instruction_pages_refilled = snapshot.flash_throughput.instruction_pages_refilled;
    report->flash.instruction_bytes_refilled = snapshot.flash_throughput.instruction_bytes_refilled;
    report->flash.instruction_refill_total_cycles =
        snapshot.flash_throughput.instruction_refill_total_cycles;
    report->flash.instruction_refill_maximum_cycles =
        snapshot.flash_throughput.instruction_refill_maximum_cycles;
    report->flash.instruction_publish_sample_count =
        snapshot.flash_throughput.instruction_publish_sample_count;
    report->flash.instruction_publish_total_cycles =
        snapshot.flash_throughput.instruction_publish_total_cycles;
    report->flash.instruction_publish_maximum_cycles =
        snapshot.flash_throughput.instruction_publish_maximum_cycles;
    report->flash.service_gap_sample_count   = snapshot.flash_throughput.service_gap_sample_count;
    report->flash.service_gap_total_cycles   = snapshot.flash_throughput.service_gap_total_cycles;
    report->flash.service_gap_maximum_cycles = snapshot.flash_throughput.service_gap_maximum_cycles;
    report->flash.refill_drain_contention_count =
        snapshot.flash_throughput.refill_drain_contention_count;
    HOST_INTERFACE_MapRunFailure( &snapshot, report );
    s_session.last_failure_source = report->failure_source;
    s_session.last_failure_stage  = report->failure_stage;
    s_session.last_failure_reason = report->failure_reason;
    return true;
}

/**
 * @brief Takes the state you want to transition to and calls the associated request function.
 *
 * @details expected tick count is used by the execution start request but for all others it can be
 *          0
 *          If the request shouldnt be supported by the host interface it will reutrn
 *          HOST_INTERFACE_STATUS_UNSUPPORTED_MESSAGE
 *
 * @param[in] expected_state                  The state we want to transition to
 * @param[in] expected_tick_count             Only used for execution request
 *
 * @return HOST_INTERFACE_STATUS_OK if the message is processed succesfully
 */
HOST_Interface_Status_T HOST_INTERFACE_state_to_state_request( Host_RunState_Request_T request,
                                                               uint32_t expected_tick_count )
{
    RunStateExecutionRequest_T execution_request = { 0 };
    execution_request.tick_count                 = expected_tick_count;
    execution_request.enable_drain_tail =
        ( s_session.instruction_family == HOST_INSTRUCTION_FAMILY_LEGACY_FIXED );
    RunStateFaultReason_T fault_request = RUN_STATE_FAULT_EXTERNAL_REQUEST;
    switch ( request )
    {
        case HOST_REQUEST_IDLE:
            return HOST_INTERFACE_STATUS_UNSUPPORTED_MESSAGE;
        case HOST_REQUEST_TEST_PACKAGE_RECEIVE:
            if ( RUN_STATE_MANAGER_RequestPackageReceiveWithTicks( expected_tick_count ) != true )
            {
                return HOST_INTERFACE_STATUS_STATE_TRANSITION_FAILURE;
            }
            return HOST_INTERFACE_STATUS_OK;
        case HOST_REQUEST_CONFIGURATION:
            if ( RUN_STATE_MANAGER_RequestConfiguration() != true )
            {
                return HOST_INTERFACE_STATUS_STATE_TRANSITION_FAILURE;
            }
            return HOST_INTERFACE_STATUS_OK;
        case HOST_REQUEST_ARMED:
            return HOST_INTERFACE_STATUS_UNSUPPORTED_MESSAGE;
        case HOST_REQUEST_EXECUTION:
            if ( RUN_STATE_MANAGER_RequestExecution( &execution_request )
                 != RUN_STATE_EXECUTION_REQUEST_ACCEPTED )
            {
                return HOST_INTERFACE_STATUS_STATE_TRANSITION_FAILURE;
            }
            return HOST_INTERFACE_STATUS_OK;
        case HOST_REQUEST_RESULT_FINALISATION:
            return HOST_INTERFACE_STATUS_UNSUPPORTED_MESSAGE;
        case HOST_REQUEST_RESULTS_READY:
            return HOST_INTERFACE_STATUS_UNSUPPORTED_MESSAGE;
        case HOST_REQUEST_RESULT_TRANSFER:
            if ( RUN_STATE_MANAGER_RequestResultTransfer() != true )
            {
                return HOST_INTERFACE_STATUS_STATE_TRANSITION_FAILURE;
            }
            return HOST_INTERFACE_STATUS_OK;
        case HOST_REQUEST_REPEAT:
            if ( RUN_STATE_MANAGER_RequestRepeat() != true )
            {
                return HOST_INTERFACE_STATUS_STATE_TRANSITION_FAILURE;
            }
            return HOST_INTERFACE_STATUS_OK;
        case HOST_REQUEST_DISCARD_RESULTS:
            if ( RUN_STATE_MANAGER_RequestDiscardResults() != true )
            {
                return HOST_INTERFACE_STATUS_STATE_TRANSITION_FAILURE;
            }
            return HOST_INTERFACE_STATUS_OK;
        case HOST_REQUEST_FAULT:
            if ( RUN_STATE_MANAGER_RequestFault( fault_request ) != true )
            {
                return HOST_INTERFACE_STATUS_STATE_TRANSITION_FAILURE;
            }
            return HOST_INTERFACE_STATUS_OK;
        case HOST_REQUEST_ABORT:
            if ( RUN_STATE_MANAGER_ExecutionAbortRequestedFromISR() != true )
            {
                return HOST_INTERFACE_STATUS_STATE_TRANSITION_FAILURE;
            }
            return HOST_INTERFACE_STATUS_OK;
        case HOST_REQUEST_RESET:
            if ( RUN_STATE_MANAGER_RequestReset() != true )
            {
                return HOST_INTERFACE_STATUS_STATE_TRANSITION_FAILURE;
            }
            return HOST_INTERFACE_STATUS_OK;
        default:
            return HOST_INTERFACE_STATUS_UNSUPPORTED_MESSAGE;
    }
}

/**
 * @brief Takes the state you want to transition to and calls the associated request function with
retrys.
 *
 * @details expected tick count is used by the execution start request but for all others it can be
 *          0
 *          If the request shouldnt be supported by the host interface it will reutrn
 *          HOST_INTERFACE_STATUS_UNSUPPORTED_MESSAGE
 *
 * @param[in] expected_state                  The state we want to transition to
 * @param[in] num_trys                        The number of attempts to transition states (10ms wait
between)
 * @param[in] expected_tick_count             Only used for execution request
 *
 * @return HOST_INTERFACE_STATUS_OK if the message is processed succesfully
 */
HOST_Interface_Status_T HOST_INTERFACE_request_state_tranistion( RunState_T expected_state,
                                                                 Host_RunState_Request_T request,
                                                                 uint16_t                num_trys,
                                                                 uint32_t expected_tick_count )
{
    if ( num_trys == 0U )
    {
        return HOST_INTERFACE_STATUS_STATE_TRANSITION_FAILURE;
    }

    // Request transition once
    HOST_Interface_Status_T state_req_status =
        HOST_INTERFACE_state_to_state_request( request, expected_tick_count );
    if ( state_req_status == HOST_INTERFACE_STATUS_UNSUPPORTED_MESSAGE )
    {
        return HOST_INTERFACE_STATUS_UNSUPPORTED_MESSAGE;
    }
    if ( state_req_status != HOST_INTERFACE_STATUS_OK )
    {
        return state_req_status;
    }
    // TODO change to use a timeout number instead of number of tries
    for ( uint16_t i = 0; i < num_trys; i++ )
    {
        /* Let the RSM consume the queued request before interpreting its prior state. */
        vTaskDelay( pdMS_TO_TICKS( HOST_INTERFACE_STATE_TRANSITION_RETRY_DELAY_MS ) );

        RunStateManagerStatus_T run_state_status = { 0 };
        RUN_STATE_MANAGER_GetStatus( &run_state_status );

        // Transition is complete only when expected state is reached AND no operation is pending
        if ( ( run_state_status.state == expected_state )
             && ( !run_state_status.transition_pending ) )
        {
            return HOST_INTERFACE_STATUS_OK;
        }

        if ( run_state_status.state == RUN_STATE_FAULT )
        {
            return HOST_INTERFACE_STATUS_INTERNAL_ERROR;
        }

        if ( ( !run_state_status.transition_pending )
             && ( run_state_status.last_request_result != RUN_STATE_REQUEST_RESULT_ACCEPTED )
             && ( run_state_status.last_request_result != RUN_STATE_REQUEST_RESULT_NONE ) )
        {
            return HOST_INTERFACE_STATUS_INTERNAL_ERROR;
        }
    }

    return HOST_INTERFACE_STATUS_STATE_TRANSITION_FAILURE;
}

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

/**
 * @brief Process an info request from the host device
 *
 * @details If the rig receives a info request from the host device this is how it will respond.
 *          Most errors will be handled by returning an error message to the host device
 *
 * @param[in] incoming_message         the info request message
 * @param[out] outgoing_message               if required the message to respond with
 * @param[out] response_required              whether or not a response is required
 * @param[out] data                           the optional additional date for variable length byte
spans
 * @param[out] data_size                      the size available to write to at data
 * @return HOST_INTERFACE_STATUS_OK if the message is processed succesfully
 */
HOST_Interface_Status_T
HOST_INTERFACE_process_Info_Request( const HIL_Application_Message_T* incoming_message,
                                     HIL_Application_Message_T*       outgoing_message,
                                     bool* response_required, uint8_t* data, size_t data_size )
{
    switch ( incoming_message->body.system_info_request.query )
    {
        case HIL_APPLICATION_SYSTEM_INFO_QUERY_INVALID:
            // Construct the error message
            HOST_INTERFACE_Default_Error( outgoing_message );
            *response_required = true;
            return HOST_INTERFACE_STATUS_OK;
        case HIL_APPLICATION_SYSTEM_INFO_QUERY_BASIC:
            // Set the type and subtype
            outgoing_message->type    = HIL_APPLICATION_MESSAGE_TYPE_SYSTEM_INFO_RESPONSE;
            outgoing_message->subtype = HIL_APPLICATION_MESSAGE_SUBTYPE_BASIC;
            // Set protocol version
            outgoing_message->body.system_info_response.application_protocol_major =
                HIL_RIG_PROTOCOL_VERSION_MAJOR;
            outgoing_message->body.system_info_response.application_protocol_minor =
                HIL_RIG_PROTOCOL_VERSION_MINOR;
            outgoing_message->body.system_info_response.application_protocol_patch =
                HIL_RIG_PROTOCOL_VERSION_PATCH;
            // Set firmware version TODO
            outgoing_message->body.system_info_response.firmware_version_major = 0U;
            outgoing_message->body.system_info_response.firmware_version_minor = 0U;
            outgoing_message->body.system_info_response.firmware_version_patch = 0U;
            // firmware git hash TODO
            uint8_t firmware_git_hash_size = 1U;
            if ( data_size < firmware_git_hash_size )
            {
                *response_required = false;
                return HOST_INTERFACE_STATUS_BUFFER_TOO_SMALL;
            }
            outgoing_message->body.system_info_response.firmware_git_hash.size =
                firmware_git_hash_size;
            data[0]                                                            = 0U;
            outgoing_message->body.system_info_response.firmware_git_hash.data = data;
            // Diagnostic data depends on the sub-type
            switch ( incoming_message->subtype )
            {
                case HIL_APPLICATION_MESSAGE_SUBTYPE_NONE:
                default:
                    outgoing_message->body.system_info_response.diagnostic_data.size = 0;
                    outgoing_message->body.system_info_response.diagnostic_data.data = NULL;
            }
            *response_required = true;
            return HOST_INTERFACE_STATUS_OK;
        case HIL_APPLICATION_SYSTEM_INFO_QUERY_RESERVED:
            // Construct the error message
            HOST_INTERFACE_Default_Error( outgoing_message );
            *response_required = true;
            return HOST_INTERFACE_STATUS_OK;
        default:
            // Construct the error message
            HOST_INTERFACE_Default_Error( outgoing_message );
            *response_required = true;
            return HOST_INTERFACE_STATUS_OK;
    }
}

/**
 * @brief Process an info response from the host device
 *
 * @details If the rig receives a info response from the host device this is how it will respond.
 *          Most errors will be handled by returning an error message to the host device
 *
 * @param[in] incoming_message         the info request message
 * @param[out] outgoing_message               if required the message to respond with
 * @param[out] response_required              whether or not a response is required
 * @param[out] data                           the optional additional date for variable length byte
spans
 * @param[out] data_size                      the size available to write to at data
 * @return HOST_INTERFACE_STATUS_OK if the message is processed succesfully
 */
HOST_Interface_Status_T
HOST_INTERFACE_process_Info_Response( const HIL_Application_Message_T* incoming_message,
                                      HIL_Application_Message_T*       outgoing_message,
                                      bool* response_required, uint8_t* data, size_t data_size )
{
    ( void )outgoing_message;
    ( void )data;
    ( void )data_size;
    switch ( incoming_message->subtype )
    {
        case HIL_APPLICATION_MESSAGE_SUBTYPE_NONE:
            // Construct the error message
            HOST_INTERFACE_Default_Error( outgoing_message );
            *response_required = true;
            return HOST_INTERFACE_STATUS_OK;
        case HIL_APPLICATION_MESSAGE_SUBTYPE_BASIC:
            // Check protocol version
            if ( incoming_message->body.system_info_response.application_protocol_major
                     != HIL_RIG_PROTOCOL_VERSION_MAJOR
                 || incoming_message->body.system_info_response.application_protocol_minor
                        != HIL_RIG_PROTOCOL_VERSION_MINOR
                 || incoming_message->body.system_info_response.application_protocol_patch
                        != HIL_RIG_PROTOCOL_VERSION_PATCH )
            {
                // Construct the error message
                HOST_INTERFACE_Default_Error( outgoing_message );
                *response_required = true;
                return HOST_INTERFACE_STATUS_OK;
            }
            // Check firmware version TODO
            // Check firmware git hash TODO
            // Check Diagnostic data (depends on the sub-type) TODO
            // switch ( incoming_message->subtype )
            // {
            //     default:
            // }
            *response_required = false;
            return HOST_INTERFACE_STATUS_OK;
        case HIL_APPLICATION_MESSAGE_SUBTYPE_RESERVED:
            // Construct the error message
            HOST_INTERFACE_Default_Error( outgoing_message );
            *response_required = true;
            return HOST_INTERFACE_STATUS_OK;
        default:
            // Construct the error message
            HOST_INTERFACE_Default_Error( outgoing_message );
            *response_required = true;
            return HOST_INTERFACE_STATUS_OK;
    }
}

/**
 * @brief Process an test config message from the host device
 *
 * @details If the rig receives a test config from the host device this will attempt
 *          to transition the run state manager into a new state and pass on the config message.
 *          Most errors will be handled by returning an error message to the host device
 *
 * @param[in] incoming_message         the info request message
 * @param[out] outgoing_message               if required the message to respond with
 * @param[out] response_required              whether or not a response is required
 * @param[out] data                           the optional additional date for variable length byte
spans
 * @param[out] data_size                      the size available to write to at data
 * @return HOST_INTERFACE_STATUS_OK if the message is processed succesfully
 */
HOST_Interface_Status_T HOST_INTERFACE_process_Test_Configuration(
    const HIL_Application_Message_T* incoming_message, HIL_Application_Message_T* outgoing_message,
    bool* response_required, uint8_t* data, size_t data_size, uint32_t* expected_tick_count )
{
    ( void )data;
    ( void )data_size;

    // A retained terminal run must be reset before a different test is uploaded.
    if ( s_session.state != HOST_INTERFACE_SESSION_STATE_IDLE )
    {
        HOST_INTERFACE_Default_Error( outgoing_message );
        outgoing_message->body.error.category = HIL_APPLICATION_ERROR_CATEGORY_PROTOCOL;
        *response_required                    = true;
        return HOST_INTERFACE_STATUS_OK;
    }

    static DutDriverConfiguration_T driver_config = { 0 };
    // Convert the config message to driver struct
    HOST_Interface_Status_T status =
        HOST_INTERFACE_Config_Message_To_Driver( incoming_message, &driver_config );
    if ( status != HOST_INTERFACE_STATUS_OK )
    {
        // Construct the error message
        HOST_INTERFACE_Default_Error( outgoing_message );
        // TODO  more specific error catagory
        outgoing_message->body.error.category = HIL_APPLICATION_ERROR_CATEGORY_PROTOCOL;
        *response_required                    = true;
        return HOST_INTERFACE_STATUS_OK;
    }

    // Signal run state manager to move to package recieving state
    status = HOST_INTERFACE_request_state_tranistion(
        RUN_STATE_TEST_PACKAGE_RECEIVE, HOST_REQUEST_TEST_PACKAGE_RECEIVE,
        HOST_INTERFACE_PACKAGE_RECEIVE_WAIT_ATTEMPTS,
        incoming_message->body.test_configuration.expected_tick_count );
    if ( status == HOST_INTERFACE_STATUS_UNSUPPORTED_MESSAGE )
    {
        // Construct the error message
        HOST_INTERFACE_Default_Error( outgoing_message );
        // TODO  more specific error catagory
        outgoing_message->body.error.category = HIL_APPLICATION_ERROR_CATEGORY_PROTOCOL;
        *response_required                    = true;
        return HOST_INTERFACE_STATUS_OK;
    }
    if ( status == HOST_INTERFACE_STATUS_INTERNAL_ERROR )
    {
        // Construct the error message
        HOST_INTERFACE_Default_Error( outgoing_message );
        outgoing_message->body.error.category = HIL_APPLICATION_ERROR_CATEGORY_INTERNAL;
        *response_required                    = true;
        return HOST_INTERFACE_STATUS_OK;
    }
    if ( status == HOST_INTERFACE_STATUS_STATE_TRANSITION_FAILURE )
    {
        // Construct the error message
        HOST_INTERFACE_Default_Error( outgoing_message );
        // TODO  more specific error catagory
        outgoing_message->body.error.category = HIL_APPLICATION_ERROR_CATEGORY_INTERNAL;
        *response_required                    = true;
        return HOST_INTERFACE_STATUS_OK;
    }

    // Commit the driver struct
    status = HOST_INTERFACE_Commit_Config_Message( &driver_config );
    if ( status != HOST_INTERFACE_STATUS_OK )
    {
        s_session.state = HOST_INTERFACE_SESSION_FAULTED;
        ( void )RUN_STATE_MANAGER_RequestFault( RUN_STATE_FAULT_HOST_INTERFACE_ERROR );
        HOST_INTERFACE_Default_Error( outgoing_message );
        outgoing_message->body.error.category = HIL_APPLICATION_ERROR_CATEGORY_PROTOCOL;
        *response_required                    = true;
        return HOST_INTERFACE_STATUS_OK;
    }

    bool check = false;
    switch ( incoming_message->body.test_configuration.tick_duration_us.microseconds )
    {
        case 10000U:
            check = RUN_STATE_MANAGER_Set_Execution_Frequency( RUN_STATE_FREQUENCY_100HZ );
            break;
        case 1000U:
            check = RUN_STATE_MANAGER_Set_Execution_Frequency( RUN_STATE_FREQUENCY_1KHZ );
            break;
        case 100U:
            check = RUN_STATE_MANAGER_Set_Execution_Frequency( RUN_STATE_FREQUENCY_10KHZ );
            break;
        default:
            s_session.state = HOST_INTERFACE_SESSION_FAULTED;
            ( void )RUN_STATE_MANAGER_RequestFault( RUN_STATE_FAULT_HOST_INTERFACE_ERROR );
            HOST_INTERFACE_Default_Error( outgoing_message );
            outgoing_message->body.error.category = HIL_APPLICATION_ERROR_CATEGORY_PROTOCOL;
            *response_required                    = true;
            return HOST_INTERFACE_STATUS_OK;
    }
    if ( !check )
    {
        s_session.state = HOST_INTERFACE_SESSION_FAULTED;
        ( void )RUN_STATE_MANAGER_RequestFault( RUN_STATE_FAULT_HOST_INTERFACE_ERROR );
        HOST_INTERFACE_Default_Error( outgoing_message );
        outgoing_message->body.error.category = HIL_APPLICATION_ERROR_CATEGORY_PROTOCOL;
        *response_required                    = true;
        return HOST_INTERFACE_STATUS_OK;
    }

    *expected_tick_count = incoming_message->body.test_configuration.expected_tick_count;
    HOST_INSTRUCTION_HANDLER_Reset();
    RESULT_MESSAGE_PRODUCER_Reset();
    HOST_VARIABLE_INSTRUCTION_HANDLER_Reset();
    VARIABLE_RESULT_MESSAGE_PRODUCER_Reset();
    VARIABLE_RESULT_MESSAGE_PRODUCER_SetExpectedTickCount( *expected_tick_count );

    // Set the type and subtype
    outgoing_message->type    = HIL_APPLICATION_MESSAGE_TYPE_RESPONSE;
    outgoing_message->subtype = HIL_APPLICATION_MESSAGE_SUBTYPE_NONE;
    // Set Response body
    outgoing_message->body.response.scope       = HIL_APPLICATION_RESPONSE_SCOPE_TEST_CONFIGURATION;
    outgoing_message->body.response.outcome     = HIL_APPLICATION_RESPONSE_OUTCOME_ACCEPTED;
    outgoing_message->body.response.reason      = HIL_APPLICATION_RESPONSE_REASON_NONE;
    outgoing_message->body.response.tick_number = 0U;
    outgoing_message->body.response.control_command        = HIL_APPLICATION_CONTROL_INVALID;
    outgoing_message->body.response.global_control_command = HIL_APPLICATION_GLOBAL_CONTROL_INVALID;
    outgoing_message->body.response.detail                 = 0U;
    *response_required                                     = true;

    /* Update the session state to track the progression through the tests*/
    s_session.state               = HOST_INTERFACE_SESSION_RECEIVING_INSTRUCTIONS;
    s_session.has_active_test_id  = ( incoming_message->has_test_id != 0U );
    s_session.active_test_id      = incoming_message->test_id;
    s_session.instruction_family  = HOST_INSTRUCTION_FAMILY_UNSET;
    s_session.expected_tick_count = incoming_message->body.test_configuration.expected_tick_count;
    s_session.tick_period_us =
        incoming_message->body.test_configuration.tick_duration_us.microseconds;

    return HOST_INTERFACE_STATUS_OK;
}

/**
 * @brief Process an incoming Test Instruction message from the host device.
 *
 * @todo Implementation guidelines & pseudocode:
 *
 * 1. Validate Test ID correlation:
 *    if (!incoming_message->has_test_id ||
 *        memcmp(incoming_message->test_id.bytes, active_test_id.bytes, 16) != 0)
 *    {
 *        HOST_INTERFACE_Default_Error(outgoing_message);
 *        outgoing_message->body.error.category = HIL_APPLICATION_ERROR_CATEGORY_REJECTED;
 *        *response_required = true;
 *        return HOST_INTERFACE_STATUS_INCONSISTENT_TEST_ID;
 *    }
 *
 * 2. Forward to the Instruction Message Handler:
 *    HOST_Interface_Status_T host_status =
 *        HOST_INSTRUCTION_HANDLER_HandleInstruction(&incoming_message->body.test_instruction);
 *
 * 3. Handle outcome:
 *    - On HOST_INTERFACE_STATUS_OK:
 *        *response_required = false;  // Fixed instruction ticks have no Application response
 *        return HOST_INTERFACE_STATUS_OK;
 *    - On error (validation failure, flash upload error, state error):
 *        HOST_INTERFACE_Default_Error(outgoing_message);
 *        *response_required = true;
 *        return host_status;
 */
HOST_Interface_Status_T
HOST_INTERFACE_process_Test_Instructions( const HIL_Application_Message_T* incoming_message,
                                          HIL_Application_Message_T*       outgoing_message,
                                          bool* response_required, uint8_t* data, size_t data_size )
{
    ( void )data;
    ( void )data_size;
    // 1. Session state check
    if ( s_session.state != HOST_INTERFACE_SESSION_RECEIVING_INSTRUCTIONS )
    {
        HOST_INTERFACE_Default_Error( outgoing_message );
        outgoing_message->body.error.category = HIL_APPLICATION_ERROR_CATEGORY_PROTOCOL;
        *response_required                    = true;
        return HOST_INTERFACE_STATUS_OK;
    }

    // 2. Test ID correlation check
    if ( !HOST_INTERFACE_Validate_Test_Id( incoming_message ) )
    {
        s_session.state = HOST_INTERFACE_SESSION_FAULTED;
        ( void )RUN_STATE_MANAGER_RequestFault( RUN_STATE_FAULT_HOST_INTERFACE_ERROR );
        HOST_INTERFACE_Default_Error( outgoing_message );
        outgoing_message->body.error.category = HIL_APPLICATION_ERROR_CATEGORY_PROTOCOL;
        *response_required                    = true;
        return HOST_INTERFACE_STATUS_OK;
    }

    // 3. Family exclusivity check (cannot send legacy Type 17 if variable Type 21 is active)
    if ( s_session.instruction_family == HOST_INSTRUCTION_FAMILY_VARIABLE_UPDATE )
    {
        s_session.state = HOST_INTERFACE_SESSION_FAULTED;
        ( void )RUN_STATE_MANAGER_RequestFault( RUN_STATE_FAULT_HOST_INTERFACE_ERROR );
        HOST_INTERFACE_Default_Error( outgoing_message );
        outgoing_message->body.error.category = HIL_APPLICATION_ERROR_CATEGORY_PROTOCOL;
        *response_required                    = true;
        return HOST_INTERFACE_STATUS_OK;
    }
    s_session.instruction_family = HOST_INSTRUCTION_FAMILY_LEGACY_FIXED;

    HOST_Interface_Status_T instruction_status =
        HOST_INSTRUCTION_HANDLER_HandleInstruction( &incoming_message->body.test_instruction );

    if ( instruction_status != HOST_INTERFACE_STATUS_OK )
    {
        s_session.state = HOST_INTERFACE_SESSION_FAULTED;
        ( void )RUN_STATE_MANAGER_RequestFault( RUN_STATE_FAULT_HOST_INTERFACE_ERROR );

        outgoing_message->type                  = HIL_APPLICATION_MESSAGE_TYPE_RESPONSE;
        outgoing_message->subtype               = HIL_APPLICATION_MESSAGE_SUBTYPE_NONE;
        outgoing_message->body.response.scope   = HIL_APPLICATION_RESPONSE_SCOPE_TICK;
        outgoing_message->body.response.outcome = HIL_APPLICATION_RESPONSE_OUTCOME_REJECTED;
        if ( instruction_status == HOST_INTERFACE_STATUS_INCONSISTENT_TICK )
        {
            outgoing_message->body.response.reason = HIL_APPLICATION_RESPONSE_REASON_INVALID_TICK;
        }
        else if ( ( instruction_status == HOST_INTERFACE_STATUS_INTERNAL_ERROR )
                  || ( instruction_status == HOST_INTERFACE_STATUS_STATE_TRANSITION_FAILURE ) )
        {
            outgoing_message->body.response.reason =
                HIL_APPLICATION_RESPONSE_REASON_INTERNAL_FAILURE;
        }
        else
        {
            outgoing_message->body.response.reason =
                HIL_APPLICATION_RESPONSE_REASON_VALIDATION_FAILED;
        }
        outgoing_message->body.response.tick_number =
            incoming_message->body.test_instruction.tick_number;
        outgoing_message->body.response.control_command = HIL_APPLICATION_CONTROL_INVALID;
        outgoing_message->body.response.global_control_command =
            HIL_APPLICATION_GLOBAL_CONTROL_INVALID;
        outgoing_message->body.response.detail = ( uint32_t )instruction_status;
        *response_required                     = true;
        return HOST_INTERFACE_STATUS_OK;
    }

    // Success: Ingest directly into flash/RAM storage with zero per-tick Application responses.
    RUN_STATE_MANAGER_RecordInstructionUploadProgress();
    *response_required = false;
    return HOST_INTERFACE_STATUS_OK;
}

HOST_Interface_Status_T HOST_INTERFACE_process_Variable_Instruction_Data(
    const HIL_Application_Message_T* incoming_message, HIL_Application_Message_T* outgoing_message,
    bool* response_required, uint8_t* data, size_t data_size )
{
    ( void )data;
    ( void )data_size;

    if ( incoming_message == NULL || outgoing_message == NULL || response_required == NULL )
    {
        return HOST_INTERFACE_STATUS_INVALID_ARGUMENT;
    }

    // 1. Session state check
    if ( s_session.state != HOST_INTERFACE_SESSION_RECEIVING_INSTRUCTIONS )
    {
        HOST_INTERFACE_Default_Error( outgoing_message );
        outgoing_message->body.error.category = HIL_APPLICATION_ERROR_CATEGORY_PROTOCOL;
        *response_required                    = true;
        return HOST_INTERFACE_STATUS_OK;
    }

    // 2. Test ID correlation check
    if ( !HOST_INTERFACE_Validate_Test_Id( incoming_message ) )
    {
        s_session.state = HOST_INTERFACE_SESSION_FAULTED;
        ( void )RUN_STATE_MANAGER_RequestFault( RUN_STATE_FAULT_HOST_INTERFACE_ERROR );
        HOST_INTERFACE_Default_Error( outgoing_message );
        outgoing_message->body.error.category = HIL_APPLICATION_ERROR_CATEGORY_PROTOCOL;
        *response_required                    = true;
        return HOST_INTERFACE_STATUS_OK;
    }

    // 3. Family exclusivity check (cannot send variable Type 21 if legacy Type 17 is active)
    if ( s_session.instruction_family == HOST_INSTRUCTION_FAMILY_LEGACY_FIXED )
    {
        s_session.state = HOST_INTERFACE_SESSION_FAULTED;
        ( void )RUN_STATE_MANAGER_RequestFault( RUN_STATE_FAULT_HOST_INTERFACE_ERROR );
        HOST_INTERFACE_Default_Error( outgoing_message );
        outgoing_message->body.error.category = HIL_APPLICATION_ERROR_CATEGORY_PROTOCOL;
        *response_required                    = true;
        return HOST_INTERFACE_STATUS_OK;
    }
    s_session.instruction_family = HOST_INSTRUCTION_FAMILY_VARIABLE_UPDATE;

    HOST_Interface_Status_T instruction_status =
        HOST_VARIABLE_INSTRUCTION_HANDLER_HandleInstruction(
            &incoming_message->body.update_instruction );

    if ( instruction_status != HOST_INTERFACE_STATUS_OK )
    {
        s_session.state = HOST_INTERFACE_SESSION_FAULTED;
        ( void )RUN_STATE_MANAGER_RequestFault( RUN_STATE_FAULT_HOST_INTERFACE_ERROR );

        outgoing_message->type                  = HIL_APPLICATION_MESSAGE_TYPE_RESPONSE;
        outgoing_message->subtype               = HIL_APPLICATION_MESSAGE_SUBTYPE_NONE;
        outgoing_message->body.response.scope   = HIL_APPLICATION_RESPONSE_SCOPE_TICK;
        outgoing_message->body.response.outcome = HIL_APPLICATION_RESPONSE_OUTCOME_REJECTED;
        if ( instruction_status == HOST_INTERFACE_STATUS_INCONSISTENT_TICK )
        {
            outgoing_message->body.response.reason = HIL_APPLICATION_RESPONSE_REASON_INVALID_TICK;
        }
        else if ( ( instruction_status == HOST_INTERFACE_STATUS_INTERNAL_ERROR )
                  || ( instruction_status == HOST_INTERFACE_STATUS_STATE_TRANSITION_FAILURE ) )
        {
            outgoing_message->body.response.reason =
                HIL_APPLICATION_RESPONSE_REASON_INTERNAL_FAILURE;
        }
        else
        {
            outgoing_message->body.response.reason =
                HIL_APPLICATION_RESPONSE_REASON_VALIDATION_FAILED;
        }
        outgoing_message->body.response.tick_number =
            incoming_message->body.update_instruction.tick_number;
        outgoing_message->body.response.control_command = HIL_APPLICATION_CONTROL_INVALID;
        outgoing_message->body.response.global_control_command =
            HIL_APPLICATION_GLOBAL_CONTROL_INVALID;
        outgoing_message->body.response.detail = ( uint32_t )instruction_status;
        *response_required                     = true;
        return HOST_INTERFACE_STATUS_OK;
    }

    // Success: Ingest directly into flash/RAM storage with zero per-tick Application responses.
    RUN_STATE_MANAGER_RecordInstructionUploadProgress();
    *response_required = false;
    return HOST_INTERFACE_STATUS_OK;
}

HOST_Interface_Status_T
HOST_INTERFACE_process_Execution_Control( const HIL_Application_Message_T* incoming_message,
                                          HIL_Application_Message_T*       outgoing_message,
                                          bool* response_required, uint8_t* data, size_t data_size )
{

    HOST_Interface_Status_T status = HOST_INTERFACE_STATUS_INTERNAL_ERROR;
    switch ( incoming_message->body.execution_control.command )
    {
        case HIL_APPLICATION_CONTROL_INVALID:
            *response_required = false;
            return HOST_INTERFACE_STATUS_UNSUPPORTED_MESSAGE;
        case HIL_APPLICATION_CONTROL_START:
            if ( s_session.state != HOST_INTERFACE_SESSION_ARMED
                 && s_session.state != HOST_INTERFACE_SESSION_AWAITING_RESET )
            {
                HOST_INTERFACE_BuildExecutionControlResponse(
                    outgoing_message, HIL_APPLICATION_RESPONSE_OUTCOME_REJECTED,
                    HIL_APPLICATION_RESPONSE_REASON_OPERATION_NOT_ALLOWED,
                    HIL_APPLICATION_CONTROL_START );
                *response_required = true;
                return HOST_INTERFACE_STATUS_OK;
            }
            if ( !HOST_INTERFACE_Validate_Test_Id( incoming_message ) )
            {
                HOST_INTERFACE_BuildExecutionControlResponse(
                    outgoing_message, HIL_APPLICATION_RESPONSE_OUTCOME_REJECTED,
                    HIL_APPLICATION_RESPONSE_REASON_INCONSISTENT_TEST_ID,
                    HIL_APPLICATION_CONTROL_START );
                *response_required = true;
                return HOST_INTERFACE_STATUS_OK;
            }

            if ( s_session.state == HOST_INTERFACE_SESSION_AWAITING_RESET )
            {
                RunStateManagerStatus_T rsm_status = { 0 };
                RUN_STATE_MANAGER_GetStatus( &rsm_status );
                if ( rsm_status.transition_pending )
                {
                    HOST_INTERFACE_BuildExecutionControlResponse(
                        outgoing_message, HIL_APPLICATION_RESPONSE_OUTCOME_REJECTED,
                        HIL_APPLICATION_RESPONSE_REASON_HARDWARE_NOT_READY,
                        HIL_APPLICATION_CONTROL_START );
                    *response_required = true;
                    return HOST_INTERFACE_STATUS_OK;
                }

                s_session.result_ticks_emitted = 0U;
                s_session.report_owed          = true;
                s_session.last_failure_source  = HIL_APPLICATION_FAILURE_SOURCE_NONE;
                s_session.last_failure_stage   = HIL_APPLICATION_FAILURE_STAGE_NONE;
                s_session.last_failure_reason  = HIL_APPLICATION_FAILURE_REASON_NONE;
                RESULT_MESSAGE_PRODUCER_Reset();
                VARIABLE_RESULT_MESSAGE_PRODUCER_Reset();
                VARIABLE_RESULT_MESSAGE_PRODUCER_SetExpectedTickCount(
                    s_session.expected_tick_count );

                status = HOST_INTERFACE_request_state_tranistion(
                    RUN_STATE_ARMED, HOST_REQUEST_REPEAT,
                    HOST_INTERFACE_POST_REPORT_RESET_WAIT_ATTEMPTS, 0U );
                if ( status != HOST_INTERFACE_STATUS_OK )
                {
                    RunMetadataSnapshot_T snapshot = { 0 };
                    if ( RUN_STATE_MANAGER_GetRunMetadataSnapshot( &snapshot ) )
                    {
                        s_session.state = HOST_INTERFACE_SESSION_FAULTED;
                        HOST_INTERFACE_BuildExecutionControlResponse(
                            outgoing_message,
                            snapshot.terminal_status == RUN_METADATA_TERMINAL_REJECTED
                                ? HIL_APPLICATION_RESPONSE_OUTCOME_REJECTED
                                : HIL_APPLICATION_RESPONSE_OUTCOME_FAILED,
                            snapshot.terminal_status == RUN_METADATA_TERMINAL_REJECTED
                                ? HIL_APPLICATION_RESPONSE_REASON_HARDWARE_NOT_READY
                                : HIL_APPLICATION_RESPONSE_REASON_INTERNAL_FAILURE,
                            HIL_APPLICATION_CONTROL_START );
                        outgoing_message->body.response.detail = snapshot.failure_reason;
                    }
                    else
                    {
                        s_session.report_owed = false;
                        HOST_INTERFACE_BuildExecutionControlResponse(
                            outgoing_message, HIL_APPLICATION_RESPONSE_OUTCOME_REJECTED,
                            HIL_APPLICATION_RESPONSE_REASON_HARDWARE_NOT_READY,
                            HIL_APPLICATION_CONTROL_START );
                    }
                    *response_required = true;
                    return HOST_INTERFACE_STATUS_OK;
                }
                s_session.state = HOST_INTERFACE_SESSION_ARMED;
            }

            // Signal run state manager to move to execution
            status = HOST_INTERFACE_request_state_tranistion(
                RUN_STATE_EXECUTION, HOST_REQUEST_EXECUTION, 100, s_session.expected_tick_count );
            if ( status == HOST_INTERFACE_STATUS_UNSUPPORTED_MESSAGE )
            {
                s_session.state = HOST_INTERFACE_SESSION_FAULTED;
                ( void )RUN_STATE_MANAGER_RequestFault( RUN_STATE_FAULT_HOST_INTERFACE_ERROR );
                HOST_INTERFACE_BuildExecutionControlResponse(
                    outgoing_message, HIL_APPLICATION_RESPONSE_OUTCOME_REJECTED,
                    HIL_APPLICATION_RESPONSE_REASON_UNSUPPORTED, HIL_APPLICATION_CONTROL_START );
                *response_required = true;
                return HOST_INTERFACE_STATUS_OK;
            }
            if ( status == HOST_INTERFACE_STATUS_INTERNAL_ERROR )
            {
                HOST_INTERFACE_BuildExecutionControlResponse(
                    outgoing_message, HIL_APPLICATION_RESPONSE_OUTCOME_FAILED,
                    HIL_APPLICATION_RESPONSE_REASON_INTERNAL_FAILURE,
                    HIL_APPLICATION_CONTROL_START );
                outgoing_message->body.response.detail =
                    ( uint32_t )RUN_STATE_MANAGER_GetFaultReason();
                *response_required = true;
                return HOST_INTERFACE_STATUS_OK;
            }
            if ( status == HOST_INTERFACE_STATUS_STATE_TRANSITION_FAILURE )
            {
                s_session.state = HOST_INTERFACE_SESSION_FAULTED;
                ( void )RUN_STATE_MANAGER_RequestFault( RUN_STATE_FAULT_HOST_INTERFACE_ERROR );
                HOST_INTERFACE_BuildExecutionControlResponse(
                    outgoing_message, HIL_APPLICATION_RESPONSE_OUTCOME_REJECTED,
                    HIL_APPLICATION_RESPONSE_REASON_OPERATION_NOT_ALLOWED,
                    HIL_APPLICATION_CONTROL_START );
                *response_required = true;
                return HOST_INTERFACE_STATUS_OK;
            }
            if ( status == HOST_INTERFACE_STATUS_OK )
            {
                s_session.state = HOST_INTERFACE_SESSION_EXECUTING;
                HOST_INTERFACE_BuildExecutionControlResponse(
                    outgoing_message, HIL_APPLICATION_RESPONSE_OUTCOME_COMPLETED,
                    HIL_APPLICATION_RESPONSE_REASON_NONE, HIL_APPLICATION_CONTROL_START );
                *response_required = true;
                return HOST_INTERFACE_STATUS_OK;
            }
            HOST_INTERFACE_BuildExecutionControlResponse(
                outgoing_message, HIL_APPLICATION_RESPONSE_OUTCOME_FAILED,
                HIL_APPLICATION_RESPONSE_REASON_INTERNAL_FAILURE, HIL_APPLICATION_CONTROL_START );
            *response_required = true;
            return HOST_INTERFACE_STATUS_OK;
        case HIL_APPLICATION_CONTROL_ABORT:
            s_session.state = HOST_INTERFACE_SESSION_FAULTED;

            // Signal run state manager to abort
            status = HOST_INTERFACE_request_state_tranistion(
                RUN_STATE_FAULT, HOST_REQUEST_FAULT, HOST_INTERFACE_POST_REPORT_RESET_WAIT_ATTEMPTS,
                0 );
            if ( status == HOST_INTERFACE_STATUS_UNSUPPORTED_MESSAGE )
            {
                // Construct the error message
                HOST_INTERFACE_Default_Error( outgoing_message );
                // TODO  more specific error catagory
                outgoing_message->body.error.category = HIL_APPLICATION_ERROR_CATEGORY_PROTOCOL;
                *response_required                    = true;
                return HOST_INTERFACE_STATUS_OK;
            }
            if ( status == HOST_INTERFACE_STATUS_INTERNAL_ERROR )
            {
                // Construct the error message
                HOST_INTERFACE_Default_Error( outgoing_message );
                outgoing_message->body.error.category = HIL_APPLICATION_ERROR_CATEGORY_INTERNAL;
                *response_required                    = true;
                return HOST_INTERFACE_STATUS_OK;
            }
            if ( status == HOST_INTERFACE_STATUS_STATE_TRANSITION_FAILURE )
            {
                // Construct the error message
                HOST_INTERFACE_Default_Error( outgoing_message );
                // TODO  more specific error catagory
                outgoing_message->body.error.category = HIL_APPLICATION_ERROR_CATEGORY_INTERNAL;
                *response_required                    = true;
                return HOST_INTERFACE_STATUS_OK;
            }
            if ( status == HOST_INTERFACE_STATUS_OK )
            {

                *response_required = false;
                return HOST_INTERFACE_STATUS_OK;
            }
            // Construct the error message
            HOST_INTERFACE_Default_Error( outgoing_message );
            // TODO  more specific error catagory
            *response_required = true;
            return HOST_INTERFACE_STATUS_OK;
        case HIL_APPLICATION_CONTROL_RESERVED:
            *response_required = false;
            return HOST_INTERFACE_STATUS_UNSUPPORTED_MESSAGE;
        default:
            *response_required = false;
            return HOST_INTERFACE_STATUS_UNSUPPORTED_MESSAGE;
    }
}

HOST_Interface_Status_T
HOST_INTERFACE_process_Global_Control( const HIL_Application_Message_T* incoming_message,
                                       HIL_Application_Message_T*       outgoing_message,
                                       bool* response_required, uint8_t* data, size_t data_size )
{
    ( void )data;
    ( void )data_size;
    HOST_Interface_Status_T status = HOST_INTERFACE_STATUS_INTERNAL_ERROR;
    switch ( incoming_message->body.global_control.command )
    {
        case HIL_APPLICATION_GLOBAL_CONTROL_INVALID:
            *response_required = false;
            return HOST_INTERFACE_STATUS_UNSUPPORTED_MESSAGE;
        case HIL_APPLICATION_GLOBAL_CONTROL_RESET_APPLICATION: {
            if ( s_session.report_owed || s_session.report_in_flight )
            {
                HOST_INTERFACE_BuildGlobalControlResponse(
                    outgoing_message, HIL_APPLICATION_RESPONSE_OUTCOME_REJECTED,
                    HIL_APPLICATION_RESPONSE_REASON_OPERATION_NOT_ALLOWED,
                    HIL_APPLICATION_GLOBAL_CONTROL_RESET_APPLICATION, 0U );
                *response_required = true;
                return HOST_INTERFACE_STATUS_OK;
            }

            if ( s_session.state != HOST_INTERFACE_SESSION_STATE_IDLE
                 && s_session.state != HOST_INTERFACE_SESSION_RECEIVING_INSTRUCTIONS
                 && s_session.state != HOST_INTERFACE_SESSION_AWAITING_RESET
                 && s_session.state != HOST_INTERFACE_SESSION_FAULTED )
            {
                HOST_INTERFACE_BuildGlobalControlResponse(
                    outgoing_message, HIL_APPLICATION_RESPONSE_OUTCOME_REJECTED,
                    HIL_APPLICATION_RESPONSE_REASON_HARDWARE_NOT_READY,
                    HIL_APPLICATION_GLOBAL_CONTROL_RESET_APPLICATION, 0U );
                *response_required = true;
                return HOST_INTERFACE_STATUS_OK;
            }

            status = HOST_INTERFACE_request_state_tranistion(
                RUN_STATE_IDLE, HOST_REQUEST_RESET, HOST_INTERFACE_POST_REPORT_RESET_WAIT_ATTEMPTS,
                0 );
            if ( status == HOST_INTERFACE_STATUS_OK )
            {
                HOST_INTERFACE_Reset_Session();
                HOST_INTERFACE_BuildGlobalControlResponse(
                    outgoing_message, HIL_APPLICATION_RESPONSE_OUTCOME_COMPLETED,
                    HIL_APPLICATION_RESPONSE_REASON_NONE,
                    HIL_APPLICATION_GLOBAL_CONTROL_RESET_APPLICATION, 0U );
                *response_required = true;
                return HOST_INTERFACE_STATUS_OK;
            }

            RunStateManagerStatus_T rsm_status = { 0 };
            RUN_STATE_MANAGER_GetStatus( &rsm_status );
            if ( rsm_status.last_request_result == RUN_STATE_REQUEST_RESULT_REJECTED_PENDING
                 || rsm_status.last_request_result
                        == RUN_STATE_REQUEST_RESULT_REJECTED_SUBSYSTEM_STATE
                 || rsm_status.last_request_result == RUN_STATE_REQUEST_RESULT_REJECTED_STATE )
            {
                HOST_INTERFACE_BuildGlobalControlResponse(
                    outgoing_message, HIL_APPLICATION_RESPONSE_OUTCOME_REJECTED,
                    HIL_APPLICATION_RESPONSE_REASON_HARDWARE_NOT_READY,
                    HIL_APPLICATION_GLOBAL_CONTROL_RESET_APPLICATION,
                    ( uint32_t )rsm_status.last_request_result );
            }
            else
            {
                HOST_INTERFACE_BuildGlobalControlResponse(
                    outgoing_message, HIL_APPLICATION_RESPONSE_OUTCOME_FAILED,
                    HIL_APPLICATION_RESPONSE_REASON_INTERNAL_FAILURE,
                    HIL_APPLICATION_GLOBAL_CONTROL_RESET_APPLICATION,
                    ( uint32_t )rsm_status.fault_reason );
            }
            *response_required = true;
            return HOST_INTERFACE_STATUS_OK;
        }
        case HIL_APPLICATION_GLOBAL_CONTROL_GET_STATUS:
            HOST_INTERFACE_BuildRigStatus( outgoing_message,
                                           HIL_APPLICATION_STATUS_ORIGIN_QUERY_RESPONSE );
            *response_required = true;
            return HOST_INTERFACE_STATUS_OK;
        case HIL_APPLICATION_GLOBAL_CONTROL_RESERVED:
            *response_required = false;
            return HOST_INTERFACE_STATUS_UNSUPPORTED_MESSAGE;
        default:
            *response_required = false;
            return HOST_INTERFACE_STATUS_UNSUPPORTED_MESSAGE;
    }
}

HOST_Interface_Status_T
HOST_INTERFACE_process_Test_Result( const HIL_Application_Message_T* incoming_message,
                                    HIL_Application_Message_T*       outgoing_message,
                                    bool* response_required, uint8_t* data, size_t data_size )
{
    ( void )incoming_message;
    ( void )data;
    ( void )data_size;

    // Incoming Test Result from host is unexpected
    HOST_INTERFACE_Default_Error( outgoing_message );
    *response_required = true;
    return HOST_INTERFACE_STATUS_OK;
}

HOST_Interface_Status_T HOST_INTERFACE_process_Variable_Result_Data(
    const HIL_Application_Message_T* incoming_message, HIL_Application_Message_T* outgoing_message,
    bool* response_required, uint8_t* data, size_t data_size )
{
    // NOT IMPLEMENTED
    ( void )incoming_message;
    ( void )outgoing_message;
    ( void )data;
    ( void )data_size;
    *response_required = false;
    return HOST_INTERFACE_STATUS_NOT_IMPLEMENTED;
}

HOST_Interface_Status_T
HOST_INTERFACE_process_Response( const HIL_Application_Message_T* incoming_message,
                                 HIL_Application_Message_T*       outgoing_message,
                                 bool* response_required, uint8_t* data, size_t data_size )
{
    ( void )incoming_message;
    ( void )data;
    ( void )data_size;
    // Host device should never be sending a response
    // report error to host device
    HOST_INTERFACE_Default_Error( outgoing_message );
    *response_required = true;
    return HOST_INTERFACE_STATUS_OK;
}

HOST_Interface_Status_T
HOST_INTERFACE_process_Error( const HIL_Application_Message_T* incoming_message,
                              HIL_Application_Message_T* outgoing_message, bool* response_required,
                              uint8_t* data, size_t data_size )
{
    ( void )data;
    ( void )data_size;
    s_session.state             = HOST_INTERFACE_SESSION_FAULTED;
    RunStateFaultReason_T fault = RUN_STATE_FAULT_EXTERNAL_REQUEST;
    switch ( incoming_message->body.error.category )
    {
        case HIL_APPLICATION_ERROR_CATEGORY_INVALID:
            if ( RUN_STATE_MANAGER_RequestFault( fault ) == false )
            {
                // Construct the error message
                HOST_INTERFACE_Default_Error( outgoing_message );
                *response_required = true;
                return HOST_INTERFACE_STATUS_OK;
            }
            *response_required = false;
            return HOST_INTERFACE_STATUS_OK;
        default:
            if ( RUN_STATE_MANAGER_RequestFault( fault ) == false )
            {
                // Construct the error message
                HOST_INTERFACE_Default_Error( outgoing_message );
                *response_required = true;
                return HOST_INTERFACE_STATUS_OK;
            }
            *response_required = false;
            return HOST_INTERFACE_STATUS_OK;
    }
}

HOST_Interface_Status_T HOST_INTERFACE_process_Finalize_Test_Upload(
    const HIL_Application_Message_T* incoming_message, HIL_Application_Message_T* outgoing_message,
    bool* response_required, uint8_t* data, size_t data_size, uint32_t* expected_tick_count )
{
    ( void )data;
    ( void )data_size;

    // 1. Session state check
    if ( s_session.state != HOST_INTERFACE_SESSION_RECEIVING_INSTRUCTIONS )
    {
        HOST_INTERFACE_Default_Error( outgoing_message );
        outgoing_message->body.error.category = HIL_APPLICATION_ERROR_CATEGORY_PROTOCOL;
        *response_required                    = true;
        return HOST_INTERFACE_STATUS_OK;
    }
    // 2. Test ID check
    if ( !HOST_INTERFACE_Validate_Test_Id( incoming_message ) )
    {
        s_session.state = HOST_INTERFACE_SESSION_FAULTED;
        ( void )RUN_STATE_MANAGER_RequestFault( RUN_STATE_FAULT_HOST_INTERFACE_ERROR );
        HOST_INTERFACE_Default_Error( outgoing_message );
        outgoing_message->body.error.category = HIL_APPLICATION_ERROR_CATEGORY_PROTOCOL;
        *response_required                    = true;
        return HOST_INTERFACE_STATUS_OK;
    }

    /* A validated complete upload now owns exactly one terminal report. */
    s_session.report_owed = true;

    // Request transition to CONFIGURATION
    // TODO change 3000 back to 4
    HOST_Interface_Status_T status = HOST_INTERFACE_request_state_tranistion(
        RUN_STATE_ARMED, HOST_REQUEST_CONFIGURATION, 3000, *expected_tick_count );
    if ( status != HOST_INTERFACE_STATUS_OK )
    {
        RunMetadataSnapshot_T snapshot     = { 0 };
        const bool            report_ready = RUN_STATE_MANAGER_GetRunMetadataSnapshot( &snapshot );
        s_session.state                    = HOST_INTERFACE_SESSION_FAULTED;
        if ( report_ready )
        {
            outgoing_message->type                  = HIL_APPLICATION_MESSAGE_TYPE_RESPONSE;
            outgoing_message->subtype               = HIL_APPLICATION_MESSAGE_SUBTYPE_NONE;
            outgoing_message->body.response.scope   = HIL_APPLICATION_RESPONSE_SCOPE_COMPLETE_TEST;
            outgoing_message->body.response.outcome = HIL_APPLICATION_RESPONSE_OUTCOME_FAILED;
            outgoing_message->body.response.reason =
                HIL_APPLICATION_RESPONSE_REASON_HARDWARE_NOT_READY;
            outgoing_message->body.response.tick_number     = 0U;
            outgoing_message->body.response.control_command = HIL_APPLICATION_CONTROL_INVALID;
            outgoing_message->body.response.global_control_command =
                HIL_APPLICATION_GLOBAL_CONTROL_INVALID;
            outgoing_message->body.response.detail = snapshot.failure_reason;
        }
        else
        {
            s_session.report_owed = false;
            ( void )RUN_STATE_MANAGER_RequestFault( RUN_STATE_FAULT_HOST_INTERFACE_ERROR );
            HOST_INTERFACE_Default_Error( outgoing_message );
            outgoing_message->body.error.category =
                ( status == HOST_INTERFACE_STATUS_UNSUPPORTED_MESSAGE )
                    ? HIL_APPLICATION_ERROR_CATEGORY_PROTOCOL
                    : HIL_APPLICATION_ERROR_CATEGORY_INTERNAL;
        }
        *response_required = true;
        return HOST_INTERFACE_STATUS_OK;
    }
    /* Update the session to track progression through the test*/
    s_session.state = HOST_INTERFACE_SESSION_ARMED;

    outgoing_message->type                          = HIL_APPLICATION_MESSAGE_TYPE_RESPONSE;
    outgoing_message->subtype                       = HIL_APPLICATION_MESSAGE_SUBTYPE_NONE;
    outgoing_message->body.response.scope           = HIL_APPLICATION_RESPONSE_SCOPE_COMPLETE_TEST;
    outgoing_message->body.response.outcome         = HIL_APPLICATION_RESPONSE_OUTCOME_ACCEPTED;
    outgoing_message->body.response.reason          = HIL_APPLICATION_RESPONSE_REASON_NONE;
    outgoing_message->body.response.tick_number     = 0U; /* FIXED line 889 bug */
    outgoing_message->body.response.control_command = HIL_APPLICATION_CONTROL_INVALID;
    outgoing_message->body.response.global_control_command = HIL_APPLICATION_GLOBAL_CONTROL_INVALID;
    outgoing_message->body.response.detail                 = 0U;
    *response_required                                     = true;

    return HOST_INTERFACE_STATUS_OK;
}

HOST_Interface_Status_T HOST_INTERFACE_process_Package_Received_Notification(
    HIL_Application_Message_T* outgoing_message, uint32_t* notifications, bool* response_required,
    uint8_t* data, size_t data_size )
{
    return HOST_INTERFACE_STATUS_NOT_IMPLEMENTED;
}

HOST_Interface_Status_T HOST_INTERFACE_process_Config_Started_Notification(
    HIL_Application_Message_T* outgoing_message, uint32_t* notifications, bool* response_required,
    uint8_t* data, size_t data_size )
{
    return HOST_INTERFACE_STATUS_NOT_IMPLEMENTED;
}

HOST_Interface_Status_T
HOST_INTERFACE_process_Armed_Notification( HIL_Application_Message_T* outgoing_message,
                                           uint32_t* notifications, bool* response_required,
                                           uint8_t* data, size_t data_size )
{
    ( void )data;
    ( void )data_size;
    // clear the armed notification flag
    *notifications = *notifications & ( uint32_t ) ~( HOST_INTERFACE_NOTIFY_ARMED );

    // If session is already ARMED, the response was already sent by FINALIZE_TEST_UPLOAD
    if ( s_session.state == HOST_INTERFACE_SESSION_ARMED )
    {
        *response_required = false;
        return HOST_INTERFACE_STATUS_OK;
    }

    s_session.state = HOST_INTERFACE_SESSION_ARMED;

    // Set the type and subtype
    outgoing_message->type    = HIL_APPLICATION_MESSAGE_TYPE_RESPONSE;
    outgoing_message->subtype = HIL_APPLICATION_MESSAGE_SUBTYPE_NONE;
    // Set Response body
    outgoing_message->body.response.scope           = HIL_APPLICATION_RESPONSE_SCOPE_COMPLETE_TEST;
    outgoing_message->body.response.outcome         = HIL_APPLICATION_RESPONSE_OUTCOME_ACCEPTED;
    outgoing_message->body.response.reason          = HIL_APPLICATION_RESPONSE_REASON_NONE;
    outgoing_message->body.response.tick_number     = 0U;
    outgoing_message->body.response.control_command = HIL_APPLICATION_CONTROL_INVALID;
    outgoing_message->body.response.global_control_command = HIL_APPLICATION_GLOBAL_CONTROL_INVALID;
    outgoing_message->body.response.detail                 = 0U;
    if ( s_session.has_active_test_id )
    {
        outgoing_message->has_test_id = 1U;
        outgoing_message->test_id     = s_session.active_test_id;
    }
    *response_required = true;
    return HOST_INTERFACE_STATUS_OK;
}

HOST_Interface_Status_T HOST_INTERFACE_process_Execution_Complete_Notification(
    HIL_Application_Message_T* outgoing_message, uint32_t* notifications, bool* response_required,
    uint8_t* data, size_t data_size )
{
    ( void )outgoing_message;
    ( void )data;
    ( void )data_size;

    *notifications = *notifications & ( uint32_t ) ~( HOST_INTERFACE_NOTIFY_EXECUTION_COMPLETE );

    if ( ( s_session.state == HOST_INTERFACE_SESSION_EXECUTING )
         || ( s_session.state == HOST_INTERFACE_SESSION_ARMED ) )
    {
        if ( RUN_STATE_MANAGER_RequestResultTransfer() != true )
        {
            s_session.state = HOST_INTERFACE_SESSION_FAULTED;
            HOST_INTERFACE_Default_Error( outgoing_message );
            outgoing_message->body.error.category = HIL_APPLICATION_ERROR_CATEGORY_INTERNAL;
            if ( s_session.has_active_test_id )
            {
                outgoing_message->has_test_id = 1U;
                outgoing_message->test_id     = s_session.active_test_id;
            }
            *response_required = true;
            return HOST_INTERFACE_STATUS_OK;
        }
    }

    *response_required = false;
    return HOST_INTERFACE_STATUS_OK;
}

HOST_Interface_Status_T
HOST_INTERFACE_process_Fault_Notification( HIL_Application_Message_T* outgoing_message,
                                           uint32_t* notifications, bool* response_required,
                                           uint8_t* data, size_t data_size )
{
    ( void )data;
    ( void )data_size;

    *notifications = *notifications & ( uint32_t ) ~( HOST_INTERFACE_NOTIFY_FAULT );

    s_session.state = HOST_INTERFACE_SESSION_FAULTED;

    HOST_INTERFACE_Default_Error( outgoing_message );
    outgoing_message->body.error.category = HIL_APPLICATION_ERROR_CATEGORY_HARDWARE;
    outgoing_message->body.error.detail   = ( uint32_t )RUN_STATE_MANAGER_GetFaultReason();
    if ( s_session.has_active_test_id )
    {
        outgoing_message->has_test_id = 1U;
        outgoing_message->test_id     = s_session.active_test_id;
    }
    *response_required = true;
    return HOST_INTERFACE_STATUS_OK;
}

HOST_Interface_Status_T HOST_INTERFACE_process_Transfer_Complete_Notification(
    HIL_Application_Message_T* outgoing_message, uint32_t* notifications, bool* response_required,
    uint8_t* data, size_t data_size )
{
    ( void )data;
    ( void )data_size;

    *notifications &= ( uint32_t ) ~( HOST_INTERFACE_NOTIFY_RESULT_TRANSFER_COMPLETE );

    if ( s_session.state != HOST_INTERFACE_SESSION_COMPLETED )
    {
        s_session.state = HOST_INTERFACE_SESSION_FAULTED;
        HOST_INTERFACE_Default_Error( outgoing_message );
        outgoing_message->body.error.category = HIL_APPLICATION_ERROR_CATEGORY_INTERNAL;
        *response_required                    = true;
        return HOST_INTERFACE_STATUS_OK;
    }

    /* Retain the accepted test until the host explicitly resets or repeats it. */
    s_session.state    = HOST_INTERFACE_SESSION_AWAITING_RESET;
    *response_required = false;
    return HOST_INTERFACE_STATUS_OK;
}

HOST_Interface_Status_T HOST_INTERFACE_process_Result_Transfer_Notification(
    HIL_Application_Message_T* outgoing_message, uint32_t* notifications, bool* response_required,
    uint8_t* data, size_t data_size )
{
    ( void )data;
    Result_Message_Producer_Status_T result_status =
        ( s_session.instruction_family == HOST_INSTRUCTION_FAMILY_VARIABLE_UPDATE )
            ? VARIABLE_RESULT_MESSAGE_PRODUCER_ProduceNextMessage( outgoing_message )
            : RESULT_MESSAGE_PRODUCER_ProduceNextMessage( outgoing_message );
    if ( result_status == RESULT_MESSAGE_PRODUCER_STATUS_END_OF_STREAM )
    {
        // clear the result notification flag
        *notifications = *notifications & ( uint32_t ) ~( HOST_INTERFACE_NOTIFY_RESULT_TRANSFER );
        *notifications |= HOST_INTERFACE_NOTIFY_RUN_REPORT;
        s_session.state    = HOST_INTERFACE_SESSION_COMPLETED;
        *response_required = false;
        return HOST_INTERFACE_STATUS_OK;
    }
    if ( result_status == RESULT_MESSAGE_PRODUCER_STATUS_NO_DATA_AVAILABLE )
    {
        // Flash Manager is busy prefetching the next NAND page into RAM; retry on next cycle
        *response_required = false;
        return HOST_INTERFACE_STATUS_OK;
    }
    if ( result_status == RESULT_MESSAGE_PRODUCER_STATUS_OK )
    {
        s_session.state = HOST_INTERFACE_SESSION_RESULT_TRANSFER;
        if ( s_session.has_active_test_id )
        {
            outgoing_message->has_test_id = 1U;
            outgoing_message->test_id     = s_session.active_test_id;
        }
        if ( outgoing_message->type == HIL_APPLICATION_MESSAGE_TYPE_TEST_RESULT )
        {
            s_session.result_ticks_emitted++;
        }
        else if ( outgoing_message->type == HIL_APPLICATION_MESSAGE_TYPE_VARIABLE_TEST_RESULT
                  && outgoing_message->body.variable_test_result.flags
                         == HIL_APPLICATION_RESULT_FLAG_COMPLETE_TICK )
        {
            s_session.result_ticks_emitted++;
        }
        *response_required = true;
        return HOST_INTERFACE_STATUS_OK;
    }
    // Result producer failed with corrupt data or internal error
    *notifications  = *notifications & ( uint32_t ) ~( HOST_INTERFACE_NOTIFY_RESULT_TRANSFER );
    s_session.state = HOST_INTERFACE_SESSION_FAULTED;
    ( void )RUN_STATE_MANAGER_RequestFault( RUN_STATE_FAULT_HOST_INTERFACE_ERROR );
    HOST_INTERFACE_Default_Error( outgoing_message );
    outgoing_message->body.error.category = HIL_APPLICATION_ERROR_CATEGORY_INTERNAL;
    *response_required                    = true;
    return HOST_INTERFACE_STATUS_OK;
}

/** Emits the terminal report after diagnostics/results and before cleanup. */
static HOST_Interface_Status_T
HOST_INTERFACE_process_Run_Report_Notification( HIL_Application_Message_T* outgoing_message,
                                                uint32_t* notifications, bool* response_required )
{
    if ( !HOST_INTERFACE_BuildRunReport( outgoing_message ) )
    {
        return HOST_INTERFACE_STATUS_INTERNAL_ERROR;
    }

    *notifications &= ( uint32_t )~HOST_INTERFACE_NOTIFY_RUN_REPORT;
    s_session.report_in_flight = true;
    *response_required         = true;
    return HOST_INTERFACE_STATUS_OK;
}

/** Emits one unsolicited snapshot after connection or successful reset. */
static HOST_Interface_Status_T
HOST_INTERFACE_process_Rig_Status_Notification( HIL_Application_Message_T* outgoing_message,
                                                uint32_t* notifications, bool* response_required )
{
    *notifications &= ( uint32_t )~HOST_INTERFACE_NOTIFY_RIG_STATUS;
    HOST_INTERFACE_BuildRigStatus( outgoing_message, HIL_APPLICATION_STATUS_ORIGIN_NOTIFICATION );
    *response_required = true;
    return HOST_INTERFACE_STATUS_OK;
}

HOST_Interface_Status_T HOST_INTERFACE_process_incoming_message(
    bool incoming_message_available, const HIL_Application_Message_T* incoming_message,
    HIL_Application_Message_T* outgoing_message, bool* response_required, uint8_t* data,
    size_t data_size, uint32_t* expected_tick_count )
{
    if ( incoming_message == NULL || outgoing_message == NULL || response_required == NULL
         || data == NULL )
    {
        return HOST_INTERFACE_STATUS_INVALID_ARGUMENT;
    }
    *response_required                  = false;
    HOST_Interface_Status_T host_status = HOST_INTERFACE_STATUS_INTERNAL_ERROR;

    if ( incoming_message_available )
    {
        switch ( incoming_message->type )
        {
            case HIL_APPLICATION_MESSAGE_TYPE_SYSTEM_INFO_REQUEST:
                host_status = HOST_INTERFACE_process_Info_Request(
                    incoming_message, outgoing_message, response_required, data, data_size );
                if ( host_status != HOST_INTERFACE_STATUS_OK )
                {
                    *response_required = false;
                    return host_status;
                }
                break;
            case HIL_APPLICATION_MESSAGE_TYPE_SYSTEM_INFO_RESPONSE:
                host_status = HOST_INTERFACE_process_Info_Response(
                    incoming_message, outgoing_message, response_required, data, data_size );
                if ( host_status != HOST_INTERFACE_STATUS_OK )
                {
                    *response_required = false;
                    return host_status;
                }
                break;
            case HIL_APPLICATION_MESSAGE_TYPE_TEST_CONFIGURATION:
                host_status = HOST_INTERFACE_process_Test_Configuration(
                    incoming_message, outgoing_message, response_required, data, data_size,
                    expected_tick_count );
                if ( host_status != HOST_INTERFACE_STATUS_OK )
                {
                    *response_required = false;
                    return host_status;
                }
                break;
            case HIL_APPLICATION_MESSAGE_TYPE_TEST_INSTRUCTION:
                host_status = HOST_INTERFACE_process_Test_Instructions(
                    incoming_message, outgoing_message, response_required, data, data_size );
                if ( host_status != HOST_INTERFACE_STATUS_OK )
                {
                    *response_required = false;
                    return host_status;
                }
                break;
            case HIL_APPLICATION_MESSAGE_TYPE_UPDATE_INSTRUCTION:
                host_status = HOST_INTERFACE_process_Variable_Instruction_Data(
                    incoming_message, outgoing_message, response_required, data, data_size );
                if ( host_status != HOST_INTERFACE_STATUS_OK )
                {
                    *response_required = false;
                    return host_status;
                }
                break;
            case HIL_APPLICATION_MESSAGE_TYPE_FINALIZE_TEST_UPLOAD:
                host_status = HOST_INTERFACE_process_Finalize_Test_Upload(
                    incoming_message, outgoing_message, response_required, data, data_size,
                    expected_tick_count );
                if ( host_status != HOST_INTERFACE_STATUS_OK )
                {
                    *response_required = false;
                    return host_status;
                }
                break;
            case HIL_APPLICATION_MESSAGE_TYPE_EXECUTION_CONTROL:
                host_status = HOST_INTERFACE_process_Execution_Control(
                    incoming_message, outgoing_message, response_required, data, data_size );
                if ( host_status != HOST_INTERFACE_STATUS_OK )
                {
                    *response_required = false;
                    return host_status;
                }
                break;
            case HIL_APPLICATION_MESSAGE_TYPE_GLOBAL_CONTROL:
                host_status = HOST_INTERFACE_process_Global_Control(
                    incoming_message, outgoing_message, response_required, data, data_size );
                if ( host_status != HOST_INTERFACE_STATUS_OK )
                {
                    *response_required = false;
                    return host_status;
                }
                break;
            case HIL_APPLICATION_MESSAGE_TYPE_TEST_RESULT:
                host_status = HOST_INTERFACE_process_Test_Result(
                    incoming_message, outgoing_message, response_required, data, data_size );
                if ( host_status != HOST_INTERFACE_STATUS_OK )
                {
                    *response_required = false;
                    return host_status;
                }
                break;
            case HIL_APPLICATION_MESSAGE_TYPE_VARIABLE_TEST_RESULT:
                host_status = HOST_INTERFACE_process_Variable_Result_Data(
                    incoming_message, outgoing_message, response_required, data, data_size );
                if ( host_status != HOST_INTERFACE_STATUS_OK )
                {
                    *response_required = false;
                    return host_status;
                }
                break;
            case HIL_APPLICATION_MESSAGE_TYPE_RESPONSE:
                host_status = HOST_INTERFACE_process_Response( incoming_message, outgoing_message,
                                                               response_required, data, data_size );
                if ( host_status != HOST_INTERFACE_STATUS_OK )
                {
                    *response_required = false;
                    return host_status;
                }
                break;
            case HIL_APPLICATION_MESSAGE_TYPE_ERROR:
                host_status = HOST_INTERFACE_process_Error( incoming_message, outgoing_message,
                                                            response_required, data, data_size );
                if ( host_status != HOST_INTERFACE_STATUS_OK )
                {
                    *response_required = false;
                    return host_status;
                }
                break;
            case HIL_APPLICATION_MESSAGE_TYPE_INVALID:
                return HOST_INTERFACE_STATUS_UNSUPPORTED_MESSAGE;
            case HIL_APPLICATION_MESSAGE_TYPE_RESERVED:
                return HOST_INTERFACE_STATUS_UNSUPPORTED_MESSAGE;
            default:
                return HOST_INTERFACE_STATUS_UNSUPPORTED_MESSAGE;
        }
    }
    return HOST_INTERFACE_STATUS_OK;
}

HOST_Interface_Status_T
HOST_INTERFACE_process_internal_message( HIL_Application_Message_T* outgoing_message,
                                         bool* response_required, uint8_t* data, size_t data_size,
                                         uint32_t* notifications )
{
    if ( *notifications == 0U )
    {
        *response_required = false;
        return HOST_INTERFACE_STATUS_OK;
    }

    HOST_Interface_Status_T host_status = HOST_INTERFACE_STATUS_UNINITIALIZED;

    if ( ( *notifications & HOST_INTERFACE_NOTIFY_PACKAGE_RECEIVE ) != 0U )
    {
        host_status = HOST_INTERFACE_process_Package_Received_Notification(
            outgoing_message, notifications, response_required, data, data_size );
        if ( host_status != HOST_INTERFACE_STATUS_OK )
        {
            *response_required = false;
            return host_status;
        }
        return HOST_INTERFACE_STATUS_OK;
    }

    if ( ( *notifications & HOST_INTERFACE_NOTIFY_CONFIGURATION ) != 0U )
    {
        host_status = HOST_INTERFACE_process_Config_Started_Notification(
            outgoing_message, notifications, response_required, data, data_size );
        if ( host_status != HOST_INTERFACE_STATUS_OK )
        {
            *response_required = false;
            return host_status;
        }
        return HOST_INTERFACE_STATUS_OK;
    }

    if ( ( *notifications & HOST_INTERFACE_NOTIFY_ARMED ) != 0U )
    {
        host_status = HOST_INTERFACE_process_Armed_Notification(
            outgoing_message, notifications, response_required, data, data_size );
        if ( host_status != HOST_INTERFACE_STATUS_OK )
        {
            *response_required = false;
            return host_status;
        }
        return HOST_INTERFACE_STATUS_OK;
    }

    if ( ( *notifications & HOST_INTERFACE_NOTIFY_EXECUTION_COMPLETE ) != 0U )
    {
        host_status = HOST_INTERFACE_process_Execution_Complete_Notification(
            outgoing_message, notifications, response_required, data, data_size );
        if ( host_status != HOST_INTERFACE_STATUS_OK )
        {
            *response_required = false;
            return host_status;
        }
        return HOST_INTERFACE_STATUS_OK;
    }

    // NOTIFY_FAULT is checked before NOTIFY_RESULT_TRANSFER so that a hardware
    // fault preempts result streaming. If FAULT is deferred behind RESULT_TRANSFER,
    // the result handler runs and returns every iteration while FAULT sits unprocessed
    // in carry_on_notifications — the fault error message is never sent to the host
    // and RESULT_TRANSFER keeps producing messages into a stalled outgoing slot.
    if ( ( *notifications & HOST_INTERFACE_NOTIFY_FAULT ) != 0U )
    {
        host_status = HOST_INTERFACE_process_Fault_Notification(
            outgoing_message, notifications, response_required, data, data_size );
        if ( host_status != HOST_INTERFACE_STATUS_OK )
        {
            *response_required = false;
            return host_status;
        }
        return HOST_INTERFACE_STATUS_OK;
    }

    if ( ( *notifications & HOST_INTERFACE_NOTIFY_RESULT_TRANSFER ) != 0U )
    {
        host_status = HOST_INTERFACE_process_Result_Transfer_Notification(
            outgoing_message, notifications, response_required, data, data_size );
        if ( host_status != HOST_INTERFACE_STATUS_OK )
        {
            *response_required = false;
            return host_status;
        }
        return HOST_INTERFACE_STATUS_OK;
    }

    if ( ( *notifications & HOST_INTERFACE_NOTIFY_RUN_REPORT ) != 0U
         && ( s_session.state == HOST_INTERFACE_SESSION_COMPLETED
              || s_session.state == HOST_INTERFACE_SESSION_FAULTED ) )
    {
        host_status = HOST_INTERFACE_process_Run_Report_Notification(
            outgoing_message, notifications, response_required );
        if ( host_status != HOST_INTERFACE_STATUS_OK )
        {
            *response_required = false;
            return host_status;
        }
        return HOST_INTERFACE_STATUS_OK;
    }

    if ( ( *notifications & HOST_INTERFACE_NOTIFY_RUN_REPORT ) != 0U )
    {
        /* The sealed report waits until fault diagnostics or all results are emitted. */
        *response_required = false;
        return HOST_INTERFACE_STATUS_OK;
    }

    if ( ( *notifications & HOST_INTERFACE_NOTIFY_RESULT_TRANSFER_COMPLETE ) != 0U )
    {
        host_status = HOST_INTERFACE_process_Transfer_Complete_Notification(
            outgoing_message, notifications, response_required, data, data_size );
        if ( host_status != HOST_INTERFACE_STATUS_OK )
        {
            *response_required = false;
            return host_status;
        }
        return HOST_INTERFACE_STATUS_OK;
    }

    if ( ( *notifications & HOST_INTERFACE_NOTIFY_RIG_STATUS ) != 0U )
    {
        return HOST_INTERFACE_process_Rig_Status_Notification( outgoing_message, notifications,
                                                               response_required );
    }

    *notifications     = 0U;
    *response_required = false;
    return HOST_INTERFACE_STATUS_UNSUPPORTED_NOTIFICATION;
}

/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */

HOST_Interface_Status_T HOST_INTERFACE_process_message(
    bool incoming_message_available, const HIL_Application_Message_T* incoming_message,
    bool outgoing_message_accepted, HIL_Application_Message_T* outgoing_message,
    HIL_Application_Message_T* overflow_outgoing_message, bool* response_required, uint8_t* data,
    size_t data_size, uint32_t* notifications, uint32_t* expected_tick_count )
{
    if ( incoming_message == NULL || outgoing_message == NULL || response_required == NULL
         || data == NULL )
    {
        return HOST_INTERFACE_STATUS_INVALID_ARGUMENT;
    }
    *response_required = false;

    if ( s_session.report_in_flight && outgoing_message_accepted )
    {
        s_session.report_in_flight = false;
        s_session.report_owed      = false;
        RUN_STATE_MANAGER_AcknowledgeRunReport();
        if ( s_session.state == HOST_INTERFACE_SESSION_COMPLETED
             && !RUN_STATE_MANAGER_RequestResultTransferComplete() )
        {
            s_session.state = HOST_INTERFACE_SESSION_FAULTED;
            return HOST_INTERFACE_STATUS_STATE_TRANSITION_FAILURE;
        }
        if ( s_session.state == HOST_INTERFACE_SESSION_FAULTED )
        {
            s_session.state = HOST_INTERFACE_SESSION_AWAITING_RESET;
        }
    }

    if ( outgoing_message_accepted
         && outgoing_message->type == HIL_APPLICATION_MESSAGE_TYPE_RESPONSE
         && outgoing_message->body.response.scope == HIL_APPLICATION_RESPONSE_SCOPE_GLOBAL_CONTROL
         && outgoing_message->body.response.outcome == HIL_APPLICATION_RESPONSE_OUTCOME_COMPLETED
         && outgoing_message->body.response.global_control_command
                == HIL_APPLICATION_GLOBAL_CONTROL_RESET_APPLICATION )
    {
        *notifications |= HOST_INTERFACE_NOTIFY_RIG_STATUS;
    }

    // create temporary output message (incase output is not accepted)
    static HIL_Application_Message_T temp_outgoing_message = { 0 };
    ( void )memset( &temp_outgoing_message, 0, sizeof( temp_outgoing_message ) );
    HOST_Interface_Status_T host_status = HOST_INTERFACE_STATUS_INTERNAL_ERROR;

    // PROCESS INCOMING MESSAGE
    host_status = HOST_INTERFACE_process_incoming_message(
        incoming_message_available, incoming_message, &temp_outgoing_message, response_required,
        data, data_size, expected_tick_count );
    if ( host_status != HOST_INTERFACE_STATUS_OK )
    {
        *response_required = false;
        return host_status;
    }

    if ( *response_required && temp_outgoing_message.type == HIL_APPLICATION_MESSAGE_TYPE_RESPONSE
         && temp_outgoing_message.body.response.scope
                == HIL_APPLICATION_RESPONSE_SCOPE_GLOBAL_CONTROL
         && temp_outgoing_message.body.response.outcome
                == HIL_APPLICATION_RESPONSE_OUTCOME_COMPLETED
         && temp_outgoing_message.body.response.global_control_command
                == HIL_APPLICATION_GLOBAL_CONTROL_RESET_APPLICATION )
    {
        /* No notification from the discarded transaction may follow reset completion. */
        *notifications = 0U;
    }

    // CHECK IF A RESPONSE IS REQUIRED
    if ( *response_required )
    {
        if ( !outgoing_message_accepted )
        {
            *overflow_outgoing_message             = temp_outgoing_message;
            overflow_outgoing_message->has_test_id = incoming_message->has_test_id;
            overflow_outgoing_message->test_id     = incoming_message->test_id;
            *response_required                     = true;
            return HOST_INTERFACE_STATUS_OUTGOING_REQUIRED;
        }
        *outgoing_message             = temp_outgoing_message;
        outgoing_message->has_test_id = incoming_message->has_test_id;
        outgoing_message->test_id     = incoming_message->test_id;
        return HOST_INTERFACE_STATUS_OK;
    }
    // If no response is required and outgoing slot is available, process internal message requests
    if ( outgoing_message_accepted )
    {
        host_status = HOST_INTERFACE_process_internal_message(
            &temp_outgoing_message, response_required, data, data_size, notifications );
        if ( host_status != HOST_INTERFACE_STATUS_OK )
        {
            *response_required = false;
            return host_status;
        }
        if ( *response_required )
        {
            *outgoing_message = temp_outgoing_message;
            return HOST_INTERFACE_STATUS_OK;
        }
    }
    return HOST_INTERFACE_STATUS_OK;
}
