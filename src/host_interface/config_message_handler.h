/******************************************************************************
 *  File:       config_message_handler.c
 *  Author:     Tim Vogelsang
 *  Created:    19-Sep-2026
 *
 *  Description:
 *
 *  Notes:
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include "host_process_message.h"
#include "test_configuration.h"

#include <stdbool.h>

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */

/** @brief Timer input clock frequency for Low Voltage PWM generation (TIM12 on APB1). */
#define HOST_INSTRUCTION_PWM_LV_TIMER_CLOCK_HZ ( 90000000U )

/** @brief Timer input clock frequency for High Voltage PWM generation (TIM8 on APB2). */
#define HOST_INSTRUCTION_PWM_HV_TIMER_CLOCK_HZ ( 180000000U )

/** @brief Number of nanoseconds in one second, used for period-to-frequency conversion. */
#define HOST_INSTRUCTION_NANOSECONDS_PER_SECOND ( 1000000000U )

/** @brief Scaling factor to convert application microvolt values to Volts. */
#define HOST_INSTRUCTION_MICROVOLTS_PER_VOLT ( 1000000.0f )

/**-----------------------------------------------------------------------------
 *  Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */


/**-----------------------------------------------------------------------------
 *  Public (global) and Extern Variables
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Private (static) Variables
 *------------------------------------------------------------------------------
 */


/**-----------------------------------------------------------------------------
 *  Private (static) Function Prototypes
 *------------------------------------------------------------------------------
 */



/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */

/**
 * @brief Converts an application config message to a driver config struct
 *
 */
HOST_Interface_Status_T
HOST_INTERFACE_Config_Message_To_Driver( const HIL_Application_Message_T* config_message,
                                         DutDriverConfiguration_T*        driver_config );

/**
 * @brief Commits a driver config to the configuration manager
 *
 *
 * Handles all of the required manager task/notifications associated with a driver config struct
 *
 */
HOST_Interface_Status_T
HOST_INTERFACE_Commit_Config_Message( const DutDriverConfiguration_T* driver_config );
