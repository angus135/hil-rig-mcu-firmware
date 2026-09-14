/******************************************************************************
 *  File:       execution_measurement_adapters.c
 *  Author:     Callum Rafferty
 *  Created:    13/09/2026
 *
 *  Description:
 *      Thin execution-time adapters for prevalidated measurement calls.
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include "execution_measurement_adapters.h"

#include "exec_analogue_input.h"
#include "exec_can.h"
#include "exec_digital_input.h"
#include "exec_pwm_capture.h"
#include "exec_spi.h"
#include "exec_uart.h"
#include "flash_manager.h"

#include <stddef.h>
#include <stdint.h>
#ifndef TEST_BUILD
#include "stm32f4xx.h"
#endif

/**-----------------------------------------------------------------------------
 *  Defines / Macros / Asserts
 *------------------------------------------------------------------------------
 */

#define EXECUTION_MEASUREMENT_ADAPTER_COUNT                                                        \
    ( 2U + EXEC_PWM_CAPTURE_CHANNEL_COUNT + EXEC_UART_CHANNEL_COUNT + EXEC_SPI_CHANNEL_COUNT       \
      + EXEC_CAN_CHANNEL_COUNT )
#define EXECUTION_MEASUREMENT_CHANNEL_UNUSED ( 0U )
#define EXECUTION_MEASUREMENT_CHANNEL_BIT( channel ) ( UINT32_C( 1 ) << ( channel ) )

#if defined( __cplusplus )
static_assert( EXEC_SPI_MAX_RX_CHUNK_SIZE <= UINT16_MAX,
               "SPI RX chunks must fit the Flash result length contract" );
static_assert( EXEC_UART_MAX_CHUNK_SIZE <= UINT16_MAX,
               "UART RX chunks must fit the Flash result length contract" );
static_assert( ( EXEC_CAN_MAX_BATCH_SIZE * sizeof( EXEC_CAN_Packet_T ) ) <= UINT16_MAX,
               "CAN RX batches must fit the Flash result length contract" );
#else
_Static_assert( EXEC_SPI_MAX_RX_CHUNK_SIZE <= UINT16_MAX,
                "SPI RX chunks must fit the Flash result length contract" );
_Static_assert( EXEC_UART_MAX_CHUNK_SIZE <= UINT16_MAX,
                "UART RX chunks must fit the Flash result length contract" );
_Static_assert( ( EXEC_CAN_MAX_BATCH_SIZE * sizeof( EXEC_CAN_Packet_T ) ) <= UINT16_MAX,
                "CAN RX batches must fit the Flash result length contract" );
#endif

/**-----------------------------------------------------------------------------
 *  Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

typedef bool ( *ExecutionMeasurementAdapter_T )( uint8_t channel, uint32_t timestamp,
                                                 BaseType_t* higher_priority_task_woken );

typedef struct
{
    ExecutionMeasurementAdapter_T adapter;
    ExecutionMeasurementType_T    type;
    uint8_t                       channel;
} ExecutionMeasurementDispatchEntry_T;

/**-----------------------------------------------------------------------------
 *  Private (static) Variables
 *------------------------------------------------------------------------------
 */

static ExecutionMeasurementDispatchEntry_T
               active_measurement_adapters[EXECUTION_MEASUREMENT_ADAPTER_COUNT] = { 0 };
static uint8_t active_measurement_count                                         = 0U;
static volatile ExecutionMeasurementTiming_T
    execution_measurement_timing[EXECUTION_MEASUREMENT_COUNT] = { 0 };

/**-----------------------------------------------------------------------------
 *  Private (static) Function Prototypes
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */

void EXECUTION_MEASUREMENT_ADAPTER_Prepare(
    const ExecutionMeasurementConfiguration_T* configuration )
{
    active_measurement_count = 0U;

    if ( configuration->analogue_input_enabled )
    {
        active_measurement_adapters[active_measurement_count++] =
            ( ExecutionMeasurementDispatchEntry_T ){
                .adapter = EXECUTION_MEASUREMENT_ADAPTER_SampleAnalogueInput,
                .type    = EXECUTION_MEASUREMENT_ANALOGUE_INPUT,
                .channel = EXECUTION_MEASUREMENT_CHANNEL_UNUSED,
            };
    }

    if ( configuration->digital_input_enabled )
    {
        active_measurement_adapters[active_measurement_count++] =
            ( ExecutionMeasurementDispatchEntry_T ){
                .adapter = EXECUTION_MEASUREMENT_ADAPTER_SampleDigitalInput,
                .type    = EXECUTION_MEASUREMENT_DIGITAL_INPUT,
                .channel = EXECUTION_MEASUREMENT_CHANNEL_UNUSED,
            };
    }

    for ( uint8_t channel = 0U; channel < EXEC_PWM_CAPTURE_CHANNEL_COUNT; channel++ )
    {
        if ( ( configuration->pwm_capture_enabled_mask
               & EXECUTION_MEASUREMENT_CHANNEL_BIT( channel ) )
             != 0U )
        {
            active_measurement_adapters[active_measurement_count++] =
                ( ExecutionMeasurementDispatchEntry_T ){
                    .adapter = EXECUTION_MEASUREMENT_ADAPTER_SamplePwmCapture,
                    .type    = EXECUTION_MEASUREMENT_PWM_CAPTURE,
                    .channel = channel,
                };
        }
    }

    for ( uint8_t channel = 0U; channel < EXEC_UART_CHANNEL_COUNT; channel++ )
    {
        if ( ( configuration->uart_receive_enabled_mask
               & EXECUTION_MEASUREMENT_CHANNEL_BIT( channel ) )
             != 0U )
        {
            active_measurement_adapters[active_measurement_count++] =
                ( ExecutionMeasurementDispatchEntry_T ){
                    .adapter = EXECUTION_MEASUREMENT_ADAPTER_SampleUartReceive,
                    .type    = EXECUTION_MEASUREMENT_UART_RECEIVE,
                    .channel = channel,
                };
        }
    }

    for ( uint8_t channel = 0U; channel < EXEC_SPI_CHANNEL_COUNT; channel++ )
    {
        if ( ( configuration->spi_receive_enabled_mask
               & EXECUTION_MEASUREMENT_CHANNEL_BIT( channel ) )
             != 0U )
        {
            active_measurement_adapters[active_measurement_count++] =
                ( ExecutionMeasurementDispatchEntry_T ){
                    .adapter = EXECUTION_MEASUREMENT_ADAPTER_SampleSpiReceive,
                    .type    = EXECUTION_MEASUREMENT_SPI_RECEIVE,
                    .channel = channel,
                };
        }
    }

    for ( uint8_t channel = 0U; channel < EXEC_CAN_CHANNEL_COUNT; channel++ )
    {
        if ( ( configuration->can_receive_enabled_mask
               & EXECUTION_MEASUREMENT_CHANNEL_BIT( channel ) )
             != 0U )
        {
            active_measurement_adapters[active_measurement_count++] =
                ( ExecutionMeasurementDispatchEntry_T ){
                    .adapter = EXECUTION_MEASUREMENT_ADAPTER_SampleCanReceive,
                    .type    = EXECUTION_MEASUREMENT_CAN_RECEIVE,
                    .channel = channel,
                };
        }
    }
}

bool EXECUTION_MEASUREMENT_ADAPTER_SampleCanReceive( uint8_t channel, uint32_t timestamp,
                                                     BaseType_t* higher_priority_task_woken )
{
    const uint16_t reservation_bytes =
        ( uint16_t )( EXEC_CAN_MAX_BATCH_SIZE * sizeof( EXEC_CAN_Packet_T ) );
    FlashManagerResultWriteLease_T lease = { 0 };
    if ( !FLASH_MANAGER_ReserveResultRecordFromISR( reservation_bytes, &lease ) )
    {
        return false;
    }

    uint16_t                packets_read = 0U;
    const EXEC_CAN_Result_T result       = EXEC_CAN_Receive( ( EXEC_CAN_Channel_T )channel,
                                                             ( EXEC_CAN_Packet_T* )( void* )lease.payload,
                                                             EXEC_CAN_MAX_BATCH_SIZE, &packets_read );
    if ( result != EXEC_CAN_RESULT_OK )
    {
        ( void )FLASH_MANAGER_CancelResultRecordFromISR( &lease );
        return false;
    }

    if ( packets_read == 0U )
    {
        ( void )FLASH_MANAGER_CancelResultRecordFromISR( &lease );
        return true;
    }

    const uint16_t payload_length_bytes =
        ( uint16_t )( packets_read * sizeof( EXEC_CAN_Packet_T ) );
    if ( FLASH_MANAGER_CommitResultRecordFromISR(
             &lease, timestamp, FLASH_MANAGER_RESULT_PERIPHERAL_CAN_RECEIVE, channel,
             payload_length_bytes, higher_priority_task_woken )
         == FLASH_MANAGER_RESULT_COMMIT_OK )
    {
        return true;
    }

    ( void )FLASH_MANAGER_CancelResultRecordFromISR( &lease );
    return false;
}

bool EXECUTION_MEASUREMENT_ADAPTER_SampleSpiReceive( uint8_t channel, uint32_t timestamp,
                                                     BaseType_t* higher_priority_task_woken )
{
    const uint32_t pending_bytes = EXEC_SPI_GetPendingReceiveBytes( ( ExecSPIChannel_T )channel );
    if ( pending_bytes == 0U )
    {
        return true;
    }

    const uint16_t reservation_bytes =
        ( uint16_t )( pending_bytes < EXEC_SPI_MAX_RX_CHUNK_SIZE ? pending_bytes
                                                                 : EXEC_SPI_MAX_RX_CHUNK_SIZE );
    FlashManagerResultWriteLease_T lease = { 0 };
    if ( !FLASH_MANAGER_ReserveResultRecordFromISR( reservation_bytes, &lease ) )
    {
        return false;
    }

    uint32_t bytes_read = 0U;
    if ( !EXEC_SPI_Receive( ( ExecSPIChannel_T )channel, lease.payload, reservation_bytes,
                            &bytes_read ) )
    {
        ( void )FLASH_MANAGER_CancelResultRecordFromISR( &lease );
        return false;
    }

    if ( bytes_read == 0U )
    {
        ( void )FLASH_MANAGER_CancelResultRecordFromISR( &lease );
        return true;
    }

    if ( FLASH_MANAGER_CommitResultRecordFromISR(
             &lease, timestamp, FLASH_MANAGER_RESULT_PERIPHERAL_SPI_RECEIVE, channel,
             ( uint16_t )bytes_read, higher_priority_task_woken )
         == FLASH_MANAGER_RESULT_COMMIT_OK )
    {
        return true;
    }

    ( void )FLASH_MANAGER_CancelResultRecordFromISR( &lease );
    return false;
}

bool EXECUTION_MEASUREMENT_ADAPTER_SampleUartReceive( uint8_t channel, uint32_t timestamp,
                                                      BaseType_t* higher_priority_task_woken )
{
    FlashManagerResultWriteLease_T lease = { 0 };
    if ( !FLASH_MANAGER_ReserveResultRecordFromISR( EXEC_UART_MAX_CHUNK_SIZE, &lease ) )
    {
        return false;
    }

    uint32_t bytes_read = 0U;
    if ( !EXEC_UART_Read( ( ExecUartChannel_T )channel, lease.payload, EXEC_UART_MAX_CHUNK_SIZE,
                          &bytes_read ) )
    {
        ( void )FLASH_MANAGER_CancelResultRecordFromISR( &lease );
        return false;
    }

    if ( bytes_read == 0U )
    {
        ( void )FLASH_MANAGER_CancelResultRecordFromISR( &lease );
        return true;
    }

    if ( FLASH_MANAGER_CommitResultRecordFromISR(
             &lease, timestamp, FLASH_MANAGER_RESULT_PERIPHERAL_UART_RECEIVE, channel,
             ( uint16_t )bytes_read, higher_priority_task_woken )
         == FLASH_MANAGER_RESULT_COMMIT_OK )
    {
        return true;
    }

    ( void )FLASH_MANAGER_CancelResultRecordFromISR( &lease );
    return false;
}

bool EXECUTION_MEASUREMENT_ADAPTER_SampleAnalogueInput( uint8_t channel, uint32_t timestamp,
                                                        BaseType_t* higher_priority_task_woken )
{
    ( void )channel;
    FlashManagerResultWriteLease_T lease = { 0 };

    if ( !FLASH_MANAGER_ReserveResultRecordFromISR( 2U * sizeof( uint32_t ), &lease ) )
    {
        return false;
    }

    const ExecAnalogueInputVoltages_T voltages = {
        .channel_0_voltage = ( uint32_t* )( void* )lease.payload,
        .channel_1_voltage = ( uint32_t* )( void* )( lease.payload + sizeof( uint32_t ) ),
    };
    EXEC_ANALOGUE_INPUT_Read_Analogue_Inputs( voltages );

    if ( FLASH_MANAGER_CommitResultRecordFromISR(
             &lease, timestamp, FLASH_MANAGER_RESULT_PERIPHERAL_ANALOGUE_INPUT, 0U,
             2U * sizeof( uint32_t ), higher_priority_task_woken )
         == FLASH_MANAGER_RESULT_COMMIT_OK )
    {
        return true;
    }

    ( void )FLASH_MANAGER_CancelResultRecordFromISR( &lease );
    return false;
}

bool EXECUTION_MEASUREMENT_ADAPTER_ApplyMeasurements( uint32_t    timestamp,
                                                      BaseType_t* higher_priority_task_woken )
{
    for ( uint8_t index = 0U; index < active_measurement_count; index++ )
    {
        const ExecutionMeasurementDispatchEntry_T* entry = &active_measurement_adapters[index];
        if ( !entry->adapter( entry->channel, timestamp, higher_priority_task_woken ) )
        {
            return false;
        }
    }

    return true;
}

bool EXECUTION_MEASUREMENT_ADAPTER_ApplyMeasurementsProfiled(
    uint32_t timestamp, BaseType_t* higher_priority_task_woken )
{
#ifdef TEST_BUILD
    return EXECUTION_MEASUREMENT_ADAPTER_ApplyMeasurements( timestamp, higher_priority_task_woken );
#else
    for ( uint8_t index = 0U; index < active_measurement_count; index++ )
    {
        const ExecutionMeasurementDispatchEntry_T* entry = &active_measurement_adapters[index];
        const uint32_t                             start_cycles = DWT->CYCCNT;
        const bool                                 accepted =
            entry->adapter( entry->channel, timestamp, higher_priority_task_woken );
        const uint32_t                         elapsed_cycles = DWT->CYCCNT - start_cycles;
        volatile ExecutionMeasurementTiming_T* timing = &execution_measurement_timing[entry->type];

        timing->sample_count++;
        timing->total_cycles += elapsed_cycles;
        if ( elapsed_cycles > timing->maximum_cycles )
        {
            timing->maximum_cycles = elapsed_cycles;
        }

        if ( !accepted )
        {
            return false;
        }
    }

    return true;
#endif
}

void EXECUTION_MEASUREMENT_ADAPTER_ResetTiming( void )
{
    for ( uint32_t type = 0U; type < EXECUTION_MEASUREMENT_COUNT; type++ )
    {
        execution_measurement_timing[type].sample_count   = 0U;
        execution_measurement_timing[type].total_cycles   = 0U;
        execution_measurement_timing[type].maximum_cycles = 0U;
    }
}

bool EXECUTION_MEASUREMENT_ADAPTER_GetTiming( ExecutionMeasurementType_T    type,
                                              ExecutionMeasurementTiming_T* timing )
{
    if ( type >= EXECUTION_MEASUREMENT_COUNT || timing == NULL )
    {
        return false;
    }

    timing->sample_count   = execution_measurement_timing[type].sample_count;
    timing->total_cycles   = execution_measurement_timing[type].total_cycles;
    timing->maximum_cycles = execution_measurement_timing[type].maximum_cycles;
    return true;
}

bool EXECUTION_MEASUREMENT_ADAPTER_SampleDigitalInput( uint8_t channel, uint32_t timestamp,
                                                       BaseType_t* higher_priority_task_woken )
{
    ( void )channel;
    FlashManagerResultWriteLease_T lease = { 0 };

    if ( !FLASH_MANAGER_ReserveResultRecordFromISR( sizeof( uint32_t ), &lease ) )
    {
        return false;
    }

    EXEC_DIGITAL_INPUT_Sample_All( ( uint32_t* )( void* )lease.payload );

    if ( FLASH_MANAGER_CommitResultRecordFromISR( &lease, timestamp,
                                                  FLASH_MANAGER_RESULT_PERIPHERAL_DIGITAL_INPUT, 0U,
                                                  sizeof( uint32_t ), higher_priority_task_woken )
         == FLASH_MANAGER_RESULT_COMMIT_OK )
    {
        return true;
    }

    ( void )FLASH_MANAGER_CancelResultRecordFromISR( &lease );
    return false;
}

bool EXECUTION_MEASUREMENT_ADAPTER_SamplePwmCapture( uint8_t channel, uint32_t timestamp,
                                                     BaseType_t* higher_priority_task_woken )
{
    ExecPwmCaptureResult_T capture = { 0 };
    if ( !EXEC_PWM_Capture_Consume( ( ExecPwmCaptureChannel_T )channel, &capture ) )
    {
        return !capture.has_new_data;
    }

    FlashManagerResultWriteLease_T lease = { 0 };
    if ( !FLASH_MANAGER_ReserveResultRecordFromISR( 2U * sizeof( uint32_t ), &lease ) )
    {
        return false;
    }

    uint32_t* payload = ( uint32_t* )( void* )lease.payload;
    payload[0]        = capture.period_ticks;
    payload[1]        = capture.high_ticks;

    if ( FLASH_MANAGER_CommitResultRecordFromISR(
             &lease, timestamp, FLASH_MANAGER_RESULT_PERIPHERAL_PWM_CAPTURE, channel,
             2U * sizeof( uint32_t ), higher_priority_task_woken )
         == FLASH_MANAGER_RESULT_COMMIT_OK )
    {
        return true;
    }

    ( void )FLASH_MANAGER_CancelResultRecordFromISR( &lease );
    return false;
}
