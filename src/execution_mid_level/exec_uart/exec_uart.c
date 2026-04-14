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
#include <stddef.h>
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

static ExecUartChannelState_T exec_uart_channel_states[HW_UART_CHANNEL_COUNT];

/**-----------------------------------------------------------------------------
 *  Private (static) Function Prototypes
 *------------------------------------------------------------------------------
 */
static HwUartConfig_T EXEC_UART_Get_Disabled_Config( void );

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

static HwUartConfig_T EXEC_UART_Get_Disabled_Config( void )
{
    HwUartConfig_T config;

    config.interface_mode = HW_UART_MODE_DISABLED;
    config.baud_rate      = 0U;
    config.word_length    = HW_UART_WORD_LENGTH_8_BITS;
    config.stop_bits      = HW_UART_STOP_BITS_1;
    config.parity         = HW_UART_PARITY_NONE;
    config.rx_enabled     = false;
    config.tx_enabled     = false;

    return config;
}

static inline bool EXEC_UART_Is_Valid_Channel( HwUartChannel_T channel )
{
    return ( ( uint32_t )channel < HW_UART_CHANNEL_COUNT );
}

/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */

bool EXEC_UART_Apply_Configuration( HwUartChannel_T channel, const HwUartConfig_T* config )
{

    // Valid Channel Check
    if ( !EXEC_UART_Is_Valid_Channel( channel ) )
    {
        return false;
    }

    // Ensure Config Exists
    if ( config == NULL )
    {
        return false;
    }

    ExecUartChannelState_T* state = &exec_uart_channel_states[channel];

    // Stop Rx if running
    if ( HW_UART_Rx_Is_Running( channel ) )
    {
        if ( !HW_UART_Rx_Stop( channel ) )
        {
            return false;
        }
    }

    // Call LL configuration. This validates configuration before applying
    if ( !HW_UART_Configure_Channel( channel, config ) )
    {
        return false;
    }

    // Start Rx if enabled
    if ( config->rx_enabled )
    {
        if ( !HW_UART_Rx_Start( channel ) )
        {
            return false;
        }
    }

    // Reset internal exec state
    state->is_configured = true;
    state->rx_enabled    = config->rx_enabled;
    state->tx_enabled    = config->tx_enabled;
    state->tx_staged     = false;

    return true;
}

bool EXEC_UART_Deconfigure( HwUartChannel_T channel )
{
    HwUartConfig_T disabled_config = EXEC_UART_Get_Disabled_Config();

    if ( !EXEC_UART_Is_Valid_Channel( channel ) )
    {
        return false;
    }

    if ( HW_UART_Rx_Is_Running( channel ) )
    {
        if ( !HW_UART_Rx_Stop( channel ) )
        {
            return false;
        }
    }

    if ( !HW_UART_Configure_Channel( channel, &disabled_config ) )
    {
        return false;
    }

    exec_uart_channel_states[channel].is_configured = false;
    exec_uart_channel_states[channel].rx_enabled    = false;
    exec_uart_channel_states[channel].tx_enabled    = false;
    exec_uart_channel_states[channel].tx_staged     = false;

    return true;
}

bool EXEC_UART_Transmit( HwUartChannel_T channel, const uint8_t* data, uint32_t length_bytes )
{
    if ( exec_uart_channel_states[channel].tx_staged )
    {
        return false;
    }

    if ( !HW_UART_Tx_Load_Buffer( channel, data, length_bytes ) )
    {
        return false;
    }

    exec_uart_channel_states[channel].tx_staged = true;

    if ( !HW_UART_Tx_Trigger( channel ) )
    {
        return false;
    }

    exec_uart_channel_states[channel].tx_staged = false;
    return true;
}
