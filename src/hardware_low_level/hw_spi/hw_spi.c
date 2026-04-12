/******************************************************************************
 *  File:       hw_spi.c
 *  Author:     Angus Corr
 *  Created:    10-Apr-2026
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
#include "tests/hw_spi_mocks.h"
#else
#include "spi.h"
#include "stm32f446xx.h"
#endif
#include "hw_spi.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */
// TODO: Change this based on true hardware
#define SPI_CHANNEL_0_HANDLE hspi1
#define SPI_CHANNEL_1_HANDLE hspi4
#define SPI_DAC_HANDLE hspi4

#define SPI_CHANNEL_0_INSTANCE SPI1
#define SPI_CHANNEL_1_INSTANCE SPI4
#define SPI_DAC_INSTANCE SPI4

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
HWSPIConfig_T spi_channel_0_config = { 0 };
HWSPIConfig_T spi_channel_1_config = { 0 };
HWSPIConfig_T spi_dac_config = { 0 };

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

/**
 * @brief Configure a hardware SPI channel.
 *
 * Applies the provided configuration to the selected SPI peripheral and prepares
 * any associated low-level resources required by the driver, such as peripheral
 * registers, DMA streams, internal software state, and internal RX/TX buffers.
 *
 * This function configures the channel role and operating parameters, including
 * settings such as master/slave mode, SPI mode, bitrate, data width, and any
 * other hardware-level options contained within @p configuration.
 *
 * This function does not start runtime operation of the channel. After a channel
 * has been successfully configured, HW_SPI_Start_Channel() must be called before
 * the peripheral is used.
 *
 * If the channel is already running, behaviour is implementation-defined. The
 * intended usage is that configuration occurs while the channel is stopped.
 *
 * @param peripheral
 *     The SPI peripheral/channel to configure.
 *
 * @param configuration
 *     The hardware SPI configuration to apply to the selected channel.
 *
 * @return
 *     true if the channel was configured successfully.
 *     false if the configuration was invalid, unsupported, or the hardware
 *     resources required for the channel could not be configured.
 */
bool HW_SPI_Configure_Channel( SPIPeripheral_T peripheral, HWSPIConfig_T configuration )
{
    SPI_HandleTypeDef* hspi = NULL;
    // SPI Channel
    switch ( peripheral )
    {
        case SPI_CHANNEL_0:
            hspi           = &SPI_CHANNEL_0_HANDLE;
            hspi->Instance = SPI_CHANNEL_0_INSTANCE;
            memcpy(&spi_channel_0_config, &configuration, sizeof(HWSPIConfig_T));
            break;
        case SPI_CHANNEL_1:
            hspi           = &SPI_CHANNEL_1_HANDLE;
            hspi->Instance = SPI_CHANNEL_1_INSTANCE;
            memcpy(&spi_channel_1_config, &configuration, sizeof(HWSPIConfig_T));
            break;
        case SPI_DAC:
            hspi           = &SPI_DAC_HANDLE;
            hspi->Instance = SPI_DAC_INSTANCE;
            memcpy(&spi_dac_config, &configuration, sizeof(HWSPIConfig_T));
            break;
        default:
            return false;
    }

    // Mode + chip select logic
    switch ( configuration.spi_mode )
    {
        case SPI_MASTER_MODE:
            hspi->Init.Mode = SPI_MODE_MASTER;
            hspi->Init.NSS  = SPI_NSS_HARD_OUTPUT;
            break;
        case SPI_SLAVE_MODE:
            hspi->Init.Mode = SPI_MODE_SLAVE;
            hspi->Init.NSS  = SPI_NSS_HARD_INPUT;
            break;
        default:
            return false;
    }

    // Direction (always 2 lines (MISO and MOSI))
    hspi->Init.Direction = SPI_DIRECTION_2LINES;
    // Datasize (8 bit or 16 bit)
    switch ( configuration.data_size )
    {
        case SPI_SIZE_8_BIT:
            hspi->Init.DataSize = SPI_DATASIZE_8BIT;
            break;
        case SPI_SIZE_16_BIT:
            hspi->Init.DataSize = SPI_DATASIZE_16BIT;
            break;
        default:
            return false;
    }

    // Clock Polarity (CPOL)
    switch ( configuration.cpol )
    {
        case SPI_CPOL_HIGH:
            hspi->Init.CLKPolarity = SPI_POLARITY_HIGH;
            break;
        case SPI_CPOL_LOW:
            hspi->Init.CLKPolarity = SPI_POLARITY_LOW;
            break;
        default:
            return false;
    }

    // Clock Phase (CPHA)
    switch ( configuration.cpha )
    {
        case SPI_CPHA_1_EDGE:
            hspi->Init.CLKPhase = SPI_PHASE_1EDGE;
            break;
        case SPI_CPHA_2_EDGE:
            hspi->Init.CLKPhase = SPI_PHASE_2EDGE;
            break;
        default:
            return false;
    }

    // Baud rate
    switch ( configuration.baud_rate )
    {
        case SPI_BAUD_45MBIT:
            hspi->Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_2;
            break;
        case SPI_BAUD_22M5BIT:
            hspi->Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_4;
            break;
        case SPI_BAUD_11M25BIT:
            hspi->Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_8;
            break;
        case SPI_BAUD_5M625BIT:
            hspi->Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_16;
            break;
        case SPI_BAUD_2M813BIT:
            hspi->Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_32;
            break;
        case SPI_BAUD_1M406BIT:
            hspi->Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_64;
            break;
        case SPI_BAUD_703KBIT:
            hspi->Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_128;
            break;
        case SPI_BAUD_352KBIT:
            hspi->Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_256;
            break;
        default:
            return false;
    }

    // MSB vs LSB
    switch ( configuration.first_bit )
    {
        case SPI_FIRST_LSB:
            hspi->Init.FirstBit = SPI_FIRSTBIT_LSB;
            break;
        case SPI_FIRST_MSB:
            hspi->Init.FirstBit = SPI_FIRSTBIT_MSB;
            break;
        default:
            return false;
    }

    // Default values sourced from HAL
    hspi->Init.TIMode         = SPI_TIMODE_DISABLE;
    hspi->Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    hspi->Init.CRCPolynomial  = 10;
    if ( HAL_SPI_Init( &hspi1 ) != HAL_OK )
    {
        return false;
    }

    return true;
}

/**
 * @brief Start operation of a configured SPI channel.
 *
 * Enables runtime operation of the selected SPI channel using the configuration
 * previously applied by HW_SPI_Configure_Channel().
 *
 * For a slave channel, this function shall start the background hardware state
 * required for slave operation, including any continuous DMA-based reception
 * mechanism and any associated internal state used by the driver.
 *
 * For a master channel, this function shall place the channel into its ready
 * state such that master write/read transactions may later be initiated using
 * HW_SPI_Master_Write_Read(). Master transfers are not started automatically by
 * this function.
 *
 * This function does not perform any higher-level framing, protocol parsing, or
 * timestamp handling. Those responsibilities belong to higher software layers.
 *
 * The channel should only be started after successful configuration.
 *
 * @param peripheral
 *     The SPI peripheral/channel to start.
 */
void HW_SPI_Start_Channel( SPIPeripheral_T peripheral )
{

}

/**
 * @brief Stop operation of a configured SPI channel.
 *
 * Stops runtime operation of the selected SPI channel and disables any active
 * hardware mechanisms used by the low-level driver for that channel.
 *
 * For a slave channel, this shall stop any background RX/TX activity managed by
 * the driver, including any DMA streams or internal tracking state associated
 * with continuous reception.
 *
 * For a master channel, this shall stop the channel and prevent further
 * transactions from being initiated until the channel is started again. If a
 * transfer is active when this function is called, behaviour is
 * implementation-defined unless otherwise specified by the driver design.
 *
 * This function does not clear the higher-level protocol state owned by the
 * mid-level driver.
 *
 * @param peripheral
 *     The SPI peripheral/channel to stop.
 */
void HW_SPI_Stop_Channel( SPIPeripheral_T peripheral )
{
    
}

/**
 * @brief Copy currently available unread slave RX data without consuming it.
 *
 * Copies unread bytes currently held in the selected slave channel's internal RX
 * buffer into the caller-provided destination buffer, without advancing the
 * driver's RX consume position.
 *
 * This function provides the "peek" stage of the slave RX peek/consume model.
 * It allows the mid-level driver to inspect received data and determine message
 * boundaries or parsing decisions before later acknowledging consumption with
 * HW_SPI_Slave_Rx_Consume().
 *
 * The function shall copy up to the number of bytes requested by the caller via
 * @p size. On return, @p size shall be updated to the number of bytes actually
 * copied.
 *
 * The copied data represents the unread portion of the driver's internal slave
 * RX buffer at the time the function executes. Because this is a low-level
 * driver over a live hardware receiver, the unread data may continue to grow in
 * the background after this function returns.
 *
 * This function is intended for use only on channels configured in slave mode.
 * Calling it on a master channel is invalid.
 *
 * Buffer ownership remains with the low-level driver. This function copies data
 * out to the caller and does not expose direct access to the internal DMA/ring
 * buffer.
 *
 * @param peripheral
 *     The SPI peripheral/channel to inspect. This channel must be configured in
 *     slave mode.
 *
 * @param data
 *     Destination buffer into which unread RX bytes will be copied.
 *
 * @param size
 *     On entry, the maximum number of bytes to copy into @p data.
 *     On return, the number of bytes actually copied.
 *
 * @note
 *     This function does not mark any bytes as consumed. Repeated calls without
 *     a matching call to HW_SPI_Slave_Rx_Consume() may return the same data.
 */
void HW_SPI_Slave_Rx_Peek( SPIPeripheral_T peripheral, uint8_t* data, size_t* size )
{

}

/**
 * @brief Mark previously received slave RX bytes as consumed.
 *
 * Advances the internal unread-data consume position for the selected slave
 * channel by the specified number of bytes.
 *
 * This function provides the "consume" stage of the slave RX peek/consume model.
 * It is intended to be called by the mid-level driver after it has inspected or
 * parsed data returned by HW_SPI_Slave_Rx_Peek() and determined that a number of
 * bytes can be discarded from the unread portion of the internal RX buffer.
 *
 * The driver shall only consume bytes that are currently unread. If
 * @p bytes_to_consume exceeds the number of unread bytes currently tracked by
 * the driver, behaviour is implementation-defined unless explicitly handled by
 * the implementation.
 *
 * This function is intended for use only on channels configured in slave mode.
 * Calling it on a master channel is invalid.
 *
 * @param peripheral
 *     The SPI peripheral/channel whose unread slave RX data should be advanced.
 *
 * @param bytes_to_consume
 *     The number of unread RX bytes to mark as consumed.
 *
 * @note
 *     This function does not copy any data. It only updates the internal consume
 *     position maintained by the low-level driver.
 */
void HW_SPI_Slave_Rx_Consume( SPIPeripheral_T peripheral, size_t bytes_to_consume )
{

}

/**
 * @brief Load data into the slave transmit buffer for a channel.
 *
 * Copies the supplied data into the selected slave channel's internal transmit
 * buffer so that the channel can present this data on future slave-side SPI
 * transmissions when clocked by an external master.
 *
 * The provided data is copied into internal driver-owned storage. The caller
 * therefore retains ownership of @p data and may modify or discard the source
 * buffer after this function returns.
 *
 * This function does not define higher-level frame semantics. It only loads the
 * low-level bytes to be shifted out by the slave peripheral. The mid-level
 * driver is responsible for determining what data should be made available to
 * the external master and when it should be updated.
 *
 * If the transmit buffer cannot accept the requested data, or the request is not
 * valid for the current state of the channel, the function shall return false.
 *
 * This function is intended for use only on channels configured in slave mode.
 * Calling it on a master channel is invalid.
 *
 * @param peripheral
 *     The SPI peripheral/channel whose slave TX buffer is to be updated.
 *
 * @param data
 *     Pointer to the source data to copy into the internal slave TX buffer.
 *
 * @param size
 *     Number of bytes to copy from @p data into the internal slave TX buffer.
 *
 * @return
 *     true if the data was accepted and loaded successfully.
 *     false if the buffer could not be loaded, the size was invalid, or the
 *     operation was not valid for the current channel state.
 */
bool HW_SPI_Slave_Load_Tx_Buffer( SPIPeripheral_T peripheral, const uint8_t* data, size_t size )
{

}

/**
 * @brief Start a master-mode SPI write/read transaction.
 *
 * Initiates a master-mode SPI transfer on the selected channel using the
 * provided write buffer and read buffer.
 *
 * At the hardware level, SPI master transfers are full duplex. This function
 * therefore represents the fundamental low-level master transaction primitive:
 * bytes are shifted out from @p write_data while bytes are simultaneously
 * captured into @p read_data.
 *
 * Higher software layers may use this primitive to implement:
 * - write-only operations, where received data is ignored,
 * - read operations, where @p write_data contains dummy bytes,
 * - write-then-read style transactions, where @p write_data contains command
 *   bytes followed by any required dummy bytes.
 *
 * This function is intended to be non-blocking. A successful call indicates that
 * the transfer has been accepted and started by the hardware driver. Completion
 * shall later be observed using HW_SPI_Master_Is_Busy(),
 * HW_SPI_Master_Transfer_Complete(), and
 * HW_SPI_Master_Get_Last_Transfer_Size().
 *
 * The caller must ensure that the memory referenced by @p write_data and
 * @p read_data remains valid and unmodified as required until the transfer has
 * completed, unless the implementation explicitly documents different ownership
 * semantics.
 *
 * If the channel is busy or the transfer cannot be started, the function shall
 * return false and no new transfer shall be initiated.
 *
 * This function is intended for use only on channels configured in master mode.
 * Calling it on a slave channel is invalid.
 *
 * @param peripheral
 *     The SPI peripheral/channel on which to start the master transaction.
 *
 * @param write_data
 *     Pointer to the data to be transmitted by the master.
 *
 * @param read_data
 *     Pointer to the destination buffer into which received bytes will be
 *     written by the transfer.
 *
 * @param size
 *     Number of bytes to transmit and receive as part of the transaction.
 *
 * @return
 *     true if the transaction was successfully started.
 *     false if the channel was busy, the arguments were invalid, or the
 *     transaction could not be started.
 */
bool HW_SPI_Master_Write_Read( SPIPeripheral_T peripheral, const uint8_t* write_data,
                               uint8_t* read_data, size_t size )
{

}

/**
 * @brief Determine whether a master transfer is currently active.
 *
 * Returns whether the selected SPI channel is currently busy performing a
 * master-mode transaction previously started by HW_SPI_Master_Write_Read().
 *
 * This function is intended for use by the mid-level driver or execution logic
 * to determine whether a new master transaction may be started.
 *
 * This function is intended for use only on channels configured in master mode.
 *
 * @param peripheral
 *     The SPI peripheral/channel to query.
 *
 * @return
 *     true if a master transfer is currently in progress on the channel.
 *     false if no master transfer is currently active.
 */
bool HW_SPI_Master_Is_Busy( SPIPeripheral_T peripheral )
{

}

/**
 * @brief Determine whether the most recent master transfer has completed.
 *
 * Returns whether the currently active or most recently started master transfer
 * on the selected channel has completed.
 *
 * The exact persistence semantics of the completion state are
 * implementation-defined unless explicitly documented by the driver. A common
 * implementation is that this function returns true after transfer completion
 * until a new transfer is started.
 *
 * This function is intended for use by higher software layers to determine when
 * a previously started master transaction has finished and its RX data may be
 * safely interpreted.
 *
 * This function is intended for use only on channels configured in master mode.
 *
 * @param peripheral
 *     The SPI peripheral/channel to query.
 *
 * @return
 *     true if the most recent master transfer has completed.
 *     false if the transfer is still in progress or no completed transfer is
 *     available according to the implementation's completion-state rules.
 */
bool HW_SPI_Master_Transfer_Complete( SPIPeripheral_T peripheral )
{
    
}

/**
 * @brief Get the size of the most recently completed master transfer.
 *
 * Returns the number of bytes associated with the most recently completed
 * master-mode write/read transaction on the selected channel.
 *
 * This function is intended to allow higher software layers to confirm how many
 * bytes were transferred in the last completed master transaction.
 *
 * The returned value corresponds to the most recent completed transfer tracked
 * by the low-level driver. If no completed transfer is available, the returned
 * value is implementation-defined unless explicitly documented otherwise.
 *
 * This function is intended for use only on channels configured in master mode.
 *
 * @param peripheral
 *     The SPI peripheral/channel to query.
 *
 * @return
 *     The size, in bytes, of the most recently completed master transfer.
 */
size_t HW_SPI_Master_Get_Last_Transfer_Size( SPIPeripheral_T peripheral )
{

}
