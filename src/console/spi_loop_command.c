/******************************************************************************
 *  File:       <filename>.c
 *  Author:     <your name>
 *  Created:    <DD-MMM-YYYY>
 *
 *  Description:
 *      <Short description of the module's purpose and responsibilities>
 *
 *  Notes:
 *      <Any design notes, dependencies, or assumptions go here>
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include "exec_spi.h"
#include "hw_spi.h"
#include "console.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
// Add other required includes here

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */

#define SPI_LOOP_MAX_MESSAGE_BYTES 128U
#define SPI_LOOP_TRANSFER_DELAY_MS 100U

/**-----------------------------------------------------------------------------
 *  Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

typedef struct SPILoopChannel_T
{
    SPIPeripheral_T peripheral;
    HWSPIConfig_T   configuration;
    bool            configured;
} SPILoopChannel_T;

typedef struct SPILoopState_T
{
    SPILoopChannel_T channel_0;
    SPILoopChannel_T channel_1;

    uint8_t master_tx[SPI_LOOP_MAX_MESSAGE_BYTES];
    uint8_t slave_tx[SPI_LOOP_MAX_MESSAGE_BYTES];
    uint8_t master_rx[SPI_LOOP_MAX_MESSAGE_BYTES];
    uint8_t slave_rx[SPI_LOOP_MAX_MESSAGE_BYTES];

    uint32_t master_tx_size_bytes;
    uint32_t slave_tx_size_bytes;
    uint32_t master_rx_size_bytes;
    uint32_t slave_rx_size_bytes;

    bool master_tx_loaded;
    bool slave_tx_loaded;
} SPILoopState_T;

/**-----------------------------------------------------------------------------
 *  Public (global) and Extern Variables
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Private (static) Variables
 *------------------------------------------------------------------------------
 */

static SPILoopState_T spi_loop_state = {
    .channel_0 =
        {
            .peripheral = SPI_CHANNEL_0,
            .configuration =
                {
                    .spi_mode  = SPI_MASTER_MODE,
                    .data_size = SPI_SIZE_8_BIT,
                    .first_bit = SPI_FIRST_MSB,
                    .baud_rate = SPI_BAUD_352KBIT,
                    .cpol      = SPI_CPOL_LOW,
                    .cpha      = SPI_CPHA_1_EDGE,
                },
            .configured = false,
        },

    .channel_1 =
        {
            .peripheral = SPI_CHANNEL_1,
            .configuration =
                {
                    .spi_mode  = SPI_SLAVE_MODE,
                    .data_size = SPI_SIZE_8_BIT,
                    .first_bit = SPI_FIRST_MSB,
                    .baud_rate = SPI_BAUD_352KBIT,
                    .cpol      = SPI_CPOL_LOW,
                    .cpha      = SPI_CPHA_1_EDGE,
                },
            .configured = false,
        },

    .master_tx_size_bytes = 0U,
    .slave_tx_size_bytes  = 0U,
    .master_rx_size_bytes = 0U,
    .slave_rx_size_bytes  = 0U,
    .master_tx_loaded     = false,
    .slave_tx_loaded      = false,
};

/**-----------------------------------------------------------------------------
 *  Private (static) Function Prototypes
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

static SPILoopChannel_T* CONSOLE_SPI_Loopback_Get_Channel( const char* channel_str )
{
    if ( strcmp( channel_str, "0" ) == 0 )
    {
        return &spi_loop_state.channel_0;
    }

    if ( strcmp( channel_str, "1" ) == 0 )
    {
        return &spi_loop_state.channel_1;
    }

    return NULL;
}

static SPILoopChannel_T* CONSOLE_SPI_Loopback_Get_Master_Channel( void )
{
    if ( spi_loop_state.channel_0.configuration.spi_mode == SPI_MASTER_MODE )
    {
        return &spi_loop_state.channel_0;
    }

    if ( spi_loop_state.channel_1.configuration.spi_mode == SPI_MASTER_MODE )
    {
        return &spi_loop_state.channel_1;
    }

    return NULL;
}

static SPILoopChannel_T* CONSOLE_SPI_Loopback_Get_Slave_Channel( void )
{
    if ( spi_loop_state.channel_0.configuration.spi_mode == SPI_SLAVE_MODE )
    {
        return &spi_loop_state.channel_0;
    }

    if ( spi_loop_state.channel_1.configuration.spi_mode == SPI_SLAVE_MODE )
    {
        return &spi_loop_state.channel_1;
    }

    return NULL;
}

static bool CONSOLE_SPI_Loopback_Parse_Mode( const char* text, SPIMode_T* mode )
{
    if ( strcmp( text, "master" ) == 0 )
    {
        *mode = SPI_MASTER_MODE;
        return true;
    }

    if ( strcmp( text, "slave" ) == 0 )
    {
        *mode = SPI_SLAVE_MODE;
        return true;
    }

    return false;
}

static bool CONSOLE_SPI_Loopback_Parse_Data_Size( const char* text, SPIDataSize_T* data_size )
{
    if ( strcmp( text, "8" ) == 0 )
    {
        *data_size = SPI_SIZE_8_BIT;
        return true;
    }

    if ( strcmp( text, "16" ) == 0 )
    {
        *data_size = SPI_SIZE_16_BIT;
        return true;
    }

    return false;
}

static bool CONSOLE_SPI_Loopback_Parse_First_Bit( const char* text, SPIFirstBit_T* first_bit )
{
    if ( strcmp( text, "msb" ) == 0 )
    {
        *first_bit = SPI_FIRST_MSB;
        return true;
    }

    if ( strcmp( text, "lsb" ) == 0 )
    {
        *first_bit = SPI_FIRST_LSB;
        return true;
    }

    return false;
}

static bool CONSOLE_SPI_Loopback_Parse_Baud( const char* text, SPIBaudRate_T* baud_rate )
{
    if ( strcmp( text, "45m" ) == 0 )
    {
        *baud_rate = SPI_BAUD_45MBIT;
        return true;
    }

    if ( strcmp( text, "22m5" ) == 0 )
    {
        *baud_rate = SPI_BAUD_22M5BIT;
        return true;
    }

    if ( strcmp( text, "11m25" ) == 0 )
    {
        *baud_rate = SPI_BAUD_11M25BIT;
        return true;
    }

    if ( strcmp( text, "5m625" ) == 0 )
    {
        *baud_rate = SPI_BAUD_5M625BIT;
        return true;
    }

    if ( strcmp( text, "2m813" ) == 0 )
    {
        *baud_rate = SPI_BAUD_2M813BIT;
        return true;
    }

    if ( strcmp( text, "1m406" ) == 0 )
    {
        *baud_rate = SPI_BAUD_1M406BIT;
        return true;
    }

    if ( strcmp( text, "703k" ) == 0 )
    {
        *baud_rate = SPI_BAUD_703KBIT;
        return true;
    }

    if ( strcmp( text, "352k" ) == 0 )
    {
        *baud_rate = SPI_BAUD_352KBIT;
        return true;
    }

    return false;
}

static bool CONSOLE_SPI_Loopback_Parse_CPOL( const char* text, SPICPOL_T* cpol )
{
    if ( strcmp( text, "low" ) == 0 )
    {
        *cpol = SPI_CPOL_LOW;
        return true;
    }

    if ( strcmp( text, "high" ) == 0 )
    {
        *cpol = SPI_CPOL_HIGH;
        return true;
    }

    return false;
}

static bool CONSOLE_SPI_Loopback_Parse_CPHA( const char* text, SPICPHA_T* cpha )
{
    if ( strcmp( text, "1edge" ) == 0 )
    {
        *cpha = SPI_CPHA_1_EDGE;
        return true;
    }

    if ( strcmp( text, "2edge" ) == 0 )
    {
        *cpha = SPI_CPHA_2_EDGE;
        return true;
    }

    return false;
}

static const char* CONSOLE_SPI_Loopback_Mode_To_String( SPIMode_T mode )
{
    return ( mode == SPI_MASTER_MODE ) ? "master" : "slave";
}

static const char* CONSOLE_SPI_Loopback_Data_Size_To_String( SPIDataSize_T data_size )
{
    return ( data_size == SPI_SIZE_8_BIT ) ? "8" : "16";
}

static const char* CONSOLE_SPI_Loopback_First_Bit_To_String( SPIFirstBit_T first_bit )
{
    return ( first_bit == SPI_FIRST_MSB ) ? "msb" : "lsb";
}

static const char* CONSOLE_SPI_Loopback_CPOL_To_String( SPICPOL_T cpol )
{
    return ( cpol == SPI_CPOL_LOW ) ? "low" : "high";
}

static const char* CONSOLE_SPI_Loopback_CPHA_To_String( SPICPHA_T cpha )
{
    return ( cpha == SPI_CPHA_1_EDGE ) ? "1edge" : "2edge";
}

static const char* CONSOLE_SPI_Loopback_Baud_To_String( SPIBaudRate_T baud_rate )
{
    switch ( baud_rate )
    {
        case SPI_BAUD_45MBIT:
            return "45m";

        case SPI_BAUD_22M5BIT:
            return "22m5";

        case SPI_BAUD_11M25BIT:
            return "11m25";

        case SPI_BAUD_5M625BIT:
            return "5m625";

        case SPI_BAUD_2M813BIT:
            return "2m813";

        case SPI_BAUD_1M406BIT:
            return "1m406";

        case SPI_BAUD_703KBIT:
            return "703k";

        case SPI_BAUD_352KBIT:
            return "352k";

        default:
            return "unknown";
    }
}

static void CONSOLE_SPI_Loopback_Print_Buffer( const char* label, const uint8_t* data,
                                               uint32_t size_bytes )
{
    CONSOLE_Printf( "%s (%lu bytes): ", label, size_bytes );

    if ( size_bytes == 0U )
    {
        CONSOLE_Printf( "<empty>\r\n" );
        return;
    }

    CONSOLE_Printf( "%.*s\r\n", ( int )size_bytes, ( const char* )data );
}

static void CONSOLE_SPI_Loopback_Print_Channel_Status( const char*             name,
                                                       const SPILoopChannel_T* channel )
{
    CONSOLE_Printf( "%s:\r\n", name );
    CONSOLE_Printf( "  configured: %s\r\n", channel->configured ? "yes" : "no" );
    CONSOLE_Printf( "  mode:       %s\r\n",
                    CONSOLE_SPI_Loopback_Mode_To_String( channel->configuration.spi_mode ) );
    CONSOLE_Printf( "  datasize:   %s\r\n",
                    CONSOLE_SPI_Loopback_Data_Size_To_String( channel->configuration.data_size ) );
    CONSOLE_Printf( "  firstbit:   %s\r\n",
                    CONSOLE_SPI_Loopback_First_Bit_To_String( channel->configuration.first_bit ) );
    CONSOLE_Printf( "  baud:       %s\r\n",
                    CONSOLE_SPI_Loopback_Baud_To_String( channel->configuration.baud_rate ) );
    CONSOLE_Printf( "  cpol:       %s\r\n",
                    CONSOLE_SPI_Loopback_CPOL_To_String( channel->configuration.cpol ) );
    CONSOLE_Printf( "  cpha:       %s\r\n",
                    CONSOLE_SPI_Loopback_CPHA_To_String( channel->configuration.cpha ) );
}

static uint32_t CONSOLE_SPI_Loopback_Copy_Rx( SPIPeripheral_T peripheral, uint8_t* data_dst,
                                              uint32_t data_dst_size_bytes )
{
    uint32_t copied_bytes = 0U;

    HWSPIRxSpans_T message = HW_SPI_Rx_Peek( peripheral );

    if ( message.total_length_bytes > data_dst_size_bytes )
    {
        // For the console command, truncate the displayed capture rather than
        // overflowing the debug buffer.
        message.total_length_bytes = data_dst_size_bytes;
    }

    if ( message.first_span.length_bytes > message.total_length_bytes )
    {
        message.first_span.length_bytes = message.total_length_bytes;
    }

    if ( message.first_span.length_bytes > 0U )
    {
        memcpy( data_dst, message.first_span.data, message.first_span.length_bytes );
        copied_bytes += message.first_span.length_bytes;
    }

    if ( copied_bytes < message.total_length_bytes )
    {
        uint32_t second_copy_size = message.total_length_bytes - copied_bytes;

        if ( second_copy_size > message.second_span.length_bytes )
        {
            second_copy_size = message.second_span.length_bytes;
        }

        memcpy( &data_dst[copied_bytes], message.second_span.data, second_copy_size );
        copied_bytes += second_copy_size;
    }

    HW_SPI_Rx_Consume( peripheral, copied_bytes );

    return copied_bytes;
}

static bool CONSOLE_SPI_Loopback_Apply_Channel( SPILoopChannel_T* channel )
{
    if ( HW_SPI_Configure_Channel( channel->peripheral, channel->configuration ) == false )
    {
        return false;
    }

    HW_SPI_Start_Channel( channel->peripheral );

    channel->configured = true;

    return true;
}

/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */
void CONSOLE_SPI_Loopback_Print_Usage( void )
{
    CONSOLE_Printf( "Usage:\r\n" );
    CONSOLE_Printf( "  spi_loop help\r\n" );
    CONSOLE_Printf( "\r\n" );

    CONSOLE_Printf( "Configuration:\r\n" );
    CONSOLE_Printf( "  spi_loop config <0|1> mode <master|slave>\r\n" );
    CONSOLE_Printf( "  spi_loop config <0|1> datasize <8|16>\r\n" );
    CONSOLE_Printf( "  spi_loop config <0|1> firstbit <msb|lsb>\r\n" );
    CONSOLE_Printf(
        "  spi_loop config <0|1> baud <45m|22m5|11m25|5m625|2m813|1m406|703k|352k>\r\n" );
    CONSOLE_Printf( "  spi_loop config <0|1> cpol <low|high>\r\n" );
    CONSOLE_Printf( "  spi_loop config <0|1> cpha <1edge|2edge>\r\n" );
    CONSOLE_Printf( "  spi_loop apply <0|1>\r\n" );
    CONSOLE_Printf( "  spi_loop apply all\r\n" );
    CONSOLE_Printf( "\r\n" );

    CONSOLE_Printf( "Messages:\r\n" );
    CONSOLE_Printf( "  spi_loop load master <message>\r\n" );
    CONSOLE_Printf( "  spi_loop load slave <message>\r\n" );
    CONSOLE_Printf( "  spi_loop clear\r\n" );
    CONSOLE_Printf( "\r\n" );

    CONSOLE_Printf( "Run:\r\n" );
    CONSOLE_Printf( "  spi_loop run\r\n" );
    CONSOLE_Printf( "  spi_loop run verbose\r\n" );
    CONSOLE_Printf( "\r\n" );

    CONSOLE_Printf( "Status:\r\n" );
    CONSOLE_Printf( "  spi_loop status\r\n" );
}

void CONSOLE_SPI_Loopback_Config( uint16_t argc, char* argv[] )
{
    if ( argc < 5 || argv[2] == NULL || argv[3] == NULL || argv[4] == NULL )
    {
        CONSOLE_Printf( "Usage: spi_loop config <0|1> <option> <value>\r\n" );
        return;
    }

    SPILoopChannel_T* channel = CONSOLE_SPI_Loopback_Get_Channel( argv[2] );

    if ( channel == NULL )
    {
        CONSOLE_Printf( "Invalid channel: %s\r\n", argv[2] );
        return;
    }

    if ( strcmp( argv[3], "mode" ) == 0 )
    {
        SPIMode_T mode;

        if ( !CONSOLE_SPI_Loopback_Parse_Mode( argv[4], &mode ) )
        {
            CONSOLE_Printf( "Invalid mode: %s\r\n", argv[4] );
            CONSOLE_Printf( "Use: master or slave\r\n" );
            return;
        }

        channel->configuration.spi_mode = mode;
    }
    else if ( strcmp( argv[3], "datasize" ) == 0 )
    {
        SPIDataSize_T data_size;

        if ( !CONSOLE_SPI_Loopback_Parse_Data_Size( argv[4], &data_size ) )
        {
            CONSOLE_Printf( "Invalid datasize: %s\r\n", argv[4] );
            CONSOLE_Printf( "Use: 8 or 16\r\n" );
            return;
        }

        channel->configuration.data_size = data_size;
    }
    else if ( strcmp( argv[3], "firstbit" ) == 0 )
    {
        SPIFirstBit_T first_bit;

        if ( !CONSOLE_SPI_Loopback_Parse_First_Bit( argv[4], &first_bit ) )
        {
            CONSOLE_Printf( "Invalid firstbit: %s\r\n", argv[4] );
            CONSOLE_Printf( "Use: msb or lsb\r\n" );
            return;
        }

        channel->configuration.first_bit = first_bit;
    }
    else if ( strcmp( argv[3], "baud" ) == 0 )
    {
        SPIBaudRate_T baud_rate;

        if ( !CONSOLE_SPI_Loopback_Parse_Baud( argv[4], &baud_rate ) )
        {
            CONSOLE_Printf( "Invalid baud: %s\r\n", argv[4] );
            CONSOLE_Printf( "Use: 45m, 22m5, 11m25, 5m625, 2m813, 1m406, 703k, or 352k\r\n" );
            return;
        }

        channel->configuration.baud_rate = baud_rate;
    }
    else if ( strcmp( argv[3], "cpol" ) == 0 )
    {
        SPICPOL_T cpol;

        if ( !CONSOLE_SPI_Loopback_Parse_CPOL( argv[4], &cpol ) )
        {
            CONSOLE_Printf( "Invalid cpol: %s\r\n", argv[4] );
            CONSOLE_Printf( "Use: low or high\r\n" );
            return;
        }

        channel->configuration.cpol = cpol;
    }
    else if ( strcmp( argv[3], "cpha" ) == 0 )
    {
        SPICPHA_T cpha;

        if ( !CONSOLE_SPI_Loopback_Parse_CPHA( argv[4], &cpha ) )
        {
            CONSOLE_Printf( "Invalid cpha: %s\r\n", argv[4] );
            CONSOLE_Printf( "Use: 1edge or 2edge\r\n" );
            return;
        }

        channel->configuration.cpha = cpha;
    }
    else
    {
        CONSOLE_Printf( "Invalid config option: %s\r\n", argv[3] );
        CONSOLE_Printf( "Use: mode, datasize, firstbit, baud, cpol, or cpha\r\n" );
        return;
    }

    channel->configured = false;

    CONSOLE_Printf( "Updated SPI channel %s %s to %s\r\n", argv[2], argv[3], argv[4] );
    CONSOLE_Printf( "Run 'spi_loop apply %s' to apply the configuration.\r\n", argv[2] );
}

void CONSOLE_SPI_Loopback_Apply( uint16_t argc, char* argv[] )
{
    if ( argc < 3 || argv[2] == NULL )
    {
        CONSOLE_Printf( "Usage: spi_loop apply <0|1|all>\r\n" );
        return;
    }

    if ( strcmp( argv[2], "all" ) == 0 )
    {
        if ( !CONSOLE_SPI_Loopback_Apply_Channel( &spi_loop_state.channel_0 ) )
        {
            CONSOLE_Printf( "Failed to apply SPI channel 0 configuration\r\n" );
            return;
        }

        if ( !CONSOLE_SPI_Loopback_Apply_Channel( &spi_loop_state.channel_1 ) )
        {
            CONSOLE_Printf( "Failed to apply SPI channel 1 configuration\r\n" );
            return;
        }

        CONSOLE_Printf( "Applied SPI channel 0 and 1 configurations\r\n" );
        return;
    }

    SPILoopChannel_T* channel = CONSOLE_SPI_Loopback_Get_Channel( argv[2] );

    if ( channel == NULL )
    {
        CONSOLE_Printf( "Invalid channel: %s\r\n", argv[2] );
        return;
    }

    if ( !CONSOLE_SPI_Loopback_Apply_Channel( channel ) )
    {
        CONSOLE_Printf( "Failed to apply SPI channel %s configuration\r\n", argv[2] );
        return;
    }

    CONSOLE_Printf( "Applied SPI channel %s configuration\r\n", argv[2] );
}

void CONSOLE_SPI_Loopback_Load( uint16_t argc, char* argv[] )
{
    if ( argc < 4 || argv[2] == NULL || argv[3] == NULL )
    {
        CONSOLE_Printf( "Usage: spi_loop load <master|slave> <message>\r\n" );
        return;
    }

    uint32_t message_size_bytes = strlen( argv[3] );

    if ( message_size_bytes > SPI_LOOP_MAX_MESSAGE_BYTES )
    {
        CONSOLE_Printf( "Message too long. Max is %lu bytes\r\n", SPI_LOOP_MAX_MESSAGE_BYTES );
        return;
    }

    if ( strcmp( argv[2], "master" ) == 0 )
    {
        memcpy( spi_loop_state.master_tx, argv[3], message_size_bytes );
        spi_loop_state.master_tx_size_bytes = message_size_bytes;
        spi_loop_state.master_tx_loaded     = true;

        CONSOLE_Printf( "Loaded master TX message (%lu bytes)\r\n", message_size_bytes );
    }
    else if ( strcmp( argv[2], "slave" ) == 0 )
    {
        memcpy( spi_loop_state.slave_tx, argv[3], message_size_bytes );
        spi_loop_state.slave_tx_size_bytes = message_size_bytes;
        spi_loop_state.slave_tx_loaded     = true;

        CONSOLE_Printf( "Loaded slave TX message (%lu bytes)\r\n", message_size_bytes );
    }
    else
    {
        CONSOLE_Printf( "Invalid endpoint: %s\r\n", argv[2] );
        CONSOLE_Printf( "Use: master or slave\r\n" );
    }
}

void CONSOLE_SPI_Loopback_Clear( void )
{
    memset( spi_loop_state.master_tx, 0, sizeof( spi_loop_state.master_tx ) );
    memset( spi_loop_state.slave_tx, 0, sizeof( spi_loop_state.slave_tx ) );
    memset( spi_loop_state.master_rx, 0, sizeof( spi_loop_state.master_rx ) );
    memset( spi_loop_state.slave_rx, 0, sizeof( spi_loop_state.slave_rx ) );

    spi_loop_state.master_tx_size_bytes = 0U;
    spi_loop_state.slave_tx_size_bytes  = 0U;
    spi_loop_state.master_rx_size_bytes = 0U;
    spi_loop_state.slave_rx_size_bytes  = 0U;

    spi_loop_state.master_tx_loaded = false;
    spi_loop_state.slave_tx_loaded  = false;

    CONSOLE_Printf( "SPI loopback messages cleared\r\n" );
}

void CONSOLE_SPI_Loopback_Status( void )
{
    CONSOLE_SPI_Loopback_Print_Channel_Status( "Channel 0", &spi_loop_state.channel_0 );
    CONSOLE_SPI_Loopback_Print_Channel_Status( "Channel 1", &spi_loop_state.channel_1 );

    CONSOLE_Printf( "Messages:\r\n" );
    CONSOLE_Printf( "  master tx loaded: %s, %lu bytes\r\n",
                    spi_loop_state.master_tx_loaded ? "yes" : "no",
                    spi_loop_state.master_tx_size_bytes );
    CONSOLE_Printf( "  slave tx loaded:  %s, %lu bytes\r\n",
                    spi_loop_state.slave_tx_loaded ? "yes" : "no",
                    spi_loop_state.slave_tx_size_bytes );
}

void CONSOLE_SPI_Loopback_Run( uint16_t argc, char* argv[] )
{
    bool verbose = false;

    if ( argc >= 3 && argv[2] != NULL )
    {
        if ( strcmp( argv[2], "verbose" ) == 0 )
        {
            verbose = true;
        }
        else
        {
            CONSOLE_Printf( "Invalid run option: %s\r\n", argv[2] );
            CONSOLE_Printf( "Usage: spi_loop run [verbose]\r\n" );
            return;
        }
    }

    SPILoopChannel_T* master_channel = CONSOLE_SPI_Loopback_Get_Master_Channel();
    SPILoopChannel_T* slave_channel  = CONSOLE_SPI_Loopback_Get_Slave_Channel();

    if ( master_channel == NULL || slave_channel == NULL )
    {
        CONSOLE_Printf( "FAIL: one channel must be configured as master and one as slave\r\n" );
        return;
    }

    if ( !master_channel->configured || !slave_channel->configured )
    {
        CONSOLE_Printf( "FAIL: both channels must be applied before running\r\n" );
        CONSOLE_Printf( "Use: spi_loop apply all\r\n" );
        return;
    }

    if ( !spi_loop_state.master_tx_loaded )
    {
        CONSOLE_Printf( "FAIL: master TX message has not been loaded\r\n" );
        return;
    }

    if ( !spi_loop_state.slave_tx_loaded )
    {
        CONSOLE_Printf( "FAIL: slave TX message has not been loaded\r\n" );
        return;
    }

    memset( spi_loop_state.master_rx, 0, sizeof( spi_loop_state.master_rx ) );
    memset( spi_loop_state.slave_rx, 0, sizeof( spi_loop_state.slave_rx ) );

    spi_loop_state.master_rx_size_bytes = 0U;
    spi_loop_state.slave_rx_size_bytes  = 0U;

    // Clear stale RX data from both sides so this run only reports the current exchange.
    spi_loop_state.master_rx_size_bytes = CONSOLE_SPI_Loopback_Copy_Rx(
        master_channel->peripheral, spi_loop_state.master_rx, sizeof( spi_loop_state.master_rx ) );

    spi_loop_state.slave_rx_size_bytes = CONSOLE_SPI_Loopback_Copy_Rx(
        slave_channel->peripheral, spi_loop_state.slave_rx, sizeof( spi_loop_state.slave_rx ) );

    spi_loop_state.master_rx_size_bytes = 0U;
    spi_loop_state.slave_rx_size_bytes  = 0U;

    // Load the slave TX buffer first. The slave must already have data ready
    // when the master starts generating SPI clocks.
    if ( !HW_SPI_Load_Tx_Buffer( slave_channel->peripheral, spi_loop_state.slave_tx,
                                 spi_loop_state.slave_tx_size_bytes ) )
    {
        CONSOLE_Printf( "FAIL: could not load slave TX buffer\r\n" );
        return;
    }

    HW_SPI_Tx_Trigger( slave_channel->peripheral );

    if ( !HW_SPI_Load_Tx_Buffer( master_channel->peripheral, spi_loop_state.master_tx,
                                 spi_loop_state.master_tx_size_bytes ) )
    {
        CONSOLE_Printf( "FAIL: could not load master TX buffer\r\n" );
        return;
    }

    HW_SPI_Tx_Trigger( master_channel->peripheral );

    vTaskDelay( SPI_LOOP_TRANSFER_DELAY_MS );

    spi_loop_state.master_rx_size_bytes = CONSOLE_SPI_Loopback_Copy_Rx(
        master_channel->peripheral, spi_loop_state.master_rx, sizeof( spi_loop_state.master_rx ) );

    spi_loop_state.slave_rx_size_bytes = CONSOLE_SPI_Loopback_Copy_Rx(
        slave_channel->peripheral, spi_loop_state.slave_rx, sizeof( spi_loop_state.slave_rx ) );

    bool slave_received_master_tx =
        ( spi_loop_state.slave_rx_size_bytes >= spi_loop_state.master_tx_size_bytes )
        && ( memcmp( spi_loop_state.slave_rx, spi_loop_state.master_tx,
                     spi_loop_state.master_tx_size_bytes )
             == 0 );

    bool master_received_slave_tx =
        ( spi_loop_state.master_rx_size_bytes >= spi_loop_state.slave_tx_size_bytes )
        && ( memcmp( spi_loop_state.master_rx, spi_loop_state.slave_tx,
                     spi_loop_state.slave_tx_size_bytes )
             == 0 );

    bool passed = slave_received_master_tx && master_received_slave_tx;

    if ( verbose && ( spi_loop_state.master_rx_size_bytes > spi_loop_state.slave_tx_size_bytes ) )
    {
        CONSOLE_Printf( "Note: master received %lu extra byte(s) after the slave TX message.\r\n",
                        spi_loop_state.master_rx_size_bytes - spi_loop_state.slave_tx_size_bytes );
        CONSOLE_Printf( "These extra bytes are ignored because the master clocked more bytes than "
                        "the slave loaded.\r\n" );
    }

    if ( verbose && ( spi_loop_state.slave_rx_size_bytes > spi_loop_state.master_tx_size_bytes ) )
    {
        CONSOLE_Printf( "Note: slave received %lu extra byte(s) after the master TX message.\r\n",
                        spi_loop_state.slave_rx_size_bytes - spi_loop_state.master_tx_size_bytes );
    }

    if ( verbose )
    {
        CONSOLE_Printf( "SPI loopback result: %s\r\n", passed ? "PASS" : "FAIL" );
        CONSOLE_Printf( "\r\n" );

        CONSOLE_SPI_Loopback_Print_Buffer( "Master transmitted", spi_loop_state.master_tx,
                                           spi_loop_state.master_tx_size_bytes );

        CONSOLE_SPI_Loopback_Print_Buffer( "Master received", spi_loop_state.master_rx,
                                           spi_loop_state.master_rx_size_bytes );

        CONSOLE_Printf( "\r\n" );

        CONSOLE_SPI_Loopback_Print_Buffer( "Slave transmitted", spi_loop_state.slave_tx,
                                           spi_loop_state.slave_tx_size_bytes );

        CONSOLE_SPI_Loopback_Print_Buffer( "Slave received", spi_loop_state.slave_rx,
                                           spi_loop_state.slave_rx_size_bytes );
    }
    else
    {
        CONSOLE_Printf( "%s\r\n", passed ? "PASS" : "FAIL" );
    }
}
