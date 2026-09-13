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

bool HOST_INTERFACE_process_Info_Request(HIL_Application_System_Info_Request_T* recent_received_message, HIL_Application_Message_T* resposne_message, bool* response_required);
bool HOST_INTERFACE_process_Info_Response(HIL_Application_System_Info_Response_T* recent_received_message, HIL_Application_Message_T* resposne_message, bool* response_required);
bool HOST_INTERFACE_process_Test_Configuration(HIL_Application_Test_Configuration_T* recent_received_message, HIL_Application_Message_T* resposne_message, bool* response_required);
bool HOST_INTERFACE_process_Test_Instructions(HIL_Application_Test_Instruction_T* recent_received_message, HIL_Application_Message_T* resposne_message, bool* response_required);
bool HOST_INTERFACE_process_Variable_Instruction_Data(HIL_Application_Variable_Instruction_Data_T* recent_received_message, HIL_Application_Message_T* resposne_message, bool* response_required);
bool HOST_INTERFACE_process_Execution_Control(HIL_Application_Execution_Control_T* recent_received_message, HIL_Application_Message_T* resposne_message, bool* response_required);
bool HOST_INTERFACE_process_Global_Control(HIL_Application_Global_Control_T* recent_received_message, HIL_Application_Message_T* resposne_message, bool* response_required);
bool HOST_INTERFACE_process_Test_Result(HIL_Application_Test_Result_T* recent_received_message, HIL_Application_Message_T* resposne_message, bool* response_required);
bool HOST_INTERFACE_process_Variable_Result_Data(HIL_Application_Variable_Result_Data_T* recent_received_message, HIL_Application_Message_T* resposne_message, bool* response_required);
bool HOST_INTERFACE_process_Response( HIL_Application_Response_T* recent_received_message,
                                      HIL_Application_Message_T*  resposne_message,
                                      bool*                       response_required );
bool HOST_INTERFACE_process_Error(HIL_Application_Error_T* recent_received_message, HIL_Application_Message_T* resposne_message, bool* response_required);

/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */

bool HOST_INTERFACE_process_message(HIL_Application_Message_T* recent_received_message, HIL_Application_Message_T* resposne_message, bool* response_required)
{
    switch ( recent_received_message->type )
    {
        case HIL_APPLICATION_MESSAGE_TYPE_SYSTEM_INFO_REQUEST:
            return HOST_INTERFACE_process_Info_Request(&(recent_received_message->body.system_info_request), resposne_message, response_required);
        case HIL_APPLICATION_MESSAGE_TYPE_SYSTEM_INFO_RESPONSE:
            return HOST_INTERFACE_process_Info_Response(&(recent_received_message->body.system_info_response), resposne_message, response_required);
        case HIL_APPLICATION_MESSAGE_TYPE_TEST_CONFIGURATION:
            return HOST_INTERFACE_process_Test_Configuration(&(recent_received_message->body.test_configuration), resposne_message, response_required);
        case HIL_APPLICATION_MESSAGE_TYPE_TEST_INSTRUCTION:
            return HOST_INTERFACE_process_Test_Instructions(&(recent_received_message->body.test_instruction), resposne_message, response_required);
        case HIL_APPLICATION_MESSAGE_TYPE_VARIABLE_INSTRUCTION_DATA:
            return HOST_INTERFACE_process_Variable_Instruction_Data(&(recent_received_message->body.variable_instruction_data), resposne_message, response_required);
        case HIL_APPLICATION_MESSAGE_TYPE_EXECUTION_CONTROL:
            return HOST_INTERFACE_process_Execution_Control(&(recent_received_message->body.execution_control), resposne_message, response_required);
        case HIL_APPLICATION_MESSAGE_TYPE_GLOBAL_CONTROL:
            return HOST_INTERFACE_process_Global_Control(&(recent_received_message->body.global_control), resposne_message, response_required);
        case HIL_APPLICATION_MESSAGE_TYPE_TEST_RESULT:
            return HOST_INTERFACE_process_Test_Result(&(recent_received_message->body.test_result), resposne_message, response_required);
        case HIL_APPLICATION_MESSAGE_TYPE_VARIABLE_RESULT_DATA:
            return HOST_INTERFACE_process_Variable_Result_Data(&(recent_received_message->body.variable_result_data), resposne_message, response_required);
        case HIL_APPLICATION_MESSAGE_TYPE_RESPONSE:
            return HOST_INTERFACE_process_Response(&(recent_received_message->body.response), resposne_message, response_required);
        case HIL_APPLICATION_MESSAGE_TYPE_ERROR:
            return HOST_INTERFACE_process_Error(&(recent_received_message->body.error), resposne_message, response_required);
        case HIL_APPLICATION_MESSAGE_TYPE_INVALID:
            return false;
        case HIL_APPLICATION_MESSAGE_TYPE_RESERVED:
            return false;
        default:
            return false;
    }
}
