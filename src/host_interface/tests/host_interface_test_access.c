/******************************************************************************
 *  File:       host_interface_test_access.c
 *  Description:
 *      C-only white-box accessors for Host Interface unit tests.
 ******************************************************************************/

#ifdef TEST_BUILD

#include <stdint.h>

#include "host_interface_test_access.h"
#ifndef HOST_INTERFACE_DIRECT_USB_STREAMING
#define HOST_INTERFACE_DIRECT_USB_STREAMING ( 0 )
#endif
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

#ifndef HOST_INTERFACE_TEST_REAL_VARIABLE_RESULT_PRODUCER
void VARIABLE_RESULT_MESSAGE_PRODUCER_GetDiagnostics( VariableResultProducerDiagnostics_T* diags )
{
    if ( diags != NULL )
    {
        ( void )memset( diags, 0, sizeof( *diags ) );
    }
}
#endif

void HOST_VARIABLE_INSTRUCTION_HANDLER_GetDiagnostics( uint32_t* last_stage,
                                                       uint32_t* last_stage_code,
                                                       uint32_t* last_failed_tick )
{
    if ( last_stage != NULL )
    {
        *last_stage = 0U;
    }
    if ( last_stage_code != NULL )
    {
        *last_stage_code = 0U;
    }
    if ( last_failed_tick != NULL )
    {
        *last_failed_tick = 0U;
    }
}

static HOST_INTERFACE_Protocol_State_T s_protocol_state;

/**
 * @brief Copy the initialized Host Interface Transport configuration for tests.
 *
 * @param[out] config Receives the configured Transport policy.
 */
void HOST_INTERFACE_Test_Access_Get_Transport_Config( HIL_Transport_Config_T* const config )
{
    if ( config == NULL )
    {
        return;
    }

#if !HOST_INTERFACE_DIRECT_USB_STREAMING
    HOST_INTERFACE_Protocol_State_T protocol_state = { 0 };
    HOST_INTERFACE_Protocol_Init( &protocol_state );
    *config = protocol_state.transport.config;
#else
    *config = ( HIL_Transport_Config_T ){ 0 };
#endif
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
    HOST_INTERFACE_Result_Tx_Audit_Reset();
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

/**
 * @brief Submit one outgoing Application message through the production protocol service path.
 *
 * @param[in] outgoing_message Message to encode and submit, or NULL to service retained output.
 * @return true when the Host Interface copied and accepted outgoing_message during this cycle.
 */
bool HOST_INTERFACE_Test_Access_Submit_Outgoing(
    const HIL_Application_Message_T* const outgoing_message )
{
    HIL_Application_Message_T incoming_message           = { 0 };
    bool                      incoming_message_available = false;
    bool                      outgoing_message_accepted  = false;

    HOST_INTERFACE_Protocol_Process( &s_protocol_state, outgoing_message,
                                     &outgoing_message_accepted, true, &incoming_message,
                                     &incoming_message_available );

    return outgoing_message_accepted;
}

/**
 * @brief Read the number of direct-stream bytes retained for a later USB retry.
 *
 * @return Number of valid bytes in the Host Interface direct transmit staging buffer.
 */
size_t HOST_INTERFACE_Test_Access_Get_Direct_Pending_Bytes( void )
{
    return s_protocol_state.application.used_send_byte_span_size;
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
#if !HOST_INTERFACE_DIRECT_USB_STREAMING
    return HIL_TRANSPORT_Get_Status( &s_protocol_state.transport.context, status );
#else
    if ( status != NULL )
    {
        *status = ( HIL_Transport_Status_Snapshot_T ){ 0 };
    }
    return HIL_TRANSPORT_STATUS_OK;
#endif
}

/**
 * @brief Read the effective Transport time for the processing-test instance.
 *
 * @return Effective Transport time in milliseconds.
 */
uint32_t HOST_INTERFACE_Test_Access_Get_Transport_Time( void )
{
#if !HOST_INTERFACE_DIRECT_USB_STREAMING
    return s_protocol_state.transport_clock.effective_time_ms;
#else
    return 0U;
#endif
}

#endif
