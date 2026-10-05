/******************************************************************************
 *  File:       console_host_interface.h
 *  Author:     Callum Rafferty
 *  Created:    22-Sep-2026
 *
 *  Description:
 *      Console command interface for Host Interface live status queries.
 ******************************************************************************/

#ifndef CONSOLE_HOST_INTERFACE_H
#define CONSOLE_HOST_INTERFACE_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdint.h>

/**
 * @brief Console command handler for the Host Interface subsystem.
 *
 * Usage:
 *   host status - Print live Host Interface state, USB link, queue, and overflow status
 *
 * @param[in] argc Number of arguments.
 * @param[in] argv Argument array.
 */
void CONSOLE_HostInterface_Command( uint16_t argc, char* argv[] );

#ifdef __cplusplus
}
#endif

#endif /* CONSOLE_HOST_INTERFACE_H */
