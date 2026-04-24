/******************************************************************************
 *  File:       command_handler.c
 *  Author:     Angus Corr
 *  Created:    21-Dec-2025
 *
 *  Description:
 *      Implementation of console commands
 *
 *  Notes:
 *
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include "console.h"
#include "exec_i2c.h"
#include "logic_expander.h"
#include "execution_manager.h"
#include "hw_gpio.h"
#include "exec_uart.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

typedef struct Command_T
{
    const char* command_name;
    void ( *command_handler )( uint16_t, char** );
    const char* command_description;
} Command_T;

typedef struct
{
    bool     is_configured;
    uint32_t baud_rate;
} ConsoleUartLoopbackState_T;

typedef enum
{
    CONSOLE_I2C_LOOPBACK_DIR_M2S,
    CONSOLE_I2C_LOOPBACK_DIR_S2M,
} ConsoleI2CLoopbackDirection_T;

/**-----------------------------------------------------------------------------
 *  Public (global) and Extern Variables
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Private (static) Function Prototypes
 *------------------------------------------------------------------------------
 */
static void CONSOLE_Command_Help( uint16_t argc, char* argv[] );
static void CONSOLE_Command_Echo( uint16_t argc, char* argv[] );
static void CONSOLE_Command_Test_Scheduler( uint16_t argc, char* argv[] );
static void CONSOLE_Command_Clear( uint16_t argc, char* argv[] );
static void CONSOLE_Command_LED( uint16_t argc, char* argv[] );
static void CONSOLE_Command_UART_Loopback( uint16_t argc, char* argv[] );
static void CONSOLE_Command_Expander( uint16_t argc, char* argv[] );
static void CONSOLE_Command_I2C_Loopback( uint16_t argc, char* argv[] );
static bool CONSOLE_Parse_Expander_Port( const char* arg, LogicExpanderPort_T* port );
static bool CONSOLE_Parse_I2C_Master_And_Slave( const char* arg,
                                                 EXECI2CExternalChannel_T* master_channel,
                                                 EXECI2CExternalChannel_T* slave_channel );
static bool CONSOLE_Parse_I2C_Loopback_Direction( const char* arg,
                                                   ConsoleI2CLoopbackDirection_T* direction );
static bool CONSOLE_Parse_I2C_Speed( const char* arg, EXECI2CSpeed_T* speed );
static bool CONSOLE_Parse_I2C_Transfer_Path( const char* arg, EXECI2CTransferPath_T* transfer_path );
static bool CONSOLE_Build_I2C_Message( uint16_t argc, char* argv[], char* out_message,
                                       size_t out_message_size, uint16_t* out_message_length );
static bool CONSOLE_Run_I2C_Loopback_M2S( EXECI2CExternalChannel_T master_channel,
                                          EXECI2CExternalChannel_T slave_channel,
                                          uint16_t slave_addr,
                                          const char* tx_message,
                                          uint16_t tx_len,
                                          char* rx_message,
                                          uint16_t rx_message_size,
                                          uint16_t* out_received_len );
static bool CONSOLE_Run_I2C_Loopback_S2M( EXECI2CExternalChannel_T master_channel,
                                          EXECI2CExternalChannel_T slave_channel,
                                          uint16_t slave_addr,
                                          const char* tx_message,
                                          uint16_t tx_len,
                                          char* rx_message,
                                          uint16_t rx_message_size,
                                          uint16_t* out_received_len );
/**-----------------------------------------------------------------------------
 *  Private (static) Variables
 *------------------------------------------------------------------------------
 */

// clang-format off

const Command_T CONSOLE_COMMANDS[] = {
    {"?",       CONSOLE_Command_Help,       "Show available commands."},
    {"help",    CONSOLE_Command_Help,       "Show available commands."},
    {"echo",    CONSOLE_Command_Echo,       "Echoes the provided arguments."},
    {"execution_manager",    CONSOLE_Command_Test_Scheduler,       "Starts the test scheduler."},
    {"clear",  CONSOLE_Command_Clear,       "Clears the console."},
    {"led",    CONSOLE_Command_LED,         "Toggle an LED. Usage: led toggle <green|blue|red|test>"},
    {"uart_loopback", CONSOLE_Command_UART_Loopback, "Configuring Channels and Rx/Tx loopback testing for Uart"},
    {"expander", CONSOLE_Command_Expander,  "Expander test commands. Usage: expander <config|set|send|reset> [args]"},
    {"i2c_loopback", CONSOLE_Command_I2C_Loopback, "Loopback test. Usage: i2c_loopback <master:1|2> <dir:m2s|s2m> <speed:100|400> \n\r <op:interrupt|dma> <message...>"}
};

// clang-format on

static ConsoleUartLoopbackState_T s_uart_loopback_state = { 0 };

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

static bool CONSOLE_Parse_Expander_Port( const char* arg, LogicExpanderPort_T* port )
{
    if ( ( arg == NULL ) || ( port == NULL ) )
    {
        return false;
    }

    if ( strcmp( arg, "A" ) == 0 )
    {
        *port = LOGIC_EXPANDER_PORT_A;
        return true;
    }

    if ( strcmp( arg, "B" ) == 0 )
    {
        *port = LOGIC_EXPANDER_PORT_B;
        return true;
    }

    return false;
}

static void CONSOLE_Command_Expander( uint16_t argc, char* argv[] )
{
    if ( argc < 2 )
    {
        CONSOLE_Printf( "Usage:\r\n" );
        CONSOLE_Printf( "  expander config      - Initialize all active expanders\r\n" );
        CONSOLE_Printf( "  expander set <addr> <port> <value> - Set control bits (e.g. expander set 0x20 A 0xFF)\r\n" );
        CONSOLE_Printf( "  expander send       - Send all staged bits to hardware\r\n" );
        CONSOLE_Printf( "  expander reset      - Reset all bits to 0 and send\r\n" );
        return;
    }

    if ( strcmp( argv[1], "config" ) == 0 )
    {
        EXECI2CStatus_T i2c_status = EXEC_I2C_Configuration_Internal();
        if ( i2c_status != EXEC_I2C_STATUS_OK )
        {
            CONSOLE_Printf( "Expander internal I2C config failed (status=%d)\r\n", ( int )i2c_status );
            return;
        }

        LogicExpanderStatus_T status = expander_self_config();
        if ( status == LOGIC_EXPANDER_STATUS_OK )
        {
            CONSOLE_Printf( "Expander config: OK\r\n" );
        }
        else
        {
            CONSOLE_Printf( "Expander config failed (status=%d)\r\n", ( int )status );
        }
        return;
    }

    if ( strcmp( argv[1], "set" ) == 0 )
    {
        if ( argc < 5 )
        {
            CONSOLE_Printf( "Usage: expander set <addr> <port> <value>\r\n" );
            CONSOLE_Printf( "Example: expander set 0x20 A 0xFF\r\n" );
            return;
        }

        uint32_t addr = 0U;
        if ( sscanf( argv[2], "0x%x", ( unsigned int* )&addr ) != 1 )
        {
            CONSOLE_Printf( "Invalid address format. Use 0x20, 0x21, etc.\r\n" );
            return;
        }

        if ( addr < 0x20U || addr > 0x27U )
        {
            CONSOLE_Printf( "Address out of range (0x20-0x27).\r\n" );
            return;
        }

        uint8_t expander_index = ( uint8_t )( addr - 0x20U );

        LogicExpanderStateSnapshot_T snapshot_before = { 0 };
        LogicExpanderStatus_T snapshot_status =
            expander_get_state_snapshot( expander_index, &snapshot_before );
        if ( snapshot_status == LOGIC_EXPANDER_STATUS_OK )
        {
            CONSOLE_Printf( "Expander state before set: idx=%u addr=0x%04X OLATA=0x%02X OLATB=0x%02X\r\n",
                            ( unsigned int )expander_index,
                            ( unsigned int )snapshot_before.device_address_7bit,
                            ( unsigned int )snapshot_before.olat_a,
                            ( unsigned int )snapshot_before.olat_b );
        }

        LogicExpanderPort_T port = LOGIC_EXPANDER_PORT_A;
        if ( !CONSOLE_Parse_Expander_Port( argv[3], &port ) )
        {
            CONSOLE_Printf( "Invalid port. Use exact 'A' or 'B'.\r\n" );
            return;
        }

        uint32_t value = 0U;
        if ( sscanf( argv[4], "0x%x", ( unsigned int* )&value ) != 1 )
        {
            CONSOLE_Printf( "Invalid value format. Use 0x00, 0xFF, etc.\r\n" );
            return;
        }

        if ( value > 0xFFU )
        {
            CONSOLE_Printf( "Value out of range (0x00-0xFF).\r\n" );
            return;
        }

        uint8_t byte_value = ( uint8_t )value;

        for ( uint8_t bit_idx = 0U; bit_idx < 8U; ++bit_idx )
        {
            bool bit_set = ( byte_value & ( 1U << bit_idx ) ) != 0U;
            LogicExpanderStatus_T status = expander_load_control_bit( expander_index, port, bit_idx, bit_set );
            if ( status != LOGIC_EXPANDER_STATUS_OK )
            {
                CONSOLE_Printf( "Failed to set bit %u (status=%d)\r\n", ( unsigned int )bit_idx,
                                ( int )status );
                return;
            }
        }

        LogicExpanderStateSnapshot_T snapshot_after = { 0 };
        snapshot_status = expander_get_state_snapshot( expander_index, &snapshot_after );
        if ( snapshot_status == LOGIC_EXPANDER_STATUS_OK )
        {
            CONSOLE_Printf( "Expander state after set:  idx=%u addr=0x%04X OLATA=0x%02X OLATB=0x%02X\r\n",
                            ( unsigned int )expander_index,
                            ( unsigned int )snapshot_after.device_address_7bit,
                            ( unsigned int )snapshot_after.olat_a,
                            ( unsigned int )snapshot_after.olat_b );
        }

        CONSOLE_Printf( "Set expander 0x%02X port %c = 0x%02X\r\n", ( unsigned int )addr,
                        ( port == LOGIC_EXPANDER_PORT_A ) ? 'A' : 'B', ( unsigned int )byte_value );
        return;
    }

    if ( strcmp( argv[1], "send" ) == 0 )
    {
        LogicExpanderStatus_T status = expander_send_control_bits();
        if ( status == LOGIC_EXPANDER_STATUS_OK )
        {
            CONSOLE_Printf( "Expander send: OK\r\n" );
        }
        else
        {
            CONSOLE_Printf( "Expander send failed (status=%d)\r\n", ( int )status );
        }
        return;
    }

    if ( strcmp( argv[1], "reset" ) == 0 )
    {
        for ( uint8_t idx = 0U; idx < LOGIC_EXPANDER_COUNT; ++idx )
        {
            for ( uint8_t bit_idx = 0U; bit_idx < 8U; ++bit_idx )
            {
                ( void )expander_load_control_bit( idx, LOGIC_EXPANDER_PORT_A, bit_idx, false );
                ( void )expander_load_control_bit( idx, LOGIC_EXPANDER_PORT_B, bit_idx, false );
            }
        }

        LogicExpanderStatus_T status = expander_send_control_bits();
        if ( status == LOGIC_EXPANDER_STATUS_OK )
        {
            CONSOLE_Printf( "Expander reset: OK (all bits cleared and sent)\r\n" );
        }
        else
        {
            CONSOLE_Printf( "Expander reset failed (status=%d)\r\n", ( int )status );
        }
        return;
    }

    CONSOLE_Printf( "Unknown expander subcommand: %s\r\n", argv[1] );
}

static bool CONSOLE_Parse_I2C_Master_And_Slave( const char* arg,
                                                 EXECI2CExternalChannel_T* master_channel,
                                                 EXECI2CExternalChannel_T* slave_channel )
{
    if ( ( arg == NULL ) || ( master_channel == NULL ) || ( slave_channel == NULL ) )
    {
        return false;
    }

    if ( strcmp( arg, "1" ) == 0 )
    {
        *master_channel = EXEC_I2C_EXTERNAL_1;
        *slave_channel  = EXEC_I2C_EXTERNAL_2;
        return true;
    }

    if ( strcmp( arg, "2" ) == 0 )
    {
        *master_channel = EXEC_I2C_EXTERNAL_2;
        *slave_channel  = EXEC_I2C_EXTERNAL_1;
        return true;
    }

    return false;
}

static bool CONSOLE_Parse_I2C_Speed( const char* arg, EXECI2CSpeed_T* speed )
{
    if ( ( arg == NULL ) || ( speed == NULL ) )
    {
        return false;
    }

    if ( ( strcmp( arg, "100" ) == 0 ) || ( strcmp( arg, "100k" ) == 0 ) ||
         ( strcmp( arg, "100khz" ) == 0 ) )
    {
        *speed = EXEC_I2C_SPEED_100KHZ;
        return true;
    }

    if ( ( strcmp( arg, "400" ) == 0 ) || ( strcmp( arg, "400k" ) == 0 ) ||
         ( strcmp( arg, "400khz" ) == 0 ) )
    {
        *speed = EXEC_I2C_SPEED_400KHZ;
        return true;
    }

    return false;
}

static bool CONSOLE_Parse_I2C_Loopback_Direction( const char* arg,
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

static bool CONSOLE_Parse_I2C_Transfer_Path( const char* arg, EXECI2CTransferPath_T* transfer_path )
{
    if ( ( arg == NULL ) || ( transfer_path == NULL ) )
    {
        return false;
    }

    if ( ( strcmp( arg, "interrupt" ) == 0 ) || ( strcmp( arg, "irq" ) == 0 ) )
    {
        *transfer_path = EXEC_I2C_TRANSFER_INTERRUPT;
        return true;
    }

    if ( strcmp( arg, "dma" ) == 0 )
    {
        *transfer_path = EXEC_I2C_TRANSFER_DMA;
        return true;
    }

    return false;
}

static bool CONSOLE_Build_I2C_Message( uint16_t argc, char* argv[], char* out_message,
                                       size_t out_message_size, uint16_t* out_message_length )
{
    if ( ( out_message == NULL ) || ( out_message_length == NULL ) || ( out_message_size == 0U ) )
    {
        return false;
    }

    size_t tx_len = 0U;
    memset( out_message, 0, out_message_size );

    for ( uint16_t arg_idx = 5U; arg_idx < argc; ++arg_idx )
    {
        const size_t part_len = strlen( argv[arg_idx] );
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

static bool CONSOLE_Run_I2C_Loopback_M2S( EXECI2CExternalChannel_T master_channel,
                                          EXECI2CExternalChannel_T slave_channel,
                                          uint16_t slave_addr,
                                          const char* tx_message,
                                          uint16_t tx_len,
                                          char* rx_message,
                                          uint16_t rx_message_size,
                                          uint16_t* out_received_len )
{
    EXECI2CStatus_T status = EXEC_I2C_Start_Slave_Receive( slave_channel, tx_len );
    if ( status != EXEC_I2C_STATUS_OK )
    {
        CONSOLE_Printf( "Failed to start slave receive (status=%d).\r\n", ( int )status );
        return false;
    }

    vTaskDelay( pdMS_TO_TICKS( 2 ) );

    status = EXEC_I2C_Master_Send( master_channel, slave_addr, ( const uint8_t* )tx_message, tx_len );
    if ( status != EXEC_I2C_STATUS_OK )
    {
        CONSOLE_Printf( "Master send failed (status=%d).\r\n", ( int )status );
        return false;
    }

    uint16_t received_len = 0U;
    memset( rx_message, 0, rx_message_size );

    for ( uint16_t wait_ms = 0U; wait_ms < 500U; ++wait_ms )
    {
        uint16_t chunk = 0U;
        status = EXEC_I2C_Receive_Copy_And_Consume(
            slave_channel, ( uint8_t* )&rx_message[received_len],
            ( uint16_t )( rx_message_size - 1U - received_len ), &chunk );

        if ( status != EXEC_I2C_STATUS_OK )
        {
            CONSOLE_Printf( "Receive failed (status=%d).\r\n", ( int )status );
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

static bool CONSOLE_Run_I2C_Loopback_S2M( EXECI2CExternalChannel_T master_channel,
                                          EXECI2CExternalChannel_T slave_channel,
                                          uint16_t slave_addr,
                                          const char* tx_message,
                                          uint16_t tx_len,
                                          char* rx_message,
                                          uint16_t rx_message_size,
                                          uint16_t* out_received_len )
{
    EXECI2CStatus_T status =
        EXEC_I2C_Slave_Send( slave_channel, ( const uint8_t* )tx_message, tx_len );
    if ( status != EXEC_I2C_STATUS_OK )
    {
        CONSOLE_Printf( "Failed to start slave send (status=%d).\r\n", ( int )status );
        return false;
    }

    vTaskDelay( pdMS_TO_TICKS( 2 ) );

    status = EXEC_I2C_Start_Master_Receive( master_channel, slave_addr, tx_len );
    if ( status != EXEC_I2C_STATUS_OK )
    {
        CONSOLE_Printf( "Master receive start failed (status=%d).\r\n", ( int )status );
        return false;
    }

    uint16_t received_len = 0U;
    memset( rx_message, 0, rx_message_size );

    for ( uint16_t wait_ms = 0U; wait_ms < 500U; ++wait_ms )
    {
        uint16_t chunk = 0U;
        status = EXEC_I2C_Receive_Copy_And_Consume(
            master_channel, ( uint8_t* )&rx_message[received_len],
            ( uint16_t )( rx_message_size - 1U - received_len ), &chunk );

        if ( status != EXEC_I2C_STATUS_OK )
        {
            CONSOLE_Printf( "Receive failed (status=%d).\r\n", ( int )status );
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

static void CONSOLE_Command_I2C_Loopback( uint16_t argc, char* argv[] )
{
    if ( argc < 6U )
    {
        CONSOLE_Printf( "Usage: i2c_loopback <master:1|2> <dir:m2s|s2m> <speed:100|400> <op:interrupt|dma> <message...>\r\n" );
        return;
    }

    EXECI2CExternalChannel_T master_channel = EXEC_I2C_EXTERNAL_1;
    EXECI2CExternalChannel_T slave_channel  = EXEC_I2C_EXTERNAL_2;
    if ( !CONSOLE_Parse_I2C_Master_And_Slave( argv[1], &master_channel, &slave_channel ) )
    {
        CONSOLE_Printf( "Invalid master channel. Use 1 or 2.\r\n" );
        return;
    }

    ConsoleI2CLoopbackDirection_T direction = CONSOLE_I2C_LOOPBACK_DIR_M2S;
    if ( !CONSOLE_Parse_I2C_Loopback_Direction( argv[2], &direction ) )
    {
        CONSOLE_Printf( "Invalid direction. Use m2s or s2m.\r\n" );
        return;
    }

    EXECI2CSpeed_T speed = EXEC_I2C_SPEED_100KHZ;
    if ( !CONSOLE_Parse_I2C_Speed( argv[3], &speed ) )
    {
        CONSOLE_Printf( "Invalid speed. Use 100 or 400.\r\n" );
        return;
    }

    EXECI2CTransferPath_T transfer_path = EXEC_I2C_TRANSFER_INTERRUPT;
    if ( !CONSOLE_Parse_I2C_Transfer_Path( argv[4], &transfer_path ) )
    {
        CONSOLE_Printf( "Invalid op. Use interrupt|irq or dma.\r\n" );
        return;
    }

    char tx_message[200];
    uint16_t tx_len = 0U;
    if ( !CONSOLE_Build_I2C_Message( argc, argv, tx_message, sizeof( tx_message ), &tx_len ) )
    {
        CONSOLE_Printf( "Invalid message (empty or too long, max %u chars).\r\n",
                        ( unsigned int )( sizeof( tx_message ) - 1U ) );
        return;
    }

    const uint16_t i2c1_addr = 0x31U;
    const uint16_t i2c2_addr = 0x32U;
    const uint16_t fmpi_addr = 0x33U;

    EXECI2CChannelConfig_T i2c1_cfg = {
        .mode             = ( master_channel == EXEC_I2C_EXTERNAL_1 ) ? EXEC_I2C_MODE_MASTER : EXEC_I2C_MODE_SLAVE,
        .speed            = speed,
        .tx_transfer_path = EXEC_I2C_TRANSFER_INTERRUPT,
        .rx_transfer_path = EXEC_I2C_TRANSFER_INTERRUPT,
        .own_address_7bit = i2c1_addr,
    };

    EXECI2CChannelConfig_T i2c2_cfg = {
        .mode             = ( master_channel == EXEC_I2C_EXTERNAL_2 ) ? EXEC_I2C_MODE_MASTER : EXEC_I2C_MODE_SLAVE,
        .speed            = speed,
        .tx_transfer_path = transfer_path,
        .rx_transfer_path = transfer_path,
        .own_address_7bit = i2c2_addr,
    };

    EXECI2CStatus_T status = EXEC_I2C_Configuration( &i2c1_cfg, &i2c2_cfg, fmpi_addr );
    if ( status != EXEC_I2C_STATUS_OK )
    {
        CONSOLE_Printf( "I2C configuration failed (status=%d).\r\n", ( int )status );
        return;
    }

    const uint16_t slave_addr = ( slave_channel == EXEC_I2C_EXTERNAL_1 ) ? i2c1_addr : i2c2_addr;

    char rx_message[200];
    uint16_t received_len = 0U;
    bool transfer_ok = false;
    if ( direction == CONSOLE_I2C_LOOPBACK_DIR_M2S )
    {
        transfer_ok = CONSOLE_Run_I2C_Loopback_M2S( master_channel, slave_channel, slave_addr,
                                                     tx_message, tx_len, rx_message,
                                                     sizeof( rx_message ), &received_len );
    }
    else
    {
        transfer_ok = CONSOLE_Run_I2C_Loopback_S2M( master_channel, slave_channel, slave_addr,
                                                     tx_message, tx_len, rx_message,
                                                     sizeof( rx_message ), &received_len );
    }

    if ( !transfer_ok )
    {
        return;
    }

    CONSOLE_Printf( "I2C loopback: master=I2C%s slave=I2C%s dir=%s speed=%s i2c1_op=Interrupt i2c2_op=%s\r\n",
                    ( master_channel == EXEC_I2C_EXTERNAL_1 ) ? "1" : "2",
                    ( slave_channel == EXEC_I2C_EXTERNAL_1 ) ? "1" : "2",
                    ( direction == CONSOLE_I2C_LOOPBACK_DIR_M2S ) ? "m2s" : "s2m",
                    ( speed == EXEC_I2C_SPEED_400KHZ ) ? "400kHz" : "100kHz",
                    ( transfer_path == EXEC_I2C_TRANSFER_DMA ) ? "DMA" : "Interrupt" );

    CONSOLE_Printf( "Sent    (%u): %.*s\r\n", ( unsigned int )tx_len, ( int )tx_len, tx_message );
    CONSOLE_Printf( "Received(%u): %.*s\r\n", ( unsigned int )received_len, ( int )received_len,
                    rx_message );

    const bool pass = ( received_len == ( uint16_t )tx_len ) &&
                      ( memcmp( tx_message, rx_message, tx_len ) == 0 );
    CONSOLE_Printf( "Result: %s\r\n", pass ? "PASS" : "FAIL" );
}

/**
 * @brief Handles the help command by providing available commands to the console
 *
 * @param argc - The number of arguments
 * @param argv - pointer to each argument string
 *
 * @returns void
 */
static void CONSOLE_Command_Help( uint16_t argc, char* argv[] )
{
    ( void )argc;
    ( void )argv;
    CONSOLE_Printf( "Available commands:\r\n" );
    for ( size_t command = 0; command < ARRAY_LEN( CONSOLE_COMMANDS ); command++ )
    {
        CONSOLE_Printf( "%s\t- %s\r\n", CONSOLE_COMMANDS[command].command_name,
                        CONSOLE_COMMANDS[command].command_description );
    }
}

/**
 * @brief Handles the echo command by echoing whatever was provided in the command to the console
 *
 * @param argc - The number of arguments
 * @param argv - pointer to each argument string
 *
 * @returns void
 */
static void CONSOLE_Command_Echo( uint16_t argc, char* argv[] )
{
    for ( uint16_t i = 1U; i < argc; i++ )
    {
        CONSOLE_Printf( "%s%s", argv[i], ( i < ( argc - 1U ) ) ? " " : "" );
    }
    CONSOLE_Printf( "\r\n" );
}

/**
 * @brief Starts the test scheduler
 *
 * @param argc - The number of arguments
 * @param argv - pointer to each argument string
 *
 * @returns void
 */
static void CONSOLE_Command_Test_Scheduler( uint16_t argc, char* argv[] )
{
    if ( argc < 2 || argv[1] == NULL )
    {
        CONSOLE_Printf( "Usage:\r\n" );
        CONSOLE_Printf( "  execution_manager start\r\n" );
        CONSOLE_Printf( "  execution_manager stop\r\n" );
        CONSOLE_Printf( "  execution_manager frequency <desired frequency>\r\n" );
        CONSOLE_Printf( "    Note: Desired frequencies can only be 100Hz, 1kHz or 10kHz\r\n" );
        return;
    }

    if ( strcmp( argv[1], "start" ) == 0 )
    {
        EXECUTION_MANAGER_Init();
        EXECUTION_MANAGER_Start();
    }
    else if ( strcmp( argv[1], "stop" ) == 0 )
    {
        EXECUTION_MANAGER_Stop();
    }
    else if ( strcmp( argv[1], "frequency" ) == 0 )
    {
        if ( argc < 3 || argv[2] == NULL )
        {
            CONSOLE_Printf( "Usage:\r\n" );
            CONSOLE_Printf( "  execution_manager frequency <desired frequency>\r\n" );
            CONSOLE_Printf( "    Note: Desired frequencies can only be 100Hz, 1kHz or 10kHz\r\n" );
            return;
        }

        if ( ( strcmp( argv[2], "10k" ) == 0 ) || ( strcmp( argv[2], "10000" ) == 0 ) )
        {
            EXECUTION_MANAGER_Set_Frequency_Mode( FREQUENCY_10KHZ );
            CONSOLE_Printf( "Scheduler Frequency is set to %sHz\r\n", argv[2] );
        }
        else if ( ( strcmp( argv[2], "1k" ) == 0 ) || ( strcmp( argv[2], "1000" ) == 0 ) )
        {
            EXECUTION_MANAGER_Set_Frequency_Mode( FREQUENCY_1KHZ );
            CONSOLE_Printf( "Scheduler Frequency is set to %sHz\r\n", argv[2] );
        }
        else if ( strcmp( argv[2], "100" ) == 0 )
        {
            EXECUTION_MANAGER_Set_Frequency_Mode( FREQUENCY_100HZ );
            CONSOLE_Printf( "Scheduler Frequency is set to %sHz\r\n", argv[2] );
        }
        else
        {
            CONSOLE_Printf( "Invalid: Desired frequencies can only be 100Hz, 1kHz or 10kHz\r\n" );
        }
    }
    else
    {
        CONSOLE_Printf( "Invalid argument: %s\r\n", argv[1] );
        CONSOLE_Printf( "Usage:\r\n" );
        CONSOLE_Printf( "  execution_manager start\r\n" );
        CONSOLE_Printf( "  execution_manager stop\r\n" );
        CONSOLE_Printf( "  execution_manager frequency <desired frequency>\r\n" );
        CONSOLE_Printf( "    Note: Desired frequencies can only be 100Hz, 1kHz or 10kHz\r\n" );
    }
}

static void CONSOLE_Command_Clear( uint16_t argc, char* argv[] )
{
    ( void )argc;
    ( void )argv;
    CONSOLE_Printf( "\033[2J\033[1;1H" );
}

/**
 * @brief Handles the LED toggle command
 *
 * @param argc - The number of arguments
 * @param argv - pointer to each argument string
 *
 * @returns void
 */
static void CONSOLE_Command_LED( uint16_t argc, char* argv[] )
{
    if ( argc != 3 )
    {
        CONSOLE_Printf( "Usage: led toggle <green|blue|red|test>\r\n" );
        return;
    }
    if ( strcmp( argv[1], "toggle" ) == 0 )
    {
        GPIO_T led = GPIO_TEST_INDICATOR;  // default to test indicator if color parsing fails
        if ( strcmp( argv[2], "green" ) == 0 )
        {
            led = GPIO_GREEN_LED_INDICATOR;
        }
        else if ( strcmp( argv[2], "blue" ) == 0 )
        {
            led = GPIO_BLUE_LED_INDICATOR;
        }
        else if ( strcmp( argv[2], "red" ) == 0 )
        {
            led = GPIO_RED_LED_INDICATOR;
        }
        else if ( strcmp( argv[2], "test" ) == 0 )
        {
            led = GPIO_TEST_INDICATOR;
        }
        else
        {
            CONSOLE_Printf( "Unknown LED: %s\r\n", argv[2] );
            return;
        }
        HW_GPIO_Toggle( led );
        CONSOLE_Printf( "Toggled %s LED\r\n", argv[2] );
    }
    else
    {
        CONSOLE_Printf( "Unknown action: %s\r\nUsage: led toggle <green|blue|red|test>\r\n",
                        argv[1] );
    }
}

static void CONSOLE_Command_UART_Loopback( uint16_t argc, char* argv[] )
{
    if ( argc < 2U )
    {
        CONSOLE_Printf( "Usage:\r\n" );
        CONSOLE_Printf( "  uart_loopback configure <baud>\r\n" );
        CONSOLE_Printf( "  uart_loopback deconfigure\r\n" );
        CONSOLE_Printf( "  uart_loopback status\r\n" );
        CONSOLE_Printf( "  uart_loopback start <sender_ch> <receiver_ch> <data ...>\r\n" );
        CONSOLE_Printf( "    note: sender_ch and receiver_ch must be in {ch1,ch2}\r\n" );
        CONSOLE_Printf( "    note: configure uses fixed mode TTL_3V3, 8N1, RX+TX enabled\r\n" );
        return;
    }

    if ( strcmp( argv[1], "configure" ) == 0 && argc == 3U )
    {
        char*    end_ptr   = NULL;
        uint32_t baud_rate = ( uint32_t )strtoul( argv[2], &end_ptr, 10 );

        if ( ( end_ptr == argv[2] ) || ( *end_ptr != '\0' ) )
        {
            CONSOLE_Printf( "Invalid baud rate\r\n" );
            return;
        }

        if ( baud_rate == 0U )
        {
            CONSOLE_Printf( "Invalid baud rate: must be greater than 0\r\n" );
            return;
        }

        if ( baud_rate > 2000000U )
        {
            CONSOLE_Printf( "Invalid baud rate: TTL loopback supports up to 2000000 baud\r\n" );
            return;
        }

        HwUartConfig_T config = { 0 };
        config.interface_mode = HW_UART_MODE_TTL_3V3;
        config.rx_enabled     = true;
        config.tx_enabled     = true;
        config.baud_rate      = baud_rate;
        config.word_length    = HW_UART_WORD_LENGTH_8_BITS;
        config.parity         = HW_UART_PARITY_NONE;
        config.stop_bits      = HW_UART_STOP_BITS_1;

        if ( !EXEC_UART_Apply_Configuration( HW_UART_CHANNEL_1, &config ) )
        {
            CONSOLE_Printf( "Failed to configure ch1\r\n" );
            return;
        }

        if ( !EXEC_UART_Apply_Configuration( HW_UART_CHANNEL_2, &config ) )
        {
            ( void )EXEC_UART_Deconfigure( HW_UART_CHANNEL_1 );
            CONSOLE_Printf( "Failed to configure ch2\r\n" );
            return;
        }

        s_uart_loopback_state.is_configured = true;
        s_uart_loopback_state.baud_rate     = baud_rate;

        CONSOLE_Printf( "uart_loopback configured\r\n" );
        CONSOLE_Printf( "  channels: ch1 + ch2\r\n" );
        CONSOLE_Printf( "  mode: TTL_3V3\r\n" );
        CONSOLE_Printf( "  baud: %lu\r\n", ( unsigned long )baud_rate );
        CONSOLE_Printf( "  framing: 8N1\r\n" );
        CONSOLE_Printf( "  rx/tx: enabled\r\n" );
    }
    else if ( strcmp( argv[1], "deconfigure" ) == 0 && argc == 2U )
    {
        bool ch1_ok = EXEC_UART_Deconfigure( HW_UART_CHANNEL_1 );
        bool ch2_ok = EXEC_UART_Deconfigure( HW_UART_CHANNEL_2 );

        s_uart_loopback_state.is_configured = false;
        s_uart_loopback_state.baud_rate     = 0U;

        if ( ch1_ok && ch2_ok )
        {
            CONSOLE_Printf( "uart_loopback deconfigured: ch1 + ch2\r\n" );
        }
        else if ( !ch1_ok && !ch2_ok )
        {
            CONSOLE_Printf( "Failed to deconfigure ch1 and ch2\r\n" );
        }
        else if ( !ch1_ok )
        {
            CONSOLE_Printf( "Failed to deconfigure ch1\r\n" );
        }
        else
        {
            CONSOLE_Printf( "Failed to deconfigure ch2\r\n" );
        }
    }
    else if ( strcmp( argv[1], "status" ) == 0 && argc == 2U )
    {
        if ( !s_uart_loopback_state.is_configured )
        {
            CONSOLE_Printf( "uart_loopback: not configured\r\n" );
            return;
        }

        CONSOLE_Printf( "uart_loopback: configured\r\n" );
        CONSOLE_Printf( "  channels: ch1 + ch2\r\n" );
        CONSOLE_Printf( "  mode: TTL_3V3\r\n" );
        CONSOLE_Printf( "  baud: %lu\r\n", ( unsigned long )s_uart_loopback_state.baud_rate );
        CONSOLE_Printf( "  framing: 8N1\r\n" );
        CONSOLE_Printf( "  rx/tx: enabled\r\n" );
    }
    else if ( strcmp( argv[1], "start" ) == 0 && argc >= 5U )
    {
        HwUartChannel_T sender_ch;
        HwUartChannel_T receiver_ch;
        char            tx_text[EXEC_UART_MAX_CHUNK_SIZE];
        uint32_t        tx_length = 0U;

        if ( !s_uart_loopback_state.is_configured )
        {
            CONSOLE_Printf( "uart_loopback not configured\r\n" );
            return;
        }

        if ( strcmp( argv[2], "ch1" ) == 0 )
        {
            sender_ch = HW_UART_CHANNEL_1;
        }
        else if ( strcmp( argv[2], "ch2" ) == 0 )
        {
            sender_ch = HW_UART_CHANNEL_2;
        }
        else
        {
            CONSOLE_Printf( "Invalid sender channel: use ch1 or ch2\r\n" );
            return;
        }

        if ( strcmp( argv[3], "ch1" ) == 0 )
        {
            receiver_ch = HW_UART_CHANNEL_1;
        }
        else if ( strcmp( argv[3], "ch2" ) == 0 )
        {
            receiver_ch = HW_UART_CHANNEL_2;
        }
        else
        {
            CONSOLE_Printf( "Invalid receiver channel: use ch1 or ch2\r\n" );
            return;
        }

        tx_text[0] = '\0';

        for ( uint16_t i = 4U; i < argc; i++ )
        {
            size_t token_len = strlen( argv[i] );
            size_t sep_len   = ( i > 4U ) ? 1U : 0U;

            if ( ( tx_length + sep_len + token_len ) >= sizeof( tx_text ) )
            {
                CONSOLE_Printf( "Data too long: max %lu bytes\r\n",
                                ( unsigned long )( sizeof( tx_text ) - 1U ) );
                return;
            }

            if ( i > 4U )
            {
                tx_text[tx_length] = ' ';
                tx_length++;
            }

            memcpy( &tx_text[tx_length], argv[i], token_len );
            tx_length += ( uint32_t )token_len;
            tx_text[tx_length] = '\0';
        }

        if ( tx_length == 0U )
        {
            CONSOLE_Printf( "No data provided\r\n" );
            return;
        }

        {
            uint8_t  discard_buf[128];
            uint32_t bytes_read = 0U;

            do
            {
                bytes_read = 0U;
                ( void )EXEC_UART_Read( HW_UART_CHANNEL_1, discard_buf, sizeof( discard_buf ),
                                        &bytes_read );
            } while ( bytes_read > 0U );

            do
            {
                bytes_read = 0U;
                ( void )EXEC_UART_Read( HW_UART_CHANNEL_2, discard_buf, sizeof( discard_buf ),
                                        &bytes_read );
            } while ( bytes_read > 0U );
        }

        if ( !EXEC_UART_Transmit( sender_ch, ( const uint8_t* )tx_text, tx_length ) )
        {
            CONSOLE_Printf( "TX failed\r\n" );
            return;
        }

        vTaskDelay( pdMS_TO_TICKS( 10U ) );

        {
            uint8_t  rx_buf[EXEC_UART_MAX_CHUNK_SIZE];
            uint32_t bytes_read = 0U;

            if ( !EXEC_UART_Read( receiver_ch, rx_buf, sizeof( rx_buf ), &bytes_read ) )
            {
                CONSOLE_Printf( "RX read failed\r\n" );
                return;
            }

            CONSOLE_Printf( "sent: %s\r\n", tx_text );
            CONSOLE_Printf( "received: " );

            for ( uint32_t i = 0U; i < bytes_read; i++ )
            {
                CONSOLE_Printf( "%c", rx_buf[i] );
            }

            CONSOLE_Printf( "\r\n" );

            if ( ( bytes_read == tx_length ) && ( memcmp( rx_buf, tx_text, tx_length ) == 0 ) )
            {
                CONSOLE_Printf( "PASS\r\n" );
            }
            else
            {
                CONSOLE_Printf( "FAIL\r\n" );
            }
        }
    }
    else
    {
        CONSOLE_Printf( "Unknown uart_loopback command\r\n" );
        CONSOLE_Printf( "Use 'uart_loopback' for usage.\r\n" );
    }
}
/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */

/**
 * @brief Handles the parsed arguments retrieved from the console
 *
 * @param argc - The number of arguments
 * @param argv - pointer to each argument string
 *
 * @returns void
 */
void CONSOLE_Command_Handler( uint16_t argc, char* argv[] )
{
    if ( argc == 0U )
    {
        return;
    }

    for ( size_t command = 0; command < ARRAY_LEN( CONSOLE_COMMANDS ); command++ )
    {
        if ( strcmp( argv[0], CONSOLE_COMMANDS[command].command_name ) == 0 )
        {
            CONSOLE_COMMANDS[command].command_handler( argc, argv );
            if ( strcmp( argv[0], "clear" ) != 0 )
            {
                CONSOLE_Printf( "\r\n" );
            }
            return;
        }
    }

    // unknown command
    CONSOLE_Printf( "Unknown command: " );
    CONSOLE_Printf( "%s", argv[0] );
    CONSOLE_Printf( "\r\n" );
}
