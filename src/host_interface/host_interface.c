/******************************************************************************
 *  File:       host_interface.c
 *  Author:     Tim Vogelsang
 *  Created:    6-Sep-2026
 *
 *  Description:
 *      Runs host transport processing from task context.
 *
 *  Notes:
 *      Instruction upload must use the Flash Manager public lifecycle. The
 *      current periodic task owns Transport and Application codec plumbing;
 *      application semantics are dispatched separately by this task.
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "hil_rig_protocol/application/application.h"
#include "hil_rig_protocol/transport/transport.h"
#include "hw_usb.h"
#include "host_interface.h"
#include "rtos_config.h"
#include "host_process_message.h"

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */

/** Period between Host Interface task iterations. */
#define HOST_INTERFACE_PERIOD_MS ( 1U )

/** Capacity of the task-owned staging buffer used to pull bytes from USB. */
#define HOST_INTERFACE_USB_RECEIVE_CAPACITY ( 512U )

/** Capacity of one complete encoded Application message. */
#define HOST_INTERFACE_APPLICATION_MESSAGE_CAPACITY ( HIL_APPLICATION_DEFAULT_MAX_MESSAGE_SIZE )

/**
 * Maximum decode-storage capacity reserved for variable-length Application data.
 *
 * The Application codec requires this storage to be aligned for the typed data
 * that variable-length message fields may reference after decoding.
 */
#define HOST_INTERFACE_APPLICATION_DECODE_CAPACITY                                                 \
    ( HIL_APPLICATION_ABSOLUTE_MAX_VARIABLE_DATA_SIZE )

/** Capacity reserved for the caller-owned Transport workspace. */
#define HOST_INTERFACE_TRANSPORT_WORKSPACE_CAPACITY ( 4096U )

/** Capacity of the task-owned buffer used to copy one Transport output frame. */
#define HOST_INTERFACE_TRANSPORT_OUTPUT_CAPACITY ( HIL_TRANSPORT_DEFAULT_MAX_ENCODED_FRAME_SIZE )

/** Transport reliable-delivery timeout configured for the Host Interface. */
#define HOST_INTERFACE_TRANSPORT_RETRANSMIT_TIMEOUT_MS ( 100U )

/** Maximum number of Transport reliable-delivery retries. */
#define HOST_INTERFACE_TRANSPORT_MAX_RETRIES ( 5U )
#define HOST_INTERFACE_OUTGOING_VARIABLE_DATA_SIZE 255

#ifndef TEST_BUILD
#include "main.h"
#define HOST_INTERFACE_Error_Handler() Error_Handler()
#else
#define HOST_INTERFACE_Error_Handler()
#endif

/**-----------------------------------------------------------------------------
 *  Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

/**
 * @brief Host Interface USB receive staging state.
 *
 * @details
 * USB reception is deliberately separated from Transport reception. The USB
 * driver fills a byte span into receive_buffer, while the Host Interface may
 * offer only the unconsumed suffix of that span to HIL_TRANSPORT_Receive_Bytes.
 * This permits Transport to apply byte-stream backpressure without losing the
 * exact suffix that still belongs to the caller.
 *
 * receive_count is the number of valid bytes currently staged. receive_offset
 * is the next byte that has not yet been accepted by Transport. When the two
 * values are equal, the staging buffer is empty and the next protocol cycle
 * may pull another USB chunk.
 */
typedef struct
{
    /** Number of valid bytes currently stored in receive_buffer. */
    uint32_t receive_count;

    /** Offset of the first byte not yet accepted by Transport. */
    uint32_t receive_offset;

    /** Task-owned USB byte staging buffer. */
    uint8_t receive_buffer[HOST_INTERFACE_USB_RECEIVE_CAPACITY];
} HOST_INTERFACE_USB_State_T;

/**
 * @brief Host Interface Application codec state and caller-owned buffers.
 *
 * @details
 * The Application context and configuration describe the stateless codec. The
 * send and receive byte spans bridge the typed Application API and the opaque
 * Transport API. receive_data is separately aligned storage for variable data
 * referenced by a successfully decoded message.
 *
 * The decoded message and any spans inside it are valid only while the caller
 * retains ownership of this state and before a later processing cycle reuses
 * the receive storage. The Transport message is consumed before Application
 * decoding begins; Application-level decode failure therefore does not return
 * the opaque Transport message to the Transport layer.
 */
typedef struct
{
    /** Initialized Application codec context. */
    HIL_Application_Context_T context;

    /** Application codec policy copied during initialization. */
    HIL_Application_Config_T config;

    /** Encoded bytes prepared for one outgoing Application message. */
    uint8_t send_byte_span[HOST_INTERFACE_APPLICATION_MESSAGE_CAPACITY];

    /** Number of valid bytes currently in send_byte_span. */
    size_t used_send_byte_span_size;

    /** Encoded bytes copied from one incoming Transport message. */
    uint8_t receive_byte_span[HOST_INTERFACE_APPLICATION_MESSAGE_CAPACITY];

    /** Number of valid bytes currently in receive_byte_span. */
    size_t used_receive_byte_span_size;

    /**
     * Aligned storage referenced by variable-length fields in a decoded message.
     */
    _Alignas( HIL_APPLICATION_DECODE_STORAGE_ALIGNMENT ) uint8_t
        receive_data[HOST_INTERFACE_APPLICATION_DECODE_CAPACITY];

    /** Number of bytes currently used in receive_data by the decoded message. */
    size_t used_receive_data_size;

    /** Status from the most recent Application codec operation. */
    HIL_Application_Status_T status;
} HOST_INTERFACE_Application_State_T;

/**
 * @brief Host Interface Transport context, policy, workspace, and I/O state.
 *
 * @details
 * This structure owns every object needed to use the caller-owned Transport
 * API from the Host Interface task. Transport retains protocol/session state in
 * context and uses workspace as its bounded private storage. output_buffer is
 * only a temporary copy of the complete frame returned by Peek_Output; the
 * frame remains Transport-owned until Commit_Output succeeds.
 *
 * output_acceptance_pending_commit records the special case where USB accepted
 * a frame immediately before entering configured suspension. The frame must be
 * committed on a later active cycle so Transport retry timing reflects actual
 * USB acceptance rather than merely a successful peek.
 */
typedef struct
{
    /** Initialized Transport context. */
    HIL_Transport_Context_T context;

    /** Transport policy used to size and initialize context. */
    HIL_Transport_Config_T config;

    /** Endpoint role assigned to this Host Interface instance. */
    HIL_Transport_Role_T role;

    /** Caller-owned workspace descriptor supplied to Transport. */
    HIL_Transport_Storage_T storage;

    /** Aligned bounded workspace used internally by Transport. */
    _Alignas( HIL_TRANSPORT_WORKSPACE_ALIGNMENT )
        uint8_t workspace[HOST_INTERFACE_TRANSPORT_WORKSPACE_CAPACITY];

    /** Required workspace size reported by Transport during initialization. */
    size_t required_workspace_size;

    /** Complete Transport output copied for the USB driver. */
    uint8_t output_buffer[HOST_INTERFACE_TRANSPORT_OUTPUT_CAPACITY];

    /** Number of valid bytes currently stored in output_buffer. */
    size_t used_output_buffer_size;

    /** Status from the most recent Transport operation. */
    HIL_Transport_Status_T status;

    /** Temporary storage for one Transport event while draining events. */
    HIL_Transport_Event_T event;

    /**
     * True when USB accepted a peeked output but the corresponding Transport
     * commit must be deferred until the link is active again.
     */
    bool output_acceptance_pending_commit;
} HOST_INTERFACE_Transport_State_T;

/**
 * @brief Monotonic Transport clock maintained around USB suspension.
 *
 * @details
 * Transport receives caller-supplied time rather than reading hardware time
 * itself. effective_time_ms advances by unsigned RTOS tick deltas during normal
 * operation. Configured USB suspension freezes that logical clock. The first
 * active cycle after suspension only clears the suspension marker, preventing
 * suspended wall-clock time from becoming a retry deadline jump.
 */
typedef struct
{
    /** RTOS tick value observed during the previous protocol cycle. */
    TickType_t last_ticks;

    /** Logical time supplied to Transport after suspension handling. */
    uint32_t effective_time_ms;

    /** True after at least one cycle observed configured suspension. */
    bool configured_suspension_observed;
} HOST_INTERFACE_Transport_Clock_T;

/**
 * @brief Complete task-owned Host Interface protocol state.
 *
 * @details
 * The protocol state groups the independent USB, Application, Transport, and
 * clock lifetimes so that one task remains the single owner of all protocol
 * API calls. observed_link_state prevents repeated link notifications, while
 * link_state_observed distinguishes the initial observation from a repeated
 * observation of the same physical state.
 */
typedef struct
{
    /** USB byte-stream staging state. */
    HOST_INTERFACE_USB_State_T usb;

    /** Application codec state and message buffers. */
    HOST_INTERFACE_Application_State_T application;

    /** Transport context, workspace, and output/event state. */
    HOST_INTERFACE_Transport_State_T transport;

    /** Logical Transport clock state. */
    HOST_INTERFACE_Transport_Clock_T transport_clock;

    /** Initial task tick used by vTaskDelayUntil for periodic scheduling. */
    TickType_t initial_ticks;

    /** Most recently reported physical-link state. */
    HIL_Transport_Link_State_T observed_link_state;

    /** True after the first physical-link observation has been processed. */
    bool link_state_observed;
} HOST_INTERFACE_Protocol_State_T;

/**-----------------------------------------------------------------------------
 *  Public (global) and Extern Variables
 *------------------------------------------------------------------------------
 */

TaskHandle_t HostInterfaceTaskHandle = NULL;  // NOLINT(readability-identifier-naming)

/**-----------------------------------------------------------------------------
 *  Private (static) Function Prototypes
 *------------------------------------------------------------------------------
 */

static void HOST_INTERFACE_Protocol_Process( HOST_INTERFACE_Protocol_State_T* protocol_state,
                                             const HIL_Application_Message_T* outgoing_message,
                                             bool* outgoing_message_accepted,
                                             bool  can_consume_incoming_message,
                                             HIL_Application_Message_T* incoming_message,
                                             bool* incoming_message_available );
static void HOST_INTERFACE_Protocol_Init( HOST_INTERFACE_Protocol_State_T* protocol_state );
static void
HOST_INTERFACE_Protocol_Update_Link_State( HOST_INTERFACE_Protocol_State_T* protocol_state,
                                           HIL_Transport_Link_State_T       observed_link_state,
                                           uint32_t                         now );
static uint32_t
HOST_INTERFACE_Transport_Clock_Update( HOST_INTERFACE_Transport_Clock_T* transport_clock,
                                       TickType_t current_ticks, bool configured_suspended );

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

/**
 * @brief Convert RTOS ticks into the logical time used by Transport.
 *
 * @details
 * The Host Interface owns the clock passed to the caller-driven Transport API.
 * TickType_t subtraction is intentionally unsigned so normal RTOS tick-wrap
 * arithmetic remains valid. While USB is configured-suspended, the logical
 * clock is held constant even though the RTOS tick count continues to advance.
 * The first active cycle after suspension also returns the frozen time and only
 * clears the suspension marker; this prevents a long USB suspension from
 * immediately expiring a Transport retry deadline.
 *
 * @param[in,out] transport_clock Clock state to update. It must have been
 *                                zero-initialized or initialized by the Host
 *                                Interface protocol initializer.
 * @param[in] current_ticks       Current RTOS tick count.
 * @param[in] configured_suspended True when USB is configured but suspended.
 * @return Logical monotonic time in the tick-based units expected by
 *         Transport.
 */
static uint32_t
HOST_INTERFACE_Transport_Clock_Update( HOST_INTERFACE_Transport_Clock_T* const transport_clock,
                                       const TickType_t                        current_ticks,
                                       const bool configured_suspended )
{
    // Unsigned subtraction deliberately preserves elapsed time across the
    // natural wrap of the RTOS tick counter.
    const TickType_t elapsed_ticks = current_ticks - transport_clock->last_ticks;

    transport_clock->last_ticks = current_ticks;

    if ( configured_suspended )
    {
        // Remember that suspension has been observed so the first active cycle
        // can resume from the frozen logical time without charging the pause.
        transport_clock->configured_suspension_observed = true;
        return transport_clock->effective_time_ms;
    }

    if ( transport_clock->configured_suspension_observed )
    {
        // Discard the elapsed wall-clock interval accumulated during USB
        // suspension. Normal elapsed-time accumulation resumes next cycle.
        transport_clock->configured_suspension_observed = false;
        return transport_clock->effective_time_ms;
    }

    // Transport sees only active-link time, not time spent in configured
    // suspension.
    transport_clock->effective_time_ms += elapsed_ticks;
    return transport_clock->effective_time_ms;
}

/**
 * @brief Apply a newly observed physical-link state to Host Interface state.
 *
 * @details
 * Repeated observations of the same state are ignored so Transport does not
 * receive duplicate link notifications. A transition to disconnected first
 * abandons task-owned USB receive/output data from the old physical link, then
 * forwards the transition to Transport. This ordering prevents bytes or a
 * previously peeked output frame from crossing into a later session.
 *
 * Transport capacity exhaustion is intentionally tolerated here because a link
 * transition may complete while its diagnostic event is temporarily unable to
 * enter the bounded event FIFO. Invalid arguments and internal failures invoke
 * the platform error handler.
 *
 * @param[in,out] protocol_state Complete Host Interface state to update.
 * @param[in] observed_link_state Current physical-link state reported to
 *                                Transport.
 * @param[in] now                 Logical Transport time for this transition.
 */
static void
HOST_INTERFACE_Protocol_Update_Link_State( HOST_INTERFACE_Protocol_State_T* const protocol_state,
                                           const HIL_Transport_Link_State_T observed_link_state,
                                           const uint32_t                   now )
{
    // Link notifications are edge-triggered at this layer. The initial
    // observation is always forwarded, including an initial disconnected state.
    if ( ( protocol_state->link_state_observed == true )
         && ( protocol_state->observed_link_state == observed_link_state ) )
    {
        return;
    }

    if ( observed_link_state == HIL_TRANSPORT_LINK_STATE_DISCONNECTED )
    {
        // Do not offer bytes retained from an abandoned physical link to a new
        // session. The staged Transport suffix and any USB transmit ownership
        // belong to the old physical connection.
        protocol_state->usb.receive_count                          = 0U;
        protocol_state->usb.receive_offset                         = 0U;
        protocol_state->transport.output_acceptance_pending_commit = false;
        HW_USB_Discard_Transmit_Data();

        // Drain bytes already queued by the USB driver. They were received
        // before the disconnect observation and must not be interpreted after
        // reconnection as part of a new Transport session.
        while ( HW_USB_Get_Receive_Stream_Used_Bytes() > 0U )
        {
            if ( HW_USB_Receive( protocol_state->usb.receive_buffer,
                                 sizeof( protocol_state->usb.receive_buffer ) )
                 == 0U )
            {
                break;
            }
        }
    }

    // Transport owns session-scoped parser and reliability cleanup. The Host
    // Interface only supplies the observation and caller-owned logical time.
    protocol_state->transport.status = HIL_TRANSPORT_Notify_Link_State(
        &protocol_state->transport.context, observed_link_state, now );
    protocol_state->observed_link_state = observed_link_state;
    protocol_state->link_state_observed = true;

    if ( protocol_state->transport.status == HIL_TRANSPORT_STATUS_INTERNAL_ERROR
         || protocol_state->transport.status == HIL_TRANSPORT_STATUS_INVALID_ARGUMENT )
    {
        HOST_INTERFACE_Error_Handler();
    }
}

/**
 * @brief Execute one complete Host Interface protocol service cycle.
 *
 * @details
 * This function is the single task-context integration point between USB, the
 * caller-driven Transport layer, and the Application codec. One cycle performs
 * the following work in order:
 *
 * 1. Convert the current RTOS tick into logical Transport time.
 * 2. Observe USB link/suspension state and perform disconnect cleanup.
 * 3. Commit any USB output whose acceptance was deferred by suspension.
 * 4. Run USB monitoring and drain all queued Transport events.
 * 5. Advance Transport before receiving bytes so timers and pending recovery
 *    work are serviced on every active cycle.
 * 6. Offer the currently staged USB byte prefix to Transport and retain any
 *    unconsumed suffix for a later cycle.
 * 7. Run Transport again so a complete frame or retained parser body can make
 *    progress immediately after byte delivery.
 * 8. Optionally read and decode one complete Application message.
 * 9. Optionally encode and submit one outgoing Application message.
 * 10. Peek, physically transmit, and commit all currently available Transport
 *     output while the USB link remains active.
 *
 * The @p can_consume_incoming_message parameter gates only step 8. When it is
 * false, USB bytes are still offered to Transport and Transport continues to
 * assemble protocol frames, generate acknowledgements, run retries, and expose
 * output. The unread Transport Application-message slot is therefore allowed
 * to provide backpressure naturally. A message delivered by an earlier call is
 * the caller's responsibility; this function does not preserve, queue, or
 * otherwise manage a previously returned caller-owned message on its behalf.
 *
 * The output Application message is borrowed for this call. It is submitted
 * only after successful Application encoding, and outgoing_message_accepted is
 * set only when Transport has copied and accepted the encoded bytes. The
 * incoming message is written only after Transport has copied one complete
 * message and Application decoding succeeds. Variable-length spans in that
 * message point into protocol_state-owned receive_data and must be consumed or
 * copied before a later active processing cycle reuses that storage.
 *
 * Fatal argument/invariant statuses are routed to HOST_INTERFACE_Error_Handler.
 * Retryable statuses such as NOT_READY and CAPACITY_EXHAUSTED remain owned by
 * the normal caller-driven retry flow and are not treated as fatal here.
 *
 * @param[in,out] protocol_state               Complete task-owned protocol state.
 * @param[in] outgoing_message                 Optional Application message to
 *                                             encode and submit; NULL means no
 *                                             outgoing message is pending.
 * @param[out] outgoing_message_accepted       Set true only when the outgoing
 *                                             message was accepted by Transport.
 * @param[in] can_consume_incoming_message     True when this call may consume
 *                                             one newly assembled Transport
 *                                             Application message; false keeps
 *                                             that Transport message unread.
 * @param[out] incoming_message                Destination for one decoded
 *                                             Application message. It is zeroed
 *                                             at the beginning of every call.
 * @param[out] incoming_message_available      Set true only when
 *                                             incoming_message contains a newly
 *                                             decoded message.
 */
static void HOST_INTERFACE_Protocol_Process(
    HOST_INTERFACE_Protocol_State_T* const protocol_state,
    const HIL_Application_Message_T* const outgoing_message, bool* const outgoing_message_accepted,
    const bool can_consume_incoming_message, HIL_Application_Message_T* const incoming_message,
    bool* const incoming_message_available )
{
    const HW_USB_Connection_State_T usb_connection_state = HW_USB_Get_Connection_State();
    const bool                      configured_suspended =
        usb_connection_state == HW_USB_CONNECTION_STATE_CONFIGURED_SUSPENDED;
    const uint32_t now = HOST_INTERFACE_Transport_Clock_Update(
        &protocol_state->transport_clock, xTaskGetTickCount(), configured_suspended );
    HIL_Transport_Link_State_T observed_link_state;

    // Outputs describe this cycle only. A false incoming availability result
    // does not transfer ownership of any prior message to this function; the
    // caller owns any such prior message itself.
    *outgoing_message_accepted  = false;
    *incoming_message_available = false;
    *incoming_message           = ( HIL_Application_Message_T ){ 0 };

    // =======------- SERVICE USB AND LINK STATE
    // USB suspension is handled before protocol work. Transport time is still
    // updated through the clock helper, but no parser, codec, or USB I/O
    // progress occurs while the device is configured-suspended.
    observed_link_state = ( usb_connection_state != HW_USB_CONNECTION_STATE_DISCONNECTED )
                              ? HIL_TRANSPORT_LINK_STATE_CONNECTED
                              : HIL_TRANSPORT_LINK_STATE_DISCONNECTED;

    HOST_INTERFACE_Protocol_Update_Link_State( protocol_state, observed_link_state, now );

    if ( configured_suspended )
    {
        return;
    }

    // The Application decoder uses state-owned storage for any variable spans.
    // Clearing the usage count marks the previous decoded spans as no longer
    // owned by the next decode operation; callers must have consumed or copied
    // the previous message before allowing another active cycle.
    protocol_state->application.used_receive_data_size = 0U;

    if ( protocol_state->transport.output_acceptance_pending_commit )
    {
        // USB accepted a frame immediately before suspension, so the prior
        // cycle could not safely commit it. Commit it now before new Transport
        // work can select another output item or advance reliability timing.
        protocol_state->transport.status =
            HIL_TRANSPORT_Commit_Output( &protocol_state->transport.context, now );
        if ( protocol_state->transport.status != HIL_TRANSPORT_STATUS_OK )
        {
            HOST_INTERFACE_Error_Handler();
        }
        protocol_state->transport.output_acceptance_pending_commit = false;
    }

    // Allow the USB abstraction to process low-level receive/transmit state
    // before the Host Interface inspects the Transport queues.
    HW_USB_Monitor_Process();

    // =======------- DRAIN TRANSPORT EVENTS
    // Event draining is mandatory service work. Transport uses a bounded FIFO;
    // leaving events unread can prevent later state transitions from publishing
    // their required diagnostics. Event contents are consumed by this
    // integration layer and are not exposed as Application messages.
    while ( true )
    {
        protocol_state->transport.status = HIL_TRANSPORT_Read_Event(
            &protocol_state->transport.context, &protocol_state->transport.event );
        if ( protocol_state->transport.status == HIL_TRANSPORT_STATUS_NOT_READY )
        {
            break;
        }
        if ( protocol_state->transport.status != HIL_TRANSPORT_STATUS_OK )
        {
            HOST_INTERFACE_Error_Handler();
        }
    }

    // =======------- ADVANCE TRANSPORT
    // Progress handshake publication, retry deadlines, and pending recovery
    // before supplying fresh USB bytes. CAPACITY_EXHAUSTED is retryable and is
    // intentionally not treated as a Host Interface fatal error.
    protocol_state->transport.status = HIL_TRANSPORT_Process(
        &protocol_state->transport.context, now, HIL_TRANSPORT_OPERATING_MODE_NORMAL );
    if ( protocol_state->transport.status == HIL_TRANSPORT_STATUS_INTERNAL_ERROR
         || protocol_state->transport.status == HIL_TRANSPORT_STATUS_INVALID_ARGUMENT )
    {
        HOST_INTERFACE_Error_Handler();
    }

    // =======------- RECEIVE USB BYTE STREAM
    if ( observed_link_state == HIL_TRANSPORT_LINK_STATE_CONNECTED )
    {
        if ( protocol_state->usb.receive_offset == protocol_state->usb.receive_count )
        {
            // Pull a new USB chunk only after the previous chunk has been fully
            // accepted by Transport. HW_USB_Receive transfers ownership into
            // this task-owned staging buffer.
            protocol_state->usb.receive_count = HW_USB_Receive(
                protocol_state->usb.receive_buffer, sizeof( protocol_state->usb.receive_buffer ) );
            protocol_state->usb.receive_offset = 0U;
        }

        if ( protocol_state->usb.receive_offset < protocol_state->usb.receive_count )
        {
            const size_t bytes_available = ( size_t )( protocol_state->usb.receive_count
                                                       - protocol_state->usb.receive_offset );
            size_t       bytes_consumed  = 0U;

            // Transport may accept only a prefix when its parser or bounded
            // ownership is temporarily blocked. Advance by exactly the
            // reported prefix and retry the untouched suffix later.
            protocol_state->transport.status = HIL_TRANSPORT_Receive_Bytes(
                &protocol_state->transport.context,
                &protocol_state->usb.receive_buffer[protocol_state->usb.receive_offset],
                bytes_available, &bytes_consumed );

            if ( bytes_consumed > bytes_available
                 || ( ( protocol_state->transport.status == HIL_TRANSPORT_STATUS_OK )
                      && ( bytes_consumed == 0U ) ) )
            {
                HOST_INTERFACE_Error_Handler();
            }

            // A complete USB chunk is released only when Transport accepts its
            // entire staged suffix. CAPACITY_EXHAUSTED therefore preserves both
            // the Transport-retained body and any USB-owned suffix.
            protocol_state->usb.receive_offset += ( uint32_t )bytes_consumed;
            if ( protocol_state->usb.receive_offset == protocol_state->usb.receive_count )
            {
                protocol_state->usb.receive_count  = 0U;
                protocol_state->usb.receive_offset = 0U;
            }

            if ( protocol_state->transport.status == HIL_TRANSPORT_STATUS_INTERNAL_ERROR
                 || protocol_state->transport.status == HIL_TRANSPORT_STATUS_INVALID_ARGUMENT )
            {
                HOST_INTERFACE_Error_Handler();
            }
        }
    }

    // Retry retained parser work after event draining and byte delivery. This
    // progress point is important when Receive_Bytes retained a complete body
    // because the unread Application slot or output capacity was unavailable.
    protocol_state->transport.status = HIL_TRANSPORT_Process(
        &protocol_state->transport.context, now, HIL_TRANSPORT_OPERATING_MODE_NORMAL );
    if ( protocol_state->transport.status == HIL_TRANSPORT_STATUS_INTERNAL_ERROR
         || protocol_state->transport.status == HIL_TRANSPORT_STATUS_INVALID_ARGUMENT )
    {
        HOST_INTERFACE_Error_Handler();
    }

    // =======------- RECEIVE ONE APPLICATION MESSAGE
    // Keep servicing USB and Transport even when the caller cannot consume an
    // assembled Application message. The Transport retains that message until
    // Read_Application_Data() is called on a later cycle.
    if ( can_consume_incoming_message )
    {
        // Read_Application_Data copies and consumes exactly one complete opaque
        // Transport message. It is deliberately called only after receive
        // servicing so a newly completed frame can be delivered without adding
        // a second Application queue in this layer.
        protocol_state->application.used_receive_byte_span_size = 0U;
        protocol_state->transport.status = HIL_TRANSPORT_Read_Application_Data(
            &protocol_state->transport.context, protocol_state->application.receive_byte_span,
            sizeof( protocol_state->application.receive_byte_span ),
            &protocol_state->application.used_receive_byte_span_size );

        if ( protocol_state->transport.status == HIL_TRANSPORT_STATUS_OK )
        {
            size_t required_decode_capacity = 0U;

            // Determine the decode-storage requirement without publishing typed
            // output. The aligned receive_data bound is checked before the
            // full decoder writes variable-length spans.
            protocol_state->application.status = HIL_APPLICATION_Decode_Storage_Size(
                &protocol_state->application.context, protocol_state->application.receive_byte_span,
                protocol_state->application.used_receive_byte_span_size,
                &required_decode_capacity );

            if ( protocol_state->application.status == HIL_APPLICATION_STATUS_OK
                 && required_decode_capacity <= sizeof( protocol_state->application.receive_data ) )
            {
                // Decode only after a complete Transport message has been
                // copied and the storage requirement has been accepted.
                protocol_state->application.status = HIL_APPLICATION_Decode_Message(
                    &protocol_state->application.context,
                    protocol_state->application.receive_byte_span,
                    protocol_state->application.used_receive_byte_span_size, incoming_message,
                    protocol_state->application.receive_data,
                    sizeof( protocol_state->application.receive_data ),
                    &protocol_state->application.used_receive_data_size );

                if ( protocol_state->application.status == HIL_APPLICATION_STATUS_OK )
                {
                    // The caller now owns the decision to consume or copy this
                    // decoded message before the next active protocol cycle.
                    *incoming_message_available = true;
                }
            }

            if ( protocol_state->application.status == HIL_APPLICATION_STATUS_INTERNAL_ERROR
                 || ( protocol_state->application.status == HIL_APPLICATION_STATUS_OK
                      && required_decode_capacity
                             > sizeof( protocol_state->application.receive_data ) ) )
            {
                HOST_INTERFACE_Error_Handler();
            }
        }
        else if ( protocol_state->transport.status != HIL_TRANSPORT_STATUS_NOT_READY )
        {
            HOST_INTERFACE_Error_Handler();
        }
    }

    // =======------- SUBMIT ONE OUTGOING APPLICATION MESSAGE
    if ( outgoing_message != NULL )
    {
        // Encode into task-owned storage so Transport can synchronously copy the
        // complete Application bytes and retain its own reliable-delivery copy.
        protocol_state->application.used_send_byte_span_size = 0U;
        protocol_state->application.status =
            HIL_APPLICATION_Encode_Message( &protocol_state->application.context, outgoing_message,
                                            protocol_state->application.send_byte_span,
                                            sizeof( protocol_state->application.send_byte_span ),
                                            &protocol_state->application.used_send_byte_span_size );

        if ( protocol_state->application.status == HIL_APPLICATION_STATUS_OK )
        {
            protocol_state->transport.status = HIL_TRANSPORT_Submit_Application_Data(
                &protocol_state->transport.context, protocol_state->application.send_byte_span,
                protocol_state->application.used_send_byte_span_size );

            if ( protocol_state->transport.status == HIL_TRANSPORT_STATUS_OK )
            {
                // Clear the caller's pending outgoing message only after
                // Transport has accepted it. NOT_READY and CAPACITY_EXHAUSTED
                // leave it pending for an unchanged retry.
                *outgoing_message_accepted = true;
            }
            else if ( protocol_state->transport.status != HIL_TRANSPORT_STATUS_NOT_READY
                      && protocol_state->transport.status
                             != HIL_TRANSPORT_STATUS_CAPACITY_EXHAUSTED )
            {
                HOST_INTERFACE_Error_Handler();
            }
        }
        else if ( protocol_state->application.status == HIL_APPLICATION_STATUS_INTERNAL_ERROR
                  || protocol_state->application.status == HIL_APPLICATION_STATUS_BUFFER_TOO_SMALL )
        {
            HOST_INTERFACE_Error_Handler();
        }
    }

    // =======------- TRANSMIT TRANSPORT OUTPUT
    while ( true )
    {
        HW_USB_Connection_State_T current_connection_state = HW_USB_Get_Connection_State();

        if ( current_connection_state != HW_USB_CONNECTION_STATE_ACTIVE )
        {
            // A configured-suspended link cannot safely report physical output
            // acceptance. A disconnect additionally performs link cleanup so no
            // old output crosses into a new session.
            if ( current_connection_state == HW_USB_CONNECTION_STATE_DISCONNECTED )
            {
                HOST_INTERFACE_Protocol_Update_Link_State(
                    protocol_state, HIL_TRANSPORT_LINK_STATE_DISCONNECTED, now );
            }
            break;
        }

        protocol_state->transport.used_output_buffer_size = 0U;
        protocol_state->transport.status                  = HIL_TRANSPORT_Peek_Output(
            &protocol_state->transport.context, protocol_state->transport.output_buffer,
            sizeof( protocol_state->transport.output_buffer ),
            &protocol_state->transport.used_output_buffer_size );

        if ( protocol_state->transport.status == HIL_TRANSPORT_STATUS_NOT_READY )
        {
            break;
        }
        if ( protocol_state->transport.status != HIL_TRANSPORT_STATUS_OK
             || protocol_state->transport.used_output_buffer_size > UINT16_MAX )
        {
            HOST_INTERFACE_Error_Handler();
        }

        if ( !HW_USB_Transmit( protocol_state->transport.output_buffer,
                               ( uint16_t )protocol_state->transport.used_output_buffer_size ) )
        {
            // USB did not accept the frame. Leave the Transport peek
            // uncommitted so the exact same bytes can be retried next cycle.
            break;
        }

        current_connection_state = HW_USB_Get_Connection_State();
        if ( current_connection_state == HW_USB_CONNECTION_STATE_DISCONNECTED )
        {
            // The driver accepted the bytes, but the link disappeared before
            // this cycle could commit Transport ownership. Link cleanup handles
            // that abandoned session-scoped output.
            HOST_INTERFACE_Protocol_Update_Link_State( protocol_state,
                                                       HIL_TRANSPORT_LINK_STATE_DISCONNECTED, now );
            break;
        }

        if ( current_connection_state == HW_USB_CONNECTION_STATE_CONFIGURED_SUSPENDED )
        {
            // USB acceptance occurred, but suspension began before commit.
            // Preserve the pinned item and commit it on the next active cycle.
            protocol_state->transport.output_acceptance_pending_commit = true;
            break;
        }

        // In the ordinary active case, commit immediately so Transport starts
        // or advances reliable-delivery timing at actual USB acceptance.
        protocol_state->transport.status =
            HIL_TRANSPORT_Commit_Output( &protocol_state->transport.context, now );
        if ( protocol_state->transport.status != HIL_TRANSPORT_STATUS_OK )
        {
            HOST_INTERFACE_Error_Handler();
        }
    }
}

/**
 * @brief Initialize all Host Interface protocol layers and owned storage.
 *
 * @details
 * Initialization clears the aggregate state, initializes USB and the
 * Application codec, obtains the configured Transport workspace requirement,
 * binds the statically allocated workspace, and initializes Transport in the
 * RIG role used by the firmware Host Interface. The initial RTOS tick is saved
 * for both logical Transport time and periodic task scheduling.
 *
 * Every initialization failure is routed to HOST_INTERFACE_Error_Handler. In
 * production this is expected to be a terminating platform error path; tests
 * replace it with a no-op and inspect the resulting state separately.
 *
 * @param[out] protocol_state Aggregate state to clear and initialize. The
 *                            pointer must not be NULL and must identify the
 *                            task-owned protocol state object.
 */
static void HOST_INTERFACE_Protocol_Init( HOST_INTERFACE_Protocol_State_T* const protocol_state )
{
    // Clear stale parser, codec, USB, clock, and output-ownership state before
    // initializing each layer. This also gives all optional state deterministic
    // zero values before the public APIs populate their contexts.
    *protocol_state = ( HOST_INTERFACE_Protocol_State_T ){ 0 };

    // =======------- INITIALISE USB INTERFACE
    // USB is initialized first because later link observations and output
    // servicing depend on a valid low-level connection abstraction.
    if ( !HW_USB_Init() )
    {
        HOST_INTERFACE_Error_Handler();
    }

    // =======------- INITIALISE APPLICATION LAYER
    // The codec owns policy and validation only; all message storage remains in
    // the enclosing Host Interface state and is supplied per operation.
    protocol_state->application.status =
        HIL_APPLICATION_Default_Config( &protocol_state->application.config );
    if ( protocol_state->application.status != HIL_APPLICATION_STATUS_OK )
    {
        HOST_INTERFACE_Error_Handler();
    }

    protocol_state->application.status = HIL_APPLICATION_Init(
        &protocol_state->application.context, &protocol_state->application.config );
    if ( protocol_state->application.status != HIL_APPLICATION_STATUS_OK )
    {
        HOST_INTERFACE_Error_Handler();
    }

    // =======------- INITIALISE TRANSPORT LAYER
    // Transport configuration is copied into task state so the same policy is
    // available for workspace sizing and actual initialization.
    HIL_TRANSPORT_Default_Config( &protocol_state->transport.config );
    protocol_state->transport.config.retransmit_timeout_ms =
        HOST_INTERFACE_TRANSPORT_RETRANSMIT_TIMEOUT_MS;
    protocol_state->transport.config.max_retries = HOST_INTERFACE_TRANSPORT_MAX_RETRIES;
    protocol_state->transport.role               = HIL_TRANSPORT_ROLE_RIG;

    // Query the exact workspace requirement before binding the fixed static
    // workspace. Rejecting an oversized requirement prevents Transport from
    // receiving a truncated storage region.
    protocol_state->transport.status = HIL_TRANSPORT_Required_Storage_Size(
        &protocol_state->transport.config, &protocol_state->transport.required_workspace_size );
    if ( protocol_state->transport.status != HIL_TRANSPORT_STATUS_OK
         || protocol_state->transport.required_workspace_size == 0U
         || protocol_state->transport.required_workspace_size
                > sizeof( protocol_state->transport.workspace ) )
    {
        HOST_INTERFACE_Error_Handler();
    }

    protocol_state->transport.storage.workspace = protocol_state->transport.workspace;
    protocol_state->transport.storage.workspace_size =
        sizeof( protocol_state->transport.workspace );

    // Transport receives caller-owned storage and retains no ownership of the
    // enclosing Host Interface object beyond this task's lifetime.
    protocol_state->transport.status =
        HIL_TRANSPORT_Init( &protocol_state->transport.context, protocol_state->transport.role,
                            &protocol_state->transport.config, &protocol_state->transport.storage );
    if ( protocol_state->transport.status != HIL_TRANSPORT_STATUS_OK )
    {
        HOST_INTERFACE_Error_Handler();
    }

    // Start both periodic scheduling and logical Transport time from the same
    // tick observation so the first cycle has no artificial elapsed interval.
    protocol_state->initial_ticks              = xTaskGetTickCount();
    protocol_state->transport_clock.last_ticks = protocol_state->initial_ticks;
}

/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */

/**
 * @brief Notify the Host interface to send some message
 *
 */
bool HOST_INTERFACE_Notify( uint32_t notification )
{
    if ( HostInterfaceTaskHandle == NULL )
    {
        return false;
    }

    return xTaskNotify( HostInterfaceTaskHandle, notification, eSetBits ) == pdPASS;
}

/**
 * @brief Host Interface Task
 *
 * @details
 * The FreeRTOS task that owns the Host Interface protocol state and runs the
 * periodic USB, Transport, and Application service loop. It is the single
 * caller of the private protocol integration function, so Transport contexts
 * and their caller-owned workspaces are never concurrently accessed by another
 * execution context.
 *
 * The task keeps an outgoing Application message pending until the protocol
 * reports that Transport copied and accepted it. Incoming Application messages
 * are handed to the future Application dispatch layer through the local output
 * object; that layer is responsible for consuming/copying each message before
 * a later cycle reuses codec storage.
 *
 * @param[in] task_parameters FreeRTOS task parameter. This implementation does
 *                            not use it.
 */
void HOST_INTERFACE_Task( void* task_parameters )
{
    static HOST_INTERFACE_Protocol_State_T protocol_state                      = { 0 };
    static HIL_Application_Message_T       outgoing_message                    = { 0 };
    static HIL_Application_Message_T       incoming_message                    = { 0 };
    bool                                   outgoing_message_pending            = false;
    uint8_t outgoing_variable_data[HOST_INTERFACE_OUTGOING_VARIABLE_DATA_SIZE] = { 0 };

    ( void )task_parameters;

    uint32_t notifications          = 0U;
    uint32_t carry_on_notifications = 0U;
    uint32_t expected_tick_count    = 0U;

    TickType_t overflow_timer = xTaskGetTickCount();

    bool can_consume_incoming = true;

    HIL_Application_Message_T overflow_outgoing_message = { 0 };
    HostInterfaceTaskHandle                             = xTaskGetCurrentTaskHandle();

    HOST_INTERFACE_Protocol_Init( &protocol_state );

    while ( true )
    {
        // These results belong to one service cycle. A failed outgoing submit
        // leaves the pending message unchanged, while incoming availability is
        // reported only for a newly decoded message from this cycle.
        notifications                   = 0U;
        bool outgoing_message_accepted  = false;
        bool incoming_message_available = false;

        // The current upstream caller can consume one incoming message per
        // cycle. This argument is intentionally separate from the output
        // availability flag: future application dispatch code can provide
        // backpressure without stopping USB/Transport service.
        HOST_INTERFACE_Protocol_Process( &protocol_state,
                                         outgoing_message_pending ? &outgoing_message : NULL,
                                         &outgoing_message_accepted, can_consume_incoming,
                                         &incoming_message, &incoming_message_available );

        ( void )xTaskNotifyWait( 0U, UINT32_MAX, &notifications, 0U );
        carry_on_notifications = carry_on_notifications | notifications;

        if ( outgoing_message_accepted )
        {
            outgoing_message_pending = false;
        }

        // check if we are overflowing (inverse of can_consume_incoming)
        if ( can_consume_incoming )
        {
            const bool outgoing_available    = !outgoing_message_pending;
            bool       new_response_required = false;
            if ( HOST_INTERFACE_process_message(
                     incoming_message_available, &incoming_message, outgoing_available,
                     &outgoing_message, &overflow_outgoing_message, &new_response_required,
                     outgoing_variable_data, HOST_INTERFACE_OUTGOING_VARIABLE_DATA_SIZE,
                     &carry_on_notifications, &expected_tick_count )
                 == HOST_INTERFACE_STATUS_OUTGOING_REQUIRED )
            {
                // We are overflowing, so stop processing incomming messages
                can_consume_incoming = false;
                overflow_timer       = xTaskGetTickCount();
            }
            else if ( new_response_required )
            {
                outgoing_message_pending = true;
            }
        }
        else
        {
            // if we are overflowing poll outgoing_message_accepted for 100ms Then fault
            if ( outgoing_message_accepted )
            {
                // overflow over so pass the latest output message and return to normal
                outgoing_message         = overflow_outgoing_message;
                outgoing_message_pending = true;
                can_consume_incoming     = true;
            }
            else if ( xTaskGetTickCount() - overflow_timer >= pdMS_TO_TICKS( 100U ) )
            {
                // Outgoing message overflow timeout
                HOST_INTERFACE_Error_Handler();
                return;
            }
        }

        /*
         * TODO: When in the result transfer phase (FLASH_MANAGER_STATE_TRANSFERRING_RESULTS),
         *       if !outgoing_message_pending:
         *
         *       HIL_Application_Message_T result_msg;
         *       Result_Message_Producer_Status_T res_status =
         *           RESULT_MESSAGE_PRODUCER_ProduceNextMessage(&result_msg);
         *
         *       if (res_status == RESULT_MESSAGE_PRODUCER_STATUS_OK) {
         *           result_msg.test_id = active_test_id; // Stamp active Test ID
         *           outgoing_message = result_msg;
         *           outgoing_message_pending = true;
         *       } else if (res_status == RESULT_MESSAGE_PRODUCER_STATUS_END_OF_STREAM) {
         *           FLASH_MANAGER_FinishResultTransfer();
         *       }
         */

        vTaskDelayUntil( &protocol_state.initial_ticks, pdMS_TO_TICKS( HOST_INTERFACE_PERIOD_MS ) );
    }
}
