/******************************************************************************
 *  File:       exec_uart.h
 *  Author:     Angus Corr
 *  Created:    25-Mar-2025
 *
 *  Description:
 *      <Short description of the module, what it exposes, and how it should be used>
 *
 *  Notes:
 *      <Public assumptions, required initialisation order, dependencies, etc.>
 ******************************************************************************/

#ifndef EXEC_UART_H
#define EXEC_UART_H

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

#include "hw_uart.h"
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

bool EXEC_UART_Apply_Configuration( HwUartChannel_T channel, const HwUartConfig_T* config );

bool EXEC_UART_Deconfigure( HwUartChannel_T channel );
bool EXEC_UART_Transmit( HwUartChannel_T channel, const uint8_t* data, uint32_t length_bytes );
bool EXEC_UART_Read( HwUartChannel_T channel, uint8_t* data, uint32_t buffer_size,
                     uint32_t* bytes_read );

#ifdef __cplusplus
}
#endif

#endif /* EXEC_UART_H */
