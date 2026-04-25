/******************************************************************************
 *  File:       hw_usb_mocks.h
 *  Author:     Callum Rafferty
 *  Created:    25-Mar-2026
 *
 *  Description:
 *      Mock definitions of HAL types and functions for unit testing hw_usb module.
 *
 *  Notes:
 *
 ******************************************************************************/

#ifndef HW_USB_MOCKS_H
#define HW_USB_MOCKS_H

#ifdef __cplusplus
extern "C"
{
#endif
// NOLINTBEGIN

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include <stdint.h>

/**-----------------------------------------------------------------------------
 *  Public Defines / Macros
 *------------------------------------------------------------------------------
 */
#define CDC_DATA_HS_MAX_PACKET_SIZE 512U /* Endpoint IN & OUT Packet size */

/**-----------------------------------------------------------------------------
 *  Public Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

typedef enum
{
    USBD_OK = 0U,
    USBD_BUSY,
    USBD_EMEM,
    USBD_FAIL,
} USBD_StatusTypeDef;

typedef struct
{
    uint32_t data[CDC_DATA_HS_MAX_PACKET_SIZE / 4U]; /* Force 32-bit alignment */
    uint8_t  CmdOpCode;
    uint8_t  CmdLength;
    uint8_t* RxBuffer;
    uint8_t* TxBuffer;
    uint32_t RxLength;
    uint32_t TxLength;

    uint32_t TxState;
    uint32_t RxState;
} USBD_CDC_HandleTypeDef;

typedef struct USBD_HandleTypeDef  // Note this definition is not complete, additional elements are
                                   // removed to reduce dependency
{
    uint8_t  id;
    uint32_t dev_config;
    uint32_t dev_default_config;
    uint32_t dev_config_status;
    uint32_t ep0_state;
    uint32_t ep0_data_len;
    uint8_t  dev_state;
    uint8_t  dev_old_state;
    uint8_t  dev_address;
    uint8_t  dev_connection_status;
    uint8_t  dev_test_mode;
    uint32_t dev_remote_wakeup;
    uint8_t  ConfIdx;
    void*    pClassData;
    void*    pData;
    void*    pBosDesc;
    void*    pConfDesc;
    uint32_t classId;
    uint32_t NumClasses;
} USBD_HandleTypeDef;

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

/**
 * @brief  CDC_Transmit_FS
 *         Data to send over USB IN endpoint are sent over CDC interface
 *         through this function.
 *         @note
 *
 *
 * @param  Buf: Buffer of data to be sent
 * @param  Len: Number of data to be sent (in bytes)
 * @retval USBD_OK if all operations are OK else USBD_FAIL or USBD_BUSY
 */
uint8_t CDC_Transmit_FS( uint8_t* Buf, uint16_t Len );

// NOLINTEND

#ifdef __cplusplus
}
#endif

#endif /* HW_QSPI_MOCKS_H */
