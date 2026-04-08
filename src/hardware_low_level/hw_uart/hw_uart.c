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
#define HW_UART_RX_BUFFER_SIZE 2048U

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
    HW_UART_WORD_LENGTH_7_BITS = 7,
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

    bool is_initialised;
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

/**-----------------------------------------------------------------------------
 *  Public (global) and Extern Variables
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Private (static) Variables
 *------------------------------------------------------------------------------
 */
static HwUartChannelState_T uart_channel_states[HW_UART_CHANNEL_COUNT];

/**-----------------------------------------------------------------------------
 *  Private (static) Function Prototypes
 *------------------------------------------------------------------------------
 */
void HW_UART_STATE_RESET_HELPER( HwUartChannelState_T* channel_state );

bool HW_UART_CONFIGURATION_VALIDATION( const HwUartConfig_T* config );

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */
void HW_UART_STATE_RESET_HELPER( HwUartChannelState_T* channel_state )
{
    if ( channel_state == NULL )
    {
        return;
    }
    channel_state->runtime.rx_read_index  = 0U;
    channel_state->runtime.latched_faults = 0U;
    channel_state->runtime.is_initialised = false;
    channel_state->runtime.is_configured  = false;
    channel_state->runtime.rx_running     = false;
    channel_state->runtime.tx_running     = false;

    // Clear the receive buffer
    for ( uint32_t i = 0; i < HW_UART_RX_BUFFER_SIZE; i++ )
    {
        channel_state->rx_buffer[i] = 0U;
    }
}

bool HW_UART_CONFIGURATION_VALIDATION( const HwUartConfig_T* config )
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

    if ( config->word_length != HW_UART_WORD_LENGTH_7_BITS
         && config->word_length != HW_UART_WORD_LENGTH_8_BITS
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

/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */
