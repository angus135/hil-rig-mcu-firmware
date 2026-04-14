/******************************************************************************
 *  File:       exec_uart.c
 *  Author:     Callum Rafferty
 *  Created:    25-Mar-2025
 *
 *  Description:
 *      Mid-level UART driver responsible for sequencing low-level UART
 *      operations for configuration and execution-facing use.
 *
 *  Notes:
 *      This layer does not access hardware directly.
 *      It coordinates use of the low-level hw_uart driver.
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */
#include <stdint.h>
#include <stdbool.h>

#include "exec_uart.h"
#include "hw_uart.h"

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
    bool is_initialised;
    bool is_configured;
    bool rx_enabled;
    bool tx_enabled;
    bool tx_staged;
} ExecUartChannelState_T;

/**-----------------------------------------------------------------------------
 *  Public (global) and Extern Variables
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Private (static) Variables
 *------------------------------------------------------------------------------
 */

static ExecUartChannelState_T g_exec_uart_channel_state[HW_UART_CHANNEL_3 + 1U];

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

bool EXEC_UART_Apply_Configuration( HwUartChannel_T channel, const HwUartConfig_T* config )
{
    ( void )channel;
    ( void )config;

    return false;
}

bool EXEC_UART_Deconfigure( HwUartChannel_T channel )
{
    ( void )channel;

    return false;
}