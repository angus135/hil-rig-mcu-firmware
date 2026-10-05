/******************************************************************************
 *  File:       hw_clock_calibration.h
 *  Author:     Callum Rafferty
 *  Created:    01-Oct-2026
 *
 *  Description:
 *      Master system clock calibration interface.
 *
 *      Provides a single unified calibration function that scales nominal
 *      peripheral and timer clock frequencies by a master calibration offset.
 *
 *  Notes:
 *      Offset is measured against an external clock reference.
 ******************************************************************************/

#ifndef HW_CLOCK_CALIBRATION_H
#define HW_CLOCK_CALIBRATION_H

#ifdef __cplusplus
extern "C"
{
#endif

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include <stdint.h>

/**-----------------------------------------------------------------------------
 *  Public Defines / Macros
 *------------------------------------------------------------------------------
 */

/**
 * @brief Master system clock calibration offset in parts-per-million (PPM).
 *
 * A positive value means the physical clock is running faster than nominal.
 * A negative value means the physical clock is running slower than nominal.
 * Set to 0 for nominal / uncalibrated (identity function).
 */
#define HW_CLOCK_CALIBRATION_OFFSET_PPM ( -1600 )

/** Nominal base clock frequencies (Hz) for STM32F446 */
#define HW_CLOCK_NOMINAL_SYSCLK_HZ ( 180000000U )
#define HW_CLOCK_NOMINAL_APB1_HZ ( 45000000U )
#define HW_CLOCK_NOMINAL_APB2_HZ ( 90000000U )
#define HW_CLOCK_NOMINAL_TIM_APB1_HZ ( 90000000U )
#define HW_CLOCK_NOMINAL_TIM_APB2_HZ ( 180000000U )

/**-----------------------------------------------------------------------------
 *  Public Inline Functions
 *------------------------------------------------------------------------------
 */

/**
 * @brief Master clock calibration scaling function.
 *
 * Scales any nominal frequency by the master calibration offset factor.
 * Currently returns nominal_hz directly when offset is 0 (identity).
 *
 * @param nominal_hz Nominal clock frequency in Hertz.
 * @return Calibrated clock frequency in Hertz.
 */
static inline uint32_t HW_CLOCK_Calibrate_Hz( uint32_t nominal_hz )
{
#if ( HW_CLOCK_CALIBRATION_OFFSET_PPM == 0 )
    return nominal_hz;
#else
    return ( uint32_t )( ( ( uint64_t )nominal_hz
                           * ( 1000000ULL + ( int64_t )HW_CLOCK_CALIBRATION_OFFSET_PPM ) )
                         / 1000000ULL );
#endif
}

/**
 * @brief Returns the calibrated APB1 timer clock frequency (TIM2, TIM3, TIM4, TIM5, TIM12).
 */
static inline uint32_t HW_CLOCK_Get_Timer_APB1_Hz( void )
{
    return HW_CLOCK_Calibrate_Hz( HW_CLOCK_NOMINAL_TIM_APB1_HZ );
}

/**
 * @brief Returns the calibrated APB2 timer clock frequency (TIM1, TIM8, TIM11).
 */
static inline uint32_t HW_CLOCK_Get_Timer_APB2_Hz( void )
{
    return HW_CLOCK_Calibrate_Hz( HW_CLOCK_NOMINAL_TIM_APB2_HZ );
}

/**
 * @brief Returns the calibrated APB1 peripheral bus clock (CAN, UART2/3, I2C, SPI2).
 */
static inline uint32_t HW_CLOCK_Get_PCLK1_Hz( void )
{
    return HW_CLOCK_Calibrate_Hz( HW_CLOCK_NOMINAL_APB1_HZ );
}

/**
 * @brief Returns the calibrated APB2 peripheral bus clock (USART6, SPI1/4).
 */
static inline uint32_t HW_CLOCK_Get_PCLK2_Hz( void )
{
    return HW_CLOCK_Calibrate_Hz( HW_CLOCK_NOMINAL_APB2_HZ );
}

/**
 * @brief Returns the calibrated system core clock (SYSCLK / HCLK).
 */
static inline uint32_t HW_CLOCK_Get_SysClock_Hz( void )
{
    return HW_CLOCK_Calibrate_Hz( HW_CLOCK_NOMINAL_SYSCLK_HZ );
}

#ifdef __cplusplus
}
#endif

#endif /* HW_CLOCK_CALIBRATION_H */
