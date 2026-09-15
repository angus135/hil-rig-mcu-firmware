/******************************************************************************
 *  File:       result_message_producer.c
 *  Author:     Callum Rafferty
 *  Created:    15-Sep-2026
 *
 *  Description:
 *      Implementation of the result message producer for retrieving raw driver
 *      measurement records from Flash Manager, unpacking and aggregating them
 *      by tick timestamp, and constructing outbound Application Test Result
 *      protocol messages during the result transfer phase.
 *
 *  Notes:
 *      Reads packed [FlashManagerResultHeader_T][payload] records from Flash
 *      Manager, unpacks driver measurement payloads, and populates
 *      HIL_Application_Message_T structures.
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include "result_message_producer.h"
#include "flash_manager/flash_manager.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

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

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */

void RESULT_MESSAGE_PRODUCER_Reset( void )
{
}

Result_Message_Producer_Status_T
RESULT_MESSAGE_PRODUCER_ProduceNextMessage( HIL_Application_Message_T* out_message )
{
    ( void )out_message;
    return RESULT_MESSAGE_PRODUCER_STATUS_NO_DATA_AVAILABLE;
}
