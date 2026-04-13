/******************************************************************************
 *  File:       exec_i2c.h
 *  Author:     Coen Pasitchnyj
 *  Created:    14-Apr-2026
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
	EXEC_I2C_EXTERNAL_CHANNEL_1 = 0,
	EXEC_I2C_EXTERNAL_CHANNEL_2 = 1,
} ExecI2cExternalChannel_T;

typedef enum
{
	EXEC_I2C_MODE_MASTER = 0,
	EXEC_I2C_MODE_SLAVE  = 1,
} ExecI2cMode_T;

typedef enum
{
	EXEC_I2C_SPEED_100KHZ = 0,
	EXEC_I2C_SPEED_400KHZ = 1,
} ExecI2cSpeed_T;

typedef enum
{
	EXEC_I2C_TRANSFER_INTERRUPT = 0,
	EXEC_I2C_TRANSFER_DMA       = 1,
} ExecI2cTransferMode_T;

typedef struct
{
	ExecI2cMode_T         mode;
	ExecI2cSpeed_T        speed;
	ExecI2cTransferMode_T transfer_mode;
	uint8_t               own_address_7bit;
	bool                  rx_enabled;
	bool                  tx_enabled;
} ExecI2cChannelConfig_T;

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

bool EXEC_I2C_Configuration( const ExecI2cChannelConfig_T* channel_1_config,
							 const ExecI2cChannelConfig_T* channel_2_config );

bool EXEC_I2C_Rx_Start( ExecI2cExternalChannel_T channel, uint8_t target_address_7bit,
						uint16_t length_bytes );

uint16_t EXEC_I2C_Rx_Copy_And_Consume( ExecI2cExternalChannel_T channel, uint8_t* output,
									   uint16_t output_capacity_bytes );

bool EXEC_I2C_Send( ExecI2cExternalChannel_T channel, uint8_t target_address_7bit,
					const uint8_t* payload, uint16_t payload_length_bytes );

#ifdef __cplusplus
}
#endif

#endif /* EXEC_I2C_H */
