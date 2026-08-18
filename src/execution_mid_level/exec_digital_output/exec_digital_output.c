/******************************************************************************
 *  File:       exec_digital_output.c
 *  Author:     Tim Vogelsang
 *  Created:    25-Mar-2026
 *
 *  Description:
 *      Aggregate lifecycle, safe GPIO state, and Logic Expander voltage
 *      selection for the ten digital-output channels.
 *
 *  Notes:
 *      Direct runtime mask operations intentionally perform no lifecycle
 *      checks. GPIO pin ownership and physical mapping remain in hw_gpio.
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include "exec_digital_output.h"
#include "hw_gpio.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "logic_expander.h"

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */
typedef enum
{
    EXEC_DIGITAL_OUTPUT_STATE_DISABLED = 0,
    EXEC_DIGITAL_OUTPUT_STATE_CONFIGURED,
    EXEC_DIGITAL_OUTPUT_STATE_STARTED
} ExecDigitalOutputLifecycleState_T;

typedef struct
{
    LogicExpanderIndex_T expander;
    LogicExpanderPort_T  port;
    uint8_t              a0_bit;
    uint8_t              a1_bit;
} ExecDigitalOutputControlMapping_T;

/**-----------------------------------------------------------------------------
 *  Public (global) and Extern Variables
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Private (static) Variables
 *------------------------------------------------------------------------------
 */
static ExecDigitalOutputLifecycleState_T exec_digital_output_state =
    EXEC_DIGITAL_OUTPUT_STATE_DISABLED;

static ExecDigitalOutputConfig_T exec_digital_output_configuration;

static const ExecDigitalOutputControlMapping_T
    exec_digital_output_control_mappings[EXEC_DIGITAL_OUTPUT_CHANNEL_COUNT] = {
        /* Channel 1: DO_A0_1 = GPA1, DO_A1_1 = GPA0 */
        { LOGIC_EXPANDER_DO_1, LOGIC_EXPANDER_PORT_A, 1U, 0U },

        /* Channel 2: DO_A0_2 = GPA4, DO_A1_2 = GPA5 */
        { LOGIC_EXPANDER_DO_1, LOGIC_EXPANDER_PORT_A, 4U, 5U },

        /* Channel 3: DO_A0_3 = GPB0, DO_A1_3 = GPB1 */
        { LOGIC_EXPANDER_DO_1, LOGIC_EXPANDER_PORT_B, 0U, 1U },

        /* Channel 4: DO_A0_4 = GPB7, DO_A1_4 = GPB6 */
        { LOGIC_EXPANDER_DO_1, LOGIC_EXPANDER_PORT_B, 7U, 6U },

        /* Channel 5: DO_A0_5 = GPB5, DO_A1_5 = GPB4 */
        { LOGIC_EXPANDER_DO_2, LOGIC_EXPANDER_PORT_B, 5U, 4U },

        /* Channel 6: DO_A0_6 = GPA6, DO_A1_6 = GPA7 */
        { LOGIC_EXPANDER_DO_2, LOGIC_EXPANDER_PORT_A, 6U, 7U },

        /* Channel 7: DO_A0_7 = GPA6, DO_A1_7 = GPA7 */
        { LOGIC_EXPANDER_DO_1, LOGIC_EXPANDER_PORT_A, 6U, 7U },

        /* Channel 8: DO_A0_8 = GPB4, DO_A1_8 = GPB5 */
        { LOGIC_EXPANDER_DO_1, LOGIC_EXPANDER_PORT_B, 4U, 5U },

        /* Channel 9: DO_A0_9 = GPB2, DO_A1_9 = GPB3 */
        { LOGIC_EXPANDER_DO_2, LOGIC_EXPANDER_PORT_B, 2U, 3U },

        /* Channel 10: DO_A0_10 = GPB6, DO_A1_10 = GPB7 */
        { LOGIC_EXPANDER_DO_2, LOGIC_EXPANDER_PORT_B, 6U, 7U },
};

static GPIOOutput_T exec_digital_output_gpio_channels[EXEC_DIGITAL_OUTPUT_CHANNEL_COUNT] = {
    DIGITAL_OUTPUT_0, DIGITAL_OUTPUT_1, DIGITAL_OUTPUT_2, DIGITAL_OUTPUT_3, DIGITAL_OUTPUT_4,
    DIGITAL_OUTPUT_5, DIGITAL_OUTPUT_6, DIGITAL_OUTPUT_7, DIGITAL_OUTPUT_8, DIGITAL_OUTPUT_9,
};

/**-----------------------------------------------------------------------------
 *  Private (static) Function Prototypes
 *------------------------------------------------------------------------------
 */

static bool EXEC_DIGITAL_OUTPUT_Is_Valid_Mode( ExecDigitalOutputMode_T mode );

static bool EXEC_DIGITAL_OUTPUT_Is_Valid_Configuration( const ExecDigitalOutputConfig_T* config );

static bool EXEC_DIGITAL_OUTPUT_Stage_Voltage( ExecDigitalOutputChannel_T channel,
                                               ExecDigitalOutputMode_T    mode );

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

static bool EXEC_DIGITAL_OUTPUT_Is_Valid_Mode( ExecDigitalOutputMode_T mode )
{
    return ( uint32_t )mode < ( uint32_t )EXEC_DIGITAL_OUTPUT_MODE_COUNT;
}

static bool EXEC_DIGITAL_OUTPUT_Is_Valid_Configuration( const ExecDigitalOutputConfig_T* config )
{
    if ( config == NULL )
    {
        return false;
    }

    for ( uint32_t channel = 0U; channel < ( uint32_t )EXEC_DIGITAL_OUTPUT_CHANNEL_COUNT;
          channel++ )
    {
        /*
         * A disabled channel ignores its requested mode because Configure()
         * will apply the channel's safe disabled selection.
         */
        if ( config->channels[channel].is_enabled
             && !EXEC_DIGITAL_OUTPUT_Is_Valid_Mode( config->channels[channel].mode ) )
        {
            return false;
        }
    }

    return true;
}

static bool EXEC_DIGITAL_OUTPUT_Stage_Voltage( ExecDigitalOutputChannel_T channel,
                                               ExecDigitalOutputMode_T    mode )
{
    if ( ( uint32_t )channel >= ( uint32_t )EXEC_DIGITAL_OUTPUT_CHANNEL_COUNT
         || !EXEC_DIGITAL_OUTPUT_Is_Valid_Mode( mode ) )
    {
        return false;
    }

    bool a0_high;
    bool a1_high;

    switch ( mode )
    {
        case EXEC_DIGITAL_OUTPUT_MODE_3V3:
            a0_high = false;
            a1_high = false;
            break;

        case EXEC_DIGITAL_OUTPUT_MODE_5V:
            a0_high = false;
            a1_high = true;
            break;

        case EXEC_DIGITAL_OUTPUT_MODE_12V:
            a0_high = true;
            a1_high = false;
            break;

        case EXEC_DIGITAL_OUTPUT_MODE_24V:
            a0_high = true;
            a1_high = true;
            break;

        default:
            return false;
    }

    const ExecDigitalOutputControlMapping_T* mapping =
        &exec_digital_output_control_mappings[channel];

    if ( LOGIC_EXPANDER_Load_Control_Bit( mapping->expander, mapping->port, mapping->a0_bit,
                                          a0_high )
         != LOGIC_EXPANDER_STATUS_OK )
    {
        return false;
    }

    if ( LOGIC_EXPANDER_Load_Control_Bit( mapping->expander, mapping->port, mapping->a1_bit,
                                          a1_high )
         != LOGIC_EXPANDER_STATUS_OK )
    {
        return false;
    }

    return true;
}
/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */

bool EXEC_DIGITAL_OUTPUT_Configure( const ExecDigitalOutputConfig_T* config )
{
    if ( exec_digital_output_state == EXEC_DIGITAL_OUTPUT_STATE_STARTED )
    {
        return false;
    }

    if ( !EXEC_DIGITAL_OUTPUT_Is_Valid_Configuration( config ) )
    {
        return false;
    }

    /*
     * Hardware configuration begins here. Any subsequent failure must leave
     * the subsystem non-startable.
     */
    exec_digital_output_state = EXEC_DIGITAL_OUTPUT_STATE_DISABLED;

    HW_GPIO_Reset_Many_Pins( exec_digital_output_gpio_channels,
                             ( uint16_t )EXEC_DIGITAL_OUTPUT_CHANNEL_COUNT );

    for ( uint32_t channel = 0U; channel < ( uint32_t )EXEC_DIGITAL_OUTPUT_CHANNEL_COUNT;
          channel++ )
    {
        const ExecDigitalOutputChannelConfig_T* channel_config = &config->channels[channel];

        const ExecDigitalOutputMode_T selected_mode =
            channel_config->is_enabled ? channel_config->mode : EXEC_DIGITAL_OUTPUT_MODE_3V3;

        if ( !EXEC_DIGITAL_OUTPUT_Stage_Voltage( ( ExecDigitalOutputChannel_T )channel,
                                                 selected_mode ) )
        {
            return false;
        }
    }

    /*
     * TODO: Remove this direct send when global configuration commits all
     * staged Logic Expander changes.
     */
    if ( LOGIC_EXPANDER_Send_Control_Bits() != LOGIC_EXPANDER_STATUS_OK )
    {
        return false;
    }

    exec_digital_output_configuration = *config;
    exec_digital_output_state         = EXEC_DIGITAL_OUTPUT_STATE_CONFIGURED;

    return true;
}

bool EXEC_DIGITAL_OUTPUT_Start( void )
{
    if ( exec_digital_output_state != EXEC_DIGITAL_OUTPUT_STATE_CONFIGURED )
    {
        return false;
    }

    GPIOOutput_T high_channels[EXEC_DIGITAL_OUTPUT_CHANNEL_COUNT];
    uint16_t     high_channel_count = 0U;

    for ( uint32_t channel = 0U; channel < ( uint32_t )EXEC_DIGITAL_OUTPUT_CHANNEL_COUNT;
          channel++ )
    {
        const ExecDigitalOutputChannelConfig_T* channel_config =
            &exec_digital_output_configuration.channels[channel];

        if ( channel_config->is_enabled && channel_config->initial_high )
        {
            high_channels[high_channel_count] = exec_digital_output_gpio_channels[channel];

            high_channel_count++;
        }
    }

    if ( high_channel_count > 0U )
    {
        HW_GPIO_Set_Many_Pins( high_channels, high_channel_count );
    }

    exec_digital_output_state = EXEC_DIGITAL_OUTPUT_STATE_STARTED;

    return true;
}

bool EXEC_DIGITAL_OUTPUT_Stop( void )
{
    if ( exec_digital_output_state != EXEC_DIGITAL_OUTPUT_STATE_STARTED )
    {
        return false;
    }

    /*
     * All digital-output signals are returned low in one batched GPIO
     * operation. Unrelated GPIOG pins are not included and remain unchanged.
     */
    HW_GPIO_Reset_Many_Pins( exec_digital_output_gpio_channels,
                             ( uint16_t )EXEC_DIGITAL_OUTPUT_CHANNEL_COUNT );

    exec_digital_output_state = EXEC_DIGITAL_OUTPUT_STATE_CONFIGURED;

    return true;
}

bool EXEC_DIGITAL_OUTPUT_Is_Configured( void )
{
    return exec_digital_output_state == EXEC_DIGITAL_OUTPUT_STATE_CONFIGURED
           || exec_digital_output_state == EXEC_DIGITAL_OUTPUT_STATE_STARTED;
}

bool EXEC_DIGITAL_OUTPUT_Is_Started( void )
{
    return exec_digital_output_state == EXEC_DIGITAL_OUTPUT_STATE_STARTED;
}

/**
 * @brief combines many GPIO's (on the same port) into one pin mask.
 *
 * @param gpio_names   an array of GPIO pin names, all of which are on the same port
 * @param length       the nubmer of GPIOOutput_T in gpio_names
 *
 * @return returns the combined pin mask (uint32_t), if fault return 0xFFFF0000
 *
 * Combines the pinmasks of the gpio_names so that they can be written to the BSR in one step
 * (instead of individually), This is much more efficient and should be used with functions liek
EXEC_DIGITAL_OUTPUT_Set_Output.
 * EXAMPLE: if we want to set both DIGITAL_OUT_CH_0 and DIGITAL_OUT_CH_1 we could write
DigitalOutputPinmask_T p = HW_GPIO_Combine_Port_Pin_Masks({DIGITAL_OUT_CH_0, DIGITAL_OUT_CH_1 }, 2)
if (p.pin_mask == 0xFFFF0000){
    error
} else {
    EXEC_DIGITAL_OUTPUT_Set_Output(p)
}
 * mocked using GoogleMock.
 */
DigitalOutputPinmask_T EXEC_DIGITAL_OUTPUT_Combine_Port_Pin_Masks( GPIOOutput_T* gpio_names,
                                                                   uint8_t       length )
{
    return HW_GPIO_Combine_Port_Pin_Masks( gpio_names, length );
}

/**
 * @brief Sets the state of all digital pins on the Digital GPIO Port (assigned in hw_gpio).
 *
 * @param PinMask   Carrys the information about which pins to set
                    If a lower 16 bit is 1 this sets the associated digital output pin
                    If any bit is 0 this indicates no change
                    0th bit corresponds to digital output 0, 1st bit to output 1 etc
 *
 *
 * This function wraps the HW_GPIO_Set_Output( ... ) function provided by the
 * LL layer. It can be used to set a single output pin or many output pins (on the same port).
 * EXAMPLE: EXEC_DIGITAL_OUTPUT_Set_Output( 0x0000_0020 ) sets LL_GPIO_PIN_5 of the Digital GPIO
Port high
 * EXAMPLE: EXEC_DIGITAL_OUTPUT_Set_Output( 0x0000_0020 | 0x0000_0001 ) sets LL_GPIO_PIN_5 and
LL_GPIO_PIN_1 of the Digital GPIO Port high
 * Setting multiple pins works because LL_GPIO_PIN_5 and LL_GPIO_PIN_1 are just uint32_t
 * in this case likely 0x0000_0020 0x0000_0001, so 0x0000_0021 is written to the BSR register
 * 0x0000_0021 = 0000_0000_0000_0000_0000_0000_0010_0001 setting pins 1 and 5 high
 * Note: This implementation assumes all digital outputs are on the same GPIO port.
 * By doing so, we can set all the outputs in a single hardware access.
 */
void EXEC_DIGITAL_OUTPUT_Set_Output( uint32_t pin_mask )
{
    HW_GPIO_Set_Output( pin_mask );
}

/**
 * @brief Resets the state of all digital pins on the Digital GPIO Port (assigned in hw_gpio).
 *
 * @param PinMask   Carrys the information about which pins to reset
                    If a lower 16 bit is 1 this resets the associated digital output pin
                    If any bit is 0 this indicates no change
                    0th bit corresponds to digital output 0, 1st bit to output 1 etc
 *
 *
 * This function wraps the HW_GPIO_Reset_Output( ... ).
 * It can be used to reset a single output pin or many output pins (on the same port).
 * EXAMPLE: EXEC_DIGITAL_OUTPUT_Reset_Output( 0x0000_0020 ) resets LL_GPIO_PIN_5 of the Digital GPIO
Port high
 * EXAMPLE: EXEC_DIGITAL_OUTPUT_Reset_Output( 0x0000_0020 | 0x0000_0001 ) resets LL_GPIO_PIN_5 and
LL_GPIO_PIN_1 of the Digital GPIO Port high
 * Resetting multiple pins works because LL_GPIO_PIN_5 and LL_GPIO_PIN_1 are just uint32_t
 * in this case likely 0x0000_0020 0x0000_0001, so 0x0000_0021 is written to the BSR register
 * 0x0000_0021 = 0000_0000_0000_0000_0000_0000_0010_0001 resetting pins 1 and 5 high
 * Note: This implementation assumes all digital outputs are on the same GPIO port.
 * By doing so, we can reset all the outputs in a single hardware access.
 */
void EXEC_DIGITAL_OUTPUT_Reset_Output( uint32_t pin_mask )
{
    HW_GPIO_Reset_Output( pin_mask );
}
