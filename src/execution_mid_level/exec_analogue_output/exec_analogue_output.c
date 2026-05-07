/******************************************************************************
 *  File:       exec_analogue_output.c
 *  Author:     Coen Pasitchnyj
 *  Created:    02-May-2026
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

#include <stdint.h>
#include <stdbool.h>

#include "exec_analogue_output.h"
#include "hw_spi.h"

#ifndef TEST_BUILD
#include "stm32f4xx_ll_gpio.h"
#include "stm32f4xx_ll_spi.h"
#include "stm32f446xx.h"
#endif

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */

#define ANALOGUE_OUTPUT_DAC_CHANNEL_COUNT 8U
#define EXEC_ANALOGUE_OUTPUT_ConfigURED_CHANNEL_COUNT 6U
#define ANALOGUE_OUTPUT_DAC_MAX_COUNT 4095U
#define ANALOGUE_OUTPUT_INPUT_MAX_V 20.0F

#define ANALOGUE_OUTPUT_REG_DAC_BASE 0x00U
#define ANALOGUE_OUTPUT_REG_VREF_CTRL 0x08U
#define ANALOGUE_OUTPUT_REG_POWER_DOWN 0x09U
#define ANALOGUE_OUTPUT_REG_GAIN_CTRL 0x0AU

#define ANALOGUE_OUTPUT_VREF_EXT_BUFFERED 0xFFFFU
#define ANALOGUE_OUTPUT_GAIN_1X 0x0000U
#define ANALOGUE_OUTPUT_PD_OPEN_CIRCUIT 0xF000U
#define ANALOGUE_OUTPUT_SPI_INSTANCE SPI1

/**-----------------------------------------------------------------------------
 *  Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Public (global) and Extern Variables
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Private (static) Variables
 *------------------------------------------------------------------------------
 */

static bool s_EXEC_ANALOGUE_OUTPUT_Configured = false;

/**-----------------------------------------------------------------------------
 *  Private (static) Function Prototypes
 *------------------------------------------------------------------------------
 */

static uint8_t  EXEC_ANALOGUE_OUTPUT_Pack_Command_Byte( uint8_t register_address );
static bool     EXEC_ANALOGUE_OUTPUT_Send_Frame( uint8_t register_address, uint16_t data_word );
static uint16_t EXEC_ANALOGUE_OUTPUT_Clamp_And_Scale_Count( float input_voltage_v );
static bool     EXEC_ANALOGUE_OUTPUT_Queue_Startup_Frames( bool use_external_vref );

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

static uint8_t EXEC_ANALOGUE_OUTPUT_Pack_Command_Byte( uint8_t register_address )
{
    return ( uint8_t )( ( register_address & 0x1FU ) << 3U );
}

static bool EXEC_ANALOGUE_OUTPUT_Send_Frame( uint8_t register_address, uint16_t data_word )
{
    uint8_t frame[3] = {
        EXEC_ANALOGUE_OUTPUT_Pack_Command_Byte( register_address ),
        ( uint8_t )( ( data_word >> 8U ) & 0xFFU ),
        ( uint8_t )( data_word & 0xFFU ),
    };

    return HW_SPI_Load_Tx_Buffer( SPI_CHANNEL_0, frame, sizeof( frame ) );
}

static uint16_t EXEC_ANALOGUE_OUTPUT_Clamp_And_Scale_Count( float input_voltage_v )
{
    float clamped_voltage_v = input_voltage_v;

    if ( clamped_voltage_v < 0.0F )
    {
        clamped_voltage_v = 0.0F;
    }
    else if ( clamped_voltage_v > ANALOGUE_OUTPUT_INPUT_MAX_V )
    {
        clamped_voltage_v = ANALOGUE_OUTPUT_INPUT_MAX_V;
    }

    float scaled_count = ( clamped_voltage_v / ANALOGUE_OUTPUT_INPUT_MAX_V )
                         * ( float )ANALOGUE_OUTPUT_DAC_MAX_COUNT;
    uint16_t count = ( uint16_t )( scaled_count + 0.5F );

    if ( count > ANALOGUE_OUTPUT_DAC_MAX_COUNT )
    {
        count = ANALOGUE_OUTPUT_DAC_MAX_COUNT;
    }

    return count;
}

static bool EXEC_ANALOGUE_OUTPUT_Queue_Startup_Frames( bool use_external_vref )
{
    uint8_t  frame_bytes[3U * ( 3U + ANALOGUE_OUTPUT_DAC_CHANNEL_COUNT )] = { 0 };
    uint32_t frame_index_bytes                                            = 0U;

    struct
    {
        uint8_t  register_address;
        uint16_t data_word;
    } frames[] = {
        { ANALOGUE_OUTPUT_REG_VREF_CTRL, 0U /* placeholder, set below */ },
        { ANALOGUE_OUTPUT_REG_GAIN_CTRL, ANALOGUE_OUTPUT_GAIN_1X },
        { ANALOGUE_OUTPUT_REG_POWER_DOWN, ANALOGUE_OUTPUT_PD_OPEN_CIRCUIT },
        { ANALOGUE_OUTPUT_REG_DAC_BASE + 0U, 0U },
        { ANALOGUE_OUTPUT_REG_DAC_BASE + 1U, 0U },
        { ANALOGUE_OUTPUT_REG_DAC_BASE + 2U, 0U },
        { ANALOGUE_OUTPUT_REG_DAC_BASE + 3U, 0U },
        { ANALOGUE_OUTPUT_REG_DAC_BASE + 4U, 0U },
        { ANALOGUE_OUTPUT_REG_DAC_BASE + 5U, 0U },
        { ANALOGUE_OUTPUT_REG_DAC_BASE + 6U, 0U },
        { ANALOGUE_OUTPUT_REG_DAC_BASE + 7U, 0U },
    };

    /* Set VREF control based on requested mode */
    if ( use_external_vref )
    {
        frames[0].data_word = ANALOGUE_OUTPUT_VREF_EXT_BUFFERED;
    }
    else
    {
        /* 00 = use VDD as reference */
        frames[0].data_word = 0x0000U;
    }

    for ( uint32_t index = 0U; index < ( uint32_t )( sizeof( frames ) / sizeof( frames[0] ) );
          index++ )
    {
        frame_bytes[frame_index_bytes] =
            EXEC_ANALOGUE_OUTPUT_Pack_Command_Byte( frames[index].register_address );
        frame_bytes[frame_index_bytes + 1U] =
            ( uint8_t )( ( frames[index].data_word >> 8U ) & 0xFFU );
        frame_bytes[frame_index_bytes + 2U] = ( uint8_t )( frames[index].data_word & 0xFFU );

        // LL_GPIO_ResetOutputPin(nCS_GPIO_Port, nCS_Pin);  // CS low

        if ( !HW_SPI_Load_Tx_Buffer( SPI_CHANNEL_0, &frame_bytes[frame_index_bytes], 3U ) )
        {
            return false;
        }

        // while ( !HW_SPI_Tx_Buffer_Empty( SPI_CHANNEL_0 ) )
        // {
        //     /* Wait for the frame to be transmitted before loading the next one to ensure
        //      * the DAC receives them in the correct order. */
        // }
        // while ( LL_SPI_IsActiveFlag_BSY( ANALOGUE_OUTPUT_SPI_INSTANCE ) )
        // {
        //     // wait until SPI fully finished shifting
        // }

        // LL_GPIO_SetOutputPin( nCS_GPIO_Port, nCS_Pin );  // CS high

        frame_index_bytes += 3U;
    }

    HW_SPI_Tx_Trigger( SPI_CHANNEL_0 );

    return true;
}

/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */

/**
 * @brief Configure and start the SPI4 hardware channel dedicated to DAC communication.
 *
 * Sets up the SPI4 peripheral with the configuration required by the
 * MCP48CVB28T-20E_ST octal DAC: 8-bit data size, 352K baud rate, MSB first,
 * CPOL low, CPHA 1 edge.
 *
 * This function must be called once during system initialization to prepare
 * SPI4 for use before any DAC operations are performed. In the real project,
 * this setup will be performed by the system/board initialization layer.
 *
 * This function is provided as a separate helper for console testing so that
 * test commands can independently set up the SPI channel without integrating
 * into the full system initialization sequence.
 *
 * The SPI channel is activated for immediate use. After this function returns
 * successfully, the channel is ready to transmit frames to the DAC.
 *
 * @return
 *     true if SPI4 configuration and startup completed successfully.
 *     false if hardware configuration or startup failed.
 */
bool EXEC_ANALOGUE_OUTPUT_SPI_Channel_Setup( void )
{
    HWSPIConfig_T configuration = {
        .spi_mode  = SPI_MASTER_MODE,
        .data_size = SPI_SIZE_8_BIT,
        .first_bit = SPI_FIRST_MSB,
        .baud_rate = SPI_BAUD_2M813BIT,
        .cpol      = SPI_CPOL_LOW,
        .cpha      = SPI_CPHA_1_EDGE,
    };

    if ( !HW_SPI_Configure_Channel( SPI_CHANNEL_0, configuration ) )
    {
        return false;
    }

    HW_SPI_Start_Channel( SPI_CHANNEL_0 );
    return true;
}

/**
 * @brief Initialize the DAC hardware registers and prepare all output channels.
 *
 * Configures the DAC's volatile control registers and sets all output channels
 * to zero voltage. This function assumes that the SPI4 hardware channel has
 * already been configured and started (via EXEC_ANALOGUE_OUTPUT_SPI_Channel_Setup()
 * or the system initialization layer).
 *
 * Configuration includes:
 * - VREF control register (08h): External 5V VREF in buffered mode
 * - Gain register (0Ah): 1x gain (5V full scale with 5V VREF)
 * - Power-down register (09h): Channels 0-5 enabled, channels 6-7 in open-circuit mode
 * - DAC output registers (00h-07h): All channels initialized to 0V
 *
 * After this function completes successfully, the module is ready to accept
 * voltage write commands via EXEC_ANALOG_OUTPUT_Write_Voltage().
 *
 * @return
 *     true if DAC initialization completed successfully.
 *     false if SPI transmission of the initialization frames failed.
 */
bool EXEC_ANALOGUE_OUTPUT_Config( bool use_external_vref )
{
    // LL_GPIO_SetOutputPin( nCS_GPIO_Port, nCS_Pin );  // CS high

    if ( !EXEC_ANALOGUE_OUTPUT_Queue_Startup_Frames( use_external_vref ) )
    {
        s_EXEC_ANALOGUE_OUTPUT_Configured = false;
        return false;
    }

    s_EXEC_ANALOGUE_OUTPUT_Configured = true;
    return true;
}

bool EXEC_ANALOG_OUTPUT_Is_Configured( void )
{
    return s_EXEC_ANALOGUE_OUTPUT_Configured;
}

/**
 * @brief Write a voltage to a single DAC output channel.
 *
 * Accepts a voltage in the range 0V to 20V, clamps it to the valid input range,
 * scales it to the DAC's 0-5V output range, converts it to a 12-bit DAC code,
 * and transmits a write command to the MCP48 DAC via SPI4.
 *
 * Input voltage scaling and clamping:
 * - Input range: 0V to 20V (nominally full scale at 20V)
 * - Values below 0V are clamped to 0V
 * - Values above 20V are clamped to 20V
 * - Scaled to DAC output range: 0V to 5V
 * - DAC code formula: code = (clamped_voltage / 20.0) * 4095
 *
 * Only channels 0-5 are functional. Attempts to write to channels 6-7 will
 * fail with false return code because those channels are disabled (configured
 * in open-circuit mode).
 *
 * The module must be initialized via EXEC_ANALOGUE_OUTPUT_Config() before this
 * function is called. Writing to an uninitialized module returns false.
 *
 * @param channel
 *     The DAC output channel number (0-5 for active channels, 6-7 disabled).
 *
 * @param input_voltage_v
 *     The desired output voltage in volts (0V to 20V, clamped and scaled).
 *
 * @return
 *     true if the voltage write was accepted and queued to SPI for transmission.
 *     false if the module is not initialized, the channel is invalid (>= 6),
 *     or SPI transmission failed.
 */
bool EXEC_ANALOG_OUTPUT_Write_Voltage( uint8_t channel, float input_voltage_v )
{
    uint16_t count = 0U;

    if ( !s_EXEC_ANALOGUE_OUTPUT_Configured )
    {
        return false;
    }

    if ( channel >= EXEC_ANALOGUE_OUTPUT_ConfigURED_CHANNEL_COUNT )
    {
        return false;
    }

    count = EXEC_ANALOGUE_OUTPUT_Clamp_And_Scale_Count( input_voltage_v );

    // LL_GPIO_ResetOutputPin(nCS_GPIO_Port, nCS_Pin);  // CS low

    if ( !EXEC_ANALOGUE_OUTPUT_Send_Frame( ( uint8_t )( ANALOGUE_OUTPUT_REG_DAC_BASE + channel ),
                                           count ) )
    {
        return false;
    }

    HW_SPI_Tx_Trigger( SPI_CHANNEL_0 );

    // while ( !HW_SPI_Tx_Buffer_Empty( SPI_CHANNEL_0 ) )
    // {
    //     /* Wait for SPI transfer to complete before raising CS */
    // }
    // while ( LL_SPI_IsActiveFlag_BSY( ANALOGUE_OUTPUT_SPI_INSTANCE ) )
    // {
    //     // wait until SPI fully finished shifting
    // }

    // LL_GPIO_SetOutputPin( nCS_GPIO_Port, nCS_Pin );  // CS high

    return true;
}
