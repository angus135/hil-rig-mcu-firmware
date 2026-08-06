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
 *      batched bit updates through the internal I2C transaction queue.
 *
 *  Notes:
 *      - Communicates with MCP23017 devices via FMPI2C1 internal I2C channel
 *      - Default I2C addresses: 0x20-0x27 (configured via device jumpers)
 *      - Active devices are selected by the module's role-indexed bitmask
 *      - All output bits default to 0x00 (OLAT A) or 0xFF (OLAT B)
 *      - Must call LOGIC_EXPANDER_Self_Config() before any other operations
 *      - Queue-full is reported immediately so callers can retry without spinning
 *      - All APIs are thread-safe task-context APIs and must not be called from an ISR
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

#define LOGIC_EXPANDER_PORT_WIDTH_BITS ( 8U )

/**-----------------------------------------------------------------------------
 *  Public Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

typedef enum LogicExpanderIndex_T
{
<<<<<<< HEAD
    LOGIC_EXPANDER_DIGITAL_OUTPUT_SELECT = 0,
    LOGIC_EXPANDER_UNASSIGNED_1          = 1,
    LOGIC_EXPANDER_UNASSIGNED_2          = 2,
    LOGIC_EXPANDER_UNASSIGNED_3          = 3,
    LOGIC_EXPANDER_UNASSIGNED_4          = 4,
    LOGIC_EXPANDER_UNASSIGNED_5          = 5,
    LOGIC_EXPANDER_UNASSIGNED_6          = 6,
    LOGIC_EXPANDER_UNASSIGNED_7          = 7,
    LOGIC_EXPANDER_COUNT                 = 8,
=======
    LOGIC_EXPANDER_DEVICE_DI_1 = 0U,
    LOGIC_EXPANDER_DEVICE_DI_2,
    LOGIC_EXPANDER_DEVICE_DO_1,
    LOGIC_EXPANDER_DEVICE_DO_2,
    LOGIC_EXPANDER_DEVICE_PWM_SPI,
    LOGIC_EXPANDER_DEVICE_UART_PWR,
    LOGIC_EXPANDER_DEVICE_I2C_AO,
    LOGIC_EXPANDER_DEVICE_NOT_PRESENT
>>>>>>> 71feb59 (DEV-155 PWM Capture Configuration Refactor)
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
 * @brief Create the mutex protecting shared expander state.
 *
 * Must be called before any other module API. The background task performs this
 * initialisation once at task startup.
 *
 * @return true when the mutex is available.
 */
bool LOGIC_EXPANDER_Init( void );

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
 * @brief Advance non-blocking configuration and observe its physical completion.
 *
 * The background task calls this every 10 ms. It resumes queue submission and
 * observes physical completion without waiting or busy-spinning. Output writes
 * that fail after being accepted are automatically resubmitted on a later call
 * using the most recent explicitly submitted snapshot. A transaction batch that
 * does not complete within 100 ms is recovered and reported as an error.
 */
LogicExpanderStatus_T LOGIC_EXPANDER_Process( void );

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
LogicExpanderStatus_T LOGIC_EXPANDER_Load_Control_Bit( LogicExpanderIndex_T expander_index,
                                                       LogicExpanderPort_T port, uint8_t bit_index,
                                                       bool bit_value );

/**
 * @brief Transmit shadow register state to all active devices.
 *
 * Sends all accumulated bit changes (from LOGIC_EXPANDER_Load_Control_Bit)
 * to their respective MCP23017 devices via I2C.
 * Only dirty devices are enqueued. Queue-full is returned immediately; devices
 * already accepted are tracked until physical completion and unsent devices
 * remain dirty for a later explicit call. Asynchronous failures are retried by
 * LOGIC_EXPANDER_Process() from the last accepted snapshot, without sending
 * newer unsent shadow changes.
 *
 * @return LOGIC_EXPANDER_STATUS_OK if all devices updated successfully
 * @return LOGIC_EXPANDER_STATUS_BUSY if the transaction queue is full
 * @return LOGIC_EXPANDER_STATUS_NOT_READY if self-config has not been called
 * @return LOGIC_EXPANDER_STATUS_ERROR on communication error
 */
LogicExpanderStatus_T LOGIC_EXPANDER_Send_Control_Bits( void );

/**
 * @brief Master transmit on the internal FMPI2C1 channel.
 *
 * Sends data to a slave device on the internal FMPI2C1 channel.
 * Data is copied into one driver-owned transaction queue slot.
 *
 * @param[in] device_address_7bit   7-bit slave address
 * @param[in] payload               Data to transmit
 * @param[in] payload_length        Number of bytes to transmit
 *
 * @return true if the complete transaction was accepted
 * @return false on failure
 */
bool LOGIC_EXPANDER_Master_Transmit_Internal( uint16_t device_address_7bit, const uint8_t* payload,
                                              uint16_t payload_length );

/**
 * @brief Initiate master receive on the internal FMPI2C1 channel.
 *
 * Requests data from a slave device on the internal FMPI2C1 channel.
 * Received data is buffered internally and can be retrieved with
 * EXEC_I2C_Receive_Copy_And_Consume().
 *
 * @param[in] device_address_7bit   7-bit slave address
 * @param[in] expected_length       Number of bytes expected from slave
 *
 * @return true if the complete receive transaction was accepted
 * @return false on failure
 */
bool LOGIC_EXPANDER_Start_Master_Receive_Internal( uint16_t device_address_7bit,
                                                   uint16_t expected_length );

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
LogicExpanderStatus_T
LOGIC_EXPANDER_Get_State_Snapshot( LogicExpanderIndex_T          expander_index,
                                   LogicExpanderStateSnapshot_T* out_snapshot );

#ifdef __cplusplus
}
#endif

#endif /* LOGIC_EXPANDER_H */
