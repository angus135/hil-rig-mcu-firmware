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

#include "host_process_message.h"
#include "hil_rig_protocol/application/application.h"
#include "hil_rig_protocol/application/application_message.h"
#include "hil_rig_protocol/transport/transport.h"
#include "hil_rig_protocol/version.h"
#include "host_interface.h"
#include "run_state_manager.h"

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */

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


/**-----------------------------------------------------------------------------
 *  Private (static) Function Prototypes
 *------------------------------------------------------------------------------
 */

/**
 * @brief Construct a generic error message
 *
 * @details is given a pointer to a message and fills it with default error values
 *
 * @param[in] recent_received_message         the info request message
 * @param[out] response_message               if required the message to respond with
 * @param[out] response_required              whether or not a response is required
 * @param[out] data                           the optional additional date for variable length byte
spans
 * @param[out] data_size                      the size available to write to at data
 * @return HOST_INTERFACE_STATUS_OK if the message is processed succesfully  
 */
HOST_Interface_Status_T HOST_INTERFACE_Default_Error(HIL_Application_Message_T* message)
{
    // Set the type and subtype
    message->type = HIL_APPLICATION_MESSAGE_TYPE_ERROR;
    message->subtype = HIL_APPLICATION_MESSAGE_SUBTYPE_NONE;
    // Set Error body TODO update error catagory
    message->body.error.category = HIL_APPLICATION_ERROR_CATEGORY_INVALID;
    message->body.error.recoverable = 1U;
    message->body.error.has_tick_number = 0U;
    message->body.error.tick_number     = 0U;
    message->body.error.detail     = 0U;
    message->body.error.diagnostic_data.size = 0U;
    message->body.error.diagnostic_data.data     = NULL;
    return HOST_INTERFACE_STATUS_OK;
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
 * @param[in] recent_received_message         the info request message
 * @param[out] response_message               if required the message to respond with
 * @param[out] response_required              whether or not a response is required
 * @param[out] data                           the optional additional date for variable length byte
spans
 * @param[out] data_size                      the size available to write to at data
 * @return HOST_INTERFACE_STATUS_OK if the message is processed succesfully
 */
HOST_Interface_Status_T HOST_INTERFACE_process_Info_Request(
    const HIL_Application_Message_T* recent_received_message,
    HIL_Application_Message_T* response_message, bool* response_required, uint8_t* data, size_t data_size )
{
    switch ( recent_received_message->body.system_info_request.query )
    {
        case HIL_APPLICATION_SYSTEM_INFO_QUERY_INVALID:
            // Construct the error message
            HOST_INTERFACE_Default_Error(response_message);
            *response_required = true;
            return HOST_INTERFACE_STATUS_OK;
        case HIL_APPLICATION_SYSTEM_INFO_QUERY_BASIC:
            // Set the type and subtype
            response_message->type = HIL_APPLICATION_MESSAGE_TYPE_SYSTEM_INFO_RESPONSE;
            response_message->subtype = HIL_APPLICATION_MESSAGE_SUBTYPE_BASIC;
            // Set protocol version
            response_message->body.system_info_response.application_protocol_major =
                HIL_RIG_PROTOCOL_VERSION_MAJOR;
            response_message->body.system_info_response.application_protocol_minor =
                HIL_RIG_PROTOCOL_VERSION_MINOR;
            response_message->body.system_info_response.application_protocol_patch =
                HIL_RIG_PROTOCOL_VERSION_PATCH;
            // Set firmware version TODO
            response_message->body.system_info_response.firmware_version_major =
                0U;
            response_message->body.system_info_response.firmware_version_minor =
                0U;
            response_message->body.system_info_response.firmware_version_patch = 0U;
            // firmware git hash TODO
            uint8_t firmware_git_hash_size = 1U;
            if ( data_size < firmware_git_hash_size )
            {
                *response_required = false;
                return HOST_INTERFACE_STATUS_BUFFER_TOO_SMALL;
            }
            response_message->body.system_info_response.firmware_git_hash.size =
                firmware_git_hash_size;
            data[0] = 0U;
            response_message->body.system_info_response.firmware_git_hash.data = data;
            // Diagnostic data depends on the sub-type
            switch ( recent_received_message->subtype )
            {
                default:
                    response_message->body.system_info_response.diagnostic_data.size = 0;
                    response_message->body.system_info_response.diagnostic_data.data = NULL;
            }
            *response_required = true;
            return HOST_INTERFACE_STATUS_OK;
        case HIL_APPLICATION_SYSTEM_INFO_QUERY_RESERVED:
            // Construct the error message
            HOST_INTERFACE_Default_Error(response_message);
            *response_required = true;
            return HOST_INTERFACE_STATUS_OK;
        default:
            // Construct the error message
            HOST_INTERFACE_Default_Error(response_message);
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
 * @param[in] recent_received_message         the info request message
 * @param[out] response_message               if required the message to respond with
 * @param[out] response_required              whether or not a response is required
 * @param[out] data                           the optional additional date for variable length byte
spans
 * @param[out] data_size                      the size available to write to at data
 * @return HOST_INTERFACE_STATUS_OK if the message is processed succesfully
 */
HOST_Interface_Status_T HOST_INTERFACE_process_Info_Response( const HIL_Application_Message_T* recent_received_message,
                                           HIL_Application_Message_T*       response_message,
                                           bool* response_required, uint8_t* data,
                                           size_t data_size )
{
    ( void )response_message;
    ( void )data;
    (void) data_size;
    switch ( recent_received_message->subtype )
    {
        case HIL_APPLICATION_MESSAGE_SUBTYPE_NONE:
            // Construct the error message
            HOST_INTERFACE_Default_Error(response_message);
            *response_required = true;
            return HOST_INTERFACE_STATUS_OK;
        case HIL_APPLICATION_MESSAGE_SUBTYPE_BASIC:
            // Check protocol version
            if ( recent_received_message->body.system_info_response.application_protocol_major
                 == HIL_RIG_PROTOCOL_VERSION_MAJOR )
            {
                *response_required = false;
                return HOST_INTERFACE_STATUS_OK;
            }
            if ( recent_received_message->body.system_info_response.application_protocol_minor
                 == HIL_RIG_PROTOCOL_VERSION_MINOR )
            {
                *response_required = false;
                return HOST_INTERFACE_STATUS_OK;
            }
            if ( recent_received_message->body.system_info_response.application_protocol_patch
                 == HIL_RIG_PROTOCOL_VERSION_PATCH )
            {
                *response_required = false;
                return HOST_INTERFACE_STATUS_OK;
            }
            // Check firmware version TODO
            // Check firmware git hash TODO
            // Check Diagnostic data (depends on the sub-type) TODO
            // switch ( recent_received_message->subtype )
            // {
            //     default:
            // }
            // Construct the error message
            HOST_INTERFACE_Default_Error(response_message);
            *response_required = true;
            return HOST_INTERFACE_STATUS_OK;
        case HIL_APPLICATION_MESSAGE_SUBTYPE_RESERVED:
            // Construct the error message
            HOST_INTERFACE_Default_Error(response_message);
            *response_required = true;
            return HOST_INTERFACE_STATUS_OK;
        default:
            // Construct the error message
            HOST_INTERFACE_Default_Error(response_message);
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
 * @param[in] recent_received_message         the info request message
 * @param[out] response_message               if required the message to respond with
 * @param[out] response_required              whether or not a response is required
 * @param[out] data                           the optional additional date for variable length byte
spans
 * @param[out] data_size                      the size available to write to at data
 * @return HOST_INTERFACE_STATUS_OK if the message is processed succesfully
 */
HOST_Interface_Status_T HOST_INTERFACE_process_Test_Configuration(
    const HIL_Application_Message_T* recent_received_message,
    HIL_Application_Message_T* response_message, bool* response_required, uint8_t* data,
    size_t data_size )
{
    ( void )data;
    ( void )data_size;
    return HOST_INTERFACE_STATUS_NOT_IMPLEMENTED;
    /** CALL CALLUMS FUNCTION TO PASS CONFIGURAITON MESSAGE
     *
     *
     *
     *
     */

    // Signal run state manager to move to package recieving state
    RunStateManagerStatus_T run_state = {0};
    RUN_STATE_MANAGER_GetStatus( &run_state );
    size_t counter = 0;
    size_t counter_limit = 100;
    if ( RUN_STATE_MANAGER_RequestPackageReceive() == false )
    {
        // report error to host device
        HOST_INTERFACE_Default_Error( response_message );
        *response_required = true;
        return HOST_INTERFACE_STATUS_OK;
    }
    while ( run_state.state != RUN_STATE_TEST_PACKAGE_RECEIVE )
    {
        // wait
        RUN_STATE_MANAGER_GetStatus( &run_state );
        counter += 1;
        if ( counter >= counter_limit )
        {
            HOST_INTERFACE_Default_Error( response_message );
            response_message->body.error.category = HIL_APPLICATION_ERROR_CATEGORY_TIMEOUT;
            *response_required = true;
            return HOST_INTERFACE_STATUS_OK;
        }
    }
    *response_required = false;
    return HOST_INTERFACE_STATUS_OK;

}

HOST_Interface_Status_T HOST_INTERFACE_process_Test_Instructions(const HIL_Application_Message_T* recent_received_message, HIL_Application_Message_T* response_message, bool* response_required, uint8_t* data, size_t data_size);

HOST_Interface_Status_T HOST_INTERFACE_process_Variable_Instruction_Data(
    const HIL_Application_Message_T* recent_received_message,
    HIL_Application_Message_T* response_message, bool* response_required, uint8_t* data, size_t data_size )
{
    // NOT IMPLEMENTED
    ( void )recent_received_message;
    ( void )response_message;
    ( void )data;
    (void) data_size;
    *response_required = false;
    return HOST_INTERFACE_STATUS_NOT_IMPLEMENTED;
}

HOST_Interface_Status_T HOST_INTERFACE_process_Execution_Control(const HIL_Application_Message_T* recent_received_message, HIL_Application_Message_T* response_message, bool* response_required, uint8_t* data, size_t data_size);

HOST_Interface_Status_T HOST_INTERFACE_process_Global_Control(const HIL_Application_Message_T* recent_received_message, HIL_Application_Message_T* response_message, bool* response_required, uint8_t* data, size_t data_size);

HOST_Interface_Status_T HOST_INTERFACE_process_Test_Result(const HIL_Application_Message_T* recent_received_message, HIL_Application_Message_T* response_message, bool* response_required, uint8_t* data, size_t data_size);

HOST_Interface_Status_T HOST_INTERFACE_process_Variable_Result_Data(
    const HIL_Application_Message_T* recent_received_message,
    HIL_Application_Message_T* response_message, bool* response_required, uint8_t* data, size_t data_size )
{
    // NOT IMPLEMENTED
    ( void )recent_received_message;
    ( void )response_message;
    ( void )data;
    (void) data_size;
    *response_required = false;
    return HOST_INTERFACE_STATUS_NOT_IMPLEMENTED;
}

HOST_Interface_Status_T HOST_INTERFACE_process_Response( const HIL_Application_Message_T* recent_received_message,
                                      HIL_Application_Message_T*  response_message,
                                      bool*                       response_required, uint8_t* data, size_t data_size );

HOST_Interface_Status_T
HOST_INTERFACE_process_Error( const HIL_Application_Message_T* recent_received_message,
                              HIL_Application_Message_T* response_message, bool* response_required,
                              uint8_t* data, size_t data_size )
{
    RunStateFaultReason_T fault = RUN_STATE_FAULT_EXTERNAL_REQUEST;
    switch ( recent_received_message->body.error.category )
    {
        case HIL_APPLICATION_ERROR_CATEGORY_INVALID:
            if ( RUN_STATE_MANAGER_RequestFault( fault ) == false )
            {
                // Construct the error message
                HOST_INTERFACE_Default_Error(response_message);
                *response_required = true;
                return HOST_INTERFACE_STATUS_OK;
            }
            *response_required = false;
            return HOST_INTERFACE_STATUS_OK;
        default:
            if ( RUN_STATE_MANAGER_RequestFault( fault ) == false )
            {
                // Construct the error message
                HOST_INTERFACE_Default_Error(response_message);
                *response_required = true;
                return HOST_INTERFACE_STATUS_OK;
            }
            *response_required = false;
            return HOST_INTERFACE_STATUS_OK;
    }
}

/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */

HOST_Interface_Status_T HOST_INTERFACE_process_message(const HIL_Application_Message_T* recent_received_message, HIL_Application_Message_T* response_message, bool* response_required, uint8_t* data, size_t data_size)
{
    if ( recent_received_message == NULL || response_message == NULL || response_required == NULL || data== NULL )
    {
        return false;
    }
    response_message->has_test_id = recent_received_message->has_test_id;
    response_message->test_id     = recent_received_message->test_id;
    switch ( recent_received_message->type )
    {
        case HIL_APPLICATION_MESSAGE_TYPE_SYSTEM_INFO_REQUEST:
            return HOST_INTERFACE_process_Info_Request(recent_received_message, response_message, response_required, data, data_size);
        case HIL_APPLICATION_MESSAGE_TYPE_SYSTEM_INFO_RESPONSE:
            return HOST_INTERFACE_process_Info_Response(recent_received_message, response_message, response_required, data, data_size);
        case HIL_APPLICATION_MESSAGE_TYPE_TEST_CONFIGURATION:
            return HOST_INTERFACE_process_Test_Configuration(recent_received_message, response_message, response_required, data, data_size);
        case HIL_APPLICATION_MESSAGE_TYPE_TEST_INSTRUCTION:
            return HOST_INTERFACE_process_Test_Instructions(recent_received_message, response_message, response_required, data, data_size);
        case HIL_APPLICATION_MESSAGE_TYPE_VARIABLE_INSTRUCTION_DATA:
            return HOST_INTERFACE_process_Variable_Instruction_Data(recent_received_message, response_message, response_required, data, data_size);
        case HIL_APPLICATION_MESSAGE_TYPE_EXECUTION_CONTROL:
            return HOST_INTERFACE_process_Execution_Control(recent_received_message, response_message, response_required, data, data_size);
        case HIL_APPLICATION_MESSAGE_TYPE_GLOBAL_CONTROL:
            return HOST_INTERFACE_process_Global_Control(recent_received_message, response_message, response_required, data, data_size);
        case HIL_APPLICATION_MESSAGE_TYPE_TEST_RESULT:
            return HOST_INTERFACE_process_Test_Result(recent_received_message, response_message, response_required, data, data_size);
        case HIL_APPLICATION_MESSAGE_TYPE_VARIABLE_RESULT_DATA:
            return HOST_INTERFACE_process_Variable_Result_Data(recent_received_message, response_message, response_required, data, data_size);
        case HIL_APPLICATION_MESSAGE_TYPE_RESPONSE:
            return HOST_INTERFACE_process_Response(recent_received_message, response_message, response_required, data, data_size);
        case HIL_APPLICATION_MESSAGE_TYPE_ERROR:
            return HOST_INTERFACE_process_Error(recent_received_message, response_message, response_required, data, data_size);
        case HIL_APPLICATION_MESSAGE_TYPE_INVALID:
            return false;
        case HIL_APPLICATION_MESSAGE_TYPE_RESERVED:
            return false;
        default:
            return false;
    }
}
