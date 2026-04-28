/******************************************************************************
 *  File:       hw_pwm_capture_mocks.h
 *  Author:     Callum Rafferty
 *  Created:    25-Mar-2026
 *
 *  Description:
 *      Mock definitions of HAL types and functions for unit testing hw_pwm_capture module.
 *
 *  Notes:
 *
 ******************************************************************************/

#ifndef HW_PWM_CAPTURE_MOCKS_H
#define HW_PWM_CAPTURE_MOCKS_H

#ifdef __cplusplus
extern "C"
{
#endif
// NOLINTBEGIN

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include <stdint.h>
#include <stdbool.h>

/**-----------------------------------------------------------------------------
 *  Public Defines / Macros
 *------------------------------------------------------------------------------
 */

#define TIM2 ( &mock_tim2 )
#define TIM5 ( &mock_tim5 )

/**-----------------------------------------------------------------------------
 *  Public Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

typedef struct
{
    volatile uint32_t CCR1;
    volatile uint32_t CCR2;
} TIM_TypeDef;

extern TIM_TypeDef mock_tim2;
extern TIM_TypeDef mock_tim5;

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

// NOLINTEND

#ifdef __cplusplus
}
#endif

#endif /* HW_PWM_CAPTURE_MOCKS_H */
