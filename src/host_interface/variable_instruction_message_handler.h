/******************************************************************************
 *  File:       variable_instruction_message_handler.h
 *  Author:     Callum Rafferty
 *  Created:    24-Sep-2026
 *
 *  Description:
 *      Public interface for handling incoming update-based application layer
 *      instruction messages (UPDATE_INSTRUCTION, Type 21), validating their
 *      sparse logical operations, converting them into canonical Execution
 *      Manager format, and uploading them to the Flash Manager.
 *
 *  Notes:
 *      Update instructions carry sparse logical peripheral operations and
 *      streaming communications (UART, SPI, CAN) scheduled for a given tick.
 *      Each instruction is converted into a 4-byte aligned canonical packed
 *      record (ExecutionInstructionHeader_T + operations) and submitted to the
 *      Flash Manager upload pipeline.
 ******************************************************************************/

#ifndef VARIABLE_INSTRUCTION_MESSAGE_HANDLER_H
#define VARIABLE_INSTRUCTION_MESSAGE_HANDLER_H

#ifdef __cplusplus
extern "C"
{
#endif

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include "hil_rig_protocol/application/application_instruction.h"
#include "host_process_message.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**-----------------------------------------------------------------------------
 *  Public Defines / Macros
 *------------------------------------------------------------------------------
 */

/** @brief Timer input clock frequency for Low Voltage PWM generation (TIM12 on APB1). */
#define HOST_VAR_INSTRUCTION_PWM_LV_TIMER_CLOCK_HZ ( 90000000U )

/** @brief Timer input clock frequency for High Voltage PWM generation (TIM8 on APB2). */
#define HOST_VAR_INSTRUCTION_PWM_HV_TIMER_CLOCK_HZ ( 180000000U )

/** @brief Number of nanoseconds in one second, used for period-to-frequency conversion. */
#define HOST_VAR_INSTRUCTION_NANOSECONDS_PER_SECOND ( 1000000000U )

/** @brief Scaling factor to convert application microvolt values to Volts. */
#define HOST_VAR_INSTRUCTION_MICROVOLTS_PER_VOLT ( 1000000.0f )

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

/**
 * @brief Resets the variable instruction message handler state.
 *
 * @details Clears timestamp monotonicity tracking and initializes the peripheral
 *          state tracker to baseline conditions from the active test configuration.
 *          Must be called before starting a new instruction upload session.
 *
 * @note Call from Host Interface task context only.
 */
void HOST_VARIABLE_INSTRUCTION_HANDLER_Reset( void );

/**
 * @brief Handles an incoming application layer update instruction message.
 *
 * @details Receives an update instruction (Type 21), validates operations against
 *          active hardware configuration, converts them into the canonical 4-byte
 *          aligned Execution Manager operation format, and submits the resulting
 *          instruction chunk to the Flash Manager upload API.
 *
 *          If the instruction produces no peripheral changes (output-free tick),
 *          timestamp tracking is updated and the upload step is safely skipped.
 *
 * @param[in] instruction Pointer to the incoming application update instruction.
 *                        Must not be NULL.
 *
 * @retval HOST_INTERFACE_STATUS_OK
 *         Instruction was converted and accepted for flash upload, or determined
 *         to be an output-free tick.
 * @retval HOST_INTERFACE_STATUS_INVALID_ARGUMENT
 *         instruction pointer is NULL.
 * @retval HOST_INTERFACE_STATUS_INCONSISTENT_TICK
 *         Tick number is not strictly monotonically increasing.
 * @retval HOST_INTERFACE_STATUS_VALIDATION_FAILED
 *         Hardware feasibility validation failed (e.g. disabled channel or invalid data).
 * @retval HOST_INTERFACE_STATUS_STATE_TRANSITION_FAILURE
 *         Flash Manager rejected the upload submission due to invalid lifecycle state.
 * @retval HOST_INTERFACE_STATUS_INTERNAL_ERROR
 *         Flash Manager upload ring is busy or encountered an internal error.
 *
 * @note Call from Host Interface task context only while Flash Manager is in
 *       FLASH_MANAGER_STATE_INSTRUCTION_UPLOAD.
 */
HOST_Interface_Status_T HOST_VARIABLE_INSTRUCTION_HANDLER_HandleInstruction(
    const HIL_Application_Update_Instruction_T* instruction );

#ifdef __cplusplus
}
#endif

#endif /* VARIABLE_INSTRUCTION_MESSAGE_HANDLER_H */
