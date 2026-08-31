#ifndef HW_TIMER_CAPTURE_START_H
#define HW_TIMER_CAPTURE_START_H

#include <stdbool.h>
#include <stdint.h>

/* Private helper. Include HAL timer declarations (or test doubles) first.
 * Both timer roles use this sequence, with their own primary/secondary mapping.
 */
static inline bool HW_TIMER_Start_Capture_Pair( TIM_HandleTypeDef* handle, uint32_t primary,
                                                uint32_t secondary )
{
    if ( HAL_TIM_IC_Start( handle, primary ) != HAL_OK )
    {
        return false;
    }

    if ( HAL_TIM_IC_Start( handle, secondary ) != HAL_OK )
    {
        /* Undo the successful first start. Failure still propagates even if
         * cleanup fails; this operation must never be reported as started.
         */
        ( void )HAL_TIM_IC_Stop( handle, primary );
        return false;
    }

    return true;
}

#endif /* HW_TIMER_CAPTURE_START_H */
