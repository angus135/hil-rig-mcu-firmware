/******************************************************************************
 *  File:       hw_uart_dut.c
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
 *      - lightweight access to unread RX data through zero-copy spans,
 *      - DMA-source TX ring buffering,
 *      - normal mode DMA TX pumping over contiguous TX buffer spans.
 *
 *      Non-execution stage functions such as configuration and RX startup may use HAL
 *      to simplify peripheral initialisation. Execution-path RX access remains
 *      lightweight and avoids unnecessary copying. TX transfer setup uses LL DMA and
 *      USART access to avoid HAL state-machine overhead during execution.
 *
 *
 *      Execution path API contract:
 *      Unless stated otherwise, execution path functions assume valid input.
 *      The caller must provide a valid channel, must ensure the channel has been
 *      configured for the requested direction, and must respect buffer ownership
 *      rules. These functions avoid defensive parameter checks to minimise execution
 *      path overhead.
 *
 *  Notes:
 *      - The low-level driver owns the RX DMA circular buffer, the TX DMA-source
 *        ring buffer, and all associated buffer management state.
 *      - Higher layers may inspect unread RX data through transient span views and
 *        must copy data into stable storage if persistence is required.
 *      - Higher layers must explicitly report RX consumption after processing.
 *      - Higher layers queue TX data by copying complete payloads directly into
 *        the TX DMA-source ring buffer. If insufficient free space exists, no
 *        bytes are copied.
 *      - The TX DMA stream is operated in normal mode. Software handles wrap-around
 *        by launching one DMA transfer per contiguous TX buffer span.
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
#include "stm32f4xx_ll_dma.h"
#include "stm32f4xx_ll_usart.h"
#endif

#include "hw_uart_dut.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */

/* Fixed UART hardware mapping definitions. */
#define HW_UART_CH1_USART USART6
#define HW_UART_CH1_HANDLE ( &huart6 )

#define HW_UART_CH1_DMA_RX_STREAM DMA2_Stream2
#define HW_UART_CH1_DMA_TX_STREAM DMA2_Stream6

#define HW_UART_CH1_TX_DMA_IRQ DMA2_Stream6_IRQn
#define HW_UART_CH1_TX_DMA_IRQ_HANDLER DMA2_Stream6_IRQHandler
#define HW_UART_CH1_RX_DMA_IRQ_HANDLER DMA2_Stream2_IRQHandler

#define HW_UART_CH1_DMA_CONTROLLER DMA2
#define HW_UART_CH1_DMA_TX_LL_STREAM LL_DMA_STREAM_6

#define HW_UART_CH1_DMA_TX_IFCR_REG ( &DMA2->HIFCR )
#define HW_UART_CH1_DMA_TX_IFCR_MASK                                                               \
    ( DMA_HIFCR_CTCIF6 | DMA_HIFCR_CTEIF6 | DMA_HIFCR_CFEIF6 | DMA_HIFCR_CDMEIF6                   \
      | DMA_HIFCR_CHTIF6 )
#define HW_UART_CH1_DMA_RX_IFCR_REG ( &DMA2->LIFCR )
#define HW_UART_CH1_DMA_RX_IFCR_MASK                                                               \
    ( DMA_LIFCR_CTCIF2 | DMA_LIFCR_CTEIF2 | DMA_LIFCR_CFEIF2 | DMA_LIFCR_CDMEIF2                   \
      | DMA_LIFCR_CHTIF2 )

#define HW_UART_CH2_USART USART2
#define HW_UART_CH2_HANDLE ( &huart2 )

#define HW_UART_CH2_DMA_RX_STREAM DMA1_Stream5
#define HW_UART_CH2_DMA_TX_STREAM DMA1_Stream6

#define HW_UART_CH2_TX_DMA_IRQ DMA1_Stream6_IRQn
#define HW_UART_CH2_TX_DMA_IRQ_HANDLER DMA1_Stream6_IRQHandler
#define HW_UART_CH2_RX_DMA_IRQ_HANDLER DMA1_Stream5_IRQHandler

#define HW_UART_CH2_DMA_CONTROLLER DMA1
#define HW_UART_CH2_DMA_TX_LL_STREAM LL_DMA_STREAM_6

#define HW_UART_CH2_DMA_TX_IFCR_REG ( &DMA1->HIFCR )
#define HW_UART_CH2_DMA_TX_IFCR_MASK                                                               \
    ( DMA_HIFCR_CTCIF6 | DMA_HIFCR_CTEIF6 | DMA_HIFCR_CFEIF6 | DMA_HIFCR_CDMEIF6                   \
      | DMA_HIFCR_CHTIF6 )
#define HW_UART_CH2_DMA_RX_IFCR_REG ( &DMA1->HIFCR )
#define HW_UART_CH2_DMA_RX_IFCR_MASK                                                               \
    ( DMA_HIFCR_CTCIF5 | DMA_HIFCR_CTEIF5 | DMA_HIFCR_CFEIF5 | DMA_HIFCR_CDMEIF5                   \
      | DMA_HIFCR_CHTIF5 )

/* Placeholder interface selection line definitions. */
#define HW_UART_CH1_MODE_SEL0_LINE GPIO_PIN_0
#define HW_UART_CH1_MODE_SEL1_LINE GPIO_PIN_1
#define HW_UART_CH1_VOLT_SEL0_LINE GPIO_PIN_2
#define HW_UART_CH1_VOLT_SEL1_LINE GPIO_PIN_3

#define HW_UART_CH2_MODE_SEL0_LINE GPIO_PIN_4
#define HW_UART_CH2_MODE_SEL1_LINE GPIO_PIN_5
#define HW_UART_CH2_VOLT_SEL0_LINE GPIO_PIN_6
#define HW_UART_CH2_VOLT_SEL1_LINE GPIO_PIN_7

/* RX buffer size must remain a power of 2 for mask-based circular indexing. */
#define HW_UART_RX_BUFFER_SIZE 4096U

#if ( ( HW_UART_RX_BUFFER_SIZE & ( HW_UART_RX_BUFFER_SIZE - 1U ) ) != 0U )
#error "HW_UART_RX_BUFFER_SIZE must be a power of 2"
#endif

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
 *         RX consumption state, TX ring buffer state, latched faults, configuration
 *         status, and whether RX or TX DMA operation is currently active.
 *
 * @note   The TX fields implement a single-producer, single-consumer DMA-source
 *         ring buffer:
 *         - higher-level TX scheduling appends data at tx_head,
 *         - the TX DMA engine reads a contiguous span starting at tx_tail,
 *         - the TX DMA completion path advances tx_tail after DMA has consumed
 *           the active span,
 *         - tx_count tracks the total number of bytes owned by the driver,
 *           including queued bytes and bytes currently being consumed by DMA,
 *         - tx_dma_length_bytes records the active linear DMA transfer length,
 *         - tx_dma_active indicates that a normal-mode DMA transfer is currently
 *           reading from the TX ring buffer.
 */
typedef struct
{
    uint32_t rx_read_index;
    uint32_t latched_faults;  // Bitmask implementation left for a later date when faults are
                              // implemented.

    bool is_configured_and_initialised;
    bool rx_running;

    uint32_t tx_head;
    uint32_t tx_tail;
    uint32_t tx_count;
    uint32_t tx_dma_length_bytes;
    bool     tx_dma_active;

} HwUartRuntimeState_T;

/**
 * @brief  Aggregates all low-level state for a UART channel.
 *
 * @note   This includes the stored channel configuration, runtime state, the
 *         DMA-backed circular RX buffer, and the DMA-source TX ring buffer owned
 *         by the low-level driver.
 *
 * @note   The TX buffer is both the software queue and the DMA source buffer.
 *         Normal-mode DMA transfers read directly from this buffer.
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
    UART_HandleTypeDef* uart_handle;

    DMA_TypeDef*       tx_dma_controller;
    uint32_t           tx_ll_stream;
    IRQn_Type          tx_dma_irq;
    volatile uint32_t* tx_dma_ifcr_reg;
    uint32_t           tx_dma_ifcr_mask;

    volatile uint32_t* rx_dma_ifcr_reg;
    uint32_t           rx_dma_ifcr_mask;
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
static HwUartChannelState_T hw_uart_channel_states[HW_UART_CHANNEL_COUNT];

/* Fixed board-level mapping from logical UART channels to MCU peripherals */
static const HwUartHardwareMap_T hw_uart_hardware_map[HW_UART_CHANNEL_COUNT] = {

    [HW_UART_CHANNEL_1] = { .uart_instance     = HW_UART_CH1_USART,
                            .rx_dma_stream     = HW_UART_CH1_DMA_RX_STREAM,
                            .tx_dma_stream     = HW_UART_CH1_DMA_TX_STREAM,
                            .uart_handle       = HW_UART_CH1_HANDLE,
                            .tx_dma_controller = HW_UART_CH1_DMA_CONTROLLER,
                            .tx_ll_stream      = HW_UART_CH1_DMA_TX_LL_STREAM,
                            .tx_dma_irq        = HW_UART_CH1_TX_DMA_IRQ,
                            .tx_dma_ifcr_reg   = HW_UART_CH1_DMA_TX_IFCR_REG,
                            .tx_dma_ifcr_mask  = HW_UART_CH1_DMA_TX_IFCR_MASK,
                            .rx_dma_ifcr_reg   = HW_UART_CH1_DMA_RX_IFCR_REG,
                            .rx_dma_ifcr_mask  = HW_UART_CH1_DMA_RX_IFCR_MASK },

    [HW_UART_CHANNEL_2] = { .uart_instance     = HW_UART_CH2_USART,
                            .rx_dma_stream     = HW_UART_CH2_DMA_RX_STREAM,
                            .tx_dma_stream     = HW_UART_CH2_DMA_TX_STREAM,
                            .uart_handle       = HW_UART_CH2_HANDLE,
                            .tx_dma_controller = HW_UART_CH2_DMA_CONTROLLER,
                            .tx_ll_stream      = HW_UART_CH2_DMA_TX_LL_STREAM,
                            .tx_dma_irq        = HW_UART_CH2_TX_DMA_IRQ,
                            .tx_dma_ifcr_reg   = HW_UART_CH2_DMA_TX_IFCR_REG,
                            .tx_dma_ifcr_mask  = HW_UART_CH2_DMA_TX_IFCR_MASK,
                            .rx_dma_ifcr_reg   = HW_UART_CH2_DMA_RX_IFCR_REG,
                            .rx_dma_ifcr_mask  = HW_UART_CH2_DMA_RX_IFCR_MASK } };

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
 *  Private (static) Function Prototypes
 *------------------------------------------------------------------------------
 */
static inline void HW_UART_Tx_Complete_Handler( HwUartChannel_T channel );
static inline void HW_UART_Tx_Error_Handler( HwUartChannel_T channel );
static inline void HW_UART_Rx_Error_Handler( HwUartChannel_T channel );

/**-----------------------------------------------------------------------------
 *  Interrupt Handler Prototypes
 *------------------------------------------------------------------------------
 */
void HW_UART_CH1_TX_DMA_IRQ_HANDLER( void );
void HW_UART_CH2_TX_DMA_IRQ_HANDLER( void );
void HW_UART_CH1_RX_DMA_IRQ_HANDLER( void );
void HW_UART_CH2_RX_DMA_IRQ_HANDLER( void );
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

static inline uint32_t HW_UART_Tx_Dma_Irq_Disable( HwUartChannel_T channel )
{
    IRQn_Type irq         = hw_uart_hardware_map[channel].tx_dma_irq;
    uint32_t  was_enabled = NVIC_GetEnableIRQ( irq );

    NVIC_DisableIRQ( irq );

    return was_enabled;
}

static inline void HW_UART_Tx_Dma_Irq_Restore( HwUartChannel_T channel, uint32_t was_enabled )
{
    if ( was_enabled != 0U )
    {
        NVIC_EnableIRQ( hw_uart_hardware_map[channel].tx_dma_irq );
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

    const HwUartSelectionLines_T* selection = &uart_selection_lines[channel];
    ( void )selection;
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
    HwUartChannelState_T* state = &hw_uart_channel_states[channel];
    UART_HandleTypeDef*   huart = hw_uart_hardware_map[channel].uart_handle;

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

/**
 * @brief  Handles completion of a TX DMA transfer for the specified UART channel.
 *
 * @param  channel The UART channel whose active TX DMA transfer has completed.
 *
 * @return void
 *
 * @note   This function is called from the DMA stream interrupt path once the DMA
 *         controller has finished reading the active linear TX buffer span.
 *
 * @note   DMA completion is sufficient to release the active TX buffer span
 *         because the DMA engine has finished reading those bytes from the
 *         DMA-source ring buffer.
 *
 * @note   This function advances tx_tail by the completed DMA length, reduces
 *         tx_count, clears the active transfer state, and restarts the TX DMA pump
 *         if queued data remains.
 *
 * @note   If the queued TX data wraps around the end of the TX ring buffer,
 *         this completion handler causes the next contiguous span to be launched
 *         as a separate normal mode DMA transfer.
 *
 * @note   This function does not determine whether the final UART stop bit has
 *         fully left the wire. It only marks completion of DMA consumption of the
 *         active TX buffer span.
 */
static inline void HW_UART_Tx_Complete_Handler( HwUartChannel_T channel )
{
    HwUartRuntimeState_T* runtime = &hw_uart_channel_states[channel].runtime;

    LL_USART_DisableDMAReq_TX( hw_uart_hardware_map[channel].uart_instance );

    uint32_t completed_length = runtime->tx_dma_length_bytes;

    runtime->tx_tail = ( runtime->tx_tail + completed_length ) % HW_UART_TX_BUFFER_SIZE;

    runtime->tx_count -= completed_length;
    runtime->tx_dma_length_bytes = 0U;
    runtime->tx_dma_active       = false;

    if ( runtime->tx_count > 0U )
    {
        ( void )HW_UART_Tx_Trigger( channel );
    }
}

/**
 * @brief  Handles a TX DMA error for the specified UART channel.
 *
 * @param  channel The UART channel whose TX DMA transfer encountered an error.
 *
 * @return void
 *
 * @note   A TX DMA error indicates loss of deterministic execution for the active
 *         transfer. This handler disables the UART DMA TX request, clears the TX
 *         ring buffer state, and releases the active DMA transfer state.
 *
 * @note   This function does not attempt recovery or retry. Higher layers are
 *         expected to treat this as a fault condition and abort execution for
 *         deterministic behaviour requirements.
 *
 * @note   Fault latching should be added when the UART fault bitmask is implemented.
 */
static inline void HW_UART_Tx_Error_Handler( HwUartChannel_T channel )
{
    HwUartRuntimeState_T* runtime = &hw_uart_channel_states[channel].runtime;

    LL_USART_DisableDMAReq_TX( hw_uart_hardware_map[channel].uart_instance );

    runtime->tx_head             = 0U;
    runtime->tx_tail             = 0U;
    runtime->tx_count            = 0U;
    runtime->tx_dma_length_bytes = 0U;
    runtime->tx_dma_active       = false;

    /* Future fault implementation:
     * runtime->latched_faults |= HW_UART_FAULT_DMA_ERROR;
     */
}

/**
 * @brief Handles an RX DMA error for the specified UART channel.
 *
 * @note This function intentionally performs no recovery while the UART fault
 *       model is not yet implemented. It exists as the future insertion point
 *       for fault latching and error policy.
 */
static inline void HW_UART_Rx_Error_Handler( HwUartChannel_T channel )
{
    HwUartRuntimeState_T* runtime = &hw_uart_channel_states[channel].runtime;
    ( void )runtime;

    /* Future fault implementation:
     * runtime->latched_faults |= HW_UART_FAULT_DMA_ERROR;
     */
}

/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */

/* Applies validated configuration and resets runtime state. */
bool HW_UART_Configure_Channel( HwUartChannel_T channel, const HwUartConfig_T* config )
{
    if ( channel >= HW_UART_CHANNEL_COUNT )
    {
        return false;
    }

    HwUartChannelState_T* state                  = &hw_uart_channel_states[channel];
    state->runtime.is_configured_and_initialised = false;

    if ( !HW_UART_Configuration_Is_Valid( config ) )
    {
        return false;
    }

    state->config = *config;

    if ( !HW_UART_Apply_Static_Hardware_Selection( channel, config->interface_mode ) )
    {
        return false;
    }

    /* Reset runtime state before reinitialising the channel. */
    state->runtime.latched_faults = 0U;
    state->runtime.rx_read_index  = 0U;
    state->runtime.rx_running     = false;

    state->runtime.tx_head             = 0U;
    state->runtime.tx_tail             = 0U;
    state->runtime.tx_count            = 0U;
    state->runtime.tx_dma_length_bytes = 0U;
    state->runtime.tx_dma_active       = false;

    if ( !HW_UART_Init_Channel( channel ) )
    {
        return false;
    }

    state->runtime.is_configured_and_initialised = true;
    return true;
}

/* Starts DMA-backed UART reception for the specified channel. */
bool HW_UART_Rx_Start( HwUartChannel_T channel )
{
    if ( channel >= HW_UART_CHANNEL_COUNT )
    {
        return false;
    }

    HwUartChannelState_T*      state  = &hw_uart_channel_states[channel];
    const HwUartHardwareMap_T* hw_map = &hw_uart_hardware_map[channel];
    UART_HandleTypeDef*        huart  = hw_map->uart_handle;

    if ( !state->runtime.is_configured_and_initialised || !state->config.rx_enabled )
    {
        return false;
    }

    if ( state->runtime.rx_running )
    {
        return false;
    }

    /* Reset RX state and clear stale buffered data before enabling DMA reception. */
    state->runtime.rx_read_index = 0U;

    for ( uint32_t i = 0U; i < HW_UART_RX_BUFFER_SIZE; i++ )
    {
        state->rx_buffer[i] = 0U;
    }

    if ( HAL_UART_Receive_DMA( huart, state->rx_buffer, HW_UART_RX_BUFFER_SIZE ) != HAL_OK )
    {
        return false;
    }

    if ( huart->hdmarx != NULL )
    {
        __HAL_DMA_DISABLE_IT( huart->hdmarx, DMA_IT_HT );
        __HAL_DMA_DISABLE_IT( huart->hdmarx, DMA_IT_TC );
    }

    state->runtime.rx_running = true;
    return true;
}

/* Stops the RX DMA for a given channel */
bool HW_UART_Rx_Stop( HwUartChannel_T channel )
{
    if ( channel >= HW_UART_CHANNEL_COUNT )
    {
        return false;
    }

    HwUartChannelState_T*      state  = &hw_uart_channel_states[channel];
    const HwUartHardwareMap_T* hw_map = &hw_uart_hardware_map[channel];
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

/* Public Helper to check if Rx is running*/
bool HW_UART_Rx_Is_Running( HwUartChannel_T channel )
{
    if ( channel >= HW_UART_CHANNEL_COUNT )
    {
        return false;
    }

    HwUartChannelState_T* state = &hw_uart_channel_states[channel];

    if ( !state->runtime.is_configured_and_initialised )
    {
        return false;
    }

    return state->runtime.rx_running;
}

/*
 * Returns spans into the RX DMA circular buffer containing unread data.
 *
 * Contract:
 * The caller must provide a valid UART channel.
 * The channel must already be configured and RX DMA must be running.
 * The returned spans are transient views into the driver owned RX DMA buffer.
 * Higher layers must copy data if it must persist beyond the current processing step.
 */
HwUartRxSpans_T HW_UART_Rx_Peek( HwUartChannel_T channel )
{
    HwUartChannelState_T*      state      = &hw_uart_channel_states[channel];
    const HwUartHardwareMap_T* hw_map     = &hw_uart_hardware_map[channel];
    uint8_t*                   rx_buffer  = state->rx_buffer;
    uint32_t                   read_index = state->runtime.rx_read_index;

    /* Derive the current DMA write index from NDTR. */
    uint32_t dma_remaining   = hw_map->rx_dma_stream->NDTR;
    uint32_t dma_write_index = HW_UART_RX_BUFFER_SIZE - dma_remaining;

    if ( dma_write_index == HW_UART_RX_BUFFER_SIZE )
    {
        dma_write_index = 0U;
    }

    uint32_t unread_bytes = HW_UART_Unread_Bytes_Count_Helper( read_index, dma_write_index );

    if ( unread_bytes == 0U )
    {
        /* No Data Available */
        return ( HwUartRxSpans_T ){ .first_span  = { .data = &rx_buffer[0], .length_bytes = 0U },
                                    .second_span = { .data = &rx_buffer[0], .length_bytes = 0U },
                                    .total_length_bytes = 0U };
    }

    if ( dma_write_index >= read_index )
    {
        /* Single contiguous unread region. */
        return ( HwUartRxSpans_T ){
            .first_span         = { .data = &rx_buffer[read_index], .length_bytes = unread_bytes },
            .second_span        = { .data = &rx_buffer[0], .length_bytes = 0U },
            .total_length_bytes = unread_bytes };
    }

    /* Wrapped unread region split into two contiguous spans. */
    uint32_t first_span_length  = HW_UART_RX_BUFFER_SIZE - read_index;
    uint32_t second_span_length = unread_bytes - first_span_length;

    return ( HwUartRxSpans_T ){
        .first_span         = { .data = &rx_buffer[read_index], .length_bytes = first_span_length },
        .second_span        = { .data = &rx_buffer[0], .length_bytes = second_span_length },
        .total_length_bytes = unread_bytes };
}

/*
 * Advances the RX read index after higher layers have consumed bytes from the
 * spans returned by HW_UART_Rx_Peek().
 *
 * Contract:
 * The caller must provide a valid UART channel.
 * bytes_to_consume must not exceed the unread byte count previously reported by
 * HW_UART_Rx_Peek().
 */
void HW_UART_Rx_Consume( HwUartChannel_T channel, uint32_t bytes_to_consume )
{
    HwUartChannelState_T* state = &hw_uart_channel_states[channel];

    state->runtime.rx_read_index =
        HW_UART_Advance_Index_Helper( state->runtime.rx_read_index, bytes_to_consume );
}

/*
 * Copies a complete payload directly into the TX DMA source ring buffer.
 *
 * Contract:
 * The caller must provide a valid UART channel.
 * data must point to at least length_bytes bytes.
 * length_bytes must be greater than zero.
 * The channel must already be configured for TX.
 * The execution layer is the sole producer for each UART channel.
 *
 * The TX buffer is both the driver owned queue and the DMA source buffer. The
 * DMA engine consumes one contiguous span at a time in normal mode. Newly copied
 * bytes are not visible to the DMA pump until tx_head and tx_count are committed
 * with interrupts disabled.
 *
 * Returns false only when there is insufficient free TX buffer space.
 */
bool HW_UART_Tx_Load_Buffer( HwUartChannel_T channel, const uint8_t* data, uint32_t length_bytes )
{

    HwUartChannelState_T* state = &hw_uart_channel_states[channel];

    uint32_t start_head;
    uint32_t new_head;
    uint32_t first_chunk;
    uint32_t second_chunk;

    uint32_t tx_irq_was_enabled = HW_UART_Tx_Dma_Irq_Disable( channel );

    uint32_t free_space = HW_UART_TX_BUFFER_SIZE - state->runtime.tx_count;

    if ( length_bytes > free_space )
    {
        HW_UART_Tx_Dma_Irq_Restore( channel, tx_irq_was_enabled );
        return false;
    }

    start_head = state->runtime.tx_head;

    HW_UART_Tx_Dma_Irq_Restore( channel, tx_irq_was_enabled );

    first_chunk = HW_UART_TX_BUFFER_SIZE - start_head;

    if ( first_chunk > length_bytes )
    {
        first_chunk = length_bytes;
    }

    second_chunk = length_bytes - first_chunk;

    memcpy( &state->tx_buffer[start_head], data, first_chunk );

    if ( second_chunk > 0U )
    {
        memcpy( &state->tx_buffer[0U], &data[first_chunk], second_chunk );
    }

    new_head = ( start_head + length_bytes ) % HW_UART_TX_BUFFER_SIZE;

    tx_irq_was_enabled = HW_UART_Tx_Dma_Irq_Disable( channel );

    state->runtime.tx_head = new_head;
    state->runtime.tx_count += length_bytes;

    HW_UART_Tx_Dma_Irq_Restore( channel, tx_irq_was_enabled );

    return true;
}

/*
 * Starts a normal mode TX DMA transfer for the next contiguous span in the TX
 * DMA source ring buffer.
 *
 * Contract:
 * The caller must provide a valid UART channel.
 * The channel must already be configured for TX.
 *
 * If a TX DMA transfer is already active, this function leaves the current
 * transfer untouched. Newly queued data will be considered after the active DMA
 * transfer completes and the completion handler calls this function again.
 *
 * If queued data wraps around the end of the ring buffer, only the first
 * contiguous span is launched. The wrapped span is launched by the completion
 * handler after the first span completes.
 */
bool HW_UART_Tx_Trigger( HwUartChannel_T channel )
{
    HwUartChannelState_T*      state             = &hw_uart_channel_states[channel];
    const HwUartHardwareMap_T* hw_map            = &hw_uart_hardware_map[channel];
    USART_TypeDef*             uart              = hw_map->uart_instance;
    DMA_TypeDef*               tx_dma_controller = hw_map->tx_dma_controller;
    uint32_t                   tx_ll_stream      = hw_map->tx_ll_stream;

    /* Enter critical section */
    uint32_t tx_irq_was_enabled = HW_UART_Tx_Dma_Irq_Disable( channel );

    if ( state->runtime.tx_dma_active )
    {
        HW_UART_Tx_Dma_Irq_Restore( channel, tx_irq_was_enabled );
        return true;
    }

    /* No queued data exists, so there is no DMA transfer to launch. */
    if ( state->runtime.tx_count == 0U )
    {
        HW_UART_Tx_Dma_Irq_Restore( channel, tx_irq_was_enabled );
        return true;
    }

    uint32_t dma_length = HW_UART_TX_BUFFER_SIZE - state->runtime.tx_tail;

    if ( dma_length > state->runtime.tx_count )
    {
        dma_length = state->runtime.tx_count;
    }

    state->runtime.tx_dma_length_bytes = dma_length;
    state->runtime.tx_dma_active       = true;

    /* Capture the starting index for the DMA transfer*/
    uint32_t dma_start_index = state->runtime.tx_tail;

    LL_DMA_DisableStream( tx_dma_controller, tx_ll_stream );

    while ( LL_DMA_IsEnabledStream( tx_dma_controller, tx_ll_stream ) )
    {
    }

    *( hw_map->tx_dma_ifcr_reg ) = hw_map->tx_dma_ifcr_mask;

    LL_DMA_SetMemoryAddress( tx_dma_controller, tx_ll_stream,
                             ( uint32_t )( uintptr_t )&state->tx_buffer[dma_start_index] );

    LL_DMA_SetPeriphAddress( tx_dma_controller, tx_ll_stream,
                             ( uint32_t )( uintptr_t )( &( uart->DR ) ) );

    LL_DMA_SetDataLength( tx_dma_controller, tx_ll_stream, dma_length );

    LL_DMA_DisableIT_HT( tx_dma_controller, tx_ll_stream );

    LL_DMA_EnableIT_TC( tx_dma_controller, tx_ll_stream );
    LL_DMA_EnableIT_TE( tx_dma_controller, tx_ll_stream );

    LL_USART_ClearFlag_TC( uart );
    LL_USART_EnableDMAReq_TX( uart );

    LL_DMA_EnableStream( tx_dma_controller, tx_ll_stream );

    HW_UART_Tx_Dma_Irq_Restore( channel, tx_irq_was_enabled );

    return true;
}

bool HW_UART_Is_Tx_Complete( HwUartChannel_T channel )
{
    HwUartRuntimeState_T* runtime = &hw_uart_channel_states[channel].runtime;
    USART_TypeDef*        uart    = hw_uart_hardware_map[channel].uart_instance;

    const bool dma_idle = ( runtime->tx_count == 0U ) && ( runtime->tx_dma_active == false );

    const bool wire_idle = ( LL_USART_IsActiveFlag_TC( uart ) != 0U );

    return dma_idle && wire_idle;
}

/**
 * @brief  DMA interrupt service routine for TX on UART Channel 1.
 *
 * @note   This function is bound to the MCU interrupt vector via the
 *         HW_UART_CH1_TX_DMA_IRQ_HANDLER macro, which expands to the
 *         device-specific DMA stream IRQ handler (e.g. DMA2_Stream6_IRQHandler).
 *
 * @note   This ISR is not declared in the public header as it is not intended
 *         to be called by application code. It exists solely to service the
 *         hardware interrupt vector.
 *
 * @note   The ISR checks for DMA transfer-complete and transfer-error conditions,
 *         clears the corresponding DMA flags, and dispatches to the appropriate
 *         low-level TX handler.
 *
 * @note   DMA completion indicates that the active linear TX buffer span has
 *         been fully consumed by the DMA engine. It does not guarantee that the
 *         final UART stop bit has left the wire.
 *
 * @note   This handler must remain minimal and deterministic. No blocking or
 *         heavy processing should be introduced here.
 */
void HW_UART_CH1_TX_DMA_IRQ_HANDLER( void )
{
    const HwUartHardwareMap_T* hw_map = &hw_uart_hardware_map[HW_UART_CHANNEL_1];

    if ( LL_DMA_IsActiveFlag_TC6( DMA2 ) )
    {
        *( hw_map->tx_dma_ifcr_reg ) = hw_map->tx_dma_ifcr_mask;
        HW_UART_Tx_Complete_Handler( HW_UART_CHANNEL_1 );
    }

    else if ( LL_DMA_IsActiveFlag_TE6( DMA2 ) )
    {
        *( hw_map->tx_dma_ifcr_reg ) = hw_map->tx_dma_ifcr_mask;
        HW_UART_Tx_Error_Handler( HW_UART_CHANNEL_1 );
    }
}

/**
 * @brief  DMA interrupt service routine for TX on UART Channel 2.
 *
 * @note   This function is bound to the MCU interrupt vector via the
 *         HW_UART_CH2_TX_DMA_IRQ_HANDLER macro, which expands to the
 *         device-specific DMA stream IRQ handler (e.g. DMA1_Stream6_IRQHandler).
 *
 * @note   This ISR is not declared in the public header as it is not intended
 *         to be called by application code. It exists solely to service the
 *         hardware interrupt vector.
 *
 * @note   The ISR checks for DMA transfer-complete and transfer-error conditions,
 *         clears the corresponding DMA flags, and dispatches to the appropriate
 *         low-level TX handler.
 *
 * @note   DMA completion indicates that the active linear TX buffer span has
 *         been fully consumed by the DMA engine. It does not guarantee that the
 *         final UART stop bit has left the wire.
 *
 * @note   This handler must remain minimal and deterministic. No blocking or
 *         heavy processing should be introduced here.
 */
void HW_UART_CH2_TX_DMA_IRQ_HANDLER( void )
{
    const HwUartHardwareMap_T* hw_map = &hw_uart_hardware_map[HW_UART_CHANNEL_2];

    if ( LL_DMA_IsActiveFlag_TC6( DMA1 ) )
    {
        *( hw_map->tx_dma_ifcr_reg ) = hw_map->tx_dma_ifcr_mask;
        HW_UART_Tx_Complete_Handler( HW_UART_CHANNEL_2 );
    }

    else if ( LL_DMA_IsActiveFlag_TE6( DMA1 ) )
    {
        *( hw_map->tx_dma_ifcr_reg ) = hw_map->tx_dma_ifcr_mask;
        HW_UART_Tx_Error_Handler( HW_UART_CHANNEL_2 );
    }
}

/**
 * @brief  DMA interrupt service routine for RX on UART Channel 1.
 *
 * @note   This function is bound to the MCU interrupt vector via the
 *         HW_UART_CH1_RX_DMA_IRQ_HANDLER macro, which expands to the
 *         device-specific DMA stream IRQ handler, DMA2_Stream2_IRQHandler.
 *
 * @note   RX DMA half-transfer and transfer-complete events are not used by the
 *         polling RX design. They are cleared if they occur unexpectedly.
 *
 * @note   RX DMA error handling is intentionally minimal until the UART fault
 *         bitmask is implemented. This handler exists to provide a valid vector,
 *         clear interrupt flags, and provide the future framework for fault
 *         reporting.
 */
void HW_UART_CH1_RX_DMA_IRQ_HANDLER( void )
{
    const HwUartHardwareMap_T* hw_map = &hw_uart_hardware_map[HW_UART_CHANNEL_1];

    if ( LL_DMA_IsActiveFlag_TE2( DMA2 ) )
    {
        *( hw_map->rx_dma_ifcr_reg ) = hw_map->rx_dma_ifcr_mask;
        HW_UART_Rx_Error_Handler( HW_UART_CHANNEL_1 );
    }

    else if ( LL_DMA_IsActiveFlag_DME2( DMA2 ) )
    {
        *( hw_map->rx_dma_ifcr_reg ) = hw_map->rx_dma_ifcr_mask;
        HW_UART_Rx_Error_Handler( HW_UART_CHANNEL_1 );
    }

    else if ( LL_DMA_IsActiveFlag_FE2( DMA2 ) )
    {
        *( hw_map->rx_dma_ifcr_reg ) = hw_map->rx_dma_ifcr_mask;
        HW_UART_Rx_Error_Handler( HW_UART_CHANNEL_1 );
    }

    else if ( LL_DMA_IsActiveFlag_HT2( DMA2 ) )
    {
        *( hw_map->rx_dma_ifcr_reg ) = hw_map->rx_dma_ifcr_mask;
    }

    else if ( LL_DMA_IsActiveFlag_TC2( DMA2 ) )
    {
        *( hw_map->rx_dma_ifcr_reg ) = hw_map->rx_dma_ifcr_mask;
    }
}

/**
 * @brief  DMA interrupt service routine for RX on UART Channel 2.
 *
 * @note   This function is bound to the MCU interrupt vector via the
 *         HW_UART_CH2_RX_DMA_IRQ_HANDLER macro, which expands to the
 *         device-specific DMA stream IRQ handler, DMA1_Stream5_IRQHandler.
 *
 * @note   RX DMA half-transfer and transfer-complete events are not used by the
 *         polling RX design. They are cleared if they occur unexpectedly.
 *
 * @note   RX DMA error handling is intentionally minimal until the UART fault
 *         bitmask is implemented. This handler exists to provide a valid vector,
 *         clear interrupt flags, and provide the future framework for fault
 *         reporting.
 */
void HW_UART_CH2_RX_DMA_IRQ_HANDLER( void )
{
    const HwUartHardwareMap_T* hw_map = &hw_uart_hardware_map[HW_UART_CHANNEL_2];

    if ( LL_DMA_IsActiveFlag_TE5( DMA1 ) )
    {
        *( hw_map->rx_dma_ifcr_reg ) = hw_map->rx_dma_ifcr_mask;
        HW_UART_Rx_Error_Handler( HW_UART_CHANNEL_2 );
    }

    else if ( LL_DMA_IsActiveFlag_DME5( DMA1 ) )
    {
        *( hw_map->rx_dma_ifcr_reg ) = hw_map->rx_dma_ifcr_mask;
        HW_UART_Rx_Error_Handler( HW_UART_CHANNEL_2 );
    }

    else if ( LL_DMA_IsActiveFlag_FE5( DMA1 ) )
    {
        *( hw_map->rx_dma_ifcr_reg ) = hw_map->rx_dma_ifcr_mask;
        HW_UART_Rx_Error_Handler( HW_UART_CHANNEL_2 );
    }

    else if ( LL_DMA_IsActiveFlag_HT5( DMA1 ) )
    {
        *( hw_map->rx_dma_ifcr_reg ) = hw_map->rx_dma_ifcr_mask;
    }

    else if ( LL_DMA_IsActiveFlag_TC5( DMA1 ) )
    {
        *( hw_map->rx_dma_ifcr_reg ) = hw_map->rx_dma_ifcr_mask;
    }
}