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
#include "logic_expander.h"
#include <string.h>

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

typedef enum
{
    EXEC_UART_STATE_DISABLED = 0U,
    EXEC_UART_STATE_CONFIGURED,
    EXEC_UART_STATE_STARTED
} ExecUartLifecycleState_T;

/**
 * @brief  Exec-level UART channel state.
 *
 * @note   This state tracks execution-layer lifecycle only.
 *         Hardware state, DMA ownership, RX buffering, and TX ring buffering
 *         remain in the low-level driver.
 */
typedef struct
{
    ExecUartLifecycleState_T lifecycle_state;
    bool                     rx_enabled;
    bool                     tx_enabled;
} ExecUartChannelState_T;

typedef struct
{
    HwUartChannel_T      hw_channel;
    LogicExpanderIndex_T expander;
    LogicExpanderPort_T  port;
    uint8_t              mode_0_bit_i;
    uint8_t              mode_1_bit_i;
} ExecUartHardwareMap_T;

/**-----------------------------------------------------------------------------
 *  Public (global) and Extern Variables
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Private (static) Variables
 *------------------------------------------------------------------------------
 */

/* Exec-level per-channel lifecycle state. */
static ExecUartChannelState_T exec_uart_channel_states[EXEC_UART_CHANNEL_COUNT];

static const ExecUartHardwareMap_T exec_uart_hardware_map[EXEC_UART_CHANNEL_COUNT] = {
    [EXEC_UART_CHANNEL_1] = { .hw_channel   = HW_UART_CHANNEL_1,
                              .expander     = LOGIC_EXPANDER_UART_PWR,
                              .port         = LOGIC_EXPANDER_PORT_A,
                              .mode_0_bit_i = 4U,
                              .mode_1_bit_i = 5U },
    [EXEC_UART_CHANNEL_2] = { .hw_channel   = HW_UART_CHANNEL_2,
                              .expander     = LOGIC_EXPANDER_UART_PWR,
                              .port         = LOGIC_EXPANDER_PORT_A,
                              .mode_0_bit_i = 6U,
                              .mode_1_bit_i = 7U },
};

/**-----------------------------------------------------------------------------
 *  Private (static) Function Prototypes
 *------------------------------------------------------------------------------
 */

static inline bool EXEC_UART_Is_Valid_Channel( ExecUartChannel_T channel );
static bool        EXEC_UART_Configuration_Is_Valid( const ExecUartConfig_T* config );
static bool        EXEC_UART_Apply_Static_Hardware_Selection( ExecUartChannel_T       channel,
                                                              ExecUartInterfaceMode_T interface_mode );

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

/**
 * @brief  Validates that the UART channel index is within range.
 *
 * @note   Used in non-hot-path functions where defensive checks are desirable.
 *         Hot-path functions rely on valid-call contracts for performance.
 */

static inline bool EXEC_UART_Is_Valid_Channel( ExecUartChannel_T channel )
{
    return ( ( uint32_t )channel < EXEC_UART_CHANNEL_COUNT );
}

static bool EXEC_UART_Configuration_Is_Valid( const ExecUartConfig_T* config )
{
    if ( config == NULL )
    {
        return false;
    }

    if ( !config->is_enabled )
    {
        return true;
    }

    if ( ( !config->rx_enabled && !config->tx_enabled ) || ( config->baud_rate == 0U ) )
    {
        return false;
    }

    /* Reject unsupported byte-transport framing before deconfiguring a channel
     * or changing the external interface. HW also validates direct callers.
     */
    if ( config->word_length == HW_UART_WORD_LENGTH_9_BITS
         && config->parity == HW_UART_PARITY_NONE )
    {
        return false;
    }

    switch ( config->interface_mode )
    {
        case EXEC_UART_MODE_TTL_3V3:
        case EXEC_UART_MODE_TTL_5V0:
            return config->baud_rate <= 2000000U;

        case EXEC_UART_MODE_RS232:
            return config->baud_rate <= 1000000U;

        case EXEC_UART_MODE_DISABLED:
        default:
            return false;
    }
}

/**
 * @brief  Applies the static hardware selection configuration for the specified UART channel.
 *
 * @param channel The UART channel whose selection lines are to be configured.
 * @param interface_mode The desired interface mode (disabled, TTL 3.3V, TTL 5V, RS232).
 *
 * @return true if the selection sequence is valid for the given mode.
 * @return false if the mode is unsupported or invalid.
 *
 * @note   UART_MODE[0:1] is driven through the UART/PWR logic expander using
 *         the board truth table: 00 disabled, 01 RS-232, 10 TTL 3.3 V,
 *         and 11 TTL 5 V.
 */
static bool EXEC_UART_Apply_Static_Hardware_Selection( ExecUartChannel_T       channel,
                                                       ExecUartInterfaceMode_T interface_mode )
{
    bool mode_0;
    bool mode_1;

    switch ( interface_mode )
    {
        case EXEC_UART_MODE_DISABLED:
            mode_0 = false;
            mode_1 = false;
            break;
        case EXEC_UART_MODE_TTL_3V3:
            mode_0 = true;
            mode_1 = false;
            break;
        case EXEC_UART_MODE_TTL_5V0:
            mode_0 = true;
            mode_1 = true;
            break;
        case EXEC_UART_MODE_RS232:
            mode_0 = false;
            mode_1 = true;
            break;
        default:
            return false;
    }

    const ExecUartHardwareMap_T* hw_map = &exec_uart_hardware_map[channel];

    if ( LOGIC_EXPANDER_Load_Control_Bit( hw_map->expander, hw_map->port, hw_map->mode_0_bit_i,
                                          mode_0 )
         != LOGIC_EXPANDER_STATUS_OK )
    {
        return false;
    }

    if ( LOGIC_EXPANDER_Load_Control_Bit( hw_map->expander, hw_map->port, hw_map->mode_1_bit_i,
                                          mode_1 )
         != LOGIC_EXPANDER_STATUS_OK )
    {
        return false;
    }

    /* Remove this send when a global configuration commit is introduced. */
    return LOGIC_EXPANDER_Send_Control_Bits() == LOGIC_EXPANDER_STATUS_OK;
}

/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */

bool EXEC_UART_Configure_Channel( ExecUartChannel_T channel, const ExecUartConfig_T* config )
{
    if ( !EXEC_UART_Is_Valid_Channel( channel ) || !EXEC_UART_Configuration_Is_Valid( config ) )
    {
        return false;
    }

    const ExecUartHardwareMap_T* hw_map = &exec_uart_hardware_map[channel];
    ExecUartChannelState_T*      state  = &exec_uart_channel_states[channel];

    /*
     * Disabled configuration stops channel activity, disables the peripheral,
     * and applies the safe external-interface state.
     */
    if ( !config->is_enabled )
    {
        if ( state->lifecycle_state == EXEC_UART_STATE_STARTED )
        {
            if ( !EXEC_UART_Stop_Channel( channel ) )
            {
                return false;
            }
        }

        if ( state->lifecycle_state == EXEC_UART_STATE_CONFIGURED )
        {
            if ( !HW_UART_Deconfigure_Channel( hw_map->hw_channel ) )
            {
                return false;
            }
        }

        if ( !EXEC_UART_Apply_Static_Hardware_Selection( channel, EXEC_UART_MODE_DISABLED ) )
        {
            return false;
        }

        state->lifecycle_state = EXEC_UART_STATE_DISABLED;
        state->rx_enabled      = false;
        state->tx_enabled      = false;

        return true;
    }

    /*
     * Active channels must be stopped before being reconfigured.
     */
    if ( state->lifecycle_state == EXEC_UART_STATE_STARTED )
    {
        return false;
    }

    /*
     * Remove the previous peripheral configuration before applying a new one.
     */
    if ( state->lifecycle_state == EXEC_UART_STATE_CONFIGURED )
    {
        if ( !HW_UART_Deconfigure_Channel( hw_map->hw_channel ) )
        {
            return false;
        }

        state->lifecycle_state = EXEC_UART_STATE_DISABLED;
        state->rx_enabled      = false;
        state->tx_enabled      = false;
    }

    /*
     * Disconnect the external interface while the UART peripheral is being
     * configured.
     */
    if ( !EXEC_UART_Apply_Static_Hardware_Selection( channel, EXEC_UART_MODE_DISABLED ) )
    {
        return false;
    }

    const HwUartPeripheralConfig_T hw_config = {
        .baud_rate   = config->baud_rate,
        .word_length = config->word_length,
        .stop_bits   = config->stop_bits,
        .parity      = config->parity,
        .rx_enabled  = config->rx_enabled,
        .tx_enabled  = config->tx_enabled,
    };

    if ( !HW_UART_Configure_Channel( hw_map->hw_channel, &hw_config ) )
    {
        return false;
    }

    if ( !EXEC_UART_Apply_Static_Hardware_Selection( channel, config->interface_mode ) )
    {
        ( void )HW_UART_Deconfigure_Channel( hw_map->hw_channel );
        ( void )EXEC_UART_Apply_Static_Hardware_Selection( channel, EXEC_UART_MODE_DISABLED );
        return false;
    }

    state->lifecycle_state = EXEC_UART_STATE_CONFIGURED;
    state->rx_enabled      = config->rx_enabled;
    state->tx_enabled      = config->tx_enabled;

    return true;
}

bool EXEC_UART_Start_Channel( ExecUartChannel_T channel )
{
    if ( !EXEC_UART_Is_Valid_Channel( channel ) )
    {
        return false;
    }

    ExecUartChannelState_T* state = &exec_uart_channel_states[channel];

    if ( state->lifecycle_state != EXEC_UART_STATE_CONFIGURED )
    {
        return false;
    }

    const HwUartChannel_T hw_channel = exec_uart_hardware_map[channel].hw_channel;

    if ( !HW_UART_Start_Channel( hw_channel ) )
    {
        return false;
    }

    state->lifecycle_state = EXEC_UART_STATE_STARTED;

    return true;
}

bool EXEC_UART_Stop_Channel( ExecUartChannel_T channel )
{
    if ( !EXEC_UART_Is_Valid_Channel( channel ) )
    {
        return false;
    }

    ExecUartChannelState_T* state = &exec_uart_channel_states[channel];

    if ( state->lifecycle_state != EXEC_UART_STATE_STARTED )
    {
        return false;
    }

    const HwUartChannel_T hw_channel = exec_uart_hardware_map[channel].hw_channel;

    if ( !HW_UART_Stop_Channel( hw_channel ) )
    {
        return false;
    }

    state->lifecycle_state = EXEC_UART_STATE_CONFIGURED;

    return true;
}

bool EXEC_UART_Transmit( ExecUartChannel_T channel, const uint8_t* data, uint32_t length_bytes )
{
    const HwUartChannel_T hw_channel = exec_uart_hardware_map[channel].hw_channel;

    if ( !HW_UART_Tx_Load_Buffer( hw_channel, data, length_bytes ) )
    {
        return false;
    }

    return HW_UART_Tx_Trigger( hw_channel );
}

bool EXEC_UART_Read( ExecUartChannel_T channel, uint8_t* dest, uint32_t dest_size,
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

    const HwUartChannel_T hw_channel = exec_uart_hardware_map[channel].hw_channel;

    spans = HW_UART_Rx_Peek( hw_channel );

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
        HW_UART_Rx_Consume( hw_channel, first_copy );
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
        HW_UART_Rx_Consume( hw_channel, *bytes_read );
    }

    return true;
}

bool EXEC_UART_Is_Tx_Complete( ExecUartChannel_T channel )
{
    return HW_UART_Is_Tx_Complete( exec_uart_hardware_map[channel].hw_channel );
}
