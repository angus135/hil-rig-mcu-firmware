/******************************************************************************
 *  File:       logic_expander.c
 *  Author:     Coen Pasitchnyj
 *  Created:    20-Apr-2026
 *
 *  Description:
 *      High-level MCP23017 I2C GPIO expander driver. Shadow updates and device
 *      configuration are submitted as complete transactions to the internal
 *      FMPI2C1 queue without busy-waiting.
 ******************************************************************************/

#include "logic_expander.h"
#include "hw_i2c.h"
#include "rtos_config.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LOGIC_EXPANDER_INTERNAL_FMPI2C1_OWN_ADDRESS_7BIT ( 0x33U )
#define LOGIC_EXPANDER_DEFAULT_ACTIVE_BITMASK ( 0x7FU )
#define LOGIC_EXPANDER_CONFIG_WRITE_COUNT ( 8U )
#define LOGIC_EXPANDER_TRANSACTION_TIMEOUT_MS ( 100U )

#define MCP23017_REG_IODIRA ( 0x00U )
#define MCP23017_REG_IPOLA ( 0x02U )
#define MCP23017_REG_GPINTENA ( 0x04U )
#define MCP23017_REG_DEFVALA ( 0x06U )
#define MCP23017_REG_INTCONA ( 0x08U )
#define MCP23017_REG_IOCON ( 0x0AU )
#define MCP23017_REG_GPPUA ( 0x0CU )
#define MCP23017_REG_OLATA ( 0x14U )

#define MCP23017_IOCON_SIMPLE_NO_INTERRUPT ( 0x00U )
#define MCP23017_ALL_OUTPUTS ( 0x00U )
#define MCP23017_POLARITY_NORMAL ( 0x00U )
#define MCP23017_PULLUPS_DISABLED ( 0x00U )
#define MCP23017_INTERRUPTS_DISABLED ( 0x00U )

typedef struct LogicExpanderState_T
{
    uint8_t olat_a;
    uint8_t olat_b;
} LogicExpanderState_T;

typedef enum LogicExpanderConfigState_T
{
    LOGIC_EXPANDER_CONFIG_NOT_STARTED = 0,
    LOGIC_EXPANDER_CONFIG_QUEUING,
    LOGIC_EXPANDER_CONFIG_WAITING_FOR_COMPLETION,
    LOGIC_EXPANDER_CONFIG_READY,
    LOGIC_EXPANDER_CONFIG_FAILED,
} LogicExpanderConfigState_T;

typedef struct LogicExpanderConfigWrite_T
{
    uint8_t register_address;
    uint8_t first_value;
    uint8_t second_value;
    bool    is_pair;
} LogicExpanderConfigWrite_T;

// clang-format off
static const uint16_t LOGIC_EXPANDER_I2C_ADDRESSES[LOGIC_EXPANDER_COUNT] = {
    [LOGIC_EXPANDER_DI_1]          = 0x20U,
    [LOGIC_EXPANDER_DI_2]          = 0x21U,
    [LOGIC_EXPANDER_DO_1]          = 0x22U,
    [LOGIC_EXPANDER_DO_2]          = 0x23U,
    [LOGIC_EXPANDER_PWM_SPI]       = 0x24U,
    [LOGIC_EXPANDER_UART_PWR]      = 0x25U,
    [LOGIC_EXPANDER_I2C_AO]        = 0x26U,
};


/* Need to change these to define safe hardware defaults for initialisation*/
static const uint8_t LOGIC_EXPANDER_INIT_OLAT_A[LOGIC_EXPANDER_COUNT] = {
    [LOGIC_EXPANDER_DI_1]          = 0x00U,
    [LOGIC_EXPANDER_DI_2]          = 0x00U,
    [LOGIC_EXPANDER_DO_1]          = 0x00U,
    [LOGIC_EXPANDER_DO_2]          = 0x00U,
    [LOGIC_EXPANDER_PWM_SPI]       = 0x00U,
    [LOGIC_EXPANDER_UART_PWR]      = 0x00U,
    [LOGIC_EXPANDER_I2C_AO]        = 0x00U,
};

static const uint8_t LOGIC_EXPANDER_INIT_OLAT_B[LOGIC_EXPANDER_COUNT] = {
    [LOGIC_EXPANDER_DI_1]          = 0xFFU,
    [LOGIC_EXPANDER_DI_2]          = 0xFFU,
    [LOGIC_EXPANDER_DO_1]          = 0xFFU,
    [LOGIC_EXPANDER_DO_2]          = 0xFFU,
    [LOGIC_EXPANDER_PWM_SPI]       = 0xFFU,
    [LOGIC_EXPANDER_UART_PWR]      = 0xFFU,
    [LOGIC_EXPANDER_I2C_AO]        = 0xFFU,
};
// clang-format on

static const LogicExpanderConfigWrite_T LOGIC_EXPANDER_CONFIG_WRITES[] = {
    { MCP23017_REG_IOCON, MCP23017_IOCON_SIMPLE_NO_INTERRUPT, 0U, false },
    { MCP23017_REG_IODIRA, MCP23017_ALL_OUTPUTS, MCP23017_ALL_OUTPUTS, true },
    { MCP23017_REG_IPOLA, MCP23017_POLARITY_NORMAL, MCP23017_POLARITY_NORMAL, true },
    { MCP23017_REG_GPINTENA, MCP23017_INTERRUPTS_DISABLED, MCP23017_INTERRUPTS_DISABLED, true },
    { MCP23017_REG_DEFVALA, 0U, 0U, true },
    { MCP23017_REG_INTCONA, 0U, 0U, true },
    { MCP23017_REG_GPPUA, MCP23017_PULLUPS_DISABLED, MCP23017_PULLUPS_DISABLED, true },
    { MCP23017_REG_OLATA, 0U, 0U, true },
};

static LogicExpanderState_T logic_expander_state[LOGIC_EXPANDER_COUNT]           = { 0 };
static LogicExpanderState_T logic_expander_submitted_state[LOGIC_EXPANDER_COUNT] = { 0 };
static bool                 logic_expander_ready                                 = false;
static uint8_t              logic_expander_active_bitmask  = LOGIC_EXPANDER_DEFAULT_ACTIVE_BITMASK;
static uint8_t              logic_expander_dirty_bitmask   = 0U;
static uint8_t              logic_expander_pending_bitmask = 0U;
static uint8_t              logic_expander_retry_bitmask   = 0U;
static LogicExpanderConfigState_T logic_expander_config_state = LOGIC_EXPANDER_CONFIG_NOT_STARTED;
static uint8_t                    logic_expander_config_index = 0U;
static uint8_t                    logic_expander_config_write = 0U;
static bool                       logic_expander_deadline_active        = false;
static TickType_t                 logic_expander_transaction_start_tick = 0U;
static SemaphoreHandle_t          logic_expander_mutex                  = NULL;
static StaticSemaphore_t          logic_expander_mutex_storage;

static LogicExpanderStatus_T LOGIC_EXPANDER_Process_Locked( void );
static LogicExpanderStatus_T LOGIC_EXPANDER_Enqueue_Control_Bits( uint8_t* source_bitmask,
                                                                  bool     is_retry );

static void LOGIC_EXPANDER_Arm_Transaction_Deadline( void )
{
    /*
     * Queue acceptance is observable forward progress. Refresh the deadline
     * whenever another transaction is accepted so a large multi-expander
     * batch is not timed out simply because it takes several queue-service
     * cycles to submit. Once no further progress is possible, the unchanged
     * timestamp still provides the normal stall timeout.
     */
    logic_expander_transaction_start_tick = xTaskGetTickCount();
    logic_expander_deadline_active        = true;
}

static void LOGIC_EXPANDER_Disarm_Transaction_Deadline( void )
{
    logic_expander_deadline_active = false;
}

static bool LOGIC_EXPANDER_Transaction_Deadline_Expired( void )
{
    if ( !logic_expander_deadline_active )
    {
        return false;
    }

    const TickType_t elapsed_ticks =
        ( TickType_t )( xTaskGetTickCount() - logic_expander_transaction_start_tick );
    return elapsed_ticks >= pdMS_TO_TICKS( LOGIC_EXPANDER_TRANSACTION_TIMEOUT_MS );
}

static void LOGIC_EXPANDER_Recover_Timed_Out_Channel( void )
{
    ( void )HW_I2C_Recover_Channel( HW_I2C_CHANNEL_FMPI2C1 );
    ( void )HW_I2C_Get_And_Clear_Transfer_Result( HW_I2C_CHANNEL_FMPI2C1 );
    LOGIC_EXPANDER_Disarm_Transaction_Deadline();
}

static bool LOGIC_EXPANDER_Lock( void )
{
    return ( logic_expander_mutex != NULL )
           && ( xSemaphoreTake( logic_expander_mutex, portMAX_DELAY ) == pdTRUE );
}

static void LOGIC_EXPANDER_Unlock( void )
{
    ( void )xSemaphoreGive( logic_expander_mutex );
}

static inline bool LOGIC_EXPANDER_Index_Is_Valid( LogicExpanderIndex_T expander_index )
{
    return ( ( int )expander_index >= 0 ) && ( expander_index < LOGIC_EXPANDER_COUNT );
}

static inline bool LOGIC_EXPANDER_Index_Is_Active( LogicExpanderIndex_T expander_index )
{
    return ( logic_expander_active_bitmask & ( uint8_t )( 1U << ( uint8_t )expander_index ) ) != 0U;
}

static inline bool LOGIC_EXPANDER_Port_Is_Valid( LogicExpanderPort_T port )
{
    return ( port == LOGIC_EXPANDER_PORT_A ) || ( port == LOGIC_EXPANDER_PORT_B );
}

static LogicExpanderStatus_T LOGIC_EXPANDER_From_HW_Status( HWI2CStatus_T status )
{
    switch ( status )
    {
        case HW_I2C_STATUS_OK:
            return LOGIC_EXPANDER_STATUS_OK;
        case HW_I2C_STATUS_BUSY:
            return LOGIC_EXPANDER_STATUS_BUSY;
        case HW_I2C_STATUS_INVALID_PARAM:
            return LOGIC_EXPANDER_STATUS_INVALID_PARAM;
        case HW_I2C_STATUS_NOT_CONFIGURED:
        case HW_I2C_STATUS_OVERFLOW:
        case HW_I2C_STATUS_ERROR:
        default:
            return LOGIC_EXPANDER_STATUS_ERROR;
    }
}

static LogicExpanderI2CStatus_T LOGIC_EXPANDER_I2C_From_HW_Status( HWI2CStatus_T status )
{
    switch ( status )
    {
        case HW_I2C_STATUS_OK:
            return LOGIC_EXPANDER_I2C_STATUS_OK;
        case HW_I2C_STATUS_BUSY:
            return LOGIC_EXPANDER_I2C_STATUS_BUSY;
        case HW_I2C_STATUS_INVALID_PARAM:
            return LOGIC_EXPANDER_I2C_STATUS_INVALID_PARAM;
        case HW_I2C_STATUS_OVERFLOW:
            return LOGIC_EXPANDER_I2C_STATUS_OVERFLOW;
        case HW_I2C_STATUS_NOT_CONFIGURED:
        case HW_I2C_STATUS_ERROR:
        default:
            return LOGIC_EXPANDER_I2C_STATUS_ERROR;
    }
}

static LogicExpanderStatus_T LOGIC_EXPANDER_From_I2C_Status( LogicExpanderI2CStatus_T status )
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

static LogicExpanderI2CStatus_T
LOGIC_EXPANDER_I2C_Internal_Master_Send( uint16_t device_address_7bit, const uint8_t* payload,
                                         uint16_t payload_length )
{
    return LOGIC_EXPANDER_I2C_From_HW_Status( HW_I2C_Enqueue_Master_Transmit(
        HW_I2C_CHANNEL_FMPI2C1, device_address_7bit, payload, payload_length ) );
}

static LogicExpanderStatus_T LOGIC_EXPANDER_Write_Register( uint16_t device_address_7bit,
                                                            uint8_t  register_address,
                                                            uint8_t  register_value )
{
    const uint8_t payload[] = { register_address, register_value };
    return LOGIC_EXPANDER_From_I2C_Status( LOGIC_EXPANDER_I2C_Internal_Master_Send(
        device_address_7bit, payload, ( uint16_t )sizeof( payload ) ) );
}

static LogicExpanderStatus_T LOGIC_EXPANDER_Write_Register_Pair( uint16_t device_address_7bit,
                                                                 uint8_t  register_address,
                                                                 uint8_t  first_value,
                                                                 uint8_t  second_value )
{
    const uint8_t payload[] = { register_address, first_value, second_value };
    return LOGIC_EXPANDER_From_I2C_Status( LOGIC_EXPANDER_I2C_Internal_Master_Send(
        device_address_7bit, payload, ( uint16_t )sizeof( payload ) ) );
}

static LogicExpanderStatus_T LOGIC_EXPANDER_I2C_Internal_Config( void )
{
    return LOGIC_EXPANDER_From_HW_Status(
        HW_I2C_Configure_Internal_FMPI2C1( LOGIC_EXPANDER_INTERNAL_FMPI2C1_OWN_ADDRESS_7BIT ) );
}

static LogicExpanderStatus_T LOGIC_EXPANDER_Enqueue_Next_Config_Write( void )
{
    const LogicExpanderIndex_T expander_index = ( LogicExpanderIndex_T )logic_expander_config_index;
    const LogicExpanderConfigWrite_T* write =
        &LOGIC_EXPANDER_CONFIG_WRITES[logic_expander_config_write];
    const uint16_t address = LOGIC_EXPANDER_I2C_ADDRESSES[logic_expander_config_index];

    if ( logic_expander_config_write == ( LOGIC_EXPANDER_CONFIG_WRITE_COUNT - 1U ) )
    {
        return LOGIC_EXPANDER_Write_Register_Pair( address, MCP23017_REG_OLATA,
                                                   logic_expander_state[expander_index].olat_a,
                                                   logic_expander_state[expander_index].olat_b );
    }

    if ( write->is_pair )
    {
        return LOGIC_EXPANDER_Write_Register_Pair( address, write->register_address,
                                                   write->first_value, write->second_value );
    }

    return LOGIC_EXPANDER_Write_Register( address, write->register_address, write->first_value );
}

static LogicExpanderStatus_T LOGIC_EXPANDER_Queue_Config_Writes( void )
{
    while ( logic_expander_config_index < LOGIC_EXPANDER_COUNT )
    {
        const LogicExpanderIndex_T expander_index =
            ( LogicExpanderIndex_T )logic_expander_config_index;
        if ( !LOGIC_EXPANDER_Index_Is_Active( expander_index ) )
        {
            logic_expander_config_index++;
            logic_expander_config_write = 0U;
            continue;
        }

        const LogicExpanderStatus_T status = LOGIC_EXPANDER_Enqueue_Next_Config_Write();
        if ( status != LOGIC_EXPANDER_STATUS_OK )
        {
            if ( status != LOGIC_EXPANDER_STATUS_BUSY )
            {
                logic_expander_config_state = LOGIC_EXPANDER_CONFIG_FAILED;
            }
            return status;
        }

        LOGIC_EXPANDER_Arm_Transaction_Deadline();
        logic_expander_config_write++;
        if ( logic_expander_config_write >= LOGIC_EXPANDER_CONFIG_WRITE_COUNT )
        {
            logic_expander_config_write = 0U;
            logic_expander_config_index++;
        }
    }

    logic_expander_config_state = LOGIC_EXPANDER_CONFIG_WAITING_FOR_COMPLETION;
    return LOGIC_EXPANDER_STATUS_OK;
}

static LogicExpanderStatus_T LOGIC_EXPANDER_Self_Config_Locked( void )
{
    if ( logic_expander_config_state == LOGIC_EXPANDER_CONFIG_READY )
    {
        return LOGIC_EXPANDER_STATUS_OK;
    }

    if ( ( logic_expander_config_state == LOGIC_EXPANDER_CONFIG_QUEUING )
         || ( logic_expander_config_state == LOGIC_EXPANDER_CONFIG_WAITING_FOR_COMPLETION ) )
    {
        return LOGIC_EXPANDER_Process_Locked();
    }

    if ( logic_expander_config_state == LOGIC_EXPANDER_CONFIG_FAILED )
    {
        ( void )HW_I2C_Recover_Channel( HW_I2C_CHANNEL_FMPI2C1 );
        ( void )HW_I2C_Get_And_Clear_Transfer_Result( HW_I2C_CHANNEL_FMPI2C1 );
        logic_expander_config_state = LOGIC_EXPANDER_CONFIG_NOT_STARTED;
    }

    logic_expander_ready                      = false;
    const LogicExpanderStatus_T config_status = LOGIC_EXPANDER_I2C_Internal_Config();
    if ( config_status != LOGIC_EXPANDER_STATUS_OK )
    {
        logic_expander_config_state = LOGIC_EXPANDER_CONFIG_NOT_STARTED;
        return config_status;
    }

    for ( uint8_t idx = 0U; idx < LOGIC_EXPANDER_COUNT; ++idx )
    {
        logic_expander_state[idx].olat_a    = LOGIC_EXPANDER_INIT_OLAT_A[idx];
        logic_expander_state[idx].olat_b    = LOGIC_EXPANDER_INIT_OLAT_B[idx];
        logic_expander_submitted_state[idx] = logic_expander_state[idx];
    }

    logic_expander_dirty_bitmask   = 0U;
    logic_expander_pending_bitmask = 0U;
    logic_expander_retry_bitmask   = 0U;
    LOGIC_EXPANDER_Disarm_Transaction_Deadline();
    logic_expander_config_index = 0U;
    logic_expander_config_write = 0U;
    logic_expander_config_state = LOGIC_EXPANDER_CONFIG_QUEUING;
    return LOGIC_EXPANDER_Process_Locked();
}

static LogicExpanderStatus_T LOGIC_EXPANDER_Process_Locked( void )
{
    if ( logic_expander_config_state == LOGIC_EXPANDER_CONFIG_FAILED )
    {
        return LOGIC_EXPANDER_STATUS_ERROR;
    }
    if ( logic_expander_config_state == LOGIC_EXPANDER_CONFIG_NOT_STARTED )
    {
        return LOGIC_EXPANDER_STATUS_NOT_READY;
    }

    HW_I2C_Service_Transaction_Queue( HW_I2C_CHANNEL_FMPI2C1 );

    if ( logic_expander_config_state == LOGIC_EXPANDER_CONFIG_QUEUING )
    {
        const LogicExpanderStatus_T queue_status = LOGIC_EXPANDER_Queue_Config_Writes();
        if ( queue_status != LOGIC_EXPANDER_STATUS_OK )
        {
            if ( ( queue_status == LOGIC_EXPANDER_STATUS_BUSY )
                 && LOGIC_EXPANDER_Transaction_Deadline_Expired() )
            {
                LOGIC_EXPANDER_Recover_Timed_Out_Channel();
                logic_expander_config_state = LOGIC_EXPANDER_CONFIG_FAILED;
                logic_expander_ready        = false;
                return LOGIC_EXPANDER_STATUS_ERROR;
            }
            return queue_status;
        }
    }

    if ( logic_expander_config_state == LOGIC_EXPANDER_CONFIG_WAITING_FOR_COMPLETION )
    {
        if ( !HW_I2C_Is_Transaction_Queue_Complete( HW_I2C_CHANNEL_FMPI2C1 ) )
        {
            if ( LOGIC_EXPANDER_Transaction_Deadline_Expired() )
            {
                LOGIC_EXPANDER_Recover_Timed_Out_Channel();
                logic_expander_config_state = LOGIC_EXPANDER_CONFIG_FAILED;
                logic_expander_ready        = false;
                return LOGIC_EXPANDER_STATUS_ERROR;
            }
            return LOGIC_EXPANDER_STATUS_BUSY;
        }

        LOGIC_EXPANDER_Disarm_Transaction_Deadline();
        const HWI2CStatus_T transfer_result =
            HW_I2C_Get_And_Clear_Transfer_Result( HW_I2C_CHANNEL_FMPI2C1 );
        if ( transfer_result != HW_I2C_STATUS_OK )
        {
            logic_expander_config_state = LOGIC_EXPANDER_CONFIG_FAILED;
            logic_expander_ready        = false;
            return LOGIC_EXPANDER_From_HW_Status( transfer_result );
        }

        logic_expander_config_state = LOGIC_EXPANDER_CONFIG_READY;
        logic_expander_ready        = true;
        return LOGIC_EXPANDER_STATUS_OK;
    }

    if ( logic_expander_config_state == LOGIC_EXPANDER_CONFIG_READY )
    {
        if ( logic_expander_pending_bitmask != 0U )
        {
            if ( !HW_I2C_Is_Transaction_Queue_Complete( HW_I2C_CHANNEL_FMPI2C1 ) )
            {
                if ( LOGIC_EXPANDER_Transaction_Deadline_Expired() )
                {
                    const uint8_t timed_out_bitmask = logic_expander_pending_bitmask;
                    LOGIC_EXPANDER_Recover_Timed_Out_Channel();
                    logic_expander_retry_bitmask |= timed_out_bitmask;
                    logic_expander_pending_bitmask = 0U;
                    return LOGIC_EXPANDER_STATUS_ERROR;
                }
                return LOGIC_EXPANDER_STATUS_OK;
            }

            LOGIC_EXPANDER_Disarm_Transaction_Deadline();
            const HWI2CStatus_T transfer_result =
                HW_I2C_Get_And_Clear_Transfer_Result( HW_I2C_CHANNEL_FMPI2C1 );
            if ( transfer_result != HW_I2C_STATUS_OK )
            {
                logic_expander_retry_bitmask |= logic_expander_pending_bitmask;
                logic_expander_pending_bitmask = 0U;
                return LOGIC_EXPANDER_From_HW_Status( transfer_result );
            }

            logic_expander_pending_bitmask = 0U;
        }

        if ( logic_expander_retry_bitmask != 0U )
        {
            return LOGIC_EXPANDER_Enqueue_Control_Bits( &logic_expander_retry_bitmask, true );
        }
        return LOGIC_EXPANDER_STATUS_OK;
    }

    return LOGIC_EXPANDER_STATUS_NOT_READY;
}

static LogicExpanderStatus_T
LOGIC_EXPANDER_Load_Control_Bit_Locked( LogicExpanderIndex_T expander_index,
                                        LogicExpanderPort_T port, uint8_t bit_index,
                                        bool bit_value )
{
    if ( !LOGIC_EXPANDER_Index_Is_Valid( expander_index ) || !LOGIC_EXPANDER_Port_Is_Valid( port )
         || ( bit_index >= LOGIC_EXPANDER_PORT_WIDTH_BITS ) )
    {
        return LOGIC_EXPANDER_STATUS_INVALID_PARAM;
    }

    uint8_t*      target_register = ( port == LOGIC_EXPANDER_PORT_A )
                                        ? &logic_expander_state[expander_index].olat_a
                                        : &logic_expander_state[expander_index].olat_b;
    const uint8_t old_value       = *target_register;
    const uint8_t bit_mask        = ( uint8_t )( 1U << bit_index );
    if ( bit_value )
    {
        *target_register |= bit_mask;
    }
    else
    {
        *target_register &= ( uint8_t )~bit_mask;
    }

    if ( *target_register != old_value )
    {
        logic_expander_dirty_bitmask |= ( uint8_t )( 1U << ( uint8_t )expander_index );
    }

    return LOGIC_EXPANDER_STATUS_OK;
}

static LogicExpanderStatus_T LOGIC_EXPANDER_Enqueue_Control_Bits( uint8_t* source_bitmask,
                                                                  bool     is_retry )
{
    for ( uint8_t idx = 0U; idx < LOGIC_EXPANDER_COUNT; ++idx )
    {
        const uint8_t expander_bit = ( uint8_t )( 1U << idx );
        if ( ( *source_bitmask & expander_bit ) == 0U
             || !LOGIC_EXPANDER_Index_Is_Active( ( LogicExpanderIndex_T )idx ) )
        {
            continue;
        }

        const LogicExpanderState_T* submitted_state =
            is_retry ? &logic_expander_submitted_state[idx] : &logic_expander_state[idx];
        const LogicExpanderStatus_T status = LOGIC_EXPANDER_Write_Register_Pair(
            LOGIC_EXPANDER_I2C_ADDRESSES[idx], MCP23017_REG_OLATA, submitted_state->olat_a,
            submitted_state->olat_b );
        if ( status != LOGIC_EXPANDER_STATUS_OK )
        {
            return status;
        }

        LOGIC_EXPANDER_Arm_Transaction_Deadline();
        if ( !is_retry )
        {
            logic_expander_submitted_state[idx] = logic_expander_state[idx];
            logic_expander_retry_bitmask &= ( uint8_t )~expander_bit;
        }
        *source_bitmask &= ( uint8_t )~expander_bit;
        logic_expander_pending_bitmask |= expander_bit;
    }

    return LOGIC_EXPANDER_STATUS_OK;
}

static LogicExpanderStatus_T LOGIC_EXPANDER_Send_Control_Bits_Locked( void )
{
    if ( !logic_expander_ready )
    {
        return LOGIC_EXPANDER_STATUS_NOT_READY;
    }

    return LOGIC_EXPANDER_Enqueue_Control_Bits( &logic_expander_dirty_bitmask, false );
}

static LogicExpanderStatus_T
LOGIC_EXPANDER_Get_State_Snapshot_Locked( LogicExpanderIndex_T          expander_index,
                                          LogicExpanderStateSnapshot_T* out_snapshot )
{
    if ( !LOGIC_EXPANDER_Index_Is_Valid( expander_index ) || ( out_snapshot == NULL ) )
    {
        return LOGIC_EXPANDER_STATUS_INVALID_PARAM;
    }

    out_snapshot->device_address_7bit = LOGIC_EXPANDER_I2C_ADDRESSES[expander_index];
    out_snapshot->olat_a              = logic_expander_state[expander_index].olat_a;
    out_snapshot->olat_b              = logic_expander_state[expander_index].olat_b;
    return LOGIC_EXPANDER_STATUS_OK;
}

bool LOGIC_EXPANDER_Init( void )
{
    if ( logic_expander_mutex == NULL )
    {
        logic_expander_mutex = xSemaphoreCreateMutexStatic( &logic_expander_mutex_storage );
    }

    return logic_expander_mutex != NULL;
}

LogicExpanderStatus_T LOGIC_EXPANDER_Self_Config( void )
{
    if ( !LOGIC_EXPANDER_Lock() )
    {
        return LOGIC_EXPANDER_STATUS_ERROR;
    }

    const LogicExpanderStatus_T status = LOGIC_EXPANDER_Self_Config_Locked();
    LOGIC_EXPANDER_Unlock();
    return status;
}

LogicExpanderStatus_T LOGIC_EXPANDER_Process( void )
{
    if ( !LOGIC_EXPANDER_Lock() )
    {
        return LOGIC_EXPANDER_STATUS_ERROR;
    }

    const LogicExpanderStatus_T status = LOGIC_EXPANDER_Process_Locked();
    LOGIC_EXPANDER_Unlock();
    return status;
}

LogicExpanderStatus_T LOGIC_EXPANDER_Load_Control_Bit( LogicExpanderIndex_T expander_index,
                                                       LogicExpanderPort_T port, uint8_t bit_index,
                                                       bool bit_value )
{
    if ( !LOGIC_EXPANDER_Lock() )
    {
        return LOGIC_EXPANDER_STATUS_ERROR;
    }

    const LogicExpanderStatus_T status =
        LOGIC_EXPANDER_Load_Control_Bit_Locked( expander_index, port, bit_index, bit_value );
    LOGIC_EXPANDER_Unlock();
    return status;
}

LogicExpanderStatus_T LOGIC_EXPANDER_Send_Control_Bits( void )
{
    if ( !LOGIC_EXPANDER_Lock() )
    {
        return LOGIC_EXPANDER_STATUS_ERROR;
    }

    const LogicExpanderStatus_T status = LOGIC_EXPANDER_Send_Control_Bits_Locked();
    LOGIC_EXPANDER_Unlock();
    return status;
}

LogicExpanderStatus_T
LOGIC_EXPANDER_Get_State_Snapshot( LogicExpanderIndex_T          expander_index,
                                   LogicExpanderStateSnapshot_T* out_snapshot )
{
    if ( !LOGIC_EXPANDER_Lock() )
    {
        return LOGIC_EXPANDER_STATUS_ERROR;
    }

    const LogicExpanderStatus_T status =
        LOGIC_EXPANDER_Get_State_Snapshot_Locked( expander_index, out_snapshot );
    LOGIC_EXPANDER_Unlock();
    return status;
}

bool LOGIC_EXPANDER_Master_Transmit_Internal( uint16_t device_address_7bit, const uint8_t* payload,
                                              uint16_t payload_length )
{
    if ( !LOGIC_EXPANDER_Lock() )
    {
        return false;
    }

    const bool accepted = HW_I2C_Enqueue_Master_Transmit(
                              HW_I2C_CHANNEL_FMPI2C1, device_address_7bit, payload, payload_length )
                          == HW_I2C_STATUS_OK;
    LOGIC_EXPANDER_Unlock();
    return accepted;
}

bool LOGIC_EXPANDER_Start_Master_Receive_Internal( uint16_t device_address_7bit,
                                                   uint16_t expected_length )
{
    if ( !LOGIC_EXPANDER_Lock() )
    {
        return false;
    }

    const bool accepted = HW_I2C_Enqueue_Master_Receive( HW_I2C_CHANNEL_FMPI2C1,
                                                         device_address_7bit, expected_length )
                          == HW_I2C_STATUS_OK;
    LOGIC_EXPANDER_Unlock();
    return accepted;
}
