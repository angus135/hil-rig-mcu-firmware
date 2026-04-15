/******************************************************************************
 *  File:       hw_uart_console.h
 *  Author:     Callum Rafferty
 *  Created:    16-Dec-2025
 *
 *  Description:
 *
 *  Notes:
 *
 *
 *  Typical usage:
 *
 ******************************************************************************/

#ifndef HW_UART_H
#define HW_UART_H

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
bool HW_UART_CONSOLE_Init( uint32_t baud_rate );
bool HW_UART_CONSOLE_Read( uint8_t* dest, uint32_t dest_size, uint32_t* bytes_read );
void HW_UART_CONSOLE_IRQHandler( void );
bool HW_UART_CONSOLE_Write( const uint8_t* data, uint32_t length );
bool HW_UART_CONSOLE_Is_Tx_Busy( void );

#ifdef __cplusplus
}
#endif

#endif /* HW_UART_CONSOLE_H */
