/******************************************************************************
 *  File:       hw_usb.h
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

#ifndef HW_USB_H
#define HW_USB_H

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
// Add any needed standard or project-specific includes here

/**-----------------------------------------------------------------------------
 *  Public Defines / Macros
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Public Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
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
bool HW_USB_Init( void );

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
bool HW_USB_Transmit( const uint8_t* data, uint16_t size_bytes );

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
void HW_USB_Receive_From_ISR( uint8_t* data_received, uint32_t* size_bytes );

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
uint32_t HW_USB_Receive( uint8_t* destination, uint32_t max_size_bytes, uint32_t timeout_ticks );

/**
 * @brief Get the number of bytes currently stored in the receive stream buffer.
 *
 * @return Number of bytes currently available to be read from the receive stream.
 */
uint32_t HW_USB_Get_Receive_Stream_Used_Bytes( void );

/**
 * @brief Get the cumulative number of received bytes dropped by this module.
 *
 * Bytes are dropped when the receive stream buffer is full or has not been
 * created. This counter is diagnostic only and is not reset by this function.
 *
 * @return Number of received bytes that could not be stored.
 */
uint32_t HW_USB_Get_Receive_Stream_Dropped_Bytes( void );

/**
 * @brief Get the number of free bytes in the receive stream buffer.
 *
 * @return Number of bytes that can currently be written into the receive stream.
 */
uint32_t HW_USB_Get_Receive_Stream_Free_Bytes( void );

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
void HW_USB_Monitor_Process( void );

#ifdef __cplusplus
}
#endif

#endif /* <FILENAME>_H */
