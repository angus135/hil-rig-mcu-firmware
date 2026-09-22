/******************************************************************************
 *  File:       host_process_message_test_access.c
 *  Description:
 *      C-only white-box accessors for Host Process Message unit tests.
 ******************************************************************************/

#include "host_process_message_test_access.h"

/* Internal helpers from host_process_message.c forward declared before include
 * to provide prototypes and avoid -Wmissing-declarations. */
extern HOST_Interface_Status_T HOST_INTERFACE_Default_Error( HIL_Application_Message_T* message );
extern HOST_Interface_Status_T
HOST_INTERFACE_state_to_state_request( Host_RunState_Request_T request,
                                       uint32_t                expected_tick_count );
extern HOST_Interface_Status_T
HOST_INTERFACE_request_state_tranistion( RunState_T expected_state, Host_RunState_Request_T request,
                                         uint16_t num_trys, uint32_t expected_tick_count );
extern HOST_Interface_Status_T
HOST_INTERFACE_process_Info_Request( const HIL_Application_Message_T* incoming_message,
                                     HIL_Application_Message_T*       outgoing_message,
                                     bool* response_required, uint8_t* data, size_t data_size );
extern HOST_Interface_Status_T
HOST_INTERFACE_process_Info_Response( const HIL_Application_Message_T* incoming_message,
                                      HIL_Application_Message_T*       outgoing_message,
                                      bool* response_required, uint8_t* data, size_t data_size );
extern HOST_Interface_Status_T HOST_INTERFACE_process_Test_Configuration(
    const HIL_Application_Message_T* incoming_message, HIL_Application_Message_T* outgoing_message,
    bool* response_required, uint8_t* data, size_t data_size, uint32_t* expected_tick_count );
extern HOST_Interface_Status_T HOST_INTERFACE_process_Test_Instructions(
    const HIL_Application_Message_T* incoming_message, HIL_Application_Message_T* outgoing_message,
    bool* response_required, uint8_t* data, size_t data_size );
extern HOST_Interface_Status_T HOST_INTERFACE_process_Finalize_Test_Upload(
    const HIL_Application_Message_T* incoming_message, HIL_Application_Message_T* outgoing_message,
    bool* response_required, uint8_t* data, size_t data_size, uint32_t* expected_tick_count );
extern HOST_Interface_Status_T HOST_INTERFACE_process_Variable_Instruction_Data(
    const HIL_Application_Message_T* incoming_message, HIL_Application_Message_T* outgoing_message,
    bool* response_required, uint8_t* data, size_t data_size );
extern HOST_Interface_Status_T HOST_INTERFACE_process_Execution_Control(
    const HIL_Application_Message_T* incoming_message, HIL_Application_Message_T* outgoing_message,
    bool* response_required, uint8_t* data, size_t data_size );
extern HOST_Interface_Status_T
HOST_INTERFACE_process_Global_Control( const HIL_Application_Message_T* incoming_message,
                                       HIL_Application_Message_T*       outgoing_message,
                                       bool* response_required, uint8_t* data, size_t data_size );
extern HOST_Interface_Status_T
HOST_INTERFACE_process_Test_Result( const HIL_Application_Message_T* incoming_message,
                                    HIL_Application_Message_T*       outgoing_message,
                                    bool* response_required, uint8_t* data, size_t data_size );
extern HOST_Interface_Status_T HOST_INTERFACE_process_Variable_Result_Data(
    const HIL_Application_Message_T* incoming_message, HIL_Application_Message_T* outgoing_message,
    bool* response_required, uint8_t* data, size_t data_size );
extern HOST_Interface_Status_T
HOST_INTERFACE_process_Response( const HIL_Application_Message_T* incoming_message,
                                 HIL_Application_Message_T*       outgoing_message,
                                 bool* response_required, uint8_t* data, size_t data_size );
extern HOST_Interface_Status_T
HOST_INTERFACE_process_Error( const HIL_Application_Message_T* incoming_message,
                              HIL_Application_Message_T* outgoing_message, bool* response_required,
                              uint8_t* data, size_t data_size );
extern HOST_Interface_Status_T HOST_INTERFACE_process_Package_Received_Notification(
    HIL_Application_Message_T* outgoing_message, uint32_t* notifications, bool* response_required,
    uint8_t* data, size_t data_size );
extern HOST_Interface_Status_T HOST_INTERFACE_process_Config_Started_Notification(
    HIL_Application_Message_T* outgoing_message, uint32_t* notifications, bool* response_required,
    uint8_t* data, size_t data_size );
extern HOST_Interface_Status_T HOST_INTERFACE_process_Armed_Notification(
    HIL_Application_Message_T* outgoing_message, uint32_t* notifications, bool* response_required,
    uint8_t* data, size_t data_size );
extern HOST_Interface_Status_T HOST_INTERFACE_process_Execution_Complete_Notification(
    HIL_Application_Message_T* outgoing_message, uint32_t* notifications, bool* response_required,
    uint8_t* data, size_t data_size );
extern HOST_Interface_Status_T HOST_INTERFACE_process_Transfer_Complete_Notification(
    HIL_Application_Message_T* outgoing_message, uint32_t* notifications, bool* response_required,
    uint8_t* data, size_t data_size );
extern HOST_Interface_Status_T HOST_INTERFACE_process_Result_Transfer_Notification(
    HIL_Application_Message_T* outgoing_message, uint32_t* notifications, bool* response_required,
    uint8_t* data, size_t data_size );
extern HOST_Interface_Status_T HOST_INTERFACE_process_incoming_message(
    bool incoming_message_available, const HIL_Application_Message_T* incoming_message,
    HIL_Application_Message_T* outgoing_message, bool* response_required, uint8_t* data,
    size_t data_size, uint32_t* expected_tick_count );
extern HOST_Interface_Status_T
HOST_INTERFACE_process_internal_message( HIL_Application_Message_T* outgoing_message,
                                         bool* response_required, uint8_t* data, size_t data_size,
                                         uint32_t* notifications );

/* Include the implementation directly so the white-box accessors and public
 * function are linked into this test target without changing production code. */
#include "../host_process_message.c"  // NOLINT

HOST_Interface_Status_T
HOST_INTERFACE_Test_Access_Default_Error( HIL_Application_Message_T* message )
{
    return HOST_INTERFACE_Default_Error( message );
}

HOST_Interface_Status_T
HOST_INTERFACE_Test_Access_State_To_State_Request( Host_RunState_Request_T request,
                                                   uint32_t                expected_tick_count )
{
    return HOST_INTERFACE_state_to_state_request( request, expected_tick_count );
}

HOST_Interface_Status_T HOST_INTERFACE_Test_Access_Request_State_Transition(
    RunState_T expected_state, Host_RunState_Request_T request, uint16_t num_trys,
    uint32_t expected_tick_count )
{
    return HOST_INTERFACE_request_state_tranistion( expected_state, request, num_trys,
                                                    expected_tick_count );
}

#define HOST_INTERFACE_TEST_FORWARD_4( public_name, internal_name )                                \
    HOST_Interface_Status_T HOST_INTERFACE_Test_Access_##public_name(                              \
        const HIL_Application_Message_T* incoming_message,                                         \
        HIL_Application_Message_T* outgoing_message, bool* response_required, uint8_t* data,       \
        size_t data_size )                                                                         \
    {                                                                                              \
        return HOST_INTERFACE_##internal_name( incoming_message, outgoing_message,                 \
                                               response_required, data, data_size );               \
    }

HOST_INTERFACE_TEST_FORWARD_4( Process_Info_Request, process_Info_Request )
HOST_INTERFACE_TEST_FORWARD_4( Process_Info_Response, process_Info_Response )
HOST_INTERFACE_TEST_FORWARD_4( Process_Variable_Instruction_Data,
                               process_Variable_Instruction_Data )
HOST_INTERFACE_TEST_FORWARD_4( Process_Execution_Control, process_Execution_Control )
HOST_INTERFACE_TEST_FORWARD_4( Process_Global_Control, process_Global_Control )
HOST_INTERFACE_TEST_FORWARD_4( Process_Test_Result, process_Test_Result )
HOST_INTERFACE_TEST_FORWARD_4( Process_Variable_Result_Data, process_Variable_Result_Data )
HOST_INTERFACE_TEST_FORWARD_4( Process_Response, process_Response )
HOST_INTERFACE_TEST_FORWARD_4( Process_Error, process_Error )

HOST_Interface_Status_T HOST_INTERFACE_Test_Access_Process_Test_Configuration(
    const HIL_Application_Message_T* incoming_message, HIL_Application_Message_T* outgoing_message,
    bool* response_required, uint8_t* data, size_t data_size, uint32_t* expected_tick_count )
{
    return HOST_INTERFACE_process_Test_Configuration( incoming_message, outgoing_message,
                                                      response_required, data, data_size,
                                                      expected_tick_count );
}

HOST_Interface_Status_T HOST_INTERFACE_Test_Access_Process_Test_Instructions(
    const HIL_Application_Message_T* incoming_message, HIL_Application_Message_T* outgoing_message,
    bool* response_required, uint8_t* data, size_t data_size )
{
    return HOST_INTERFACE_process_Test_Instructions( incoming_message, outgoing_message,
                                                     response_required, data, data_size );
}

HOST_Interface_Status_T HOST_INTERFACE_Test_Access_Process_Finalize_Test_Upload(
    const HIL_Application_Message_T* incoming_message, HIL_Application_Message_T* outgoing_message,
    bool* response_required, uint8_t* data, size_t data_size, uint32_t* expected_tick_count )
{
    return HOST_INTERFACE_process_Finalize_Test_Upload( incoming_message, outgoing_message,
                                                        response_required, data, data_size,
                                                        expected_tick_count );
}

#define HOST_INTERFACE_TEST_FORWARD_NOTIFICATION( public_name, internal_name )                     \
    HOST_Interface_Status_T HOST_INTERFACE_Test_Access_##public_name(                              \
        HIL_Application_Message_T* outgoing_message, uint32_t* notifications,                      \
        bool* response_required, uint8_t* data, size_t data_size )                                 \
    {                                                                                              \
        return HOST_INTERFACE_##internal_name( outgoing_message, notifications, response_required, \
                                               data, data_size );                                  \
    }

HOST_INTERFACE_TEST_FORWARD_NOTIFICATION( Process_Package_Received_Notification,
                                          process_Package_Received_Notification )
HOST_INTERFACE_TEST_FORWARD_NOTIFICATION( Process_Config_Started_Notification,
                                          process_Config_Started_Notification )
HOST_INTERFACE_TEST_FORWARD_NOTIFICATION( Process_Armed_Notification,
                                          process_Armed_Notification )
HOST_INTERFACE_TEST_FORWARD_NOTIFICATION( Process_Execution_Complete_Notification,
                                          process_Execution_Complete_Notification )
HOST_INTERFACE_TEST_FORWARD_NOTIFICATION( Process_Transfer_Complete_Notification,
                                          process_Transfer_Complete_Notification )
HOST_INTERFACE_TEST_FORWARD_NOTIFICATION( Process_Result_Transfer_Notification,
                                          process_Result_Transfer_Notification )

HOST_Interface_Status_T HOST_INTERFACE_Test_Access_Process_Incoming_Message(
    bool incoming_message_available, const HIL_Application_Message_T* incoming_message,
    HIL_Application_Message_T* outgoing_message, bool* response_required, uint8_t* data,
    size_t data_size, uint32_t* expected_tick_count )
{
    return HOST_INTERFACE_process_incoming_message( incoming_message_available, incoming_message,
                                                    outgoing_message, response_required, data,
                                                    data_size, expected_tick_count );
}

HOST_Interface_Status_T
HOST_INTERFACE_Test_Access_Process_Internal_Message( HIL_Application_Message_T* outgoing_message,
                                                     bool* response_required, uint8_t* data,
                                                     size_t data_size, uint32_t* notifications )
{
    return HOST_INTERFACE_process_internal_message( outgoing_message, response_required, data,
                                                    data_size, notifications );
}

#undef HOST_INTERFACE_TEST_FORWARD_4
#undef HOST_INTERFACE_TEST_FORWARD_NOTIFICATION
