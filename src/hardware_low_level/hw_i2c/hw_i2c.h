/******************************************************************************
 *  File:       hw_i2c.h
 *  Author:     Callum Rafferty
 *  Created:    25-Mar-2024
 *
 *  Description:
 *      <Short description of the module, what it exposes, and how it should be used>
 *
 *  Notes:
 *      <Public assumptions, required initialisation order, dependencies, etc.>
 ******************************************************************************/

#ifndef HW_I2C_H
#define HW_I2C_H

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
// Add any needed standard or project-specific includes here

/**-----------------------------------------------------------------------------
 *  Public Defines / Macros
 *------------------------------------------------------------------------------
 */

#define HW_I2C_RX_BUFFER_SIZE    ( 512U )
#define HW_I2C_TX_STAGE_SIZE     ( 256U )

// #define MODULE_FEATURE_FLAG   (1U)
// Add macros intended for use outside this module here

/**-----------------------------------------------------------------------------
 *  Public Typedefs / Enums / Structures
 *------------------------------------------------------------------------------
 */

typedef enum HWI2CChannel_T
{
	HW_I2C_CHANNEL_1,
	HW_I2C_CHANNEL_2,
	HW_I2C_CHANNEL_FMPI2C1,

	HW_I2C_CHANNEL_COUNT,
} HWI2CChannel_T;

typedef enum HWI2CMode_T
{
	HW_I2C_MODE_MASTER,
	HW_I2C_MODE_SLAVE,
} HWI2CMode_T;

typedef enum HWI2CSpeed_T
{
	HW_I2C_SPEED_100KHZ,
	HW_I2C_SPEED_400KHZ,
} HWI2CSpeed_T;

typedef enum HWI2CTransferPath_T
{
	HW_I2C_TRANSFER_INTERRUPT,
	HW_I2C_TRANSFER_DMA,
} HWI2CTransferPath_T;

typedef enum HWI2CStatus_T
{
	HW_I2C_STATUS_OK,
	HW_I2C_STATUS_BUSY,
	HW_I2C_STATUS_ERROR,
	HW_I2C_STATUS_INVALID_PARAM,
	HW_I2C_STATUS_NOT_CONFIGURED,
	HW_I2C_STATUS_OVERFLOW,
} HWI2CStatus_T;

typedef struct HWI2CChannelConfig_T
{
	HWI2CMode_T mode;
	HWI2CSpeed_T speed;
	HWI2CTransferPath_T tx_transfer_path;
	HWI2CTransferPath_T rx_transfer_path;
	uint16_t own_address_7bit;
} HWI2CChannelConfig_T;

typedef struct HWI2CSpan_T
{
	const uint8_t* data;
	uint16_t length;
} HWI2CSpan_T;

typedef struct HWI2CRxPeek_T
{
	HWI2CSpan_T first;
	HWI2CSpan_T second;
	uint16_t total_length;
} HWI2CRxPeek_T;

// typedef enum { STATE_IDLE, STATE_BUSY } Module_State_T;
// typedef struct { uint16_t value; bool ready; } Module_Data_T;
// Add types that must be visible to other modules here

/**-----------------------------------------------------------------------------
 *  Public Function Prototypes
 *------------------------------------------------------------------------------
 */

HWI2CStatus_T HW_I2C_Configure_Channel( HWI2CChannel_T channel,
										const HWI2CChannelConfig_T* config );

HWI2CStatus_T HW_I2C_Configure_Internal_FMPI2C1( uint16_t own_address_7bit );

HWI2CStatus_T HW_I2C_Load_Stage_Buffer( HWI2CChannel_T channel, const uint8_t* data,
										uint16_t length );

HWI2CStatus_T HW_I2C_Trigger_Master_Transmit( HWI2CChannel_T channel, uint16_t device_address_7bit );
HWI2CStatus_T HW_I2C_Trigger_Master_Receive( HWI2CChannel_T channel, uint16_t device_address_7bit,
											 uint16_t expected_length );
HWI2CStatus_T HW_I2C_Trigger_Slave_Transmit( HWI2CChannel_T channel );
HWI2CStatus_T HW_I2C_Trigger_Slave_Receive( HWI2CChannel_T channel, uint16_t expected_length );

HWI2CStatus_T HW_I2C_Peek_Received( HWI2CChannel_T channel, HWI2CRxPeek_T* peek );
HWI2CStatus_T HW_I2C_Consume_Received( HWI2CChannel_T channel, uint16_t bytes_to_consume );

void HW_I2C_Service_Event_IRQ( HWI2CChannel_T channel );
void HW_I2C_Service_Error_IRQ( HWI2CChannel_T channel );
void HW_I2C_Service_DMA_Rx_IRQ( HWI2CChannel_T channel );
void HW_I2C_Service_DMA_Tx_IRQ( HWI2CChannel_T channel );

HWI2CStatus_T HW_I2C_Get_Last_Error( HWI2CChannel_T channel );

#ifdef __cplusplus
}
#endif

#endif /* HW_I2C_H */
