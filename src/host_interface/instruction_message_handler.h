/******************************************************************************
 *  File:       instruction_message_handler.h
 *  Author:     Callum Rafferty
 *  Created:    15-Sep-2026
 *
 *  Description:
 *      Public interface for handling incoming application layer instruction
 *      messages, converting them into canonical format, validating them,
 *      and uploading them to flash storage.
 *
 *  Notes:
 *      None
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
 * @details Resets timestamp sequencing and internal state tracking for a new
 *          test session.
 */
void HOST_INSTRUCTION_HANDLER_Reset( void );

/**
 * @brief Handles an incoming application layer instruction message.
 *
 * @details Converts the instruction to the canonical Execution Manager format,
 *          validates it against the caller contract, and uploads it to flash.
 *
 * @param[in] instruction Pointer to the incoming application test instruction.
 *
 * @return HOST_INTERFACE_STATUS_OK on success, or an error status code on failure.
 */
HOST_Interface_Status_T
HOST_INSTRUCTION_HANDLER_HandleInstruction( const HIL_Application_Test_Instruction_T* instruction );

#ifdef __cplusplus
}
#endif

#endif /* INSTRUCTION_MESSAGE_HANDLER_H */
