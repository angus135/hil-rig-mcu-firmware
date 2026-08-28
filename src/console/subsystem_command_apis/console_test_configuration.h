/******************************************************************************
 *  File:       console_test_configuration.h
 *  Description:
 *      Hardware-bring-up controls for the committed DUT test configuration.
 ******************************************************************************/

#ifndef CONSOLE_TEST_CONFIGURATION_H
#define CONSOLE_TEST_CONFIGURATION_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdint.h>

void CONSOLE_TestConfiguration_Command( uint16_t argc, char* argv[] );

#ifdef __cplusplus
}
#endif

#endif /* CONSOLE_TEST_CONFIGURATION_H */
