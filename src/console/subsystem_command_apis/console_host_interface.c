/******************************************************************************
 *  File:       console_host_interface.c
 *  Author:     Callum Rafferty
 *  Created:    22-Sep-2026
 *
 *  Description:
 *      Console command interface for Host Interface live status queries.
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include "console_host_interface.h"
#include "console.h"
#include "host_interface.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/**-----------------------------------------------------------------------------
 *  Private (static) Function Prototypes
 *------------------------------------------------------------------------------
 */

static void CONSOLE_HostInterface_PrintUsage( void );
static void CONSOLE_HostInterface_PrintStatus( void );

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

static void CONSOLE_HostInterface_PrintUsage( void )
{
    CONSOLE_Printf( "Usage: host <status>\r\n" );
}

static void CONSOLE_HostInterface_PrintStatus( void )
{
    HostInterfaceStatus_T status = { 0 };
    HOST_INTERFACE_GetStatus( &status );

    CONSOLE_Printf( "Host Interface Status:\r\n" );
    CONSOLE_Printf( "  Initialized:         %s\r\n", status.is_initialized ? "yes" : "no" );
    CONSOLE_Printf( "  USB link connected:  %s\r\n", status.usb_connected ? "yes" : "no" );
    CONSOLE_Printf( "  Can consume input:   %s\r\n", status.can_consume_incoming ? "yes" : "no" );
    CONSOLE_Printf( "  Outgoing pending:    %s\r\n", status.outgoing_message_pending ? "yes" : "no" );
    CONSOLE_Printf( "  Overflow state:      %s\r\n", status.is_overflowing ? "OVERFLOWING" : "normal" );
    CONSOLE_Printf( "  Expected tick count: %lu\r\n", ( unsigned long )status.expected_tick_count );
    CONSOLE_Printf( "  Notifications:       0x%08lX\r\n", ( unsigned long )status.carry_on_notifications );
}

/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */

void CONSOLE_HostInterface_Command( uint16_t argc, char* argv[] )
{
    if ( argc < 2U || argv == NULL )
    {
        CONSOLE_HostInterface_PrintUsage();
        return;
    }

    if ( strcmp( argv[1], "status" ) == 0 )
    {
        CONSOLE_HostInterface_PrintStatus();
    }
    else
    {
        CONSOLE_HostInterface_PrintUsage();
    }
}
