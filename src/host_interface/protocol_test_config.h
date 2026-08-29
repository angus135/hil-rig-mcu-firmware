/******************************************************************************
 *  File:       protocol_test_config.h
 *  Author:     OpenAI
 *  Created:    30-Aug-2026
 *
 *  Description:
 *      Configuration for the temporary DEV-138 Transport hardware-test harness.
 *
 *  Notes:
 *      This file is intentionally test-branch specific. It is not an Application
 *      layer configuration and is not intended to merge into main unchanged.
 ******************************************************************************/

#ifndef PROTOCOL_TEST_CONFIG_H
#define PROTOCOL_TEST_CONFIG_H

#include "hil_rig_protocol/transport/transport_types.h"

/** Service cadence for the owning HOST_INTERFACE_Task. */
#define HOST_TRANSPORT_SERVICE_PERIOD_MS 1U

/** Use the pinned protocol's normal complete-message capacity for hardware testing. */
#define HOST_TRANSPORT_MAX_APPLICATION_MESSAGE_SIZE                                                \
    HIL_TRANSPORT_DEFAULT_MAX_APPLICATION_MESSAGE_SIZE

/** Use the pinned protocol's normal maximum encoded output capacity. */
#define HOST_TRANSPORT_MAX_ENCODED_FRAME_SIZE HIL_TRANSPORT_DEFAULT_MAX_ENCODED_FRAME_SIZE

/** MVP does not implement peer-liveness expiry. */
#define HOST_TRANSPORT_CONNECTION_TIMEOUT_MS 0U

/** Retry committed reliable traffic after this interval during hardware testing. */
#define HOST_TRANSPORT_RETRANSMIT_TIMEOUT_MS 100U

/** Retransmissions permitted after the initial committed reliable transmission. */
#define HOST_TRANSPORT_MAX_RETRIES 5U

/** RIG endpoints adopt the HOST session identity and therefore use INVALID seed. */
#define HOST_TRANSPORT_SESSION_SEED HIL_TRANSPORT_SESSION_SEED_INVALID

/** First local reliable sequence for a newly established RIG session. */
#define HOST_TRANSPORT_INITIAL_RELIABLE_SEQUENCE 0U

/**
 * Static workspace reserved for Transport. The selected configuration requires
 * 3289 bytes at protocol commit a24fccc403007cbf6268ff7d0d21f50566a6b2de.
 * The runtime sizing check remains authoritative and fails initialization if a
 * later protocol revision exceeds this capacity.
 */
#define HOST_TRANSPORT_WORKSPACE_CAPACITY_BYTES 4096U

/** Caller-owned unconsumed USB receive bytes retained across service iterations. */
#define HOST_TRANSPORT_PENDING_RX_CAPACITY_BYTES 1024U

/** Maximum bytes read from the USB receive stream in one operation. */
#define HOST_TRANSPORT_USB_READ_CHUNK_BYTES 256U

/** Recent Transport events retained only for debugger diagnostics. */
#define HOST_TRANSPORT_RECENT_EVENT_COUNT 8U

/** Bounded work limits for one 1 ms service iteration. */
#define HOST_TRANSPORT_MAX_RECEIVE_OPERATIONS_PER_SERVICE 4U
#define HOST_TRANSPORT_MAX_PROCESS_OPERATIONS_PER_SERVICE 2U
#define HOST_TRANSPORT_MAX_OUTPUT_OPERATIONS_PER_SERVICE 4U
#define HOST_TRANSPORT_MAX_EVENT_OPERATIONS_PER_SERVICE 8U
#define HOST_TRANSPORT_MAX_ZERO_RECEIVE_OPERATIONS_PER_SERVICE 2U

#endif /* PROTOCOL_TEST_CONFIG_H */
