/******************************************************************************
 *  File:       exec_i2c.c
 *  Author:     Coen Pasitchnyj
 *  Created:    14-Apr-2026
 *
 *  Description:
 *      <Short description of the module's purpose and responsibilities>
 *
 *  Notes:
 *      <Any design notes, dependencies, or assumptions go here>
 ******************************************************************************/

/**-----------------------------------------------------------------------------
 *  Includes
 *------------------------------------------------------------------------------
 */

#include "exec_i2c.h"
#include "hardware_low_level/hw_i2c/hw_i2c.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/**-----------------------------------------------------------------------------
 *  Defines / Macros
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Public (global) and Extern Variables
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Private (static) Variables
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Private (static) Function Prototypes
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Private Function Definitions
 *------------------------------------------------------------------------------
 */

/**-----------------------------------------------------------------------------
 *  Public Function Definitions
 *------------------------------------------------------------------------------
 */

static bool EXEC_I2C_Config_Is_Valid( const ExecI2cChannelConfig_T* config )
{
	if ( config == NULL )
	{
		return false;
	}

	if ( config->mode != EXEC_I2C_MODE_MASTER && config->mode != EXEC_I2C_MODE_SLAVE )
	{
		return false;
	}

	if ( config->speed != EXEC_I2C_SPEED_100KHZ && config->speed != EXEC_I2C_SPEED_400KHZ )
	{
		return false;
	}

	if ( config->transfer_mode != EXEC_I2C_TRANSFER_INTERRUPT
		 && config->transfer_mode != EXEC_I2C_TRANSFER_DMA )
	{
		return false;
	}

	if ( !config->rx_enabled && !config->tx_enabled )
	{
		return false;
	}

	return ( config->own_address_7bit <= 0x7FU );
}

static inline HwI2cChannel_T EXEC_I2C_Map_Channel( ExecI2cExternalChannel_T channel )
{
	return ( channel == EXEC_I2C_EXTERNAL_CHANNEL_1 ) ? HW_I2C_CHANNEL_1 : HW_I2C_CHANNEL_2;
}

static inline HwI2cConfig_T EXEC_I2C_Map_Config( const ExecI2cChannelConfig_T* config )
{
	HwI2cConfig_T mapped = {
		.role            = ( config->mode == EXEC_I2C_MODE_MASTER ) ? HW_I2C_ROLE_MASTER : HW_I2C_ROLE_SLAVE,
		.speed           = ( config->speed == EXEC_I2C_SPEED_100KHZ ) ? HW_I2C_SPEED_100KHZ : HW_I2C_SPEED_400KHZ,
		.transfer_mode   = ( config->transfer_mode == EXEC_I2C_TRANSFER_DMA ) ? HW_I2C_TRANSFER_DMA : HW_I2C_TRANSFER_INTERRUPT,
		.own_address_7bit = config->own_address_7bit,
		.rx_enabled      = config->rx_enabled,
		.tx_enabled      = config->tx_enabled,
	};

	return mapped;
}

bool EXEC_I2C_Configuration( const ExecI2cChannelConfig_T* channel_1_config,
							 const ExecI2cChannelConfig_T* channel_2_config )
{
	if ( !EXEC_I2C_Config_Is_Valid( channel_1_config ) || !EXEC_I2C_Config_Is_Valid( channel_2_config ) )
	{
		return false;
	}

	const HwI2cConfig_T channel_1_mapped = EXEC_I2C_Map_Config( channel_1_config );
	const HwI2cConfig_T channel_2_mapped = EXEC_I2C_Map_Config( channel_2_config );

	if ( !HW_I2C_Configure_Channel( HW_I2C_CHANNEL_1, &channel_1_mapped ) )
	{
		return false;
	}

	if ( !HW_I2C_Configure_Channel( HW_I2C_CHANNEL_2, &channel_2_mapped ) )
	{
		return false;
	}

	return HW_I2C_Configure_Internal_Channel();
}

bool EXEC_I2C_Rx_Start( ExecI2cExternalChannel_T channel, uint8_t target_address_7bit,
						uint16_t length_bytes )
{
	if ( channel != EXEC_I2C_EXTERNAL_CHANNEL_1 && channel != EXEC_I2C_EXTERNAL_CHANNEL_2 )
	{
		return false;
	}

	return HW_I2C_Rx_Start( EXEC_I2C_Map_Channel( channel ), target_address_7bit, length_bytes );
}

uint16_t EXEC_I2C_Rx_Copy_And_Consume( ExecI2cExternalChannel_T channel, uint8_t* output,
									   uint16_t output_capacity_bytes )
{
	if ( output == NULL || output_capacity_bytes == 0U )
	{
		return 0U;
	}

	if ( channel != EXEC_I2C_EXTERNAL_CHANNEL_1 && channel != EXEC_I2C_EXTERNAL_CHANNEL_2 )
	{
		return 0U;
	}

	const HwI2cChannel_T hw_channel = EXEC_I2C_Map_Channel( channel );
	const HwI2cRxSpans_T spans      = HW_I2C_Rx_Peek( hw_channel );

	uint16_t copied = 0U;

	const uint32_t first_copy = ( spans.first_span.length_bytes < output_capacity_bytes )
									? spans.first_span.length_bytes
									: output_capacity_bytes;
	if ( first_copy > 0U )
	{
		memcpy( output, spans.first_span.data, first_copy );
		copied = (uint16_t)first_copy;
	}

	const uint32_t remaining_capacity = (uint32_t)output_capacity_bytes - copied;
	const uint32_t second_copy = ( spans.second_span.length_bytes < remaining_capacity )
									 ? spans.second_span.length_bytes
									 : remaining_capacity;
	if ( second_copy > 0U )
	{
		memcpy( &output[copied], spans.second_span.data, second_copy );
		copied = (uint16_t)( copied + second_copy );
	}

	HW_I2C_Rx_Consume( hw_channel, copied );

	return copied;
}

bool EXEC_I2C_Send( ExecI2cExternalChannel_T channel, uint8_t target_address_7bit,
					const uint8_t* payload, uint16_t payload_length_bytes )
{
	if ( channel != EXEC_I2C_EXTERNAL_CHANNEL_1 && channel != EXEC_I2C_EXTERNAL_CHANNEL_2 )
	{
		return false;
	}

	const HwI2cChannel_T hw_channel = EXEC_I2C_Map_Channel( channel );

	if ( !HW_I2C_Tx_Load_Buffer( hw_channel, target_address_7bit, payload, payload_length_bytes ) )
	{
		return false;
	}

	return HW_I2C_Tx_Trigger( hw_channel );
}
