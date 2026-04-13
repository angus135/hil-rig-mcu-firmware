/******************************************************************************
 *  File:       stub_queue.c
 *  Author:     Angus Corr
 *  Created:    6-Dec-2025
 *
 *  Description:
 *      Stubs that correspond to queue.c in FreeRTOS
 *
 *  Notes:
 *     None
 ******************************************************************************/
#ifdef TEST_BUILD
/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */
#include "rtos_config.h"
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

// NOLINTBEGIN

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Public (global) and Extern Variables
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Private (static) Variables
 *------------------------------------------------------------------------------
 */

static uint32_t s_xQueueCreate_called;
static uint32_t s_xQueueSend_called;
static uint32_t s_xQueueSendFromISR_called;
static uint32_t s_xQueueReceive_called;
static uint32_t s_xQueueReceiveFromISR_called;
static uint32_t s_xQueuePeek_called;
static uint32_t s_xQueuePeekFromISR_called;

// Last args
static UBaseType_t s_xQueueCreate_uxQueueLength;
static UBaseType_t s_xQueueCreate_uxItemSize;

static QueueHandle_t s_xQueueSend_xQueue;
static const void*   s_xQueueSend_pvItemToQueue;
static TickType_t    s_xQueueSend_xTicksToWait;

static QueueHandle_t s_xQueueSendFromISR_xQueue;
static const void*   s_xQueueSendFromISR_pvItemToQueue;
static BaseType_t*   s_xQueueSendFromISR_pxHigherPriorityTaskWoken;

static QueueHandle_t s_xQueueReceive_xQueue;
static void*         s_xQueueReceive_pvBuffer;
static TickType_t    s_xQueueReceive_xTicksToWait;

static QueueHandle_t s_xQueueReceiveFromISR_xQueue;
static void*         s_xQueueReceiveFromISR_pvBuffer;
static BaseType_t*   s_xQueueReceiveFromISR_pxHigherPriorityTaskWoken;

static QueueHandle_t s_xQueuePeek_xQueue;
static void*         s_xQueuePeek_pvBuffer;
static TickType_t    s_xQueuePeek_xTicksToWait;

static QueueHandle_t s_xQueuePeekFromISR_xQueue;
static void*         s_xQueuePeekFromISR_pvBuffer;

// Configurable return values
static QueueHandle_t s_xQueueCreate_return         = ( QueueHandle_t )0;
static BaseType_t    s_xQueueSend_return           = pdPASS;
static BaseType_t    s_xQueueSendFromISR_return    = pdPASS;
static BaseType_t    s_xQueueReceive_return        = pdPASS;
static BaseType_t    s_xQueueReceiveFromISR_return = pdPASS;
static BaseType_t    s_xQueuePeek_return           = pdPASS;
static BaseType_t    s_xQueuePeekFromISR_return    = pdPASS;

// Optional output behaviour (copy bytes into provided buffer)
static uint8_t     s_xQueueReceive_write_data[64];
static UBaseType_t s_xQueueReceive_write_len;
static BaseType_t  s_xQueueReceive_should_write;

static uint8_t     s_xQueueReceiveFromISR_write_data[64];
static UBaseType_t s_xQueueReceiveFromISR_write_len;
static BaseType_t  s_xQueueReceiveFromISR_should_write;

static uint8_t     s_xQueuePeek_write_data[64];
static UBaseType_t s_xQueuePeek_write_len;
static BaseType_t  s_xQueuePeek_should_write;

static uint8_t     s_xQueuePeekFromISR_write_data[64];
static UBaseType_t s_xQueuePeekFromISR_write_len;
static BaseType_t  s_xQueuePeekFromISR_should_write;

// Optional ISR "higher priority task woken" write
static BaseType_t s_xQueueSendFromISR_write_hptw;
static BaseType_t s_xQueueSendFromISR_should_write_hptw;

static BaseType_t s_xQueueReceiveFromISR_write_hptw;
static BaseType_t s_xQueueReceiveFromISR_should_write_hptw;

/**-----------------------------------------------------------------------------
 *  Private (static) Function Prototypes
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */

/**
 * @brief stub implementing FreeRTOS xQueueCreate
 */
QueueHandle_t xQueueCreate( UBaseType_t uxQueueLength, UBaseType_t uxItemSize )
{
    s_xQueueCreate_called++;

    s_xQueueCreate_uxQueueLength = uxQueueLength;
    s_xQueueCreate_uxItemSize    = uxItemSize;

    return s_xQueueCreate_return;
}

/**
 * @brief stub implementing FreeRTOS xQueueSend
 */
BaseType_t xQueueSend( QueueHandle_t xQueue, const void* pvItemToQueue, TickType_t xTicksToWait )
{
    s_xQueueSend_called++;

    s_xQueueSend_xQueue        = xQueue;
    s_xQueueSend_pvItemToQueue = pvItemToQueue;
    s_xQueueSend_xTicksToWait  = xTicksToWait;

    return s_xQueueSend_return;
}

/**
 * @brief stub implementing FreeRTOS xQueueSendFromISR
 */
BaseType_t xQueueSendFromISR( QueueHandle_t xQueue, const void* pvItemToQueue,
                              BaseType_t* pxHigherPriorityTaskWoken )
{
    s_xQueueSendFromISR_called++;

    s_xQueueSendFromISR_xQueue                    = xQueue;
    s_xQueueSendFromISR_pvItemToQueue             = pvItemToQueue;
    s_xQueueSendFromISR_pxHigherPriorityTaskWoken = pxHigherPriorityTaskWoken;

    if ( ( pxHigherPriorityTaskWoken != NULL )
         && ( s_xQueueSendFromISR_should_write_hptw != pdFALSE ) )
    {
        *pxHigherPriorityTaskWoken = s_xQueueSendFromISR_write_hptw;
    }

    return s_xQueueSendFromISR_return;
}

/**
 * @brief stub implementing FreeRTOS xQueueReceive
 */
BaseType_t xQueueReceive( QueueHandle_t xQueue, void* pvBuffer, TickType_t xTicksToWait )
{
    s_xQueueReceive_called++;

    s_xQueueReceive_xQueue       = xQueue;
    s_xQueueReceive_pvBuffer     = pvBuffer;
    s_xQueueReceive_xTicksToWait = xTicksToWait;

    if ( ( pvBuffer != NULL ) && ( s_xQueueReceive_should_write != pdFALSE ) )
    {
        const UBaseType_t n =
            ( s_xQueueReceive_write_len > ( UBaseType_t )sizeof( s_xQueueReceive_write_data ) )
                ? ( UBaseType_t )sizeof( s_xQueueReceive_write_data )
                : s_xQueueReceive_write_len;
        ( void )memcpy( pvBuffer, s_xQueueReceive_write_data, ( size_t )n );
    }

    return s_xQueueReceive_return;
}

/**
 * @brief stub implementing FreeRTOS xQueueReceiveFromISR
 */
BaseType_t xQueueReceiveFromISR( QueueHandle_t xQueue, void* pvBuffer,
                                 BaseType_t* pxHigherPriorityTaskWoken )
{
    s_xQueueReceiveFromISR_called++;

    s_xQueueReceiveFromISR_xQueue                    = xQueue;
    s_xQueueReceiveFromISR_pvBuffer                  = pvBuffer;
    s_xQueueReceiveFromISR_pxHigherPriorityTaskWoken = pxHigherPriorityTaskWoken;

    if ( ( pvBuffer != NULL ) && ( s_xQueueReceiveFromISR_should_write != pdFALSE ) )
    {
        const UBaseType_t n = ( s_xQueueReceiveFromISR_write_len
                                > ( UBaseType_t )sizeof( s_xQueueReceiveFromISR_write_data ) )
                                  ? ( UBaseType_t )sizeof( s_xQueueReceiveFromISR_write_data )
                                  : s_xQueueReceiveFromISR_write_len;
        ( void )memcpy( pvBuffer, s_xQueueReceiveFromISR_write_data, ( size_t )n );
    }

    if ( ( pxHigherPriorityTaskWoken != NULL )
         && ( s_xQueueReceiveFromISR_should_write_hptw != pdFALSE ) )
    {
        *pxHigherPriorityTaskWoken = s_xQueueReceiveFromISR_write_hptw;
    }

    return s_xQueueReceiveFromISR_return;
}

/**
 * @brief stub implementing FreeRTOS xQueuePeek
 */
BaseType_t xQueuePeek( QueueHandle_t xQueue, void* pvBuffer, TickType_t xTicksToWait )
{
    s_xQueuePeek_called++;

    s_xQueuePeek_xQueue       = xQueue;
    s_xQueuePeek_pvBuffer     = pvBuffer;
    s_xQueuePeek_xTicksToWait = xTicksToWait;

    if ( ( pvBuffer != NULL ) && ( s_xQueuePeek_should_write != pdFALSE ) )
    {
        const UBaseType_t n =
            ( s_xQueuePeek_write_len > ( UBaseType_t )sizeof( s_xQueuePeek_write_data ) )
                ? ( UBaseType_t )sizeof( s_xQueuePeek_write_data )
                : s_xQueuePeek_write_len;
        ( void )memcpy( pvBuffer, s_xQueuePeek_write_data, ( size_t )n );
    }

    return s_xQueuePeek_return;
}

/**
 * @brief stub implementing FreeRTOS xQueuePeekFromISR
 */
BaseType_t xQueuePeekFromISR( QueueHandle_t xQueue, void* pvBuffer )
{
    s_xQueuePeekFromISR_called++;

    s_xQueuePeekFromISR_xQueue   = xQueue;
    s_xQueuePeekFromISR_pvBuffer = pvBuffer;

    if ( ( pvBuffer != NULL ) && ( s_xQueuePeekFromISR_should_write != pdFALSE ) )
    {
        const UBaseType_t n = ( s_xQueuePeekFromISR_write_len
                                > ( UBaseType_t )sizeof( s_xQueuePeekFromISR_write_data ) )
                                  ? ( UBaseType_t )sizeof( s_xQueuePeekFromISR_write_data )
                                  : s_xQueuePeekFromISR_write_len;
        ( void )memcpy( pvBuffer, s_xQueuePeekFromISR_write_data, ( size_t )n );
    }

    return s_xQueuePeekFromISR_return;
}

#endif
// NOLINTEND
