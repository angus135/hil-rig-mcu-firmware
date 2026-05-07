/******************************************************************************
 *  File:       exec_i2c.h
 *  Author:     Coen Pasitchnyj
 *  Created:    20-Apr-2026
 *
 *  Description:
 *      <Short description of the module, what it exposes, and how it should be used>
 *
 *  Notes:
 *      <Public assumptions, required initialisation order, dependencies, etc.>
 ******************************************************************************/

#ifndef EXEC_I2C_H
#define EXEC_I2C_H

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
#include "hw_i2c.h"

/**-----------------------------------------------------------------------------
 *  Public Defines / Macros
 *------------------------------------------------------------------------------
 */

#define EXEC_I2C_EXTERNAL_CHANNEL_COUNT ( 2U )

/**-----------------------------------------------------------------------------
 *  Public Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

typedef enum EXECI2CStatus_T
{
    EXEC_I2C_STATUS_OK,
    EXEC_I2C_STATUS_BUSY,
    EXEC_I2C_STATUS_ERROR,
    EXEC_I2C_STATUS_INVALID_PARAM,
    EXEC_I2C_STATUS_OVERFLOW,
} EXECI2CStatus_T;

typedef struct EXECI2CChannelConfig_T
{
    HWI2CMode_T         mode;
    HWI2CSpeed_T        speed;
    HWI2CTransferPath_T tx_transfer_path;
    HWI2CTransferPath_T rx_transfer_path;
    uint16_t            own_address_7bit;
} EXECI2CChannelConfig_T;

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

EXECI2CStatus_T EXEC_I2C_Configuration( const EXECI2CChannelConfig_T* i2c1_config,
                                        const EXECI2CChannelConfig_T* i2c2_config,
                                        uint16_t internal_fmpi2c1_own_address_7bit );

EXECI2CStatus_T EXEC_I2C_Configuration_Internal( void );

bool EXEC_I2C_Master_Send( HWI2CChannel_T channel, uint16_t device_address_7bit,
                           const uint8_t* payload, uint16_t payload_length );
bool EXEC_I2C_Internal_Master_Send( uint16_t device_address_7bit, const uint8_t* payload,
                                    uint16_t payload_length );
bool EXEC_I2C_Slave_Send( HWI2CChannel_T channel, const uint8_t* payload,
                          uint16_t payload_length );

bool EXEC_I2C_Start_Master_Receive( HWI2CChannel_T channel, uint16_t device_address_7bit,
                                    uint16_t expected_length );
bool EXEC_I2C_Start_Slave_Receive( HWI2CChannel_T channel, uint16_t expected_length );

bool EXEC_I2C_Receive_Copy_And_Consume( HWI2CChannel_T channel, uint8_t* result_storage,
                                        uint16_t result_storage_capacity,
                                        uint16_t* bytes_copied );

#ifdef __cplusplus
}
#endif

#endif /* EXEC_I2C_H */
