/******************************************************************************
 *  File:       host_interface_test_access.c
 *  Description:
 *      C-only white-box accessors for Host Interface unit tests.
 ******************************************************************************/

#ifdef TEST_BUILD

#include <stdint.h>

#include "host_interface_test_access.h"

#include "../host_interface.c"  // NOLINT

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
    HIL_Application_Message_T incoming_message;
    bool                      outgoing_message_accepted;
    bool                      incoming_message_available;

    HOST_INTERFACE_Protocol_Process( &s_protocol_state, NULL, &outgoing_message_accepted,
                                     &incoming_message, &incoming_message_available );
}

#endif
