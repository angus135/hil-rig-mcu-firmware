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
#include <math.h>
#include <stddef.h>
#include <string.h>

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
#define ANALOGUE_OUTPUT_SPI_CHANNEL SPI_DAC

#define ANALOGUE_OUTPUT_DAC_CHANNEL_COUNT 8U
#define EXEC_ANALOGUE_OUTPUT_CONFIGURED_CHANNEL_COUNT 6U
#define ANALOGUE_OUTPUT_STARTUP_CONTROL_FRAME_COUNT 3U
#define ANALOGUE_OUTPUT_STARTUP_FRAME_COUNT                                                        \
    ( ANALOGUE_OUTPUT_STARTUP_CONTROL_FRAME_COUNT + ANALOGUE_OUTPUT_DAC_CHANNEL_COUNT )
#define ANALOGUE_OUTPUT_STARTUP_PACKET_SIZE_BYTES                                                  \
    ( ANALOGUE_OUTPUT_STARTUP_FRAME_COUNT * EXEC_ANALOG_OUTPUT_FRAME_SIZE_BYTES )
#define ANALOGUE_OUTPUT_DAC_MAX_COUNT 4095U
#define ANALOGUE_OUTPUT_INPUT_MAX_V 20.0F

#define ANALOGUE_OUTPUT_REG_DAC_BASE 0x00U
#define ANALOGUE_OUTPUT_REG_VREF_CTRL 0x08U
#define ANALOGUE_OUTPUT_REG_POWER_DOWN 0x09U
#define ANALOGUE_OUTPUT_REG_GAIN_CTRL 0x0AU

#define ANALOGUE_OUTPUT_VREF_EXT_BUFFERED 0xFFFFU
#define ANALOGUE_OUTPUT_GAIN_1X 0x0000U
#define ANALOGUE_OUTPUT_PD_OPEN_CIRCUIT 0xF000U

/**-----------------------------------------------------------------------------
 *  Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

typedef struct AnalogueOutputStartupPacket_T
{
    uint8_t bytes[ANALOGUE_OUTPUT_STARTUP_PACKET_SIZE_BYTES];
} AnalogueOutputStartupPacket_T;

_Static_assert( sizeof( AnalogueOutputStartupPacket_T ) == 33U,
                "The complete DAC startup packet must contain eleven three-byte frames" );

/**-----------------------------------------------------------------------------
 *  Public (global) and Extern Variables
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Private (static) Variables
 *------------------------------------------------------------------------------
 */

static AnalogueOutputState_T s_EXEC_ANALOGUE_OUTPUT_State = EXEC_ANALOG_OUTPUT_STATE_UNCONFIGURED;

/**-----------------------------------------------------------------------------
 *  Private (static) Function Prototypes
 *------------------------------------------------------------------------------
 */

static inline uint8_t EXEC_ANALOGUE_OUTPUT_Pack_Command_Byte( uint8_t register_address );
static inline void
EXEC_ANALOGUE_OUTPUT_Prepare_Register_Frame( uint8_t register_address, uint16_t data_word,
                                             AnalogueOutputPreparedFrame_T* prepared_frame );
static uint16_t EXEC_ANALOGUE_OUTPUT_Clamp_And_Scale_Count( float input_voltage_v );

static void
            EXEC_ANALOGUE_OUTPUT_Prepare_Startup_Packet( bool                           use_external_vref,
                                                         AnalogueOutputStartupPacket_T* startup_packet );
static void EXEC_ANALOGUE_OUTPUT_Update_Readiness( void );

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

static inline uint8_t EXEC_ANALOGUE_OUTPUT_Pack_Command_Byte( uint8_t register_address )
{
    /* Pack the DAC register address into the 8-bit command byte.
     * The MCP48 octal DAC expects the 5-bit register address in bits [7:3]
     * of the first byte, so mask to 5 bits then shift left by 3.
     */
    return ( uint8_t )( ( register_address & 0x1FU ) << 3U );
}

static inline void
EXEC_ANALOGUE_OUTPUT_Prepare_Register_Frame( uint8_t register_address, uint16_t data_word,
                                             AnalogueOutputPreparedFrame_T* prepared_frame )
{
    /* Frame layout sent to the DAC over SPI:
     *   byte 0: command byte (register address packed into bits [7:3])
     *   byte 1: data MSB (upper 8 bits of the 16-bit data word)
     *   byte 2: data LSB (lower 8 bits of the 16-bit data word)
     *
     * Data is split into two bytes explicitly to make the SPI payload clear
     * and to avoid endianness assumptions.
     */
    prepared_frame->bytes[0] = EXEC_ANALOGUE_OUTPUT_Pack_Command_Byte( register_address );
    prepared_frame->bytes[1] = ( uint8_t )( ( data_word >> 8U ) & 0xFFU );
    prepared_frame->bytes[2] = ( uint8_t )( data_word & 0xFFU );
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

    // TODO(DEV-80): This assumes an ideal fixed 0-20 V transfer, without actual VREF, analogue
    // gain, offset, or per-channel calibration. Verify whether the MCP48CVB28's 4096-step transfer
    // should scale by 4095 or by 4096 then saturate to 4095; do this in frame preparation, not
    // here.
    float scaled_count = ( clamped_voltage_v / ANALOGUE_OUTPUT_INPUT_MAX_V )
                         * ( float )ANALOGUE_OUTPUT_DAC_MAX_COUNT;
    uint16_t count = ( uint16_t )( scaled_count + 0.5F );

    if ( count > ANALOGUE_OUTPUT_DAC_MAX_COUNT )
    {
        count = ANALOGUE_OUTPUT_DAC_MAX_COUNT;
    }

    return count;
}

static void
EXEC_ANALOGUE_OUTPUT_Prepare_Startup_Packet( bool                           use_external_vref,
                                             AnalogueOutputStartupPacket_T* startup_packet )
{
    // TODO(DEV-80): The DAC powers up at 12-bit mid-scale with channels in normal operation, so
    // this order may drive channels 0-5 before they are zeroed. Verify against the schematic; the
    // safe order is likely power-down all, set VREF/gain, preload zeros, then enable channels 0-5.
    // Use LAT or an external output enable if exposed by the PCB; schematic confirmation is needed.
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

    /* Configure the VREF control word depending on whether an external
     * buffered reference is requested. This updates the placeholder in the
     * first entry of the `frames` array before packing.
     */
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
        AnalogueOutputPreparedFrame_T prepared_frame;

        EXEC_ANALOGUE_OUTPUT_Prepare_Register_Frame( frames[index].register_address,
                                                     frames[index].data_word, &prepared_frame );

        memcpy( &startup_packet->bytes[index * EXEC_ANALOG_OUTPUT_FRAME_SIZE_BYTES],
                prepared_frame.bytes, sizeof( prepared_frame ) );
    }
}

static void EXEC_ANALOGUE_OUTPUT_Update_Readiness( void )
{
    // TODO(DEV-80): Extend the SPI public status API so an initializing transfer that faulted can
    // be distinguished from one that is still busy. HW_SPI_Tx_Is_Complete() conservatively reports
    // false for both states, so this module must otherwise remain INITIALIZING.
    if ( ( s_EXEC_ANALOGUE_OUTPUT_State == EXEC_ANALOG_OUTPUT_STATE_INITIALIZING )
         && HW_SPI_Tx_Is_Complete( ANALOGUE_OUTPUT_SPI_CHANNEL ) )
    {
        s_EXEC_ANALOGUE_OUTPUT_State = EXEC_ANALOG_OUTPUT_STATE_READY;
    }
}

/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */

/**
 * @brief Configure and start the SPI hardware channel dedicated to DAC communication.
 *
 * Intended to only be used for console testing to set up the SPI channel independently
 *
 * Sets up the SPI peripheral with the configuration required by the
 * MCP48CVB28T-20E_ST octal DAC: 8-bit data size, MSB first,
 * CPOL low, CPHA 1 edge.
 *
 * This function must be called once during system initialization to prepare
 * SPI for use before any DAC operations are performed. In the real project,
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
 *     true if SPI configuration and startup completed successfully.
 *     false if hardware configuration or startup failed.
 */
bool EXEC_ANALOGUE_OUTPUT_SPI_Channel_Setup( void )
{
    HWSPIConfig_T configuration = {
        .spi_mode  = SPI_MASTER_MODE,
        .data_size = SPI_SIZE_8_BIT,
        .first_bit = SPI_FIRST_MSB,
        // TODO(DEV-80): Increase SPI_DAC to 45 Mbit/s and validate signal integrity on the final
        // PCB. The current bring-up wiring requires a conservative rate.
        .baud_rate = SPI_BAUD_703KBIT,
        .cpol      = SPI_CPOL_LOW,
        .cpha      = SPI_CPHA_1_EDGE,
        .nss_pin   = GPIO_SPI4_NSS,
    };

    s_EXEC_ANALOGUE_OUTPUT_State = EXEC_ANALOG_OUTPUT_STATE_UNCONFIGURED;

    if ( !HW_SPI_Configure_Channel( ANALOGUE_OUTPUT_SPI_CHANNEL, configuration ) )
    {
        s_EXEC_ANALOGUE_OUTPUT_State = EXEC_ANALOG_OUTPUT_STATE_FAULTED;
        return false;
    }

    if ( !HW_SPI_Start_Channel( ANALOGUE_OUTPUT_SPI_CHANNEL ) )
    {
        s_EXEC_ANALOGUE_OUTPUT_State = EXEC_ANALOG_OUTPUT_STATE_FAULTED;
        return false;
    }

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
 * A successful return means the complete startup packet was accepted and
 * triggered. Use EXEC_ANALOG_OUTPUT_Is_Configured() to determine when the
 * startup transmission has completed and runtime writes are ready.
 *
 * @return true if the complete startup packet was accepted and triggered.
 * @return false if SPI rejected the startup packet.
 */
bool EXEC_ANALOGUE_OUTPUT_Config( bool use_external_vref )
{
    AnalogueOutputStartupPacket_T startup_packet;

    s_EXEC_ANALOGUE_OUTPUT_State = EXEC_ANALOG_OUTPUT_STATE_UNCONFIGURED;
    EXEC_ANALOGUE_OUTPUT_Prepare_Startup_Packet( use_external_vref, &startup_packet );

    if ( !HW_SPI_Load_Tx_Buffer( ANALOGUE_OUTPUT_SPI_CHANNEL, startup_packet.bytes,
                                 sizeof( startup_packet.bytes ) ) )
    {
        s_EXEC_ANALOGUE_OUTPUT_State = EXEC_ANALOG_OUTPUT_STATE_FAULTED;
        return false;
    }

    s_EXEC_ANALOGUE_OUTPUT_State = EXEC_ANALOG_OUTPUT_STATE_INITIALIZING;
    HW_SPI_Tx_Trigger( ANALOGUE_OUTPUT_SPI_CHANNEL );
    EXEC_ANALOGUE_OUTPUT_Update_Readiness();

    return true;
}

bool EXEC_ANALOG_OUTPUT_Is_Configured( void )
{
    return EXEC_ANALOG_OUTPUT_Get_State() == EXEC_ANALOG_OUTPUT_STATE_READY;
}

AnalogueOutputState_T EXEC_ANALOG_OUTPUT_Get_State( void )
{
    EXEC_ANALOGUE_OUTPUT_Update_Readiness();
    return s_EXEC_ANALOGUE_OUTPUT_State;
}

bool EXEC_ANALOG_OUTPUT_Prepare_Frame( uint8_t channel, float input_voltage_v,
                                       AnalogueOutputPreparedFrame_T* prepared_frame )
{
    AnalogueOutputPreparedFrame_T temporary_frame;
    uint16_t                      count;

    if ( prepared_frame == NULL )
    {
        return false;
    }

    if ( channel >= EXEC_ANALOGUE_OUTPUT_CONFIGURED_CHANNEL_COUNT )
    {
        return false;
    }

    if ( !isfinite( input_voltage_v ) )
    {
        return false;
    }

    count = EXEC_ANALOGUE_OUTPUT_Clamp_And_Scale_Count( input_voltage_v );

    EXEC_ANALOGUE_OUTPUT_Prepare_Register_Frame(
        ( uint8_t )( ANALOGUE_OUTPUT_REG_DAC_BASE + channel ), count, &temporary_frame );
    *prepared_frame = temporary_frame;

    return true;
}

bool EXEC_ANALOG_OUTPUT_Batch_Init( AnalogueOutputPreparedBatch_T* prepared_batch )
{
    if ( prepared_batch == NULL )
    {
        return false;
    }

    memset( prepared_batch->bytes, 0, sizeof( prepared_batch->bytes ) );
    prepared_batch->byte_count = 0U;

    return true;
}

bool EXEC_ANALOG_OUTPUT_Batch_Append( AnalogueOutputPreparedBatch_T*       prepared_batch,
                                      const AnalogueOutputPreparedFrame_T* prepared_frame )
{
    if ( ( prepared_batch == NULL ) || ( prepared_frame == NULL ) )
    {
        return false;
    }

    if ( ( prepared_batch->byte_count % EXEC_ANALOG_OUTPUT_FRAME_SIZE_BYTES ) != 0U )
    {
        return false;
    }

    if ( prepared_batch->byte_count
         > ( EXEC_ANALOG_OUTPUT_BATCH_MAX_BYTES - EXEC_ANALOG_OUTPUT_FRAME_SIZE_BYTES ) )
    {
        return false;
    }

    memcpy( &prepared_batch->bytes[prepared_batch->byte_count], prepared_frame->bytes,
            sizeof( *prepared_frame ) );
    prepared_batch->byte_count =
        ( uint8_t )( prepared_batch->byte_count + EXEC_ANALOG_OUTPUT_FRAME_SIZE_BYTES );

    return true;
}

bool EXEC_ANALOG_OUTPUT_Submit_Prepared_Batch( const AnalogueOutputPreparedBatch_T* prepared_batch )
{
    EXEC_ANALOGUE_OUTPUT_Update_Readiness();

    if ( s_EXEC_ANALOGUE_OUTPUT_State != EXEC_ANALOG_OUTPUT_STATE_READY )
    {
        return false;
    }

    if ( prepared_batch == NULL )
    {
        return false;
    }

    if ( ( prepared_batch->byte_count > EXEC_ANALOG_OUTPUT_BATCH_MAX_BYTES )
         || ( ( prepared_batch->byte_count % EXEC_ANALOG_OUTPUT_FRAME_SIZE_BYTES ) != 0U ) )
    {
        return false;
    }

    if ( prepared_batch->byte_count == 0U )
    {
        return true;
    }

    if ( !HW_SPI_Load_Tx_Buffer( ANALOGUE_OUTPUT_SPI_CHANNEL, prepared_batch->bytes,
                                 prepared_batch->byte_count ) )
    {
        return false;
    }

    HW_SPI_Tx_Trigger( ANALOGUE_OUTPUT_SPI_CHANNEL );

    // TODO(DEV-80): Confirm whether LAT0 and LAT1 are connected and use them if simultaneous
    // same-tick application is required. Batching preserves a future LAT implementation without
    // changing the stored prepared-frame format.
    return true;
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
    AnalogueOutputPreparedFrame_T prepared_frame;
    AnalogueOutputPreparedBatch_T prepared_batch;

    if ( !EXEC_ANALOG_OUTPUT_Prepare_Frame( channel, input_voltage_v, &prepared_frame ) )
    {
        return false;
    }

    if ( !EXEC_ANALOG_OUTPUT_Batch_Init( &prepared_batch ) )
    {
        return false;
    }

    if ( !EXEC_ANALOG_OUTPUT_Batch_Append( &prepared_batch, &prepared_frame ) )
    {
        return false;
    }

    return EXEC_ANALOG_OUTPUT_Submit_Prepared_Batch( &prepared_batch );
}
