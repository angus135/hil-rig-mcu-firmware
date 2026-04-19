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
#include "execution_mid_level/exec_i2c/exec_i2c.h"
#include "execution_manager.h"
#include "hw_gpio.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

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
static void CONSOLE_Command_I2C_Loopback( uint16_t argc, char* argv[] );
static bool CONSOLE_Parse_I2C_Master_And_Slave( const char* arg,
                                                 EXECI2CExternalChannel_T* master_channel,
                                                 EXECI2CExternalChannel_T* slave_channel );
static bool CONSOLE_Parse_I2C_Speed( const char* arg, EXECI2CSpeed_T* speed );
static bool CONSOLE_Parse_I2C_Transfer_Path( const char* arg, EXECI2CTransferPath_T* transfer_path );
static bool CONSOLE_Build_I2C_Message( uint16_t argc, char* argv[], char* out_message,
                                       size_t out_message_size, uint16_t* out_message_length );
static bool CONSOLE_Run_I2C_Loopback_Transfer( EXECI2CExternalChannel_T master_channel,
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
    {"i2c_loopback", CONSOLE_Command_I2C_Loopback, "Loopback test. Usage: i2c_loopback <master:1|2> <speed:100|400> <op:interrupt|dma> <message...>"}

};

// clang-format on

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

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

    for ( uint16_t arg_idx = 4U; arg_idx < argc; ++arg_idx )
    {
        const size_t part_len = strlen( argv[arg_idx] );
        if ( tx_len + part_len + 1U >= out_message_size )
        {
            return false;
        }

        if ( arg_idx > 4U )
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

static bool CONSOLE_Run_I2C_Loopback_Transfer( EXECI2CExternalChannel_T master_channel,
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

static void CONSOLE_Command_I2C_Loopback( uint16_t argc, char* argv[] )
{
    if ( argc < 5U )
    {
        CONSOLE_Printf( "Usage: i2c_loopback <master:1|2> <speed:100|400> <op:interrupt|dma> <message...>\r\n" );
        return;
    }

    EXECI2CExternalChannel_T master_channel = EXEC_I2C_EXTERNAL_1;
    EXECI2CExternalChannel_T slave_channel  = EXEC_I2C_EXTERNAL_2;
    if ( !CONSOLE_Parse_I2C_Master_And_Slave( argv[1], &master_channel, &slave_channel ) )
    {
        CONSOLE_Printf( "Invalid master channel. Use 1 or 2.\r\n" );
        return;
    }

    EXECI2CSpeed_T speed = EXEC_I2C_SPEED_100KHZ;
    if ( !CONSOLE_Parse_I2C_Speed( argv[2], &speed ) )
    {
        CONSOLE_Printf( "Invalid speed. Use 100 or 400.\r\n" );
        return;
    }

    EXECI2CTransferPath_T transfer_path = EXEC_I2C_TRANSFER_INTERRUPT;
    if ( !CONSOLE_Parse_I2C_Transfer_Path( argv[3], &transfer_path ) )
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
        .tx_transfer_path = transfer_path,
        .rx_transfer_path = transfer_path,
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
    if ( !CONSOLE_Run_I2C_Loopback_Transfer( master_channel, slave_channel, slave_addr, tx_message,
                                             tx_len, rx_message, sizeof( rx_message ), &received_len ) )
    {
        return;
    }

    CONSOLE_Printf( "I2C loopback: master=I2C%s slave=I2C%s speed=%s op=%s\r\n",
                    ( master_channel == EXEC_I2C_EXTERNAL_1 ) ? "1" : "2",
                    ( slave_channel == EXEC_I2C_EXTERNAL_1 ) ? "1" : "2",
                    ( speed == EXEC_I2C_SPEED_400KHZ ) ? "400kHz" : "100kHz",
                    ( transfer_path == EXEC_I2C_TRANSFER_DMA ) ? "DMA" : "Interrupt" );

    CONSOLE_Printf( "Sent    (%u): %.*s\r\n", ( unsigned int )tx_len, ( int )tx_len, tx_message );
    CONSOLE_Printf( "Received(%u): %.*s\r\n", ( unsigned int )received_len, ( int )received_len,
                    rx_message );

    const bool pass = ( received_len == ( uint16_t )tx_len ) &&
                      ( memcmp( tx_message, rx_message, tx_len ) == 0 );
    CONSOLE_Printf( "Result: %s\r\n", pass ? "PASS" : "FAIL" );
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
    CONSOLE_Printf( argv[0] );
    CONSOLE_Printf( "\r\n" );
}
