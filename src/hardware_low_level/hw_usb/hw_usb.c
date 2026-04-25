/******************************************************************************
 *  File:       hw_usb.c
 *  Author:     Angus Corr
 *  Created:    25-Apr-2026
 *
 *  Description:
 *      Thin hardware abstraction wrapper around the STM32 USB CDC device
 *      interface. This module keeps the generated USB device code isolated from
 *      the application by providing buffered transmit and receive interfaces.
 *
 *      Transmit data is copied into a module-owned circular buffer before being
 *      passed to the CDC driver. This removes the requirement for application
 *      buffers to remain valid after HW_USB_Transmit() returns.
 *
 *      Receive data is copied from the CDC receive callback into a FreeRTOS
 *      stream buffer. Higher-level application or protocol code can then read
 *      received bytes outside of the USB interrupt context.
 *
 *  Notes:
 *      - USB CDC is treated as a byte transport layer only.
 *      - This module does not parse protocol frames or validate messages.
 *      - HW_USB_Receive_From_ISR() is intended to be called from the CDC receive
 *        callback in usbd_cdc_if.c.
 *      - HW_USB_Monitor_Process() must be called periodically by an application
 *        task or main loop to advance queued USB transmissions.
 *      - If HW_USB_Transmit() and HW_USB_Monitor_Process() are called from
 *        different tasks, access to usb_state should be protected by a mutex or
 *        critical section.
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#ifdef TEST_BUILD
#include "tests/hw_usb_mocks.h"
#else
#include "usbd_cdc_if.h"
#endif
#include "rtos_config.h"
#include "hw_usb.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */

// Maximum number of bytes that can be queued for USB transmission.
#define MAX_USB_TRANSMIT_BYTES 512U

// Maximum number of bytes that can be queued from USB receive callbacks.
#define MAX_USB_RECEIVE_STREAM_BYTES 1024U

// Unblock a waiting receive task as soon as at least one byte is available.
#define USB_RECEIVE_STREAM_TRIGGER_LEVEL_BYTES 1U

/**-----------------------------------------------------------------------------
 *  Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

/**
 * @brief Runtime state for the USB CDC wrapper.
 *
 * The transmit side uses a circular buffer. Bytes between transmit_live_start
 * and transmit_waiting_end are either currently being transmitted by the CDC
 * driver or waiting to be transmitted. transmit_num_buffered is used to
 * distinguish empty and full buffer states after index wraparound.
 *
 * The receive side uses a FreeRTOS stream buffer. The CDC receive callback
 * copies incoming data into this stream buffer so that higher-level code can
 * consume the data later outside of interrupt context.
 */
typedef struct HWUSBState_T
{
    uint8_t transmit_buffer[MAX_USB_TRANSMIT_BYTES];

    // Index of the oldest byte in the transmit ring buffer.
    // If CDC is currently transmitting, this is the first byte owned by CDC.
    uint32_t transmit_live_start;

    // Index one past the final byte in the active CDC transmission.
    // This is only meaningful when transmit_num_in_transmission is non-zero.
    uint32_t transmit_live_end;

    // Index where the next queued transmit byte will be written.
    uint32_t transmit_waiting_end;

    // Number of bytes currently passed to CDC_Transmit_FS() and not yet complete.
    uint32_t transmit_num_in_transmission;

    // Total number of bytes stored in transmit_buffer.
    // This includes the active CDC transmission and any queued waiting data.
    uint32_t transmit_num_buffered;

    // FreeRTOS stream buffer used to store bytes received from USB CDC.
    StreamBufferHandle_t receive_stream;

    // Number of received bytes that could not be copied into receive_stream.
    uint32_t receive_stream_bytes_dropped;

} HWUSBState_T;

/**-----------------------------------------------------------------------------
 *  Public (global) and Extern Variables
 *------------------------------------------------------------------------------
 */

extern USBD_HandleTypeDef hUsbDeviceFS;  // NOLINT

/**-----------------------------------------------------------------------------
 *  Private (static) Variables
 *------------------------------------------------------------------------------
 */

static HWUSBState_T usb_state = { 0 };

/**-----------------------------------------------------------------------------
 *  Private (static) Function Prototypes
 *------------------------------------------------------------------------------
 */

/**
 * @brief Check whether the current USB CDC transmit operation has completed.
 *
 * This function reads the ST CDC class state and checks whether TxState has
 * returned to zero. A non-zero TxState means the CDC class still owns the
 * transmit buffer previously passed to CDC_Transmit_FS().
 *
 * @return true if CDC is initialised and no transmit is currently active.
 * @return false if CDC is not initialised or a transmit is still active.
 */
static inline bool HW_USB_Transmit_Is_Complete( void );

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

static inline bool HW_USB_Transmit_Is_Complete( void )
{
    USBD_CDC_HandleTypeDef* hcdc = ( USBD_CDC_HandleTypeDef* )hUsbDeviceFS.pClassData;

    // pClassData is NULL before the USB CDC class has been initialised.
    if ( hcdc == NULL )
    {
        return false;
    }

    // ST's CDC driver sets TxState while an IN endpoint transfer is active.
    if ( hcdc->TxState != 0 )
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
 * @brief Initialise the USB wrapper module.
 *
 * Creates the FreeRTOS stream buffer used by the receive path and resets the
 * receive dropped-byte counter. This function must be called before USB receive
 * callbacks are allowed to write data into the module.
 *
 * @return true if the receive stream buffer was created successfully.
 * @return false if the receive stream buffer could not be created.
 */
bool HW_USB_Init( void )
{
    usb_state.receive_stream =
        xStreamBufferCreate( MAX_USB_RECEIVE_STREAM_BYTES, USB_RECEIVE_STREAM_TRIGGER_LEVEL_BYTES );

    if ( usb_state.receive_stream == NULL )
    {
        return false;
    }

    usb_state.receive_stream_bytes_dropped = 0U;

    return true;
}

/**
 * @brief Queue data for transmission over USB CDC.
 *
 * The supplied data is copied into the module-owned transmit ring buffer. This
 * means the caller may modify or release its original buffer after this function
 * returns. The actual CDC transmission may start immediately or later when
 * HW_USB_Monitor_Process() determines that the CDC driver is idle.
 *
 * @param data Pointer to the bytes to transmit.
 * @param size_bytes Number of bytes to queue for transmission.
 *
 * @return true if the data was successfully queued or size_bytes was zero.
 * @return false if data was NULL or there was not enough free transmit space.
 */
bool HW_USB_Transmit( const uint8_t* data, uint16_t size_bytes )
{
    uint32_t free_bytes       = 0;
    uint32_t first_copy_size  = 0;
    uint32_t second_copy_size = 0;

    if ( data == NULL )
    {
        return false;
    }

    // Treat a zero-length transmit as a successful no-op.
    if ( size_bytes == 0U )
    {
        return true;
    }

    // Only accept the transmit request if the complete message can be queued.
    free_bytes = MAX_USB_TRANSMIT_BYTES - usb_state.transmit_num_buffered;

    if ( size_bytes > free_bytes )
    {
        return false;
    }

    // Work out how many bytes can be copied before the physical buffer wraps.
    first_copy_size = MAX_USB_TRANSMIT_BYTES - usb_state.transmit_waiting_end;

    if ( first_copy_size > size_bytes )
    {
        first_copy_size = size_bytes;
    }

    // Copy the first part of the message into the current write position.
    memcpy( &usb_state.transmit_buffer[usb_state.transmit_waiting_end], data, first_copy_size );

    second_copy_size = ( uint32_t )size_bytes - first_copy_size;

    // If the message wrapped around the end of the ring buffer, copy the
    // remaining bytes to the beginning of the physical buffer.
    if ( second_copy_size > 0U )
    {
        memcpy( &usb_state.transmit_buffer[0], &data[first_copy_size], second_copy_size );
    }

    // Advance the ring write index and update the total number of queued bytes.
    usb_state.transmit_waiting_end =
        ( usb_state.transmit_waiting_end + size_bytes ) % MAX_USB_TRANSMIT_BYTES;

    usb_state.transmit_num_buffered += size_bytes;

    // Attempt to start transmission immediately. If CDC is busy, the queued
    // data will remain buffered and will be retried by HW_USB_Monitor_Process().
    HW_USB_Monitor_Process();

    return true;
}

/**
 * @brief Copy newly received USB CDC data into the receive stream buffer.
 *
 * This function is intended to be called from the CubeMX-generated CDC receive
 * callback. The CDC receive buffer is reused by the USB driver after the
 * callback returns, so this function must copy the received bytes before
 * returning.
 *
 * If the receive stream buffer does not have enough free space, only the bytes
 * that fit are copied. Dropped bytes are counted for diagnostics. Higher-level
 * protocol code is expected to detect and recover from missing data.
 *
 * @param data_received Pointer to the received USB CDC bytes.
 * @param size_bytes Pointer to the number of received bytes.
 */
void HW_USB_Receive_From_ISR( uint8_t* data_received, uint32_t* size_bytes )
{
    BaseType_t higher_priority_task_woken = pdFALSE;
    size_t     bytes_written              = 0;

    if ( data_received == NULL )
    {
        return;
    }

    if ( size_bytes == NULL )
    {
        return;
    }

    if ( *size_bytes == 0U )
    {
        return;
    }

    // If the stream has not been created, drop the whole receive packet.
    if ( usb_state.receive_stream == NULL )
    {
        usb_state.receive_stream_bytes_dropped += *size_bytes;
        return;
    }

    // Copy the received bytes into FreeRTOS-owned stream buffer storage.
    // This makes it safe for the CDC layer to reuse data_received later.
    bytes_written = xStreamBufferSendFromISR( usb_state.receive_stream, data_received, *size_bytes,
                                              &higher_priority_task_woken );

    // Count any bytes that did not fit in the stream buffer.
    if ( bytes_written != *size_bytes )
    {
        usb_state.receive_stream_bytes_dropped += ( uint32_t )( *size_bytes - bytes_written );
    }

    // If a higher-priority task was unblocked by the stream write, yield to it.
    portYIELD_FROM_ISR( higher_priority_task_woken );
}

/**
 * @brief Read received USB CDC bytes from the receive stream buffer.
 *
 * This function is intended to be called from normal task context, not from an
 * interrupt. It reads bytes previously copied into the stream buffer by
 * HW_USB_Receive_From_ISR().
 *
 * @param destination Buffer to copy received bytes into.
 * @param max_size_bytes Maximum number of bytes to read.
 * @param timeout_ticks FreeRTOS timeout, in ticks, to wait for data.
 *
 * @return Number of bytes copied into destination.
 */
uint32_t HW_USB_Receive( uint8_t* destination, uint32_t max_size_bytes, uint32_t timeout_ticks )
{
    size_t bytes_read = 0;

    if ( destination == NULL )
    {
        return 0U;
    }

    if ( max_size_bytes == 0U )
    {
        return 0U;
    }

    if ( usb_state.receive_stream == NULL )
    {
        return 0U;
    }

    bytes_read = xStreamBufferReceive( usb_state.receive_stream, destination, max_size_bytes,
                                       timeout_ticks );

    return ( uint32_t )bytes_read;
}

/**
 * @brief Get the number of bytes currently stored in the receive stream buffer.
 *
 * @return Number of bytes currently available to be read from the receive stream.
 */
uint32_t HW_USB_Get_Receive_Stream_Used_Bytes( void )
{
    size_t free_bytes = 0;

    if ( usb_state.receive_stream == NULL )
    {
        return 0U;
    }

    free_bytes = xStreamBufferSpacesAvailable( usb_state.receive_stream );

    return MAX_USB_RECEIVE_STREAM_BYTES - ( uint32_t )free_bytes;
}

/**
 * @brief Get the cumulative number of received bytes dropped by this module.
 *
 * Bytes are dropped when the receive stream buffer is full or has not been
 * created. This counter is diagnostic only and is not reset by this function.
 *
 * @return Number of received bytes that could not be stored.
 */
uint32_t HW_USB_Get_Receive_Stream_Dropped_Bytes( void )
{
    return usb_state.receive_stream_bytes_dropped;
}

/**
 * @brief Get the number of free bytes in the receive stream buffer.
 *
 * @return Number of bytes that can currently be written into the receive stream.
 */
uint32_t HW_USB_Get_Receive_Stream_Free_Bytes( void )
{
    if ( usb_state.receive_stream == NULL )
    {
        return 0U;
    }

    return ( uint32_t )xStreamBufferSpacesAvailable( usb_state.receive_stream );
}

/**
 * @brief Advance the USB CDC transmit state machine.
 *
 * This function should be called periodically from an application task or main
 * loop. It checks whether the current CDC transmission has completed. If it has,
 * the completed bytes are removed from the transmit ring buffer and the next
 * contiguous section of queued data is passed to CDC_Transmit_FS().
 *
 * The CDC driver requires a contiguous transmit buffer, so this function only
 * transmits up to the end of the physical ring buffer in a single call. If more
 * bytes are queued after wraparound, they will be transmitted by a later call.
 */
void HW_USB_Monitor_Process( void )
{
    uint32_t contiguous_bytes_available = 0;
    uint16_t bytes_to_transmit          = 0;
    uint8_t* transmit_data              = NULL;

    // If CDC currently owns part of the ring buffer, it must finish before the
    // live start index can be advanced or the next transfer can begin.
    if ( usb_state.transmit_num_in_transmission > 0U )
    {
        if ( HW_USB_Transmit_Is_Complete() == false )
        {
            return;
        }

        // CDC has completed the active transfer, so those bytes can now be
        // removed from the ring buffer.
        usb_state.transmit_live_start =
            ( usb_state.transmit_live_start + usb_state.transmit_num_in_transmission )
            % MAX_USB_TRANSMIT_BYTES;

        usb_state.transmit_num_buffered -= usb_state.transmit_num_in_transmission;

        // Mark the CDC transmit slot as idle.
        usb_state.transmit_num_in_transmission = 0U;
        usb_state.transmit_live_end            = usb_state.transmit_live_start;
    }

    // Nothing is queued, so there is no new CDC transfer to start.
    if ( usb_state.transmit_num_buffered == 0U )
    {
        return;
    }

    // Start a new CDC transmission.

    // CDC_Transmit_FS() needs a contiguous memory region. If the queued data
    // wraps around the end of transmit_buffer, only send the first contiguous
    // part now. The wrapped part will be sent by a later monitor call.
    contiguous_bytes_available = MAX_USB_TRANSMIT_BYTES - usb_state.transmit_live_start;

    if ( contiguous_bytes_available > usb_state.transmit_num_buffered )
    {
        contiguous_bytes_available = usb_state.transmit_num_buffered;
    }

    // The generated CDC transmit API takes a uint16_t length.
    if ( contiguous_bytes_available > UINT16_MAX )
    {
        contiguous_bytes_available = UINT16_MAX;
    }

    bytes_to_transmit = ( uint16_t )contiguous_bytes_available;
    transmit_data     = &usb_state.transmit_buffer[usb_state.transmit_live_start];

    // If CDC is still not ready for any reason, leave the data queued and retry
    // on the next call.
    if ( CDC_Transmit_FS( transmit_data, bytes_to_transmit ) != USBD_OK )
    {
        return;
    }

    // CDC now owns this contiguous section of transmit_buffer. These bytes must
    // not be overwritten or removed until HW_USB_Transmit_Is_Complete() is true.
    usb_state.transmit_num_in_transmission = bytes_to_transmit;

    usb_state.transmit_live_end =
        ( usb_state.transmit_live_start + bytes_to_transmit ) % MAX_USB_TRANSMIT_BYTES;
}
