/******************************************************************************
 *  File:       stub_semphr.c
 *  Author:     Angus Corr
 *  Created:    31-Jul-2026
 *
 *  Description:
 *      Host-build stubs for the FreeRTOS semaphore APIs used by modules.
 ******************************************************************************/
#ifdef TEST_BUILD

#include "rtos_config.h"
#include <stddef.h>

SemaphoreHandle_t xSemaphoreCreateMutexStatic( StaticSemaphore_t* mutex_buffer )
{
    return ( SemaphoreHandle_t )mutex_buffer;
}

BaseType_t xSemaphoreTake( SemaphoreHandle_t semaphore, TickType_t ticks_to_wait )
{
    ( void )ticks_to_wait;
    return ( semaphore != NULL ) ? pdTRUE : pdFALSE;
}

BaseType_t xSemaphoreGive( SemaphoreHandle_t semaphore )
{
    return ( semaphore != NULL ) ? pdTRUE : pdFALSE;
}

#endif
