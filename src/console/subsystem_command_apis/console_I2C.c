/******************************************************************************
 *  File:       console_I2C.c
 *  Author:     Coen Pasitchnyj
 *  Created:    3-May-2026
 *
 *  Description:
 *      Console command API for I2C functionality.
 *
 *      This module owns I2C related console command handling and loopback functions.
 *
 *  Notes:
 *      The top level console command handler dispatches to this module for the
 *      "i2c_loopback" command namespace.
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include "console.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include "exec_i2c.h"
#include "console_I2C.h"

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
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */

/**
 * @brief Parses the master and slave I2C channel selection.
 */
bool CONSOLE_Parse_I2C_Master_And_Slave( const char* arg, HWI2CChannel_T* master_channel,
                                         HWI2CChannel_T* slave_channel )
{
    if ( ( arg == NULL ) || ( master_channel == NULL ) || ( slave_channel == NULL ) )
    {
        return false;
    }

    if ( strcmp( arg, "1" ) == 0 )
    {
        *master_channel = HW_I2C_CHANNEL_1;
        *slave_channel  = HW_I2C_CHANNEL_2;
        return true;
    }

    if ( strcmp( arg, "2" ) == 0 )
    {
        *master_channel = HW_I2C_CHANNEL_2;
        *slave_channel  = HW_I2C_CHANNEL_1;
        return true;
    }

    return false;
}

/**
 * @brief Parses the requested I2C bus speed.
 */
bool CONSOLE_Parse_I2C_Speed( const char* arg, HWI2CSpeed_T* speed )
{
    if ( ( arg == NULL ) || ( speed == NULL ) )
    {
        return false;
    }

    if ( ( strcmp( arg, "100" ) == 0 ) || ( strcmp( arg, "100k" ) == 0 )
         || ( strcmp( arg, "100khz" ) == 0 ) )
    {
        *speed = HW_I2C_SPEED_100KHZ;
        return true;
    }

    if ( ( strcmp( arg, "400" ) == 0 ) || ( strcmp( arg, "400k" ) == 0 )
         || ( strcmp( arg, "400khz" ) == 0 ) )
    {
        *speed = HW_I2C_SPEED_400KHZ;
        return true;
    }

    return false;
}

/**
 * @brief Parses the loopback direction selector.
 */
bool CONSOLE_Parse_I2C_Loopback_Direction( const char*                    arg,
                                           ConsoleI2CLoopbackDirection_T* direction )
{
    if ( ( arg == NULL ) || ( direction == NULL ) )
    {
        return false;
    }

    if ( strcmp( arg, "m2s" ) == 0 )
    {
        *direction = CONSOLE_I2C_LOOPBACK_DIR_M2S;
        return true;
    }

    if ( strcmp( arg, "s2m" ) == 0 )
    {
        *direction = CONSOLE_I2C_LOOPBACK_DIR_S2M;
        return true;
    }

    return false;
}

/**
 * @brief Parses the I2C transfer path selection.
 */
bool CONSOLE_Parse_I2C_Transfer_Path( const char* arg, HWI2CTransferPath_T* transfer_path )
{
    if ( ( arg == NULL ) || ( transfer_path == NULL ) )
    {
        return false;
    }

    if ( ( strcmp( arg, "interrupt" ) == 0 ) || ( strcmp( arg, "irq" ) == 0 ) )
    {
        *transfer_path = HW_I2C_TRANSFER_INTERRUPT;
        return true;
    }

    if ( strcmp( arg, "dma" ) == 0 )
    {
        *transfer_path = HW_I2C_TRANSFER_DMA;
        return true;
    }

    return false;
}

/**
 * @brief Builds the loopback payload from command arguments.
 */
bool CONSOLE_Build_I2C_Message( uint16_t argc, char* argv[], char* out_message,
                                uint16_t out_message_size, uint16_t* out_message_length )
{
    if ( ( out_message == NULL ) || ( out_message_length == NULL ) || ( out_message_size == 0U ) )
    {
        return false;
    }

    uint16_t tx_len = 0U;
    memset( out_message, 0, out_message_size );

    for ( uint16_t arg_idx = 5U; arg_idx < argc; ++arg_idx )
    {
        const uint16_t part_len = ( uint16_t )strlen( argv[arg_idx] );
        if ( tx_len + part_len + 1U >= out_message_size )
        {
            return false;
        }

        if ( arg_idx > 5U )
        {
            out_message[tx_len] = ' ';
            tx_len++;
        }

        memcpy( &out_message[tx_len], argv[arg_idx], part_len );
        tx_len += part_len;
    }

    if ( tx_len == 0U )
    {
        return false;
    }

    *out_message_length = ( uint16_t )tx_len;
    return true;
}

/**
 * @brief Runs a master-to-slave I2C loopback transfer.
 */
bool CONSOLE_Run_I2C_Loopback_M2S( CONSOLEI2CLoopbackChannels_T channels, uint16_t slave_addr,
                                   const char* tx_message, uint16_t tx_len, char* rx_message,
                                   uint16_t rx_message_size, uint16_t* out_received_len )
{
    bool is_ok = EXEC_I2C_Start_Slave_Receive_External( channels.slave, tx_len );
    if ( !is_ok )
    {
        CONSOLE_Printf( "Failed to start slave receive.\r\n" );
        return false;
    }

    vTaskDelay( pdMS_TO_TICKS( 2 ) );

    is_ok = EXEC_I2C_Master_Transmit_External( channels.master, slave_addr,
                                               ( const uint8_t* )tx_message, tx_len );
    if ( !is_ok )
    {
        CONSOLE_Printf( "Master send failed.\r\n" );
        return false;
    }

    uint16_t received_len = 0U;
    memset( rx_message, 0, rx_message_size );

    for ( uint16_t wait_ms = 0U; wait_ms < 500U; ++wait_ms )
    {
        uint16_t chunk = 0U;
        is_ok          = EXEC_I2C_Receive_Copy_And_Consume(
            channels.slave, ( uint8_t* )&rx_message[received_len],
            ( uint16_t )( rx_message_size - 1U - received_len ), &chunk );

        if ( !is_ok )
        {
            CONSOLE_Printf( "Receive failed.\r\n" );
            return false;
        }

        received_len = ( uint16_t )( received_len + chunk );
        if ( received_len >= tx_len )
        {
            *out_received_len = received_len;
            return true;
        }

        vTaskDelay( pdMS_TO_TICKS( 1 ) );
    }

    *out_received_len = received_len;
    return true;
}

/**
 * @brief Runs a slave-to-master I2C loopback transfer.
 */
bool CONSOLE_Run_I2C_Loopback_S2M( CONSOLEI2CLoopbackChannels_T channels, uint16_t slave_addr,
                                   const char* tx_message, uint16_t tx_len, char* rx_message,
                                   uint16_t rx_message_size, uint16_t* out_received_len )
{
    bool is_ok =
        EXEC_I2C_Slave_Transmit_External( channels.slave, ( const uint8_t* )tx_message, tx_len );
    if ( !is_ok )
    {
        CONSOLE_Printf( "Failed to start slave send.\r\n" );
        return false;
    }

    vTaskDelay( pdMS_TO_TICKS( 2 ) );

    is_ok = EXEC_I2C_Start_Master_Receive_External( channels.master, slave_addr, tx_len );
    if ( !is_ok )
    {
        CONSOLE_Printf( "Master receive start failed.\r\n" );
        return false;
    }

    uint16_t received_len = 0U;
    memset( rx_message, 0, rx_message_size );

    for ( uint16_t wait_ms = 0U; wait_ms < 500U; ++wait_ms )
    {
        uint16_t chunk = 0U;
        is_ok          = EXEC_I2C_Receive_Copy_And_Consume(
            channels.master, ( uint8_t* )&rx_message[received_len],
            ( uint16_t )( rx_message_size - 1U - received_len ), &chunk );

        if ( !is_ok )
        {
            CONSOLE_Printf( "Receive failed.\r\n" );
            return false;
        }

        received_len = ( uint16_t )( received_len + chunk );
        if ( received_len >= tx_len )
        {
            *out_received_len = received_len;
            return true;
        }

        vTaskDelay( pdMS_TO_TICKS( 1 ) );
    }

    *out_received_len = received_len;
    return true;
}
