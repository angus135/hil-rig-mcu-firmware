/******************************************************************************
 *  File:       exec_can.h
 *  Author:     HIL-RIG Firmware Team
 *  Created:    25-Mar-2026
 *
 *  Description:
 *      Execution-level lifecycle and frame API for DUT-facing classic CAN.
 *
 *      This layer mirrors the configuration, load/trigger and receive patterns
 *      used by exec_spi and exec_uart while preserving CAN frame boundaries and
 *      identifiers.
 ******************************************************************************/

#ifndef EXEC_CAN_H
#define EXEC_CAN_H

#ifdef __cplusplus
extern "C"
{
#endif

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include "hw_can.h"

#include <stdbool.h>
#include <stdint.h>

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

/**
 * @brief Configure and start one CAN channel.
 *
 * Configuration is intended for setup time, before the 100 us execution loop is
 * started. If the channel is already active or faulted, a hardware stop is
 * attempted before the new configuration is applied. Execution state becomes
 * active only after both the low-level configure and start operations succeed.
 *
 * @param channel Physical CAN channel.
 * @param config Complete timing, mode, retransmission and filter configuration.
 *
 * @return The precise low-level result. On failure no normal transmit/receive
 * operation is permitted. The execution channel is unconfigured when cleanup
 * succeeds, or faulted when cleanup itself fails so a later configure/recover
 * operation can retry the physical shutdown.
 */
HwCanResult_T EXEC_CAN_Configure_Channel( HwCanChannel_T channel, const HwCanConfig_T* config );

/**
 * @brief Stop and deconfigure one CAN channel.
 *
 * Queued TX frames, unread RX frames and diagnostics are discarded as part of
 * deconfiguration. Calling this for an already unconfigured channel succeeds.
 */
HwCanResult_T EXEC_CAN_Deconfigure_Channel( HwCanChannel_T channel );

/**
 * @brief Reapply stored hardware configuration after a bus fault.
 *
 * Recovery is a non-ISR operation. Frames still waiting in the software TX
 * queue are retained; frames that had already entered hardware mailboxes are
 * classified as aborted because their final bus outcome cannot be guaranteed.
 */
HwCanResult_T EXEC_CAN_Recover_Channel( HwCanChannel_T channel );

/**
 * @brief Queue and asynchronously trigger a complete CAN frame batch.
 *
 * Each frame carries its own standard/extended identifier, DLC and data/remote
 * type. The low-level load operation is all-or-nothing. A successful return
 * means every frame was queued and TX servicing was requested; it does not mean
 * the frames have already been acknowledged on the bus.
 *
 * Loading transfers ownership before triggering. If triggering then reports
 * HW_CAN_RESULT_BUS_OFF, the complete batch remains queued and must not be
 * submitted again; EXEC_CAN_Recover_Channel() preserves and retriggers it.
 *
 * The caller is the single execution-level TX producer for the channel.
 * Concurrent calls from multiple contexts are not supported.
 */
HwCanResult_T EXEC_CAN_Transmit( HwCanChannel_T channel, const HwCanFrame_T* frames,
                                 uint32_t frame_count );

/**
 * @brief Copy up to destination_capacity_frames unread frames into caller storage.
 *
 * The function performs one low-level peek, copies at most the caller-provided
 * capacity from the first and optional wrapped span, and consumes exactly the
 * number copied. A small destination capacity therefore provides a direct,
 * deterministic per-call work bound for the execution scheduler.
 *
 * @param channel CAN channel to read.
 * @param destination Caller-owned frame array. May be NULL only when capacity is zero.
 * @param destination_capacity_frames Number of complete frames destination can hold.
 * @param frames_read Receives the number of complete frames copied.
 */
HwCanResult_T EXEC_CAN_Receive( HwCanChannel_T channel, HwCanRxFrame_T* destination,
                                uint32_t destination_capacity_frames, uint32_t* frames_read );

/**
 * @brief Return true when no TX queue entry or hardware mailbox remains pending.
 *
 * Idle does not imply success. Call EXEC_CAN_Get_Status() to inspect any
 * arbitration, transmission, abort or bus-state faults.
 */
bool EXEC_CAN_Is_Transmission_Complete( HwCanChannel_T channel );

/**
 * @brief Retrieve a non-blocking low-level status and diagnostic snapshot.
 */
HwCanResult_T EXEC_CAN_Get_Status( HwCanChannel_T channel, HwCanStatus_T* status );

/**
 * @brief Clear diagnostics while the channel is configured but stopped.
 */
HwCanResult_T EXEC_CAN_Clear_Diagnostics( HwCanChannel_T channel );

#ifdef __cplusplus
}
#endif

#endif /* EXEC_CAN_H */
