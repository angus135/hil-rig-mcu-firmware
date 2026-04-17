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
#include "hw_gpio.h"
#include "exec_uart.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

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
    {"uart_loopback", CONSOLE_Command_UART_Loopback, "Configuring Channels and Rx/Tx loopback testing for Uart"}

};

// clang-format on

static ConsoleUartLoopbackState_T s_uart_loopback_state = { 0 };

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
 * @brief Handles the SPI loopback command
 *
 * @param argc - The number of arguments
 * @param argv - pointer to each argument string
 *
 * @returns void
 */
static void CONSOLE_Command_SPI_Loopback( uint16_t argc, char* argv[] )
{
    if ( argc < 2 || argv[1] == NULL )
    {
        CONSOLE_Printf( "Usage:\r\n" );
        CONSOLE_Printf( "  spi_loop run <message>\r\n" );
        CONSOLE_Printf( "  spi_loop config <channel> <master|slave>\r\n" );
        return;
    }

    if ( strcmp( argv[1], "run" ) == 0 )
    {
        if ( argc < 3 || argv[2] == NULL )
        {
            CONSOLE_Printf( "Usage: spi_loop run <message>\r\n" );
            return;
        }

        HW_SPI_Load_Tx_Buffer( SPI_CHANNEL_0, ( const uint8_t* )argv[2], strlen( argv[2] ) );
        HW_SPI_Tx_Trigger( SPI_CHANNEL_0 );

        CONSOLE_Printf( "SPI loopback run started with message: %s\r\n", argv[2] );
        vTaskDelay( 100 );
        HWSPIRxSpans_T message = HW_SPI_Rx_Peek( SPI_CHANNEL_1 );
        HW_SPI_Rx_Consume( SPI_CHANNEL_1, message.total_length_bytes );

        CONSOLE_Printf( "Received (%lu bytes): ", message.total_length_bytes );

        /* First span */
        if ( message.first_span.length_bytes > 0U )
        {
            CONSOLE_Printf( "%.*s", ( int )message.first_span.length_bytes,
                            ( const char* )message.first_span.data );
        }

        /* Second span (only valid if wrapped) */
        if ( message.second_span.length_bytes > 0U )
        {
            CONSOLE_Printf( "%.*s", ( int )message.second_span.length_bytes,
                            ( const char* )message.second_span.data );
        }

        CONSOLE_Printf( "\r\n" );
    }
    else if ( strcmp( argv[1], "config" ) == 0 )
    {
        if ( argc < 4 || argv[2] == NULL || argv[3] == NULL )
        {
            CONSOLE_Printf( "Usage: spi_loop config <channel> <master|slave>\r\n" );
            return;
        }

        SPIPeripheral_T peripheral = SPI_CHANNEL_0;
        HWSPIConfig_T   configuration;

        if ( strcmp( argv[2], "0" ) == 0 )
        {
            peripheral = SPI_CHANNEL_0;
        }
        else
        {
            peripheral = SPI_CHANNEL_1;
        }

        if ( strcmp( argv[3], "master" ) == 0 )
        {
            configuration.spi_mode = SPI_MASTER_MODE;
        }
        else if ( strcmp( argv[3], "slave" ) == 0 )
        {
            configuration.spi_mode = SPI_SLAVE_MODE;
        }
        else
        {
            CONSOLE_Printf( "Invalid mode: %s\r\nUse master or slave\r\n", argv[3] );
            return;
        }

        configuration.data_size = SPI_SIZE_8_BIT;
        configuration.first_bit = SPI_FIRST_MSB;
        configuration.baud_rate = SPI_BAUD_352KBIT;
        configuration.cpol      = SPI_CPOL_LOW;
        configuration.cpha      = SPI_CPHA_1_EDGE;

        if ( HW_SPI_Configure_Channel( peripheral, configuration ) == false )
        {
            CONSOLE_Printf( "Failed to configure SPI channel %s\r\n", argv[2] );
            return;
        }

        HW_SPI_Start_Channel( peripheral );

        CONSOLE_Printf( "Configured SPI channel %s as %s\r\n", argv[2], argv[3] );
    }
    else
    {
        CONSOLE_Printf( "Unknown action: %s\r\n", argv[1] );
        CONSOLE_Printf( "Usage:\r\n" );
        CONSOLE_Printf( "  spi_loop run <message>\r\n" );
        CONSOLE_Printf( "  spi_loop config <channel> <master|slave>\r\n" );
    }
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
