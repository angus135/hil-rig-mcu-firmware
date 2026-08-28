/******************************************************************************
 *  File:       dut_driver_lifecycle.h
 *  Author:     Callum Rafferty
 *  Created:    26-Aug-2026
 *
 *  Description:
 *      Public interface for coordinated DUT-facing driver lifecycle control.
 *
 *  Notes:
 *      These functions compose the relevant execution-driver APIs for one test
 *      lifecycle. The Run State Manager is their only intended caller. This
 *      module owns no run state, RTOS task, execution timer, Flash Manager
 *      sequencing, or transport operation.
 ******************************************************************************/

#ifndef DUT_DRIVER_LIFECYCLE_H
#define DUT_DRIVER_LIFECYCLE_H

#ifdef __cplusplus
extern "C"
{
#endif

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include <stdbool.h>
#include "test_configuration.h"

/**-----------------------------------------------------------------------------
 *  Public Defines / Macros
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Public Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

typedef struct
{
    bool     configuration_valid;
    bool     analogue_input_enabled;
    bool     analogue_input_started;
    bool     analogue_output_enabled;
    bool     analogue_output_started;
    bool     digital_inputs_enabled;
    bool     digital_inputs_started;
    bool     digital_outputs_enabled;
    bool     digital_outputs_started;
    uint32_t can_enabled_mask;
    uint32_t can_started_mask;
    uint32_t pwm_capture_enabled_mask;
    uint32_t pwm_capture_started_mask;
    uint32_t pwm_generation_enabled_mask;
    uint32_t pwm_generation_started_mask;
    uint32_t spi_enabled_mask;
    uint32_t spi_started_mask;
    uint32_t uart_enabled_mask;
    uint32_t uart_started_mask;
} DutDriverLifecycleStatus_T;

/** Aggregate readiness of the most recently accepted driver configuration. */
typedef enum
{
    DUT_DRIVER_CONFIGURATION_PENDING = 0,
    DUT_DRIVER_CONFIGURATION_READY,
    DUT_DRIVER_CONFIGURATION_FAILED
} DutDriverConfigurationStatus_T;

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

/**
 * @brief Applies the active test configuration to all DUT-facing drivers.
 *
 * @param configuration Complete validated DUT driver configuration. Every
 *        channel is applied, including disabled channels, so stale state from
 *        a previous test cannot remain active.
 *
 * Implementations must leave drivers stopped. If configuration partially
 * succeeds, all affected drivers must be returned to a safe stopped condition
 * before failure is returned.
 *
 * External I2C channels are temporarily forced to a disabled zero
 * configuration because of the known I2C hardware fault. Requested I2C
 * settings are retained in the active test configuration but are not applied.
 *
 * @returns true if every required driver was configured, otherwise false.
 */
bool DUT_DRIVER_LIFECYCLE_Configure( const DutDriverConfiguration_T* configuration );

/**
 * @brief Polls completion of configuration work accepted by all DUT drivers.
 *
 * This function performs no waiting and must not reapply configuration.
 *
 * @return PENDING while an enabled driver is still configuring, READY when all
 *         enabled drivers may be started, or FAILED after a driver fault.
 */
DutDriverConfigurationStatus_T DUT_DRIVER_LIFECYCLE_GetConfigurationStatus( void );

/**
 * @brief Starts all configured DUT-facing drivers.
 *
 * Only channels enabled by the last successful configuration are started.
 * The lifecycle records each successful start so partial-start rollback and
 * normal stop affect only drivers that actually entered their started state.
 *
 * The Run State Manager starts the execution timer only after this succeeds.
 * If a driver fails to start, every driver already started by this call must be
 * stopped before failure is returned.
 *
 * @returns true if every required driver was started, otherwise false.
 */
bool DUT_DRIVER_LIFECYCLE_Start( void );

/**
 * @brief Stops all DUT-facing drivers after execution has stopped.
 *
 * This operation is idempotent and attempts every recorded started driver even
 * if an earlier stop reports failure. Drivers are stopped in reverse startup
 * order where practical.
 */
bool DUT_DRIVER_LIFECYCLE_Stop( void );

/** @brief Copies the configured enable plan and actual started bookkeeping. */
void DUT_DRIVER_LIFECYCLE_GetStatus( DutDriverLifecycleStatus_T* status );

/**
 * @brief Places DUT-facing drivers into their normal idle condition.
 *
 * This operation must be idempotent. DUT power remains enabled in idle.
 */
void DUT_DRIVER_LIFECYCLE_EnterIdle( void );

/**
 * @brief Places DUT-facing drivers and DUT power into their fault condition.
 *
 * This operation must be idempotent and best-effort: all safety actions must be
 * attempted even if an earlier action fails.
 */
void DUT_DRIVER_LIFECYCLE_EnterFault( void );

#ifdef __cplusplus
}
#endif

#endif /* DUT_DRIVER_LIFECYCLE_H */
