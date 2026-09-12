/******************************************************************************
 *  File:       host_interface_test_access.h
 *  Description:
 *      C-only white-box accessors for Host Interface unit tests.
 ******************************************************************************/

#ifndef HOST_INTERFACE_TEST_ACCESS_H
#define HOST_INTERFACE_TEST_ACCESS_H
#ifdef TEST_BUILD

#include <stdbool.h>

#include "hil_rig_protocol/transport/transport.h"

/**
 * @brief Copy the initialized Host Interface Transport configuration for tests.
 *
 * @param[out] config Receives the configured Transport policy.
 */
void HOST_INTERFACE_Test_Access_Get_Transport_Config( HIL_Transport_Config_T* config );

/**
 * @brief Check that Host Interface Application decode storage is aligned.
 *
 * @return true when the decode-storage address meets the public requirement.
 */
bool HOST_INTERFACE_Test_Access_Application_Decode_Storage_Is_Aligned( void );

/**
 * @brief Exercise first-observation disconnected-link cleanup for tests.
 */
void HOST_INTERFACE_Test_Access_Observe_Disconnected_Link( void );

/**
 * @brief Reset the Host Interface protocol state used by processing tests.
 */
void HOST_INTERFACE_Test_Access_Reset_Protocol( void );

/**
 * @brief Process one Host Interface protocol cycle for tests.
 */
void HOST_INTERFACE_Test_Access_Process_Once( void );

#endif
#endif /* HOST_INTERFACE_TEST_ACCESS_H */
