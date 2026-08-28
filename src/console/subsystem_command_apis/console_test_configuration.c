/******************************************************************************
 *  File:       console_test_configuration.c
 *  Description:
 *      Builds complete per-channel configurations for RSM lifecycle testing.
 ******************************************************************************/

#include "console_test_configuration.h"
#include "console.h"
#include "run_state_manager.h"
#include "test_configuration.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static void CONSOLE_TestConfiguration_PrintUsage( void )
{
    CONSOLE_Printf( "Usage:\r\n" );
    CONSOLE_Printf( "  test_config status\r\n" );
    CONSOLE_Printf( "  test_config inert\r\n" );
    CONSOLE_Printf( "  test_config analogue_input <enable|disable>\r\n" );
    CONSOLE_Printf( "  test_config digital_input <1-10> <off|3v3|5v|12v|24v>\r\n" );
    CONSOLE_Printf( "  test_config pwm_capture <1-2> <off|3v3|5v|12v|24v>\r\n" );
    CONSOLE_Printf( "  test_config analogue_output <off|internal|external>\r\n" );
    CONSOLE_Printf( "  test_config digital_output <1-10> <off|3v3|5v|12v|24v> <low|high>\r\n" );
    CONSOLE_Printf(
        "  test_config pwm_generation <1-2> <off|3v3|5v|12v|24v> <arr> <ccr> <psc>\r\n" );
    CONSOLE_Printf(
        "  test_config can <1-2> <off|bitrate> [filter_bank filter_id filter_mask]\r\n" );
    CONSOLE_Printf( "  test_config spi <1-2> off\r\n" );
    CONSOLE_Printf(
        "  test_config spi <1-2> <master|slave> <8|16> <mode0|mode1|mode2|mode3> default_cs\r\n" );
    CONSOLE_Printf( "  test_config uart <1-2> <off|3v3|5v|rs232> [baud] [rx|tx|both]\r\n" );
    CONSOLE_Printf( "  I2C is intentionally forced disabled during hardware bring-up.\r\n" );
}

static bool CONSOLE_TestConfiguration_ParseU32( const char* token, uint32_t* value )
{
    char*               end    = NULL;
    const unsigned long parsed = strtoul( token, &end, 0 );
    if ( end == token || *end != '\0' || parsed > UINT32_MAX )
    {
        return false;
    }
    *value = ( uint32_t )parsed;
    return true;
}

static bool CONSOLE_TestConfiguration_ParseVoltage( const char*               token,
                                                    ExecPwmGenVoltageLevel_T* voltage )
{
    if ( strcmp( token, "off" ) == 0 )
    {
        *voltage = EXEC_PWM_GEN_VOLTAGE_DISABLED;
    }
    else if ( strcmp( token, "3v3" ) == 0 )
    {
        *voltage = EXEC_PWM_GEN_VOLTAGE_3V3;
    }
    else if ( strcmp( token, "5v" ) == 0 )
    {
        *voltage = EXEC_PWM_GEN_VOLTAGE_5V;
    }
    else if ( strcmp( token, "12v" ) == 0 )
    {
        *voltage = EXEC_PWM_GEN_VOLTAGE_12V;
    }
    else if ( strcmp( token, "24v" ) == 0 )
    {
        *voltage = EXEC_PWM_GEN_VOLTAGE_24V;
    }
    else
    {
        return false;
    }
    return true;
}

static bool CONSOLE_TestConfiguration_CanCommit( void )
{
    const RunState_T state = RUN_STATE_MANAGER_GetState();
    return !RUN_STATE_MANAGER_IsTransitionPending()
           && ( state == RUN_STATE_IDLE || state == RUN_STATE_TEST_PACKAGE_RECEIVE );
}

static bool CONSOLE_TestConfiguration_ParseDigitalInputMode( const char*             token,
                                                             ExecDigitalInputMode_T* mode )
{
    if ( strcmp( token, "off" ) == 0 )
    {
        *mode = EXEC_DIGITAL_INPUT_MODE_DISABLED;
    }
    else if ( strcmp( token, "3v3" ) == 0 )
    {
        *mode = EXEC_DIGITAL_INPUT_MODE_3V3;
    }
    else if ( strcmp( token, "5v" ) == 0 )
    {
        *mode = EXEC_DIGITAL_INPUT_MODE_5V;
    }
    else if ( strcmp( token, "12v" ) == 0 )
    {
        *mode = EXEC_DIGITAL_INPUT_MODE_12V;
    }
    else if ( strcmp( token, "24v" ) == 0 )
    {
        *mode = EXEC_DIGITAL_INPUT_MODE_24V;
    }
    else
    {
        return false;
    }

    return true;
}

static bool CONSOLE_TestConfiguration_ParsePwmCaptureMode( const char*             token,
                                                           ExecPwmCaptureConfig_T* configuration )
{
    configuration->is_enabled = strcmp( token, "off" ) != 0;

    if ( strcmp( token, "off" ) == 0 || strcmp( token, "3v3" ) == 0 )
    {
        configuration->mode = EXEC_PWM_CAPTURE_LV_3V3;
    }
    else if ( strcmp( token, "5v" ) == 0 )
    {
        configuration->mode = EXEC_PWM_CAPTURE_LV_5V;
    }
    else if ( strcmp( token, "12v" ) == 0 )
    {
        configuration->mode = EXEC_PWM_CAPTURE_HV_12V;
    }
    else if ( strcmp( token, "24v" ) == 0 )
    {
        configuration->mode = EXEC_PWM_CAPTURE_HV_24V;
    }
    else
    {
        return false;
    }

    return true;
}

static void CONSOLE_TestConfiguration_PrintStatus( void )
{
    DutDriverConfiguration_T configuration = { 0 };
    if ( !TEST_CONFIGURATION_GetActive( &configuration ) )
    {
        CONSOLE_Printf( "No active test configuration.\r\n" );
        return;
    }

    CONSOLE_Printf( "Analogue input: %s, channels=%u/%u, sample_rate=%u\r\n",
                    configuration.analogue_input.is_enabled ? "enabled" : "disabled",
                    ( unsigned int )configuration.analogue_input.ch_0_is_enabled,
                    ( unsigned int )configuration.analogue_input.ch_1_is_enabled,
                    ( unsigned int )configuration.analogue_input.sample_rate );

    CONSOLE_Printf( "Digital input modes:" );
    for ( uint32_t channel = 0U; channel < EXEC_DIGITAL_INPUT_CHANNEL_COUNT; channel++ )
    {
        CONSOLE_Printf( " %u", ( unsigned int )configuration.digital_inputs.channels[channel] );
    }
    CONSOLE_Printf( "\r\nPWM capture:" );
    for ( uint32_t channel = 0U; channel < TEST_CONFIGURATION_PWM_CAPTURE_CHANNEL_COUNT; channel++ )
    {
        CONSOLE_Printf( " ch%u=%s(mode=%u)", ( unsigned int )( channel + 1U ),
                        configuration.pwm_capture_channels[channel].is_enabled ? "on" : "off",
                        ( unsigned int )configuration.pwm_capture_channels[channel].mode );
    }
    CONSOLE_Printf( "\r\n" );
    CONSOLE_Printf( "Analogue output: %s, vref=%s\r\n",
                    configuration.analogue_output.is_enabled ? "enabled" : "disabled",
                    configuration.analogue_output.use_external_vref ? "external" : "internal" );
    CONSOLE_Printf( "Digital outputs:" );
    for ( uint32_t channel = 0U; channel < EXEC_DIGITAL_OUTPUT_CHANNEL_COUNT; channel++ )
    {
        const ExecDigitalOutputChannelConfig_T* item =
            &configuration.digital_outputs.channels[channel];
        CONSOLE_Printf( " ch%u=%s(mode=%u,%s)", ( unsigned int )( channel + 1U ),
                        item->is_enabled ? "on" : "off", ( unsigned int )item->mode,
                        item->initial_high ? "high" : "low" );
    }
    CONSOLE_Printf( "\r\nPWM generation:" );
    for ( uint32_t channel = 0U; channel < EXEC_PWM_GEN_CHANNEL_COUNT; channel++ )
    {
        const ExecPwmGenConfig_T* item = &configuration.pwm_generation_channels[channel];
        CONSOLE_Printf( " ch%u=%s(v=%u,arr=%u,ccr=%u,psc=%u)", ( unsigned int )( channel + 1U ),
                        item->is_enabled ? "on" : "off", ( unsigned int )item->voltage_level,
                        ( unsigned int )item->initial_arr, ( unsigned int )item->initial_ccr,
                        ( unsigned int )item->initial_psc );
    }
    CONSOLE_Printf( "\r\nCAN:" );
    for ( uint32_t channel = 0U; channel < EXEC_CAN_CHANNEL_COUNT; channel++ )
    {
        const EXEC_CAN_Config_T* item = &configuration.can_channels[channel];
        CONSOLE_Printf( " ch%u=%s(%lu bit/s)", ( unsigned int )( channel + 1U ),
                        item->is_enabled ? "on" : "off", ( unsigned long )item->bitrate );
    }
    CONSOLE_Printf( "\r\nSPI:" );
    for ( uint32_t channel = 0U; channel < TEST_CONFIGURATION_SPI_CHANNEL_COUNT; channel++ )
    {
        CONSOLE_Printf( " ch%u=%s", ( unsigned int )( channel + 1U ),
                        configuration.spi_channels[channel].is_enabled ? "on" : "off" );
    }
    CONSOLE_Printf( "\r\nUART:" );
    for ( uint32_t channel = 0U; channel < EXEC_UART_CHANNEL_COUNT; channel++ )
    {
        const ExecUartConfig_T* item = &configuration.uart_channels[channel];
        CONSOLE_Printf( " ch%u=%s(mode=%u,%lu bit/s,rx=%u,tx=%u)", ( unsigned int )( channel + 1U ),
                        item->is_enabled ? "on" : "off", ( unsigned int )item->interface_mode,
                        ( unsigned long )item->baud_rate, ( unsigned int )item->rx_enabled,
                        ( unsigned int )item->tx_enabled );
    }
    CONSOLE_Printf( "\r\nI2C: forced disabled\r\n" );
}

void CONSOLE_TestConfiguration_Command( uint16_t argc, char* argv[] )
{
    if ( argc == 2U && strcmp( argv[1], "status" ) == 0 )
    {
        CONSOLE_TestConfiguration_PrintStatus();
        return;
    }

    if ( !CONSOLE_TestConfiguration_CanCommit() )
    {
        CONSOLE_Printf( "Configuration commit rejected: RSM must be IDLE or receiving.\r\n" );
        return;
    }

    DutDriverConfiguration_T configuration = { 0 };
    if ( !TEST_CONFIGURATION_GetActive( &configuration ) )
    {
        CONSOLE_Printf( "Configuration commit failed: no active configuration.\r\n" );
        return;
    }

    if ( argc == 2U && strcmp( argv[1], "inert" ) == 0 )
    {
        ( void )memset( &configuration, 0, sizeof( configuration ) );
    }
    else if ( argc == 3U && strcmp( argv[1], "analogue_input" ) == 0 )
    {
        if ( strcmp( argv[2], "enable" ) == 0 )
        {
            configuration.analogue_input = ( ExecAnalogueInputConfig_T ){
                .is_enabled      = true,
                .sample_rate     = EXEC_ANALOGUE_INPUT_SAMPLE_RATE_1K_HZ,
                .ch_0_is_enabled = true,
                .ch_1_is_enabled = true,
            };
        }
        else if ( strcmp( argv[2], "disable" ) == 0 )
        {
            configuration.analogue_input = ( ExecAnalogueInputConfig_T ){ 0 };
        }
        else
        {
            CONSOLE_TestConfiguration_PrintUsage();
            return;
        }
    }
    else if ( argc == 4U && strcmp( argv[1], "digital_input" ) == 0 )
    {
        char*                  end     = NULL;
        const uint32_t         channel = ( uint32_t )strtoul( argv[2], &end, 10 );
        ExecDigitalInputMode_T mode    = EXEC_DIGITAL_INPUT_MODE_DISABLED;
        if ( end == argv[2] || *end != '\0' || channel < 1U
             || channel > EXEC_DIGITAL_INPUT_CHANNEL_COUNT
             || !CONSOLE_TestConfiguration_ParseDigitalInputMode( argv[3], &mode ) )
        {
            CONSOLE_TestConfiguration_PrintUsage();
            return;
        }
        configuration.digital_inputs.channels[channel - 1U] = mode;
    }
    else if ( argc == 4U && strcmp( argv[1], "pwm_capture" ) == 0 )
    {
        char*          end     = NULL;
        const uint32_t channel = ( uint32_t )strtoul( argv[2], &end, 10 );
        if ( end == argv[2] || *end != '\0' || channel < 1U
             || channel > TEST_CONFIGURATION_PWM_CAPTURE_CHANNEL_COUNT
             || !CONSOLE_TestConfiguration_ParsePwmCaptureMode(
                 argv[3], &configuration.pwm_capture_channels[channel - 1U] ) )
        {
            CONSOLE_TestConfiguration_PrintUsage();
            return;
        }
    }
    else if ( argc == 3U && strcmp( argv[1], "analogue_output" ) == 0 )
    {
        if ( strcmp( argv[2], "off" ) == 0 )
        {
            configuration.analogue_output = ( ExecAnalogueOutputConfig_T ){ 0 };
        }
        else if ( strcmp( argv[2], "internal" ) == 0 || strcmp( argv[2], "external" ) == 0 )
        {
            configuration.analogue_output.is_enabled        = true;
            configuration.analogue_output.use_external_vref = strcmp( argv[2], "external" ) == 0;
        }
        else
        {
            CONSOLE_TestConfiguration_PrintUsage();
            return;
        }
    }
    else if ( argc == 5U && strcmp( argv[1], "digital_output" ) == 0 )
    {
        uint32_t                 channel = 0U;
        ExecPwmGenVoltageLevel_T voltage;
        if ( !CONSOLE_TestConfiguration_ParseU32( argv[2], &channel ) || channel < 1U
             || channel > EXEC_DIGITAL_OUTPUT_CHANNEL_COUNT
             || !CONSOLE_TestConfiguration_ParseVoltage( argv[3], &voltage )
             || ( strcmp( argv[4], "low" ) != 0 && strcmp( argv[4], "high" ) != 0 ) )
        {
            CONSOLE_TestConfiguration_PrintUsage();
            return;
        }
        ExecDigitalOutputChannelConfig_T* item =
            &configuration.digital_outputs.channels[channel - 1U];
        item->is_enabled   = voltage != EXEC_PWM_GEN_VOLTAGE_DISABLED;
        item->mode         = item->is_enabled ? ( ExecDigitalOutputMode_T )( voltage - 1 )
                                              : EXEC_DIGITAL_OUTPUT_MODE_3V3;
        item->initial_high = strcmp( argv[4], "high" ) == 0;
    }
    else if ( argc == 7U && strcmp( argv[1], "pwm_generation" ) == 0 )
    {
        uint32_t                 channel = 0U, arr = 0U, ccr = 0U, psc = 0U;
        ExecPwmGenVoltageLevel_T voltage;
        if ( !CONSOLE_TestConfiguration_ParseU32( argv[2], &channel ) || channel < 1U
             || channel > EXEC_PWM_GEN_CHANNEL_COUNT
             || !CONSOLE_TestConfiguration_ParseVoltage( argv[3], &voltage )
             || !CONSOLE_TestConfiguration_ParseU32( argv[4], &arr ) || arr > UINT16_MAX
             || !CONSOLE_TestConfiguration_ParseU32( argv[5], &ccr ) || ccr > UINT16_MAX
             || !CONSOLE_TestConfiguration_ParseU32( argv[6], &psc ) || psc > UINT16_MAX
             || ccr > ( arr + 1U ) )
        {
            CONSOLE_TestConfiguration_PrintUsage();
            return;
        }
        configuration.pwm_generation_channels[channel - 1U] =
            ( ExecPwmGenConfig_T ){ .is_enabled    = voltage != EXEC_PWM_GEN_VOLTAGE_DISABLED,
                                    .voltage_level = voltage,
                                    .initial_arr   = ( uint16_t )arr,
                                    .initial_ccr   = ( uint16_t )ccr,
                                    .initial_psc   = ( uint16_t )psc };
    }
    else if ( ( argc == 4U || argc == 7U ) && strcmp( argv[1], "can" ) == 0 )
    {
        uint32_t channel = 0U;
        if ( !CONSOLE_TestConfiguration_ParseU32( argv[2], &channel ) || channel < 1U
             || channel > EXEC_CAN_CHANNEL_COUNT )
        {
            CONSOLE_TestConfiguration_PrintUsage();
            return;
        }
        EXEC_CAN_Config_T* item = &configuration.can_channels[channel - 1U];
        if ( argc == 4U && strcmp( argv[3], "off" ) == 0 )
        {
            *item = ( EXEC_CAN_Config_T ){ 0 };
        }
        else
        {
            uint32_t bitrate = 0U, bank = 0U, id = 0U, mask = 0U;
            if ( argc != 7U || !CONSOLE_TestConfiguration_ParseU32( argv[3], &bitrate )
                 || !CONSOLE_TestConfiguration_ParseU32( argv[4], &bank ) || bank > UINT16_MAX
                 || !CONSOLE_TestConfiguration_ParseU32( argv[5], &id ) || id > UINT16_MAX
                 || !CONSOLE_TestConfiguration_ParseU32( argv[6], &mask ) || mask > UINT16_MAX )
            {
                CONSOLE_TestConfiguration_PrintUsage();
                return;
            }
            *item = ( EXEC_CAN_Config_T ){ true, bitrate, ( uint16_t )bank, ( uint16_t )id,
                                           ( uint16_t )mask };
        }
    }
    else if ( ( argc == 4U || argc == 7U ) && strcmp( argv[1], "spi" ) == 0 )
    {
        uint32_t channel = 0U;
        if ( !CONSOLE_TestConfiguration_ParseU32( argv[2], &channel ) || channel < 1U
             || channel > TEST_CONFIGURATION_SPI_CHANNEL_COUNT )
        {
            CONSOLE_TestConfiguration_PrintUsage();
            return;
        }
        ExecSPIConfig_T* item = &configuration.spi_channels[channel - 1U];
        if ( argc == 4U && strcmp( argv[3], "off" ) == 0 )
        {
            *item = ( ExecSPIConfig_T ){ 0 };
        }
        else if ( argc == 7U )
        {
            const bool master = strcmp( argv[3], "master" ) == 0;
            const bool slave  = strcmp( argv[3], "slave" ) == 0;
            if ( ( !master && !slave )
                 || ( strcmp( argv[4], "8" ) != 0 && strcmp( argv[4], "16" ) != 0 )
                 || strncmp( argv[5], "mode", 4U ) != 0 || argv[5][4] < '0' || argv[5][4] > '3'
                 || argv[5][5] != '\0' || strcmp( argv[6], "default_cs" ) != 0 )
            {
                CONSOLE_TestConfiguration_PrintUsage();
                return;
            }
            const uint8_t mode = ( uint8_t )( argv[5][4] - '0' );
            *item              = ( ExecSPIConfig_T ){
                             .is_enabled = true,
                             .hardware   = { .spi_mode = master ? SPI_MASTER_MODE : SPI_SLAVE_MODE,
                                             .data_size =
                                  strcmp( argv[4], "16" ) == 0 ? SPI_SIZE_16_BIT : SPI_SIZE_8_BIT,
                                             .first_bit = SPI_FIRST_MSB,
                                             .baud_rate = SPI_BAUD_1M406BIT,
                                             .cpol      = mode >= 2U ? SPI_CPOL_HIGH : SPI_CPOL_LOW,
                                             .cpha      = ( mode & 1U ) != 0U ? SPI_CPHA_2_EDGE : SPI_CPHA_1_EDGE,
                                             .nss_pin   = channel == 1U ? GPIO_SPI1_NSS : GPIO_SPI2_NSS } };
        }
        else
        {
            CONSOLE_TestConfiguration_PrintUsage();
            return;
        }
    }
    else if ( ( argc == 4U || argc == 6U ) && strcmp( argv[1], "uart" ) == 0 )
    {
        uint32_t channel = 0U;
        if ( !CONSOLE_TestConfiguration_ParseU32( argv[2], &channel ) || channel < 1U
             || channel > EXEC_UART_CHANNEL_COUNT )
        {
            CONSOLE_TestConfiguration_PrintUsage();
            return;
        }
        ExecUartConfig_T* item = &configuration.uart_channels[channel - 1U];
        if ( argc == 4U && strcmp( argv[3], "off" ) == 0 )
        {
            *item = ( ExecUartConfig_T ){ 0 };
        }
        else
        {
            uint32_t                baud = 0U;
            ExecUartInterfaceMode_T mode = EXEC_UART_MODE_DISABLED;
            if ( argc != 6U || !CONSOLE_TestConfiguration_ParseU32( argv[4], &baud ) || baud == 0U
                 || ( strcmp( argv[3], "3v3" ) != 0 && strcmp( argv[3], "5v" ) != 0
                      && strcmp( argv[3], "rs232" ) != 0 )
                 || ( strcmp( argv[5], "rx" ) != 0 && strcmp( argv[5], "tx" ) != 0
                      && strcmp( argv[5], "both" ) != 0 ) )
            {
                CONSOLE_TestConfiguration_PrintUsage();
                return;
            }
            mode = strcmp( argv[3], "3v3" ) == 0  ? EXEC_UART_MODE_TTL_3V3
                   : strcmp( argv[3], "5v" ) == 0 ? EXEC_UART_MODE_TTL_5V0
                                                  : EXEC_UART_MODE_RS232;
            if ( ( mode == EXEC_UART_MODE_RS232 && baud > 1000000U )
                 || ( mode != EXEC_UART_MODE_RS232 && baud > 2000000U ) )
            {
                CONSOLE_Printf( "UART baud rate exceeds the selected interface limit.\r\n" );
                return;
            }
            *item = ( ExecUartConfig_T ){ .interface_mode = mode,
                                          .baud_rate      = baud,
                                          .word_length    = HW_UART_WORD_LENGTH_8_BITS,
                                          .stop_bits      = HW_UART_STOP_BITS_1,
                                          .parity         = HW_UART_PARITY_NONE,
                                          .rx_enabled     = strcmp( argv[5], "tx" ) != 0,
                                          .tx_enabled     = strcmp( argv[5], "rx" ) != 0,
                                          .is_enabled     = true };
        }
    }
    else
    {
        CONSOLE_TestConfiguration_PrintUsage();
        return;
    }

    CONSOLE_Printf( "%s", TEST_CONFIGURATION_Commit( &configuration )
                              ? "Test configuration committed.\r\n"
                              : "Test configuration commit failed.\r\n" );
}
