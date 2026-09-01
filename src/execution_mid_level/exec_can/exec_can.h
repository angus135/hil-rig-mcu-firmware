/******************************************************************************
 *  File:       exec_can.h
 *  Author:     Angus Corr
 *  Created:    25-Mar-2026
 *
 *  Description:
 *      Execution-layer interface for classical CAN lifecycle, buffered
 *      transmission, reception, status, and recovery.
 *
 *  Notes:
 *      The execution API owns its public types and deliberately does not
 *      expose hardware-driver types.
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

#include <stdbool.h>
#include <stdint.h>

/**-----------------------------------------------------------------------------
 *  Public Defines / Macros
 *------------------------------------------------------------------------------
 */

#define EXEC_CAN_MAX_PAYLOAD_SIZE ( 8U )
/** Usable hardware queue capacity, compile-time checked in exec_can.c. */
#define EXEC_CAN_MAX_BATCH_SIZE ( 19U )
#define EXEC_CAN_STANDARD_ID_MAX ( 0x7FFU )

/**-----------------------------------------------------------------------------
 *  Public Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

/** CAN channel selected by an execution or protocol operation. */
typedef enum EXEC_CAN_Channel_T
{
    EXEC_CAN_CHANNEL_1 = 0,
    EXEC_CAN_CHANNEL_2,
    EXEC_CAN_CHANNEL_COUNT,
} EXEC_CAN_Channel_T;

/**
 * @brief Execution-level configuration for one CAN channel.
 */
typedef struct EXEC_CAN_Config_T
{
    bool     is_enabled;
    uint32_t bitrate;
    uint16_t filter_bank;
    uint16_t filter_id;
    uint16_t filter_mask;
} EXEC_CAN_Config_T;

/** Result of an execution-layer CAN operation. */
typedef enum EXEC_CAN_Result_T
{
    EXEC_CAN_RESULT_OK = 0,
    EXEC_CAN_RESULT_INVALID_ARGUMENT,
    EXEC_CAN_RESULT_BUSY,
    EXEC_CAN_RESULT_EMPTY,
    EXEC_CAN_RESULT_NOT_CONFIGURED,
    EXEC_CAN_RESULT_NOT_STARTED,
    EXEC_CAN_RESULT_TIMING_ERROR,
    EXEC_CAN_RESULT_FILTER_ERROR,
    EXEC_CAN_RESULT_ERROR,
} EXEC_CAN_Result_T;

/** Observable state of one buffered CAN transmit batch. */
typedef enum EXEC_CAN_Tx_Status_T
{
    EXEC_CAN_TX_STATUS_IDLE = 0,
    EXEC_CAN_TX_STATUS_ACTIVE,
    EXEC_CAN_TX_STATUS_COMPLETE,
    EXEC_CAN_TX_STATUS_ERROR,
    EXEC_CAN_TX_STATUS_INVALID_CHANNEL,
} EXEC_CAN_Tx_Status_T;

/** Standard classical CAN data frame. */
typedef struct EXEC_CAN_Packet_T
{
    uint16_t id;
    uint8_t  dlc;
    uint8_t  data[EXEC_CAN_MAX_PAYLOAD_SIZE];
} EXEC_CAN_Packet_T;

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

/**
 * @brief Configure or disable one CAN channel.
 *
 * An enabled configuration applies peripheral timing and filtering but leaves
 * the channel stopped. Call EXEC_CAN_Start_Channel() to begin operation.
 *
 * A disabled configuration stops the channel when necessary and clears its
 * execution-layer configured state.
 *
 * @note CAN transceiver-level safe-state control will be added when the board
 *       control mapping is confirmed during hardware bring-up.
 */
EXEC_CAN_Result_T EXEC_CAN_Configure_Channel( EXEC_CAN_Channel_T       channel,
                                              const EXEC_CAN_Config_T* configuration );

/** Start a configured CAN channel. */
EXEC_CAN_Result_T EXEC_CAN_Start_Channel( EXEC_CAN_Channel_T channel );

/** Stop a started CAN channel while retaining its configuration. */
EXEC_CAN_Result_T EXEC_CAN_Stop_Channel( EXEC_CAN_Channel_T channel );

/** Return true when the selected channel is configured. */
bool EXEC_CAN_Is_Configured( EXEC_CAN_Channel_T channel );

/** Return true when the selected channel is configured and started. */
bool EXEC_CAN_Is_Started( EXEC_CAN_Channel_T channel );

/**
 * @brief Load and start one complete CAN transmit batch.
 *
 * The hardware trigger is called exactly once and only after the complete
 * batch has loaded successfully. A trigger failure discards the loaded batch.
 * Null storage, zero or oversized batches, extended identifiers, and DLCs
 * greater than eight return EXEC_CAN_RESULT_INVALID_ARGUMENT.
 */
EXEC_CAN_Result_T EXEC_CAN_Transmit( EXEC_CAN_Channel_T channel, const EXEC_CAN_Packet_T packets[],
                                     uint16_t packet_count );

/**
 * @brief Copy and consume received packets from one CAN channel.
 *
 * @param destination  Caller-owned packet storage.
 * @param capacity     Maximum packets that fit in destination.
 * @param packets_read Number of packets copied and consumed.
 *
 * Invalid channels and null required pointers return
 * EXEC_CAN_RESULT_INVALID_ARGUMENT. No available packets and zero capacity
 * return EXEC_CAN_RESULT_OK with packets_read set to zero.
 */
EXEC_CAN_Result_T EXEC_CAN_Receive( EXEC_CAN_Channel_T channel, EXEC_CAN_Packet_T destination[],
                                    uint16_t capacity, uint16_t* packets_read );

/** Return the current buffered transmit status for one CAN channel. */
EXEC_CAN_Tx_Status_T EXEC_CAN_Get_Tx_Status( EXEC_CAN_Channel_T channel );

/**
 * @brief Recover a configured, started CAN channel after a terminal error.
 *
 * Recovery may discard failed and queued transmit work. If hardware recovery
 * fails, execution lifecycle state is synchronized with the resulting HW
 * lifecycle state.
 */
EXEC_CAN_Result_T EXEC_CAN_Recover( EXEC_CAN_Channel_T channel );

/** Return the sticky software RX dropped-frame count for one CAN channel. */
uint32_t EXEC_CAN_Get_Rx_Dropped_Count( EXEC_CAN_Channel_T channel );

#ifdef __cplusplus
}
#endif

#endif /* EXEC_CAN_H */
