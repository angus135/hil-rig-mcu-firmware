/******************************************************************************
 *  File:       app_main.h
 *  Author:     Angus Corr
 *  Created:    6-12-2025
 *
 *  Description:
 *      Call for the MCU application.
 *
 *  Notes:
 *      None.
 ******************************************************************************/

#ifndef APP_MAIN_H
#define APP_MAIN_H

#ifdef __cplusplus
extern "C"
{
#endif

    /*------------------------------------------------------------------------------
     *  Includes
     *----------------------------------------------------------------------------*/

    /*------------------------------------------------------------------------------
     *  Public Defines / Macros
     *----------------------------------------------------------------------------*/

    /*------------------------------------------------------------------------------
     *  Public Typedefs / Enums / Structures
     *----------------------------------------------------------------------------*/

    /*------------------------------------------------------------------------------
     *  Public Function Prototypes
     *----------------------------------------------------------------------------*/

    /**
     * @brief MCU application
     *
     * @param             void
     * @return            void
     *
     * This function starts the MCU application after all HAL layers have been initialised
     */
    void APP_MAIN_Application(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_MAIN_H */