/******************************************************************************
 *  File:       console.c
 *  Author:     Angus Corr
 *  Created:    6-Dec-2025
 *
 *  Description:
 *      Console module implementation.
 *
 *      This module provides:
 *        - A simple command line console which can be accessed to interface with the MCU
 *
 *  Notes:
 *     None
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */
#include "rtos_config.h"
#include "console.h"
#include "hw_uart.h"
#include <stdint.h>
#include <stdbool.h>

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */
#define CONSOLE_TASK_PERIOD 5 // 200Hz
#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0])) // TODO: move this to a common helper file

/**-----------------------------------------------------------------------------
 *  Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Public (global) and Extern Variables
 *------------------------------------------------------------------------------
 */

TaskHandle_t* ConsoleTaskHandle = NULL; // NOLINT(readability-identifier-naming)

/**-----------------------------------------------------------------------------
 *  Private (static) Variables
 *------------------------------------------------------------------------------
 */
static const char welcome_message[] = "Welcome to HIL-RIG MCU!\r\n";

/**-----------------------------------------------------------------------------
 *  Private (static) Function Prototypes
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

static void CONSOLE_Init(void)
{
    for (size_t index = 0; index < ARRAY_LEN(welcome_message); index++)
    {
        HW_UART_Write_Byte(UART_CONSOLE, welcome_message[index]);
    }
}

static void CONSOLE_Process(void)
{
    uint8_t      byte   = 0;
    UARTStatus_T status = HW_UART_Read_Byte(UART_CONSOLE, &byte);
    if (status == UART_SUCCESS)
    {
        if (byte == '\r')
        {
            HW_UART_Write_Byte(UART_CONSOLE, '\r');
            HW_UART_Write_Byte(UART_CONSOLE, '\n');
        }
        else
        {
            HW_UART_Write_Byte(UART_CONSOLE, byte);
        }
    }
}

/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */

/**
 * @brief Console Task
 *
 * The FreeRTOS task that runs all the console related logic
 */
void CONSOLE_Task(void* task_parameters)
{
    (void)task_parameters;

    CONSOLE_Init();

    TickType_t initial_ticks = xTaskGetTickCount();
    while (true)
    {
        CONSOLE_Process();
        vTaskDelayUntil(&initial_ticks, pdMS_TO_TICKS(CONSOLE_TASK_PERIOD));
    }
}
