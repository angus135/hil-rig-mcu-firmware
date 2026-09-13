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

#include "hil_rig_protocol/application/application.h"
#include "hil_rig_protocol/transport/transport.h"
#include "hil_rig_protocol/version.h"
#include "host_interface.h"
#include "rtos_config.h"

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

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

bool HOST_INTERFACE_process_Info_Request(
    const HIL_Application_Message_T* recent_received_message,
    HIL_Application_Message_T* response_message, bool* response_required, uint8_t* data, size_t data_size )
{
    switch ( recent_received_message->body.system_info_request.query )
    {
        case HIL_APPLICATION_SYSTEM_INFO_QUERY_INVALID:
            *response_required = false;
            return false;
        case HIL_APPLICATION_SYSTEM_INFO_QUERY_BASIC:
            // Set the type and subtype
            response_message->type = HIL_APPLICATION_MESSAGE_TYPE_SYSTEM_INFO_RESPONSE;
            response_message->subtype = HIL_APPLICATION_MESSAGE_SUBTYPE_NONE;
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
                return false;
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
            return true;
        case HIL_APPLICATION_SYSTEM_INFO_QUERY_RESERVED:
            *response_required = false;
            return false;
        default:
            *response_required = false;
            return false;
    }
}

bool HOST_INTERFACE_process_Info_Response(const HIL_Application_Message_T* recent_received_message, HIL_Application_Message_T* response_message, bool* response_required, uint8_t* data, size_t data_size);

bool HOST_INTERFACE_process_Test_Configuration(const HIL_Application_Message_T* recent_received_message, HIL_Application_Message_T* response_message, bool* response_required, uint8_t* data, size_t data_size);

bool HOST_INTERFACE_process_Test_Instructions(const HIL_Application_Message_T* recent_received_message, HIL_Application_Message_T* response_message, bool* response_required, uint8_t* data, size_t data_size);

bool HOST_INTERFACE_process_Variable_Instruction_Data(
    const HIL_Application_Message_T* recent_received_message,
    HIL_Application_Message_T* response_message, bool* response_required, uint8_t* data, size_t data_size )
{
    // NOT IMPLEMENTED
    *response_required = false;
    return false;
}

bool HOST_INTERFACE_process_Execution_Control(const HIL_Application_Message_T* recent_received_message, HIL_Application_Message_T* response_message, bool* response_required, uint8_t* data, size_t data_size);

bool HOST_INTERFACE_process_Global_Control(const HIL_Application_Message_T* recent_received_message, HIL_Application_Message_T* response_message, bool* response_required, uint8_t* data, size_t data_size);

bool HOST_INTERFACE_process_Test_Result(const HIL_Application_Message_T* recent_received_message, HIL_Application_Message_T* response_message, bool* response_required, uint8_t* data, size_t data_size);

bool HOST_INTERFACE_process_Variable_Result_Data(
    const HIL_Application_Message_T* recent_received_message,
    HIL_Application_Message_T* response_message, bool* response_required, uint8_t* data, size_t data_size )
{
    // NOT IMPLEMENTED
    *response_required = false;
    return false;
}

bool HOST_INTERFACE_process_Response( const HIL_Application_Message_T* recent_received_message,
                                      HIL_Application_Message_T*  response_message,
                                      bool*                       response_required, uint8_t* data, size_t data_size );

bool HOST_INTERFACE_process_Error(const HIL_Application_Message_T* recent_received_message, HIL_Application_Message_T* response_message, bool* response_required, uint8_t* data, size_t data_size);

/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */

bool HOST_INTERFACE_process_message(const HIL_Application_Message_T* recent_received_message, HIL_Application_Message_T* response_message, bool* response_required, uint8_t* data, size_t data_size)
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
