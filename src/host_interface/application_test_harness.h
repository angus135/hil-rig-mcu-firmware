/******************************************************************************
 *  File:       application_test_harness.h
 *  Author:     OpenAI
 *  Created:    06-Sep-2026
 *
 *  Description:
 *      Test-only Application-layer transaction harness for Transport hardware
 *      testing. The harness uses only the public shared Application codec and
 *      deliberately does not operate physical peripherals.
 ******************************************************************************/

#ifndef APPLICATION_TEST_HARNESS_H
#define APPLICATION_TEST_HARNESS_H

#ifdef __cplusplus
extern "C"
{
#endif

#include "hil_rig_protocol/application/application.h"

#include <stddef.h>
#include <stdint.h>

#define APPLICATION_TEST_HARNESS_MAX_ENCODED_MESSAGE_SIZE 512U
#define APPLICATION_TEST_HARNESS_MAX_VARIABLE_DATA_SIZE 255U
#define APPLICATION_TEST_HARNESS_MAX_VARIABLE_TRANSFERS_PER_TICK 8U
#define APPLICATION_TEST_HARNESS_MAX_EXPECTED_TICK_COUNT 1000000U
#define APPLICATION_TEST_HARNESS_COMPATIBILITY_PROFILE_ID UINT32_C( 0x41505032 )
#define APPLICATION_TEST_HARNESS_MAX_RETAINED_TICKS 4U
#define APPLICATION_TEST_HARNESS_MAX_VARIABLE_ASSEMBLY_BYTES 4096U
#define APPLICATION_TEST_HARNESS_MAX_VARIABLE_RECORDS_PER_TICK 512U
#define APPLICATION_TEST_HARNESS_MAX_RESULT_RECORDS_PER_CHUNK 64U
#define APPLICATION_TEST_HARNESS_MAX_RESULT_CHUNKS_PER_TICK 8U

typedef enum
{
    APPLICATION_TEST_HARNESS_STATE_UNINITIALIZED             = 0,
    APPLICATION_TEST_HARNESS_STATE_WAITING_FOR_CONFIGURATION = 1,
    APPLICATION_TEST_HARNESS_STATE_ACCEPTING_INSTRUCTIONS    = 2,
    APPLICATION_TEST_HARNESS_STATE_READY_TO_START             = 3,
    APPLICATION_TEST_HARNESS_STATE_EMITTING_RESULTS           = 4,
    APPLICATION_TEST_HARNESS_STATE_COMPLETE                   = 5,
    APPLICATION_TEST_HARNESS_STATE_UPLOAD_INVALID             = 6
} APPLICATION_TEST_HARNESS_State_T;

typedef struct
{
    uint32_t codec_initialized;
    uint32_t initialization_status;
    uint32_t application_messages_received;
    uint32_t decode_failures;
    uint32_t semantic_rejections;
    uint32_t encode_failures;
    uint32_t configurations_accepted;
    uint32_t instructions_accepted;
    uint32_t results_encoded;
    uint32_t state;
    uint32_t next_expected_tick;
    uint32_t active_expected_tick_count;
    uint32_t last_application_status;
    uint32_t last_decoded_message_type;
    uint32_t configuration_digest;
    uint32_t instruction_digest;
    uint32_t selected_instruction_family;
    uint32_t selected_result_family;
    uint32_t completed_instruction_ticks;
    uint32_t current_chunk_count;
    uint32_t maximum_chunk_count;
    uint32_t finalization_requests;
    uint32_t accepted_finalizations;
    uint32_t variable_operations_accepted;
    uint32_t result_messages_emitted;
    uint32_t result_records_emitted;
    uint32_t capture_overflow_events;
    uint32_t i2c_not_implemented_rejections;
    uint32_t maximum_decode_storage_required;
    uint32_t decode_storage_used;
    uint32_t selected_test_profile;
    uint32_t selected_fault_mode;
    uint32_t spontaneous_output_pending;
} APPLICATION_TEST_HARNESS_Diagnostics_T;

/** Initialize the shared Application codec with the hardware-test profile. */
HIL_Application_Status_T APPLICATION_TEST_HARNESS_Init( void );

/**
 * Clear the active test transaction while preserving protocol confirmation and
 * cumulative diagnostics.
 */
void APPLICATION_TEST_HARNESS_Reset_Transaction( void );

/** Clear the active test and require protocol discovery for a new session. */
void APPLICATION_TEST_HARNESS_Reset_Session( void );

/**
 * Decode and process one non-HRTP Transport Application payload.
 *
 * A System Information Request always publishes a local System Information
 * Response and confirms the exact protocol triplet only when it matches. Until
 * then, ordinary Application messages are rejected with VERSION_MISMATCH.
 *
 * Structurally valid requests publish correlated Application Responses for
 * acceptance or semantic rejection. Results are emitted through Poll_Output
 * after START; malformed or codec-invalid messages publish no output.
 */
HIL_Application_Status_T APPLICATION_TEST_HARNESS_Handle_Message( const uint8_t* message,
                                                                  size_t         message_size,
                                                                  uint8_t*       response,
                                                                  size_t         response_capacity,
                                                                  size_t*        response_size );

/**
 * Encode the next synthetic result without consuming it.
 *
 * The caller submits the returned opaque message through Transport and calls
 * APPLICATION_TEST_HARNESS_Commit_Output only after Transport accepts it.
 */
HIL_Application_Status_T APPLICATION_TEST_HARNESS_Poll_Output( uint8_t* output,
                                                               size_t output_capacity,
                                                               size_t* output_size );

/** Commit one result previously returned by APPLICATION_TEST_HARNESS_Poll_Output. */
HIL_Application_Status_T APPLICATION_TEST_HARNESS_Commit_Output( void );

/** Return debugger-visible cumulative Application harness diagnostics. */
const APPLICATION_TEST_HARNESS_Diagnostics_T* APPLICATION_TEST_HARNESS_Get_Diagnostics( void );

#ifdef TEST_BUILD
/** Reset all static harness state for isolated host tests. */
void APPLICATION_TEST_HARNESS_Test_Reset( void );

/** Initialize with a caller-supplied codec profile to exercise failure reporting. */
HIL_Application_Status_T
APPLICATION_TEST_HARNESS_Test_Init_With_Config( const HIL_Application_Config_T* config );

/** Return the static decode-storage address for alignment verification. */
uintptr_t APPLICATION_TEST_HARNESS_Test_Decode_Storage_Address( void );
#endif

#ifdef __cplusplus
}
#endif

#endif /* APPLICATION_TEST_HARNESS_H */
