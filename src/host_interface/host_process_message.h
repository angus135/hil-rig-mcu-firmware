/******************************************************************************
 *  File:       host_process_message.h
 *  Author:     Tim Vogelsang
 *  Created:    13-Sep-2026
 *
 *  Description:
 *      TODO
 *
 *  Notes:
 *      TODO
 ******************************************************************************/

#ifndef HOST_PROCESS_MESSAGE_H
#define HOST_PROCESS_MESSAGE_H

#ifdef __cplusplus
extern "C"
{
#endif

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include <stdint.h>
#include <stdbool.h>
#include "hil_rig_protocol/application/application.h"
#include "hil_rig_protocol/application/application_message.h"
#include "hil_rig_protocol/transport/transport.h"

#include "host_interface.h"

/**-----------------------------------------------------------------------------
 *  Public Defines / Macros
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Public Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

typedef enum
{
    HOST_REQUEST_IDLE = 0,
    HOST_REQUEST_TEST_PACKAGE_RECEIVE,
    HOST_REQUEST_CONFIGURATION,
    HOST_REQUEST_ARMED,
    HOST_REQUEST_EXECUTION,
    HOST_REQUEST_RESULT_FINALISATION,
    HOST_REQUEST_RESULTS_READY,
    HOST_REQUEST_RESULT_TRANSFER,
    HOST_REQUEST_FAULT,
    HOST_REQUEST_ABORT,
    HOST_REQUEST_RESET
} Host_RunState_Request_T;

/**
 * @brief Result of one local host information function.
 *
 * @details Numeric assignments are explicit for stable bindings and diagnostic
 * output. They are local API values and are never serialized as Application
 * wire fields. Function-level documentation specifies output guarantees for
 * each result.
 */
typedef enum
{
    /** Operation completed successfully. */
    HOST_INTERFACE_STATUS_OK = 0,

    /** A pointer, alignment, enum, count, or argument combination is invalid. */
    HOST_INTERFACE_STATUS_INVALID_ARGUMENT = 1,

    /** The supplied context has not completed successful initialization. */
    HOST_INTERFACE_STATUS_UNINITIALIZED = 2,

    /** Caller-provided output or decode storage is smaller than required. */
    HOST_INTERFACE_STATUS_BUFFER_TOO_SMALL = 3,

    /** The tagged message type is invalid or reserved. */
    HOST_INTERFACE_STATUS_INVALID_MESSAGE_TYPE = 4,

    /** The subtype is invalid for the selected message type. */
    HOST_INTERFACE_STATUS_INVALID_SUBTYPE = 5,

    /** Encoded bytes violate the Application envelope or body syntax. */
    HOST_INTERFACE_STATUS_MALFORMED_MESSAGE = 6,

    /** A complete-message input ends before all declared fields are present. */
    HOST_INTERFACE_STATUS_TRUNCATED_MESSAGE = 7,

    /** A byte length, declared data length, or size relationship is invalid. */
    HOST_INTERFACE_STATUS_INVALID_LENGTH = 8,

    /** An element count is invalid or exceeds configured policy. */
    HOST_INTERFACE_STATUS_INVALID_COUNT = 9,

    /** Message feature or overall HIL-RIG protocol version is unsupported. */
    HOST_INTERFACE_STATUS_UNSUPPORTED_MESSAGE = 10,

    /** Test-ID presence is inconsistent with the selected message structure. */
    HOST_INTERFACE_STATUS_INCONSISTENT_TEST_ID = 11,

    /** Tick metadata is inconsistent with the selected message or response scope. */
    HOST_INTERFACE_STATUS_INCONSISTENT_TICK = 12,

    /** A typed message omits structurally required fixed or variable data. */
    HOST_INTERFACE_STATUS_INCOMPLETE_DATA = 13,

    /** Structural validation failed without a more specific result. */
    HOST_INTERFACE_STATUS_VALIDATION_FAILED = 14,

    /** The declared API exists but its runtime behavior is intentionally absent. */
    HOST_INTERFACE_STATUS_NOT_IMPLEMENTED = 15,

    /** A library-private invariant failed without a more specific status. */
    HOST_INTERFACE_STATUS_INTERNAL_ERROR = 16,

    /** A incoming message contained out of date information. */
    HOST_INTERFACE_STATUS_OUT_OF_DATE = 17,

    /** Failed to transition to a different state. */
    HOST_INTERFACE_STATUS_STATE_TRANSITION_FAILURE = 18,

    /** An incoming message requries a response but the outgoing message is busy */
    HOST_INTERFACE_STATUS_OUTGOING_REQUIRED = 19,

    /** Failed to transition to a different state. */
    HOST_INTERFACE_STATUS_UNSUPPORTED_NOTIFICATION = 20,
} HOST_Interface_Status_T;

/**
 * @brief Active instruction family within a test session.
 */
typedef enum
{
    HOST_INSTRUCTION_FAMILY_UNSET = 0,
    HOST_INSTRUCTION_FAMILY_LEGACY_FIXED,   /**< TEST_INSTRUCTION (Type 17) */
    HOST_INSTRUCTION_FAMILY_VARIABLE_UPDATE /**< UPDATE_INSTRUCTION (Type 21) */
} HostInstructionFamily_T;

/**
 * @brief Session context tracked by the Host Interface.
 */
typedef struct
{
    HOST_INTERFACE_Session_State_T state;
    bool                           has_active_test_id;
    HIL_Application_Test_Id_T      active_test_id;
    HostInstructionFamily_T        instruction_family;
    uint32_t                       expected_tick_count;
} HostTestSession_T;

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

HOST_Interface_Status_T HOST_INTERFACE_process_message(
    bool incoming_message_available, const HIL_Application_Message_T* incoming_message,
    bool outgoing_message_accepted, HIL_Application_Message_T* outgoing_message,
    HIL_Application_Message_T* overflow_outgoing_message, bool* response_required, uint8_t* data,
    size_t data_size, uint32_t* notifications, uint32_t* expected_tick_count );

/**
 * @brief Resets the active test session to IDLE.
 */
void HOST_INTERFACE_Reset_Session( void );

/**
 * @brief Returns a read-only pointer to the active session state.
 */
const HostTestSession_T* HOST_INTERFACE_Get_Session( void );

#ifdef __cplusplus
}
#endif

#endif /* HOST_INTERFACE_H */
