/******************************************************************************
 *  File:       execution_measurement_adapters.c
 *
 *  Description:
 *      Thin execution-time adapters for prevalidated measurement calls.
 ******************************************************************************/

#include "execution_measurement_adapters.h"

#include "exec_digital_input.h"
#include "flash_manager.h"

#include <stdint.h>

typedef bool ( *ExecutionMeasurementAdapter_T )( uint32_t timestamp,
                                                  BaseType_t* higher_priority_task_woken );

#define EXECUTION_MEASUREMENT_ADAPTER_COUNT ( 1U )

static ExecutionMeasurementAdapter_T
    active_measurement_adapters[EXECUTION_MEASUREMENT_ADAPTER_COUNT] = { 0 };
static uint8_t active_measurement_count = 0U;

void EXECUTION_MEASUREMENT_ADAPTER_Prepare(
    const ExecutionMeasurementConfiguration_T* configuration )
{
    active_measurement_count = 0U;

    if ( configuration->digital_input_enabled )
    {
        active_measurement_adapters[active_measurement_count++] =
            EXECUTION_MEASUREMENT_ADAPTER_SampleDigitalInput;
    }
}

bool EXECUTION_MEASUREMENT_ADAPTER_ApplyMeasurements(
    uint32_t timestamp, BaseType_t* higher_priority_task_woken )
{
    for ( uint8_t index = 0U; index < active_measurement_count; index++ )
    {
        if ( !active_measurement_adapters[index]( timestamp, higher_priority_task_woken ) )
        {
            return false;
        }
    }

    return true;
}

bool EXECUTION_MEASUREMENT_ADAPTER_SampleDigitalInput(
    uint32_t timestamp, BaseType_t* higher_priority_task_woken )
{
    FlashManagerResultWriteLease_T lease = { 0 };

    if ( !FLASH_MANAGER_ReserveResultRecordFromISR( sizeof( uint32_t ), &lease ) )
    {
        return false;
    }

    EXEC_DIGITAL_INPUT_Sample_All( ( uint32_t* )( void* )lease.payload );

    if ( FLASH_MANAGER_CommitResultRecordFromISR(
             &lease, timestamp, FLASH_MANAGER_RESULT_PERIPHERAL_DIGITAL_INPUT, 0U,
             sizeof( uint32_t ), higher_priority_task_woken )
         == FLASH_MANAGER_RESULT_COMMIT_OK )
    {
        return true;
    }

    ( void )FLASH_MANAGER_CancelResultRecordFromISR( &lease );
    return false;
}
