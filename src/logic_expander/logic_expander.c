/******************************************************************************
 *  File:       logic_expander.c
 *  Author:     Angus Corr
 *  Created:    25-Mar-2026
 *
 *  Description:
 *
 *  Notes:
 *     None
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */
#include "logic_expander.h"
#include "exec_i2c.h"
#include <stdint.h>
#include <stdbool.h>

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */

#define LOGIC_EXPANDER_ACTIVE_BITMASK ( 0x01U )

#define LOGIC_EXPANDER_I2C_ADDR_TABLE_INIT                                                         \
    {                                                                                              \
        0x20U, 0x21U, 0x22U, 0x23U, 0x24U, 0x25U, 0x26U, 0x27U                                     \
    }

#define LOGIC_EXPANDER_INIT_OLATA_TABLE_INIT                                                       \
    {                                                                                              \
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U                                     \
    }
// #define LOGIC_EXPANDER_INIT_OLATB_TABLE_INIT  { 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
// 0x00U }

#define LOGIC_EXPANDER_INIT_OLATB_TABLE_INIT                                                       \
    {                                                                                              \
        0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU                                     \
    }

#define MCP23017_REG_IODIRA ( 0x00U )
#define MCP23017_REG_IODIRB ( 0x01U )
#define MCP23017_REG_IPOLA ( 0x02U )
#define MCP23017_REG_IPOLB ( 0x03U )
#define MCP23017_REG_GPINTENA ( 0x04U )
#define MCP23017_REG_GPINTENB ( 0x05U )
#define MCP23017_REG_DEFVALA ( 0x06U )
#define MCP23017_REG_DEFVALB ( 0x07U )
#define MCP23017_REG_INTCONA ( 0x08U )
#define MCP23017_REG_INTCONB ( 0x09U )
#define MCP23017_REG_IOCON ( 0x0AU )
#define MCP23017_REG_GPPUA ( 0x0CU )
#define MCP23017_REG_GPPUB ( 0x0DU )
#define MCP23017_REG_OLATA ( 0x14U )
#define MCP23017_REG_OLATB ( 0x15U )

#define MCP23017_IOCON_SIMPLE_NO_INTERRUPT ( 0x00U )

#define MCP23017_ALL_OUTPUTS ( 0x00U )
#define MCP23017_POLARITY_NORMAL ( 0x00U )
#define MCP23017_PULLUPS_DISABLED ( 0x00U )
#define MCP23017_INTERRUPTS_DISABLED ( 0x00U )

#define LOGIC_EXPANDER_INTERNAL_SEND_BUSY_RETRY_LIMIT ( 200000U )

/**-----------------------------------------------------------------------------
 *  Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

typedef struct LogicExpanderState_T
{
    uint16_t device_address_7bit;
    uint8_t  olat_a;
    uint8_t  olat_b;
} LogicExpanderState_T;

/**-----------------------------------------------------------------------------
 *  Public (global) and Extern Variables
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Private (static) Variables
 *------------------------------------------------------------------------------
 */

static const uint16_t LOGIC_EXPANDER_I2C_ADDRESSES[LOGIC_EXPANDER_COUNT] =
    LOGIC_EXPANDER_I2C_ADDR_TABLE_INIT;

static const uint8_t LOGIC_EXPANDER_INIT_OLAT_A[LOGIC_EXPANDER_COUNT] =
    LOGIC_EXPANDER_INIT_OLATA_TABLE_INIT;

static const uint8_t LOGIC_EXPANDER_INIT_OLAT_B[LOGIC_EXPANDER_COUNT] =
    LOGIC_EXPANDER_INIT_OLATB_TABLE_INIT;

static LogicExpanderState_T logic_expander_state[LOGIC_EXPANDER_COUNT] = { 0 };
static bool                 logic_expander_ready                       = false;

/**-----------------------------------------------------------------------------
 *  Private (static) Function Prototypes
 *------------------------------------------------------------------------------
 */

static inline bool                  logic_expander_index_is_valid( uint8_t expander_index );
static inline bool                  logic_expander_port_is_valid( LogicExpanderPort_T port );
static inline LogicExpanderStatus_T logic_expander_from_exec_status( EXECI2CStatus_T status );
static LogicExpanderStatus_T
logic_expander_internal_send_with_busy_retry( uint16_t device_address_7bit, const uint8_t* payload,
                                              uint16_t payload_length );
static LogicExpanderStatus_T logic_expander_write_register( uint16_t device_address_7bit,
                                                            uint8_t  register_address,
                                                            uint8_t  register_value );
static LogicExpanderStatus_T logic_expander_write_register_pair( uint16_t device_address_7bit,
                                                                 uint8_t  register_address,
                                                                 uint8_t  first_value,
                                                                 uint8_t  second_value );

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

static inline bool logic_expander_index_is_valid( uint8_t expander_index )
{
    return expander_index < LOGIC_EXPANDER_COUNT;
}

static inline bool logic_expander_index_is_active( uint8_t expander_index )
{
    return ( LOGIC_EXPANDER_ACTIVE_BITMASK & ( 1U << expander_index ) ) != 0U;
}

static inline bool logic_expander_port_is_valid( LogicExpanderPort_T port )
{
    return ( port == LOGIC_EXPANDER_PORT_A ) || ( port == LOGIC_EXPANDER_PORT_B );
}

static inline LogicExpanderStatus_T logic_expander_from_exec_status( EXECI2CStatus_T status )
{
    switch ( status )
    {
        case EXEC_I2C_STATUS_OK:
            return LOGIC_EXPANDER_STATUS_OK;
        case EXEC_I2C_STATUS_BUSY:
            return LOGIC_EXPANDER_STATUS_BUSY;
        case EXEC_I2C_STATUS_INVALID_PARAM:
            return LOGIC_EXPANDER_STATUS_INVALID_PARAM;
        case EXEC_I2C_STATUS_OVERFLOW:
        case EXEC_I2C_STATUS_ERROR:
        default:
            return LOGIC_EXPANDER_STATUS_ERROR;
    }
}

static LogicExpanderStatus_T
logic_expander_internal_send_with_busy_retry( uint16_t device_address_7bit, const uint8_t* payload,
                                              uint16_t payload_length )
{
    for ( uint32_t retry = 0U; retry < LOGIC_EXPANDER_INTERNAL_SEND_BUSY_RETRY_LIMIT; ++retry )
    {
        EXECI2CStatus_T status =
            EXEC_I2C_Internal_Master_Send( device_address_7bit, payload, payload_length );
        if ( status == EXEC_I2C_STATUS_OK )
        {
            return LOGIC_EXPANDER_STATUS_OK;
        }

        if ( status != EXEC_I2C_STATUS_BUSY )
        {
            return logic_expander_from_exec_status( status );
        }
    }

    return LOGIC_EXPANDER_STATUS_BUSY;
}

static LogicExpanderStatus_T logic_expander_write_register( uint16_t device_address_7bit,
                                                            uint8_t  register_address,
                                                            uint8_t  register_value )
{
    uint8_t payload[2] = { register_address, register_value };
    return logic_expander_internal_send_with_busy_retry( device_address_7bit, payload,
                                                         ( uint16_t )sizeof( payload ) );
}

static LogicExpanderStatus_T logic_expander_write_register_pair( uint16_t device_address_7bit,
                                                                 uint8_t  register_address,
                                                                 uint8_t  first_value,
                                                                 uint8_t  second_value )
{
    uint8_t payload[3] = { register_address, first_value, second_value };
    return logic_expander_internal_send_with_busy_retry( device_address_7bit, payload,
                                                         ( uint16_t )sizeof( payload ) );
}

/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */

LogicExpanderStatus_T expander_self_config( void )
{
    for ( uint8_t idx = 0U; idx < LOGIC_EXPANDER_COUNT; ++idx )
    {
        if ( !logic_expander_index_is_active( idx ) )
        {
            continue;
        }

        logic_expander_state[idx].device_address_7bit = LOGIC_EXPANDER_I2C_ADDRESSES[idx];
        logic_expander_state[idx].olat_a              = LOGIC_EXPANDER_INIT_OLAT_A[idx];
        logic_expander_state[idx].olat_b              = LOGIC_EXPANDER_INIT_OLAT_B[idx];

        LogicExpanderStatus_T status =
            logic_expander_write_register( logic_expander_state[idx].device_address_7bit,
                                           MCP23017_REG_IOCON, MCP23017_IOCON_SIMPLE_NO_INTERRUPT );
        if ( status != LOGIC_EXPANDER_STATUS_OK )
        {
            logic_expander_ready = false;
            return status;
        }

        status = logic_expander_write_register_pair( logic_expander_state[idx].device_address_7bit,
                                                     MCP23017_REG_IODIRA, MCP23017_ALL_OUTPUTS,
                                                     MCP23017_ALL_OUTPUTS );
        if ( status != LOGIC_EXPANDER_STATUS_OK )
        {
            logic_expander_ready = false;
            return status;
        }

        status = logic_expander_write_register_pair( logic_expander_state[idx].device_address_7bit,
                                                     MCP23017_REG_IPOLA, MCP23017_POLARITY_NORMAL,
                                                     MCP23017_POLARITY_NORMAL );
        if ( status != LOGIC_EXPANDER_STATUS_OK )
        {
            logic_expander_ready = false;
            return status;
        }

        status = logic_expander_write_register_pair(
            logic_expander_state[idx].device_address_7bit, MCP23017_REG_GPINTENA,
            MCP23017_INTERRUPTS_DISABLED, MCP23017_INTERRUPTS_DISABLED );
        if ( status != LOGIC_EXPANDER_STATUS_OK )
        {
            logic_expander_ready = false;
            return status;
        }

        status = logic_expander_write_register_pair( logic_expander_state[idx].device_address_7bit,
                                                     MCP23017_REG_DEFVALA, 0x00U, 0x00U );
        if ( status != LOGIC_EXPANDER_STATUS_OK )
        {
            logic_expander_ready = false;
            return status;
        }

        status = logic_expander_write_register_pair( logic_expander_state[idx].device_address_7bit,
                                                     MCP23017_REG_INTCONA, 0x00U, 0x00U );
        if ( status != LOGIC_EXPANDER_STATUS_OK )
        {
            logic_expander_ready = false;
            return status;
        }

        status = logic_expander_write_register_pair( logic_expander_state[idx].device_address_7bit,
                                                     MCP23017_REG_GPPUA, MCP23017_PULLUPS_DISABLED,
                                                     MCP23017_PULLUPS_DISABLED );
        if ( status != LOGIC_EXPANDER_STATUS_OK )
        {
            logic_expander_ready = false;
            return status;
        }

        status = logic_expander_write_register_pair(
            logic_expander_state[idx].device_address_7bit, MCP23017_REG_OLATA,
            logic_expander_state[idx].olat_a, logic_expander_state[idx].olat_b );
        if ( status != LOGIC_EXPANDER_STATUS_OK )
        {
            logic_expander_ready = false;
            return status;
        }
    }

    logic_expander_ready = true;
    return LOGIC_EXPANDER_STATUS_OK;
}

LogicExpanderStatus_T expander_load_control_bit( uint8_t expander_index, LogicExpanderPort_T port,
                                                 uint8_t bit_index, bool bit_value )
{
    if ( !logic_expander_index_is_valid( expander_index ) || !logic_expander_port_is_valid( port )
         || ( bit_index >= LOGIC_EXPANDER_PORT_WIDTH_BITS ) )
    {
        return LOGIC_EXPANDER_STATUS_INVALID_PARAM;
    }

    uint8_t* target_register = ( port == LOGIC_EXPANDER_PORT_A )
                                   ? &logic_expander_state[expander_index].olat_a
                                   : &logic_expander_state[expander_index].olat_b;

    uint8_t bit_mask = ( uint8_t )( 1U << bit_index );
    if ( bit_value )
    {
        *target_register |= bit_mask;
    }
    else
    {
        *target_register &= ( uint8_t )~bit_mask;
    }

    return LOGIC_EXPANDER_STATUS_OK;
}

LogicExpanderStatus_T expander_send_control_bits( void )
{
    if ( !logic_expander_ready )
    {
        return LOGIC_EXPANDER_STATUS_NOT_READY;
    }

    for ( uint8_t idx = 0U; idx < LOGIC_EXPANDER_COUNT; ++idx )
    {
        if ( !logic_expander_index_is_active( idx ) )
        {
            continue;
        }

        LogicExpanderStatus_T status = logic_expander_write_register_pair(
            logic_expander_state[idx].device_address_7bit, MCP23017_REG_OLATA,
            logic_expander_state[idx].olat_a, logic_expander_state[idx].olat_b );
        if ( status != LOGIC_EXPANDER_STATUS_OK )
        {
            return status;
        }
    }

    return LOGIC_EXPANDER_STATUS_OK;
}

LogicExpanderStatus_T expander_get_state_snapshot( uint8_t                       expander_index,
                                                   LogicExpanderStateSnapshot_T* out_snapshot )
{
    if ( !logic_expander_index_is_valid( expander_index ) || ( out_snapshot == 0 ) )
    {
        return LOGIC_EXPANDER_STATUS_INVALID_PARAM;
    }

    out_snapshot->device_address_7bit = logic_expander_state[expander_index].device_address_7bit;
    out_snapshot->olat_a              = logic_expander_state[expander_index].olat_a;
    out_snapshot->olat_b              = logic_expander_state[expander_index].olat_b;

    return LOGIC_EXPANDER_STATUS_OK;
}
