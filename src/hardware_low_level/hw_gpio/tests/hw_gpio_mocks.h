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
extern "C" {
#endif

#include <stdint.h>

/* ---- Fake GPIO port type ---- */
typedef struct
{
    uint32_t id; /* just for uniqueness / debugging */
} GPIO_PortMock;

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
#define GPIOA ((void*)&GPIOA_mock)
#define GPIOB ((void*)&GPIOB_mock)
#define GPIOC ((void*)&GPIOC_mock)
#define GPIOD ((void*)&GPIOD_mock)
#define GPIOE ((void*)&GPIOE_mock)
#define GPIOF ((void*)&GPIOF_mock)
#define GPIOG ((void*)&GPIOG_mock)
#define GPIOH ((void*)&GPIOH_mock)

/* ---- Pin bitmasks (match STM32 HAL style: GPIO_PIN_x == (1u<<x)) ---- */
#define Digital_Input_0_Pin (1u << 3)
#define Digital_Input_1_Pin (1u << 4)
#define Digital_Input_2_Pin (1u << 5)
#define Digital_Input_3_Pin (1u << 7)
#define Digital_Input_4_Pin (1u << 10)
#define Digital_Input_5_Pin (1u << 11)
#define Digital_Input_6_Pin (1u << 12)
#define Digital_Input_7_Pin (1u << 13)
#define Digital_Input_8_Pin (1u << 14)
#define Digital_Input_9_Pin (1u << 15)

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

#ifdef __cplusplus
}
#endif

#endif /* HW_GPIO_MOCKS_H */