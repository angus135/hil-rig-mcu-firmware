/******************************************************************************
 *  File:      console_I2C.h
 *  Author:    Coen Pasitchnyj
*  Created:    3-May-2026
 *
 *  Description:
 *      <Short description of the module, what it exposes, and how it should be used>
 *
 *  Notes:
 *      <Public assumptions, required initialisation order, dependencies, etc.>
 ******************************************************************************/

#ifndef CONSOLE_I2C_H
#define CONSOLE_I2C_H

#ifdef __cplusplus
extern "C"
{
#endif

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include <stdint.h>
#include <stdbool.h>
#include "exec_i2c.h"

/**-----------------------------------------------------------------------------
 *  Public Defines / Macros
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Public Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

typedef enum
{
    CONSOLE_I2C_LOOPBACK_DIR_M2S,
    CONSOLE_I2C_LOOPBACK_DIR_S2M,
} ConsoleI2CLoopbackDirection_T;

typedef struct
{
    HWI2CChannel_T master;
    HWI2CChannel_T slave;
} CONSOLEI2CLoopbackChannels_T;

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

bool CONSOLE_Parse_I2C_Master_And_Slave( const char*               arg,
                                         HWI2CChannel_T* master_channel,
                                         HWI2CChannel_T* slave_channel );
bool CONSOLE_Parse_I2C_Loopback_Direction( const char*                    arg,
                                                  ConsoleI2CLoopbackDirection_T* direction );
bool CONSOLE_Parse_I2C_Speed( const char* arg, EXECI2CSpeed_T* speed );
bool CONSOLE_Parse_I2C_Transfer_Path( const char*            arg,
                                             EXECI2CTransferPath_T* transfer_path );
bool CONSOLE_Build_I2C_Message( uint16_t argc, char* argv[], char* out_message,
                                uint16_t out_message_size, uint16_t* out_message_length );
bool CONSOLE_Run_I2C_Loopback_M2S( CONSOLEI2CLoopbackChannels_T channels,
                                          uint16_t slave_addr, const char* tx_message,
                                          uint16_t tx_len, char* rx_message,
                                          uint16_t rx_message_size, uint16_t* out_received_len );
bool CONSOLE_Run_I2C_Loopback_S2M( CONSOLEI2CLoopbackChannels_T channels,
                                          uint16_t slave_addr, const char* tx_message,
                                          uint16_t tx_len, char* rx_message,
                                          uint16_t rx_message_size, uint16_t* out_received_len );

#ifdef __cplusplus
}
#endif

#endif /* CONSOLE_I2C_H */
