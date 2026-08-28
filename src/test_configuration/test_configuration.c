/******************************************************************************
 *  File:       test_configuration.c
 *  Author:     Callum Rafferty
 *  Created:    28-Aug-2026
 *
 *  Description:
 *      Owns the active validated runtime configuration for one HIL-RIG test.
 *
 *  Notes:
 *      Host Interface is the future producer. Run State Manager and DUT driver
 *      lifecycle are consumers. A module-owned mutex makes commit and snapshot
 *      operations atomic across those task-context users.
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include "test_configuration.h"
#include "rtos_config.h"
#include <string.h>

/**-----------------------------------------------------------------------------
 *  Private (static) Variables
 *------------------------------------------------------------------------------
 */

static DutDriverConfiguration_T active_configuration;
static bool                     active_configuration_valid = false;
static SemaphoreHandle_t        configuration_mutex        = NULL;
static StaticSemaphore_t        configuration_mutex_storage;

/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */

void TEST_CONFIGURATION_Init( void )
{
    if ( configuration_mutex == NULL )
    {
        configuration_mutex = xSemaphoreCreateMutexStatic( &configuration_mutex_storage );
    }

    if ( configuration_mutex == NULL )
    {
        return;
    }

    ( void )xSemaphoreTake( configuration_mutex, portMAX_DELAY );
    ( void )memset( &active_configuration, 0, sizeof( active_configuration ) );
    active_configuration_valid = true;
    ( void )xSemaphoreGive( configuration_mutex );
}

bool TEST_CONFIGURATION_Commit( const DutDriverConfiguration_T* configuration )
{
    if ( configuration == NULL || configuration_mutex == NULL
         || xSemaphoreTake( configuration_mutex, portMAX_DELAY ) != pdTRUE )
    {
        return false;
    }

    active_configuration       = *configuration;
    active_configuration_valid = true;
    ( void )xSemaphoreGive( configuration_mutex );
    return true;
}

bool TEST_CONFIGURATION_GetActive( DutDriverConfiguration_T* configuration )
{
    if ( configuration == NULL || configuration_mutex == NULL
         || xSemaphoreTake( configuration_mutex, portMAX_DELAY ) != pdTRUE )
    {
        return false;
    }

    const bool is_valid = active_configuration_valid;
    if ( is_valid )
    {
        *configuration = active_configuration;
    }
    ( void )xSemaphoreGive( configuration_mutex );
    return is_valid;
}

bool TEST_CONFIGURATION_IsActive( void )
{
    if ( configuration_mutex == NULL
         || xSemaphoreTake( configuration_mutex, portMAX_DELAY ) != pdTRUE )
    {
        return false;
    }

    const bool is_valid = active_configuration_valid;
    ( void )xSemaphoreGive( configuration_mutex );
    return is_valid;
}

void TEST_CONFIGURATION_Clear( void )
{
    if ( configuration_mutex == NULL
         || xSemaphoreTake( configuration_mutex, portMAX_DELAY ) != pdTRUE )
    {
        return;
    }

    ( void )memset( &active_configuration, 0, sizeof( active_configuration ) );
    active_configuration_valid = false;
    ( void )xSemaphoreGive( configuration_mutex );
}
