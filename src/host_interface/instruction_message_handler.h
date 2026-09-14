/******************************************************************************
 *  File:       instruction_message_handler.h
 *  Author:     Callum Rafferty
 *  Created:    15-Sep-2026
 *
 *  Description:
 *      Public interface for handling incoming application-layer instruction
 *      messages, converting them into canonical Execution Manager format,
 *      filtering state deltas, and uploading them to the Flash Manager.
 *
 *  Notes:
 *      Instruction upload requires monotonic, 1-based execution timestamps.
 *      Each instruction is converted into a 4-byte aligned canonical packed
 *      record (ExecutionInstructionHeader_T + operations) and submitted to the
 *      Flash Manager upload pipeline.
 ******************************************************************************/

#ifndef INSTRUCTION_MESSAGE_HANDLER_H
#define INSTRUCTION_MESSAGE_HANDLER_H

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

/**-----------------------------------------------------------------------------
 *  Public Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

/**
 * @brief Resets the instruction message handler state.
 *
 * @details Clears timestamp monotonicity tracking and initializes the peripheral
 *          state delta tracker to baseline zero conditions. Must be called before
 *          starting a new instruction upload session.
 *
 * @note Call from Host Interface task context only.
 */
void HOST_INSTRUCTION_HANDLER_Reset( void );

/**
 * @brief Handles an incoming application layer instruction message.
 *
 * @details Receives an application protocol instruction, tracks state changes
 *          relative to previous ticks, converts any modified outputs into the
 *          canonical 4-byte aligned Execution Manager operation format, and
 *          submits the resulting instruction chunk to the Flash Manager upload API.
 *
 *          If the instruction produces no peripheral changes (output-free tick),
 *          timestamp tracking is updated and the upload step is safely skipped.
 *
 * @param[in] instruction Pointer to the incoming application test instruction.
 *                        Must not be NULL.
 *
 * @retval HOST_INTERFACE_STATUS_OK
 *         Instruction was converted and accepted for flash upload, or determined
 *         to be an output-free tick.
 * @retval HOST_INTERFACE_STATUS_INVALID_ARGUMENT
 *         instruction pointer is NULL.
 * @retval HOST_INTERFACE_STATUS_VALIDATION_FAILED
 *         Hardware feasibility validation failed (e.g. unrepresentable PWM frequency).
 * @retval HOST_INTERFACE_STATUS_STATE_TRANSITION_FAILURE
 *         Flash Manager rejected the upload submission due to invalid lifecycle state.
 * @retval HOST_INTERFACE_STATUS_INTERNAL_ERROR
 *         Flash Manager upload ring is busy or encountered an internal error.
 *
 * @note Call from Host Interface task context only while Flash Manager is in
 *       FLASH_MANAGER_STATE_INSTRUCTION_UPLOAD.
 */
HOST_Interface_Status_T
HOST_INSTRUCTION_HANDLER_HandleInstruction( const HIL_Application_Test_Instruction_T* instruction );

#ifdef __cplusplus
}
#endif

#endif /* INSTRUCTION_MESSAGE_HANDLER_H */
