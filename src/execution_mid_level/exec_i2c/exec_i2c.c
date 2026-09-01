/******************************************************************************
 *  File:       exec_i2c.c
 *  Author:     Coen Pasitchnyj
 *  Created:    20-Apr-2026
 *
 *  Description:
 *      Mid-level I2C execution layer implementation. Validates I2C configuration
 *      and transfer requests before delegating to the low-level hw_i2c driver.
 *      Provides a simplified API by handling buffer management and parameter validation.
 *
 *  Notes:
 *      - Configuration validation includes mode, speed, transfer path, and address checks
 *      - I2C3 does not support DMA transfers (interrupt-only)
 *      - I2C2 supports both interrupt and DMA transfer paths
 *      - All functions delegate to hw_i2c for actual hardware operations
 *      - Thread-safety must be ensured at higher layers
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include "exec_i2c.h"
#include "hw_i2c.h"
#include "logic_expander.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

typedef struct
{
    bool                   is_configured;
    bool                   is_started;
    EXECI2CChannelConfig_T configuration;
} EXECI2CChannelState_T;

typedef struct
{
    uint8_t voltage_select_bit;
    uint8_t pullup_a0_bit;
    uint8_t pullup_a1_bit;
    uint8_t pullup_enable_bit;
} ExecI2CControlMapping_T;

/**-----------------------------------------------------------------------------
 *  Public (global) and Extern Variables
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Private (static) Variables
 *------------------------------------------------------------------------------
 */

static EXECI2CChannelState_T exec_i2c_channel_state[EXEC_I2C_EXTERNAL_CHANNEL_COUNT] = { 0 };

static const ExecI2CControlMapping_T EXEC_I2C_CONTROL_MAPPING[EXEC_I2C_EXTERNAL_CHANNEL_COUNT] = {
    [EXEC_I2C_CHANNEL_1] =
        {
            .voltage_select_bit = 0U,
            .pullup_a0_bit      = 1U,
            .pullup_a1_bit      = 2U,
            .pullup_enable_bit  = 3U,
        },
    [EXEC_I2C_CHANNEL_2] =
        {
            .voltage_select_bit = 4U,
            .pullup_a0_bit      = 5U,
            .pullup_a1_bit      = 6U,
            .pullup_enable_bit  = 7U,
        },
};

/**-----------------------------------------------------------------------------
 *  Private (static) Function Prototypes
 *------------------------------------------------------------------------------
 */

static bool EXEC_I2C_Apply_Interface_Control( ExecI2CChannel_T channel, ExecI2CVoltage_T voltage,
                                              ExecI2CPullup_T pullup, bool pullup_enabled );
static bool EXEC_I2C_Apply_Safe_State( ExecI2CChannel_T channel );

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

static inline EXECI2CStatus_T EXEC_I2C_From_HW_Status( HWI2CStatus_T status )
{
    switch ( status )
    {
        case HW_I2C_STATUS_OK:
            return EXEC_I2C_STATUS_OK;
        case HW_I2C_STATUS_BUSY:
            return EXEC_I2C_STATUS_BUSY;
        case HW_I2C_STATUS_INVALID_PARAM:
            return EXEC_I2C_STATUS_INVALID_PARAM;
        case HW_I2C_STATUS_NOT_CONFIGURED:
            return EXEC_I2C_STATUS_NOT_CONFIGURED;
        case HW_I2C_STATUS_OVERFLOW:
            return EXEC_I2C_STATUS_OVERFLOW;
        case HW_I2C_STATUS_ERROR:
        default:
            return EXEC_I2C_STATUS_ERROR;
    }
}

static inline bool EXEC_I2C_Is_External_Channel( ExecI2CChannel_T channel )
{
    return ( channel == EXEC_I2C_CHANNEL_1 ) || ( channel == EXEC_I2C_CHANNEL_2 );
}

static EXECI2CChannelState_T* EXEC_I2C_Get_Channel_State( ExecI2CChannel_T channel )
{
    if ( !EXEC_I2C_Is_External_Channel( channel ) )
    {
        return NULL;
    }

    return &exec_i2c_channel_state[channel];
}

static EXECI2CStatus_T EXEC_I2C_Validate_Config( ExecI2CChannel_T              channel,
                                                 const EXECI2CChannelConfig_T* config )
{
    if ( !EXEC_I2C_Is_External_Channel( channel ) || config == NULL )
    {
        return EXEC_I2C_STATUS_INVALID_PARAM;
    }

    /*
     * A disabled request intentionally permits an otherwise zero-initialized
     * configuration. Its remaining fields are not applied.
     */
    if ( !config->is_enabled )
    {
        return EXEC_I2C_STATUS_OK;
    }

    /* I2C mode must be either master or slave */
    if ( ( config->mode != HW_I2C_MODE_MASTER ) && ( config->mode != HW_I2C_MODE_SLAVE ) )
    {
        return EXEC_I2C_STATUS_INVALID_PARAM;
    }

    /* I2C speed must be either 100 kHz or 400 kHz */
    if ( ( config->speed != HW_I2C_SPEED_100KHZ ) && ( config->speed != HW_I2C_SPEED_400KHZ ) )
    {
        return EXEC_I2C_STATUS_INVALID_PARAM;
    }

    /* Own address must be 7 bits max */
    if ( config->own_address_7bit > 0x7FU )
    {
        return EXEC_I2C_STATUS_INVALID_PARAM;
    }

    if ( ( config->voltage >= EXEC_I2C_VOLTAGE_COUNT )
         || ( config->pullup >= EXEC_I2C_PULLUP_COUNT ) )
    {
        return EXEC_I2C_STATUS_INVALID_PARAM;
    }

    return EXEC_I2C_STATUS_OK;
}

static bool EXEC_I2C_Apply_Interface_Control( ExecI2CChannel_T channel, ExecI2CVoltage_T voltage,
                                              ExecI2CPullup_T pullup, bool pullup_enabled )
{
    if ( !EXEC_I2C_Is_External_Channel( channel ) || ( voltage >= EXEC_I2C_VOLTAGE_COUNT )
         || ( pullup >= EXEC_I2C_PULLUP_COUNT ) )
    {
        return false;
    }

    const ExecI2CControlMapping_T* mapping = &EXEC_I2C_CONTROL_MAPPING[channel];

    const bool voltage_select = voltage == EXEC_I2C_VOLTAGE_5V;

    /*
     * Pull-up enum values intentionally encode the 2-bit decoder selection:
     * 0 = 1 k, 1 = 2.2 k, 2 = 4.7 k, 3 = 10 k.
     */
    const bool pullup_a0 = ( ( uint8_t )pullup & 0x01U ) != 0U;
    const bool pullup_a1 = ( ( uint8_t )pullup & 0x02U ) != 0U;

    if ( LOGIC_EXPANDER_Load_Control_Bit( LOGIC_EXPANDER_I2C_AO, LOGIC_EXPANDER_PORT_A,
                                          mapping->voltage_select_bit, voltage_select )
         != LOGIC_EXPANDER_STATUS_OK )
    {
        return false;
    }

    if ( LOGIC_EXPANDER_Load_Control_Bit( LOGIC_EXPANDER_I2C_AO, LOGIC_EXPANDER_PORT_A,
                                          mapping->pullup_a0_bit, pullup_a0 )
         != LOGIC_EXPANDER_STATUS_OK )
    {
        return false;
    }

    if ( LOGIC_EXPANDER_Load_Control_Bit( LOGIC_EXPANDER_I2C_AO, LOGIC_EXPANDER_PORT_A,
                                          mapping->pullup_a1_bit, pullup_a1 )
         != LOGIC_EXPANDER_STATUS_OK )
    {
        return false;
    }

    if ( LOGIC_EXPANDER_Load_Control_Bit( LOGIC_EXPANDER_I2C_AO, LOGIC_EXPANDER_PORT_A,
                                          mapping->pullup_enable_bit, pullup_enabled )
         != LOGIC_EXPANDER_STATUS_OK )
    {
        return false;
    }

    return LOGIC_EXPANDER_Send_Control_Bits() == LOGIC_EXPANDER_STATUS_OK;
}

static bool EXEC_I2C_Apply_Safe_State( ExecI2CChannel_T channel )
{
    return EXEC_I2C_Apply_Interface_Control( channel, EXEC_I2C_VOLTAGE_3V3, EXEC_I2C_PULLUP_1K,
                                             false );
}

/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */

EXECI2CStatus_T EXEC_I2C_Configure_Channel( ExecI2CChannel_T              channel,
                                            const EXECI2CChannelConfig_T* config )
{
    const EXECI2CStatus_T validation_status = EXEC_I2C_Validate_Config( channel, config );

    if ( validation_status != EXEC_I2C_STATUS_OK )
    {
        return validation_status;
    }

    EXECI2CChannelState_T* state = EXEC_I2C_Get_Channel_State( channel );

    if ( state == NULL )
    {
        return EXEC_I2C_STATUS_INVALID_PARAM;
    }

    if ( !config->is_enabled )
    {
        if ( state->is_started )
        {
            const EXECI2CStatus_T stop_status = EXEC_I2C_Stop_Channel( channel );

            if ( stop_status != EXEC_I2C_STATUS_OK )
            {
                return stop_status;
            }
        }
        else
        {
            /* A failed disable must not leave a stopped channel startable. */
            memset( &state->configuration, 0, sizeof( state->configuration ) );
            state->is_configured = false;
            state->is_started    = false;

            if ( !EXEC_I2C_Apply_Safe_State( channel ) )
            {
                return EXEC_I2C_STATUS_ERROR;
            }
        }

        memset( &state->configuration, 0, sizeof( state->configuration ) );
        state->is_configured = false;
        state->is_started    = false;

        return EXEC_I2C_STATUS_OK;
    }

    if ( state->is_started )
    {
        return EXEC_I2C_STATUS_BUSY;
    }

    /*
     * A valid reconfiguration may partially change the external controls or
     * peripheral. Revoke the previous start permission until all steps succeed.
     */
    memset( &state->configuration, 0, sizeof( state->configuration ) );
    state->is_configured = false;
    state->is_started    = false;

    /*
     * Apply static voltage and resistance selection while keeping the pull-up
     * disconnected. Start() connects it after starting the HW peripheral.
     */
    if ( !EXEC_I2C_Apply_Interface_Control( channel, config->voltage, config->pullup, false ) )
    {
        return EXEC_I2C_STATUS_ERROR;
    }

    const HWI2CTransferPath_T transfer_path =
        ( channel == EXEC_I2C_CHANNEL_2 ) ? HW_I2C_TRANSFER_DMA : HW_I2C_TRANSFER_INTERRUPT;

    const HWI2CChannelConfig_T hw_config = {
        .mode             = config->mode,
        .speed            = config->speed,
        .tx_transfer_path = transfer_path,
        .rx_transfer_path = transfer_path,
        .own_address_7bit = config->own_address_7bit,
    };

    const HWI2CStatus_T hw_status =
        HW_I2C_Configure_Channel( ( HWI2CChannel_T )channel, &hw_config );

    if ( hw_status != HW_I2C_STATUS_OK )
    {
        ( void )EXEC_I2C_Apply_Safe_State( channel );
        return EXEC_I2C_From_HW_Status( hw_status );
    }

    state->configuration = *config;
    state->is_configured = true;
    state->is_started    = false;

    return EXEC_I2C_STATUS_OK;
}

EXECI2CStatus_T EXEC_I2C_Start_Channel( ExecI2CChannel_T channel )
{
    EXECI2CChannelState_T* state = EXEC_I2C_Get_Channel_State( channel );

    if ( state == NULL )
    {
        return EXEC_I2C_STATUS_INVALID_PARAM;
    }

    if ( !state->is_configured )
    {
        return EXEC_I2C_STATUS_NOT_CONFIGURED;
    }

    if ( state->is_started )
    {
        return EXEC_I2C_STATUS_BUSY;
    }

    const HWI2CStatus_T hw_status = HW_I2C_Start_Channel( ( HWI2CChannel_T )channel );

    if ( hw_status != HW_I2C_STATUS_OK )
    {
        return EXEC_I2C_From_HW_Status( hw_status );
    }

    /*
     * The MCU peripheral is running before the selected external pull-up is
     * connected.
     */
    if ( !EXEC_I2C_Apply_Interface_Control( channel, state->configuration.voltage,
                                            state->configuration.pullup, true ) )
    {
        const HWI2CStatus_T rollback_status = HW_I2C_Stop_Channel( ( HWI2CChannel_T )channel );

        if ( rollback_status != HW_I2C_STATUS_OK )
        {
            /*
             * The HW peripheral could not be returned to its stopped state.
             * Preserve STARTED so Stop() can be retried.
             */
            state->is_started = true;
        }

        return EXEC_I2C_STATUS_ERROR;
    }

    state->is_started = true;

    return EXEC_I2C_STATUS_OK;
}

EXECI2CStatus_T EXEC_I2C_Stop_Channel( ExecI2CChannel_T channel )
{
    EXECI2CChannelState_T* state = EXEC_I2C_Get_Channel_State( channel );

    if ( state == NULL )
    {
        return EXEC_I2C_STATUS_INVALID_PARAM;
    }

    if ( !state->is_configured )
    {
        return EXEC_I2C_STATUS_NOT_CONFIGURED;
    }

    if ( !state->is_started )
    {
        return EXEC_I2C_STATUS_BUSY;
    }

    /*
     * Disconnect the DUT-facing pull-ups and select the deterministic safe
     * voltage/resistance state before stopping the MCU peripheral.
     */
    if ( !EXEC_I2C_Apply_Safe_State( channel ) )
    {
        return EXEC_I2C_STATUS_ERROR;
    }

    const HWI2CStatus_T hw_status = HW_I2C_Stop_Channel( ( HWI2CChannel_T )channel );

    if ( hw_status != HW_I2C_STATUS_OK )
    {
        /*
         * The external pull-ups are disconnected, but the HW peripheral is
         * still running or its state is uncertain. Preserve STARTED so Stop()
         * can be retried.
         */
        return EXEC_I2C_From_HW_Status( hw_status );
    }

    state->is_started = false;

    return EXEC_I2C_STATUS_OK;
}

bool EXEC_I2C_Is_Channel_Configured( ExecI2CChannel_T channel )
{
    EXECI2CChannelState_T* state = EXEC_I2C_Get_Channel_State( channel );

    if ( state == NULL )
    {
        return false;
    }

    return state->is_configured;
}

bool EXEC_I2C_Is_Channel_Started( ExecI2CChannel_T channel )
{
    EXECI2CChannelState_T* state = EXEC_I2C_Get_Channel_State( channel );

    if ( state == NULL )
    {
        return false;
    }

    return state->is_started;
}

/**
 * @brief Master transmit on an external channel.
 *
 * Sends data to a slave device on the specified channel.
 * Data must be provided directly in the payload; internally handles buffering.
 *
 * The external channel selector is validated here. The low-level enqueue validates
 * the address, payload, length, configured mode, and available queue capacity.
 *
 * @param[in] channel               External execution I2C channel
 * @param[in] device_address_7bit   7-bit slave address
 * @param[in] payload               Data to transmit
 * @param[in] payload_length        Number of bytes to transmit
 *
 * @return true if the complete request was accepted into the driver queue
 * @return false on failure
 */
bool EXEC_I2C_Master_Transmit_External( ExecI2CChannel_T channel, uint16_t device_address_7bit,
                                        const uint8_t* payload, uint16_t payload_length )
{
    if ( !EXEC_I2C_Is_External_Channel( channel ) )
    {
        return false;
    }

    return HW_I2C_Enqueue_Master_Transmit( ( HWI2CChannel_T )channel, device_address_7bit, payload,
                                           payload_length )
           == HW_I2C_STATUS_OK;
}

/**
 * @brief Slave transmit on an external channel.
 *
 * Prepares the channel to respond to a master read request with the provided data.
 *
 * @note Validity checks are minimal. Callers must ensure:
 *       - channel is a valid external execution I2C channel
 *       - channel has been configured via EXEC_I2C_Configure_Channel()
 *       - payload is non-NULL if payload_length > 0
 *       Invalid channel access will result in undefined behavior (no range checking).
 *
 * Caller should call EXEC_I2C_Did_Last_Transfer_Overflow afterwards to check for overflow.
 *
 * @param[in] channel               I2C channel
 * @param[in] payload               Data to transmit when master requests
 * @param[in] payload_length        Number of bytes available to transmit
 *
 * @return true if slave transmit was prepared
 * @return false on failure
 */
bool EXEC_I2C_Slave_Transmit_External( ExecI2CChannel_T channel, const uint8_t* payload,
                                       uint16_t payload_length )
{
    return HW_I2C_Load_Stage_Buffer( ( HWI2CChannel_T )channel, payload, payload_length )
           && HW_I2C_Trigger_Slave_Transmit_External( ( HWI2CChannel_T )channel );
}

/**
 * @brief Initiate master receive on an external I2C channel.
 *
 * Requests data from a slave device on the specified external channel (I2C3 or I2C2).
 * Received data is buffered internally and can be retrieved with
 * EXEC_I2C_Receive_Copy_And_Consume().
 *
 * The external channel selector is validated here. The low-level enqueue validates
 * the address, expected length, configured mode, and available queue capacity.
 *
 * @param[in] channel               External execution I2C channel
 * @param[in] device_address_7bit   7-bit slave address
 * @param[in] expected_length       Number of bytes expected from slave
 *
 * @return true if the complete receive request was accepted into the queue
 * @return false on failure
 */
bool EXEC_I2C_Start_Master_Receive_External( ExecI2CChannel_T channel, uint16_t device_address_7bit,
                                             uint16_t expected_length )
{
    if ( !EXEC_I2C_Is_External_Channel( channel ) )
    {
        return false;
    }

    return HW_I2C_Enqueue_Master_Receive( ( HWI2CChannel_T )channel, device_address_7bit,
                                          expected_length )
           == HW_I2C_STATUS_OK;
}

/**
 * @brief Initiate slave receive on an external channel.
 *
 * Prepares the channel to receive data from a master. Received data
 * is buffered internally and can be retrieved with EXEC_I2C_Receive_Copy_And_Consume().
 *
 * @note Validity checks are minimal. Callers must ensure:
 *       - channel is a valid external execution I2C channel
 *       - channel has been configured via EXEC_I2C_Configure_Channel()
 *       Invalid channel access will result in undefined behavior (no range checking).
 *
 * Caller should call EXEC_I2C_Did_Last_Transfer_Overflow afterwards to check for overflow.
 *
 * @param[in] channel           I2C channel
 * @param[in] expected_length   Number of bytes expected from master
 *
 * @return true if receive was prepared
 * @return false on failure
 */
bool EXEC_I2C_Start_Slave_Receive_External( ExecI2CChannel_T channel, uint16_t expected_length )
{
    return HW_I2C_Trigger_Slave_Receive_External( ( HWI2CChannel_T )channel, expected_length );
}

void EXEC_I2C_Service_Transaction_Queue( ExecI2CChannel_T channel )
{
    HW_I2C_Service_Transaction_Queue( ( HWI2CChannel_T )channel );
}

bool EXEC_I2C_Is_Transaction_Queue_Complete( ExecI2CChannel_T channel )
{
    return HW_I2C_Is_Transaction_Queue_Complete( ( HWI2CChannel_T )channel );
}

EXECI2CStatus_T EXEC_I2C_Get_And_Clear_Transfer_Result( ExecI2CChannel_T channel )
{
    return EXEC_I2C_From_HW_Status(
        HW_I2C_Get_And_Clear_Transfer_Result( ( HWI2CChannel_T )channel ) );
}

EXECI2CStatus_T EXEC_I2C_Recover_Channel( ExecI2CChannel_T channel )
{
    return EXEC_I2C_From_HW_Status( HW_I2C_Recover_Channel( ( HWI2CChannel_T )channel ) );
}

EXECI2CStatus_T EXEC_I2C_Receive_Message_Copy_And_Consume(
    ExecI2CChannel_T channel, uint8_t* result_storage, uint16_t result_storage_capacity,
    HWI2CRxMessageDescriptor_T* descriptor, uint16_t* bytes_copied, uint16_t* required_length )
{
    if ( ( descriptor == NULL ) || ( bytes_copied == NULL ) || ( required_length == NULL ) )
    {
        return EXEC_I2C_STATUS_INVALID_PARAM;
    }

    memset( descriptor, 0, sizeof( *descriptor ) );
    descriptor->transfer_kind = HW_I2C_TRANSFER_KIND_IDLE;
    *bytes_copied             = 0U;
    *required_length          = 0U;

    HW_I2C_Service_Transaction_Queue( ( HWI2CChannel_T )channel );

    HWI2CRxMessagePeek_T message;
    memset( &message, 0, sizeof( message ) );
    if ( !HW_I2C_Peek_Received_Message( ( HWI2CChannel_T )channel, &message ) )
    {
        return EXEC_I2C_STATUS_INVALID_PARAM;
    }

    if ( message.descriptor.transfer_kind == HW_I2C_TRANSFER_KIND_IDLE )
    {
        return EXEC_I2C_STATUS_NO_DATA;
    }

    *descriptor      = message.descriptor;
    *required_length = message.descriptor.length;
    if ( result_storage_capacity < message.descriptor.length )
    {
        return EXEC_I2C_STATUS_BUFFER_TOO_SMALL;
    }

    if ( ( result_storage == NULL ) && ( message.descriptor.length > 0U ) )
    {
        return EXEC_I2C_STATUS_INVALID_PARAM;
    }

    if ( message.first.length > 0U )
    {
        memcpy( result_storage, message.first.data, message.first.length );
    }
    if ( message.second.length > 0U )
    {
        memcpy( &result_storage[message.first.length], message.second.data, message.second.length );
    }

    if ( !HW_I2C_Consume_Received_Message( ( HWI2CChannel_T )channel ) )
    {
        return EXEC_I2C_STATUS_ERROR;
    }

    *bytes_copied = message.descriptor.length;
    return EXEC_I2C_STATUS_OK;
}

/**
 * @brief Copy received data and advance the receive pointer.
 *
 * Copies received data from the internal ring buffer into caller-provided storage,
 * then consumes (advances pointer past) the copied bytes.
 *
 * @note Validity checks are minimal. Callers must ensure:
 *       - channel is a valid external execution I2C channel
 *       - channel has been configured via EXEC_I2C_Configure_Channel()
 *       - result_storage is non-NULL
 *       - bytes_copied is non-NULL
 *       Invalid channel access will result in undefined behavior (no range checking).
 *
 * @param[in]  channel                     I2C channel
 * @param[out] result_storage              Buffer to copy received data into
 * @param[in]  result_storage_capacity     Size of result_storage buffer
 * @param[out] bytes_copied                Number of bytes actually copied
 *
 * @return true if operation succeeded
 * @return false on failure
 */
bool EXEC_I2C_Receive_Copy_And_Consume( ExecI2CChannel_T channel, uint8_t* result_storage,
                                        uint16_t result_storage_capacity, uint16_t* bytes_copied )
{
    if ( bytes_copied == NULL )
    {
        return false;
    }

    HWI2CRxMessageDescriptor_T descriptor;
    uint16_t                   required_length = 0U;
    const EXECI2CStatus_T      status =
        EXEC_I2C_Receive_Message_Copy_And_Consume( channel, result_storage, result_storage_capacity,
                                                   &descriptor, bytes_copied, &required_length );

    return ( status == EXEC_I2C_STATUS_OK ) || ( status == EXEC_I2C_STATUS_NO_DATA );
}

/**
 * @brief Check if the last transfer on the channel resulted in an overflow.
 *
 * Returns true if the ring buffer overflowed during the last receive transfer, indicating
 * that data was lost. This can be used by callers to detect when they are not consuming
 * received data fast enough.
 *
 * @param[in]  channel      I2C channel
 *
 * @return true if overflow was detected
 * @return false otherwise
 */
bool EXEC_I2C_Did_Last_Transfer_Overflow( ExecI2CChannel_T channel )
{
    // Delegate to hw_i2c
    return HW_I2C_Get_Overflow_Status( ( HWI2CChannel_T )channel );
}
