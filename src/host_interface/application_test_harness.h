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
#define APPLICATION_TEST_HARNESS_COMPATIBILITY_PROFILE_ID UINT32_C( 0x41505031 )

typedef enum
{
    APPLICATION_TEST_HARNESS_STATE_UNINITIALIZED             = 0,
    APPLICATION_TEST_HARNESS_STATE_WAITING_FOR_CONFIGURATION = 1,
    APPLICATION_TEST_HARNESS_STATE_ACCEPTING_INSTRUCTIONS    = 2,
    APPLICATION_TEST_HARNESS_STATE_COMPLETE                  = 3
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
} APPLICATION_TEST_HARNESS_Diagnostics_T;

/** Initialize the shared Application codec with the hardware-test profile. */
HIL_Application_Status_T APPLICATION_TEST_HARNESS_Init( void );

/**
 * Clear only the active test transaction while preserving codec initialization
 * and cumulative diagnostics.
 */
void APPLICATION_TEST_HARNESS_Reset_Transaction( void );

/**
 * Decode and process one non-HRTP Transport Application payload.
 *
 * A successful Test Configuration is consumed with response_size == 0. A
 * successful Test Instruction publishes exactly one encoded Test Result into
 * response. Decode or semantic failures publish no output.
 */
HIL_Application_Status_T APPLICATION_TEST_HARNESS_Handle_Message( const uint8_t* message,
                                                                  size_t         message_size,
                                                                  uint8_t*       response,
                                                                  size_t         response_capacity,
                                                                  size_t*        response_size );

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
