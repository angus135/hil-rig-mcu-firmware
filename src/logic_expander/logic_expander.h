/******************************************************************************
 *  File:       logic_expander.h
 *  Author:     Coen Pasitchnyj
 *  Created:    20-Apr-2026
 *
 *  Description:
 *      High-level logic expander interface for MCP23017 I2C GPIO expanders.
 *      Manages up to 8 MCP23017 devices on the internal FMPI2C1 channel.
 *      Provides bit-level control of the 16-bit output ports (OLAT A/B).
 *      Handles device initialization, configuration register setup, and
 *      batched bit updates with retry logic for robustness.
 *
 *  Notes:
 *      - Communicates with MCP23017 devices via FMPI2C1 internal I2C channel
 *      - Default I2C addresses: 0x20-0x27 (configured via device jumpers)
 *      - Active devices controlled via LOGIC_EXPANDER_ACTIVE_BITMASK
 *      - All output bits default to 0x00 (OLAT A) or 0xFF (OLAT B)
 *      - Must call LOGIC_EXPANDER_Self_Config() before any other operations
 *      - Retry logic provides up to 200,000 attempts on busy conditions
 ******************************************************************************/

#ifndef LOGIC_EXPANDER_H
#define LOGIC_EXPANDER_H

#ifdef __cplusplus
extern "C"
{
#endif

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include <stdint.h>
#include <stdbool.h>

/**-----------------------------------------------------------------------------
 *  Public Defines / Macros
 *------------------------------------------------------------------------------
 */

#define LOGIC_EXPANDER_COUNT ( 8U )
#define LOGIC_EXPANDER_PORT_WIDTH_BITS ( 8U )

/**-----------------------------------------------------------------------------
 *  Public Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

typedef enum LogicExpanderIndex_T
{
    LOGIC_EXPANDER_RESERVED_0,
    LOGIC_EXPANDER_RESERVED_1,
    LOGIC_EXPANDER_RESERVED_2,
    LOGIC_EXPANDER_RESERVED_3,
    LOGIC_EXPANDER_RESERVED_4,
    LOGIC_EXPANDER_RESERVED_5,
    LOGIC_EXPANDER_RESERVED_6,
    LOGIC_EXPANDER_RESERVED_7,
} LogicExpanderIndex_T;

typedef enum LogicExpanderPort_T
{
    LOGIC_EXPANDER_PORT_A,
    LOGIC_EXPANDER_PORT_B,
} LogicExpanderPort_T;

typedef enum LogicExpanderStatus_T
{
    LOGIC_EXPANDER_STATUS_OK,
    LOGIC_EXPANDER_STATUS_BUSY,
    LOGIC_EXPANDER_STATUS_ERROR,
    LOGIC_EXPANDER_STATUS_INVALID_PARAM,
    LOGIC_EXPANDER_STATUS_NOT_READY,
} LogicExpanderStatus_T;

typedef struct LogicExpanderStateSnapshot_T
{
    uint16_t device_address_7bit;
    uint8_t  olat_a;
    uint8_t  olat_b;
} LogicExpanderStateSnapshot_T;

typedef enum LogicExpanderI2CStatus_T
{
    LOGIC_EXPANDER_I2C_STATUS_OK,
    LOGIC_EXPANDER_I2C_STATUS_BUSY,
    LOGIC_EXPANDER_I2C_STATUS_ERROR,
    LOGIC_EXPANDER_I2C_STATUS_INVALID_PARAM,
    LOGIC_EXPANDER_I2C_STATUS_OVERFLOW,
} LogicExpanderI2CStatus_T;

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
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
LogicExpanderStatus_T LOGIC_EXPANDER_Self_Config( void );

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
                                                 uint8_t bit_index, bool bit_value );

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
LogicExpanderStatus_T LOGIC_EXPANDER_Send_Control_Bits( void );

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
                                                   LogicExpanderStateSnapshot_T* out_snapshot );

#ifdef __cplusplus
}
#endif

#endif /* LOGIC_EXPANDER_H */
