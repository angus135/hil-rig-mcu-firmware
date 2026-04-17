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

 typedef struct
{
  uint32_t MODER;    /*!< GPIO port mode register,               Address offset: 0x00      */
  uint32_t OTYPER;   /*!< GPIO port output type register,        Address offset: 0x04      */
  uint32_t OSPEEDR;  /*!< GPIO port output speed register,       Address offset: 0x08      */
  uint32_t PUPDR;    /*!< GPIO port pull-up/pull-down register,  Address offset: 0x0C      */
  uint32_t IDR;      /*!< GPIO port input data register,         Address offset: 0x10      */
  uint32_t ODR;      /*!< GPIO port output data register,        Address offset: 0x14      */
  uint32_t BSRR;     /*!< GPIO port bit set/reset register,      Address offset: 0x18      */
  uint32_t LCKR;     /*!< GPIO port configuration lock register, Address offset: 0x1C      */
  uint32_t AFR[2];   /*!< GPIO alternate function registers,     Address offset: 0x20-0x24 */
} GPIO_TypeDef;

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Public varibles
 *------------------------------------------------------------------------------
 */

// NOLINTEND

#ifdef __cplusplus
}
#endif

#endif /* HW_UART_MOCKS_H */
