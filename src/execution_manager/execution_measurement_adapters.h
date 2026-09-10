/******************************************************************************
 *  File:       execution_measurement_adapters.h
 *
 *  Description:
 *      Thin execution-time adapters for prevalidated measurement calls.
 ******************************************************************************/

#ifndef EXECUTION_MEASUREMENT_ADAPTERS_H
#define EXECUTION_MEASUREMENT_ADAPTERS_H

#ifdef __cplusplus
extern "C"
{
#endif

#include "rtos_config.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct
{
    bool digital_input_enabled;
} ExecutionMeasurementConfiguration_T;

/** Builds the ISR measurement dispatch list from validated task-context configuration. */
void EXECUTION_MEASUREMENT_ADAPTER_Prepare(
    const ExecutionMeasurementConfiguration_T* configuration );

/** Applies every measurement selected during preparation in fixed list order. */
bool EXECUTION_MEASUREMENT_ADAPTER_ApplyMeasurements(
    uint32_t timestamp, BaseType_t* higher_priority_task_woken );

/**
 * @brief Samples digital inputs and commits one result record.
 *
 * The caller contract guarantees that the digital-input driver is configured
 * and started and that Flash Manager is accepting ISR result records. No
 * lifecycle or payload validation is performed here.
 */
bool EXECUTION_MEASUREMENT_ADAPTER_SampleDigitalInput( uint32_t       timestamp,
                                                       BaseType_t* higher_priority_task_woken );

#ifdef __cplusplus
}
#endif

#endif /* EXECUTION_MEASUREMENT_ADAPTERS_H */
