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
#include "hw_adc.h"
#include "hw_can.h"
#include "exec_digital_input.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */
#define NUM_DIGITAL_INPUTS 10

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
static void CONSOLE_Command_Set_Pin( uint16_t argc, char** argv );
static void CONSOLE_Command_Set_Many_Pins( uint16_t argc, char** argv );
static void CONSOLE_Command_Analogue_Inputs( uint16_t argc, char* argv[] );
static void CONSOLE_Command_DigitalInput( uint16_t argc, char* argv[] );
static void CONSOLE_Command_Can_tx( uint16_t argc, char* argv[] );
static void CONSOLE_Command_Can_rx( uint16_t argc, char* argv[] );
static void CONSOLE_Command_Can_config( uint16_t argc, char* argv[] );
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
    {"set_pin", CONSOLE_Command_Set_Pin, "Set or reset digital output, Usage: set_pin PIN_NAME <0|1>"},
    {"set_pins", CONSOLE_Command_Set_Many_Pins, "Set or reset many digital output"},
    {"analogue_inputs", CONSOLE_Command_Analogue_Inputs, "Allows for interaction with Analogue Inputs."},
    {"digital_input", CONSOLE_Command_DigitalInput, "Print digital input states as 1s and 0s."},
    {"can_tx", CONSOLE_Command_Can_tx, "Transmit a 8 byte message Usage: can_tx <1|2> <message1> <message2> ..."},
    {"can_rx", CONSOLE_Command_Can_rx, "Read and print an 8 byte message Usage: can_rx <1|2>"},
    {"can_config", CONSOLE_Command_Can_config, "Configures Can channel 1&2"}
};

// clang-format on

static ConsoleUartLoopbackState_T s_uart_loopback_state = { 0 };

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

static void CONSOLE_Command_DigitalInput( uint16_t argc, char* argv[] )
{
    uint32_t sampled_inputs = 0U;

    DigitalInputChannelConfig_T config = { .channel_0_mode = DIGITAL_INPUT_MODE_3V3,
                                           .channel_1_mode = DIGITAL_INPUT_MODE_3V3,
                                           .channel_2_mode = DIGITAL_INPUT_MODE_3V3,
                                           .channel_3_mode = DIGITAL_INPUT_MODE_3V3,
                                           .channel_4_mode = DIGITAL_INPUT_MODE_3V3,
                                           .channel_5_mode = DIGITAL_INPUT_MODE_3V3,
                                           .channel_6_mode = DIGITAL_INPUT_MODE_3V3,
                                           .channel_7_mode = DIGITAL_INPUT_MODE_3V3,
                                           .channel_8_mode = DIGITAL_INPUT_MODE_3V3,
                                           .channel_9_mode = DIGITAL_INPUT_MODE_3V3 };

    if ( argc != 2 || argv[1] == NULL )
    {
        CONSOLE_Printf( "Usage: digital_input <channel 0-9> or digital_input all\r\n" );
        return;
    }

    EXEC_DigitalInput_Configure( &config );

    if ( strcmp( argv[1], "all" ) == 0 )
    {
        EXEC_DigitalInput_SampleAll( &sampled_inputs );

        uint32_t lower_bits_mask = ( uint32_t )( ( 1UL << NUM_DIGITAL_INPUTS ) - 1UL );
        uint32_t lower_bits      = sampled_inputs & lower_bits_mask;

        CONSOLE_Printf( "Digital Inputs: " );
        for ( int8_t bit = ( int8_t )NUM_DIGITAL_INPUTS - 1; bit >= 0; --bit )
        {
            CONSOLE_Printf( "%d", ( lower_bits >> bit ) & 0x1U );
        }
        CONSOLE_Printf( "\r\n" );
    }
    else
    {
        int channel = atoi( argv[1] );
        if ( channel < 0 || channel >= NUM_DIGITAL_INPUTS )
        {
            CONSOLE_Printf( "Invalid channel. Must be 0-9.\r\n" );
            return;
        }

        bool state = HW_GPIO_Read_Pin( ( GPIOInput_T )channel );
        CONSOLE_Printf( "Digital Input %d: %d\r\n", channel, state ? 1 : 0 );
    }
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
        CONSOLE_Printf( "%s\t\t\t- %s\r\n", CONSOLE_COMMANDS[command].command_name,
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

/**
 * @brief Sets or resets a single digital pin
 *
 * @param argc - The number of arguments
 * @param argv - pointer to each argument string
 *
 * @returns void
 */
static void CONSOLE_Command_Set_Pin( uint16_t argc, char* argv[] )
{
    if ( argc != 3 )
    {
        CONSOLE_Printf( "Incorrect number of inputs, expected 2 but recieved %d", argc - 1 );
        return;
    }
    GPIOOutput_T pin;
    bool         check = HW_GPIO_StringToEnum( argv[1], &pin );
    if ( !check )
    {
        CONSOLE_Printf( "Unrecognised pin name: %s", argv[1] );
        return;
    }
    if ( argv[2][0] == '0' )
    {
        HW_GPIO_Reset_Single_Pin( pin );
        return;
    }
    else if ( argv[2][0] == '1' )
    {
        HW_GPIO_Set_Single_Pin( pin );
        return;
    }
    else
    {
        CONSOLE_Printf( "Unrecognised input, expected 1 or 0 but recieved %c", argv[2][0] );
        return;
    }
}

/**
 * @brief Sets or resets many digital pins
 *
 * @param argc - The number of arguments
 * @param argv - pointer to each argument string
 *
 * @returns void
 */
static void CONSOLE_Command_Set_Many_Pins( uint16_t argc, char* argv[] )
{
    int arg_limit = 10;
    if ( ( argc < 3 ) | ( argc > ( arg_limit + 1 ) ) )
    {
        CONSOLE_Printf( "Incorrect number of inputs, expected >2 and <%dbut recieved %d", arg_limit,
                        argc );
        CONSOLE_Printf( "Usage: set_pin PIN_NAME0 PIN_NAME1 ... PIN_NAMEX <0|1>" );
        return;
    }

    GPIOOutput_T pins[arg_limit];
    for ( int i = 0; i < argc - 2; i++ )
    {
        bool check = HW_GPIO_StringToEnum( argv[i + 1], &( pins[i] ) );
        if ( !check )
        {
            CONSOLE_Printf( "Unrecognised pin name: %s", argv[i + 1] );
            return;
        }
    }
    if ( argv[argc - 1][0] == '0' )
    {
        HW_GPIO_Reset_Many_Pins( pins, argc - 2 );
        return;
    }
    if ( argv[argc - 1][0] == '1' )
    {
        HW_GPIO_Set_Many_Pins( pins, argc - 2 );
        return;
    }
    CONSOLE_Printf( "Unrecognised input, expected 1 or 0 but recieved %c", argv[argc - 1] );
}

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
    char out[8];
    for ( int i = 0; i < 8; i++ )
    {
        out[i] = '0';
    }
    if ( strcmp( argv[1], "1" ) == 0 )
    {
        if ( HW_CAN_Rx_Buffer_Pop1( out ) != 0 )
        {
            CONSOLE_Printf( "Nothing in channel 1 buffer\n\r" );
            return;
        }
    }
    else if ( strcmp( argv[1], "2" ) == 0 )
    {
        if ( HW_CAN_Rx_Buffer_Pop2( out ) != 0 )
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
    CONSOLE_Printf( "Recieved: %s", out );
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

static void CONSOLE_Command_Analogue_Inputs( uint16_t argc, char* argv[] )
{
    if ( argc < 2 || argv[1] == NULL )
    {
        CONSOLE_Printf( "Usage:\r\n" );
        CONSOLE_Printf( "  analogue_inputs start\r\n" );
        CONSOLE_Printf( "  analogue_inputs stop\r\n" );
        CONSOLE_Printf( "  analogue_inputs read\r\n" );
        CONSOLE_Printf( "  analogue_inputs frequency\r\n" );
        return;
    }

    if ( strcmp( argv[1], "start" ) == 0 )
    {
        HW_ADC_Start_DMA_Measurements();
        CONSOLE_Printf( "Analogue Inputs are now being read into DMA\r\n" );
    }
    else if ( strcmp( argv[1], "stop" ) == 0 )
    {
        HW_ADC_Stop_DMA_Measurements();
        CONSOLE_Printf( "Analogue Inputs are no longer being read into DMA\r\n" );
    }
    else if ( strcmp( argv[1], "read" ) == 0 )
    {
        ADCMeasurement_T measurement;
        HW_ADC_Read_DMA_Measurements( &measurement, 1 );
        CONSOLE_Printf( "DMA Input 0: %u\r\n", measurement.ch_0 );
        CONSOLE_Printf( "DMA Input 1: %u\r\n", measurement.ch_1 );
        uint16_t value = HW_ADC_Read_Polled_Measurement( ADC_SOURCE_VIN );
        CONSOLE_Printf( "Vin: %u\r\n", value );
    }
    else if ( strcmp( argv[1], "frequency" ) == 0 )
    {
        if ( argc < 3 || argv[2] == NULL )
        {
            CONSOLE_Printf( "Usage:\r\n" );
            CONSOLE_Printf( "  analogue_inputs frequency <desired frequency>\r\n" );
            CONSOLE_Printf( "    Note: Desired frequencies can only be one of the following:\r\n" );
            CONSOLE_Printf(
                "\t- 100kHz\r\n\t- 50kHz\r\n\t- 10kHz\r\n\t- 5kHz\r\n\t- 1kHz\r\n\t- 500Hz\r\n" );
        }
        else if ( strcmp( argv[2], "100kHz" ) == 0 || strcmp( argv[2], "100k" ) == 0 )
        {
            HW_ADC_Configure_ADC_Measurement_Frequency( ADC_SAMPLE_RATE_100K_HZ );
        }
        else if ( strcmp( argv[2], "50kHz" ) == 0 || strcmp( argv[2], "50k" ) == 0 )
        {
            HW_ADC_Configure_ADC_Measurement_Frequency( ADC_SAMPLE_RATE_50K_HZ );
        }
        else if ( strcmp( argv[2], "10kHz" ) == 0 || strcmp( argv[2], "10k" ) == 0 )
        {
            HW_ADC_Configure_ADC_Measurement_Frequency( ADC_SAMPLE_RATE_10K_HZ );
        }
        else if ( strcmp( argv[2], "5kHz" ) == 0 || strcmp( argv[2], "5k" ) == 0 )
        {
            HW_ADC_Configure_ADC_Measurement_Frequency( ADC_SAMPLE_RATE_5K_HZ );
        }
        else if ( strcmp( argv[2], "1kHz" ) == 0 || strcmp( argv[2], "1k" ) == 0 )
        {
            HW_ADC_Configure_ADC_Measurement_Frequency( ADC_SAMPLE_RATE_1K_HZ );
        }
        else if ( strcmp( argv[2], "500Hz" ) == 0 || strcmp( argv[2], "500" ) == 0 )
        {
            HW_ADC_Configure_ADC_Measurement_Frequency( ADC_SAMPLE_RATE_500_HZ );
        }
        else
        {
            CONSOLE_Printf( "Usage:\r\n" );
            CONSOLE_Printf( "  analogue_inputs frequency <desired frequency>\r\n" );
            CONSOLE_Printf( "    Note: Desired frequencies can only be one of the following:\r\n" );
            CONSOLE_Printf(
                "\t- 100kHz\r\n\t- 50kHz\r\n\t- 10kHz\r\n\t- 5kHz\r\n\t- 1kHz\r\n\t- 500Hz\r\n" );
        }
    }
    else
    {
        CONSOLE_Printf( "Invalid argument: %s\r\n", argv[1] );
        CONSOLE_Printf( "Usage:\r\n" );
        CONSOLE_Printf( "  analogue_inputs start\r\n" );
        CONSOLE_Printf( "  analogue_inputs stop\r\n" );
        CONSOLE_Printf( "  analogue_inputs read\r\n" );
        CONSOLE_Printf( "  analogue_inputs frequency\r\n" );
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
