/******************************************************************************
 *  File:       example_hal_gpio.h
 *  Author:     Angus Corr
 *  Created:    6-Dec-2025
 *
 *  Description:
 *      Minimal GPIO EXAMPLE_HAL interface used by the Example module.
 *
 *  Notes:
 *      - In the production build, this function will be implemented by the
 *        platform EXAMPLE_HAL layer.
 *      - In unit tests, this symbol is overridden by a GoogleMock-based
 *        implementation in C++.
 ******************************************************************************/

#ifndef EXAMPLE_HAL_GPIO_H
#define EXAMPLE_HAL_GPIO_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdint.h>
#include <stdbool.h>

    /**
     * @brief Drives a digital output.
     *
     * @param pin    Logical pin identifier.
     * @param level  Output level (true = high, false = low).
     */
    void EXAMPLE_HAL_GPIO_WritePin(uint32_t pin, bool level);

#ifdef __cplusplus
}
#endif

#endif /* EXAMPLE_HAL_GPIO_H */
