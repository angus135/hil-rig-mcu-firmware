/******************************************************************************
 *  File:       hw_spi_tx_config.c
 *  Author:     Angus Corr
 *  Created:    10-Apr-2026
 *
 *  Description:
 *      Shared TX configuration and common TX-side implementation for the
 *      low-level SPI driver used by the HIL-RIG firmware.
 *
 *      This file contains common DMA flag handling,
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
 *   - the SPI DAC timer may run for two bounded intervals because DMA TC can
 *     leave one frame shifting while another frame is buffered in SPI->DR
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

static const uint32_t SPI_FINAL_DRAIN_TIMER_PSC_ARRAY[SPI_NUM_CHANNELS] = {
    [SPI_CHANNEL_0] = SPI_CHANNEL_0_FINAL_DRAIN_TIMER_PSC,
    [SPI_CHANNEL_1] = SPI_CHANNEL_1_FINAL_DRAIN_TIMER_PSC,
    [SPI_DAC]       = SPI_DAC_FINAL_DRAIN_TIMER_PSC,
};

/**-----------------------------------------------------------------------------
 *  Private (static) Function Prototypes
 *------------------------------------------------------------------------------
 */

static uint32_t HW_SPI_Get_Tx_Timer_Psc( const SPIPeripheralState_T* peripheral_state );
static uint32_t HW_SPI_Get_Tx_Final_Drain_Timer_Arr( const SPIPeripheralState_T* peripheral_state );
static void     HW_SPI_Configure_Tx_Timer( SPIPeripheralState_T* peripheral_state );
HW_SPI_ALWAYS_INLINE void
HW_SPI_TX_Bounded_Final_Drain_Wait( const SPIPeripheralState_T* peripheral_state );
HW_SPI_ALWAYS_INLINE void
                                 HW_SPI_TX_Complete_Master_Transaction( SPIChannel_T          peripheral,
                                                                        SPIPeripheralState_T* peripheral_state );
static void HW_SPI_COLD_NOINLINE HW_SPI_TX_Fault_Master_Transaction(
    SPIChannel_T peripheral, SPIPeripheralState_T* peripheral_state );
HW_SPI_ALWAYS_INLINE bool
                          HW_SPI_TX_Try_Start_Final_Drain_Timer( SPIChannel_T          peripheral,
                                                                 SPIPeripheralState_T* peripheral_state );
HW_SPI_ALWAYS_INLINE void HW_SPI_TX_Handle_Master_DMA_TC( SPIChannel_T          peripheral,
                                                          SPIPeripheralState_T* peripheral_state );
HW_SPI_ALWAYS_INLINE void HW_SPI_TX_Handle_Slave_DMA_TC( SPIChannel_T          peripheral,
                                                         SPIPeripheralState_T* peripheral_state );

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

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
    return SPI_FINAL_DRAIN_TIMER_PSC_ARRAY[( uint32_t )peripheral_state->logical_peripheral];
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
            // Return 0 if invalid
            return 0;
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
HW_SPI_ALWAYS_INLINE void
HW_SPI_TX_Bounded_Final_Drain_Wait( const SPIPeripheralState_T* peripheral_state )
{
    volatile uint32_t wait_cycles = peripheral_state->tx_final_drain_cycles;

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
    ( void )peripheral_state;
#ifndef TEST_BUILD
    HAL_GPIO_WritePin( SPI1_CS_TEST_GPIO_Port, SPI1_CS_TEST_Pin, 0 );
#endif
}

void HW_SPI_TX_Master_CS_Deassert( SPIPeripheralState_T* peripheral_state )
{
    // TODO: Replace this hard-coded development CS deassertion with the project GPIO driver.
    // The deassert point must remain after final drain and BSY-clear confirmation.
    // peripheral_state will be used once the CS line is selected via the GPIO driver.
    ( void )peripheral_state;
#ifndef TEST_BUILD
    HAL_GPIO_WritePin( SPI1_CS_TEST_GPIO_Port, SPI1_CS_TEST_Pin, 1 );
#endif
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
HW_SPI_ALWAYS_INLINE bool
HW_SPI_TX_Try_Start_Final_Drain_Timer( SPIChannel_T          peripheral,
                                       SPIPeripheralState_T* peripheral_state )
{
    ( void )peripheral;
    // The final-drain timer is only valid after TX DMA TC has occurred.
    // At this point DMA has finished feeding the SPI peripheral, but the final
    // SPI frame may still be shifting out. Move the transaction into the
    // final-drain wait state before starting the timer so the timer callback can
    // distinguish a valid final-drain timeout from a stale/incorrect callback.
    peripheral_state->tx_transaction_state          = HW_SPI_TX_TRANSACTION_WAIT_FINAL_DRAIN;
    peripheral_state->tx_final_drain_timer_attempts = 1U;

    HW_TIMER_Start_Timer( peripheral_state->tx_final_drain_timer );

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
HW_SPI_ALWAYS_INLINE void
HW_SPI_TX_Complete_Master_Transaction( SPIChannel_T          peripheral,
                                       SPIPeripheralState_T* peripheral_state )
{
#ifndef TEST_BUILD
    // A two-line channel without RX DMA still receives a frame for every frame
    // transmitted. Once BSY is clear, read DR then SR to clear RXNE/OVR before
    // starting the next software-CS transaction. Full-duplex channels continue
    // to have RX DMA drain DR and do not use this path.
    if ( peripheral_state->rx_dma == NULL )
    {
        LL_SPI_ClearFlag_OVR( peripheral_state->spi_peripheral );
    }
#endif

    peripheral_state->tx_final_drain_timer_attempts = 0U;
    HW_SPI_TX_Master_CS_Deassert( peripheral_state );

    if ( peripheral_state->tx_num_packets_pending == 0U )
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

    if ( HW_SPI_TX_Start_Master_Packet_DMA( peripheral_state ) == false )
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
static void HW_SPI_COLD_NOINLINE HW_SPI_TX_Fault_Master_Transaction(
    SPIChannel_T peripheral, SPIPeripheralState_T* peripheral_state )
{
    ( void )peripheral;

    LL_SPI_DisableDMAReq_TX( peripheral_state->spi_peripheral );
    HW_SPI_TX_Master_CS_Deassert( peripheral_state );
    peripheral_state->tx_num_bytes_in_transmission  = 0U;
    peripheral_state->tx_final_drain_timer_attempts = 0U;
    peripheral_state->tx_transaction_state          = HW_SPI_TX_TRANSACTION_ERROR;
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
HW_SPI_ALWAYS_INLINE void HW_SPI_TX_Handle_Master_DMA_TC( SPIChannel_T          peripheral,
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

    if ( peripheral_state->tx_uses_final_drain_timer != false
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

HW_SPI_ALWAYS_INLINE void HW_SPI_TX_Handle_Slave_DMA_TC( SPIChannel_T          peripheral,
                                                         SPIPeripheralState_T* peripheral_state )
{
    peripheral_state->tx_num_bytes_in_transmission = 0U;

    if ( peripheral_state->tx_num_bytes_pending == 0U )
    {
        return;
    }

    if ( HW_SPI_TX_Start_Slave_Stream_DMA( peripheral_state ) == false )
    {
        HW_SPI_TX_Error_Handler( peripheral );
    }
}

/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */

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
    return peripheral_state->tx_final_drain_timer;
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
void HW_SPI_COLD_NOINLINE HW_SPI_TX_Error_Handler( SPIChannel_T peripheral )
{
    SPIPeripheralState_T* peripheral_state = HW_SPI_Get_State( peripheral );
    if ( peripheral_state == NULL )
    {
        return;  // TODO: hnalde this
    }

    // Stop further TX DMA activity for this channel. The error path disables
    // the stream and the SPI TX DMA request so the peripheral cannot keep
    // requesting data from a failed transfer.
    LL_DMA_DisableIT_TC( peripheral_state->tx_dma, peripheral_state->tx_dma_stream );
    LL_DMA_DisableIT_TE( peripheral_state->tx_dma, peripheral_state->tx_dma_stream );
    LL_SPI_DisableDMAReq_TX( peripheral_state->spi_peripheral );
    LL_DMA_DisableStream( peripheral_state->tx_dma, peripheral_state->tx_dma_stream );

    uint32_t timeout = HW_SPI_DMA_DISABLE_TIMEOUT_ITERATIONS;
    while ( LL_DMA_IsEnabledStream( peripheral_state->tx_dma, peripheral_state->tx_dma_stream )
            != 0U )
    {
        if ( timeout == 0U )
        {
            break;
        }
        timeout--;
    }

    // Drop knowledge of the currently active DMA transfer. The pending ring
    // state is left alone so a higher layer can decide whether to flush,
    // rebuild, or retry the transaction.
    peripheral_state->tx_num_bytes_in_transmission  = 0U;
    peripheral_state->tx_final_drain_timer_attempts = 0U;

    if ( peripheral_state->is_master != false )
    {
        HW_SPI_TX_Master_CS_Deassert( peripheral_state );
        peripheral_state->tx_transaction_state = HW_SPI_TX_TRANSACTION_ERROR;
    }

    /* TODO: Add fault logging, error counters, or escalation here if desired. */
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
    peripheral_state->tx_write_position             = 0U;
    peripheral_state->tx_read_position              = 0U;
    peripheral_state->tx_num_bytes_pending          = 0U;
    peripheral_state->tx_num_bytes_in_transmission  = 0U;
    peripheral_state->tx_final_drain_timer_attempts = 0U;
    peripheral_state->tx_transaction_state          = HW_SPI_TX_TRANSACTION_IDLE;

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
 * @brief Configure TX Timer support for a SPI channel.
 *
 * @details
 *     Performs TX-side setup that depends on the configured channel mode.
 *
 *     The current TX implementation calls the concrete master/slave functions
 *     directly in the runtime paths rather than using mode-specific function
 *     pointers. This avoids indirect-call overhead in ISR and frequently used
 *     TX paths.
 *
 *     For master-mode channels, this function configures the final-drain timer
 *     used after TX DMA completion. The timer allows slower SPI transfers to
 *     wait for the final frame to physically leave the peripheral without
 *     blocking inside the DMA interrupt for the full drain time.
 *
 *     Slave-mode channels do not require final-drain timer configuration because
 *     the external SPI master owns clocking and chip-select timing.
 *
 * @param peripheral_state
 *     SPI channel state whose TX support should be configured.
 */
void HW_SPI_TX_Configure_Timer( SPIPeripheralState_T* peripheral_state )
{
    if ( peripheral_state == NULL )
    {
        return;
    }

    if ( peripheral_state->is_master )
    {
        HW_SPI_Configure_Tx_Timer( peripheral_state );
    }
}

void SPI_CHANNEL_0_TX_DMA_IRQ( void )
{
    SPIPeripheralState_T* peripheral_state = &( channel_state_array[SPI_CHANNEL_0] );
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
        if ( peripheral_state->is_master != false )
        {
            HW_SPI_TX_Handle_Master_DMA_TC( SPI_CHANNEL_0, peripheral_state );
        }
        else
        {
            HW_SPI_TX_Handle_Slave_DMA_TC( SPI_CHANNEL_0, peripheral_state );
        }
        return;
    }
}

void SPI_CHANNEL_1_TX_DMA_IRQ( void )
{
    SPIPeripheralState_T* peripheral_state = &( channel_state_array[SPI_CHANNEL_1] );
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
        if ( peripheral_state->is_master != false )
        {
            HW_SPI_TX_Handle_Master_DMA_TC( SPI_CHANNEL_1, peripheral_state );
        }
        else
        {
            HW_SPI_TX_Handle_Slave_DMA_TC( SPI_CHANNEL_1, peripheral_state );
        }
        return;
    }
}

void SPI_DAC_TX_DMA_IRQ( void )
{
    SPIPeripheralState_T* dac_state = &( channel_state_array[SPI_DAC] );
    // Handle transfer error first. If TE and TC are both latched, error
    // handling wins and normal completion processing is skipped.
    if ( SPI_DAC_TX_DMA_IS_ACTIVE_TE( SPI_DAC_TX_DMA ) != 0U )
    {
        SPI_DAC_TX_DMA_CLEAR_TE( SPI_DAC_TX_DMA );
        HW_SPI_TX_Error_Handler( SPI_DAC );
        return;
    }

    if ( SPI_DAC_TX_DMA_IS_ACTIVE_TC( SPI_DAC_TX_DMA ) != 0U )
    {
        SPI_DAC_TX_DMA_CLEAR_TC( SPI_DAC_TX_DMA );
        if ( dac_state->is_master != false )
        {
            HW_SPI_TX_Handle_Master_DMA_TC( SPI_DAC, dac_state );
        }
        else
        {
            HW_SPI_TX_Handle_Slave_DMA_TC( SPI_DAC, dac_state );
        }
        return;
    }
}

/**
 * @brief Complete slow-baud final-drain handling from the SPI timer ISR.
 *
 * @details
 *     This callback is valid only while a master transaction is in
 *     HW_SPI_TX_TRANSACTION_WAIT_FINAL_DRAIN. It checks BSY after the one-shot
 *     timer delay and either completes the software-CS transaction or marks it
 *     faulted. SPI_DAC may start one final bounded interval and keeps CS asserted
 *     if SPI remains busy after that interval.
 *
 * @param peripheral
 *     Logical SPI peripheral whose final-drain timer elapsed.
 */
void HW_SPI_Timer_Callback_From_ISR( SPIChannel_T peripheral )
{
    SPIPeripheralState_T* peripheral_state = HW_SPI_Get_State_Fast( peripheral );

    if ( peripheral_state->is_master == false
         || peripheral_state->tx_transaction_state != HW_SPI_TX_TRANSACTION_WAIT_FINAL_DRAIN )
    {
        return;
    }

    if ( LL_SPI_IsActiveFlag_BSY( peripheral_state->spi_peripheral ) == 0U )
    {
        HW_SPI_TX_Complete_Master_Transaction( peripheral, peripheral_state );
        return;
    }

    if ( peripheral == SPI_DAC
         && peripheral_state->tx_final_drain_timer_attempts
                < SPI_DAC_FINAL_DRAIN_TIMER_MAX_ATTEMPTS )
    {
        peripheral_state->tx_final_drain_timer_attempts++;
        HW_TIMER_Start_Timer( peripheral_state->tx_final_drain_timer );
        return;
    }

    if ( peripheral == SPI_DAC )
    {
        // The bounded DAC drain period expired with SPI still busy. Stop further DMA
        // requests and expose the existing error state, but keep CS asserted.
        LL_SPI_DisableDMAReq_TX( peripheral_state->spi_peripheral );
        peripheral_state->tx_num_bytes_in_transmission  = 0U;
        peripheral_state->tx_final_drain_timer_attempts = 0U;
        peripheral_state->tx_transaction_state          = HW_SPI_TX_TRANSACTION_ERROR;
        return;
    }

    HW_SPI_TX_Fault_Master_Transaction( peripheral, peripheral_state );
}

/**
 * @brief Public TX load wrapper.
 *
 * @details
 *     Copies caller data into the selected channel's internal TX storage.
 *     Master mode records one packet descriptor per call; slave mode appends raw
 *     stream bytes. This function only queues data and does not require the TX
 *     engine to be idle.
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
 *     true if the data was queued; false if the peripheral is invalid, the size
 *     is not frame-aligned, or insufficient queue space is available.
 */
bool HW_SPI_Load_Tx_Buffer( SPIChannel_T peripheral, const uint8_t* data, uint32_t size )
{
    SPIPeripheralState_T* peripheral_state = HW_SPI_Get_State_Fast( peripheral );
    bool                  accepted         = false;

    // Prevent the TX DMA IRQ from modifying pending/in-flight state while the
    // selected TX load implementation calculates free space and updates the
    // queue state. This disables only the channel's TX DMA IRQ, not global
    // interrupts.
    NVIC_DisableIRQ( peripheral_state->tx_dma_irqn );

    if ( peripheral_state->is_master != false )
    {
        accepted = HW_SPI_TX_Load_Master_Packet( peripheral_state, data, size );
    }
    else
    {
        accepted = HW_SPI_TX_Load_Slave_Stream( peripheral_state, data, size );
    }

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
void HW_SPI_Tx_Trigger( SPIChannel_T peripheral )
{
    SPIPeripheralState_T* peripheral_state = HW_SPI_Get_State_Fast( peripheral );

    // Protect against a race with the TX DMA IRQ handler. We only disable the
    // specific DMA IRQ for this channel, not global interrupts.
    NVIC_DisableIRQ( peripheral_state->tx_dma_irqn );

    // Trigger is only a kick. If a transaction/stream span is already active,
    // queued bytes remain pending and will be drained by the DMA completion path.
    if ( peripheral_state->tx_num_bytes_in_transmission > 0U
         || peripheral_state->tx_transaction_state != HW_SPI_TX_TRANSACTION_IDLE )
    {
        NVIC_EnableIRQ( peripheral_state->tx_dma_irqn );
        return;
    }

    if ( peripheral_state->is_master != false )
    {
        if ( peripheral_state->tx_num_packets_pending == 0U )
        {
            NVIC_EnableIRQ( peripheral_state->tx_dma_irqn );
            return;
        }

        if ( HW_SPI_TX_Start_Master_Packet_DMA( peripheral_state ) == false )
        {
            HW_SPI_TX_Fault_Master_Transaction( peripheral, peripheral_state );
        }
    }
    else
    {
        if ( peripheral_state->tx_num_bytes_pending == 0U )
        {
            NVIC_EnableIRQ( peripheral_state->tx_dma_irqn );
            return;
        }

        if ( HW_SPI_TX_Start_Slave_Stream_DMA( peripheral_state ) == false )
        {
            HW_SPI_TX_Error_Handler( peripheral );
        }
    }

    NVIC_EnableIRQ( peripheral_state->tx_dma_irqn );
}

/**
 * @brief Check whether TX activity has fully completed for an SPI channel.
 *
 * Reports whether the selected channel has no TX data waiting in the software
 * queue, no TX data currently owned by DMA, and no frame still being shifted by
 * the SPI peripheral.
 *
 * In the TX path, data/activity can exist in several places:
 * - pending bytes still waiting in the software TX queue,
 * - in-flight bytes that have already been handed to DMA,
 * - a final SPI frame that has left DMA but is still shifting out of the SPI
 *   peripheral, and
 * - in master mode, a software-CS-framed transaction that is still waiting for
 *   final-drain completion.
 *
 * This function treats TX as complete only when all software/DMA byte counts are
 * zero, the SPI peripheral BSY flag is clear, and, for master mode, the internal
 * TX transaction state has returned to idle. This makes the function suitable
 * for higher-level code that needs to know whether previously loaded TX data has
 * fully completed electrically, not just whether the software TX buffer has
 * available space.
 *
 * This function assumes the caller provides a valid SPI peripheral. Invalid
 * peripheral validation is intentionally not performed here because this is a
 * lightweight low-level helper intended for frequent use.
 *
 * @param peripheral
 *     The SPI peripheral/channel to inspect.
 *
 * @return
 *     true if there are no pending bytes, no DMA-owned bytes, the SPI peripheral
 *     is not busy, and any master-mode software-CS transaction has completed.
 *     false if TX data is still pending, DMA is still active, the SPI peripheral
 *     is still busy, or a master-mode final-drain transaction is still active.
 */
bool HW_SPI_Tx_Is_Complete( SPIChannel_T peripheral )
{
    SPIPeripheralState_T* state = HW_SPI_Get_State_Fast( peripheral );

    // First check the driver-owned TX storage. This includes both bytes still
    // waiting in the software queue and bytes already handed to DMA.
    if ( HW_SPI_TX_Get_Used_Space_Fast( state ) != 0U )
    {
        return false;
    }

    // DMA transfer-complete does not guarantee that the SPI peripheral has
    // physically shifted the final frame out. BSY must be clear before the
    // transfer can be considered electrically complete.
    if ( LL_SPI_IsActiveFlag_BSY( state->spi_peripheral ) != 0U )
    {
        return false;
    }

    // In master mode, the driver also owns software chip-select framing. Even
    // after the byte counts reach zero and BSY clears, the final-drain path may
    // still be completing the transaction and releasing CS.
    if ( state->is_master != false && state->tx_transaction_state != HW_SPI_TX_TRANSACTION_IDLE )
    {
        return false;
    }

    return true;
}
