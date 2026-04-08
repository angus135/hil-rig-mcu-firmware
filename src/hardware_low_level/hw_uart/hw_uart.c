/******************************************************************************
 *  File:       hw_uart.c
 *  Author:     Callum Rafferty
 *  Created:    16-Dec-2025
 *
 *  Description:
 *      <Short description of the module's purpose and responsibilities>
 *
 *  Notes:
 *      <Any design notes, dependencies, or assumptions go here>
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#ifdef TEST_BUILD
#include "tests/hw_uart_mocks.h"
#else
#include "usart.h"
#include "stm32f446xx.h"
#endif
#include "hw_uart.h"
#include "rtos_config.h"
#include <stdint.h>
#include <stdbool.h>

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */
// UART Hardware mapping definitions
#define HW_UART_CH1_USART USART1
#define HW_UART_CH1_DMA_RX_STREAM DMA2_Stream2
#define HW_UART_CH1_DMA_TX_STREAM DMA2_Stream7

#define HW_UART_CH2_USART USART2
#define HW_UART_CH2_DMA_RX_STREAM DMA1_Stream5
#define HW_UART_CH2_DMA_TX_STREAM DMA1_Stream6

// TODO - These pin definitions are placeholders, need to be updated based on actual design
// May change from GPIO to something else depending on how the mode/voltage selection is implemented
#define HW_UART_CH1_MODE_SEL0_LINE GPIO_PIN_0
#define HW_UART_CH1_MODE_SEL1_LINE GPIO_PIN_1
#define HW_UART_CH1_VOLT_SEL0_LINE GPIO_PIN_2
#define HW_UART_CH1_VOLT_SEL1_LINE GPIO_PIN_3

#define HW_UART_CH2_MODE_SEL0_LINE GPIO_PIN_4
#define HW_UART_CH2_MODE_SEL1_LINE GPIO_PIN_5
#define HW_UART_CH2_VOLT_SEL0_LINE GPIO_PIN_6
#define HW_UART_CH2_VOLT_SEL1_LINE GPIO_PIN_7

// Size of the receive buffer for each UART channel,
// can be adjusted based on expected data rates and memory constraints
#define HW_UART_RX_BUFFER_SIZE                                                                     \
    2048U  // Must be a power of 2 for the circular buffer management to work correctly
#if ( ( HW_UART_RX_BUFFER_SIZE & ( HW_UART_RX_BUFFER_SIZE - 1U ) ) != 0U )
#error "HW_UART_RX_BUFFER_SIZE must be a power of 2"
#endif

// Number of UART channels supported by the hardware
#define HW_UART_CHANNEL_COUNT 2U

/**-----------------------------------------------------------------------------
 *  Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

/* To be implemented in the future.
typedef enum
{
    HW_UART_FAULT_NONE = 0U,

    // Configuration faults
    HW_UART_FAULT_INVALID_CONFIGURATION = ( 1U << 0 ),

    // RX faults
    HW_UART_FAULT_RX_OVERRUN  = ( 1U << 1 ),
    HW_UART_FAULT_RX_FRAMING  = ( 1U << 2 ),
    HW_UART_FAULT_RX_PARITY   = ( 1U << 3 ),
    HW_UART_FAULT_RX_NOISE    = ( 1U << 4 ),
    HW_UART_FAULT_RX_OVERFLOW = ( 1U << 5 ),

    // DMA faults
    HW_UART_FAULT_DMA_ERROR = ( 1U << 6 ),

    // TX faults
    HW_UART_FAULT_TX_BUSY       = ( 1U << 7 ),
    HW_UART_FAULT_TX_INCOMPLETE = ( 1U << 8 )

} HwUartFaultMask_T;
*/

typedef enum
{
    HW_UART_MODE_DISABLED = 0,  // Default state, no UART functionality
    HW_UART_MODE_TTL_3V3,       // Standard TTL logic levels (0V for LOW, 3.3V for HIGH)
    HW_UART_MODE_TTL_5V0,       // Standard TTL logic levels (0V for LOW, 5V for HIGH)
    HW_UART_MODE_RS232          // RS-232 line driver interface
} HwUartInterfaceMode_T;

typedef enum
{
    HW_UART_CHANNEL_1 = 0,
    HW_UART_CHANNEL_2 = 1,
} HwUartChannel_T;

typedef enum
{
    HW_UART_PARITY_NONE = 0,
    HW_UART_PARITY_ODD  = 1,
    HW_UART_PARITY_EVEN = 2
} HwUartParity_T;

typedef enum
{
    HW_UART_WORD_LENGTH_8_BITS = 8,
    HW_UART_WORD_LENGTH_9_BITS = 9
} HwUartWordLength_T;

typedef enum
{
    HW_UART_STOP_BITS_1 = 1,
    HW_UART_STOP_BITS_2 = 2
} HwUartStopBits_T;

typedef struct
{
    HwUartInterfaceMode_T interface_mode;  // Determines the Uart interface type and voltage levels

    uint32_t           baud_rate;
    HwUartWordLength_T word_length;
    HwUartStopBits_T   stop_bits;
    HwUartParity_T     parity;

    bool rx_enabled;  // Enable reception functionality
    bool tx_enabled;  // Enable transmission functionality
    // Currently not supporting half-duplex mode.
} HwUartConfig_T;

typedef struct
{
    uint32_t rx_read_index;
    uint32_t latched_faults;  // Bitmask implementation left for a later date when faults are
                              // implemented.

    bool is_configured;

    bool rx_running;
    bool tx_running;
} HwUartRuntimeState_T;

typedef struct
{
    HwUartConfig_T       config;
    HwUartRuntimeState_T runtime;
    uint8_t              rx_buffer[HW_UART_RX_BUFFER_SIZE];
} HwUartChannelState_T;

typedef struct
{
    const uint8_t* data;
    uint32_t       length_bytes;
} HwUartRxSpan_T;

typedef struct
{
    HwUartRxSpan_T first_span;
    HwUartRxSpan_T second_span;
    uint32_t       total_length_bytes;
} HwUartRxSpans_T;

typedef struct
{
    USART_TypeDef*      uart_instance;
    DMA_Stream_TypeDef* rx_dma_stream;
    DMA_Stream_TypeDef* tx_dma_stream;
    UART_HandleTypeDef* uart_handle;  // Used for configuration stage HAL calls.
} HwUartHardwareMap_T;

/**-----------------------------------------------------------------------------
 *  Public (global) and Extern Variables
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Private (static) Variables
 *------------------------------------------------------------------------------
 */
static HwUartChannelState_T uart_channel_states[HW_UART_CHANNEL_COUNT];

static const HwUartHardwareMap_T uart_hardware_map[HW_UART_CHANNEL_COUNT] = {
    [HW_UART_CHANNEL_1] = { .uart_instance = HW_UART_CH1_USART,
                            .rx_dma_stream = HW_UART_CH1_DMA_RX_STREAM,
                            .tx_dma_stream = HW_UART_CH1_DMA_TX_STREAM,
                            .uart_handle   = &huart1 },
    [HW_UART_CHANNEL_2] = { .uart_instance = HW_UART_CH2_USART,
                            .rx_dma_stream = HW_UART_CH2_DMA_RX_STREAM,
                            .tx_dma_stream = HW_UART_CH2_DMA_TX_STREAM,
                            .uart_handle   = &huart2 } };

/**-----------------------------------------------------------------------------
 *  Private (static) Function Definitions
 *------------------------------------------------------------------------------
 */
static bool HW_UART_CONFIGURATION_VALIDATION( const HwUartConfig_T* config )
{
    if ( config == NULL )
    {
        return false;
    }

    if ( config->interface_mode == HW_UART_MODE_DISABLED )
    {
        return ( !config->rx_enabled && !config->tx_enabled && config->baud_rate == 0U );
    }

    if ( !config->rx_enabled && !config->tx_enabled )
    {
        return false;
    }

    if ( config->baud_rate == 0U )
    {
        return false;
    }

    if ( config->word_length != HW_UART_WORD_LENGTH_8_BITS
         && config->word_length != HW_UART_WORD_LENGTH_9_BITS )
    {
        return false;
    }

    if ( config->stop_bits != HW_UART_STOP_BITS_1 && config->stop_bits != HW_UART_STOP_BITS_2 )
    {
        return false;
    }

    if ( config->parity != HW_UART_PARITY_NONE && config->parity != HW_UART_PARITY_EVEN
         && config->parity != HW_UART_PARITY_ODD )
    {
        return false;
    }

    bool valid_baud = false;

    switch ( config->interface_mode )
    {
        case HW_UART_MODE_TTL_3V3:
        case HW_UART_MODE_TTL_5V0:
            valid_baud = ( config->baud_rate <= 2000000U );
            break;

        case HW_UART_MODE_RS232:
            valid_baud = ( config->baud_rate <= 1000000U );
            break;

        case HW_UART_MODE_DISABLED:
            // Handled by earlier check, but included here for completeness
            // Should never reach this point
        default:
            return false;
    }

    return valid_baud;
}

/** -----------------------------------------------------------------------------
 * @brief Helper function to calculate the number of unread bytes in the circular RX buffer.
 * @param read_index The current read index in the RX buffer.
 * @param write_index The current write index in the RX buffer.
 * @return The number of unread bytes available in the RX buffer.
 * inline for performance, as this will be called frequently in the execution path
 * Note: This function assumes that the buffer size is a power of 2, which allows for efficient
 * wrapping using bitwise operations.
 */
static inline uint32_t HW_UART_UNREAD_BYTES_COUNT_HELPER( uint32_t read_index,
                                                          uint32_t write_index )
{
    return ( write_index - read_index ) & ( HW_UART_RX_BUFFER_SIZE - 1U );
}

/** -----------------------------------------------------------------------------
 * @brief Helper function to advance the index in a circular buffer.
 * @param current_index The current index.
 * @param advance_by The number of positions to advance.
 * @return The new index after advancement.
 * inline for performance, as this will be called frequently in the execution path
 *
 * Note: This function assumes that the buffer size is a power of 2, which allows for efficient
 * wrapping using bitwise operations.
 */
static inline uint32_t HW_UART_ADVANCE_INDEX_HELPER( uint32_t current_index, uint32_t advance_by )
{
    // Power of 2 buffer wrap using bitmask.
    return ( current_index + advance_by ) & ( HW_UART_RX_BUFFER_SIZE - 1U );
}

static bool HW_UART_APPLY_STATIC_HARDWARE_SELECTION( HwUartChannel_T       channel,
                                                     HwUartInterfaceMode_T interface_mode )
{
    switch ( interface_mode )
    {
        case HW_UART_MODE_DISABLED:
            // 1: Set MODE_SEL[0:1] to [0, 0] to select the disabled mode

            // 2: Set VOLT_SEL[0:1] to [0, 0] to set TTL_VCCB to 0V
            return true;
        case HW_UART_MODE_TTL_3V3:

            // 1: Set MODE_SEL[0:1] to [0, 0] to temporarily select the disabled mode while changing
            // voltage levels. This prevents potential damage to the hardware from voltage level
            // changes while the UART is active.

            // 2: Set VOLT_SEL[0:1] to [0, 1] to set TTL_VCCB to 3.3V

            // 3: Set MODE_SEL[0:1] to [0, 1] to select the TTL mode with the new voltage levels

            return true;
        case HW_UART_MODE_TTL_5V0:
            // 1: Set MODE_SEL[0:1] to [0, 0] to temporarily select the disabled mode while changing
            // voltage levels. This prevents potential damage to the hardware from voltage level
            // changes while the UART is active.

            // 2: Set VOLT_SEL[0:1] to [1, 0] to set TTL_VCCB to 5V

            // 3: Set MODE_SEL[0:1] to [0, 1] to select the TTL mode with the new voltage levels
            return true;
        case HW_UART_MODE_RS232:
            // 1: Set MODE_SEL[0:1] to [0, 0] to temporarily select the disabled mode while changing
            // voltage levels. This prevents potential damage to the hardware from voltage level
            // changes while the UART is active.

            // 2: Set VOLT_SEL[0:1] to [0, 0] to set TTL_VCCB to 0V, as the RS-232 line driver will
            // generate the necessary voltage levels for the RS-232 interface

            // 3: Set MODE_SEL[0:1] to [1, 0] to select the RS-232 mode
            return true;
        default:
            return false;
    }
}
/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */

bool HW_UART_CONFIGURE_CHANNEL( HwUartChannel_T channel, const HwUartConfig_T* config )
{
    // Validate channel number and configuration parameters before applying settings to the hardware
    if ( channel >= HW_UART_CHANNEL_COUNT )
    {
        return false;
    }

    if ( !HW_UART_CONFIGURATION_VALIDATION( config ) )
    {
        return false;
    }

    // Store the configuration in the channel state for future reference
    HwUartChannelState_T* state = &uart_channel_states[channel];
    state->config               = *config;

    // Apply the static hardware selection states (mode/voltage) based on the interface mode
    // specified in the configuration to the appropriate hardware lines (GPIOs or other control
    // interfaces)
    if ( !HW_UART_APPLY_STATIC_HARDWARE_SELECTION( channel, config->interface_mode ) )
    {
        return false;
    }

    // Reset runtime state variables to their default values
    state->runtime.latched_faults = 0U;
    state->runtime.rx_read_index  = 0U;
    state->runtime.rx_running     = false;
    state->runtime.tx_running     = false;

    // Mark the channel as configured after successfully applying the configuration to the hardware
    state->runtime.is_configured = true;

    return true;
}

bool HW_UART_RX_START( HwUartChannel_T channel )
{
    if ( channel >= HW_UART_CHANNEL_COUNT )
    {
        return false;
    }

    HwUartChannelState_T*      state  = &uart_channel_states[channel];
    const HwUartHardwareMap_T* hw_map = &uart_hardware_map[channel];
    UART_HandleTypeDef*        huart  = hw_map->uart_handle;

    if ( !state->runtime.is_configured || !state->config.rx_enabled )
    {
        return false;
    }

    if ( state->runtime.rx_running )
    {
        return false;
    }

    // Reset the RX buffer read index to start reading from the beginning of the buffer
    state->runtime.rx_read_index = 0U;
    // Clear the entire RX buffer to ensure it doesn't contain stale data
    for ( uint32_t i = 0U; i < HW_UART_RX_BUFFER_SIZE; i++ )
    {
        state->rx_buffer[i] = 0U;
    }

    huart->Init.BaudRate = state->config.baud_rate;

    switch ( state->config.word_length )
    {
        case HW_UART_WORD_LENGTH_8_BITS:
            huart->Init.WordLength = UART_WORDLENGTH_8B;
            break;
        case HW_UART_WORD_LENGTH_9_BITS:
            huart->Init.WordLength = UART_WORDLENGTH_9B;
            break;
        default:
            return false;  // Should never reach here due to prior validation
    }

    switch ( state->config.stop_bits )
    {
        case HW_UART_STOP_BITS_1:
            huart->Init.StopBits = UART_STOPBITS_1;
            break;
        case HW_UART_STOP_BITS_2:
            huart->Init.StopBits = UART_STOPBITS_2;
            break;
        default:
            return false;  // Should never reach here due to prior validation
    }

    switch ( state->config.parity )
    {
        case HW_UART_PARITY_NONE:
            huart->Init.Parity = UART_PARITY_NONE;
            break;
        case HW_UART_PARITY_EVEN:
            huart->Init.Parity = UART_PARITY_EVEN;
        case HW_UART_PARITY_ODD:
            huart->Init.Parity = UART_PARITY_ODD;
            break;
        default:
            return false;  // Should never reach here due to prior validation
    }

    huart->Init.Mode = ( state->config.tx_enabled ? UART_MODE_TX : 0U )
                       | ( state->config.rx_enabled ? UART_MODE_RX : 0U );

    if ( HAL_UART_Init( huart ) != HAL_OK )
    {
        return false;
    }

    if ( HAL_UART_Receive_DMA( huart, state->rx_buffer, HW_UART_RX_BUFFER_SIZE ) != HAL_OK )
    {
        return false;
    }

    state->runtime.rx_running = true;
    return true;
}

/**
 * @brief  Peeks at the current unread data in the RX buffer for the specified UART channel,
 *         returning one or two spans of contiguous data.
 *
 * @param channel The UART channel to peek at.
 * @return HwUartRxSpans_T
 *              A structure containing one or two spans of contiguous data available in the RX
 *              buffer, along with the total length of unread data.
 */
HwUartRxSpans_T HW_UART_RX_PEEK( HwUartChannel_T channel )
{
    // Cache reused values in local variables for performance, as this function will be called
    // frequently in the execution path
    HwUartChannelState_T*      state           = &uart_channel_states[channel];
    const HwUartHardwareMap_T* hw_map          = &uart_hardware_map[channel];
    uint8_t*                   rx_buffer       = state->rx_buffer;
    uint32_t                   read_index      = state->runtime.rx_read_index;
    uint32_t                   dma_remaining   = hw_map->rx_dma_stream->NDTR;
    uint32_t                   dma_write_index = HW_UART_RX_BUFFER_SIZE - dma_remaining;

    if ( dma_write_index == HW_UART_RX_BUFFER_SIZE )
    {
        dma_write_index = 0U;
    }

    uint32_t unread_bytes = HW_UART_UNREAD_BYTES_COUNT_HELPER( read_index, dma_write_index );

    if ( unread_bytes == 0U )
    {
        // No data available
        return ( HwUartRxSpans_T ){ .first_span  = { .data = &rx_buffer[0], .length_bytes = 0U },
                                    .second_span = { .data = &rx_buffer[0], .length_bytes = 0U },
                                    .total_length_bytes = 0U };
    }

    if ( dma_write_index >= read_index )
    {
        // Data does not wrap around the end of the buffer
        return ( HwUartRxSpans_T ){
            .first_span         = { .data = &rx_buffer[read_index], .length_bytes = unread_bytes },
            .second_span        = { .data = &rx_buffer[0], .length_bytes = 0U },
            .total_length_bytes = unread_bytes };
    }
    // else case:

    // Data wraps around the end of the buffer, need to provide two spans
    uint32_t first_span_length  = HW_UART_RX_BUFFER_SIZE - read_index;
    uint32_t second_span_length = unread_bytes - first_span_length;

    return ( HwUartRxSpans_T ){
        .first_span         = { .data = &rx_buffer[read_index], .length_bytes = first_span_length },
        .second_span        = { .data = &rx_buffer[0], .length_bytes = second_span_length },
        .total_length_bytes = unread_bytes };
}

void HW_UART_RX_CONSUME( HwUartChannel_T channel, uint32_t bytes_to_consume )
{
    HwUartChannelState_T* state = &uart_channel_states[channel];

    // Advance the read index by the specified number of bytes, wrapping around the buffer as needed
    state->runtime.rx_read_index =
        HW_UART_ADVANCE_INDEX_HELPER( state->runtime.rx_read_index, bytes_to_consume );
}