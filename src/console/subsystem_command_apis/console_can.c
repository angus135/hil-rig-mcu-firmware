/******************************************************************************
 *  File:       console_uart.c
 *  Author:     Callum Rafferty
 *  Created:    29 Apr 2026
 *
 *  Description:
 *      Console command API for DUT facing UART functionality.
 *
 *      This module owns UART related console command handling, including
 *      channel configuration and UART loopback testing commands.
 *
 *  Notes:
 *      The top level console command handler dispatches to this module for the
 *      "uart" command namespace.
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include "console_can.h"

#include "console.h"
#include "exec_can.h"
#include "hw_can.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Private Variables
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Private Function Prototypes
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Private Variables: Dispatch Tables
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

/**
 * @brief Transmits a 8 byte message over xbCan
 *
 * @param argc - The number of arguments
 * @param argv - pointer to each argument string
 *
 * @returns void
 */
static void CONSOLE_Command_Can_tx( uint16_t argc, char* argv[] )
{
    if ( argc < 3 )
    {
        CONSOLE_Printf( "Incorrect number of inputs, expected atleast 2 but recieved %d",
                        argc - 2 );
        return;
    }
    char out[argc - 2][8];
    for ( int j = 0; j < ( argc - 2 ); j++ )
    {
        int len = strlen( argv[j + 2] );
        // fill packet with '_'
        for ( int i = 0; i < 8; i++ )
        {
            out[j][i] = '_';
        }
        if ( len > 8 )
        {
            len = 8;
        }
        // move data into packet
        CONSOLE_Printf( "Adding %s to buffer...\n\r", argv[j + 2] );
        for ( int i = 0; i < len; i++ )
        {
            out[j][i] = argv[j + 2][i];
        }
    }
    if ( strcmp( argv[1], "1" ) == 0 )
    {
        if ( HW_CAN_Tx_Buffer_Write1( out, argc - 2 ) != 0 )
        {
            CONSOLE_Printf( "Buffer Error" );
            return;
        }
        CONSOLE_Printf( "Written to buffer...\n\r" );
        HW_CAN_Tx_Trigger1();
        CONSOLE_Printf( "Transmitted on channel 1" );
    }
    else if ( strcmp( argv[1], "2" ) == 0 )
    {
        if ( HW_CAN_Tx_Buffer_Write2( out, argc - 2 ) != 0 )
        {
            CONSOLE_Printf( "Buffer Error" );
            return;
        }
        CONSOLE_Printf( "Written to buffer...\n\r" );
        HW_CAN_Tx_Trigger2();
        CONSOLE_Printf( "Transmitted on channel 2" );
    }
    else
    {
        CONSOLE_Printf( "Unknown channel %s\n\r", argv[1] );
    }
}

/**
 * @brief Transmits a 8 byte message over xbCan
 *
 * @param argc - The number of arguments
 * @param argv - pointer to each argument string
 *
 * @returns void
 */
static void CONSOLE_Command_Can_config( uint16_t argc, char* argv[] )
{
    int check = HW_CAN_Configure1( 1000000 );
    if ( check == 1 )
    {
        CONSOLE_Printf( "Can 1  Timing set up error" );
        return;
    }
    if ( check == 2 )
    {
        CONSOLE_Printf( "Can 1  Filter set up error" );
        return;
    }
    if ( check == 3 )
    {
        CONSOLE_Printf( "Can 1 Start set up error" );
        return;
    }
    if ( check != 0 )
    {
        CONSOLE_Printf( "Can 1 Config Error" );
        return;
    }
    check = HW_CAN_Configure2( 1000000 );
    if ( check == 1 )
    {
        CONSOLE_Printf( "Can 2  Timing set up error" );
        return;
    }
    if ( check == 2 )
    {
        CONSOLE_Printf( "Can 2  Filter set up error" );
        return;
    }
    if ( check == 3 )
    {
        CONSOLE_Printf( "Can 2 Start set up error" );
        return;
    }
    if ( check != 0 )
    {
        CONSOLE_Printf( "Can 2 Config Error" );
        return;
    }
    CONSOLE_Printf( "Can 1&2 Set up correctly" );
}

/**
 * @brief Transmits a 8 byte message over xbCan
 *
 * @param argc - The number of arguments
 * @param argv - pointer to each argument string
 *
 * @returns void
 */
static void CONSOLE_Command_Can_rx( uint16_t argc, char* argv[] )
{
    if ( argc != 2 )
    {
        CONSOLE_Printf( "Incorrect number of inputs, expected 1 but recieved %d", argc - 1 );
        return;
    }
    char     out[20][8];
    uint16_t read = 0;
    for ( int i = 0; i < 20; i++ )
    {
        for ( int j = 0; j < 8; j++ )
        {
            out[i][j] = '0';
        }
    }
    if ( strcmp( argv[1], "1" ) == 0 )
    {
        read = EXEC_CAN_Rx_Buffer_Read1( out );
        if ( read == 0 )
        {
            CONSOLE_Printf( "Nothing in channel 1 buffer\n\r" );
            return;
        }
    }
    else if ( strcmp( argv[1], "2" ) == 0 )
    {
        read = EXEC_CAN_Rx_Buffer_Read2( out );
        if ( read == 0 )
        {
            CONSOLE_Printf( "Nothing in channel 2 buffer\n\r" );
            return;
        }
    }
    else
    {
        CONSOLE_Printf( "Unknown parameter %s\n\r", argv[1] );
        return;
    }
    for ( int i = 0; i < read; i++ )
    {
        CONSOLE_Printf( "Recieved: %s", out );
    }
}

/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */

/**
 * @brief Transmits a 8 byte message over xbCan
 *
 * @param argc - The number of arguments
 * @param argv - pointer to each argument string
 *
 * @returns void
 */
void CONSOLE_CAN_Command_Handler( uint16_t argc, char* argv[] )
{
    uint16_t pass_argc = 0;
    char*    pass_argv[8];
    if ( argc < 2 )
    {
        CONSOLE_Printf( "Incorrect number of inputs, expected atleast 1 but recieved %d",
                        argc - 1 );
        return;
    }
    if ( strcmp( argv[1], "tx" ) == 0 )
    {
        if ( argc < 3 )
        {
            CONSOLE_Printf( "Incorrect number of inputs, expected atleast 2 but recieved %d",
                            argc - 1 );
            return;
        }
        if ( strcmp( argv[2], "1" ) == 0 )
        {
            if ( argc < 4 )
            {
                CONSOLE_Printf( "Incorrect number of inputs, expected atleast 3 but recieved %d",
                                argc - 1 );
                return;
            }
            // transmitting on channel 1
            pass_argc = argc - 1;
            for ( int i = 1; i < argc - 2; i++ )
            {
                pass_argv[i] = argv[i + 1];
            }
            CONSOLE_Command_Can_rx( pass_argc, pass_argv );
            return;
        }
        else if ( strcmp( argv[2], "2" ) == 0 )
        {
            if ( argc < 4 )
            {
                CONSOLE_Printf( "Incorrect number of inputs, expected atleast 3 but recieved %d",
                                argc - 1 );
                return;
            }
            // transmitting on channel 2
            pass_argc = argc - 1;
            for ( int i = 1; i < argc - 2; i++ )
            {
                pass_argv[i] = argv[i + 1];
            }
            CONSOLE_Command_Can_rx( pass_argc, pass_argv );
            return;
        }
        else
        {
            CONSOLE_Printf( "Uknown channel, expected <1|2> but recieved %d", argv[2] );
            return;
        }
    }
    else if ( strcmp( argv[1], "rx" ) == 0 )
    {
        if ( argc < 3 )
        {
            CONSOLE_Printf( "Incorrect number of inputs, expected atleast 2 but recieved %d",
                            argc - 1 );
            return;
        }
        if ( strcmp( argv[2], "1" ) == 0 )
        {
            // recieving on channel 1
            pass_argc    = 2;
            pass_argv[1] = "1";
            CONSOLE_Command_Can_rx( pass_argc, pass_argv );
            return;
        }
        else if ( strcmp( argv[2], "2" ) == 0 )
        {
            // recieving on channel 2
            pass_argc    = 2;
            pass_argv[1] = "2";
            CONSOLE_Command_Can_rx( pass_argc, pass_argv );
            return;
        }
        else
        {
            CONSOLE_Printf( "Uknown channel, expected <1|2> but recieved %d", argv[2] );
            return;
        }
    }
    else if ( strcmp( argv[1], "config" ) == 0 )
    {
        // can configure
        CONSOLE_Command_Can_config( pass_argc, pass_argv );
        return;
    }
    else
    {
        CONSOLE_Printf( "Uknown command, expected <tx|rx> but recieved %s", argv[1] );
        return;
    }
}
