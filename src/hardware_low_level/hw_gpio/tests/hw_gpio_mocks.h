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

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */
#include "hw_gpio.h"
#include <stdint.h>

//NOLINTBEGIN

/* ---- Fake GPIO port type ---- */
typedef struct
{
    uint32_t id; /* just for uniqueness / debugging */
} GPIO_PortMock;

#define GPIOA ( ( GPIO_TypeDef* )( 1 ) )
#define GPIOB ( ( GPIO_TypeDef* )( 2 ) )
#define GPIOC ( ( GPIO_TypeDef* )( 3 ) )
#define GPIOD ( ( GPIO_TypeDef* )( 4 ) )
#define GPIOE ( ( GPIO_TypeDef* )( 5 ) )
#define GPIOF ( ( GPIO_TypeDef* )( 6 ) )
#define GPIOG ( ( GPIO_TypeDef* )( 7 ) )
#define GPIOH ( ( GPIO_TypeDef* )( 8 ) )

#define LD1_GPIO_Port GPIOA
#define LD1_Pin 1
#define LD2_GPIO_Port GPIOA
#define LD2_Pin 2
#define LD3_GPIO_Port GPIOA
#define LD3_Pin 3

/**-----------------------------------------------------------------------------
 *  Public Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

typedef struct
{
    uint32_t MODER;   /*!< GPIO port mode register,               Address offset: 0x00      */
    uint32_t OTYPER;  /*!< GPIO port output type register,        Address offset: 0x04      */
    uint32_t OSPEEDR; /*!< GPIO port output speed register,       Address offset: 0x08      */
    uint32_t PUPDR;   /*!< GPIO port pull-up/pull-down register,  Address offset: 0x0C      */
    uint32_t IDR;     /*!< GPIO port input data register,         Address offset: 0x10      */
    uint32_t ODR;     /*!< GPIO port output data register,        Address offset: 0x14      */
    uint32_t BSRR;    /*!< GPIO port bit set/reset register,      Address offset: 0x18      */
    uint32_t LCKR;    /*!< GPIO port configuration lock register, Address offset: 0x1C      */
    uint32_t AFR[2];  /*!< GPIO alternate function registers,     Address offset: 0x20-0x24 */
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
/* ---- Fake port instances (one per STM32 port you reference) ---- */
static GPIO_PortMock GPIOA_mock = { .id = 0xA };
static GPIO_PortMock GPIOB_mock = { .id = 0xB };
static GPIO_PortMock GPIOC_mock = { .id = 0xC };
static GPIO_PortMock GPIOD_mock = { .id = 0xD };
static GPIO_PortMock GPIOE_mock = { .id = 0xE };
static GPIO_PortMock GPIOF_mock = { .id = 0xF };
static GPIO_PortMock GPIOG_mock = { .id = 0x10 };
static GPIO_PortMock GPIOH_mock = { .id = 0x11 };

/* ---- Match STM32-style port symbols used by main.h (GPIOF, GPIOC, etc.) ---- */
#define GPIOA ( ( void* )&GPIOA_mock )
#define GPIOB ( ( void* )&GPIOB_mock )
#define GPIOC ( ( void* )&GPIOC_mock )
#define GPIOD ( ( void* )&GPIOD_mock )
#define GPIOE ( ( void* )&GPIOE_mock )
#define GPIOF ( ( void* )&GPIOF_mock )
#define GPIOG ( ( void* )&GPIOG_mock )
#define GPIOH ( ( void* )&GPIOH_mock )

/* ---- Pin bitmasks (match STM32 HAL style: GPIO_PIN_x == (1u<<x)) ---- */
#define Digital_Input_0_Pin ( 1u << 3 )
#define Digital_Input_1_Pin ( 1u << 4 )
#define Digital_Input_2_Pin ( 1u << 5 )
#define Digital_Input_3_Pin ( 1u << 7 )
#define Digital_Input_4_Pin ( 1u << 10 )
#define Digital_Input_5_Pin ( 1u << 11 )
#define Digital_Input_6_Pin ( 1u << 12 )
#define Digital_Input_7_Pin ( 1u << 13 )
#define Digital_Input_8_Pin ( 1u << 14 )
#define Digital_Input_9_Pin ( 1u << 15 )

/* ---- Port assignments (all inputs on GPIOF like the real board) ---- */
#define Digital_Input_0_GPIO_Port GPIOF
#define Digital_Input_1_GPIO_Port GPIOF
#define Digital_Input_2_GPIO_Port GPIOF
#define Digital_Input_3_GPIO_Port GPIOF
#define Digital_Input_4_GPIO_Port GPIOF
#define Digital_Input_5_GPIO_Port GPIOF
#define Digital_Input_6_GPIO_Port GPIOF
#define Digital_Input_7_GPIO_Port GPIOF
#define Digital_Input_8_GPIO_Port GPIOF
#define Digital_Input_9_GPIO_Port GPIOF

//NOLINTEND

#ifdef __cplusplus
}
#endif

#endif /* HW_GPIO_MOCKS_H */
