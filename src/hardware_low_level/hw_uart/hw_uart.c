/******************************************************************************
 *  File:       hw_uart.c
 *  Author:     Callum Rafferty
 *  Created:    16-Dec-2025
 *
 *  Description:
 *      Low-level UART driver for DUT-facing UART channels in the HIL-RIG.
 *
 *      This module owns:
 *      - channel configuration and validation,
 *      - fixed hardware mapping for UART peripherals and DMA streams,
 *      - static hardware interface selection sequencing,
 *      - DMA-backed circular RX buffering,
 *      - lightweight access to unread RX data through zero-copy spans.
 *
 *      Non-execution stage functions such as configuration and RX startup may use HAL
 *      to simplify peripheral initialisation. Execution-path RX access remains
 *      lightweight and avoids unnecessary copying.
 *
 *  Notes:
 *      - The low-level driver owns the DMA circular buffer and its management state.
 *      - Higher layers may inspect unread data through transient span views and
 *        must copy data into stable storage if persistence is required.
 *      - Higher layers must explicitly report consumption after processing.
 *      - This module does not define execution result storage or tick semantics.
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
#include <string.h>

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */
// UART Hardware mapping definitions
#define HW_UART_CH1_USART USART6
#define HW_UART_CH1_DMA_RX_STREAM DMA2_Stream1
#define HW_UART_CH1_DMA_TX_STREAM DMA2_Stream6

#define HW_UART_CH2_USART USART2
#define HW_UART_CH2_DMA_RX_STREAM DMA1_Stream5
#define HW_UART_CH2_DMA_TX_STREAM DMA1_Stream6

#define HW_UART_CH3_USART USART3
#define HW_UART_CH3_DMA_RX_STREAM DMA1_Stream1
#define HW_UART_CH3_DMA_TX_STREAM DMA1_Stream3

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
    4096U  // Must be a power of 2 for the circular buffer management to work correctly
#if ( ( HW_UART_RX_BUFFER_SIZE & ( HW_UART_RX_BUFFER_SIZE - 1U ) ) != 0U )
#error "HW_UART_RX_BUFFER_SIZE must be a power of 2"
#endif

#define HW_UART_TX_BUFFER_SIZE 256U

// Number of UART channels supported by the hardware
#define HW_UART_CHANNEL_COUNT 3U  // Update this  to 2U when removing console channel

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

/**
 * @brief  Defines the hardware control lines used to select the external UART interface mode.
 *
 * @note   These lines represent board-level control signals such as MODE_SEL and
 *         VOLT_SEL. They are fixed per channel and are used during configuration
 *         to place the external interface hardware into the required electrical mode.
 *
 * @note   The actual GPIO control implementation is not yet integrated. This structure serves as a
 * placeholder defining the required control lines and sequencing for future implementation.
 */
typedef struct
{
    uint16_t mode_sel0_line;  // GPIO pin number for MODE_SEL bit 0
    uint16_t mode_sel1_line;  // GPIO pin number for MODE_SEL bit 1
    uint16_t volt_sel0_line;  // GPIO pin number for VOLT_SEL bit 0
    uint16_t volt_sel1_line;  // GPIO pin number for VOLT_SEL bit 1
} HwUartSelectionLines_T;

/**
 * @brief  Stores low-level runtime state associated with a UART channel.
 *
 * @note   This structure is owned entirely by the low-level driver and tracks
 *         circular buffer consumption state, latched faults, configuration status,
 *         and whether RX or TX operation is currently active.
 */
typedef struct
{
    uint32_t rx_read_index;
    uint32_t latched_faults;  // Bitmask implementation left for a later date when faults are
                              // implemented.

    uint32_t tx_length_bytes;

    bool is_configured_and_initialised;
    bool rx_running;
    bool tx_loaded;
    bool tx_running;

} HwUartRuntimeState_T;

/**
 * @brief  Aggregates all low-level state for a UART channel.
 *
 * @note   This includes the stored channel configuration, runtime state, and the
 *         DMA-backed circular RX buffer owned by the low-level driver.
 */
typedef struct
{
    HwUartConfig_T       config;
    HwUartRuntimeState_T runtime;
    uint8_t              rx_buffer[HW_UART_RX_BUFFER_SIZE];
    uint8_t              tx_buffer[HW_UART_TX_BUFFER_SIZE];
} HwUartChannelState_T;

/**
 * @brief  Defines the fixed hardware resources associated with a UART channel.
 *
 * @note   This structure maps each logical channel to its UART peripheral,
 *         associated DMA streams, and HAL handle used during non-hot-path
 *         initialisation and startup.
 */
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

/* Driver owned per-channel runtime and buffer storage */
static HwUartChannelState_T uart_channel_states[HW_UART_CHANNEL_COUNT];

/* Fixed board-level mapping from logical UART channels to MCU peripherals */
static const HwUartHardwareMap_T uart_hardware_map[HW_UART_CHANNEL_COUNT] = {
    [HW_UART_CHANNEL_1] = { .uart_instance = HW_UART_CH1_USART,
                            .rx_dma_stream = HW_UART_CH1_DMA_RX_STREAM,
                            .tx_dma_stream = HW_UART_CH1_DMA_TX_STREAM,
                            .uart_handle   = &huart6 },
    [HW_UART_CHANNEL_2] = { .uart_instance = HW_UART_CH2_USART,
                            .rx_dma_stream = HW_UART_CH2_DMA_RX_STREAM,
                            .tx_dma_stream = HW_UART_CH2_DMA_TX_STREAM,
                            .uart_handle   = &huart2 },
    [HW_UART_CHANNEL_3] = { .uart_instance = HW_UART_CH3_USART,
                            .rx_dma_stream = HW_UART_CH3_DMA_RX_STREAM,
                            .tx_dma_stream = HW_UART_CH3_DMA_TX_STREAM,
                            .uart_handle   = &huart3 } };

/* Fixed board-level mapping from logical UART channels to interface selection lines */
static const HwUartSelectionLines_T uart_selection_lines[HW_UART_CHANNEL_COUNT] = {
    [HW_UART_CHANNEL_1] = { .mode_sel0_line = HW_UART_CH1_MODE_SEL0_LINE,
                            .mode_sel1_line = HW_UART_CH1_MODE_SEL1_LINE,
                            .volt_sel0_line = HW_UART_CH1_VOLT_SEL0_LINE,
                            .volt_sel1_line = HW_UART_CH1_VOLT_SEL1_LINE },
    [HW_UART_CHANNEL_2] = { .mode_sel0_line = HW_UART_CH2_MODE_SEL0_LINE,
                            .mode_sel1_line = HW_UART_CH2_MODE_SEL1_LINE,
                            .volt_sel0_line = HW_UART_CH2_VOLT_SEL0_LINE,
                            .volt_sel1_line = HW_UART_CH2_VOLT_SEL1_LINE } };

/**-----------------------------------------------------------------------------
 *  Private (static) Function Definitions
 *------------------------------------------------------------------------------
 */

/**
 * @brief  Validates a UART channel configuration before it is stored or applied.
 *
 * @param  config Pointer to the configuration to validate.
 *
 * @return true if the configuration is internally consistent and supported.
 * @return false if the configuration is null, incomplete, or requests unsupported
 *         UART framing or interface settings.
 *
 * @note   This function is used only in the non-hot-path configuration stage.
 *
 * @note   Validation ensures that later startup code can assume the stored
 *         configuration is well-formed and can focus on applying it to hardware.
 */
static bool HW_UART_Configuration_Is_Valid( const HwUartConfig_T* config )
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

/**
 * @brief  Computes the number of unread bytes in the DMA-backed circular RX buffer.
 *
 * @param  read_index   Current read index maintained by the LL driver.
 * @param  write_index  Current write index derived from the DMA stream.
 *
 * @return Number of unread bytes available for consumption.
 *
 * @note   This function implements circular buffer distance using power-of-2 wrapping:
 *         (write_index - read_index) & (buffer_size - 1).
 *
 * @note   The buffer size must be a power of 2. This allows wrapping via a bitmask
 *         instead of a modulo operation for improved performance in the hot path.
 *
 * @note   This function is used in the execution path and is marked inline to minimise
 *         overhead.
 *
 * @note   The returned value represents a snapshot and may change as DMA continues
 *         writing to the buffer.
 */
static inline uint32_t HW_UART_Unread_Bytes_Count_Helper( uint32_t read_index,
                                                          uint32_t write_index )
{
    return ( write_index - read_index ) & ( HW_UART_RX_BUFFER_SIZE - 1U );
}

/**
 * @brief  Advances an index within the DMA-backed circular RX buffer.
 *
 * @param  current_index Current index maintained by the LL driver.
 * @param  advance_by    Number of positions to advance.
 *
 * @return Updated index after advancement with circular wrap applied.
 *
 * @note   This function performs circular buffer wrapping using a power-of-2 mask:
 *         (current_index + advance_by) & (buffer_size - 1).
 *
 * @note   The buffer size must be a power of 2. This enables efficient wrapping
 *         using a bitmask instead of a modulo operation.
 *
 * @note   This function is used in the execution path and is marked inline to
 *         minimise overhead.
 *
 * @note   The caller is responsible for ensuring that advance_by does not exceed
 *         the number of unread bytes, as this function does not perform bounds checking.
 */
static inline uint32_t HW_UART_Advance_Index_Helper( uint32_t current_index, uint32_t advance_by )
{
    // Power of 2 buffer wrap using bitmask.
    return ( current_index + advance_by ) & ( HW_UART_RX_BUFFER_SIZE - 1U );
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
 * @note   This function is responsible for configuring the external hardware selection lines
 *         (e.g. MODE_SEL[1:0], VOLT_SEL[1:0]) associated with the UART channel. These lines control
 *         the electrical interface presented to the DUT, including voltage levels and interface
 *         type.
 *
 * @note   The intended behaviour is to follow a safe sequencing strategy:
 *         - temporarily force the interface into a disabled state,
 *         - update voltage selection lines,
 *         - then enable the required interface mode.
 *
 * @note   This function belongs to the low-level driver because it applies
 *         deterministic hardware-facing configuration prior to enabling UART operation.
 *
 * @note   The actual GPIO or control-line implementation is not yet integrated. The current
 *         implementation serves as a placeholder defining the required sequencing and structure.
 *         Physical line driving will be added in a future revision.
 */
static bool HW_UART_Apply_Static_Hardware_Selection( HwUartChannel_T       channel,
                                                     HwUartInterfaceMode_T interface_mode )
{
    if ( channel == HW_UART_CHANNEL_3 )
    {
        // Console for Nucleo board does not need selection
        return true;
    }
    switch ( interface_mode )
    {
        case HW_UART_MODE_DISABLED:
            // 1: Set MODE_SEL[0:1] to [0, 0] to select the disabled mode
            // uart_selection_lines[channel].mode_sel0_line = 0U;
            // uart_selection_lines[channel].mode_sel1_line = 0U;
            // 2: Set VOLT_SEL[0:1] to [0, 0] to set TTL_VCCB to 0V
            // uart_selection_lines[channel].volt_sel0_line = 0U;
            // uart_selection_lines[channel].volt_sel1_line = 0U;
            return true;
        case HW_UART_MODE_TTL_3V3:

            // 1: Set MODE_SEL[0:1] to [0, 0] to temporarily select the disabled mode while changing
            // voltage levels. This prevents potential damage to the hardware from voltage level
            // changes while the UART is active.
            // uart_selection_lines[channel].mode_sel0_line = 0U;
            // uart_selection_lines[channel].mode_sel1_line = 0U;

            // 2: Set VOLT_SEL[0:1] to [0, 1] to set TTL_VCCB to 3.3V
            // uart_selection_lines[channel].volt_sel0_line = 0U;
            // uart_selection_lines[channel].volt_sel1_line = 1U;

            // 3: Set MODE_SEL[0:1] to [0, 1] to select the TTL mode with the new voltage levels
            // uart_selection_lines[channel].mode_sel0_line = 0U;
            // uart_selection_lines[channel].mode_sel1_line = 1U;

            return true;
        case HW_UART_MODE_TTL_5V0:
            // 1: Set MODE_SEL[0:1] to [0, 0] to temporarily select the disabled mode while changing
            // voltage levels. This prevents potential damage to the hardware from voltage level
            // changes while the UART is active.
            // uart_selection_lines[channel].mode_sel0_line = 0U;
            // uart_selection_lines[channel].mode_sel1_line = 0U;

            // 2: Set VOLT_SEL[0:1] to [1, 0] to set TTL_VCCB to 5V
            // uart_selection_lines[channel].volt_sel0_line = 1U;
            // uart_selection_lines[channel].volt_sel1_line = 0U;

            // 3: Set MODE_SEL[0:1] to [0, 1] to select the TTL mode with the new voltage levels
            // uart_selection_lines[channel].mode_sel0_line = 0U;
            // uart_selection_lines[channel].mode_sel1_line = 1U;
            return true;
        case HW_UART_MODE_RS232:
            // 1: Set MODE_SEL[0:1] to [0, 0] to temporarily select the disabled mode while changing
            // voltage levels. This prevents potential damage to the hardware from voltage level
            // changes while the UART is active.
            // uart_selection_lines[channel].mode_sel0_line = 0U;
            // uart_selection_lines[channel].mode_sel1_line = 0U;

            // 2: Set VOLT_SEL[0:1] to [0, 0] to set TTL_VCCB to 0V, as the RS-232 line driver will
            // generate the necessary voltage levels for the RS-232 interface
            // uart_selection_lines[channel].volt_sel0_line = 0U;
            // uart_selection_lines[channel].volt_sel1_line = 0U;

            // 3: Set MODE_SEL[0:1] to [1, 0] to select the RS-232 mode
            // uart_selection_lines[channel].mode_sel0_line = 1U;
            // uart_selection_lines[channel].mode_sel1_line = 0U;
            return true;
        default:
            return false;
    }
}

/**
 * @brief  Applies the stored UART configuration to the hardware peripheral and performs one-time
 *         peripheral initialisation for the specified channel.
 *
 * @param  channel The UART channel to initialise.
 *
 * @return true if the UART peripheral was successfully initialised.
 * @return false if the stored configuration cannot be translated to HAL settings or if
 *         HAL_UART_Init() fails.
 *
 * @note   This function is part of the non-hot-path configuration stage and must only be called
 *         after a valid channel configuration has been stored.
 *
 * @note   This function applies framing and mode settings from the low-level driver owned channel
 *         configuration to the underlying HAL UART handle.
 *
 * @note   Runtime RX and TX operations assume this initialisation has already completed
 *         successfully and do not reinitialise the peripheral.
 */
static bool HW_UART_Init_Channel( HwUartChannel_T channel )
{
    HwUartChannelState_T* state = &uart_channel_states[channel];
    UART_HandleTypeDef*   huart = uart_hardware_map[channel].uart_handle;

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
            break;
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
    return true;
}

/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */

/**
 * @brief  Configures a UART channel with the specified settings and applies
 *         the associated static hardware configuration.
 *
 * @param  channel The UART channel to configure.
 * @param  config  Pointer to the configuration structure describing UART
 *                 parameters and interface mode.
 *
 * @return true if the channel was successfully configured.
 * @return false if the channel index or configuration is invalid, or if
 *         hardware selection fails.
 *
 * @note   This function performs validation of the provided configuration
 *         before applying any changes to the hardware.
 *
 * @note   The configuration is stored within the low-level driver and used
 *         later during start operations. This function does not enable RX or
 *         TX operation.
 *
 * @note   Static hardware selection (e.g. interface mode and voltage levels)
 *         is applied as part of configuration to ensure the physical interface
 *         is in a safe and defined state prior to enabling UART activity.
 *
 * @note   Runtime state is reset as part of configuration, including read index
 *         and fault flags.
 *
 * @note   This function must be called successfully before invoking
 *         HW_UART_Rx_Start() or any TX-related operations.
 */
bool HW_UART_Configure_Channel( HwUartChannel_T channel, const HwUartConfig_T* config )
{
    // Validate channel number and configuration before applying changes
    if ( channel >= HW_UART_CHANNEL_COUNT )
    {
        return false;
    }

    HwUartChannelState_T* state                  = &uart_channel_states[channel];
    state->runtime.is_configured_and_initialised = false;

    if ( !HW_UART_Configuration_Is_Valid( config ) )
    {
        return false;
    }

    // Store the validated configuration

    state->config = *config;

    // Apply the static hardware selection states (mode/voltage) based on the interface mode
    // specified in the configuration to the appropriate hardware lines (GPIOs or other control
    // interfaces)
    if ( !HW_UART_Apply_Static_Hardware_Selection( channel, config->interface_mode ) )
    {
        return false;
    }

    // Reset runtime state variables to their default values
    state->runtime.latched_faults       = 0U;
    state->runtime.rx_read_index        = 0U;
    state->runtime.tx_length_bytes      = 0U;
    state->runtime.rx_running           = false;
    state->runtime.tx_loaded            = false;
    state->runtime.tx_running           = false;

    if ( !HW_UART_Init_Channel( channel ) )
    {
        return false;
    }
    // Mark channel as configured and initialised.
    state->runtime.is_configured_and_initialised = true;
    return true;
}

/**
 * @brief  Starts UART reception for the specified channel using DMA into the
 *         LL driver owned circular RX buffer.
 *
 * @param  channel The UART channel to start reception on.
 *
 * @return true if RX was successfully started.
 * @return false if the channel is invalid, not configured, RX is disabled, or
 *         hardware initialisation fails.
 *
 * @note   This function applies the stored configuration to the underlying UART
 *         peripheral via HAL and initiates DMA-based reception into the internal
 *         circular buffer owned by the low-level driver.
 *
 * @note   The RX buffer and read index are reset prior to enabling reception to
 *         ensure a clean starting state.
 *
 * @note   This function does not expose or transfer ownership of received data.
 *         Data is made available to higher layers via HW_UART_Rx_Peek().
 *
 * @note   The DMA stream is expected to be configured in circular mode so that
 *         reception continues indefinitely without software intervention.
 *
 * @note   This function must only be called after successful configuration via
 *         HW_UART_Configure_Channel().
 *
 * @note   RX operation is considered active once this function returns true, and
 *         can be queried via the runtime state.
 */
bool HW_UART_Rx_Start( HwUartChannel_T channel )
{
    if ( channel >= HW_UART_CHANNEL_COUNT )
    {
        return false;
    }

    HwUartChannelState_T*      state  = &uart_channel_states[channel];
    const HwUartHardwareMap_T* hw_map = &uart_hardware_map[channel];
    UART_HandleTypeDef*        huart  = hw_map->uart_handle;

    if ( !state->runtime.is_configured_and_initialised || !state->config.rx_enabled )
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

    if ( HAL_UART_Receive_DMA( huart, state->rx_buffer, HW_UART_RX_BUFFER_SIZE ) != HAL_OK )
    {
        return false;
    }

    state->runtime.rx_running = true;
    return true;
}

/**
 * @brief  Stops UART RX operation for the specified channel and halts DMA reception.
 *
 * @param  channel The UART channel to stop reception on.
 *
 * @return true if RX was successfully stopped.
 * @return false if the channel is invalid, not configured, not running, or if
 *         the underlying HAL stop operation fails.
 *
 * @note   This function disables DMA-based reception and prevents further data
 *         from being written into the low-level driver owned circular RX buffer.
 *
 * @note   The internal read index is reset as part of the stop operation, and any
 *         previously buffered data is considered invalid after this call.
 *
 * @note   This function is intended for non-hot-path control of RX lifecycle and
 *         should not be used in the execution path.
 *
 * @note   The channel configuration remains intact after stopping RX. Reception
 *         can be restarted by calling HW_UART_Rx_Start().
 */
bool HW_UART_Rx_Stop( HwUartChannel_T channel )
{
    if ( channel >= HW_UART_CHANNEL_COUNT )
    {
        return false;
    }

    HwUartChannelState_T*      state  = &uart_channel_states[channel];
    const HwUartHardwareMap_T* hw_map = &uart_hardware_map[channel];
    UART_HandleTypeDef*        huart  = hw_map->uart_handle;

    if ( !state->runtime.is_configured_and_initialised || !state->runtime.rx_running )
    {
        return false;
    }

    if ( HAL_UART_DMAStop( huart ) != HAL_OK )
    {

        return false;
    }
    state->runtime.rx_running    = false;
    state->runtime.rx_read_index = 0U;
    return true;
}

bool HW_UART_Rx_Is_Running( HwUartChannel_T channel )
{
    if ( channel >= HW_UART_CHANNEL_COUNT )
    {
        return false;
    }

    HwUartChannelState_T* state = &uart_channel_states[channel];

    if ( !state->runtime.is_configured_and_initialised )
    {
        return false;
    }

    return state->runtime.rx_running;
}

/**
 * @brief  Exposes a transient zero-copy view of the current unread RX data for the specified UART
 *         channel as one or two contiguous spans into the LL driver owned DMA circular buffer.
 *
 * @param channel The UART channel to inspect.
 *
 * @return HwUartRxSpans_T
 *         A structure containing up to two readable spans and the total unread byte count.
 *
 * @note   This function does not copy data. It provides a read-only view into the low-level driver
 *         owned DMA buffer. Buffer allocation, DMA write ownership, wrap handling, and consume
 *         semantics remain the responsibility of the low-level driver.
 *
 * @note   The returned spans are intended for immediate higher-level processing. Higher layers may
 *         copy the data into execution-owned result storage if persistence is required beyond the
 *         current processing step.
 *
 * @note   Higher layers must not directly manage the DMA buffer, modify the returned memory, or
 *         retain the returned pointers beyond the valid processing window. Once the required copy
 * or processing is complete, the caller shall report consumption through HW_UART_Rx_Consume().
 *
 * @note   This interface preserves a clean ownership boundary:
 *         - the low-level driver owns the DMA circular buffer and its management,
 *         - the mid-level driver owns adaptation from raw UART bytes to execution-level data,
 *         - the execution manager owns result storage and tick association.
 */
HwUartRxSpans_T HW_UART_Rx_Peek( HwUartChannel_T channel )
{
    // Remove for optimisation, but for safety in case of misuse.
    if ( channel >= HW_UART_CHANNEL_COUNT )
    {
        return ( HwUartRxSpans_T ){ 0 };
    }

    // Cache reused values in local variables for performance, as this function will be called
    // frequently in the execution path
    HwUartChannelState_T*      state      = &uart_channel_states[channel];
    const HwUartHardwareMap_T* hw_map     = &uart_hardware_map[channel];
    uint8_t*                   rx_buffer  = state->rx_buffer;
    uint32_t                   read_index = state->runtime.rx_read_index;

    // Remove for optimisation, but for safety in case of misuse.
    if ( !state->runtime.is_configured_and_initialised || !state->runtime.rx_running )
    {
        return ( HwUartRxSpans_T ){ 0 };
    }

    // Calculate the current write index based on the DMA stream's remaining data count (NDTR) and
    // the known buffer size. This reflects the total number of bytes that have been written by the
    // DMA into the buffer, regardless of any wrapping that may have occurred.
    uint32_t dma_remaining   = hw_map->rx_dma_stream->NDTR;
    uint32_t dma_write_index = HW_UART_RX_BUFFER_SIZE - dma_remaining;

    if ( dma_write_index == HW_UART_RX_BUFFER_SIZE )
    {
        dma_write_index = 0U;
    }

    uint32_t unread_bytes = HW_UART_Unread_Bytes_Count_Helper( read_index, dma_write_index );

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

/**
 * @brief  Marks unread RX data as consumed by advancing the LL driver managed read index.
 *
 * @param channel The UART channel to update.
 * @param bytes_to_consume Number of bytes previously obtained via HW_UART_Rx_Peek() that have been
 *                         processed by higher layers.
 *
 * @note   The LL driver retains ownership of the DMA buffer. This function only updates the read
 *         index and does not copy or modify buffer contents.
 *
 * @note   The caller shall only consume data that has already been processed or copied into
 *         stable storage. Consuming data allows the DMA buffer region to be reused.
 */
void HW_UART_Rx_Consume( HwUartChannel_T channel, uint32_t bytes_to_consume )
{
    HwUartChannelState_T* state = &uart_channel_states[channel];

    // Advance the read index by the specified number of bytes, wrapping around the buffer as needed
    state->runtime.rx_read_index =
        HW_UART_Advance_Index_Helper( state->runtime.rx_read_index, bytes_to_consume );
}

/**
 * @brief  Copies a transmit payload into the low-level driver owned TX staging buffer for the
 *         specified UART channel.
 *
 * @param  channel       The UART channel whose TX staging buffer is to be loaded.
 * @param  data          Pointer to the source payload to copy into the staging buffer.
 * @param  length_bytes  Number of payload bytes to stage for transmission.
 *
 * @return true if the payload was successfully copied into the staging buffer.
 * @return false if the channel is invalid, the payload pointer is null, the payload length is
 *         zero or exceeds the staging buffer capacity, the channel is not ready for TX, or
 *         staged or in-flight TX data already owns the buffer.
 *
 * @note   This function copies data into low-level driver owned memory. The caller retains
 *         ownership of the source buffer and may reuse or discard it after this function returns.
 *
 * @note   Staged data must not be overwritten before it is transmitted or otherwise released.
 *         This function therefore rejects loads while TX data is already staged or while a TX
 *         transfer is currently in progress.
 *
 * @note   This function stages data only. It does not begin transmission. Transmission begins
 *         only when HW_UART_Tx_Trigger() is called.
 */
bool HW_UART_Tx_Load_Buffer( HwUartChannel_T channel, const uint8_t* data, uint32_t length_bytes )
{
    if ( channel >= HW_UART_CHANNEL_COUNT )
    {
        return false;
    }  // Could later assume valid channel for optimisation

    if ( data == NULL )
    {
        return false;
    }  // Could later assume valid data for optimisation

    if ( ( length_bytes == 0U ) || ( length_bytes > HW_UART_TX_BUFFER_SIZE ) )
    {
        return false;
    }  // Could later assume bounded length for optimisation

    HwUartChannelState_T* state = &uart_channel_states[channel];

    if ( !state->runtime.is_configured_and_initialised || !state->config.tx_enabled )
    {
        return false;
    }  // Could later assume configured and enabled for optimisation

    if ( state->runtime.tx_loaded || state->runtime.tx_running )
    {
        return false;
    }  // Staged data must not be overwritten, this should be a fault condition

    memcpy( state->tx_buffer, data, length_bytes );

    state->runtime.tx_length_bytes = length_bytes;
    state->runtime.tx_loaded = true;

    return true;
}

/**
 * @brief  Starts transmission of the currently staged TX payload for the specified UART channel
 *         using DMA.
 *
 * @param  channel The UART channel to transmit on.
 *
 * @return true if DMA transmission was successfully started.
 * @return false if the channel is invalid, the channel is not ready for TX, no payload has been
 *         staged, a TX transfer is already running, the staged length is invalid, or
 *         HAL_UART_Transmit_DMA() fails.
 *
 * @note   This function does not copy payload data. It launches transmission of data that has
 *         already been staged in the low-level driver owned TX buffer via HW_UART_Tx_Load_Buffer().
 *
 * @note   If DMA launch fails, the staged payload remains present in the TX staging buffer and
 *         may be retried or otherwise handled by higher layers.
 *
 * @note   TX buffer ownership is released only when transmission completes, currently through
 *         HAL_UART_TxCpltCallback().
 */
bool HW_UART_Tx_Trigger( HwUartChannel_T channel )
{
    if ( channel >= HW_UART_CHANNEL_COUNT )
    {
        return false;
    }

    HwUartChannelState_T*      state  = &uart_channel_states[channel];
    const HwUartHardwareMap_T* hw_map = &uart_hardware_map[channel];
    UART_HandleTypeDef*        huart  = hw_map->uart_handle;

    if ( !state->runtime.is_configured_and_initialised || !state->config.tx_enabled )
    {
        return false;
    }  // Can remove to optimise if assuming is configured and intilialised

    if ( !state->runtime.tx_loaded || state->runtime.tx_running )
    {
        return false;
    }  // No staged payload available or tx is currently running

    if ( state->runtime.tx_length_bytes == 0U )
    {
        return false;
    }  // Defensive check, should never happen if load succeeded

    

    if ( HAL_UART_Transmit_DMA( huart,
                                state->tx_buffer,
                                ( uint16_t )state->runtime.tx_length_bytes ) != HAL_OK )
    {
        return false;
    }
    state->runtime.tx_running = true;
    return true;
}


/**
 * @brief  HAL UART transmit complete callback used to release low-level TX staging state once a
 *         DMA-backed UART transmission finishes.
 *
 * @param  huart Pointer to the HAL UART handle that completed transmission.
 *
 * @return void
 *
 * @note   This callback clears the TX running state, releases staged TX buffer ownership, and
 *         resets the staged payload length for the matching logical UART channel.
 *
 * @note   This is currently the mechanism by which the low-level driver marks a staged TX buffer
 *         as free after transmission.
 *
 * @note   The callback ignores UART handles that do not match any logical channel owned by this
 *         driver.
 */
void HAL_UART_TxCpltCallback( UART_HandleTypeDef* huart )
{
    for ( uint32_t channel = 0U; channel < HW_UART_CHANNEL_COUNT; channel++ )
    {
        if ( uart_hardware_map[channel].uart_handle == huart )
        {
            uart_channel_states[channel].runtime.tx_running      = false;
            uart_channel_states[channel].runtime.tx_loaded       = false;
            uart_channel_states[channel].runtime.tx_length_bytes = 0U;
            break;
        }
    }
}
