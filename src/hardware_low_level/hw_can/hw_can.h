/******************************************************************************
 *  File:       hw_can.h
 *  Author:     HIL-RIG Firmware Team
 *  Created:    25-Mar-2026
 *
 *  Description:
 *      Public low-level interface for the two DUT-facing classic-CAN channels.
 *
 *      The interface preserves complete CAN frames, including their identifier,
 *      identifier format, frame type and data length. It also provides fixed-
 *      allocation transmit and receive queues suitable for use by the HIL-RIG
 *      execution path.
 *
 *  Notes:
 *      This MCU contains an STM32 bxCAN peripheral. bxCAN supports classic CAN
 *      frames with at most eight data bytes; it does not support CAN FD.
 ******************************************************************************/

#ifndef HW_CAN_H
#define HW_CAN_H

#ifdef __cplusplus
extern "C"
{
#endif

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include <stdbool.h>
#include <stdint.h>

/**-----------------------------------------------------------------------------
 *  Public Defines / Macros
 *------------------------------------------------------------------------------
 */

// Number of independent DUT-facing CAN controllers fitted to the HIL-RIG MCU.
#define HW_CAN_CHANNEL_COUNT 2U

// Classic CAN carries between zero and eight data bytes in one frame.
#define HW_CAN_CLASSIC_MAX_DATA_BYTES 8U

// Public identifier bounds used by configuration, frame builders and consoles.
#define HW_CAN_STANDARD_IDENTIFIER_MAX 0x7FFU
#define HW_CAN_EXTENDED_IDENTIFIER_MAX 0x1FFFFFFFU

// Power-of-two queue sizes allow the ISR paths to wrap indices with a mask.
#define HW_CAN_TX_QUEUE_DEPTH_FRAMES 32U
#define HW_CAN_RX_QUEUE_DEPTH_FRAMES 32U

// The STM32F446 shares 28 filter banks between CAN1 and CAN2 at this split.
#define HW_CAN_FILTER_BANK_SPLIT 14U
#define HW_CAN_MAX_FILTERS_PER_CHANNEL 14U

// Bit-timing validation limits used only while a channel is being configured.
#define HW_CAN_DEFAULT_SAMPLE_POINT_PERMILL 800U
#define HW_CAN_MAX_BITRATE_ERROR_PPM 5000U

// Latched diagnostic bits. Multiple faults may be present at the same time.
typedef uint32_t HwCanFaultFlags_T;

#define HW_CAN_FAULT_NONE ( ( HwCanFaultFlags_T )0U )
#define HW_CAN_FAULT_TX_ARBITRATION_LOST ( ( HwCanFaultFlags_T )( 1UL << 0U ) )
#define HW_CAN_FAULT_TX_ERROR ( ( HwCanFaultFlags_T )( 1UL << 1U ) )
#define HW_CAN_FAULT_TX_ABORTED ( ( HwCanFaultFlags_T )( 1UL << 2U ) )
#define HW_CAN_FAULT_RX_SOFTWARE_OVERFLOW ( ( HwCanFaultFlags_T )( 1UL << 3U ) )
#define HW_CAN_FAULT_RX_FIFO_FULL ( ( HwCanFaultFlags_T )( 1UL << 4U ) )
#define HW_CAN_FAULT_RX_FIFO_OVERRUN ( ( HwCanFaultFlags_T )( 1UL << 5U ) )
#define HW_CAN_FAULT_ERROR_WARNING ( ( HwCanFaultFlags_T )( 1UL << 6U ) )
#define HW_CAN_FAULT_ERROR_PASSIVE ( ( HwCanFaultFlags_T )( 1UL << 7U ) )
#define HW_CAN_FAULT_BUS_OFF ( ( HwCanFaultFlags_T )( 1UL << 8U ) )
#define HW_CAN_FAULT_PROTOCOL_ERROR ( ( HwCanFaultFlags_T )( 1UL << 9U ) )

/**-----------------------------------------------------------------------------
 *  Public Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

/**
 * @brief Identifies one physical CAN controller.
 *
 * The enum values are contiguous so cold-path APIs can use them to index the
 * channel state table after validating the value.
 */
typedef enum HwCanChannel_T
{
    HW_CAN_CHANNEL_1 = 0U,  ///< DUT-facing CAN controller connected to CAN1.
    HW_CAN_CHANNEL_2 = 1U,  ///< DUT-facing CAN controller connected to CAN2.
} HwCanChannel_T;

/**
 * @brief Result returned by CAN hardware and execution operations.
 *
 * Queueing is asynchronous. HW_CAN_RESULT_OK from a transmit-load call means
 * the complete batch was copied into the software queue; it does not mean the
 * frames have already won arbitration or been acknowledged on the CAN bus.
 */
typedef enum HwCanResult_T
{
    HW_CAN_RESULT_OK = 0U,
    HW_CAN_RESULT_INVALID_CHANNEL,
    HW_CAN_RESULT_INVALID_ARGUMENT,
    HW_CAN_RESULT_NOT_CONFIGURED,
    HW_CAN_RESULT_NOT_STARTED,
    HW_CAN_RESULT_ALREADY_STARTED,
    HW_CAN_RESULT_UNSUPPORTED_TIMING,
    HW_CAN_RESULT_FILTER_ERROR,
    HW_CAN_RESULT_QUEUE_FULL,
    HW_CAN_RESULT_BUS_OFF,
    HW_CAN_RESULT_BUSY,
    HW_CAN_RESULT_HARDWARE_ERROR,
} HwCanResult_T;

/**
 * @brief Driver lifecycle state for one CAN channel.
 */
typedef enum HwCanState_T
{
    HW_CAN_STATE_UNCONFIGURED = 0U,
    HW_CAN_STATE_CONFIGURED,
    HW_CAN_STATE_ACTIVE,
    HW_CAN_STATE_FAULT,
} HwCanState_T;

/**
 * @brief bxCAN operating mode.
 *
 * Loopback modes are useful for driver validation. Silent modes prevent the
 * controller from actively acknowledging or driving normal CAN traffic.
 */
typedef enum HwCanMode_T
{
    HW_CAN_MODE_NORMAL = 0U,
    HW_CAN_MODE_LOOPBACK,
    HW_CAN_MODE_SILENT,
    HW_CAN_MODE_SILENT_LOOPBACK,
} HwCanMode_T;

/**
 * @brief Arbitration policy between frames already loaded into local mailboxes.
 *
 * This setting does not change CAN bus arbitration against other nodes.
 */
typedef enum HwCanTxPriority_T
{
    HW_CAN_TX_PRIORITY_REQUEST_ORDER = 0U,  ///< Preserve software queue request order.
    HW_CAN_TX_PRIORITY_IDENTIFIER,          ///< Lower CAN identifier wins local priority.
} HwCanTxPriority_T;

/**
 * @brief Receive-filter policy applied when the channel is configured.
 */
typedef enum HwCanFilterPolicy_T
{
    HW_CAN_FILTER_ACCEPT_NONE = 0U,  ///< Deactivate every bank assigned to the channel.
    HW_CAN_FILTER_ACCEPT_ALL,        ///< Explicit bus-monitor mode; accept every frame.
    HW_CAN_FILTER_CONFIGURED,        ///< Use the filter array supplied in HwCanConfig_T.
} HwCanFilterPolicy_T;

/**
 * @brief Hardware comparison mode for one 32-bit bxCAN filter bank.
 */
typedef enum HwCanFilterMode_T
{
    HW_CAN_FILTER_MODE_MASK = 0U,  ///< Match one identifier using identifier_mask.
    HW_CAN_FILTER_MODE_LIST,       ///< Match either of two complete filter values.
} HwCanFilterMode_T;

/**
 * @brief Last protocol error reported by the bxCAN error-status register.
 */
typedef enum HwCanLastError_T
{
    HW_CAN_LAST_ERROR_NONE = 0U,
    HW_CAN_LAST_ERROR_STUFF,
    HW_CAN_LAST_ERROR_FORM,
    HW_CAN_LAST_ERROR_ACKNOWLEDGEMENT,
    HW_CAN_LAST_ERROR_BIT_RECESSIVE,
    HW_CAN_LAST_ERROR_BIT_DOMINANT,
    HW_CAN_LAST_ERROR_CRC,
    HW_CAN_LAST_ERROR_SOFTWARE,
} HwCanLastError_T;

/**
 * @brief Complete classic-CAN frame used by transmit callers and RX consumers.
 *
 * CAN is a broadcast bus. The identifier is not a point-to-point destination
 * address; it labels the message, participates in arbitration and is used by
 * receive filters. A lower numeric identifier has higher arbitration priority.
 *
 * Standard identifiers occupy 11 bits and must be at most 0x7FF. Extended
 * identifiers occupy 29 bits and must be at most 0x1FFFFFFF.
 *
 * For a data frame, dlc gives the number of meaningful bytes in data. For a
 * remote frame, dlc gives the requested response length and data is ignored.
 */
typedef struct HwCanFrame_T
{
    uint32_t identifier;  ///< Eleven-bit or twenty-nine-bit CAN identifier.
    uint8_t  data[HW_CAN_CLASSIC_MAX_DATA_BYTES];  ///< Payload storage; only dlc bytes are valid.
    uint8_t  dlc;              ///< Data Length Code in the inclusive range zero to eight.
    bool     is_extended_id;   ///< false for 11-bit standard ID, true for 29-bit extended ID.
    bool     is_remote_frame;  ///< false for a data frame, true for a remote-request frame.
} HwCanFrame_T;

/**
 * @brief RX frame plus metadata captured by the STM32 receive mailbox.
 *
 * filter_match_index identifies the hardware filter that accepted the frame.
 * timestamp is the optional 16-bit bxCAN receive timestamp. Both are captured
 * in the ISR before the hardware FIFO entry is released.
 */
typedef struct HwCanRxFrame_T
{
    HwCanFrame_T frame;
    uint16_t     timestamp;
    uint8_t      filter_match_index;
} HwCanRxFrame_T;

/**
 * @brief One exact identifier/type value used by list filters.
 */
typedef struct HwCanFilterValue_T
{
    uint32_t identifier;
    bool     is_extended_id;
    bool     is_remote_frame;
} HwCanFilterValue_T;

/**
 * @brief Description of one 32-bit bxCAN acceptance-filter bank.
 *
 * In mask mode, first contains the expected identifier and identifier format.
 * identifier_mask selects the identifier bits that must compare equal. The IDE
 * bit is always compared so standard and extended traffic cannot alias. RTR is
 * compared only when match_remote_frame is true.
 *
 * In list mode, first and second are two exact accepted frame identifiers. The
 * identifier format and data/remote type are part of each exact value.
 */
typedef struct HwCanFilter_T
{
    HwCanFilterMode_T  mode;
    HwCanFilterValue_T first;
    HwCanFilterValue_T second;
    uint32_t           identifier_mask;
    bool               match_remote_frame;
} HwCanFilter_T;

/**
 * @brief Complete configuration for one CAN channel.
 *
 * Configuration is a cold-path operation. The low-level driver computes legal
 * bxCAN time-segment and prescaler values from the current APB1 peripheral
 * clock, requested bitrate, sample point and synchronization jump width.
 * Configurations whose best achievable bitrate exceeds
 * HW_CAN_MAX_BITRATE_ERROR_PPM are rejected.
 *
 * The filters pointer is read only during HW_CAN_Configure_Channel(). The
 * driver copies configured filters into private storage, so caller-owned filter
 * memory may be released immediately after configuration returns.
 */
typedef struct HwCanConfig_T
{
    uint32_t bitrate;               ///< Requested classic-CAN bitrate in bits per second.
    uint16_t sample_point_permill;  ///< Desired sample point; 800 means 80.0 percent.
    uint8_t  sync_jump_width_tq;    ///< Resynchronization jump width from one to four TQ.

    HwCanMode_T          mode;
    HwCanTxPriority_T    tx_priority;
    HwCanFilterPolicy_T  filter_policy;
    const HwCanFilter_T* filters;
    uint8_t              filter_count;

    bool automatic_retransmission;    ///< Retry failed bus transmissions in hardware.
    bool automatic_bus_off_recovery;  ///< Recover after the ISO-defined bus-off idle period.
    bool automatic_wake_up;           ///< Wake the peripheral when bus activity is detected.
    bool receive_fifo_locked;         ///< Preserve oldest hardware FIFO entries when full.
} HwCanConfig_T;

/**
 * @brief One contiguous span of unread complete CAN frames.
 *
 * The pointer refers to driver-owned storage and remains valid only until the
 * corresponding frames are consumed or the channel is stopped/reconfigured.
 * Callers must not modify the frames.
 */
typedef struct HwCanRxSpan_T
{
    const HwCanRxFrame_T* frames;
    uint32_t              frame_count;
} HwCanRxSpan_T;

/**
 * @brief Unread RX data represented as one or two frame spans.
 *
 * A wrapped circular queue requires two spans. The first span always contains
 * the oldest unread frame. Consume frames only after processing has completed.
 */
typedef struct HwCanRxSpans_T
{
    HwCanRxSpan_T first_span;
    HwCanRxSpan_T second_span;
    uint32_t      total_frame_count;
} HwCanRxSpans_T;

/**
 * @brief Non-blocking diagnostic snapshot for one CAN channel.
 *
 * Queueing, hardware-mailbox ownership and wire transmission are deliberately
 * reported separately. tx_queue_frame_count excludes frames already copied to
 * a hardware mailbox; tx_in_flight_count reports those mailbox-owned frames.
 */
typedef struct HwCanStatus_T
{
    HwCanState_T state;

    uint32_t requested_bitrate;
    uint32_t actual_bitrate;
    uint16_t actual_sample_point_permill;
    uint16_t bitrate_error_ppm;
    uint16_t prescaler;
    uint8_t  bit_segment_1_tq;
    uint8_t  bit_segment_2_tq;
    uint8_t  sync_jump_width_tq;

    uint32_t tx_queue_frame_count;
    uint32_t rx_queue_frame_count;
    uint8_t  tx_in_flight_count;

    bool    error_warning;
    bool    error_passive;
    bool    bus_off;
    uint8_t transmit_error_count;
    uint8_t receive_error_count;

    HwCanLastError_T  last_error;
    HwCanFaultFlags_T latched_faults;

    uint32_t tx_frames_queued;
    uint32_t tx_frames_rejected;
    uint32_t tx_frames_submitted;
    uint32_t tx_frames_succeeded;
    uint32_t tx_arbitration_losses;
    uint32_t tx_errors;
    uint32_t tx_aborted;

    uint32_t rx_frames_received;
    uint32_t rx_software_drops;
    uint32_t rx_fifo_full_events;
    uint32_t rx_fifo_overruns;

    uint32_t error_warning_events;
    uint32_t error_passive_events;
    uint32_t bus_off_events;
} HwCanStatus_T;

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

/**
 * @brief Validate and store a complete CAN channel configuration.
 *
 * This function configures bit timing and acceptance filters but does not start
 * bus participation. The channel must not be active. All existing software
 * queue contents and diagnostics are reset after successful configuration.
 * CAN1 and CAN2 share filter-initialization hardware; configure both channels
 * while stopped if the peer channel must not experience a brief receive gap.
 *
 * @return HW_CAN_RESULT_OK when configuration was applied successfully.
 * @return HW_CAN_RESULT_UNSUPPORTED_TIMING when no legal timing meets tolerance.
 * @return HW_CAN_RESULT_FILTER_ERROR when a filter is invalid or HAL rejects it.
 */
HwCanResult_T HW_CAN_Configure_Channel( HwCanChannel_T channel, const HwCanConfig_T* config );

/**
 * @brief Start a previously configured channel and enable CAN notifications.
 *
 * Starting enables receive, transmit-completion and error observation. It does
 * not itself queue or transmit a frame.
 */
HwCanResult_T HW_CAN_Start_Channel( HwCanChannel_T channel );

/**
 * @brief Stop an active channel and discard queued/runtime frame state.
 *
 * Any hardware-owned transmission is classified as completed if its completion
 * flag is already present; otherwise it is counted as aborted. Configuration is
 * retained so the channel may subsequently be started again.
 */
HwCanResult_T HW_CAN_Stop_Channel( HwCanChannel_T channel );

/**
 * @brief Stop a channel, deactivate its filter banks and forget configuration.
 *
 * Filter deactivation uses the CAN1/CAN2 shared filter-initialization hardware.
 * Stop the peer channel first when loss-free reception on that peer is required.
 */
HwCanResult_T HW_CAN_Deconfigure_Channel( HwCanChannel_T channel );

/**
 * @brief Reapply the stored configuration after a bus fault.
 *
 * Recovery runs only in normal execution context. It never runs in a CAN ISR.
 * Software TX frames not yet submitted to hardware are preserved; in-flight
 * frames are counted as aborted because their wire outcome is no longer known.
 * At least one complete prior configuration must have succeeded; a fault from
 * the first failed configuration attempt returns HW_CAN_RESULT_NOT_CONFIGURED.
 * Recovery reprograms shared filters, so it is coordinated cold-path work when
 * the peer channel must receive without a gap.
 */
HwCanResult_T HW_CAN_Recover_Channel( HwCanChannel_T channel );

/**
 * @brief Return whether the low-level channel is currently active.
 */
bool HW_CAN_Is_Channel_Started( HwCanChannel_T channel );

/**
 * @brief Atomically append a complete frame batch to the TX software queue.
 *
 * The function validates every frame and checks capacity before publishing any
 * entry. Therefore the operation is all-or-nothing: on failure, no frame from
 * the batch becomes visible to the TX ISR. Loading is independent of current
 * bus-off state; HW_CAN_Tx_Trigger() reports whether service can begin. This
 * separation ensures a bus-off result after loading has one ownership meaning:
 * the complete batch remains queued for explicit recovery.
 *
 * @param frame_count Number of frames to append, from one through
 * HW_CAN_TX_QUEUE_DEPTH_FRAMES. A zero-frame batch is invalid so callers cannot
 * accidentally request a useless transmit interrupt.
 *
 * The caller owns the single TX-producer role. Concurrent calls from multiple
 * producer contexts are not supported. The CAN TX ISR is the sole consumer.
 */
HwCanResult_T HW_CAN_Load_Tx_Buffer( HwCanChannel_T channel, const HwCanFrame_T* frames,
                                     uint32_t frame_count );

/**
 * @brief Request asynchronous servicing of queued TX frames.
 *
 * The function pends the channel's TX interrupt rather than calling the vector
 * directly. The ISR loads at most the three bxCAN mailboxes and returns.
 */
HwCanResult_T HW_CAN_Tx_Trigger( HwCanChannel_T channel );

/**
 * @brief Return true only when no software frame or hardware mailbox is pending.
 *
 * A true result means the mechanism is idle; inspect status diagnostics to
 * determine whether all completed frames succeeded.
 */
bool HW_CAN_Is_Tx_Complete( HwCanChannel_T channel );

/**
 * @brief Peek at unread RX frames without advancing the consumer index.
 */
HwCanRxSpans_T HW_CAN_Rx_Peek( HwCanChannel_T channel );

/**
 * @brief Consume frames previously returned by HW_CAN_Rx_Peek().
 *
 * @return HW_CAN_RESULT_INVALID_ARGUMENT if frame_count exceeds the currently
 * unread frame count. In that case the read index is not changed.
 */
HwCanResult_T HW_CAN_Rx_Consume( HwCanChannel_T channel, uint32_t frame_count );

/**
 * @brief Copy a non-blocking diagnostic snapshot into caller-provided storage.
 */
HwCanResult_T HW_CAN_Get_Status( HwCanChannel_T channel, HwCanStatus_T* status );

/**
 * @brief Clear counters and latched faults while a channel is stopped.
 *
 * Live hardware warning/passive/bus-off flags are not writable through this
 * function. They remain visible in subsequent status snapshots.
 */
HwCanResult_T HW_CAN_Clear_Diagnostics( HwCanChannel_T channel );

#ifdef __cplusplus
}
#endif

#endif /* HW_CAN_H */
