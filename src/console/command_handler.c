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
#include "execution_manager.h"
#include "execution_mid_level/exec_i2c/exec_i2c.h"
#include "hw_gpio.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */

#define CONSOLE_I2C_LOOPBACK_MAX_PAYLOAD_BYTES 64U
#define CONSOLE_I2C_LOOPBACK_TIMEOUT_MS        300U
#define CONSOLE_I2C_LOOPBACK_SLAVE_ADDRESS     0x52U

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
static bool CONSOLE_I2C_Parse_Speed( const char* token, ExecI2cSpeed_T* out_speed );
static bool CONSOLE_I2C_Parse_Transfer_Mode( const char* token,
                                             ExecI2cTransferMode_T* out_transfer_mode );
static bool CONSOLE_I2C_Parse_Byte( const char* token, uint8_t* out_byte );
static void CONSOLE_I2C_Print_Loopback_Usage( void );

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
    {"i2c_loopback", CONSOLE_Command_I2C_Loopback,
     "Run CH1(master TX)->CH2(slave RX) test. Usage: i2c_loopback run <100|400> <irq|dma> <irq|dma> <byte0> [byte1 ...]"}

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

static bool CONSOLE_I2C_Parse_Speed( const char* token, ExecI2cSpeed_T* out_speed )
{
    if ( token == NULL || out_speed == NULL )
    {
        return false;
    }

    if ( strcmp( token, "100" ) == 0 || strcmp( token, "100k" ) == 0
         || strcmp( token, "100khz" ) == 0 )
    {
        *out_speed = EXEC_I2C_SPEED_100KHZ;
        return true;
    }

    if ( strcmp( token, "400" ) == 0 || strcmp( token, "400k" ) == 0
         || strcmp( token, "400khz" ) == 0 )
    {
        *out_speed = EXEC_I2C_SPEED_400KHZ;
        return true;
    }

    return false;
}

static bool CONSOLE_I2C_Parse_Transfer_Mode( const char* token,
                                             ExecI2cTransferMode_T* out_transfer_mode )
{
    if ( token == NULL || out_transfer_mode == NULL )
    {
        return false;
    }

    if ( strcmp( token, "irq" ) == 0 || strcmp( token, "int" ) == 0
         || strcmp( token, "interrupt" ) == 0 )
    {
        *out_transfer_mode = EXEC_I2C_TRANSFER_INTERRUPT;
        return true;
    }

    if ( strcmp( token, "dma" ) == 0 )
    {
        *out_transfer_mode = EXEC_I2C_TRANSFER_DMA;
        return true;
    }

    return false;
}

static bool CONSOLE_I2C_Parse_Byte( const char* token, uint8_t* out_byte )
{
    if ( token == NULL || out_byte == NULL || token[0] == '\0' )
    {
        return false;
    }

    char* end_ptr      = NULL;
    unsigned long byte = strtoul( token, &end_ptr, 0 );

    if ( end_ptr == token || *end_ptr != '\0' || byte > 0xFFUL )
    {
        return false;
    }

    *out_byte = (uint8_t)byte;
    return true;
}

static void CONSOLE_I2C_Print_Loopback_Usage( void )
{
    CONSOLE_Printf( "Usage:\r\n" );
    CONSOLE_Printf( "  i2c_loopback run <100|400> <irq|dma> <irq|dma> <byte0> [byte1 ...]\r\n" );
    CONSOLE_Printf( "Example:\r\n" );
    CONSOLE_Printf( "  i2c_loopback run 100 irq dma 0x11 0x22 0x33\r\n" );
    CONSOLE_Printf( "Wiring:\r\n" );
    CONSOLE_Printf( "  Connect CH1 SCL<->CH2 SCL, CH1 SDA<->CH2 SDA and common GND.\r\n" );
}

static void CONSOLE_Command_I2C_Loopback( uint16_t argc, char* argv[] )
{
    if ( argc < 6U || argv[1] == NULL || argv[2] == NULL || argv[3] == NULL || argv[4] == NULL )
    {
        CONSOLE_I2C_Print_Loopback_Usage();
        return;
    }

    if ( strcmp( argv[1], "run" ) != 0 )
    {
        CONSOLE_I2C_Print_Loopback_Usage();
        return;
    }

    ExecI2cSpeed_T speed;
    ExecI2cTransferMode_T master_transfer_mode;
    ExecI2cTransferMode_T slave_transfer_mode;

    if ( !CONSOLE_I2C_Parse_Speed( argv[2], &speed ) )
    {
        CONSOLE_Printf( "Invalid speed '%s'. Use 100 or 400.\r\n", argv[2] );
        return;
    }

    if ( !CONSOLE_I2C_Parse_Transfer_Mode( argv[3], &master_transfer_mode ) )
    {
        CONSOLE_Printf( "Invalid master mode '%s'. Use irq or dma.\r\n", argv[3] );
        return;
    }

    if ( !CONSOLE_I2C_Parse_Transfer_Mode( argv[4], &slave_transfer_mode ) )
    {
        CONSOLE_Printf( "Invalid slave mode '%s'. Use irq or dma.\r\n", argv[4] );
        return;
    }

    const uint16_t payload_len = (uint16_t)( argc - 5U );
    if ( payload_len == 0U || payload_len > CONSOLE_I2C_LOOPBACK_MAX_PAYLOAD_BYTES )
    {
        CONSOLE_Printf( "Payload must contain 1..%u bytes.\r\n", CONSOLE_I2C_LOOPBACK_MAX_PAYLOAD_BYTES );
        return;
    }

    uint8_t tx_payload[CONSOLE_I2C_LOOPBACK_MAX_PAYLOAD_BYTES] = { 0U };
    uint8_t rx_payload[CONSOLE_I2C_LOOPBACK_MAX_PAYLOAD_BYTES] = { 0U };

    for ( uint16_t i = 0U; i < payload_len; i++ )
    {
        if ( !CONSOLE_I2C_Parse_Byte( argv[i + 5U], &tx_payload[i] ) )
        {
            CONSOLE_Printf( "Invalid byte token '%s'. Use decimal or 0xHH format.\r\n", argv[i + 5U] );
            return;
        }
    }

    const ExecI2cChannelConfig_T channel_1_config = {
        .mode           = EXEC_I2C_MODE_MASTER,
        .speed          = speed,
        .transfer_mode  = master_transfer_mode,
        .own_address_7bit = 0U,
        .rx_enabled     = false,
        .tx_enabled     = true,
    };

    const ExecI2cChannelConfig_T channel_2_config = {
        .mode           = EXEC_I2C_MODE_SLAVE,
        .speed          = speed,
        .transfer_mode  = slave_transfer_mode,
        .own_address_7bit = CONSOLE_I2C_LOOPBACK_SLAVE_ADDRESS,
        .rx_enabled     = true,
        .tx_enabled     = false,
    };

    if ( !EXEC_I2C_Configuration( &channel_1_config, &channel_2_config ) )
    {
        CONSOLE_Printf( "I2C config failed.\r\n" );
        return;
    }

    if ( !EXEC_I2C_Rx_Start( EXEC_I2C_EXTERNAL_CHANNEL_2, 0U, payload_len ) )
    {
        CONSOLE_Printf( "I2C slave RX start failed.\r\n" );
        return;
    }

    vTaskDelay( pdMS_TO_TICKS( 2U ) );

    if ( !EXEC_I2C_Send( EXEC_I2C_EXTERNAL_CHANNEL_1, CONSOLE_I2C_LOOPBACK_SLAVE_ADDRESS,
                         tx_payload, payload_len ) )
    {
        CONSOLE_Printf( "I2C master send failed.\r\n" );
        return;
    }

    uint16_t total_rx = 0U;
    for ( uint32_t elapsed_ms = 0U; elapsed_ms < CONSOLE_I2C_LOOPBACK_TIMEOUT_MS
                                  && total_rx < payload_len;
          elapsed_ms++ )
    {
        total_rx += EXEC_I2C_Rx_Copy_And_Consume( EXEC_I2C_EXTERNAL_CHANNEL_2,
                                                  &rx_payload[total_rx],
                                                  (uint16_t)( payload_len - total_rx ) );

        if ( total_rx < payload_len )
        {
            vTaskDelay( pdMS_TO_TICKS( 1U ) );
        }
    }

    CONSOLE_Printf( "I2C loopback result: tx=%u rx=%u\r\n", payload_len, total_rx );

    if ( total_rx != payload_len || memcmp( tx_payload, rx_payload, payload_len ) != 0 )
    {
        CONSOLE_Printf( "Loopback FAILED\r\n" );
    }
    else
    {
        CONSOLE_Printf( "Loopback PASSED\r\n" );
    }

    CONSOLE_Printf( "TX:" );
    for ( uint16_t i = 0U; i < payload_len; i++ )
    {
        CONSOLE_Printf( " %02X", tx_payload[i] );
    }
    CONSOLE_Printf( "\r\n" );

    CONSOLE_Printf( "RX:" );
    for ( uint16_t i = 0U; i < total_rx; i++ )
    {
        CONSOLE_Printf( " %02X", rx_payload[i] );
    }
    CONSOLE_Printf( "\r\n" );
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
