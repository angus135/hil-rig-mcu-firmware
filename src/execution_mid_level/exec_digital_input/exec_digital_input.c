/******************************************************************************
 *  File:       exec_digital_input.c
 *  Author:     Coen Pasitchnyj
 *  Created:    6-April-2026
 *
 *  Description:
 *      Execution-layer digital input handling for the HIL-RIG. This module
 *      configures execution-time digital input sampling and allows higher level
 *      modules to read digital input states.
 *
 *  Notes:
 *      Configure/Start/Stop own the sampling mask lifecycle. The sampling
 *      path remains a direct GPIO port read and mask operation.
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */
#ifdef TEST_BUILD
#include "tests/exec_digital_input_mocks.h"
#else
#include "main.h"
#endif

#include "exec_digital_input.h"
#include "hw_gpio.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "logic_expander.h"

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */
#define NUM_DIGITAL_INPUTS 10

/**-----------------------------------------------------------------------------
 *  Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

typedef enum
{
    EXEC_DIGITAL_INPUT_STATE_DISABLED = 0,
    EXEC_DIGITAL_INPUT_STATE_CONFIGURED,
    EXEC_DIGITAL_INPUT_STATE_STARTED
} ExecDigitalInputLifecycleState_T;

typedef struct
{
    LogicExpanderIndex_T expander;
    LogicExpanderPort_T  port;
    uint8_t              s0_bit;
    uint8_t              s1_bit;
} ExecDigitalInputControlMapping_T;

/**-----------------------------------------------------------------------------
 *  Public (global) and Extern Variables
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Private (static) Variables
 *------------------------------------------------------------------------------
 */

static const uint8_t DIGITAL_INPUT_PIN_POSITIONS[NUM_DIGITAL_INPUTS] = {
    __builtin_ctz( Digital_Input_0_Pin ), __builtin_ctz( Digital_Input_1_Pin ),
    __builtin_ctz( Digital_Input_2_Pin ), __builtin_ctz( Digital_Input_3_Pin ),
    __builtin_ctz( Digital_Input_4_Pin ), __builtin_ctz( Digital_Input_5_Pin ),
    __builtin_ctz( Digital_Input_6_Pin ), __builtin_ctz( Digital_Input_7_Pin ),
    __builtin_ctz( Digital_Input_8_Pin ), __builtin_ctz( Digital_Input_9_Pin ) };

static ExecDigitalInputLifecycleState_T exec_digital_input_state =
    EXEC_DIGITAL_INPUT_STATE_DISABLED;

static uint32_t exec_digital_input_configured_mask = 0U;
static uint32_t exec_digital_input_active_mask     = 0U;

static const ExecDigitalInputControlMapping_T
    exec_digital_input_control_mappings[EXEC_DIGITAL_INPUT_CHANNEL_COUNT] = {
        /* Channel 1: DI_S0_1 = GPA6, DI_S1_1 = GPA7 */
        { LOGIC_EXPANDER_DI_1, LOGIC_EXPANDER_PORT_A, 6U, 7U },

        /* Channel 2: DI_S0_2 = GPB4, DI_S1_2 = GPB5 */
        { LOGIC_EXPANDER_DI_1, LOGIC_EXPANDER_PORT_B, 4U, 5U },

        /* Channel 3: DI_S0_3 = GPB3, DI_S1_3 = GPB2 */
        { LOGIC_EXPANDER_DI_1, LOGIC_EXPANDER_PORT_B, 3U, 2U },

        /* Channel 4: DI_S0_4 = GPB7, DI_S1_4 = GPB6 */
        { LOGIC_EXPANDER_DI_1, LOGIC_EXPANDER_PORT_B, 7U, 6U },

        /* Channel 5: DI_S0_5 = GPA6, DI_S1_5 = GPA7 */
        { LOGIC_EXPANDER_DI_2, LOGIC_EXPANDER_PORT_A, 6U, 7U },

        /* Channel 6: DI_S0_6 = GPA0, DI_S1_6 = GPA1 */
        { LOGIC_EXPANDER_DI_1, LOGIC_EXPANDER_PORT_A, 0U, 1U },

        /* Channel 7: DI_S0_7 = GPB7, DI_S1_7 = GPB6 */
        { LOGIC_EXPANDER_DI_2, LOGIC_EXPANDER_PORT_B, 7U, 6U },

        /* Channel 8: DI_S0_8 = GPB5, DI_S1_8 = GPB4 */
        { LOGIC_EXPANDER_DI_2, LOGIC_EXPANDER_PORT_B, 5U, 4U },

        /* Channel 9: DI_S0_9 = GPA0, DI_S1_9 = GPA1 */
        { LOGIC_EXPANDER_DI_2, LOGIC_EXPANDER_PORT_A, 0U, 1U },

        /* Channel 10: DI_S0_10 = GPA2, DI_S1_10 = GPA3 */
        { LOGIC_EXPANDER_DI_2, LOGIC_EXPANDER_PORT_A, 2U, 3U },
};

/**-----------------------------------------------------------------------------
 *  Private (static) Function Prototypes
 *------------------------------------------------------------------------------
 */

static bool EXEC_DIGITAL_INPUT_Is_Valid_Configuration( const ExecDigitalInputConfig_T* config );

static bool EXEC_DIGITAL_INPUT_Stage_Mode( ExecDigitalInputChannel_T channel,
                                           ExecDigitalInputMode_T    mode );

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

static bool EXEC_DIGITAL_INPUT_Is_Valid_Configuration( const ExecDigitalInputConfig_T* config )
{
    if ( config == NULL )
    {
        return false;
    }

    for ( uint32_t channel = 0U; channel < ( uint32_t )EXEC_DIGITAL_INPUT_CHANNEL_COUNT; channel++ )
    {
        if ( ( uint32_t )config->channels[channel] >= ( uint32_t )EXEC_DIGITAL_INPUT_MODE_COUNT )
        {
            return false;
        }
    }

    return true;
}

static bool EXEC_DIGITAL_INPUT_Stage_Mode( ExecDigitalInputChannel_T channel,
                                           ExecDigitalInputMode_T    mode )
{
    if ( ( uint32_t )channel >= ( uint32_t )EXEC_DIGITAL_INPUT_CHANNEL_COUNT
         || ( uint32_t )mode >= ( uint32_t )EXEC_DIGITAL_INPUT_MODE_COUNT )
    {
        return false;
    }

    bool s0_high = false;
    bool s1_high = false;

    /*
     * The schematic truth table is written in (S1, S0) order.
     */
    switch ( mode )
    {
        case EXEC_DIGITAL_INPUT_MODE_DISABLED:
        case EXEC_DIGITAL_INPUT_MODE_3V3:
            s0_high = false;
            s1_high = false;
            break;

        case EXEC_DIGITAL_INPUT_MODE_5V:
            s0_high = true;
            s1_high = false;
            break;

        case EXEC_DIGITAL_INPUT_MODE_12V:
            s0_high = false;
            s1_high = true;
            break;

        case EXEC_DIGITAL_INPUT_MODE_24V:
            s0_high = true;
            s1_high = true;
            break;

        case EXEC_DIGITAL_INPUT_MODE_COUNT:
        default:
            return false;
    }

    const ExecDigitalInputControlMapping_T* mapping = &exec_digital_input_control_mappings[channel];

    if ( LOGIC_EXPANDER_Load_Control_Bit( mapping->expander, mapping->port, mapping->s0_bit,
                                          s0_high )
         != LOGIC_EXPANDER_STATUS_OK )
    {
        return false;
    }

    if ( LOGIC_EXPANDER_Load_Control_Bit( mapping->expander, mapping->port, mapping->s1_bit,
                                          s1_high )
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

bool EXEC_DIGITAL_INPUT_Configure( const ExecDigitalInputConfig_T* config )
{
    if ( exec_digital_input_state == EXEC_DIGITAL_INPUT_STATE_STARTED )
    {
        return false;
    }

    if ( !EXEC_DIGITAL_INPUT_Is_Valid_Configuration( config ) )
    {
        return false;
    }

    uint32_t configured_mask = 0U;

    /*
     * Once hardware staging begins, failure must leave the subsystem
     * non-startable.
     */
    exec_digital_input_state = EXEC_DIGITAL_INPUT_STATE_DISABLED;

    exec_digital_input_active_mask = 0U;

    for ( uint32_t channel = 0U; channel < ( uint32_t )EXEC_DIGITAL_INPUT_CHANNEL_COUNT; channel++ )
    {
        const ExecDigitalInputMode_T mode = config->channels[channel];

        if ( !EXEC_DIGITAL_INPUT_Stage_Mode( ( ExecDigitalInputChannel_T )channel, mode ) )
        {
            return false;
        }

        if ( mode != EXEC_DIGITAL_INPUT_MODE_DISABLED )
        {
            configured_mask |= ( uint32_t )1U << DIGITAL_INPUT_PIN_POSITIONS[channel];
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

    exec_digital_input_configured_mask = configured_mask;

    exec_digital_input_state = EXEC_DIGITAL_INPUT_STATE_CONFIGURED;

    return true;
}

bool EXEC_DIGITAL_INPUT_Start( void )
{
    if ( exec_digital_input_state != EXEC_DIGITAL_INPUT_STATE_CONFIGURED )
    {
        return false;
    }

    exec_digital_input_active_mask = exec_digital_input_configured_mask;

    exec_digital_input_state = EXEC_DIGITAL_INPUT_STATE_STARTED;

    return true;
}

bool EXEC_DIGITAL_INPUT_Stop( void )
{
    if ( exec_digital_input_state != EXEC_DIGITAL_INPUT_STATE_STARTED )
    {
        return false;
    }

    exec_digital_input_active_mask = 0U;

    exec_digital_input_state = EXEC_DIGITAL_INPUT_STATE_CONFIGURED;

    return true;
}

bool EXEC_DIGITAL_INPUT_Is_Configured( void )
{
    return exec_digital_input_state == EXEC_DIGITAL_INPUT_STATE_CONFIGURED
           || exec_digital_input_state == EXEC_DIGITAL_INPUT_STATE_STARTED;
}

bool EXEC_DIGITAL_INPUT_Is_Started( void )
{
    return exec_digital_input_state == EXEC_DIGITAL_INPUT_STATE_STARTED;
}

/**
 * @brief Samples all configured digital inputs and writes a masked input word.
 *
 * @param destination  Pointer to destination for sampled input bits.
 *
 * This function reads the raw digital input port state and applies the current
 * enabled-input mask before returning the result through destination.
 */
void EXEC_DIGITAL_INPUT_Sample_All( uint32_t* destination )
{
    /* TODO: Reassess uint16_t after all capture-buffer consumers are updated. */
    const uint32_t port_state = HW_GPIO_Read_All_Digital_Inputs();

    *destination = port_state & exec_digital_input_active_mask;
}
