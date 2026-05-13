/******************************************************************************
 *  File:       logic_expander.c
 *  Author:     Coen Pasitchnyj
 *  Created:    20-Apr-2026
 *
 *  Description:
 *      High-level MCP23017 I2C GPIO expander driver implementation.
 *      Manages initialization, configuration, and bit-level control of up to 8
 *      MCP23017 devices on the internal FMPI2C1 channel. Uses shadow registers
 *      (OLAT A/B) for efficient batch updates and includes retry logic for
 *      robustness during concurrent I2C activity.
 *
 *  Notes:
 *      - All I2C communication via the internal FMPI2C1 channel (high-speed)
 *      - Devices must be pre-addressed via hardware jumpers (A2:A0 pins)
 *      - Active devices controlled via LOGIC_EXPANDER_ACTIVE_BITMASK define
 *      - Configuration registers set all pins as outputs (IODIRA/B = 0x00)
 *      - Retries up to 200,000 times on I2C busy condition
 *      - Thread-safety must be ensured at higher layers
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */
#include "logic_expander.h"
#include "hw_i2c.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

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

static inline bool                  LOGIC_EXPANDER_Index_Is_Valid( LogicExpanderIndex_T expander_index );
static inline bool                  LOGIC_EXPANDER_Index_Is_Active( LogicExpanderIndex_T expander_index );
static inline bool                  LOGIC_EXPANDER_Port_Is_Valid( LogicExpanderPort_T port );
static inline LogicExpanderStatus_T LOGIC_EXPANDER_From_Exec_Status( LogicExpanderI2CStatus_T status );
static LogicExpanderStatus_T
LOGIC_EXPANDER_Internal_Send_With_Busy_Retry( uint16_t device_address_7bit, const uint8_t* payload,
                                              uint16_t payload_length );
static LogicExpanderStatus_T LOGIC_EXPANDER_Write_Register( uint16_t device_address_7bit,
                                                            uint8_t  register_address,
                                                            uint8_t  register_value );
static LogicExpanderStatus_T LOGIC_EXPANDER_Write_Register_Pair( uint16_t device_address_7bit,
                                                                 uint8_t  register_address,
                                                                 uint8_t  first_value,
                                                                 uint8_t  second_value );
static LogicExpanderI2CStatus_T LOGIC_EXPANDER_I2C_Internal_Master_Send( uint16_t device_address_7bit, const uint8_t* payload, uint16_t payload_length );

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

static inline bool LOGIC_EXPANDER_Index_Is_Valid( LogicExpanderIndex_T expander_index )
{
    /* Check if the expander index is within the valid range */
    return expander_index < LOGIC_EXPANDER_COUNT;
}

static inline bool LOGIC_EXPANDER_Index_Is_Active( LogicExpanderIndex_T expander_index )
{
    /* Check if the expander index is active */
    return ( LOGIC_EXPANDER_ACTIVE_BITMASK & ( 1U << ( uint8_t )expander_index ) ) != 0U;
}

static inline bool LOGIC_EXPANDER_Port_Is_Valid( LogicExpanderPort_T port )
{
    /* Expander only has ports A and B*/
    return ( port == LOGIC_EXPANDER_PORT_A ) || ( port == LOGIC_EXPANDER_PORT_B );
}

static inline LogicExpanderStatus_T LOGIC_EXPANDER_From_Exec_Status( LogicExpanderI2CStatus_T status )
{
    switch ( status )
    {
        case LOGIC_EXPANDER_I2C_STATUS_OK:
            return LOGIC_EXPANDER_STATUS_OK;
        case LOGIC_EXPANDER_I2C_STATUS_BUSY:
            return LOGIC_EXPANDER_STATUS_BUSY;
        case LOGIC_EXPANDER_I2C_STATUS_INVALID_PARAM:
            return LOGIC_EXPANDER_STATUS_INVALID_PARAM;
        case LOGIC_EXPANDER_I2C_STATUS_OVERFLOW:
        case LOGIC_EXPANDER_I2C_STATUS_ERROR:
        default:
            return LOGIC_EXPANDER_STATUS_ERROR;
    }
}

/**
 * @brief Retry wrapper for I2C send with busy-wait handling.
 *
 * Attempts to send a payload up to LOGIC_EXPANDER_INTERNAL_SEND_BUSY_RETRY_LIMIT times.
 * If the I2C channel reports BUSY, retries; for other errors, fails immediately.
 */
static LogicExpanderStatus_T
LOGIC_EXPANDER_Internal_Send_With_Busy_Retry( uint16_t device_address_7bit, const uint8_t* payload,
                                              uint16_t payload_length )
{
    /* Retry loop: attempt to send payload up to the retry limit. */
    for ( uint32_t retry = 0U; retry < LOGIC_EXPANDER_INTERNAL_SEND_BUSY_RETRY_LIMIT; ++retry )
    {
        LogicExpanderI2CStatus_T status =
            LOGIC_EXPANDER_I2C_Internal_Master_Send( device_address_7bit, payload, payload_length );

        /* Success: return immediately. */
        if ( status == LOGIC_EXPANDER_I2C_STATUS_OK )
        {
            return LOGIC_EXPANDER_STATUS_OK;
        }

        /* Non-busy error (invalid param, overflow, etc.): fail fast without retrying. */
        if ( status != LOGIC_EXPANDER_I2C_STATUS_BUSY )
        {
            return LOGIC_EXPANDER_From_Exec_Status( status );
        }
        /* BUSY: continue loop and retry. */
    }

    /* Exhausted retries; still busy. */
    return LOGIC_EXPANDER_STATUS_BUSY;
}

/**
 * @brief Write a single 8-bit register to an MCP23017 device.
 *
 * Constructs a 2-byte I2C message [register_address, register_value] and
 * sends via I2C with retry logic for busy conditions.
 */
static LogicExpanderStatus_T LOGIC_EXPANDER_Write_Register( uint16_t device_address_7bit,
                                                            uint8_t  register_address,
                                                            uint8_t  register_value )
{
    /* Build payload: [reg_addr, reg_val] */
    uint8_t payload[2] = { register_address, register_value };
    return LOGIC_EXPANDER_Internal_Send_With_Busy_Retry( device_address_7bit, payload,
                                                         ( uint16_t )sizeof( payload ) );
}

/**
 * @brief Write two consecutive 8-bit registers to an MCP23017 device.
 *
 * Constructs a 3-byte I2C message [register_address, first_value, second_value].
 * Leverages MCP23017's auto-increment feature to update port A and B in one transaction.
 */
static LogicExpanderStatus_T LOGIC_EXPANDER_Write_Register_Pair( uint16_t device_address_7bit,
                                                                 uint8_t  register_address,
                                                                 uint8_t  first_value,
                                                                 uint8_t  second_value )
{
    /* Build payload: [reg_addr, first_val, second_val].
       MCP23017 auto-increments register pointer, so the second value is
       written to (register_address + 1). */
    uint8_t payload[3] = { register_address, first_value, second_value };
    return LOGIC_EXPANDER_Internal_Send_With_Busy_Retry( device_address_7bit, payload,
                                                         ( uint16_t )sizeof( payload ) );
}

/**
 * @brief Initiate an I2C master transmit on the internal FMPI2C1 channel.
 *
 * Validates parameters, loads the payload into the I2C transmit stage buffer,
 * and triggers the master transmit. If the stage buffer is full (another transfer
 * in progress), reports BUSY to allow the caller to retry.
 *
 * @param device_address_7bit  7-bit slave address
 * @param payload              Data to transmit (must not be NULL)
 * @param payload_length       Number of bytes to transmit (must be > 0)
 *
 * @return LOGIC_EXPANDER_I2C_STATUS_OK on success
 * @return LOGIC_EXPANDER_I2C_STATUS_BUSY if stage buffer is full or transfer in progress
 * @return LOGIC_EXPANDER_I2C_STATUS_INVALID_PARAM if address/payload invalid
 * @return LOGIC_EXPANDER_I2C_STATUS_ERROR if trigger fails
 */
LogicExpanderI2CStatus_T LOGIC_EXPANDER_I2C_Internal_Master_Send( uint16_t device_address_7bit, const uint8_t* payload,
                                                                  uint16_t payload_length )
{
    /* Validate parameters: address must be 7-bit, payload non-NULL and non-empty. */
    if ( ( device_address_7bit > 0x7FU ) || ( payload == NULL ) || ( payload_length == 0U ) )
    {
        return LOGIC_EXPANDER_I2C_STATUS_INVALID_PARAM;
    }

    /* Load payload into the I2C transmit stage buffer. Returns false if the
       buffer is already full or a transfer is in progress; in that case,
       signal BUSY to allow the caller to retry. This check is CRITICAL:
       removing it breaks the expander because payloads may be corrupted if
       the stage buffer is overwritten while a transfer is still in flight. */
    bool is_ok = HW_I2C_Load_Stage_Buffer( HW_I2C_CHANNEL_FMPI2C1, payload, payload_length );
    if ( !is_ok )
    {
        return LOGIC_EXPANDER_I2C_STATUS_BUSY;
    }

    /* Payload loaded; trigger the I2C master transmit to the target address. */
    is_ok = HW_I2C_Trigger_Master_Transmit_Internal( device_address_7bit );
    return is_ok ? LOGIC_EXPANDER_I2C_STATUS_OK : LOGIC_EXPANDER_I2C_STATUS_ERROR;
}

/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */

/**
 * @brief Initialize and configure all active MCP23017 devices.
 *
 * Discovers active devices (based on LOGIC_EXPANDER_ACTIVE_BITMASK),
 * sends configuration registers (IODIR, IPOL, GPINTEN, etc.) to set all
 * pins as outputs, and initializes OLAT registers.
 * Must be called before any other operations.
 *
 * @return LOGIC_EXPANDER_STATUS_OK if all devices configured successfully
 * @return LOGIC_EXPANDER_STATUS_BUSY if I2C channel is busy
 * @return LOGIC_EXPANDER_STATUS_ERROR on communication error
 */
LogicExpanderStatus_T LOGIC_EXPANDER_Self_Config( void )
{
    /* Iterate through all available expander slots. */
    for ( uint8_t idx = 0U; idx < LOGIC_EXPANDER_COUNT; ++idx )
    {
        /* Skip devices marked as inactive via LOGIC_EXPANDER_ACTIVE_BITMASK. */
        if ( !LOGIC_EXPANDER_Index_Is_Active( idx ) )
        {
            continue;
        }

        /* Initialize device state: address and shadow registers. */
        logic_expander_state[idx].device_address_7bit = LOGIC_EXPANDER_I2C_ADDRESSES[idx];
        logic_expander_state[idx].olat_a              = LOGIC_EXPANDER_INIT_OLAT_A[idx];
        logic_expander_state[idx].olat_b              = LOGIC_EXPANDER_INIT_OLAT_B[idx];

        /* Configure IOCON register (interrupt settings). */
        LogicExpanderStatus_T status =
            LOGIC_EXPANDER_Write_Register( logic_expander_state[idx].device_address_7bit,
                                           MCP23017_REG_IOCON, MCP23017_IOCON_SIMPLE_NO_INTERRUPT );
        if ( status != LOGIC_EXPANDER_STATUS_OK )
        {
            logic_expander_ready = false;
            return status;
        }

        /* Configure IODIR A/B registers: all pins as outputs (0x00 = output). */
        status = LOGIC_EXPANDER_Write_Register_Pair( logic_expander_state[idx].device_address_7bit,
                                                     MCP23017_REG_IODIRA, MCP23017_ALL_OUTPUTS,
                                                     MCP23017_ALL_OUTPUTS );
        if ( status != LOGIC_EXPANDER_STATUS_OK )
        {
            logic_expander_ready = false;
            return status;
        }

        /* Configure IPOL A/B registers: normal polarity (no inversion). */
        status = LOGIC_EXPANDER_Write_Register_Pair( logic_expander_state[idx].device_address_7bit,
                                                     MCP23017_REG_IPOLA, MCP23017_POLARITY_NORMAL,
                                                     MCP23017_POLARITY_NORMAL );
        if ( status != LOGIC_EXPANDER_STATUS_OK )
        {
            logic_expander_ready = false;
            return status;
        }

        /* Configure GPINTEN A/B registers: disable interrupts. */
        status = LOGIC_EXPANDER_Write_Register_Pair(
            logic_expander_state[idx].device_address_7bit, MCP23017_REG_GPINTENA,
            MCP23017_INTERRUPTS_DISABLED, MCP23017_INTERRUPTS_DISABLED );
        if ( status != LOGIC_EXPANDER_STATUS_OK )
        {
            logic_expander_ready = false;
            return status;
        }

        /* Configure DEFVAL A/B registers: default comparison values. */
        status = LOGIC_EXPANDER_Write_Register_Pair( logic_expander_state[idx].device_address_7bit,
                                                     MCP23017_REG_DEFVALA, 0x00U, 0x00U );
        if ( status != LOGIC_EXPANDER_STATUS_OK )
        {
            logic_expander_ready = false;
            return status;
        }

        /* Configure INTCON A/B registers: compare-to-previous mode. */
        status = LOGIC_EXPANDER_Write_Register_Pair( logic_expander_state[idx].device_address_7bit,
                                                     MCP23017_REG_INTCONA, 0x00U, 0x00U );
        if ( status != LOGIC_EXPANDER_STATUS_OK )
        {
            logic_expander_ready = false;
            return status;
        }

        /* Configure GPPU A/B registers: disable internal pull-ups. */
        status = LOGIC_EXPANDER_Write_Register_Pair( logic_expander_state[idx].device_address_7bit,
                                                     MCP23017_REG_GPPUA, MCP23017_PULLUPS_DISABLED,
                                                     MCP23017_PULLUPS_DISABLED );
        if ( status != LOGIC_EXPANDER_STATUS_OK )
        {
            logic_expander_ready = false;
            return status;
        }

        /* Initialize OLAT A/B registers with shadow state. */
        status = LOGIC_EXPANDER_Write_Register_Pair(
            logic_expander_state[idx].device_address_7bit, MCP23017_REG_OLATA,
            logic_expander_state[idx].olat_a, logic_expander_state[idx].olat_b );
        if ( status != LOGIC_EXPANDER_STATUS_OK )
        {
            logic_expander_ready = false;
            return status;
        }
    }

    /* All active devices configured successfully. */
    logic_expander_ready = true;
    return LOGIC_EXPANDER_STATUS_OK;
}

/**
 * @brief Load a single control bit into the shadow register.
 *
 * Modifies the bit in the local OLAT shadow register (OLAT A or OLAT B)
 * for the specified expander. Does not immediately transmit; use
 * LOGIC_EXPANDER_Send_Control_Bits() to apply changes.
 *
 * @param[in] expander_index  Device index (LogicExpanderIndex_T)
 * @param[in] port            Port A or Port B
 * @param[in] bit_index       Bit position within port (0 to 7)
 * @param[in] bit_value       Value to set (true for 1, false for 0)
 *
 * @return LOGIC_EXPANDER_STATUS_OK on success
 * @return LOGIC_EXPANDER_STATUS_INVALID_PARAM if parameters are out of range
 */
LogicExpanderStatus_T LOGIC_EXPANDER_Load_Control_Bit( LogicExpanderIndex_T expander_index, LogicExpanderPort_T port,
                                                 uint8_t bit_index, bool bit_value )
{
    /* Validate index, port, and bit position. */
    if ( !LOGIC_EXPANDER_Index_Is_Valid( expander_index ) || !LOGIC_EXPANDER_Port_Is_Valid( port )
         || ( bit_index >= LOGIC_EXPANDER_PORT_WIDTH_BITS ) )
    {
        return LOGIC_EXPANDER_STATUS_INVALID_PARAM;
    }

    /* Select the appropriate shadow register (OLAT A or OLAT B). */
    uint8_t* target_register = ( port == LOGIC_EXPANDER_PORT_A )
                                   ? &logic_expander_state[( uint8_t )expander_index].olat_a
                                   : &logic_expander_state[( uint8_t )expander_index].olat_b;

    /* Create a bit mask for the target bit position. */
    uint8_t bit_mask = ( uint8_t )( 1U << bit_index );

    /* Set or clear the bit based on the requested value. */
    if ( bit_value )
    {
        *target_register |= bit_mask;  /* Set bit to 1. */
    }
    else
    {
        *target_register &= ( uint8_t )~bit_mask;  /* Clear bit to 0. */
    }

    return LOGIC_EXPANDER_STATUS_OK;
}

/**
 * @brief Transmit shadow register state to all active devices.
 *
 * Sends all accumulated bit changes (from LOGIC_EXPANDER_Load_Control_Bit)
 * to their respective MCP23017 devices via I2C.
 * Includes retry logic for robustness against temporary I2C bus congestion.
 *
 * @return LOGIC_EXPANDER_STATUS_OK if all devices updated successfully
 * @return LOGIC_EXPANDER_STATUS_BUSY if I2C remains busy after retry limit
 * @return LOGIC_EXPANDER_STATUS_NOT_READY if self-config has not been called
 * @return LOGIC_EXPANDER_STATUS_ERROR on communication error
 */
LogicExpanderStatus_T LOGIC_EXPANDER_Send_Control_Bits( void )
{
    /* Ensure initialization has been completed. */
    if ( !logic_expander_ready )
    {
        return LOGIC_EXPANDER_STATUS_NOT_READY;
    }

    /* Iterate through all expander slots and transmit shadow state. */
    for ( uint8_t idx = 0U; idx < LOGIC_EXPANDER_COUNT; ++idx )
    {
        /* Skip inactive devices. */
        if ( !LOGIC_EXPANDER_Index_Is_Active( idx ) )
        {
            continue;
        }

        /* Send OLAT A and OLAT B shadow registers to the device via I2C. */
        LogicExpanderStatus_T status = LOGIC_EXPANDER_Write_Register_Pair(
            logic_expander_state[idx].device_address_7bit, MCP23017_REG_OLATA,
            logic_expander_state[idx].olat_a, logic_expander_state[idx].olat_b );
        if ( status != LOGIC_EXPANDER_STATUS_OK )
        {
            return status;
        }
    }

    /* All active devices updated successfully. */
    return LOGIC_EXPANDER_STATUS_OK;
}

/**
 * @brief Retrieve the current shadow state of a single expander.
 *
 * Returns a snapshot of the device's OLAT A, OLAT B, and address.
 * Reflects the last known state; not a direct hardware read.
 *
 * @param[in]  expander_index  Device index (LogicExpanderIndex_T)
 * @param[out] out_snapshot    Pointer to snapshot structure to fill
 *
 * @return LOGIC_EXPANDER_STATUS_OK on success
 * @return LOGIC_EXPANDER_STATUS_INVALID_PARAM if parameters are invalid
 */
LogicExpanderStatus_T LOGIC_EXPANDER_Get_State_Snapshot( LogicExpanderIndex_T          expander_index,
                                                   LogicExpanderStateSnapshot_T* out_snapshot )
{
    /* Validate expander index and snapshot pointer. */
    if ( !LOGIC_EXPANDER_Index_Is_Valid( expander_index ) || ( out_snapshot == 0 ) )
    {
        return LOGIC_EXPANDER_STATUS_INVALID_PARAM;
    }

    /* Copy current device address and shadow register state to snapshot. */
    out_snapshot->device_address_7bit = logic_expander_state[( uint8_t )expander_index].device_address_7bit;
    out_snapshot->olat_a              = logic_expander_state[( uint8_t )expander_index].olat_a;
    out_snapshot->olat_b              = logic_expander_state[( uint8_t )expander_index].olat_b;

    return LOGIC_EXPANDER_STATUS_OK;
}
