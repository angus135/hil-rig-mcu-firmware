/******************************************************************************
 *  File:       execution_manager.c
 *  Author:     Angus Corr
 *  Created:    20-Dec-2025
 *
 *  Description:
 *
 *  Notes:
 *     None
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */
#include "execution_manager.h"
#include "hw_timer.h"
#include "hw_gpio.h"
#include <stdint.h>
#include <stdbool.h>

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */
/*
 * Timer update frequency:
 *     update_hz = 90,000,000 / ((PSC + 1) * (ARR + 1))
 *
 * Values below are chosen to produce exact 100 Hz, 1 kHz, and 10 kHz update
 * rates while keeping PSC as small as possible for the best counter resolution.
 *
 * 100 Hz:
 *     90,000,000 / 100 = 900,000 timer counts per update.
 *     ARR cannot hold 899,999 on TIM4, so divide by PSC + 1 = 15.
 *     900,000 / 15 = 60,000 counts, so PSC = 14 and ARR = 59,999.
 *
 * 1 kHz:
 *     90,000,000 / 1,000 = 90,000 timer counts per update.
 *     ARR cannot hold 89,999 on TIM4, so divide by PSC + 1 = 2.
 *     90,000 / 2 = 45,000 counts, so PSC = 1 and ARR = 44,999.
 *
 * 10 kHz:
 *     90,000,000 / 10,000 = 9,000 timer counts per update.
 *     ARR can hold 8,999 directly, so PSC = 0 and ARR = 8,999.
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

void EXECUTION_MANAGER_Process_From_ISR( void )
{
    HW_GPIO_Toggle( GPIO_GREEN_LED_INDICATOR );
    // TODO: This is where all the I/O will actually run, this needs to be quick
}

void EXECUTION_MANAGER_Start( void )
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

void EXECUTION_MANAGER_Stop( void )
{
    HW_TIMER_Stop_Timer( EXECUTION_MANAGER_TIMER );
}

void EXECUTION_MANAGER_Set_Frequency_Mode( FrequencyMode_T mode )
{
    frequency_mode = mode;
}

FrequencyMode_T EXECUTION_MANAGER_Get_Frequency_Mode( void )
{
    return frequency_mode;
}

void EXECUTION_MANAGER_Init( void )
{
    EXECUTION_MANAGER_Start();
}
