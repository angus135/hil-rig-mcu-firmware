/******************************************************************************
 *  File:       host_interface_test_access.c
 *  Description:
 *      C-only white-box accessors for Host Interface unit tests.
 ******************************************************************************/

#ifdef TEST_BUILD

#include <stdint.h>

#include "host_interface_test_access.h"
#define HOST_INTERFACE_DIRECT_USB_STREAMING ( 0 )
#include "../host_interface.c"  // NOLINT

/* The Host Interface integration tests exercise the protocol service loop
 * directly. Keep task-level message dispatch out of this white-box target. */
HOST_Interface_Status_T HOST_INTERFACE_process_message(
    bool incoming_message_available, const HIL_Application_Message_T* incoming_message,
    bool outgoing_message_accepted, HIL_Application_Message_T* outgoing_message,
    HIL_Application_Message_T* overflow_outgoing_message, bool* response_required, uint8_t* data,
    size_t data_size, uint32_t* notifications, uint32_t* expected_tick_count )
{
    ( void )incoming_message_available;
    ( void )incoming_message;
    ( void )outgoing_message_accepted;
    ( void )outgoing_message;
    ( void )overflow_outgoing_message;
    ( void )response_required;
    ( void )data;
    ( void )data_size;
    ( void )notifications;
    ( void )expected_tick_count;
    return HOST_INTERFACE_STATUS_OK;
}

void HOST_INTERFACE_Reset_Session( void )
{
}

const HostTestSession_T* HOST_INTERFACE_Get_Session( void )
{
    static const HostTestSession_T s_stub_session = { 0 };
    return &s_stub_session;
}

bool RUN_STATE_MANAGER_RequestFault( RunStateFaultReason_T reason )
{
    ( void )reason;
    return true;
}

RunState_T RUN_STATE_MANAGER_GetState( void )
{
    return RUN_STATE_IDLE;
}

static HOST_INTERFACE_Protocol_State_T s_protocol_state;

/**
 * @brief Copy the initialized Host Interface Transport configuration for tests.
 *
 * @param[out] config Receives the configured Transport policy.
 */
void HOST_INTERFACE_Test_Access_Get_Transport_Config( HIL_Transport_Config_T* const config )
{
    HOST_INTERFACE_Protocol_State_T protocol_state = { 0 };

    if ( config == NULL )
    {
        return;
    }

    HOST_INTERFACE_Protocol_Init( &protocol_state );
    *config = protocol_state.transport.config;
}

/**
 * @brief Check that Host Interface Application decode storage is aligned.
 *
 * @return true when the decode-storage address meets the public requirement.
 */
bool HOST_INTERFACE_Test_Access_Application_Decode_Storage_Is_Aligned( void )
{
    HOST_INTERFACE_Application_State_T application_state = { 0 };

    return ( ( uintptr_t )application_state.receive_data
             % HIL_APPLICATION_DECODE_STORAGE_ALIGNMENT )
           == 0U;
}

/**
 * @brief Exercise first-observation disconnected-link cleanup for tests.
 */
void HOST_INTERFACE_Test_Access_Observe_Disconnected_Link( void )
{
    HOST_INTERFACE_Protocol_State_T protocol_state = { 0 };

    HOST_INTERFACE_Protocol_Init( &protocol_state );
    HOST_INTERFACE_Protocol_Update_Link_State( &protocol_state,
                                               HIL_TRANSPORT_LINK_STATE_DISCONNECTED, 0U );
}

/**
 * @brief Reset the Host Interface protocol state used by processing tests.
 */
void HOST_INTERFACE_Test_Access_Reset_Protocol( void )
{
    HOST_INTERFACE_Protocol_Init( &s_protocol_state );
}

/**
 * @brief Process one Host Interface protocol cycle for tests.
 */
void HOST_INTERFACE_Test_Access_Process_Once( void )
{
    HIL_Application_Message_T incoming_message           = { 0 };
    bool                      incoming_message_available = false;
    bool                      outgoing_message_accepted  = false;

    HOST_INTERFACE_Protocol_Process( &s_protocol_state, NULL, &outgoing_message_accepted, true,
                                     &incoming_message, &incoming_message_available );
}

void HOST_INTERFACE_Test_Access_Process_Once_With_Consumption(
    const bool can_consume_incoming_message, HIL_Application_Message_T* const incoming_message,
    bool* const incoming_message_available )
{
    bool outgoing_message_accepted = false;

    if ( incoming_message == NULL || incoming_message_available == NULL )
    {
        return;
    }

    HOST_INTERFACE_Protocol_Process( &s_protocol_state, NULL, &outgoing_message_accepted,
                                     can_consume_incoming_message, incoming_message,
                                     incoming_message_available );
}

/**
 * @brief Read the public Transport status for the processing-test instance.
 *
 * @param[out] status Receives the public Transport status snapshot.
 * @return Status returned by HIL_TRANSPORT_Get_Status().
 */
HIL_Transport_Status_T
HOST_INTERFACE_Test_Access_Get_Transport_Status( HIL_Transport_Status_Snapshot_T* const status )
{
    return HIL_TRANSPORT_Get_Status( &s_protocol_state.transport.context, status );
}

/**
 * @brief Read the effective Transport time for the processing-test instance.
 *
 * @return Effective Transport time in milliseconds.
 */
uint32_t HOST_INTERFACE_Test_Access_Get_Transport_Time( void )
{
    return s_protocol_state.transport_clock.effective_time_ms;
}

#endif
