/******************************************************************************
 *  File:       test_configuration.h
 *  Author:     Callum Rafferty
 *  Created:    28-Aug-2026
 *
 *  Description:
 *      Defines the validated runtime configuration for DUT-facing execution
 *      drivers used by one HIL-RIG test.
 *
 *  Notes:
 *      This is the system-level source of truth for the in-memory DUT driver
 *      configuration. Each execution-driver header remains the source of truth
 *      for its own leaf configuration type and validation rules.
 *
 *      This structure is not a host wire format. The Host Interface must parse,
 *      validate, and translate a versioned test package before publishing this
 *      internal representation.
 ******************************************************************************/

#ifndef TEST_CONFIGURATION_H
#define TEST_CONFIGURATION_H

#ifdef __cplusplus
extern "C"
{
#endif

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include <stdbool.h>
#include "exec_analogue_input.h"
#include "exec_analogue_output.h"
#include "exec_can.h"
#include "exec_digital_input.h"
#include "exec_digital_output.h"
#include "exec_i2c.h"
#include "exec_pwm_capture.h"
#include "exec_pwm_gen.h"
#include "exec_spi.h"
#include "exec_uart.h"

/**-----------------------------------------------------------------------------
 *  Public Defines / Macros
 *------------------------------------------------------------------------------
 */

/** Number of general-purpose DUT-facing SPI channels. SPI_DAC is owned by the
 *  analogue-output driver and is intentionally excluded. */
#define TEST_CONFIGURATION_SPI_CHANNEL_COUNT ( 2U )

/** Number of DUT-facing PWM capture channels. */
#define TEST_CONFIGURATION_PWM_CAPTURE_CHANNEL_COUNT ( 2U )

/**-----------------------------------------------------------------------------
 *  Public Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

/**
 * @brief Complete validated configuration for all DUT-facing execution drivers.
 *
 * Subsystem-configured drivers retain their existing aggregate configuration.
 * Channel-configured drivers are represented by arrays indexed by their public
 * channel enums. Every entry must be populated, including disabled channels,
 * so applying a new test cannot retain stale configuration from a previous run.
 */
typedef struct DutDriverConfiguration_T
{
    ExecAnalogueInputConfig_T  analogue_input;
    ExecAnalogueOutputConfig_T analogue_output;

    EXEC_CAN_Config_T can_channels[EXEC_CAN_CHANNEL_COUNT];

    ExecDigitalInputConfig_T  digital_inputs;
    ExecDigitalOutputConfig_T digital_outputs;

    EXECI2CChannelConfig_T i2c_channels[EXEC_I2C_CHANNEL_COUNT];

    ExecPwmCaptureConfig_T
        pwm_capture_channels[TEST_CONFIGURATION_PWM_CAPTURE_CHANNEL_COUNT];
    ExecPwmGenConfig_T pwm_generation_channels[EXEC_PWM_GEN_CHANNEL_COUNT];

    ExecSPIConfig_T spi_channels[TEST_CONFIGURATION_SPI_CHANNEL_COUNT];

    ExecUartConfig_T uart_channels[EXEC_UART_CHANNEL_COUNT];
} DutDriverConfiguration_T;

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

/**
 * @brief Initialises the active configuration to a valid all-disabled setup.
 *
 * The inert configuration supports lifecycle hardware bring-up before Host
 * Interface package decoding is available.
 */
void TEST_CONFIGURATION_Init( void );

/**
 * @brief Publishes a completely validated DUT driver configuration.
 *
 * @param configuration Candidate configuration copied into module-owned storage.
 * @return true when the candidate was committed; false for a null pointer.
 *
 * @note Call only from task context while package reception owns configuration
 *       updates. A committed configuration is immutable for the active run.
 */
bool TEST_CONFIGURATION_Commit( const DutDriverConfiguration_T* configuration );

/**
 * @brief Copies the active configuration into caller-owned storage.
 *
 * @param configuration Destination for the complete active configuration.
 * @return true when an active configuration was copied; otherwise false.
 */
bool TEST_CONFIGURATION_GetActive( DutDriverConfiguration_T* configuration );

/** @brief Returns whether an active configuration is available. */
bool TEST_CONFIGURATION_IsActive( void );

/** @brief Invalidates and clears the active configuration. */
void TEST_CONFIGURATION_Clear( void );

#ifdef __cplusplus
}
#endif

#endif /* TEST_CONFIGURATION_H */
