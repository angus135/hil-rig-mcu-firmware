/******************************************************************************
 *  File:       hw_spi_tx_config.c
 *  Author:     Angus Corr
 *  Created:    10-Apr-2026
 *
 *  Description:
 *      Shared TX configuration and common TX-side implementation for the
 *      low-level SPI driver used by the HIL-RIG firmware.
 *
 *      This file contains TX operation selection, common DMA flag handling,
 *      final-drain timer configuration, shared TX DMA programming, TX DMA IRQ
 *      entry points, TX error handling, and the public TX API functions that
 *      dispatch into the master/slave TX implementations.
 *
 *      Master-specific packet queue behaviour lives in hw_spi_tx_master.c.
 *      Slave-specific byte-stream behaviour lives in hw_spi_tx_slave.c. Shared
 *      helper prototypes needed across those translation units are declared in
 *      hw_spi_internal.h when HW_SPI_INTERNAL is enabled.
 *
 *  Notes:
 *      - This file intentionally keeps mode-independent TX plumbing in one
 *        place so the master and slave implementation files stay focused.
 *      - TX DMA is programmed as a normal/one-shot transfer, not circular.
 *      - RX DMA remains separate and is implemented in hw_spi_rx.c.
 *      - In master mode, every queued packet is automatically framed by
 *        software chip-select. DMA TC starts the transaction-completion path;
 *        CS is not released until the final SPI frame has drained.
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#define HW_SPI_INTERNAL
#include "hw_gpio.h"
#include "hw_spi.h"
#include "hw_timer.h"

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */

/*-----------------------------------------------------------------------------
 * SPI final-drain timer configuration
 *----------------------------------------------------------------------------*/

/*
 * Mocked timer prescalers for now.
 *
 * Assumption:
 *   - SPI channel 0 uses TIM11 on APB2 timer clock = 180 MHz
 *   - SPI channel 1 uses TIM6  on APB1 timer clock = 90 MHz
 *   - SPI DAC       uses TIM7  on APB1 timer clock = 90 MHz
 *
 * Target timer tick:
 *   1 MHz => 1 timer count = 1 us
 *
 * PSC = (timer_clock_hz / 1,000,000) - 1
 *
 * Update these once the final Cube clock tree is locked.
 */
#define SPI_CHANNEL_0_FINAL_DRAIN_TIMER_PSC 179U
#define SPI_CHANNEL_1_FINAL_DRAIN_TIMER_PSC 89U
#define SPI_DAC_FINAL_DRAIN_TIMER_PSC 89U

/*
 * Timer path is only used below SPI_BAUD_5M625BIT.
 *
 * ARR values below are deliberately conservative. They assume:
 *   - 8-bit SPI frames are the common case
 *   - 1 MHz timer tick
 *   - ARR is programmed directly as a delay count, not delay_us - 1
 *   - a small guard margin is included
 *
 * Approximate 8-bit final-drain times:
 *   2.813 Mbit/s  -> 2.84 us
 *   1.406 Mbit/s  -> 5.69 us
 *   703 kbit/s    -> 11.38 us
 *   352 kbit/s    -> 22.76 us
 *
 * Conservative ARR values:
 *   ceil(frame_time_us) + guard
 */
#define SPI_FINAL_DRAIN_ARR_2M813_8BIT 5U
#define SPI_FINAL_DRAIN_ARR_1M406_8BIT 8U
#define SPI_FINAL_DRAIN_ARR_703K_8BIT 14U
#define SPI_FINAL_DRAIN_ARR_352K_8BIT 26U

/*
 * Optional 16-bit values, in case 16-bit mode is enabled.
 *
 * Approximate 16-bit final-drain times:
 *   2.813 Mbit/s  -> 5.69 us
 *   1.406 Mbit/s  -> 11.38 us
 *   703 kbit/s    -> 22.76 us
 *   352 kbit/s    -> 45.51 us
 */
#define SPI_FINAL_DRAIN_ARR_2M813_16BIT 8U
#define SPI_FINAL_DRAIN_ARR_1M406_16BIT 14U
#define SPI_FINAL_DRAIN_ARR_703K_16BIT 26U
#define SPI_FINAL_DRAIN_ARR_352K_16BIT 50U

/*
 * For baud rates at or above 5.625 Mbit/s, the SPI path should use the inline
 * bounded wait rather than the timer. The timer may still be configured with a
 * harmless default value.
 */
#define SPI_FINAL_DRAIN_UNUSED_TIMER_ARR 1U

/*
 * Small guard added to the calculated final-frame wait. This is deliberately
 * conservative because the loop is only used after DMA TC, where the remaining
 * work should be at most the final SPI frame plus software latency margin.
 */
#define SPI_FINAL_DRAIN_GUARD_CYCLES 16U

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

/**
 * @brief Clear TX DMA terminal/error flags for SPI channel 0.
 *
 * @param dma
 *     DMA controller instance associated with the channel 0 TX stream.
 */
static inline void HW_SPI_TX_Clear_Channel_0_DMA_Flags( DMA_TypeDef* dma );
/**
 * @brief Clear TX DMA terminal/error flags for SPI channel 1.
 *
 * @param dma
 *     DMA controller instance associated with the channel 1 TX stream.
 */
static inline void HW_SPI_TX_Clear_Channel_1_DMA_Flags( DMA_TypeDef* dma );
/**
 * @brief Clear TX DMA terminal/error flags for the DAC SPI channel.
 *
 * @param dma
 *     DMA controller instance associated with the DAC TX stream.
 */
static inline void HW_SPI_TX_Clear_DAC_DMA_Flags( DMA_TypeDef* dma );
static uint32_t    HW_SPI_Get_Tx_Timer_Psc( const SPIPeripheralState_T* peripheral_state );
static uint32_t HW_SPI_Get_Tx_Final_Drain_Timer_Arr( const SPIPeripheralState_T* peripheral_state );
static void     HW_SPI_Configure_Tx_Timer( SPIPeripheralState_T* peripheral_state );
static bool     HW_SPI_TX_Is_Master( const SPIPeripheralState_T* peripheral_state );
/**
 * @brief Convert a configured SPI baud enum into CPU cycles per SCK period.
 *
 * @details
 *     The mapping assumes the current 180 MHz CPU clock and the SPI prescaler
 *     values selected in HW_SPI_Configure_Channel(). It is used only to size
 *     the fast-baud inline final-drain wait.
 *
 * @param baud_rate
 *     SPI baud-rate enum stored in the channel configuration.
 *
 * @return
 *     Approximate CPU cycles per SPI clock period.
 */
static uint32_t HW_SPI_TX_Get_Cycles_Per_SCK( SPIBaudRate_T baud_rate );
/**
 * @brief Calculate a bounded inline wait for the final SPI frame to drain.
 *
 * @param peripheral_state
 *     SPI channel state containing baud-rate and frame-size configuration.
 *
 * @return
 *     Approximate CPU-cycle count for one final SPI frame plus guard cycles.
 */
static uint32_t HW_SPI_TX_Get_Final_Drain_Cycles( const SPIPeripheralState_T* peripheral_state );
static void     HW_SPI_TX_Bounded_Final_Drain_Wait( const SPIPeripheralState_T* peripheral_state );
static void     HW_SPI_TX_Complete_Master_Transaction( SPIPeripheral_T       peripheral,
                                                       SPIPeripheralState_T* peripheral_state );
static void     HW_SPI_TX_Fault_Master_Transaction( SPIPeripheral_T       peripheral,
                                                    SPIPeripheralState_T* peripheral_state );
static bool     HW_SPI_TX_Try_Start_Final_Drain_Timer( SPIPeripheral_T       peripheral,
                                                       SPIPeripheralState_T* peripheral_state );
static void     HW_SPI_TX_Handle_Master_DMA_TC( SPIPeripheral_T       peripheral,
                                                SPIPeripheralState_T* peripheral_state );

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

static inline void HW_SPI_TX_Clear_Channel_0_DMA_Flags( DMA_TypeDef* dma )
{
    SPI_CHANNEL_0_TX_DMA_CLEAR_TC( dma );
    SPI_CHANNEL_0_TX_DMA_CLEAR_TE( dma );
}

static inline void HW_SPI_TX_Clear_Channel_1_DMA_Flags( DMA_TypeDef* dma )
{
    SPI_CHANNEL_1_TX_DMA_CLEAR_TC( dma );
    SPI_CHANNEL_1_TX_DMA_CLEAR_TE( dma );
}

static inline void HW_SPI_TX_Clear_DAC_DMA_Flags( DMA_TypeDef* dma )
{
    SPI_DAC_TX_DMA_CLEAR_TC( dma );
    SPI_DAC_TX_DMA_CLEAR_TE( dma );
}

/**
 * @brief Return the timer prescaler used for this SPI channel's final-drain timer.
 *
 * The prescaler values are currently mocked against the expected timer clocks.
 * They configure the selected timer to count at approximately 1 MHz so the ARR
 * value can be treated as a conservative microsecond delay.
 *
 * @param peripheral_state
 *     SPI channel state used to identify the associated final-drain timer.
 *
 * @return
 *     Timer prescaler value to pass to HW_TIMER_Configure_Timer().
 */
static uint32_t HW_SPI_Get_Tx_Timer_Psc( const SPIPeripheralState_T* peripheral_state )
{
    if ( peripheral_state == channel_0_state )
    {
        return SPI_CHANNEL_0_FINAL_DRAIN_TIMER_PSC;
    }

    if ( peripheral_state == channel_1_state )
    {
        return SPI_CHANNEL_1_FINAL_DRAIN_TIMER_PSC;
    }

    if ( peripheral_state == dac_state )
    {
        return SPI_DAC_FINAL_DRAIN_TIMER_PSC;
    }

    return SPI_DAC_FINAL_DRAIN_TIMER_PSC;
}

/**
 * @brief Return the final-drain timer ARR value for the configured SPI speed.
 *
 * The timer path is intended for slower SPI baud rates where an inline NOP wait
 * would keep the DMA ISR blocked for too long. The ARR values represent roughly
 * one final SPI frame time plus guard margin, expressed in 1 us timer ticks.
 *
 * For faster baud rates the timer is not expected to be used, but a harmless
 * fallback ARR is still returned so the timer can be configured consistently.
 *
 * @param peripheral_state
 *     SPI channel state containing configured baud rate and data size.
 *
 * @return
 *     Conservative ARR value for the final-drain one-shot timer.
 */
static uint32_t HW_SPI_Get_Tx_Final_Drain_Timer_Arr( const SPIPeripheralState_T* peripheral_state )
{
    bool use_16_bit = false;

    use_16_bit = peripheral_state->config.data_size == SPI_SIZE_16_BIT;

    switch ( peripheral_state->config.baud_rate )
    {
        case SPI_BAUD_45MBIT:
        case SPI_BAUD_22M5BIT:
        case SPI_BAUD_11M25BIT:
        case SPI_BAUD_5M625BIT:
            // These baud rates should use the inline NOP/bounded-wait path.
            // Configure a harmless fallback value anyway.
            return SPI_FINAL_DRAIN_UNUSED_TIMER_ARR;

        case SPI_BAUD_2M813BIT:
            return use_16_bit ? SPI_FINAL_DRAIN_ARR_2M813_16BIT : SPI_FINAL_DRAIN_ARR_2M813_8BIT;

        case SPI_BAUD_1M406BIT:
            return use_16_bit ? SPI_FINAL_DRAIN_ARR_1M406_16BIT : SPI_FINAL_DRAIN_ARR_1M406_8BIT;

        case SPI_BAUD_703KBIT:
            return use_16_bit ? SPI_FINAL_DRAIN_ARR_703K_16BIT : SPI_FINAL_DRAIN_ARR_703K_8BIT;

        case SPI_BAUD_352KBIT:
            return use_16_bit ? SPI_FINAL_DRAIN_ARR_352K_16BIT : SPI_FINAL_DRAIN_ARR_352K_8BIT;

        default:
            // Conservative fallback for unknown/invalid baud setting.
            return use_16_bit ? SPI_FINAL_DRAIN_ARR_352K_16BIT : SPI_FINAL_DRAIN_ARR_352K_8BIT;
    }
}

/**
 * @brief Configure the final-drain timer associated with a master-mode SPI channel.
 *
 * The final-drain timer is used after TX DMA completion when the selected SPI
 * baud rate is slow enough that waiting inline for the final SPI frame to leave
 * the peripheral would consume too much ISR time. The timer is only meaningful
 * for master-mode automatic-CS transfers; in slave mode an external master owns
 * chip-select timing and frame boundaries.
 *
 * @param peripheral_state
 *     SPI channel state to configure timer settings for.
 */
static void HW_SPI_Configure_Tx_Timer( SPIPeripheralState_T* peripheral_state )
{
    Timer_T  tx_timer = SPI_DAC_TIMER;
    uint32_t psc      = 0U;
    uint32_t arr      = 0U;

    if ( peripheral_state == NULL )
    {
        return;
    }

    // The final-drain timer is only meaningful for master-mode automatic-CS
    // transmission. In slave mode, chip-select/framing is controlled by the
    // external master, so there is no software-CS final-drain timer to configure.
    if ( peripheral_state->config.spi_mode != SPI_MASTER_MODE )
    {
        return;
    }

    tx_timer = HW_SPI_Get_Tx_Timer( peripheral_state );
    psc      = HW_SPI_Get_Tx_Timer_Psc( peripheral_state );
    arr      = HW_SPI_Get_Tx_Final_Drain_Timer_Arr( peripheral_state );

    HW_TIMER_Configure_Timer( tx_timer, psc, arr );
}

/**
 * @brief Return whether a channel is configured for master mode.
 *
 * @param peripheral_state
 *     SPI channel state to inspect.
 *
 * @return
 *     true when the channel is configured as an SPI master; false otherwise.
 */
static bool HW_SPI_TX_Is_Master( const SPIPeripheralState_T* peripheral_state )
{
    return ( peripheral_state != NULL ) && ( peripheral_state->config.spi_mode == SPI_MASTER_MODE );
}

static uint32_t HW_SPI_TX_Get_Cycles_Per_SCK( SPIBaudRate_T baud_rate )
{
    switch ( baud_rate )
    {
        case SPI_BAUD_45MBIT:
            return 4U;
        case SPI_BAUD_22M5BIT:
            return 8U;
        case SPI_BAUD_11M25BIT:
            return 16U;
        case SPI_BAUD_5M625BIT:
            return 32U;
        case SPI_BAUD_2M813BIT:
            return 64U;
        case SPI_BAUD_1M406BIT:
            return 128U;
        case SPI_BAUD_703KBIT:
            return 256U;
        case SPI_BAUD_352KBIT:
        default:
            return 512U;
    }
}

static uint32_t HW_SPI_TX_Get_Final_Drain_Cycles( const SPIPeripheralState_T* peripheral_state )
{
    uint32_t frame_bits = 8U;

    if ( peripheral_state == NULL )
    {
        return 0U;
    }

    if ( peripheral_state->config.data_size == SPI_SIZE_16_BIT )
    {
        frame_bits = 16U;
    }

    return ( frame_bits * HW_SPI_TX_Get_Cycles_Per_SCK( peripheral_state->config.baud_rate ) )
           + SPI_FINAL_DRAIN_GUARD_CYCLES;
}

/**
 * @brief Perform a short bounded inline wait for fast SPI final-drain handling.
 *
 * @details
 *     This path is used only for baud rates where the remaining final-frame
 *     delay is small enough to tolerate inside the TX DMA ISR. Slower baud
 *     rates use a one-shot timer so the ISR is not held for tens of
 *     microseconds.
 *
 * @param peripheral_state
 *     SPI channel state used to calculate the wait length.
 */
static void HW_SPI_TX_Bounded_Final_Drain_Wait( const SPIPeripheralState_T* peripheral_state )
{
    volatile uint32_t wait_cycles = HW_SPI_TX_Get_Final_Drain_Cycles( peripheral_state );

    while ( wait_cycles > 0U )
    {
        __asm volatile( "nop" );
        wait_cycles--;
    }
}

void HW_SPI_TX_Master_CS_Assert( SPIPeripheralState_T* peripheral_state )
{
    // TODO: Replace this hard-coded development CS assertion with the project GPIO driver.
    // The assert point must remain immediately before DMA is armed for the packet.
    // peripheral_state will be used once the CS line is selected via the GPIO driver.
    // ( void )peripheral_state;
    HAL_GPIO_WritePin( SPI1_CS_TEST_GPIO_Port, SPI1_CS_TEST_Pin, 0 );
}

void HW_SPI_TX_Master_CS_Deassert( SPIPeripheralState_T* peripheral_state )
{
    // TODO: Replace this hard-coded development CS deassertion with the project GPIO driver.
    // The deassert point must remain after final drain and BSY-clear confirmation.
    // peripheral_state will be used once the CS line is selected via the GPIO driver.
    // ( void )peripheral_state;
    HAL_GPIO_WritePin( SPI1_CS_TEST_GPIO_Port, SPI1_CS_TEST_Pin, 1 );
}

/**
 * @brief Start the one-shot final-drain timer for a slow master transaction.
 *
 * @details
 *     This function is called only after TX DMA TC has occurred and BSY is
 *     still asserted. It moves the transaction state into
 *     HW_SPI_TX_TRANSACTION_WAIT_FINAL_DRAIN before starting the timer so the
 *     timer callback can reject stale or early interrupts.
 *
 * @param peripheral
 *     Logical SPI peripheral whose transaction is waiting for final drain.
 *
 * @param peripheral_state
 *     SPI channel state for the active master transaction.
 *
 * @return
 *     true when the one-shot timer has been started.
 */
static bool HW_SPI_TX_Try_Start_Final_Drain_Timer( SPIPeripheral_T       peripheral,
                                                   SPIPeripheralState_T* peripheral_state )
{
    ( void )peripheral;
    // The final-drain timer is only valid after TX DMA TC has occurred.
    // At this point DMA has finished feeding the SPI peripheral, but the final
    // SPI frame may still be shifting out. Move the transaction into the
    // final-drain wait state before starting the timer so the timer callback can
    // distinguish a valid final-drain timeout from a stale/incorrect callback.
    peripheral_state->tx_transaction_state = HW_SPI_TX_TRANSACTION_WAIT_FINAL_DRAIN;

    Timer_T timer = HW_SPI_Get_Tx_Timer( peripheral_state );

    HW_TIMER_Start_Timer( timer );

    return true;
}

/**
 * @brief Complete one software-CS-framed master transaction.
 *
 * @details
 *     Deasserts CS for the packet whose SPI frame has fully drained. If more
 *     master packet descriptors are pending, the next packet is started
 *     immediately so the DMA/IRQ chain drains the queue without requiring
 *     another external HW_SPI_Tx_Trigger() call.
 *
 * @param peripheral
 *     Logical SPI peripheral that completed a transaction.
 *
 * @param peripheral_state
 *     SPI channel state containing the master packet queue.
 */
static void HW_SPI_TX_Complete_Master_Transaction( SPIPeripheral_T       peripheral,
                                                   SPIPeripheralState_T* peripheral_state )
{
    HW_SPI_TX_Master_CS_Deassert( peripheral_state );

    if ( peripheral_state->tx_has_pending_function( peripheral_state ) == false )
    {
        peripheral_state->tx_transaction_state = HW_SPI_TX_TRANSACTION_IDLE;
        return;
    }

    // The previous CS-framed transaction is now complete. If another master
    // packet is already queued, start it immediately so the DMA/IRQ chain drains
    // the packet queue without requiring another external trigger.
    //
    // The state is set back to IDLE before starting the next packet because
    // HW_SPI_TX_Start_Master_Packet_DMA() is the function that owns the
    // transition into DMA_ACTIVE for the next transaction.
    peripheral_state->tx_transaction_state = HW_SPI_TX_TRANSACTION_IDLE;

    if ( peripheral_state->tx_start_dma_function( peripheral_state ) == false )
    {
        HW_SPI_TX_Fault_Master_Transaction( peripheral, peripheral_state );
    }
}

/**
 * @brief Place the active master transaction into the TX error state.
 *
 * @details
 *     Used when DMA setup fails or final-drain completion does not reach a safe
 *     CS release point. The current policy disables further TX DMA requests,
 *     releases CS, clears the in-flight byte count, and leaves recovery to a
 *     higher-level reset/reconfiguration path.
 *
 * @param peripheral
 *     Logical SPI peripheral that faulted. Currently unused by this helper.
 *
 * @param peripheral_state
 *     SPI channel state to mark as failed.
 */
static void HW_SPI_TX_Fault_Master_Transaction( SPIPeripheral_T       peripheral,
                                                SPIPeripheralState_T* peripheral_state )
{
    ( void )peripheral;

    LL_SPI_DisableDMAReq_TX( peripheral_state->spi_peripheral );
    HW_SPI_TX_Master_CS_Deassert( peripheral_state );
    peripheral_state->tx_num_bytes_in_transmission = 0U;
    peripheral_state->tx_transaction_state         = HW_SPI_TX_TRANSACTION_ERROR;
}
/**
 * @brief Handle TX DMA transfer-complete for a master software-CS packet.
 *
 * @details
 *     DMA TC means DMA has finished feeding the SPI TX path, not that the last
 *     bit has physically left SCK/MOSI. This helper disables further TX DMA
 *     requests, waits for the final frame to drain using either inline delay or
 *     a one-shot timer, and completes/faults the transaction based on BSY.
 *
 * @param peripheral
 *     Logical SPI peripheral whose TX DMA stream completed.
 *
 * @param peripheral_state
 *     SPI channel state for the active master transaction.
 */
static void HW_SPI_TX_Handle_Master_DMA_TC( SPIPeripheral_T       peripheral,
                                            SPIPeripheralState_T* peripheral_state )
{
    peripheral_state->tx_num_bytes_in_transmission = 0U;

    // The one-shot DMA transfer has finished feeding the SPI TX path. Stop
    // further DMA requests before waiting for the peripheral to physically drain
    // the final frame.
    LL_SPI_DisableDMAReq_TX( peripheral_state->spi_peripheral );

    if ( peripheral_state->tx_transaction_state != HW_SPI_TX_TRANSACTION_DMA_ACTIVE )
    {
        HW_SPI_TX_Fault_Master_Transaction( peripheral, peripheral_state );
        return;
    }

    // DMA TC does not mean the last SPI bit is already off the pin. Check BSY
    // late, after the unavoidable ISR bookkeeping, then wait only if required.
    if ( LL_SPI_IsActiveFlag_BSY( peripheral_state->spi_peripheral ) == 0U )
    {
        HW_SPI_TX_Complete_Master_Transaction( peripheral, peripheral_state );
        return;
    }

    if ( HW_SPI_TX_Should_Use_Final_Drain_Timer( peripheral_state ) != false
         && HW_SPI_TX_Try_Start_Final_Drain_Timer( peripheral, peripheral_state ) != false )
    {
        return;
    }

    HW_SPI_TX_Bounded_Final_Drain_Wait( peripheral_state );

    if ( LL_SPI_IsActiveFlag_BSY( peripheral_state->spi_peripheral ) == 0U )
    {
        HW_SPI_TX_Complete_Master_Transaction( peripheral, peripheral_state );
        return;
    }

    HW_SPI_TX_Fault_Master_Transaction( peripheral, peripheral_state );
}

/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */

/**
 * @brief Return whether this SPI baud rate should use a final-drain timer.
 *
 * Fast SPI baud rates use a short inline bounded wait after TX DMA completion.
 * Slower SPI baud rates use a timer one-shot so the DMA ISR does not block for
 * several microseconds to tens of microseconds while waiting for the final SPI
 * frame to leave the peripheral.
 *
 * @param peripheral_state
 *     SPI channel state containing the configured baud rate.
 *
 * @return
 *     true if the timer path should be used; false if the inline wait path is
 *     preferred.
 */
bool HW_SPI_TX_Should_Use_Final_Drain_Timer( const SPIPeripheralState_T* peripheral_state )
{
    if ( peripheral_state == NULL )
    {
        return false;
    }

    switch ( peripheral_state->config.baud_rate )
    {
        case SPI_BAUD_45MBIT:
        case SPI_BAUD_22M5BIT:
        case SPI_BAUD_11M25BIT:
        case SPI_BAUD_5M625BIT:
            return false;

        case SPI_BAUD_2M813BIT:
        case SPI_BAUD_1M406BIT:
        case SPI_BAUD_703KBIT:
        case SPI_BAUD_352KBIT:
        default:
            return true;
    }
}

/**
 * @brief Return the timer assigned to a SPI peripheral's final-drain delay.
 *
 * Each SPI logical peripheral is mapped to a hardware timer used by the later
 * software chip-select completion path. The current mapping is intentionally
 * simple and should be updated if the final hardware allocation changes.
 *
 * @param peripheral_state
 *     SPI channel state used to identify the logical SPI peripheral.
 *
 * @return
 *     Timer_T value associated with the SPI channel.
 */
Timer_T HW_SPI_Get_Tx_Timer( const SPIPeripheralState_T* peripheral_state )
{
    if ( peripheral_state == channel_0_state )
    {
        return SPI_CHANNEL_0_TIMER;
    }

    if ( peripheral_state == channel_1_state )
    {
        return SPI_CHANNEL_1_TIMER;
    }

    if ( peripheral_state == dac_state )
    {
        return SPI_DAC_TIMER;
    }

    // Fallback. This should not be reached for a valid SPI state.
    // There is no INVALID_TIMER enum yet, so use a safe default.
    return SPI_DAC_TIMER;
}

uint32_t HW_SPI_TX_Get_Used_Space( const SPIPeripheralState_T* peripheral_state )
{
    // Both software-pending bytes and DMA-owned bytes occupy storage in
    // tx_buffer, so both must be included when checking whether new data fits.
    return peripheral_state->tx_num_bytes_pending + peripheral_state->tx_num_bytes_in_transmission;
}

uint32_t HW_SPI_TX_Get_Free_Space( const SPIPeripheralState_T* peripheral_state )
{
    // The caller is responsible for not overflowing the TX ring. This helper is
    // intentionally small because it is used on the load fast path.
    return TX_BUFFER_SIZE_BYTES - HW_SPI_TX_Get_Used_Space( peripheral_state );
}

/**
 * @brief Program the TX DMA stream for one selected linear memory span.
 *
 * @details
 *     This helper performs the common hardware programming used by both master
 *     packet TX and slave stream TX. Queue ownership has already been updated
 *     by the mode-specific caller before this function is entered. The SPI TX
 *     DMA request is disabled before stream reconfiguration and re-enabled only
 *     after the DMA stream is ready.
 *
 * @param peripheral_state
 *     Configured SPI channel state containing the DMA stream and SPI instance.
 *
 * @param tx_ptr
 *     Pointer to the first byte of the already-selected TX buffer span.
 *
 * @param size_bytes
 *     Number of bytes to hand to DMA. Must be aligned to the configured SPI
 *     frame size.
 *
 * @return
 *     true if the DMA stream was programmed and enabled; false if setup failed.
 */
bool HW_SPI_TX_Program_DMA( SPIPeripheralState_T* peripheral_state, uint8_t* tx_ptr,
                            uint32_t size_bytes )
{
    uint32_t dma_elements = 0U;

    if ( HW_SPI_Is_Frame_Aligned_Size( peripheral_state, size_bytes ) == false )
    {
        return false;
    }

    dma_elements = HW_SPI_Bytes_To_DMA_Elements( peripheral_state, size_bytes );

    // Disable the SPI-side DMA request before touching the DMA stream. This
    // prevents the peripheral from requesting a transfer while the stream is
    // being reconfigured for the next one-shot packet.
    LL_SPI_DisableDMAReq_TX( peripheral_state->spi_peripheral );

    LL_DMA_DisableStream( peripheral_state->tx_dma, peripheral_state->tx_dma_stream );
    while ( LL_DMA_IsEnabledStream( peripheral_state->tx_dma, peripheral_state->tx_dma_stream )
            != 0U )
    {
    }

    if ( peripheral_state->tx_clear_dma_flags_function == NULL )
    {
        return false;
    }

    peripheral_state->tx_clear_dma_flags_function( peripheral_state->tx_dma );

    LL_DMA_SetMemoryAddress( peripheral_state->tx_dma, peripheral_state->tx_dma_stream,
                             ( uintptr_t )tx_ptr );

    LL_DMA_SetPeriphAddress( peripheral_state->tx_dma, peripheral_state->tx_dma_stream,
                             LL_SPI_DMA_GetRegAddr( peripheral_state->spi_peripheral ) );

    LL_DMA_SetDataLength( peripheral_state->tx_dma, peripheral_state->tx_dma_stream, dma_elements );

    LL_DMA_EnableIT_TC( peripheral_state->tx_dma, peripheral_state->tx_dma_stream );
    LL_DMA_EnableIT_TE( peripheral_state->tx_dma, peripheral_state->tx_dma_stream );
    LL_DMA_EnableStream( peripheral_state->tx_dma, peripheral_state->tx_dma_stream );

    // Enable the SPI request only after the DMA stream is fully armed.

    LL_SPI_EnableDMAReq_TX( peripheral_state->spi_peripheral );

    return true;
}

/**
 * @brief Generic low-level TX DMA error handler.
 *
 * This function is called when a TX DMA transfer error is detected for a SPI
 * channel. It stops the active TX DMA stream, disables the SPI TX DMA request,
 * and clears the driver's in-flight TX tracking so higher-level software can
 * decide how to recover.
 *
 * @param peripheral
 *     The SPI peripheral/channel that encountered the TX DMA error.
 */
void HW_SPI_TX_Error_Handler( SPIPeripheral_T peripheral )
{
    SPIPeripheralState_T* peripheral_state = HW_SPI_Get_State( peripheral );

    // Stop further TX DMA activity for this channel. The error path disables
    // the stream and the SPI TX DMA request so the peripheral cannot keep
    // requesting data from a failed transfer.
    LL_DMA_DisableIT_TC( peripheral_state->tx_dma, peripheral_state->tx_dma_stream );
    LL_DMA_DisableIT_TE( peripheral_state->tx_dma, peripheral_state->tx_dma_stream );
    LL_SPI_DisableDMAReq_TX( peripheral_state->spi_peripheral );
    LL_DMA_DisableStream( peripheral_state->tx_dma, peripheral_state->tx_dma_stream );

    while ( LL_DMA_IsEnabledStream( peripheral_state->tx_dma, peripheral_state->tx_dma_stream )
            != 0U )
    {
    }

    // Drop knowledge of the currently active DMA transfer. The pending ring
    // state is left alone so a higher layer can decide whether to flush,
    // rebuild, or retry the transaction.
    peripheral_state->tx_num_bytes_in_transmission = 0U;

    if ( HW_SPI_TX_Is_Master( peripheral_state ) != false )
    {
        HW_SPI_TX_Master_CS_Deassert( peripheral_state );
        peripheral_state->tx_transaction_state = HW_SPI_TX_TRANSACTION_ERROR;
    }

    /* TODO: Add fault logging, error counters, or escalation here if desired. */
}

/**
 * @brief Shared TX DMA IRQ completion handler.
 *
 * @details
 *     Dispatches TX DMA completion differently for master and slave mode. Master
 *     mode enters the software-CS completion path because DMA TC is not the end
 *     of the electrical SPI transaction. Slave mode preserves stream behaviour
 *     and immediately arms the next pending contiguous span when available.
 *
 * @param peripheral
 *     Logical SPI peripheral whose TX DMA IRQ fired.
 */
void HW_SPI_TX_IRQ_Handler( SPIPeripheral_T peripheral )
{
    SPIPeripheralState_T* peripheral_state = HW_SPI_Get_State( peripheral );

    if ( HW_SPI_TX_Is_Master( peripheral_state ) != false )
    {
        HW_SPI_TX_Handle_Master_DMA_TC( peripheral, peripheral_state );
        return;
    }

    // Slave mode remains a raw stream. The completed TX span was already
    // removed from the pending software state when DMA was started. The IRQ only
    // marks the active DMA span as complete, then starts more pending stream data
    // if available.
    peripheral_state->tx_num_bytes_in_transmission = 0U;

    if ( peripheral_state->tx_has_pending_function( peripheral_state ) == false )
    {
        return;
    }

    if ( peripheral_state->tx_start_dma_function( peripheral_state ) == false )
    {
        HW_SPI_TX_Error_Handler( peripheral );
    }
}

/**
 * @brief Reset TX queue, descriptor, and transaction bookkeeping.
 *
 * @details
 *     This is called during channel configuration and recovery-style setup. It
 *     does not reconfigure hardware; it only returns the driver's software TX
 *     state to an empty idle condition.
 *
 * @param peripheral_state
 *     SPI channel state to reset.
 */
void HW_SPI_TX_Reset_State( SPIPeripheralState_T* peripheral_state )
{
    if ( peripheral_state == NULL )
    {
        return;
    }

    // Reset the circular TX queue state. Pending bytes are bytes still in the
    // software ring. In-flight bytes are bytes already handed to DMA.
    peripheral_state->tx_write_position            = 0U;
    peripheral_state->tx_read_position             = 0U;
    peripheral_state->tx_num_bytes_pending         = 0U;
    peripheral_state->tx_num_bytes_in_transmission = 0U;
    peripheral_state->tx_transaction_state         = HW_SPI_TX_TRANSACTION_IDLE;

    // Reset packet descriptor queue state. Descriptor contents are cleared only
    // for debug/readability; queue validity is controlled by the explicit
    // packet read/write/count fields rather than by descriptor contents.
    peripheral_state->tx_packet_write_position = 0U;
    peripheral_state->tx_packet_read_position  = 0U;
    peripheral_state->tx_num_packets_pending   = 0U;
    memset( peripheral_state->tx_packet_descriptors, 0,
            sizeof( peripheral_state->tx_packet_descriptors ) );
}

/**
 * @brief Configure TX operation function pointers for the selected SPI mode.
 *
 * This is called during channel configuration so TX hot paths can dispatch
 * directly through function pointers rather than branching on master/slave mode.
 */
/**
 * @brief Select mode-specific TX operation function pointers.
 *
 * @details
 *     Master and slave TX have different queue semantics. Configuration selects
 *     the appropriate load, pending, and DMA-start functions once so the hot
 *     paths can dispatch through function pointers without repeatedly branching
 *     on the configured SPI mode.
 *
 * @param peripheral_state
 *     SPI channel state whose TX operations should be selected.
 */
void HW_SPI_TX_Configure_Operations( SPIPeripheralState_T* peripheral_state )
{
    if ( peripheral_state == NULL )
    {
        return;
    }

    switch ( peripheral_state->config.spi_mode )
    {
        case SPI_MASTER_MODE:
            peripheral_state->tx_load_function        = HW_SPI_TX_Load_Master_Packet;
            peripheral_state->tx_start_dma_function   = HW_SPI_TX_Start_Master_Packet_DMA;
            peripheral_state->tx_has_pending_function = HW_SPI_TX_Master_Has_Pending;
            HW_SPI_Configure_Tx_Timer( peripheral_state );
            break;

        case SPI_SLAVE_MODE:
            peripheral_state->tx_load_function        = HW_SPI_TX_Load_Slave_Stream;
            peripheral_state->tx_start_dma_function   = HW_SPI_TX_Start_Slave_Stream_DMA;
            peripheral_state->tx_has_pending_function = HW_SPI_TX_Slave_Has_Pending;
            break;

        default:
            peripheral_state->tx_load_function        = NULL;
            peripheral_state->tx_start_dma_function   = NULL;
            peripheral_state->tx_has_pending_function = NULL;
            break;
    }

    if ( peripheral_state == channel_0_state )
    {
        peripheral_state->tx_clear_dma_flags_function = HW_SPI_TX_Clear_Channel_0_DMA_Flags;
    }
    else if ( peripheral_state == channel_1_state )
    {
        peripheral_state->tx_clear_dma_flags_function = HW_SPI_TX_Clear_Channel_1_DMA_Flags;
    }
    else if ( peripheral_state == dac_state )
    {
        peripheral_state->tx_clear_dma_flags_function = HW_SPI_TX_Clear_DAC_DMA_Flags;
    }
    else
    {
        peripheral_state->tx_clear_dma_flags_function = NULL;
    }
}

void SPI_CHANNEL_0_TX_DMA_IRQ( void )
{
    // Handle transfer error first. If TE and TC are both latched, error
    // handling wins and normal completion processing is skipped.
    if ( SPI_CHANNEL_0_TX_DMA_IS_ACTIVE_TE( SPI_CHANNEL_0_TX_DMA ) != 0U )
    {
        SPI_CHANNEL_0_TX_DMA_CLEAR_TE( SPI_CHANNEL_0_TX_DMA );
        HW_SPI_TX_Error_Handler( SPI_CHANNEL_0 );
        return;
    }

    if ( SPI_CHANNEL_0_TX_DMA_IS_ACTIVE_TC( SPI_CHANNEL_0_TX_DMA ) != 0U )
    {
        SPI_CHANNEL_0_TX_DMA_CLEAR_TC( SPI_CHANNEL_0_TX_DMA );
        HW_SPI_TX_IRQ_Handler( SPI_CHANNEL_0 );
        return;
    }
}

void SPI_CHANNEL_1_TX_DMA_IRQ( void )
{
    // Handle transfer error first. If TE and TC are both latched, error
    // handling wins and normal completion processing is skipped.
    if ( SPI_CHANNEL_1_TX_DMA_IS_ACTIVE_TE( SPI_CHANNEL_1_TX_DMA ) != 0U )
    {
        SPI_CHANNEL_1_TX_DMA_CLEAR_TE( SPI_CHANNEL_1_TX_DMA );
        HW_SPI_TX_Error_Handler( SPI_CHANNEL_1 );
        return;
    }

    if ( SPI_CHANNEL_1_TX_DMA_IS_ACTIVE_TC( SPI_CHANNEL_1_TX_DMA ) != 0U )
    {
        SPI_CHANNEL_1_TX_DMA_CLEAR_TC( SPI_CHANNEL_1_TX_DMA );
        HW_SPI_TX_IRQ_Handler( SPI_CHANNEL_1 );
        return;
    }
}

/*
 * Note: Not implemented yet as Channel 1 and DAC are on the same port right now.
 *
 * void SPI_DAC_TX_DMA_IRQ( void )
 * {
 *     if ( LL_DMA_IsActiveFlag_TE1( SPI_DAC_TX_DMA ) != 0U )
 *     {
 *         LL_DMA_ClearFlag_TE1( SPI_DAC_TX_DMA );
 *         HW_SPI_TX_Error_Handler( SPI_DAC );
 *         return;
 *     }
 *
 *     if ( LL_DMA_IsActiveFlag_TC1( SPI_DAC_TX_DMA ) != 0U )
 *     {
 *         LL_DMA_ClearFlag_TC1( SPI_DAC_TX_DMA );
 *         HW_SPI_TX_IRQ_Handler( SPI_DAC );
 *         return;
 *     }
 * }
 */

/**
 * @brief Complete slow-baud final-drain handling from the SPI timer ISR.
 *
 * @details
 *     This callback is valid only while a master transaction is in
 *     HW_SPI_TX_TRANSACTION_WAIT_FINAL_DRAIN. It checks BSY after the one-shot
 *     timer delay and either completes the software-CS transaction or marks it
 *     faulted.
 *
 * @param peripheral
 *     Logical SPI peripheral whose final-drain timer elapsed.
 */
void HW_SPI_Timer_Callback_From_ISR( SPIPeripheral_T peripheral )
{
    SPIPeripheralState_T* peripheral_state = HW_SPI_Get_State( peripheral );

    if ( HW_SPI_TX_Is_Master( peripheral_state ) == false
         || peripheral_state->tx_transaction_state != HW_SPI_TX_TRANSACTION_WAIT_FINAL_DRAIN )
    {
        return;
    }

    if ( LL_SPI_IsActiveFlag_BSY( peripheral_state->spi_peripheral ) == 0U )
    {
        HW_SPI_TX_Complete_Master_Transaction( peripheral, peripheral_state );
        return;
    }

    HW_SPI_TX_Fault_Master_Transaction( peripheral, peripheral_state );
}

/**
 * @brief Public TX load wrapper.
 *
 * @details
 *     Copies caller data into the selected channel's internal TX storage and
 *     dispatches to the configured master/slave load function. Master mode
 *     records one packet descriptor per call; slave mode appends raw stream
 *     bytes. This function only queues data and does not require the TX engine
 *     to be idle.
 *
 * @param peripheral
 *     Logical SPI peripheral whose TX queue should receive data.
 *
 * @param data
 *     Pointer to caller-owned source data. The bytes are copied before return.
 *
 * @param size
 *     Number of bytes to queue. Must be aligned to the configured SPI frame
 *     size.
 *
 * @return
 *     true if the data was queued; false if the peripheral is invalid, no load
 *     operation is configured, or insufficient queue space is available.
 */
bool HW_SPI_Load_Tx_Buffer( SPIPeripheral_T peripheral, const uint8_t* data, uint32_t size )
{
    SPIPeripheralState_T* peripheral_state = HW_SPI_Get_State( peripheral );
    bool                  accepted         = false;

    // Prevent the TX DMA IRQ from modifying pending/in-flight state while the
    // selected TX load implementation calculates free space and updates the
    // queue state. This disables only the channel's TX DMA IRQ, not global
    // interrupts.
    NVIC_DisableIRQ( peripheral_state->tx_dma_irqn );
    accepted = peripheral_state->tx_load_function( peripheral_state, data, size );
    NVIC_EnableIRQ( peripheral_state->tx_dma_irqn );

    return accepted;
}

/**
 * @brief Kick the TX engine for a channel with queued data.
 *
 * @details
 *     This function starts transmission only when the selected channel is idle.
 *     It does not drain the queue in a loop. Master mode automatically starts
 *     subsequent queued packets from the transaction-completion path after each
 *     packet's CS has been released. Slave mode re-arms contiguous stream spans
 *     from the TX DMA IRQ handler.
 *
 * @param peripheral
 *     Logical SPI peripheral whose TX engine should be kicked.
 */
void HW_SPI_Tx_Trigger( SPIPeripheral_T peripheral )
{
    SPIPeripheralState_T* peripheral_state = HW_SPI_Get_State( peripheral );

    // Protect against a race with the TX DMA IRQ handler. We only disable the
    // specific DMA IRQ for this channel, not global interrupts.
    NVIC_DisableIRQ( peripheral_state->tx_dma_irqn );

    // If DMA is already active or the selected TX mode has no queued work,
    // leave the queue unchanged. The mode-specific pending check is dispatched
    // through the configured function pointer.
    if ( peripheral_state->tx_num_bytes_in_transmission > 0U
         || peripheral_state->tx_transaction_state != HW_SPI_TX_TRANSACTION_IDLE
         || peripheral_state->tx_has_pending_function( peripheral_state ) == false )
    {
        NVIC_EnableIRQ( peripheral_state->tx_dma_irqn );
        return;
    }

    if ( peripheral_state->tx_clear_dma_flags_function != NULL )
    {
        peripheral_state->tx_clear_dma_flags_function( peripheral_state->tx_dma );
    }

    if ( peripheral_state->tx_start_dma_function( peripheral_state ) == false )
    {
        if ( peripheral_state->config.spi_mode == SPI_MASTER_MODE )
        {
            HW_SPI_TX_Fault_Master_Transaction( peripheral, peripheral_state );
        }
        else
        {
            HW_SPI_TX_Error_Handler( peripheral );
        }
    }

    NVIC_EnableIRQ( peripheral_state->tx_dma_irqn );
}

/**
 * @brief Return whether a channel has no pending or active TX data.
 *
 * @param peripheral
 *     Logical SPI peripheral to inspect.
 *
 * @return
 *     true when no software-pending bytes and no DMA-owned bytes remain; false
 *     otherwise.
 */
bool HW_SPI_Tx_Buffer_Empty( SPIPeripheral_T peripheral )
{
    SPIPeripheralState_T* state = HW_SPI_Get_State( peripheral );

    /* Empty only means no pending software-ring bytes and no in-flight DMA bytes. */
    return HW_SPI_TX_Get_Used_Space( state ) == 0U;
}
