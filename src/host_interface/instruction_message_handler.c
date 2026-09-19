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


#ifdef __cplusplus
}
#endif

#endif /* INSTRUCTION_MESSAGE_HANDLER_H */
