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

typedef enum
{
    HW_UART_MODE_DISABLED = 0,  // Default state, no UART functionality
    HW_UART_MODE_TTL_3V3,       // Standard TTL logic levels (0V for LOW, 3.3V for HIGH)
    HW_UART_MODE_TTL_5V0,       // Standard TTL logic levels (0V for LOW, 5V for HIGH)
    HW_UART_MODE_RS232          // RS-232 line driver interface
} HwUartInterfaceMode_T;

typedef enum
{
    HW_UART_FAULT_NONE = 0U,

    // RX faults
    HW_UART_FAULT_RX_OVERRUN  = ( 1U << 0 ),
    HW_UART_FAULT_RX_FRAMING  = ( 1U << 1 ),
    HW_UART_FAULT_RX_PARITY   = ( 1U << 2 ),
    HW_UART_FAULT_RX_NOISE    = ( 1U << 3 ),
    HW_UART_FAULT_RX_OVERFLOW = ( 1U << 4 ),

    // DMA faults
    HW_UART_FAULT_DMA_ERROR = ( 1U << 5 ),

    // TX faults
    HW_UART_FAULT_TX_BUSY       = ( 1U << 6 ),
    HW_UART_FAULT_TX_INCOMPLETE = ( 1U << 7 )

} HwUartFaultMask_T;

typedef struct
{
    HwUartInterfaceMode_T interface_mode;  // Determines the Uart interfect type and voltage levels

    uint32_t baud_rate;
    uint32_t word_length;
    uint32_t stop_bits;
    uint32_t parity;

    bool rx_enabled;           // Enable reception functionality
    bool tx_enabled;           // Enable transmission functionality
    bool half_duplex_enabled;  // Enable half-duplex mode for TTL interface modes only.
    // Must be false if interface_mode is HW_UART_MODE_RS232 or HW_UART_MODE_DISABLED.
} HwUartConfig_T;

typedef struct
{
    uint32_t          rx_read_index;
    HwUartFaultMask_T latched_faults;

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

typedef enum
{
    HW_UART_CHANNEL_1 = 0,
    HW_UART_CHANNEL_2,
} HwUartChannel_T;

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

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */

// ISRs
