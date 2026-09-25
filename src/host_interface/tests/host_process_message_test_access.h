/******************************************************************************
 *  File:       host_process_message_test_access.h
 *  Author:     OpenAI
 *
 *  Description:
 *      C-only white-box accessors for Host Process Message unit tests.
 *
 *      The implementation under test keeps several message-processing helpers
 *      outside the public header. These accessors expose those helpers to the
 *      GoogleTest suite without changing production headers.
 ******************************************************************************/

#ifndef HOST_PROCESS_MESSAGE_TEST_ACCESS_H
#define HOST_PROCESS_MESSAGE_TEST_ACCESS_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "host_process_message.h"
#include "hil_rig_protocol/application/application_message.h"
#include "run_state_manager.h"

HOST_Interface_Status_T
HOST_INTERFACE_Test_Access_Default_Error( HIL_Application_Message_T* message );

HOST_Interface_Status_T
HOST_INTERFACE_Test_Access_State_To_State_Request( Host_RunState_Request_T request,
                                                   uint32_t                expected_tick_count );

HOST_Interface_Status_T HOST_INTERFACE_Test_Access_Request_State_Transition(
    RunState_T expected_state, Host_RunState_Request_T request, uint16_t num_trys,
    uint32_t expected_tick_count );

HOST_Interface_Status_T HOST_INTERFACE_Test_Access_Process_Info_Request(
    const HIL_Application_Message_T* incoming_message, HIL_Application_Message_T* outgoing_message,
    bool* response_required, uint8_t* data, size_t data_size );

HOST_Interface_Status_T HOST_INTERFACE_Test_Access_Process_Info_Response(
    const HIL_Application_Message_T* incoming_message, HIL_Application_Message_T* outgoing_message,
    bool* response_required, uint8_t* data, size_t data_size );

HOST_Interface_Status_T HOST_INTERFACE_Test_Access_Process_Test_Configuration(
    const HIL_Application_Message_T* incoming_message, HIL_Application_Message_T* outgoing_message,
    bool* response_required, uint8_t* data, size_t data_size, uint32_t* expected_tick_count );

HOST_Interface_Status_T HOST_INTERFACE_Test_Access_Process_Test_Instructions(
    const HIL_Application_Message_T* incoming_message, HIL_Application_Message_T* outgoing_message,
    bool* response_required, uint8_t* data, size_t data_size );

HOST_Interface_Status_T HOST_INTERFACE_Test_Access_Process_Finalize_Test_Upload(
    const HIL_Application_Message_T* incoming_message, HIL_Application_Message_T* outgoing_message,
    bool* response_required, uint8_t* data, size_t data_size, uint32_t* expected_tick_count );

HOST_Interface_Status_T HOST_INTERFACE_Test_Access_Process_Variable_Instruction_Data(
    const HIL_Application_Message_T* incoming_message, HIL_Application_Message_T* outgoing_message,
    bool* response_required, uint8_t* data, size_t data_size );

HOST_Interface_Status_T HOST_INTERFACE_Test_Access_Process_Execution_Control(
    const HIL_Application_Message_T* incoming_message, HIL_Application_Message_T* outgoing_message,
    bool* response_required, uint8_t* data, size_t data_size );

HOST_Interface_Status_T HOST_INTERFACE_Test_Access_Process_Global_Control(
    const HIL_Application_Message_T* incoming_message, HIL_Application_Message_T* outgoing_message,
    bool* response_required, uint8_t* data, size_t data_size );

HOST_Interface_Status_T HOST_INTERFACE_Test_Access_Process_Test_Result(
    const HIL_Application_Message_T* incoming_message, HIL_Application_Message_T* outgoing_message,
    bool* response_required, uint8_t* data, size_t data_size );

HOST_Interface_Status_T HOST_INTERFACE_Test_Access_Process_Variable_Result_Data(
    const HIL_Application_Message_T* incoming_message, HIL_Application_Message_T* outgoing_message,
    bool* response_required, uint8_t* data, size_t data_size );

HOST_Interface_Status_T HOST_INTERFACE_Test_Access_Process_Response(
    const HIL_Application_Message_T* incoming_message, HIL_Application_Message_T* outgoing_message,
    bool* response_required, uint8_t* data, size_t data_size );

HOST_Interface_Status_T HOST_INTERFACE_Test_Access_Process_Error(
    const HIL_Application_Message_T* incoming_message, HIL_Application_Message_T* outgoing_message,
    bool* response_required, uint8_t* data, size_t data_size );

HOST_Interface_Status_T HOST_INTERFACE_Test_Access_Process_Package_Received_Notification(
    HIL_Application_Message_T* outgoing_message, uint32_t* notifications, bool* response_required,
    uint8_t* data, size_t data_size );

HOST_Interface_Status_T HOST_INTERFACE_Test_Access_Process_Config_Started_Notification(
    HIL_Application_Message_T* outgoing_message, uint32_t* notifications, bool* response_required,
    uint8_t* data, size_t data_size );

HOST_Interface_Status_T HOST_INTERFACE_Test_Access_Process_Armed_Notification(
    HIL_Application_Message_T* outgoing_message, uint32_t* notifications, bool* response_required,
    uint8_t* data, size_t data_size );

HOST_Interface_Status_T HOST_INTERFACE_Test_Access_Process_Execution_Complete_Notification(
    HIL_Application_Message_T* outgoing_message, uint32_t* notifications, bool* response_required,
    uint8_t* data, size_t data_size );

HOST_Interface_Status_T HOST_INTERFACE_Test_Access_Process_Fault_Notification(
    HIL_Application_Message_T* outgoing_message, uint32_t* notifications, bool* response_required,
    uint8_t* data, size_t data_size );

HOST_Interface_Status_T HOST_INTERFACE_Test_Access_Process_Transfer_Complete_Notification(
    HIL_Application_Message_T* outgoing_message, uint32_t* notifications, bool* response_required,
    uint8_t* data, size_t data_size );

HOST_Interface_Status_T HOST_INTERFACE_Test_Access_Process_Result_Transfer_Notification(
    HIL_Application_Message_T* outgoing_message, uint32_t* notifications, bool* response_required,
    uint8_t* data, size_t data_size );

HOST_Interface_Status_T HOST_INTERFACE_Test_Access_Process_Incoming_Message(
    bool incoming_message_available, const HIL_Application_Message_T* incoming_message,
    HIL_Application_Message_T* outgoing_message, bool* response_required, uint8_t* data,
    size_t data_size, uint32_t* expected_tick_count );

HOST_Interface_Status_T
HOST_INTERFACE_Test_Access_Process_Internal_Message( HIL_Application_Message_T* outgoing_message,
                                                     bool* response_required, uint8_t* data,
                                                     size_t data_size, uint32_t* notifications );

void HOST_INTERFACE_Test_Access_Set_Session_State( HOST_INTERFACE_Session_State_T state );

void HOST_INTERFACE_Test_Access_Set_Session( const HostTestSession_T* session );

#ifdef __cplusplus
}
#endif

#endif /* HOST_PROCESS_MESSAGE_TEST_ACCESS_H */
