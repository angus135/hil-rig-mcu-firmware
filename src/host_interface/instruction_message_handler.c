/******************************************************************************
 *  File:       instruction_message_handler.c
 *  Author:     Callum Rafferty
 *  Created:    15-Sep-2026
 *
 *  Description:
 *      Implementation of incoming application instruction message handling
 *      and canonical Flash Manager instruction conversion.
 *
 *  Notes:
 *      None
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include "instruction_message_handler.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

typedef struct
{
    uint8_t* buffer;
    size_t   capacity;
    size_t   offset;
    uint8_t  operation_count;
} InstructionWriter_T;

/**-----------------------------------------------------------------------------
 *  Public (global) and Extern Variables
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Private (static) Variables
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Private (static) Function Prototypes
 *------------------------------------------------------------------------------
 */

static HOST_Interface_Status_T
EncodeDigitalOutput( const HIL_Application_Test_Instruction_T* instruction,
                     InstructionWriter_T*                      writer );

static HOST_Interface_Status_T
EncodeAnalogueOutputs( const HIL_Application_Test_Instruction_T* instruction,
                       InstructionWriter_T*                      writer );

static HOST_Interface_Status_T
EncodePwmOutputs( const HIL_Application_Test_Instruction_T* instruction,
                  InstructionWriter_T*                      writer );

static HOST_Interface_Status_T HOST_INSTRUCTION_HANDLER_EncodeUartTransmitStub( void );
static HOST_Interface_Status_T HOST_INSTRUCTION_HANDLER_EncodeSpiTransmitStub( void );
static HOST_Interface_Status_T HOST_INSTRUCTION_HANDLER_EncodeCanTransmitStub( void );

static HOST_Interface_Status_T
HOST_INSTRUCTION_HANDLER_ConvertInstruction( const HIL_Application_Test_Instruction_T* instruction,
                                             uint8_t* destination, size_t destination_capacity,
                                             size_t* bytes_written );

static HOST_Interface_Status_T HOST_INSTRUCTION_HANDLER_UploadToFlash( const uint8_t* data,
                                                                       size_t         length );
/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */
