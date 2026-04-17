/******************************************************************************
 *  File:       hw_gpio_mocks.h
 *  Author:     Callum Rafferty
 *  Created:    25-Mar-2026
 *
 *  Description:
 *      Mock definitions of HAL types and functions for unit testing hw_gpio module.
 *
 *  Notes:
 *
 ******************************************************************************/

#ifndef HW_GPIO_MOCKS_H
#define HW_GPIO_MOCKS_H

#ifdef __cplusplus
extern "C"
{
#endif
// NOLINTBEGIN

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */
#include "hw_gpio.h"

/**-----------------------------------------------------------------------------
 *  Public Defines / Macros
 *------------------------------------------------------------------------------
 */

#define LD1_GPIO_Port GPIOA
#define LD1_Pin 1

/**-----------------------------------------------------------------------------
 *  Public Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Public varibles
 *------------------------------------------------------------------------------
 */

GPIO_PORT_PACKET HW_GPIO_port_pin_association_to_return =
    ( struct GPIO_PORT_PACKET ){ LD1_GPIO_Port, LD1_Pin };

// NOLINTEND

#ifdef __cplusplus
}
#endif

#endif /* HW_UART_MOCKS_H */
