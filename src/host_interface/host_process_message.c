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
#include "hil_rig_protocol/transport/transport.h"
#include "hil_rig_protocol/version.h"
#include "host_interface.h"
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
    .state               = HOST_INTERFACE_SESSION_STATE_IDLE,
    .has_active_test_id  = false,
    .active_test_id      = { { 0 } },
    .instruction_family  = HOST_INSTRUCTION_FAMILY_UNSET,
    .expected_tick_count = 0U,
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
    s_session.instruction_family  = HOST_INSTRUCTION_FAMILY_UNSET;
    s_session.expected_tick_count = 0U;
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
    RunStateFaultReason_T fault_request          = RUN_STATE_FAULT_EXTERNAL_REQUEST;
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

        vTaskDelay( pdMS_TO_TICKS( HOST_INTERFACE_STATE_TRANSITION_RETRY_DELAY_MS ) );
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

    // Only accept configuration when IDLE or COMPLETED
    if ( ( s_session.state != HOST_INTERFACE_SESSION_STATE_IDLE )
         && ( s_session.state != HOST_INTERFACE_SESSION_COMPLETED ) )
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
        // Construct the error message
        HOST_INTERFACE_Default_Error( outgoing_message );
        // TODO  more specific error catagory
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
            // Construct the error message
            HOST_INTERFACE_Default_Error( outgoing_message );
            // TODO  more specific error catagory
            outgoing_message->body.error.category = HIL_APPLICATION_ERROR_CATEGORY_PROTOCOL;
            *response_required                    = true;
            return HOST_INTERFACE_STATUS_OK;
    }
    if ( !check )
    {
        // Construct the error message
        HOST_INTERFACE_Default_Error( outgoing_message );
        // TODO  more specific error catagory
        outgoing_message->body.error.category = HIL_APPLICATION_ERROR_CATEGORY_PROTOCOL;
        *response_required                    = true;
        return HOST_INTERFACE_STATUS_OK;
    }

    *expected_tick_count = incoming_message->body.test_configuration.expected_tick_count;
    HOST_INSTRUCTION_HANDLER_Reset();
    RESULT_MESSAGE_PRODUCER_Reset();
    HOST_VARIABLE_INSTRUCTION_HANDLER_Reset();
    VARIABLE_RESULT_MESSAGE_PRODUCER_Reset();

    // Set the type and subtype
    outgoing_message->type    = HIL_APPLICATION_MESSAGE_TYPE_RESPONSE;
    outgoing_message->subtype = HIL_APPLICATION_MESSAGE_SUBTYPE_NONE;
    // Set Response body
    outgoing_message->body.response.scope   = HIL_APPLICATION_RESPONSE_SCOPE_TEST_CONFIGURATION;
    outgoing_message->body.response.outcome = HIL_APPLICATION_RESPONSE_OUTCOME_ACCEPTED;
    outgoing_message->body.response.reason  = HIL_APPLICATION_RESPONSE_REASON_NONE;
    outgoing_message->body.response.tick_number =
        incoming_message->body.test_instruction.tick_number;
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
        HOST_INTERFACE_Default_Error( outgoing_message );
        outgoing_message->body.error.category = HIL_APPLICATION_ERROR_CATEGORY_PROTOCOL;
        *response_required                    = true;
        return HOST_INTERFACE_STATUS_OK;
    }

    // 3. Family exclusivity check (cannot send legacy Type 17 if variable Type 21 is active)
    if ( s_session.instruction_family == HOST_INSTRUCTION_FAMILY_VARIABLE_UPDATE )
    {
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
        HOST_INTERFACE_Default_Error( outgoing_message );
        outgoing_message->body.error.category = HIL_APPLICATION_ERROR_CATEGORY_PROTOCOL;
        *response_required                    = true;
        return HOST_INTERFACE_STATUS_OK;
    }

    // 3. Family exclusivity check (cannot send variable Type 21 if legacy Type 17 is active)
    if ( s_session.instruction_family == HOST_INSTRUCTION_FAMILY_LEGACY_FIXED )
    {
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

            // Must be ARMED to start
            if ( s_session.state != HOST_INTERFACE_SESSION_ARMED )
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

            // Signal run state manager to move to execution
            status = HOST_INTERFACE_request_state_tranistion(
                RUN_STATE_EXECUTION, HOST_REQUEST_EXECUTION, 100, s_session.expected_tick_count );
            if ( status == HOST_INTERFACE_STATUS_UNSUPPORTED_MESSAGE )
            {
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
            status = HOST_INTERFACE_request_state_tranistion( RUN_STATE_FAULT, HOST_REQUEST_FAULT,
                                                              2, 0 );
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
    HOST_Interface_Status_T status = HOST_INTERFACE_STATUS_INTERNAL_ERROR;
    switch ( incoming_message->body.global_control.command )
    {
        case HIL_APPLICATION_GLOBAL_CONTROL_INVALID:
            *response_required = false;
            return HOST_INTERFACE_STATUS_UNSUPPORTED_MESSAGE;
        case HIL_APPLICATION_GLOBAL_CONTROL_RESET_APPLICATION:
            // Signal run state manager to abort
            // TODO set up reset instead of just fault
            status =
                HOST_INTERFACE_request_state_tranistion( RUN_STATE_IDLE, HOST_REQUEST_RESET, 2, 0 );
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
                HOST_INTERFACE_Reset_Session();
                *response_required = false;
                return HOST_INTERFACE_STATUS_OK;
            }
            // Construct the error message
            HOST_INTERFACE_Default_Error( outgoing_message );
            // TODO  more specific error catagory
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
        HOST_INTERFACE_Default_Error( outgoing_message );
        outgoing_message->body.error.category = HIL_APPLICATION_ERROR_CATEGORY_PROTOCOL;
        *response_required                    = true;
        return HOST_INTERFACE_STATUS_OK;
    }

    // Request transition to CONFIGURATION
    // TODO change 3000 back to 4
    HOST_Interface_Status_T status = HOST_INTERFACE_request_state_tranistion(
        RUN_STATE_ARMED, HOST_REQUEST_CONFIGURATION, 3000, *expected_tick_count );
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
    return HOST_INTERFACE_STATUS_NOT_IMPLEMENTED;
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
        if ( RUN_STATE_MANAGER_RequestResultTransferComplete() == false )
        {
            s_session.state = HOST_INTERFACE_SESSION_FAULTED;
            HOST_INTERFACE_Default_Error( outgoing_message );
            outgoing_message->body.error.category = HIL_APPLICATION_ERROR_CATEGORY_INTERNAL;
            *response_required                    = true;
            return HOST_INTERFACE_STATUS_OK;
        }
        s_session.state    = HOST_INTERFACE_SESSION_COMPLETED;
        *response_required = false;
        return HOST_INTERFACE_STATUS_OK;
    }
    if ( result_status == RESULT_MESSAGE_PRODUCER_STATUS_NO_DATA_AVAILABLE )
    {
        *response_required = false;
        return HOST_INTERFACE_STATUS_INTERNAL_ERROR;
    }
    if ( result_status == RESULT_MESSAGE_PRODUCER_STATUS_OK )
    {
        s_session.state = HOST_INTERFACE_SESSION_RESULT_TRANSFER;
        if ( s_session.has_active_test_id )
        {
            outgoing_message->has_test_id = 1U;
            outgoing_message->test_id     = s_session.active_test_id;
        }
        *response_required = true;
        return HOST_INTERFACE_STATUS_OK;
    }
    *response_required = false;
    return HOST_INTERFACE_STATUS_INTERNAL_ERROR;
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
    // If no response is required then we can process internal message requests
    // PROCESS INTERNAL REQUESTS
    host_status = HOST_INTERFACE_process_internal_message(
        &temp_outgoing_message, response_required, data, data_size, notifications );
    if ( host_status != HOST_INTERFACE_STATUS_OK )
    {
        *response_required = false;
        return host_status;
    }
    // Check if a response is required
    if ( *response_required )
    {
        if ( !outgoing_message_accepted )
        {
            *overflow_outgoing_message = temp_outgoing_message;
            *response_required         = true;
            return HOST_INTERFACE_STATUS_OUTGOING_REQUIRED;
        }
        *outgoing_message = temp_outgoing_message;
        return HOST_INTERFACE_STATUS_OK;
    }
    return HOST_INTERFACE_STATUS_OK;
}
