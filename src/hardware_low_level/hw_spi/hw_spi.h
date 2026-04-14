/******************************************************************************
 *  File:       hw_spi.h
 *  Author:     Angus Corr
 *  Created:    10-Apr-2026
 *
 *  Description:
 *      <Short description of the module, what it exposes, and how it should be used>
 *
 *  Notes:
 *      <Public assumptions, required initialisation order, dependencies, etc.>
 ******************************************************************************/

#ifndef HW_SPI_H
#define HW_SPI_H

#ifdef __cplusplus
extern "C"
{
#endif

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
// Add any needed standard or project-specific includes here

/**-----------------------------------------------------------------------------
 *  Public Defines / Macros
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Public Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */
typedef enum SPIBaudRate_T
{
    SPI_BAUD_45MBIT,
    SPI_BAUD_22M5BIT,
    SPI_BAUD_11M25BIT,
    SPI_BAUD_5M625BIT,
    SPI_BAUD_2M813BIT,
    SPI_BAUD_1M406BIT,
    SPI_BAUD_703KBIT,
    SPI_BAUD_352KBIT,
} SPIBaudRate_T;

typedef enum SPICPOL_T
{
    SPI_CPOL_LOW,
    SPI_CPOL_HIGH,
} SPICPOL_T;

typedef enum SPICPHA_T
{
    SPI_CPHA_1_EDGE,
    SPI_CPHA_2_EDGE,
} SPICPHA_T;

typedef enum SPIDataSize_T
{
    SPI_SIZE_8_BIT,
    SPI_SIZE_16_BIT,
} SPIDataSize_T;

typedef enum SPIFirstBit_T
{
    SPI_FIRST_MSB,
    SPI_FIRST_LSB,
} SPIFirstBit_T;

typedef enum SPIMode_T
{
    SPI_MASTER_MODE,
    SPI_SLAVE_MODE,
} SPIMode_T;

typedef enum SPIPeripheral_T
{
    SPI_CHANNEL_0,
    SPI_CHANNEL_1,
    SPI_DAC,
} SPIPeripheral_T;

typedef struct HWSPIConfig_T
{
    SPIMode_T     spi_mode;
    SPIDataSize_T data_size;
    SPIFirstBit_T first_bit;
    SPIBaudRate_T baud_rate;
    SPICPOL_T     cpol;
    SPICPHA_T     cpha;
} HWSPIConfig_T;

typedef struct
{
    const uint8_t* data;          // Pointer to the start of the unread data span
    uint32_t       length_bytes;  // Length of the unread data span in bytes
} HWSPIRxSpan_T;

typedef struct
{
    HWSPIRxSpan_T first_span;   // First contiguous unread span
    HWSPIRxSpan_T second_span;  // Second contiguous unread span, non-zero only if wrapping occurs
    uint32_t      total_length_bytes;  // Total unread bytes across both spans
} HWSPIRxSpans_T;

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

/**
 * @brief Configure a hardware SPI channel.
 *
 * Applies the provided configuration to the selected SPI peripheral and prepares
 * any associated low-level resources required by the driver, such as peripheral
 * registers, internal software state, and internal RX/TX buffers.
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
bool HW_SPI_Configure_Channel( SPIPeripheral_T peripheral, HWSPIConfig_T configuration );

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
void HW_SPI_Start_Channel( SPIPeripheral_T peripheral );

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
void HW_SPI_Stop_Channel( SPIPeripheral_T peripheral );

/**
 * @brief  Return the unread slave RX data as one or two spans into the internal DMA buffer.
 *
 * Provides a read-only view of the unread portion of the selected SPI slave RX
 * DMA buffer without consuming any data.
 *
 * Because the RX DMA buffer is circular, unread data may either be contiguous or
 * split across the end and beginning of the buffer. This function returns up to
 * two spans describing that unread data.
 *
 * The returned pointers refer to memory owned by the low-level driver. The
 * caller must not modify this memory and must copy it if persistence is
 * required.
 *
 * This function does not advance the RX consume position. The mid-level driver
 * must call HW_SPI_Slave_Rx_Consume() after it has processed the required
 * number of bytes.
 *
 * @param peripheral
 *     The SPI peripheral/channel to inspect. This channel must be configured in
 *     slave mode.
 *
 * @return
 *     A structure describing the unread slave RX data as one or two spans.
 */
HWSPIRxSpans_T HW_SPI_Slave_Rx_Peek( SPIPeripheral_T peripheral );

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
void HW_SPI_Slave_Rx_Consume( SPIPeripheral_T peripheral, uint32_t bytes_to_consume );

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
bool HW_SPI_Slave_Load_Tx_Buffer( SPIPeripheral_T peripheral, const uint8_t* data, uint32_t size );

/**
 * @brief Trigger transmission of queued slave TX data for a channel.
 *
 * Starts the slave transmit DMA for the selected SPI channel if queued transmit
 * data is available and no transmit DMA transfer is currently in progress.
 *
 * This function provides the "trigger" stage of the slave TX queue model. It is
 * intended to be called by the mid-level driver after one or more messages have
 * been loaded into the internal slave TX buffer using the corresponding load
 * function.
 *
 * If a transmit DMA transfer is already active, this function does not restart,
 * interrupt, or modify the current transfer. In this case, the function simply
 * leaves the existing transmission in progress.
 *
 * If no queued transmit data is available, or the selected channel is not valid
 * for slave TX operation, the function shall do nothing.
 *
 * This function only starts transmission of data already stored in the
 * low-level driver's internal slave TX buffer. It does not copy any new data
 * into the transmit queue and does not define higher-level frame semantics. The
 * mid-level driver is responsible for determining what data should be queued and
 * when transmission should be triggered.
 *
 * This function is intended for use only on channels configured in slave mode.
 * Calling it on a master channel is invalid.
 *
 * @param peripheral
 *     The SPI peripheral/channel whose queued slave TX data should be
 *     transmitted.
 */
void HW_SPI_Slave_Tx_Trigger( SPIPeripheral_T peripheral );

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
                               uint8_t* read_data, uint32_t size );

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
bool HW_SPI_Master_Is_Busy( SPIPeripheral_T peripheral );

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
bool HW_SPI_Master_Transfer_Complete( SPIPeripheral_T peripheral );

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
uint32_t HW_SPI_Master_Get_Last_Transfer_Size( SPIPeripheral_T peripheral );

#ifdef __cplusplus
}
#endif

#endif /* HW_SPI_H */
