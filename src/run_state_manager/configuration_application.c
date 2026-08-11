/******************************************************************************
 *  File:       configuration_application.c
 *  Author:     Angus Corr
 *  Created:    25-Mar-2026
 *
 *  Description:
 *      Legacy timer-configuration placeholder pending consolidation into the
 *      Execution Manager and Run State Manager lifecycle.
 *
 *  Notes:
 *      This module must not become a second execution path. It is retained
 *      temporarily and should be removed when lifecycle integration is built.
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */
#include "configuration_application.h"
#include "hw_timer.h"
#include "hw_gpio.h"
#include <stdint.h>
#include <stdbool.h>

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */
#define PSC_100HZ 14u
#define ARR_100HZ 59999u

#define PSC_1KHZ 1u
#define ARR_1KHZ 44999u

#define PSC_10KHZ 0u
#define ARR_10KHZ 8999u

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
static FrequencyMode_T frequency_mode = FREQUENCY_10KHZ;

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

void CONFIGURATION_APPLICATION_Process_From_ISR( void )
{
    HW_GPIO_Toggle_Output( USER_LED_RED_0 );
    /*
     * TODO: Remove this placeholder. Peripheral I/O execution belongs in
     * EXECUTION_MANAGER_Process_From_ISR(), not configuration application code.
     */
}

void CONFIGURATION_APPLICATION_Start( void )
{
    switch ( frequency_mode )
    {
        case FREQUENCY_100HZ:
            HW_TIMER_Configure_Timer( EXECUTION_MANAGER_TIMER, PSC_100HZ, ARR_100HZ );
            break;
        case FREQUENCY_1KHZ:
            HW_TIMER_Configure_Timer( EXECUTION_MANAGER_TIMER, PSC_1KHZ, ARR_1KHZ );
            break;
        case FREQUENCY_10KHZ:
            HW_TIMER_Configure_Timer( EXECUTION_MANAGER_TIMER, PSC_10KHZ, ARR_10KHZ );
            break;
        default:
            break;
    }
    HW_TIMER_Start_Timer( EXECUTION_MANAGER_TIMER );
}

void CONFIGURATION_APPLICATION_Stop( void )
{
    HW_TIMER_Stop_Timer( EXECUTION_MANAGER_TIMER );
}

void CONFIGURATION_APPLICATION_Set_Frequency_Mode( FrequencyMode_T mode )
{
    frequency_mode = mode;
}

FrequencyMode_T CONFIGURATION_APPLICATION_Get_Frequency_Mode( void )
{
    return frequency_mode;
}

void CONFIGURATION_APPLICATION_Init( void )
{
    CONFIGURATION_APPLICATION_Start();
}
