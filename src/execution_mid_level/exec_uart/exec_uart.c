/******************************************************************************
 *  File:       exec_uart.c
 *  Author:     Callum Rafferty
 *  Created:    25-Mar-2025
 *
 *
 *  Description:
 *      Implementation of the mid-level UART driver responsible for sequencing
 *      low-level UART operations for configuration and execution use.
 *
 *      This module:
 *      - sequences configuration and deconfiguration operations,
 *      - bridges execution-level TX requests to the low-level TX ring buffer
 *        and DMA pump,
 *      - copies low-level RX spans into caller-owned storage.
 *
 *  Notes:
 *      - Hardware access, DMA ownership, and buffer ownership remain in the
 *        low-level hw_uart driver.
 *      - RX data is copied from low-level circular buffers into caller storage.
 *      - TX payloads are queued atomically through the low-level driver. If the
 *        full payload cannot fit in the low-level TX ring buffer, transmission
 *        is rejected and no partial payload is queued.
 *      - The execution layer is the sole producer of DUT-facing UART TX data.
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "exec_uart.h"
#include <string.h>

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

/**
 * @brief  Exec-level UART channel state.
 *
 * @note   This state tracks execution-layer lifecycle only.
 *         Hardware state, DMA ownership, RX buffering, and TX ring buffering
 *         remain in the low-level driver.
 */
typedef struct
{
    bool is_configured;
    bool rx_enabled;
    bool tx_enabled;
} ExecUartChannelState_T;

/**-----------------------------------------------------------------------------
 *  Public (global) and Extern Variables
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Private (static) Variables
 *------------------------------------------------------------------------------
 */

/* Exec-level per-channel lifecycle state. */
static ExecUartChannelState_T exec_uart_channel_states[HW_UART_CHANNEL_COUNT];

/**-----------------------------------------------------------------------------
 *  Private (static) Function Prototypes
 *------------------------------------------------------------------------------
 */

static HwUartConfig_T EXEC_UART_Get_Disabled_Config( void );
static inline bool    EXEC_UART_Is_Valid_Channel( HwUartChannel_T channel );

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

/**
 * @brief  Builds a canonical disabled UART configuration.
 *
 * @return A UART configuration structure representing a fully disabled channel.
 *
 * @note   This configuration is used during exec-level deconfiguration to place
 *         the low-level driver and external interface into a known disabled state.
 *
 * @note   Framing fields are assigned valid default values even though they are
 *         not operationally relevant while the interface mode is disabled.
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

/**
 * @brief  Validates that the UART channel index is within range.
 *
 * @note   Used in non-hot-path functions where defensive checks are desirable.
 *         Hot-path functions rely on valid-call contracts for performance.
 */

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

    if ( !EXEC_UART_Is_Valid_Channel( channel ) )
    {
        return false;
    }

    if ( config == NULL )
    {
        return false;
    }

    ExecUartChannelState_T* state = &exec_uart_channel_states[channel];

    /* Stop RX before reconfiguration to avoid modifying LL state while active */
    if ( HW_UART_Rx_Is_Running( channel ) )
    {
        if ( !HW_UART_Rx_Stop( channel ) )
        {
            return false;
        }
    }

    /* Call LL configuration. This validates configuration before applying */
    if ( !HW_UART_Configure_Channel( channel, config ) )
    {
        return false;
    }

    /* Start Rx if enabled */
    if ( config->rx_enabled )
    {
        if ( !HW_UART_Rx_Start( channel ) )
        {
            return false;
        }
    }

    /* Reset exec-level state after successful LL configuration */
    state->is_configured = true;
    state->rx_enabled    = config->rx_enabled;
    state->tx_enabled    = config->tx_enabled;

    return true;
}

bool EXEC_UART_Deconfigure( HwUartChannel_T channel )
{
    /* Apply canonical disabled configuration to force safe hardware state */
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

    return true;
}

bool EXEC_UART_Transmit( HwUartChannel_T channel, const uint8_t* data, uint32_t length_bytes )
{
    if ( !HW_UART_Tx_Load_Buffer( channel, data, length_bytes ) )
    {
        return false;
    }

    return HW_UART_Tx_Trigger( channel );
}

bool EXEC_UART_Read( HwUartChannel_T channel, uint8_t* dest, uint32_t dest_size,
                     uint32_t* bytes_read )
{
    HwUartRxSpans_T spans;
    uint32_t        first_copy      = 0U;
    uint32_t        second_copy     = 0U;
    uint32_t        remaining_space = 0U;

    if ( dest == NULL || bytes_read == NULL )
    {
        return false;
    }

    *bytes_read = 0U;

    if ( dest_size == 0U )
    {
        return true;
    }

    spans = HW_UART_Rx_Peek( channel );

    if ( spans.total_length_bytes == 0U )
    {
        return true;
    }

    /* Copy from first contiguous span of unread data */
    first_copy = spans.first_span.length_bytes;
    if ( first_copy > dest_size )
    {
        first_copy = dest_size;
    }

    if ( first_copy > 0U )
    {
        memcpy( dest, spans.first_span.data, first_copy );
    }

    /* Destination filled by first span, consume and return */
    if ( first_copy == dest_size )
    {
        HW_UART_Rx_Consume( channel, first_copy );
        *bytes_read = first_copy;
        return true;
    }

    /* Copy remaining data from wrapped second span */
    remaining_space = dest_size - first_copy;

    second_copy = spans.second_span.length_bytes;
    if ( second_copy > remaining_space )
    {
        second_copy = remaining_space;
    }

    if ( second_copy > 0U )
    {
        memcpy( &dest[first_copy], spans.second_span.data, second_copy );
    }

    *bytes_read = first_copy + second_copy;

    /* Consume exactly the number of bytes copied from LL buffer */
    if ( *bytes_read > 0U )
    {
        HW_UART_Rx_Consume( channel, *bytes_read );
    }

    return true;
}

bool EXEC_UART_Is_Tx_Complete( HwUartChannel_T channel )
{
    return HW_UART_Is_Tx_Complete( channel );
}